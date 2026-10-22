// A normal host must never activate a game-specific patch or pin the DLL.
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <stdexcept>

static void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}

int wmain(int argc,wchar_t** argv) try {
    require(argc==2,"usage: test_archive_load <Archive4GB.dll>");
    const std::filesystem::path source=std::filesystem::absolute(argv[1]);
    auto log=source;log.replace_extension(L".log");
    // Each CTest build owns its copy and adjacent log.
    require(!std::filesystem::exists(log),"refuse to overwrite an existing DLL log");
    for(unsigned attempt=0;attempt<2;++attempt) {
        const auto module=LoadLibraryW(source.c_str());
        require(module!=nullptr,"load DLL in non-Fable host");
        require(FreeLibrary(module)!=0,"unload refused DLL");
        require(GetModuleHandleW(source.c_str())==nullptr,"refused DLL must not be pinned");
    }
    std::ifstream in(log);std::stringstream text;text<<in.rdbuf();in.close();
    constexpr auto expected="refused: not the audited Fable.exe build; archive limit unchanged\n";
    require(text.str()==std::string(expected)+expected,"both host loads must refuse before activation");
    require(std::filesystem::remove(log),"remove owned refusal log");
    std::puts("PASS Archive4GB refuses two non-game loads and remains unloadable");
} catch(const std::exception& error) {
    std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;
}
