#include "forge/xboxadpcm.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace forge::xboxadpcm {
namespace {

constexpr std::array<int,89> steps={
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,
    50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,
    253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
    1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,
    3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,
    11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,
    32767};
constexpr std::array<int,8> indexDelta={-1,-1,-1,-1,2,4,6,8};

struct State {int predictor=0,index=0;};

int16_t expand(uint8_t nibble,State& state) {
    const int difference=((2*int(nibble&7)+1)*steps[size_t(state.index)])>>3;
    state.predictor=std::clamp(state.predictor+((nibble&8)?-difference:difference),-32768,32767);
    state.index=std::clamp(state.index+indexDelta[size_t(nibble&7)],0,88);
    return int16_t(state.predictor);
}

} // namespace

std::vector<int16_t> decode(std::span<const uint8_t> bytes,
                            uint16_t channels,uint16_t blockAlign) {
    if((channels!=1 && channels!=2) || blockAlign!=36*channels)
        throw std::runtime_error("xboxadpcm: expected 36 bytes per channel, mono or stereo");
    if(bytes.size()%blockAlign)
        throw std::runtime_error("xboxadpcm: partial block");
    const size_t blocks=bytes.size()/blockAlign;
    if(blocks>std::vector<int16_t>().max_size()/(64*channels))
        throw std::runtime_error("xboxadpcm: decoded output too large");
    std::vector<int16_t> pcm;
    pcm.reserve(blocks*64*channels);
    for(size_t block=0;block<blocks;++block) {
        const uint8_t* input=bytes.data()+block*blockAlign;
        std::array<State,2> state{};
        std::array<std::array<int16_t,65>,2> samples{};
        for(uint16_t channel=0;channel<channels;++channel) {
            const uint8_t* header=input+4*channel;
            state[channel].predictor=int16_t(uint16_t(header[0])|uint16_t(header[1])<<8);
            state[channel].index=std::min(int(header[2]),88);
            samples[channel][0]=int16_t(state[channel].predictor);
        }
        const uint8_t* encoded=input+4*channels;
        for(size_t group=0;group<8;++group)
            for(uint16_t channel=0;channel<channels;++channel)
                for(size_t byte=0;byte<4;++byte) {
                    const uint8_t packed=*encoded++;
                    const size_t sample=1+group*8+byte*2;
                    samples[channel][sample]=expand(packed&15,state[channel]);
                    samples[channel][sample+1]=expand(packed>>4,state[channel]);
                }
        // The 64th nibble's decoded sample is a spare; the header plus the
        // first 63 decoded nibbles make one 64-sample block per channel.
        for(size_t sample=0;sample<64;++sample)
            for(uint16_t channel=0;channel<channels;++channel)
                pcm.push_back(samples[channel][sample]);
    }
    return pcm;
}

} // namespace forge::xboxadpcm
