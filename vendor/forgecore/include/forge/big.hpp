#pragma once
// Reader for the Lionhead BIG archive container (BIGB / "B\0\0\0"). Retail
// asset archives (graphics.big, text.big, ...) and community `.fmp` mod packages
// are both BIG files (an .fmp is a BIGFile with contentType 510 whose banks are
// the mod's changed records). Layout ported from SilverChest.Formats.Big
// (BigReader.cs) and validated against ControllerSupport.fmp + HalsSword.fmp.
//
//   header: char[4] magic ("BIGB" or "B\0\0\0"), u32 version, u32 bankDirOffset,
//           u32 contentType
//   bank directory @ bankDirOffset: u32 bankCount, then per bank:
//           asciiz name, u32 id, u32 entryCount, u32 entryStart, u32 length,
//           u32 blockSize
//   entry table @ entryStart: u32 typeCount, skip typeCount*8, then per entry:
//           u32 magic, id, type, length, dataOffset, devFileType, u32 nameLen,
//           char[nameLen] name, u32 devCrc, u32 devSourceCount,
//           devSourceCount x { u32 len, char[len] }, u32 subHeaderLen,
//           byte[subHeaderLen] subHeader
// For BIN banks (game.bin/script.bin records) the subHeader is the ASCIIZ
// definition-type string, and the entry data is the raw compiled-def payload.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace forge::big {

struct Entry {
    std::string name;                 // DevSymbolName, e.g. PLAYER_GUI_PC
    std::string definition;           // subHeader as ASCIIZ (def-type for BIN banks)
    uint32_t id = 0;
    uint32_t type = 0;
    uint32_t magic = 0;
    uint32_t devFileType = 0;
    uint32_t devCrc = 0;
    uint32_t dataOffset = 0;          // absolute file offset of the payload
    uint32_t length = 0;              // payload byte length
    std::vector<std::string> devSources;
    std::vector<uint8_t> subHeader;   // raw subheader bytes
    std::vector<uint8_t> data;        // payload bytes (populated by writer/builder)
};

struct Bank {
    std::string name;                 // e.g. GameBINEntries
    uint32_t id = 0;
    uint32_t blockSize = 0;
    std::vector<Entry> entries;
};

class File {
public:
    // open() reads only the bank directory and entry tables
    // (the tail of the file) and reads entry payloads from disk on demand --
    // textures.big is 535 MB and graphics.big 244 MB, which used to sit in RAM for
    // the whole session. openFully() keeps the old whole-file behaviour, which
    // serialize() round-trips rely on.
    static File open(const std::filesystem::path& path);
    static File openFully(const std::filesystem::path& path);

    uint32_t version() const { return version_; }
    uint32_t contentType() const { return contentType_; }
    const std::string& magic() const { return magic_; }
    const std::vector<Bank>& banks() const { return banks_; }
    std::vector<Bank>& banks() { return banks_; }
    const Bank* findBank(const std::string& name) const;
    Bank* findBank(const std::string& name);

    // Raw payload bytes for an entry (a slice of the backing file).
    std::vector<uint8_t> entryData(const Entry& e) const;

    // Serialize to BIG bytes. Each entry's payload is taken from its `data`
    // field if non-empty, else sliced from this file (dataOffset/length), so a
    // freshly-read File round-trips. Banks/entries are written in order.
    std::vector<uint8_t> serialize() const;

    // Build a File from scratch (for `fmp export`). Set version/contentType, add
    // banks, add entries with data. Entries need name/definition/data; other
    // fields default. magic defaults to 0 (old .fmp) unless set.
    void setVersion(uint32_t v) { version_ = v; }
    void setContentType(uint32_t c) { contentType_ = c; }
    void setMagic(const std::string& magic) { magic_ = magic; }
    Bank& addBank(const std::string& name, uint32_t id);

private:
    std::string magic_ = std::string("B\0\0\0", 4);
    uint32_t version_ = 0;
    uint32_t contentType_ = 0;
    std::vector<Bank> banks_;
    std::vector<uint8_t> raw_;        // whole file (only when opened with openFully)
    std::filesystem::path path_;      // lazy mode : entries are read from disk on demand
    uint64_t fileSize_ = 0;
};

} // namespace forge::big
