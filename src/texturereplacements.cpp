#include "textureproject.hpp"
#include "resolvedpath.hpp"
#include "forge/modorder.hpp"
#include "forge/temporarydirectory.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>

namespace albion::textureproject {
namespace fs=std::filesystem;
using Json=nlohmann::json;
namespace {
void need(bool condition,const std::string& error) {if(!condition) throw std::runtime_error(error);}
std::string lower(std::string value) {
    for(char& c:value) if(c>='A' && c<='Z') c=char(c-'A'+'a');
    return value;
}
bool inside(const fs::path& child,const fs::path& parent) {
    auto c=child.begin();
    for(auto p=parent.begin();p!=parent.end();++p,++c) if(c==child.end() || lower(c->generic_string())!=lower(p->generic_string())) return false;
    return true;
}
void checkpoint(Progress* progress) {
    need(!progress || !progress->cancel.load(),"Cancelled. No replacement project was created.");
}
void copyImage(const fs::path& source,const fs::path& target,Progress* progress) {
    checkpoint(progress);
    std::ifstream in(source,std::ios::binary);std::ofstream out(target,std::ios::binary);
    need(bool(in) && bool(out),"Cannot open replacement image for copying");
    std::array<char,65536> buffer;
    while(in.read(buffer.data(),buffer.size()) || in.gcount()) {
        checkpoint(progress);out.write(buffer.data(),in.gcount());
        need(bool(out),"Cannot write replacement image");
    }
    need(in.eof() && !in.bad(),"Cannot read replacement image");
    out.close();need(bool(out),"Cannot finish replacement image");checkpoint(progress);
}
struct Match {size_t row;fs::path source,target;std::string hash;};
struct Plan {Result result;Json manifest;std::vector<Match> matches;};
Plan plan(const fs::path& project,const fs::path& images,const std::string& bankFilter,Progress* progress,size_t passes=1) {
    checkpoint(progress);
    Plan p;
    const auto root=paths::resolvedPath(images),projectRoot=paths::resolvedPath(project);
    need(fs::is_directory(root),"Choose a folder of replacement PNG or DDS images");
    const auto manifest=projectRoot/"texture-project.json";
    need(fs::file_size(manifest)<32*1024*1024,"Texture project manifest is too large");
    std::ifstream input(manifest);input>>p.manifest;
    need(p.manifest.at("kind")=="FableForge texture project" && p.manifest.at("schema")==1,"Unsupported texture project");
    const auto& rows=p.manifest.at("entries");need(rows.is_array() && rows.size()<=100000,"Invalid project entry list");
    std::map<std::string,std::set<size_t>> relativeAliases,names;
    std::set<std::pair<std::string,uint32_t>> identities;
    std::set<std::string> targets;
    for(size_t n=0;n<rows.size();++n) {
        checkpoint(progress);
        const auto& row=rows[n];const auto bank=row.at("bank").get<std::string>();
        const auto id=row.at("id").get<uint32_t>();const auto name=row.at("name").get<std::string>();
        need(identities.emplace(bank,id).second,"Duplicate project texture identity");
        const auto file=row.at("file").get<std::string>();const fs::path relative(file);
        need(!relative.empty() && !relative.is_absolute() && !relative.has_root_name() &&
             *relative.begin()=="images" && relative.extension()==".png" && file.find(':')==std::string::npos,"Invalid project image path");
        for(const auto& part:relative) need(part!=".." && part!=".","Invalid project image path");
        need(targets.insert(lower(relative.generic_string())).second,"Duplicate project image path");
        if(!bankFilter.empty() && bank!=bankFilter) continue;
        auto stem=relative;stem.replace_extension();
        relativeAliases[lower(stem.generic_string())].insert(n);
        relativeAliases[lower(stem.lexically_relative("images").generic_string())].insert(n);
        names[lower(relative.stem().string())].insert(n);
        if(name.find_first_of("/\\:")==std::string::npos) names[lower(name)].insert(n);
    }
    std::vector<fs::path> files;
    for(const auto& item:fs::recursive_directory_iterator(root)) {
        checkpoint(progress);
        // Never traverse linked subfolders or accept aliased source files.
        need(paths::resolvedPath(item.path())==item.path(),"Replacement path uses a link or alias: "+item.path().string());
        if(!item.is_regular_file()) continue;
        files.push_back(item.path());need(files.size()<=100000,"Too many replacement files");
    }
    std::sort(files.begin(),files.end());
    if(progress && passes) progress->total=passes*(files.size()+1);
    std::set<size_t> chosen;
    std::string fingerprint=projectRoot.generic_string()+"\n"+root.generic_string()+"\n"+bankFilter+"\n"+p.manifest.dump();
    for(const auto& file:files) {
        checkpoint(progress);if(progress) ++progress->done;
        const auto relative=file.lexically_relative(root);auto stem=relative;stem.replace_extension();
        const auto extension=lower(file.extension().string());
        fingerprint+="\n"+relative.generic_string();
        if(extension!=".png" && extension!=".dds") {++p.result.skipped;continue;}
        std::set<size_t> candidates;
        if(auto it=relativeAliases.find(lower(stem.generic_string()));it!=relativeAliases.end()) candidates=it->second;
        else if(auto it=names.find(lower(file.stem().string()));it!=names.end()) candidates=it->second;
        if(candidates.empty()) {++p.result.skipped;p.result.details.push_back("No match: "+relative.generic_string());continue;}
        need(candidates.size()==1,"Ambiguous replacement: "+relative.generic_string()+". Use exported relative folders or select --bank.");
        const auto n=*candidates.begin();
        need(chosen.insert(n).second,"Multiple replacement files target "+rows[n].at("name").get<std::string>()+". Choose one PNG or DDS per texture.");
        fs::path target=rows[n].at("file").get<std::string>();target.replace_extension(extension);
        const auto hash=forge::modorder::sha256File(file,[&]{checkpoint(progress);});fingerprint+="\n"+hash;
        p.matches.push_back({n,file,target,hash});
        p.result.details.push_back(relative.generic_string()+" -> "+target.generic_string()+" ["+
            rows[n].at("bank").get<std::string>()+" / "+std::to_string(rows[n].at("id").get<uint32_t>())+"]");
    }
    need(!p.matches.empty(),"No replacement images matched this project");
    p.result.matched=p.matches.size();p.result.ok=true;
    p.result.token=forge::modorder::sha256OfBytes(fingerprint);
    checkpoint(progress);if(progress) ++progress->done;
    return p;
}
}
Result matchReplacements(const fs::path& project,const fs::path& images,const std::string& bank,Progress* progress) {
    try {return plan(project,images,bank,progress).result;}
    catch(const std::exception& error) {Result r;r.error=error.what();return r;}
}
Result prepareReplacements(const fs::path& project,const fs::path& images,const fs::path& output,
                           const std::string& expectedToken,const std::string& bank,Progress* progress) {
    try {
        need(!output.empty() && !fs::exists(output),"Choose a new output project folder");
        const auto destination=paths::resolvedPath(output);
        need(!inside(destination,paths::resolvedPath(project)) && !inside(destination,paths::resolvedPath(images)),
             "Choose an output folder outside both input folders");
        auto p=plan(project,images,bank,progress,2);
        if(progress) progress->total+=p.matches.size()+1;
        need(!expectedToken.empty() && expectedToken==p.result.token,"Replacement files or project changed. Match the folder again before preparing.");
        forge::TemporaryDirectory pending(destination.parent_path(),".tx-");
        Json selected=Json::array();
        for(const auto& match:p.matches) {
            const auto target=pending.path()/match.target;
            checkpoint(progress);
            fs::create_directories(target.parent_path());copyImage(match.source,target,progress);
            need(forge::modorder::sha256File(target,[&]{checkpoint(progress);})==match.hash,"Replacement image changed while copying");
            selected.push_back(p.manifest.at("entries")[match.row]);
            if(progress) ++progress->done;
        }
        // Retain project ownership for selected entries; other pack overrides
        // remain outside this subset and therefore cannot be removed by it.
        p.manifest["entries"]=selected;p.manifest["originals"]=Json::array();p.manifest["skipped"]=Json::array();
        p.manifest.erase("exportProfileSeconds");
        p.manifest["preparedReplacements"]={{"project",paths::resolvedPath(project).generic_string()},
            {"images",paths::resolvedPath(images).generic_string()},{"matchToken",p.result.token}};
        std::ofstream out(pending.path()/"texture-project.json");out<<p.manifest.dump(2)<<'\n';out.close();need(bool(out),"Cannot write prepared project manifest");
        std::ofstream guide(pending.path()/"README.txt");
        guide<<"PREPARED TEXTURE REPLACEMENTS\n\nOnly matched PNG/DDS images are included. Original project and replacement files were not modified.\nReview this folder in FableForge with the intended dimensions and pack destination before saving or applying.\nMatching filenames is not image validation; review checks the images and source identities.\nThis subset keeps its original project's ownership identity. It does not remove other entries saved by that project.\nNative source payloads remain in the original export or game archive, not duplicated here.\n";
        guide.close();need(bool(guide),"Cannot write prepared project guide");
        need(plan(project,images,bank,progress,0).result.token==expectedToken,"Replacement files or project changed while preparing");
        need(!fs::exists(destination),"Output project appeared while preparing");
        checkpoint(progress);
        fs::rename(pending.path(),destination);
        if(progress) progress->done=progress->total.load();
        p.result.exported=p.matches.size();return p.result;
    } catch(const std::exception& error) {Result r;r.error=error.what();return r;}
}
}
