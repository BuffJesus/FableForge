#include "pinned_alpha.hpp"
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>

namespace texture_font {
static LoadError loadPinned(const std::filesystem::path& path,HANDLE native, std::size_t expectedSize,
                          std::string_view sha256, std::vector<std::uint8_t>& output) {
    if (!expectedSize || expectedSize>512u*1024u*1024u || sha256.size()!=64) return LoadError::parameters;
    std::array<std::uint8_t,32> expected{};
    auto nibble=[](char c)->int { return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; };
    for(unsigned i=0;i<32;++i) {
        int hi=nibble(sha256[2*i]),lo=nibble(sha256[2*i+1]);
        if(hi<0||lo<0) return LoadError::parameters;
        expected[i]=std::uint8_t(hi*16+lo);
    }
    struct Resources {
        HANDLE file=INVALID_HANDLE_VALUE;
        BCRYPT_ALG_HANDLE algorithm=nullptr;
        BCRYPT_HASH_HANDLE hash=nullptr;
        std::vector<unsigned char> object;
        ~Resources() {
            if(hash) BCryptDestroyHash(hash);
            if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
            if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);
        }
    } r;
    r.file=native ? ReOpenFile(native,GENERIC_READ,FILE_SHARE_READ,FILE_FLAG_SEQUENTIAL_SCAN) :
        CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if(r.file==INVALID_HANDLE_VALUE) return LoadError::open;
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(r.file,&size) || size.QuadPart<0 || std::uint64_t(size.QuadPart)!=expectedSize) return LoadError::size;
    if(BCryptOpenAlgorithmProvider(&r.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return LoadError::crypto;
    DWORD objectSize=0,written=0;
    if(BCryptGetProperty(r.algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof objectSize,&written,0)<0 || written!=sizeof objectSize)
        return LoadError::crypto;
    r.object.resize(objectSize);
    if(BCryptCreateHash(r.algorithm,&r.hash,r.object.data(),objectSize,nullptr,0,0)<0) return LoadError::crypto;
    std::vector<std::uint8_t> candidate(expectedSize);
    for(std::size_t at=0;at<expectedSize;) {
        const DWORD wanted=DWORD(std::min<std::size_t>(expectedSize-at,1024*1024));
        DWORD got=0;
        if(!ReadFile(r.file,candidate.data()+at,wanted,&got,nullptr)||got!=wanted) return LoadError::read;
        if(BCryptHashData(r.hash,candidate.data()+at,got,0)<0) return LoadError::crypto;
        at+=got;
    }
    std::array<std::uint8_t,32> actual{};
    if(BCryptFinishHash(r.hash,actual.data(),DWORD(actual.size()),0)<0) return LoadError::crypto;
    if(actual!=expected) return LoadError::hash;
    output.swap(candidate);
    return LoadError::none;
}
LoadError loadPinnedAlpha(const std::filesystem::path& path,std::size_t size,
                         std::string_view sha,std::vector<std::uint8_t>& output) {
    return loadPinned(path,nullptr,size,sha,output);
}
LoadError loadPinnedAlphaHandle(void* handle,std::size_t size,
                               std::string_view sha,std::vector<std::uint8_t>& output) {
    if(!handle || handle==INVALID_HANDLE_VALUE) return LoadError::open;
    return loadPinned({},handle,size,sha,output);
}
}
