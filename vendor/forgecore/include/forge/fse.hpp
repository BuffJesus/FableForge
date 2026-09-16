#pragma once
// Reader for the Fable Script Extender (FSE) API manifest
// (FableTLC refs/fse_api_manifest.json) — the runtime-scripting authoring
// surface, distinct from the native cutscene macro VM. FSE exposes a Lua/Sol2
// API of Entity and Quest functions; this manifest is the reversed, versioned
// index of them (933 functions in the shipped manifest: 88 Entity, 845 Quest).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace forge::fse {

struct Parameter {
    std::string name;
    std::string type;
    bool optional = false;
};

struct Function {
    std::string name;
    std::string scope;        // "Entity" | "Quest"
    std::string returnType;
    std::vector<Parameter> parameters;
    bool blocking = false;
    std::string category;
    std::string description;

    // C-like one-line signature, e.g. "void AcquireControl(CScriptThing* pMe)".
    std::string signature() const;
};

struct Manifest {
    std::string fseVersion;
    std::string apiVersion;
    std::string generatedAtUtc;
    std::vector<Function> functions;
    // Declared script limits (maxQuestScripts / maxEntityScripts / etc).
    std::vector<std::pair<std::string, int64_t>> limits;

    const Function* find(std::string_view name) const;
};

// Throws std::runtime_error on unreadable file or malformed JSON.
Manifest load(const std::filesystem::path& path);
Manifest loadText(const std::string& json, const std::string& sourceName = {});

} // namespace forge::fse
