#include "dialogueaudio.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace albion::gui {
namespace {
std::string errorText(MMRESULT result) {
    char text[256]={};
    if(waveOutGetErrorTextA(result,text,sizeof text)==MMSYSERR_NOERROR) return text;
    return "Windows audio error "+std::to_string(result);
}
}

DialogueAudioPlayer::~DialogueAudioPlayer() {closeDevice();}

void DialogueAudioPlayer::closeDevice() {
    if(!device_) return;
    waveOutReset(device_);
    if(prepared_) waveOutUnprepareHeader(device_,&header_,sizeof header_);
    waveOutClose(device_);
    device_=nullptr;
    header_={};
    prepared_=false;
}

void DialogueAudioPlayer::load(std::vector<int16_t> pcm,uint32_t sampleRate,
                               uint16_t channels) {
    stop();
    pcm_=std::move(pcm);
    rate_=sampleRate;
    channels_=channels;
    if(channels_ && pcm_.size()%channels_) {
        pcm_.clear();rate_=0;channels_=0;
    }
}

double DialogueAudioPlayer::duration() const {
    return available()?double(pcm_.size()/channels_)/rate_:0;
}

uint64_t DialogueAudioPlayer::positionFrames() const {
    if(!available()) return 0;
    uint64_t frames=offsetFrames_;
    if(device_) {
        MMTIME time{};
        time.wType=TIME_SAMPLES;
        if(waveOutGetPosition(device_,&time,sizeof time)==MMSYSERR_NOERROR) {
            if(time.wType==TIME_SAMPLES) frames+=time.u.sample;
            else if(time.wType==TIME_MS) frames+=uint64_t(time.u.ms)*rate_/1000;
        }
    }
    return std::min<uint64_t>(frames,pcm_.size()/channels_);
}

double DialogueAudioPlayer::position() const {
    return available()?double(positionFrames())/rate_:0;
}

bool DialogueAudioPlayer::finished() const {
    return playing_ && device_ && (header_.dwFlags&WHDR_DONE);
}

bool DialogueAudioPlayer::play(std::string& error) {
    error.clear();
    if(!available()) {error="Audio unavailable for this line.";return false;}
    if(playing_ && !finished()) return true;
    if(paused_ && device_) {
        const MMRESULT result=waveOutRestart(device_);
        if(result!=MMSYSERR_NOERROR) {error=errorText(result);return false;}
        playing_=true;paused_=false;
        return true;
    }
    closeDevice();
    if(offsetFrames_>=pcm_.size()/channels_) offsetFrames_=0;
    WAVEFORMATEX format{};
    format.wFormatTag=WAVE_FORMAT_PCM;
    format.nChannels=channels_;
    format.nSamplesPerSec=rate_;
    format.wBitsPerSample=16;
    format.nBlockAlign=uint16_t(channels_*2);
    format.nAvgBytesPerSec=rate_*format.nBlockAlign;
    const MMRESULT opened=waveOutOpen(&device_,WAVE_MAPPER,&format,0,0,CALLBACK_NULL);
    if(opened!=MMSYSERR_NOERROR) {device_=nullptr;error=errorText(opened);return false;}
    const uint64_t samples=pcm_.size()-offsetFrames_*channels_;
    if(samples>std::numeric_limits<DWORD>::max()/2) {
        error="Audio clip is too large for the output device.";closeDevice();return false;
    }
    header_.lpData=reinterpret_cast<LPSTR>(pcm_.data()+offsetFrames_*channels_);
    header_.dwBufferLength=DWORD(samples*2);
    const MMRESULT prepared=waveOutPrepareHeader(device_,&header_,sizeof header_);
    if(prepared!=MMSYSERR_NOERROR) {error=errorText(prepared);closeDevice();return false;}
    prepared_=true;
    const MMRESULT written=waveOutWrite(device_,&header_,sizeof header_);
    if(written!=MMSYSERR_NOERROR) {error=errorText(written);closeDevice();return false;}
    playing_=true;paused_=false;
    return true;
}

void DialogueAudioPlayer::pause() {
    if(!playing_ || !device_) return;
    if(waveOutPause(device_)==MMSYSERR_NOERROR) {playing_=false;paused_=true;}
}

void DialogueAudioPlayer::stop() {
    closeDevice();
    offsetFrames_=0;
    playing_=paused_=false;
}

bool DialogueAudioPlayer::seek(double seconds,std::string& error) {
    if(!available()) return false;
    const bool resume=playing_;
    closeDevice();
    offsetFrames_=uint64_t(std::clamp(std::isfinite(seconds)?seconds:0.0,
                                     0.0,duration())*rate_);
    playing_=false;
    paused_=!resume;
    return !resume || play(error);
}

} // namespace albion::gui
