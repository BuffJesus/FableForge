// Offline integration driver for the same importer used by Fable Textures.
#include "import.hpp"
#include "forge/modorder.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include <windows.h>

namespace fs=std::filesystem;
static std::map<std::string,std::string> snapshot(const fs::path& root) {
    std::map<std::string,std::string> out;
    for(const auto& entry:fs::recursive_directory_iterator(root))if(entry.is_regular_file())
        out[entry.path().lexically_relative(root).generic_string()]=forge::modorder::sha256Of(entry.path());
    return out;
}
int main(int argc,char** argv) try {
    if(argc!=4 && argc!=6)throw std::runtime_error("usage: import_probe root project runtime [mutate-image replacement]");
    const fs::path root=argv[1],project=argv[2],runtime=argv[3];
    const auto plan=textureimport::review(root,project);
    if(!plan.error.empty())throw std::runtime_error(plan.error);
    HANDLE lock=INVALID_HANDLE_VALUE;
    if(argc==6) {
        if(std::string(argv[4])=="--lock-destination") {
            const fs::path target=argv[5];
            lock=CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("could not lock test destination");
        } else fs::copy_file(argv[5],argv[4],fs::copy_options::overwrite_existing);
    }
    const auto before=snapshot(root);
    const auto result=textureimport::apply(root,plan,runtime);
    if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
    const auto after=snapshot(root);
    bool existing=true;
    for(const auto& [path,sha]:before)if(!after.contains(path) || after.at(path)!=sha)existing=false;
    nlohmann::json report={{"ok",result.ok},{"error",result.error},{"imported",result.imported},
        {"resized",result.resized},{"rootFilesPreserved",before==after},{"existingFilesPreserved",existing},{"notes",result.notes},
        {"reviewCanImport",plan.hasChanges()},{"reviewRestoredArchives",plan.restoredArchives},{"restoredArchives",result.restoredArchives}};
    report["reviewNeedsArchive4GB"]=plan.needsArchive4GB;report["archive4GBInstalled"]=result.archive4GBInstalled;
    std::cout<<report.dump()<<'\n';return result.ok?0:2;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
