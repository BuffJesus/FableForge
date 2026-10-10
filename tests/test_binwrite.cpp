#include "forge/bin.hpp"
#include "forge/temporarydirectory.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>

using Bytes = std::vector<uint8_t>;
static void word(Bytes& bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(uint8_t(value >> (8 * i)));
}
static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void write(const std::filesystem::path& path, const Bytes& bytes) {
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    check(bool(stream), "fixture write failed");
}
static Bytes read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "output read failed");
    return Bytes(std::istreambuf_iterator<char>(stream), {});
}

int main() {
    try {
        forge::TemporaryDirectory scratch(std::filesystem::temp_directory_path(), "bin-write-");
        const auto names = scratch.path() / "names.bin", bank = scratch.path() / "game.bin";
        Bytes namesBytes;
        for (auto value : {0x7ab1eu, 0xa8e36c34u, 0u, 4u, 0u}) word(namesBytes, value);
        Bytes bankBytes(9);
        for (auto value : {0u, 1u, 0u, 0u, 0u}) word(bankBytes, value);
        write(names, namesBytes); write(bank, bankBytes);
        for (bool populated : {false, true}) {
            forge::bin::File uninitialized;
            if (populated) uninitialized.addEntry("OBJECT", "NEW", {1,2,3});
            bool refused = false;
            try { uninitialized.save(names, bank); }
            catch (const std::exception& e) {
                refused = std::string(e.what()).find("loaded file headers") != std::string::npos;
            }
            check(refused, "save without loaded headers was not refused");
            check(read(names) == namesBytes && read(bank) == bankBytes,
                  "uninitialized save changed an existing output");
            const auto absentNames=scratch.path()/"absent-names.bin", absentBank=scratch.path()/"absent-game.bin";
            try { uninitialized.save(absentNames, absentBank); } catch (const std::exception&) {}
            check(!std::filesystem::exists(absentNames) && !std::filesystem::exists(absentBank),
                  "uninitialized save created output files");
        }
        auto file = forge::bin::File::open(names, bank);
        for (size_t size : {size_t(0), size_t(32766), size_t(32767), size_t(65534), size_t(0)})
            file.addEntry("OBJECT", "", Bytes(size, uint8_t(size)));
        // Enough empty records to force a split through offset-table growth alone.
        for (size_t i = 0; i < 16385; ++i) file.addEntry("OBJECT", "", {});
        file.save(names, bank);
        const auto reopened = forge::bin::File::open(names, bank);
        check(reopened.entries().size() == file.entries().size(), "writer lost records at a chunk boundary");
        for (size_t i = 0; i < file.entries().size(); ++i) {
            check(reopened.entries()[i].data == file.entries()[i].data, "writer changed a boundary payload");
            check(reopened.entries()[i].indexInDefinition == int32_t(i), "writer changed dense record order");
        }
        const auto savedNames = read(names), savedBank = read(bank);
        const auto alias=scratch.path()/"names-alias.bin";
        std::filesystem::create_hard_link(names,alias);
        for (const auto& target : {names,scratch.path()/"sub"/".."/"names.bin",alias}) {
            bool refused = false;
            try { file.save(names,target); }
            catch (const std::exception& e) {
                refused = std::string(e.what()).find("distinct output files") != std::string::npos;
            }
            check(refused, "aliased names/game outputs were not refused");
            check(read(names)==savedNames && read(bank)==savedBank && read(alias)==savedNames,
                  "aliased-output refusal changed an existing file");
        }
        std::filesystem::remove(alias);
        const auto missing=scratch.path()/"same-new-output.bin";
        bool sameMissingRefused=false;
        try { file.save(missing,missing); }
        catch (const std::exception& e) {
            sameMissingRefused=std::string(e.what()).find("distinct output files")!=std::string::npos;
        }
        check(sameMissingRefused && !std::filesystem::exists(missing),
              "aliased missing output was created");
        file.addEntry("OBJECT", "OVERSIZE", Bytes(65535, 0x6d));
        bool refused = false;
        try { file.save(names, bank); }
        catch (const std::exception& e) {
            refused = std::string(e.what()).find("chunk capacity") != std::string::npos;
        }
        check(refused, "oversized record was not refused");
        check(read(names) == savedNames && read(bank) == savedBank,
              "oversized record refusal changed an existing output");
        for (bool className : {false, true}) {
            auto malformed = reopened;
            const std::string embeddedNul("BAD\0NAME", 8);
            malformed.addEntry(className ? embeddedNul : "OBJECT", className ? "VALID" : embeddedNul, {});
            refused = false;
            try { malformed.save(names, bank); }
            catch (const std::exception& e) {
                refused = std::string(e.what()).find("embedded NUL") != std::string::npos;
            }
            check(refused, "embedded-NUL name was not refused");
            check(read(names) == savedNames && read(bank) == savedBank,
                  "embedded-NUL refusal changed an existing output");
        }
        std::cout << "BIN writer chunk limits and output preservation passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
