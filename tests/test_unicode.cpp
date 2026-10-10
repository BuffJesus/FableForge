#include "forge/unicode.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

int main() try {
    const auto require=[](bool value,const char* message) {if(!value) throw std::runtime_error(message);};
    const std::vector<std::pair<std::string,std::vector<uint8_t>>> valid={
        {"",{0,0}}, {"A",{65,0,0,0}},
        {"\xc3\xa9",{0xe9,0,0,0}},
        {"\xe6\xbc\xa2",{0x22,0x6f,0,0}},
        {"\xf0\x9f\x99\x82",{0x3d,0xd8,0x42,0xde,0,0}},
        {"\xf4\x8f\xbf\xbf",{0xff,0xdb,0xff,0xdf,0,0}}
    };
    for(const auto& [text,bytes]:valid) {
        std::vector<uint8_t> encoded;forge::unicode::appendUtf16z(encoded,text);
        require(encoded==bytes,"UTF-16 bytes differ from independent expected value");
        size_t cursor=0;require(forge::unicode::readUtf16z(bytes,cursor)==text && cursor==bytes.size(),"UTF-8 decode differs");
    }
    const std::vector<std::string> invalid={
        std::string("A\0B",3),"\x80","\xc0\x80","\xc1\xbf","\xc2",
        "\xc2" "A","\xe0\x80\x80","\xed\xa0\x80","\xf0\x80\x80\x80",
        "\xf4\x90\x80\x80","\xf5\x80\x80\x80","\xf8\x80\x80\x80\x80","\xff"
    };
    for(const auto& text:invalid) {
        bool refused=false;try {std::vector<uint8_t> encoded;forge::unicode::appendUtf16z(encoded,text);} catch(const std::runtime_error&) {refused=true;}
        require(refused,"Malformed UTF-8 accepted");
    }
    const std::vector<std::vector<uint8_t>> malformed={{},{65},{65,0},{0,0xd8,0,0},{0,0xdc,0,0},{0,0xd8,65,0,0,0},{0,0xd8}};
    for(const auto& bytes:malformed) {
        bool refused=false;try {size_t cursor=0;forge::unicode::readUtf16z(bytes,cursor);} catch(const std::runtime_error&) {refused=true;}
        require(refused,"Malformed UTF-16 accepted");
    }
    bool cursorRefused=false;try {size_t cursor=std::numeric_limits<size_t>::max();forge::unicode::readUtf16z({65,0,0,0},cursor);} catch(const std::runtime_error&) {cursorRefused=true;}
    require(cursorRefused,"Overflowing cursor accepted");
    const std::vector<uint8_t> framed={0x7f,65,0,0,0,0xaa};size_t cursor=1;
    require(forge::unicode::readUtf16z(framed,cursor)=="A" && cursor==5,"Decoder consumed following field bytes");
    std::cout<<"Unicode encoding, malformed input and exact terminator boundaries: PASS\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
