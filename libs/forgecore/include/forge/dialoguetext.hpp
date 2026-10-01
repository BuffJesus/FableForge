#pragma once
// Reverse lookup from a language-local dialogue bank and Sound ID to the
// text.big strings that name that voice. The join goes through
// data/Defs/<bank>snds.bin: crc0("SND_" + text entry name) -> Sound ID.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace forge::dialoguetext {

struct Line {
    std::string name;
    std::string content;
    std::string speaker;
};

struct Match {
    uint32_t soundId = 0;
    Line line;
    std::string lipsyncBank;
};

class Index {
public:
    static Index open(const std::filesystem::path& textBig,
                      const std::filesystem::path& defsRoot,
                      const std::string& language);
    const std::vector<Line>* find(const std::string& lipsyncBank,
                                  uint32_t soundId) const;
    // Empty bank searches all banks; empty query browses lines in bank/ID order.
    std::vector<Match> search(const std::string& lipsyncBank,
                              const std::string& query,size_t limit=100) const;
    size_t resolvedCount() const { return resolvedCount_; }
private:
    std::map<std::pair<std::string,uint32_t>,std::vector<Line>> lines_;
    size_t resolvedCount_ = 0;
};

} // namespace forge::dialoguetext
