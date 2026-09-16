#include "forge/tng.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace forge::tng {
namespace {

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

// "Key rest of line;" -> {Key, rest of line}
Property parseProperty(std::string_view logical) {
    Property prop;
    size_t split = 0;
    while (split < logical.size() &&
           !std::isspace(static_cast<unsigned char>(logical[split]))) {
        ++split;
    }
    prop.key = std::string(logical.substr(0, split));
    prop.value = std::string(trim(logical.substr(split)));
    return prop;
}

bool isBlankLine(const std::string& raw) { return trim(raw).empty(); }

const Property* findProperty(const std::vector<Property>& properties,
                             std::string_view key) {
    for (const Property& prop : properties) {
        if (equalsIgnoreCase(prop.key, key)) return &prop;
    }
    return nullptr;
}

} // namespace

std::optional<std::string> Thing::find(std::string_view key) const {
    if (const Property* prop = findProperty(properties, key)) {
        return prop->value;
    }
    return std::nullopt;
}

std::string Thing::scriptName() const {
    return find("ScriptName").value_or(std::string());
}

std::string Thing::definitionType() const {
    std::string value = find("DefinitionType").value_or(std::string());
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

const CtcBlock* Thing::findCtc(std::string_view name) const {
    for (const CtcBlock& block : ctcBlocks) {
        if (equalsIgnoreCase(block.name, name)) return &block;
    }
    return nullptr;
}

File File::parse(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("tng: cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parseText(buffer.str(), path.filename().string());
}

File File::parseText(std::string text, std::string sourceName) {
    File result;
    result.source_ = std::move(sourceName);

    // Split into raw lines, each keeping its own terminator, so that
    // serialize() reproduces the input exactly.
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            result.rawLines_.push_back(text.substr(start));
            break;
        }
        result.rawLines_.push_back(text.substr(start, end - start + 1));
        start = end + 1;
    }

    // Dominant line terminator, for lines mutations add.
    size_t crlf = 0, lf = 0;
    for (const std::string& raw : result.rawLines_) {
        if (raw.size() >= 2 && raw[raw.size() - 2] == '\r' && raw.back() == '\n') {
            ++crlf;
        } else if (!raw.empty() && raw.back() == '\n') {
            ++lf;
        }
    }
    result.lineTerminator_ = (lf > crlf) ? "\n" : "\r\n";

    Thing* currentThing = nullptr;
    CtcBlock* currentBlock = nullptr;

    for (size_t lineIndex = 0; lineIndex < result.rawLines_.size(); ++lineIndex) {
        std::string_view logical = trim(result.rawLines_[lineIndex]);
        if (logical.empty()) continue;
        if (!logical.empty() && logical.back() == ';') {
            logical = trim(logical.substr(0, logical.size() - 1));
        }
        if (logical.empty()) continue;

        Property prop = parseProperty(logical);
        prop.line = lineIndex;

        if (equalsIgnoreCase(prop.key, "NewThing")) {
            result.things_.emplace_back();
            currentThing = &result.things_.back();
            currentThing->type = prop.value;
            currentThing->startLine = lineIndex;
            currentThing->endLine = lineIndex; // until EndThing is seen
            currentBlock = nullptr;
            continue;
        }
        if (equalsIgnoreCase(prop.key, "EndThing")) {
            if (currentThing != nullptr) currentThing->endLine = lineIndex;
            currentThing = nullptr;
            currentBlock = nullptr;
            continue;
        }
        if (currentThing != nullptr && prop.key.starts_with("StartCTC")) {
            currentThing->ctcBlocks.emplace_back();
            currentBlock = &currentThing->ctcBlocks.back();
            currentBlock->name = prop.key.substr(5); // drop "Start"
            currentBlock->startLine = lineIndex;
            currentBlock->endLine = lineIndex; // until EndCTC is seen
            continue;
        }
        if (currentThing != nullptr && prop.key.starts_with("EndCTC")) {
            if (currentBlock != nullptr) currentBlock->endLine = lineIndex;
            currentBlock = nullptr;
            continue;
        }

        if (currentBlock != nullptr) {
            currentBlock->properties.push_back(std::move(prop));
        } else if (currentThing != nullptr) {
            currentThing->properties.push_back(std::move(prop));
        }
        // Lines outside any thing (Version, XXXSectionStart, ...) stay only in
        // rawLines_; the mutation API keys everything off recorded line indices.
    }

    return result;
}

std::string File::serialize() const {
    std::string out;
    size_t total = 0;
    for (const std::string& line : rawLines_) total += line.size();
    out.reserve(total);
    for (const std::string& line : rawLines_) out += line;
    return out;
}

const Thing& File::thingAt(size_t index) const {
    if (index >= things_.size()) {
        throw std::out_of_range("tng: thing index " + std::to_string(index) +
                                " out of range (" + std::to_string(things_.size()) +
                                " things)");
    }
    return things_[index];
}

