#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <stdexcept>
static void require(bool x){if(!x)throw std::runtime_error("font probe load isolation failed");}
int wmain(int argc,wchar_t** argv) {
    require(argc==2);
    // This process is deliberately not the designated scratch Fable.exe.
    for(const wchar_t* flag:{static_cast<const wchar_t*>(nullptr),L"0",L"1",L"invalid"}) {
        require(SetEnvironmentVariableW(L"FABLE_FORGE_FONT_PROBE",flag)!=0);
        auto module=LoadLibraryW(argv[1]);require(module!=nullptr);
        require(FreeLibrary(module)!=0);
    }
    std::puts("PASS font DLL load/unload outside scratch game with unset/off/on/invalid opt-in");
}
