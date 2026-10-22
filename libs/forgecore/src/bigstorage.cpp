#include "forge/bigstorage.hpp"
#include "forge/big.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <stdexcept>

namespace forge::bigstorage {
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(std::string("bigstorage: ") + message);
}
struct Reader {
    std::span<const uint8_t> bytes;
    size_t at = 0;
    uint32_t word() {
        require(at <= bytes.size() && bytes.size()-at >= 4, "truncated metadata word");
        const auto* p = bytes.data()+at; at += 4;
        return uint32_t(p[0]) | (uint32_t(p[1])<<8) | (uint32_t(p[2])<<16) | (uint32_t(p[3])<<24);
    }
    std::string name() {
        const size_t start = at;
        while (at < bytes.size() && bytes[at]) ++at;
        require(at < bytes.size() && at > start, "invalid bank name");
        std::string value(bytes.begin()+start, bytes.begin()+at); ++at;
        return value;
    }
};
struct BankSpan {
    std::string name;
    uint32_t version, count, offset, length, alignment;
};
bool text(const std::string& value) {
    return value.find('\0') == std::string::npos;
}
}

Storage validateEnvelope(const std::filesystem::path& path) {
    // OpenRetailBank 02FD0530 and FinishRetailBanks 02FD1A70: BIGB header,
    // explicit footer byte length, named bank directory and per-bank TOCs.
    // Entry metadata is the CBankFile storage reviewed alongside WAD/STB.
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    require(bool(input), "cannot open archive");
    const auto end = input.tellg(); require(end >= 20, "truncated archive");
    const uint64_t size = uint64_t(end);
    auto read = [&](uint64_t offset, size_t count) {
        require(offset <= size && count <= size-offset, "metadata outside archive");
        std::vector<uint8_t> bytes(count);
        input.seekg(std::streamoff(offset));
        if (count) input.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(count));
        require(bool(input), "metadata read failed"); return bytes;
    };
    const auto header = read(0,16); Reader h{header};
    require(h.word() == 0x42474942u && h.word() == 100, "unsupported BIGB header");
    const uint64_t directory = h.word(), directorySize = h.word();
    require(directory >= 16 && directory <= size && directorySize == size-directory &&
            directorySize <= 64000000, "invalid directory extent");
    const auto footer = read(directory, size_t(directorySize)); Reader d{footer};
    const auto count = d.word();
    require(count && count <= (footer.size()-4)/22, "invalid bank count");
    const std::map<std::string,uint32_t> versions = {
        {"GBANK_MAIN_PC",122}, {"GBANK_GUI_PC",122}, {"GBANK_FRONT_END_PC",122},
        {"MBANK_ALLMESHES",205}, {"MBANK_ENGINE",205}, {"TEXT_ENGLISH_MAIN",106},
        // CParticleDataBank::GetDataFormatVersion 02F02210; common CBankFile envelope.
        {"PARTICLE_MAIN_PC",111}};
    std::vector<BankSpan> spans; std::set<std::string> bankNames;
    uint64_t firstTable = directory, previousEnd = 0;
    for (uint32_t i=0; i<count; ++i) {
        BankSpan bank{d.name(), d.word(), d.word(), d.word(), d.word(), d.word()};
        const auto version = versions.find(bank.name);
        require(version != versions.end() && version->second == bank.version, "unsupported bank/version");
        require(bankNames.insert(bank.name).second, "duplicate bank name");
        require(bank.alignment && !(bank.alignment & (bank.alignment-1)), "unsupported bank alignment");
        require(bank.offset >= 16 && bank.offset <= directory && bank.length >= 4 &&
                bank.length <= directory-bank.offset, "invalid entry table extent");
        if (i == 0) firstTable = bank.offset;
        else require(bank.offset == previousEnd, "entry tables are not contiguous in directory order");
        previousEnd = uint64_t(bank.offset)+bank.length;
        spans.push_back(std::move(bank));
    }
    require(d.at == footer.size() && previousEnd == directory, "unowned directory/table bytes");
    require(size-firstTable <= 64000000, "metadata exceeds diagnostic limit (64 MB)");
    // The checks above bound the canonical reader's complete directory window.
    const auto file = big::File::open(path);
    require(file.banks().size() == spans.size(), "bank parser disagreement");
    Storage result; result.banks = spans.size();
    std::vector<std::pair<uint64_t,uint64_t>> payloads;
    for (size_t i=0; i<spans.size(); ++i) {
        const auto& span = spans[i]; const auto& bank = file.banks()[i];
        require(bank.name == span.name && bank.id == span.version && bank.blockSize == span.alignment &&
                bank.entries.size() == span.count, "bank metadata disagreement");
        const auto table = read(span.offset,span.length); Reader t{table};
        const auto types = t.word();
        require(types <= (table.size()-4)/8, "histogram exceeds table");
        std::map<uint32_t,uint32_t> histogram, actual;
        for (uint32_t j=0; j<types; ++j) {
            const auto type = t.word(), number = t.word();
            require(number && histogram.emplace(type,number).second, "invalid type histogram");
        }
        uint64_t accounted = t.at;
        std::set<uint32_t> ids; std::set<std::string> names;
        for (const auto& entry : bank.entries) {
            // ReadEntryList 02F9A180 reports a bad resource file for !=42.
            // Some community additions use zero; retain the refusal explicitly.
            if (entry.magic != 42)
                throw std::runtime_error("bigstorage: unsupported entry marker " + std::to_string(entry.magic) +
                    " in " + bank.name + "/" + entry.name + " (ID " + std::to_string(entry.id) + ")");
            require(entry.id && ids.insert(entry.id).second,
                    "invalid or duplicate entry ID");
            require(!entry.name.empty() && text(entry.name) && names.insert(entry.name).second,
                    "invalid or duplicate entry name");
            accounted += 40ull + entry.name.size() + entry.subHeader.size();
            for (const auto& source : entry.devSources) {
                require(text(source), "embedded NUL in entry source");
                accounted += 4ull + source.size();
            }
            ++actual[entry.type];
            require(entry.dataOffset >= 16 && entry.dataOffset <= firstTable &&
                    entry.dataOffset % span.alignment == 0 &&
                    entry.length <= firstTable-entry.dataOffset, "invalid payload span or alignment");
            if (entry.length) payloads.emplace_back(entry.dataOffset, uint64_t(entry.dataOffset)+entry.length);
            result.payloadBytes += entry.length; ++result.entries;
        }
        require(accounted == span.length && histogram == actual, "entry table or histogram mismatch");
    }
    std::sort(payloads.begin(),payloads.end());
    uint64_t cursor = 16;
    auto gap = [&](uint64_t stop) {
        require(cursor <= stop, "overlapping payloads");
        result.zeroPaddingBytes += stop-cursor;
        input.seekg(std::streamoff(cursor)); std::array<char,65536> bytes;
        while (cursor < stop) {
            const size_t amount = size_t(std::min<uint64_t>(bytes.size(), stop-cursor));
            input.read(bytes.data(),std::streamsize(amount)); require(bool(input), "padding read failed");
            require(std::all_of(bytes.begin(),bytes.begin()+amount,[](char c){return c==0;}), "nonzero unowned bytes");
            cursor += amount;
        }
    };
    for (const auto& [start,stop] : payloads) { gap(start); cursor = stop; }
    gap(firstTable);
    return result;
}
}