// Rebuild "Key Value;" on an existing line, keeping the original key spelling,
// leading whitespace, and terminator.
void File::replaceValueOnLine(size_t line, std::string_view value) {
    const std::string& raw = rawLines_[line];
    size_t bodyStart = 0;
    while (bodyStart < raw.size() &&
           std::isspace(static_cast<unsigned char>(raw[bodyStart]))) {
        ++bodyStart;
    }
    size_t keyEnd = bodyStart;
    while (keyEnd < raw.size() &&
           !std::isspace(static_cast<unsigned char>(raw[keyEnd]))) {
        ++keyEnd;
    }
    std::string terminator;
    if (raw.size() >= 2 && raw[raw.size() - 2] == '\r' && raw.back() == '\n') {
        terminator = "\r\n";
    } else if (!raw.empty() && raw.back() == '\n') {
        terminator = "\n";
    }
    std::string rebuilt = raw.substr(0, keyEnd);
    rebuilt += ' ';
    rebuilt += value;
    rebuilt += ';';
    rebuilt += terminator;
    rawLines_[line] = std::move(rebuilt);
}

void File::insertLine(size_t line, std::string text) {
    rawLines_.insert(rawLines_.begin() + static_cast<ptrdiff_t>(line),
                     std::move(text));
}

void File::reindex() {
    std::string sourceName = std::move(source_);
    *this = parseText(serialize(), std::move(sourceName));
}

void File::setThingProperty(size_t thingIndex, std::string_view key,
                            std::string_view value) {
    const Thing& thing = thingAt(thingIndex);
    if (const Property* prop = findProperty(thing.properties, key)) {
        replaceValueOnLine(prop->line, value);
        reindex();
        return;
    }
    std::string line = std::string(key) + ' ' + std::string(value) + ';' +
                       lineTerminator_;
    insertLine(thing.endLine, std::move(line));
    reindex();
}

bool File::removeThingProperty(size_t thingIndex, std::string_view key) {
    const Thing& thing = thingAt(thingIndex);
    const Property* prop = findProperty(thing.properties, key);
    if (prop == nullptr) return false;
    rawLines_.erase(rawLines_.begin() + static_cast<ptrdiff_t>(prop->line));
    reindex();
    return true;
}

void File::setCtcProperty(size_t thingIndex, std::string_view ctcName,
                          std::string_view key, std::string_view value) {
    const Thing& thing = thingAt(thingIndex);
    const CtcBlock* block = thing.findCtc(ctcName);
    if (block == nullptr) {
        throw std::runtime_error("tng: thing " + std::to_string(thingIndex) +
                                 " has no block " + std::string(ctcName));
    }
    if (const Property* prop = findProperty(block->properties, key)) {
        replaceValueOnLine(prop->line, value);
        reindex();
        return;
    }
    std::string line = std::string(key) + ' ' + std::string(value) + ';' +
                       lineTerminator_;
    insertLine(block->endLine, std::move(line));
    reindex();
}

bool File::removeCtcProperty(size_t thingIndex, std::string_view ctcName,
                             std::string_view key) {
    const Thing& thing = thingAt(thingIndex);
    const CtcBlock* block = thing.findCtc(ctcName);
    if (block == nullptr) return false;
    const Property* prop = findProperty(block->properties, key);
    if (prop == nullptr) return false;
    rawLines_.erase(rawLines_.begin() + static_cast<ptrdiff_t>(prop->line));
    reindex();
    return true;
}

std::string File::serializeThingBlock(const Thing& thing) const {
    std::string block;
    const std::string& eol = lineTerminator_;
    block += "NewThing " + thing.type + ';' + eol;
    for (const Property& prop : thing.properties) {
        block += prop.key + ' ' + prop.value + ';' + eol;
    }
    for (const CtcBlock& ctc : thing.ctcBlocks) {
        block += "Start" + ctc.name + ';' + eol;
        for (const Property& prop : ctc.properties) {
            block += prop.key + ' ' + prop.value + ';' + eol;
        }
        block += "End" + ctc.name + ';' + eol;
    }
    block += std::string("EndThing;") + eol;
    return block;
}

size_t File::addThing(const Thing& thing) {
    std::string block = serializeThingBlock(thing);
    const std::string& eol = lineTerminator_;

    // Insert before XXXSectionEnd when present, else at EOF.
    size_t insertAt = rawLines_.size();
    for (size_t i = 0; i < rawLines_.size(); ++i) {
        std::string_view logical = trim(rawLines_[i]);
        if (logical.starts_with("XXXSectionEnd")) {
            insertAt = i;
            break;
        }
    }
    const bool blankBefore = insertAt > 0 && isBlankLine(rawLines_[insertAt - 1]);
    std::string text = blankBefore ? std::move(block) : eol + block;
    if (insertAt < rawLines_.size()) text += eol; // keep a blank line after
    insertLine(insertAt, std::move(text));
    reindex();
    return things_.size() - 1;
}

