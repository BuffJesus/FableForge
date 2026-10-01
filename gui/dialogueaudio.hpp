#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>
#include <mmsystem.h>

namespace albion::gui {

// One selected PCM16 dialogue clip. The device sample counter drives the
// lip sync clock; no UI delta-time clock is used during playback.
class DialogueAudioPlayer {
public:
    ~DialogueAudioPlayer();
    void load(std::vector<int16_t> pcm,uint32_t sampleRate,uint16_t channels);
    bool play(std::string& error);
    void pause();
    void stop();
    bool seek(double seconds,std::string& error);
    double position() const;
    double duration() const;
    bool playing() const {return playing_;}
    bool paused() const {return paused_;}
    bool finished() const;
    bool available() const {return !pcm_.empty() && rate_ && channels_;}
private:
    void closeDevice();
    uint64_t positionFrames() const;
    std::vector<int16_t> pcm_;
    uint32_t rate_=0;
    uint16_t channels_=0;
    uint64_t offsetFrames_=0;
    HWAVEOUT device_=nullptr;
    WAVEHDR header_{};
    bool prepared_=false,playing_=false,paused_=false;
};

} // namespace albion::gui
