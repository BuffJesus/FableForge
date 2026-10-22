// Loading TerrainExtent256.dll into anything but the audited Fable.exe must refuse
// without patching, and say so in its log.
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <stdexcept>
static void require(bool x,const char* what){if(!x)throw std::runtime_error(what);}
int wmain(int argc,wchar_t** argv) {
    require(argc==2,"usage: test_terrain_extent_load <dll>");
    std::filesystem::path log=argv[1];log.replace_extension(L".log");
    std::error_code ignored;std::filesystem::remove(log,ignored);
    auto module=LoadLibraryW(argv[1]);require(module!=nullptr,"load");
    std::ifstream in(log);std::stringstream text;text<<in.rdbuf();
    require(text.str().rfind("refused: not the audited Fable.exe build",0)==0,"host process was not refused");
    in.close();std::filesystem::remove(log,ignored);
    std::puts("PASS terrain extent DLL refuses a non-Fable host and logs it");
}