size_t File::insertThingBlock(std::string_view sectionName,
                              std::string blockText) {
    const std::string& eol = lineTerminator_;

    // Locate the requested section's XXXSectionEnd line.
    size_t insertAt = rawLines_.size();
    bool inSection = false;
    bool found = false;
    bool hasAnySection = false;
    for (size_t i = 0; i < rawLines_.size(); ++i) {
        std::string_view logical = trim(rawLines_[i]);
        if (!logical.empty() && logical.back() == ';') {
            logical = trim(logical.substr(0, logical.size() - 1));
        }
        if (logical.starts_with("XXXSectionStart")) {
            hasAnySection = true;
            std::string_view name = trim(logical.substr(15));
            inSection = equalsIgnoreCase(name, sectionName);
            continue;
        }
        if (inSection && logical.starts_with("XXXSectionEnd")) {
            insertAt = i;
            found = true;
            break;
        }
    }
    if (!found) {
        if (!hasAnySection) {
            std::string section;
            if (!rawLines_.empty() && !isBlankLine(rawLines_.back())) section += eol;
            section += "XXXSectionStart " + std::string(sectionName) + ';' + eol;
            section += eol;
            section += "XXXSectionEnd;" + eol;
            insertLine(rawLines_.size(), std::move(section));
            reindex();
            return insertThingBlock(sectionName, std::move(blockText));
        }
        throw std::runtime_error("tng: no section named " +
                                 std::string(sectionName));
    }

    // Normalise the caller's terminators to this file's dominant one.
    std::string normalised;
    normalised.reserve(blockText.size() + 16);
    for (size_t i = 0; i < blockText.size(); ++i) {
        if (blockText[i] == '\r') continue;
        if (blockText[i] == '\n') {
            normalised += eol;
            continue;
        }
        normalised += blockText[i];
    }
    if (!normalised.empty() && !normalised.ends_with(eol)) normalised += eol;

    const bool blankBefore = insertAt > 0 && isBlankLine(rawLines_[insertAt - 1]);
    std::string text = blankBefore ? std::move(normalised) : eol + normalised;
    text += eol; // keep a blank separator before XXXSectionEnd
    insertLine(insertAt, std::move(text));
    reindex();

    // The new thing is the last one that starts at or after the insert point.
    size_t index = things_.size() - 1;
    for (size_t i = 0; i < things_.size(); ++i) {
        if (things_[i].startLine >= insertAt) { index = i; break; }
    }
    return index;
}

std::string File::thingBlockText(size_t thingIndex) const {
    const Thing& thing = thingAt(thingIndex);
    std::string text;
    for (size_t i = thing.startLine; i <= thing.endLine && i < rawLines_.size(); ++i) text += rawLines_[i];
    return text;
}

std::string File::sectionOf(size_t thingIndex) const {
    const Thing& thing = thingAt(thingIndex);
    std::string name = "NULL";
    for (size_t i = 0; i < thing.startLine && i < rawLines_.size(); ++i) {
        std::string_view logical = trim(rawLines_[i]);
        if (!logical.empty() && logical.back() == ';') logical = trim(logical.substr(0, logical.size() - 1));
        if (logical.starts_with("XXXSectionStart")) name = std::string(trim(logical.substr(15)));
    }
    return name;
}

size_t File::insertThingBlockBefore(size_t thingIndex, std::string blockText) {
    if (thingIndex >= things_.size()) {
        const std::string section = things_.empty() ? std::string("NULL") : sectionOf(things_.size() - 1);
        return insertThingBlock(section, std::move(blockText));
    }
    const std::string& eol = lineTerminator_;
    std::string normalised;
    normalised.reserve(blockText.size() + 16);
    for (size_t i = 0; i < blockText.size(); ++i) {
        if (blockText[i] == '\r') continue;
        if (blockText[i] == '\n') { normalised += eol; continue; }
        normalised += blockText[i];
    }
    if (!normalised.empty() && !normalised.ends_with(eol)) normalised += eol;
    const size_t insertAt = thingAt(thingIndex).startLine;
    insertLine(insertAt, normalised + eol);   // blank separator before the displaced thing
    reindex();
    return thingIndex;
}

void File::removeThing(size_t thingIndex) {
    const Thing& thing = thingAt(thingIndex);
    size_t first = thing.startLine;
    size_t last = thing.endLine;
    // Also drop one adjacent blank separator line to keep the file's rhythm.
    if (last + 1 < rawLines_.size() && isBlankLine(rawLines_[last + 1])) {
        ++last;
    } else if (first > 0 && isBlankLine(rawLines_[first - 1])) {
        --first;
    }
    rawLines_.erase(rawLines_.begin() + static_cast<ptrdiff_t>(first),
                    rawLines_.begin() + static_cast<ptrdiff_t>(last + 1));
    reindex();
}

void File::replaceThing(size_t thingIndex, const Thing& thing) {
    const Thing& existing = thingAt(thingIndex);
    const size_t first = existing.startLine;
    const size_t last = existing.endLine;
    std::string block = serializeThingBlock(thing);
    rawLines_.erase(rawLines_.begin() + static_cast<ptrdiff_t>(first),
                    rawLines_.begin() + static_cast<ptrdiff_t>(last + 1));
    rawLines_.insert(rawLines_.begin() + static_cast<ptrdiff_t>(first),
                     std::move(block));
    reindex();
}

} // namespace forge::tng
