#pragma once
// Read-only index for Fable dialogue .lut banks. A clip's Index joins the
// same-numbered LIPSYNC entry in its paired dialogue.big sub-bank.

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace forge::lut {

struct DialoguePair {
    std::string lutFilename;
    std::string lipsyncBank;
    std::string sndsFilename;
};

// Resolve one text.big SpeechBank (which normally ends in .lug) to its exact
// language-local .lut and dialogue.big sub-bank. Unknown names are rejected;
// no search through other banks or installed languages is performed.
std::optional<DialoguePair> dialoguePair(std::string_view speechBank,
                                         std::string_view language);

struct Clip {
    uint32_t index = 0;
    uint64_t recordOffset = 0, riffOffset = 0, dataOffset = 0;
    uint32_t riffLength = 0, dataBytes = 0;
    uint16_t formatTag = 0, channels = 0, blockAlign = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0, averageBytesPerSecond = 0;
    float minDistance = 0, maxDistance = 0;
    uint32_t flags2 = 0, priority = 0;
    double durationSeconds() const {
        return averageBytesPerSecond ? double(dataBytes)/averageBytesPerSecond : 0.0;
    }
};

class File {
public:
    static File open(const std::filesystem::path& path);
    const std::vector<Clip>& clips() const { return clips_; }
    const Clip* find(uint32_t index) const;
    // Reads only the requested embedded RIFF/WAVE clip from disk.
    std::vector<uint8_t> riff(uint32_t index) const;
    // Decodes the selected Xbox ADPCM clip to interleaved PCM16.
    std::vector<int16_t> pcm16(uint32_t index) const;
    // Returns a standard PCM16 RIFF/WAVE for the selected clip.
    std::vector<uint8_t> wavPcm16(uint32_t index) const;
private:
    std::filesystem::path path_;
    uint64_t fileSize_ = 0;
    std::vector<Clip> clips_;
    std::unordered_map<uint32_t,size_t> byIndex_;
};

} // namespace forge::lut
