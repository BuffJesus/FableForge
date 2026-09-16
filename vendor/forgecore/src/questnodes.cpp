// FQT visual-scripting port: node registry + graph -> FSE-Lua compiler.
// The codegen deliberately mirrors FQT's CodeGenerator.cs entity-script
// semantics line-for-line (linear action flattening, structured nesting,
// branch-label substitution, escaping, default-value fill, variable nodes,
// action-queue rewriting); FQT's snapshot fixtures gate it in the tests.

#include "forge/questnodes.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "questnode_defs_data.hpp"

using nlohmann::json;
using nlohmann::ordered_json;

namespace forge::questnodes {

namespace {

std::string lowered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string uppered(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

bool istartsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() &&
           iequals(text.substr(0, prefix.size()), prefix);
}

void replaceAll(std::string& text, std::string_view needle, std::string_view value) {
    if (needle.empty()) return;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        text.replace(pos, needle.size(), value);
        pos += value.size();
    }
}

std::string indentOf(int level) { return std::string(static_cast<size_t>(level) * 4, ' '); }

bool isBlank(std::string_view line) {
    return std::all_of(line.begin(), line.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::string trimTrailingNewlines(std::string text) {
    while (!text.empty() && text.back() == '\n') text.pop_back();
    return text;
}

std::string trimTrailingWhitespace(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

// FQT CodeGenerator.Escape: make a value safe inside a Lua string literal.
std::string escapeLua(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\0': break;
            default: out += c; break;
        }
    }
    return out;
}

// The eight FQT node types whose direct Quest:* calls crash from entity
// threads; FQT rewrites them into a state-queue push consumed by a
// quest-thread worker (ProcessQuestActions).
const char* const kActionQueueTypes[] = {
    "highlightQuestTarget",       "clearQuestTargetHighlight",
    "showMinimapMarker",          "hideMinimapMarker",
    "highlightQuestTargetByName", "clearQuestTargetHighlightByName",
    "showMinimapMarkerByName",    "hideMinimapMarkerByName",
};

bool isActionQueueType(std::string_view type) {
    for (const char* t : kActionQueueTypes) {
        if (iequals(type, t)) return true;
    }
    return false;
}

std::string jsonValueToString(const ordered_json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_null()) return "";
    return value.dump();
}

// ---------------------------------------------------------------------------
// Curated definitions (embedded JSON generated from FQT NodeDefinitions.cs).

std::vector<NodeDef> parseCuratedDefs() {
    const json root = json::parse(detail::kCuratedNodeDefsJson);
    std::vector<NodeDef> defs;
    for (const json& item : root.at("nodes")) {
        NodeDef def;
        def.type = item.value("type", std::string());
        def.label = item.value("label", std::string());
        def.category = item.value("category", std::string());
        def.description = item.value("description", std::string());
        def.codeTemplate = item.value("codeTemplate", std::string());
        def.advanced = item.value("advanced", false);
        def.hasBranching = item.value("hasBranching", false);
        if (const auto it = item.find("branchLabels"); it != item.end()) {
            for (const json& label : *it) def.branchLabels.push_back(label.get<std::string>());
        }
        if (const auto it = item.find("properties"); it != item.end()) {
            for (const json& pj : *it) {
                PropertyDef prop;
                prop.name = pj.value("name", std::string());
                prop.type = pj.value("type", std::string());
                prop.label = pj.value("label", std::string());
                prop.defaultValue = pj.value("defaultValue", std::string());
                prop.optionsSource = pj.value("optionsSource", std::string());
                def.properties.push_back(std::move(prop));
            }
        }
        def.source = "curated";
        defs.push_back(std::move(def));
    }
    return defs;
}

const std::vector<NodeDef>& curatedDefs() {
    static const std::vector<NodeDef> defs = parseCuratedDefs();
    return defs;
}

// Map an FSE manifest C++ parameter type to a node property type.
struct MappedParam {
    std::string propType;
    std::string defaultValue;
    bool quoted = false;   // wrap the placeholder in Lua quotes
    bool skipped = false;  // internal parameter, not graph-authorable
};

MappedParam mapManifestParam(const std::string& cppType, bool entityScope, size_t index) {
    MappedParam m;
    if (cppType == "sol::this_state") {
        m.skipped = true;
        return m;
    }
    if (entityScope && index == 0 && cppType.find("CScriptThing") != std::string::npos) {
        m.skipped = true;  // implicit Me/self
        return m;
    }
    if (cppType.find("CScriptThing") != std::string::npos) {
        m.propType = "thing";
        m.defaultValue = "Me";
        return m;
    }
    if (cppType.find("std::string") != std::string::npos) {
        m.propType = "string";
        m.defaultValue = "";
        m.quoted = true;
        return m;
    }
    if (cppType.find("float") != std::string::npos) {
        m.propType = "float";
        m.defaultValue = "0.0";
        return m;
    }
    if (cppType.find("bool") != std::string::npos) {
        m.propType = "bool";
        m.defaultValue = "false";
        return m;
    }
    if (cppType.find("int") != std::string::npos) {
        m.propType = "int";
        m.defaultValue = "0";
        return m;
    }
    if (cppType == "sol::table") {
        m.propType = "lua";
        m.defaultValue = "{}";
        return m;
    }
    m.propType = "lua";  // sol::object, void*, anything else: raw expression
    m.defaultValue = "nil";
    return m;
}

NodeDef buildManifestNode(const fse::Function& fn) {
    const bool entityScope = fn.scope == "Entity";
    NodeDef def;
    def.type = "fse." + fn.scope + "." + fn.name;
    def.label = fn.name;
    def.category = "action";  // plain call: linear codegen, {CHILDREN} follows
    def.description = fn.description;
    def.advanced = true;
    def.source = "fse-manifest";
    def.fseCall = fn.name;

    std::string args;
    for (size_t i = 0; i < fn.parameters.size(); ++i) {
        const auto& param = fn.parameters[i];
        const MappedParam m = mapManifestParam(param.type, entityScope, i);
        if (m.skipped) continue;
        PropertyDef prop;
        prop.name = param.name.empty() ? "arg" + std::to_string(i + 1) : param.name;
        prop.type = m.propType;
        prop.label = prop.name;
        prop.defaultValue = m.defaultValue;
        if (!args.empty()) args += ", ";
        const std::string hole = "{" + prop.name + "}";
        args += m.quoted ? "\"" + hole + "\"" : hole;
        def.properties.push_back(std::move(prop));
    }
    const std::string receiver = entityScope ? "Me" : "Quest";
    def.codeTemplate = receiver + ":" + fn.name + "(" + args + ")\n{CHILDREN}";
    return def;
}

} // namespace

Registry Registry::curated() {
    Registry registry;
    registry.nodes_ = curatedDefs();
    registry.curatedCount_ = registry.nodes_.size();
    return registry;
}

Registry Registry::withManifest(const fse::Manifest& manifest) {
    Registry registry = curated();
    std::set<std::string> taken;
    for (const auto& def : registry.nodes_) taken.insert(def.type);
    for (const auto& fn : manifest.functions) {
        NodeDef def = buildManifestNode(fn);
        if (taken.count(def.type)) continue;  // curated wins on collision
        taken.insert(def.type);
        registry.nodes_.push_back(std::move(def));
    }
    return registry;
}

const NodeDef* Registry::find(std::string_view type) const {
    for (const auto& def : nodes_) {
        if (def.type == type) return &def;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Graph loading

Graph loadGraph(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("questnodes: cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadGraphText(buffer.str(), path.string());
}

Graph loadGraphText(const std::string& text, const std::string& sourceName) {
    // ordered_json keeps property authoring order (FQT substitutes config
    // values in insertion order).
    ordered_json root = ordered_json::parse(text, nullptr, false);
    if (root.is_discarded()) {
        const std::string_view sv(text);
        const std::string_view stripped =
            sv.size() >= 3 && sv.substr(0, 3) == "\xEF\xBB\xBF" ? sv.substr(3) : sv;
        root = ordered_json::parse(stripped, nullptr, false);
    }
    if (root.is_discarded() || !root.is_object()) {
        throw std::runtime_error("questnodes: malformed graph JSON in " +
                                 (sourceName.empty() ? std::string("<text>") : sourceName));
    }

    Graph graph;
    graph.questName = root.value("questName", std::string());
    graph.scriptName = root.value("scriptName", std::string());
    graph.makeBehavioral = root.value("makeBehavioral", false);
    graph.acquireControl = root.value("acquireControl", false);
    graph.exclusiveControl = root.value("exclusiveControl", false);
    graph.invulnerable = root.value("invulnerable", false);
    graph.unkillable = root.value("unkillable", false);
    graph.persistent = root.value("persistent", false);
    graph.killOnLevelUnload = root.value("killOnLevelUnload", false);

    if (const auto it = root.find("variables"); it != root.end() && it->is_array()) {
        for (const ordered_json& vj : *it) {
            GraphVariable var;
            var.name = vj.value("name", std::string());
            var.type = vj.value("type", std::string("String"));
            var.defaultValue = vj.contains("defaultValue")
                                   ? jsonValueToString(vj.at("defaultValue"))
                                   : std::string();
            var.exposed = vj.value("exposed", false);
            graph.variables.push_back(std::move(var));
        }
    }

    if (const auto it = root.find("nodes"); it != root.end() && it->is_array()) {
        for (const ordered_json& nj : *it) {
            GraphNode node;
            node.id = nj.value("id", std::string());
            node.type = nj.value("type", std::string());
            node.category = nj.value("category", std::string());
            node.x = nj.value("x", 0.0);
            node.y = nj.value("y", 0.0);
            if (const auto pj = nj.find("properties");
                pj != nj.end() && pj->is_object()) {
                for (const auto& [key, value] : pj->items()) {
                    node.properties.emplace_back(key, jsonValueToString(value));
                }
            }
            if (node.id.empty() || node.type.empty()) {
                throw std::runtime_error(
                    "questnodes: graph node missing id or type in " + sourceName);
            }
            graph.nodes.push_back(std::move(node));
        }
    }

    if (const auto it = root.find("connections"); it != root.end() && it->is_array()) {
        for (const ordered_json& cj : *it) {
            GraphConnection conn;
            conn.from = cj.value("from", std::string());
            conn.to = cj.value("to", std::string());
            conn.fromPort = cj.value("fromPort", std::string());
            graph.connections.push_back(std::move(conn));
        }
    }

    if (const auto it = root.find("functions"); it != root.end() && it->is_array()) {
        for (const ordered_json& fj : *it) {
            GraphFunction function;
            function.name = fj.value("name", std::string());
            if (function.name.empty())
                throw std::runtime_error("questnodes: function missing name in " + sourceName);
            if (const auto nodes = fj.find("nodes"); nodes != fj.end() && nodes->is_array()) {
                for (const ordered_json& nj : *nodes) {
                    GraphNode node;
                    node.id = nj.value("id", std::string());
                    node.type = nj.value("type", std::string());
                    node.category = nj.value("category", std::string());
                    node.x = nj.value("x", 0.0);
                    node.y = nj.value("y", 0.0);
                    if (const auto props = nj.find("properties");
                        props != nj.end() && props->is_object())
                        for (const auto& [key, value] : props->items())
                            node.properties.emplace_back(key, jsonValueToString(value));
                    if (node.id.empty() || node.type.empty())
                        throw std::runtime_error("questnodes: function node missing id or type in " + sourceName);
                    function.nodes.push_back(std::move(node));
                }
            }
            if (const auto connections = fj.find("connections");
                connections != fj.end() && connections->is_array()) {
                for (const ordered_json& cj : *connections) {
                    GraphConnection connection;
                    connection.from = cj.value("from", std::string());
                    connection.to = cj.value("to", std::string());
                    connection.fromPort = cj.value("fromPort", std::string());
                    function.connections.push_back(std::move(connection));
                }
            }
            graph.functions.push_back(std::move(function));
        }
    }

    return graph;
}

std::string saveGraphText(const Graph& graph) {
    ordered_json root;
    root["questName"] = graph.questName;
    root["scriptName"] = graph.scriptName;
    root["makeBehavioral"] = graph.makeBehavioral;
    root["acquireControl"] = graph.acquireControl;
    root["exclusiveControl"] = graph.exclusiveControl;
    root["invulnerable"] = graph.invulnerable;
    root["unkillable"] = graph.unkillable;
    root["persistent"] = graph.persistent;
    root["killOnLevelUnload"] = graph.killOnLevelUnload;

    ordered_json vars = ordered_json::array();
    for (const auto& v : graph.variables) {
        ordered_json vj;
        vj["name"] = v.name;
        vj["type"] = v.type;
        vj["defaultValue"] = v.defaultValue;
        vj["exposed"] = v.exposed;
        vars.push_back(std::move(vj));
    }
    root["variables"] = std::move(vars);

    ordered_json nodes = ordered_json::array();
    for (const auto& n : graph.nodes) {
        ordered_json nj;
        nj["id"] = n.id;
        nj["type"] = n.type;
        if (!n.category.empty()) nj["category"] = n.category;
        nj["x"] = n.x;
        nj["y"] = n.y;
        if (!n.properties.empty()) {
            ordered_json props = ordered_json::object();
            for (const auto& [k, val] : n.properties) props[k] = val;
            nj["properties"] = std::move(props);
        }
        nodes.push_back(std::move(nj));
    }
    root["nodes"] = std::move(nodes);

    ordered_json conns = ordered_json::array();
    for (const auto& c : graph.connections) {
        ordered_json cj;
        cj["from"] = c.from;
        cj["to"] = c.to;
        if (!c.fromPort.empty()) cj["fromPort"] = c.fromPort;
        conns.push_back(std::move(cj));
    }
    root["connections"] = std::move(conns);

    ordered_json functions = ordered_json::array();
    for (const auto& function : graph.functions) {
        ordered_json fj;
        fj["name"] = function.name;
        ordered_json functionNodes = ordered_json::array();
        for (const auto& n : function.nodes) {
            ordered_json nj;
            nj["id"] = n.id;
            nj["type"] = n.type;
            if (!n.category.empty()) nj["category"] = n.category;
            nj["x"] = n.x;
            nj["y"] = n.y;
            if (!n.properties.empty()) {
                ordered_json props = ordered_json::object();
                for (const auto& [key, value] : n.properties) props[key] = value;
                nj["properties"] = std::move(props);
            }
            functionNodes.push_back(std::move(nj));
        }
        fj["nodes"] = std::move(functionNodes);
        ordered_json functionConnections = ordered_json::array();
        for (const auto& connection : function.connections) {
            ordered_json cj;
            cj["from"] = connection.from;
            cj["to"] = connection.to;
            if (!connection.fromPort.empty()) cj["fromPort"] = connection.fromPort;
            functionConnections.push_back(std::move(cj));
        }
        fj["connections"] = std::move(functionConnections);
        functions.push_back(std::move(fj));
    }
    root["functions"] = std::move(functions);

    return root.dump(2);
}

// ---------------------------------------------------------------------------
// Compiler (FQT CodeGenerator port)

namespace {

class Compiler {
public:
    Compiler(const Graph& graph, const Registry& registry)
        : graph_(graph), registry_(registry) {
        for (const auto& var : graph.variables) {
            if (var.exposed && !graph.scriptName.empty() && !var.name.empty()) {
                exposedTypes_[graph.scriptName + "." + var.name] = var.type;
            }
        }
    }

    std::string behaviorCode(int indent) {
        if (graph_.nodes.empty()) {
            return indentOf(indent) + "-- [FQT-CG-002] No behavior nodes defined\n";
        }
        std::vector<const GraphNode*> triggers;
        for (const auto& node : graph_.nodes) {
            if (effectiveCategory(node) == "trigger") triggers.push_back(&node);
        }
        if (triggers.empty()) {
            return indentOf(indent) + "-- [FQT-CG-003] No trigger nodes found\n";
        }
        std::string out;
        for (const GraphNode* trigger : triggers) {
            out += nodeCode(*trigger, indent);
        }
        return out;
    }

    std::string entryCode(int indent) {
        std::set<std::string> hasIncoming;
        for (const auto& connection : graph_.connections)
            hasIncoming.insert(connection.to);
        std::string out;
        for (const auto& node : graph_.nodes)
            if (node.type != "comment" && !hasIncoming.count(node.id)) out += nodeCode(node, indent);
        return out;
    }

    std::string functionDefinitions() {
        std::string out;
        for (const auto& function : graph_.functions) {
            Graph body = graph_;
            body.nodes = function.nodes;
            body.connections = function.connections;
            Compiler nested(body, registry_);
            out += "local function " + buildLuaFunctionName(function.name) + "()\n";
            const std::string code = nested.entryCode(1);
            out += code.empty() ? "    -- empty function\n" : code;
            out += "end\n\n";
        }
        return out;
    }

private:
    const Graph& graph_;
    const Registry& registry_;
    std::map<std::string, std::string> exposedTypes_;  // scriptName.var -> type
    std::deque<NodeDef> synthesized_;  // stable refs for variable-node defs

    const GraphNode* findNode(const std::string& id) const {
        for (const auto& node : graph_.nodes) {
            if (node.id == id) return &node;
        }
        return nullptr;
    }

    std::vector<const GraphConnection*> connectionsFrom(const std::string& id) const {
        std::vector<const GraphConnection*> out;
        for (const auto& conn : graph_.connections) {
            if (conn.from == id) out.push_back(&conn);
        }
        return out;
    }

    const GraphVariable* findVariable(std::string_view name) const {
        for (const auto& var : graph_.variables) {
            if (iequals(var.name, name)) return &var;
        }
        return nullptr;
    }

    std::string effectiveCategory(const GraphNode& node) const {
        if (!node.category.empty()) return node.category;
        if (const NodeDef* def = registry_.find(node.type)) return def->category;
        return {};
    }

    // ---- variable support (FQT BuildLuaVariableName & friends) ----

    static std::string buildLuaVariableName(std::string_view name) {
        if (name.empty() ||
            std::all_of(name.begin(), name.end(), [](unsigned char c) {
                return std::isspace(c) != 0;
            })) {
            return "var_unnamed";
        }
        std::string normalized;
        for (char c : name) {
            const auto uc = static_cast<unsigned char>(c);
            normalized += (uc <= 127 && (std::isalnum(uc) || c == '_')) ? c : '_';
        }
        if (std::isdigit(static_cast<unsigned char>(normalized[0]))) {
            normalized.insert(normalized.begin(), '_');
        }
        return "var_" + normalized;
    }

    static std::string buildLuaFunctionName(std::string_view name) {
        std::string result = buildLuaVariableName(name);
        result.replace(0, 4, "fn_");
        return result;
    }

    static std::string exposedGetExpr(const std::string& key, const std::string& type) {
        const std::string k = escapeLua(key);
        if (type == "Boolean") return "Quest:GetStateBool(\"" + k + "\")";
        if (type == "Integer") return "Quest:GetStateInt(\"" + k + "\")";
        if (type == "Float") return "tonumber(Quest:GetStateString(\"" + k + "\"))";
        return "Quest:GetStateString(\"" + k + "\")";  // Object/String/default
    }

    static std::string exposedSetExpr(const std::string& key, const std::string& type,
                                      const std::string& valueExpr) {
        const std::string k = escapeLua(key);
        if (type == "Boolean") return "Quest:SetStateBool(\"" + k + "\", " + valueExpr + ")";
        if (type == "Integer") return "Quest:SetStateInt(\"" + k + "\", " + valueExpr + ")";
        return "Quest:SetStateString(\"" + k + "\", tostring(" + valueExpr + "))";
    }

    static std::string mapVariableTypeToNodeType(const std::string& type) {
        if (type == "Boolean") return "bool";
        if (type == "Integer") return "int";
        if (type == "Float") return "float";
        if (type == "Object") return "object";
        return "string";
    }

    bool resolveVariableReference(const std::string& value, std::string& luaName) const {
        if (value.empty() || value[0] != '$') return false;
        const bool external = value.rfind("$@", 0) == 0;
        std::string name = value.substr(external ? 2 : 1);
        // trim
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
            name.erase(name.begin());
        while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
            name.pop_back();
        if (name.empty()) return false;

        if (external) {
            const auto it = exposedTypes_.find(name);
            luaName = exposedGetExpr(name, it != exposedTypes_.end() ? it->second
                                                                     : std::string("String"));
            return true;
        }
        const GraphVariable* var = findVariable(name);
        if (var == nullptr) return false;
        if (var->exposed) {
            const std::string key = graph_.scriptName + "." + name;
            luaName = exposedGetExpr(key, var->type);
            return true;
        }
        luaName = buildLuaVariableName(name);
        return true;
    }

    // FQT TryBuildVariableNodeDefinition: synthesize defs for var_get_/var_set_
    // node types (and their _ext external variants).
    const NodeDef* tryBuildVariableNode(const GraphNode& node) {
        constexpr std::string_view getPrefix = "var_get_";
        constexpr std::string_view setPrefix = "var_set_";
        constexpr std::string_view getExtPrefix = "var_get_ext";
        constexpr std::string_view setExtPrefix = "var_set_ext";

        auto hasProp = [&](std::string_view key) {
            return std::any_of(node.properties.begin(), node.properties.end(),
                               [&](const auto& p) { return p.first == key; });
        };
        auto propOr = [&](std::string_view key, std::string fallback) {
            for (const auto& p : node.properties) {
                if (p.first == key) return p.second;
            }
            return fallback;
        };

        const bool external = istartsWith(node.type, getExtPrefix) ||
                              istartsWith(node.type, setExtPrefix) ||
                              hasProp("extEntity");
        if (!external && !istartsWith(node.type, getPrefix) &&
            !istartsWith(node.type, setPrefix)) {
            return nullptr;
        }

        NodeDef def;
        def.type = node.type;
        def.category = "variable";
        def.source = "curated";

        if (external) {
            const std::string extEntity = propOr("extEntity", "");
            const std::string extVariable = propOr("extVariable", "");
            const std::string extType = propOr("extType", "String");
            const std::string key = extEntity + "." + extVariable;
            const std::string nodeType = mapVariableTypeToNodeType(extType);
            const bool isSet = istartsWith(node.type, setExtPrefix);
            if (!isSet) {
                def.label = "Get " + key;
                def.description = "Gets exposed variable '" + key + "'";
                def.codeTemplate =
                    "local var_value = " + exposedGetExpr(key, extType) + "\n{CHILDREN}";
            } else {
                def.label = "Set " + key;
                def.description = "Sets exposed variable '" + key + "'";
                const std::string valueTemplate =
                    nodeType == "string" ? "\"{value}\"" : "{value}";
                def.codeTemplate =
                    exposedSetExpr(key, extType, valueTemplate) + "\n{CHILDREN}";
                def.properties.push_back({"value", nodeType, "Value", "", ""});
            }
            synthesized_.push_back(std::move(def));
            return &synthesized_.back();
        }

        const bool isGet = istartsWith(node.type, getPrefix);
        const std::string variableName =
            node.type.substr(isGet ? getPrefix.size() : setPrefix.size());
        const GraphVariable* variable = findVariable(variableName);
        if (variable == nullptr) return nullptr;

        const std::string variableType = variable->type.empty() ? "String" : variable->type;
        const std::string luaName = buildLuaVariableName(variableName);
        const std::string nodeType = mapVariableTypeToNodeType(variableType);
        const std::string key = graph_.scriptName + "." + variableName;

        if (isGet) {
            def.label = "Get " + variableName;
            def.description = "Gets the value of variable '" + variableName + "'";
            const std::string getExpr =
                variable->exposed ? exposedGetExpr(key, variableType) : luaName;
            def.codeTemplate = "local " + luaName + "_value = " + getExpr + "\n{CHILDREN}";
        } else {
            def.label = "Set " + variableName;
            def.description = "Sets the value of variable '" + variableName + "'";
            const std::string valueTemplate =
                nodeType == "string" ? "\"{value}\"" : "{value}";
            def.codeTemplate =
                (variable->exposed ? exposedSetExpr(key, variableType, valueTemplate)
                                   : luaName + " = " + valueTemplate) +
                "\n{CHILDREN}";
            def.properties.push_back({"value", nodeType, "Value",
                                      variable->defaultValue, ""});
        }
        synthesized_.push_back(std::move(def));
        return &synthesized_.back();
    }

    // ---- template expansion (FQT ProcessNodeTemplate) ----

    static void replacePlaceholderWithVariable(std::string& code,
                                               const std::string& propName,
                                               const std::string& luaName) {
        replaceAll(code, "\"{" + propName + "}\"", luaName);
        replaceAll(code, "{" + propName + "}", luaName);
    }

    std::string processTemplate(const NodeDef& def, const GraphNode& node) const {
        std::string code = def.codeTemplate;
        replaceAll(code, "{QUEST_NAME}", escapeLua(graph_.questName));

        auto propType = [&](std::string_view name) -> const std::string* {
            for (const auto& p : def.properties) {
                if (iequals(p.name, name)) return &p.type;
            }
            return nullptr;
        };

        // Values set on the node, in authoring order.
        for (const auto& [key, rawValue] : node.properties) {
            const std::string placeholder = "{" + key + "}";
            std::string value = rawValue;
            std::string luaName;
            if (resolveVariableReference(value, luaName)) {
                replacePlaceholderWithVariable(code, key, luaName);
                continue;
            }
            if (const std::string* type = propType(key);
                type != nullptr && (*type == "text" || *type == "string")) {
                value = escapeLua(value);
            }
            replaceAll(code, placeholder, value);
        }

        // Definition defaults for anything still unfilled.
        for (const auto& prop : def.properties) {
            const std::string placeholder = "{" + prop.name + "}";
            if (code.find(placeholder) == std::string::npos) continue;
            std::string value = prop.defaultValue;
            std::string luaName;
            if (resolveVariableReference(value, luaName)) {
                replacePlaceholderWithVariable(code, prop.name, luaName);
                continue;
            }
            if (prop.type == "text" || prop.type == "string") {
                value = escapeLua(value);
            }
            replaceAll(code, placeholder, value);
        }

        return code;
    }

    // ---- node emission (FQT GenerateNodeCode / GenerateStructuredNode) ----

    // FQT BuildActionQueueCode: entity-thread-safe state-queue push.
    std::string actionQueueCode(const GraphNode& node) const {
        std::string actionType = "Highlight";
        if (istartsWith(node.type, "clearQuestTargetHighlight")) {
            actionType = "ClearHighlight";
        } else if (istartsWith(node.type, "showMinimapMarker")) {
            actionType = "ShowMarker";
        } else if (istartsWith(node.type, "hideMinimapMarker")) {
            actionType = "HideMarker";
        }

        std::string targetName = graph_.scriptName;
        std::string markerName = graph_.scriptName;
        for (const auto& [key, value] : node.properties) {
            if (key == "targetScriptName") {
                targetName = value;
                markerName = value;
            }
        }
        for (const auto& [key, value] : node.properties) {
            if (key == "markerName" && !isBlank(value)) markerName = value;
        }
        targetName = escapeLua(targetName);
        markerName = escapeLua(markerName);

        return "local actionId = Quest:GetStateInt(\"FQT_ActionCounter\")\n"
               "if actionId == nil then actionId = 0 end\n"
               "actionId = actionId + 1\n"
               "Quest:SetStateInt(\"FQT_ActionCounter\", actionId)\n"
               "Quest:SetStateString(\"FQT_Action_\" .. tostring(actionId), \"" +
               actionType + "|" + targetName + "|" + markerName + "\")";
    }

    std::string nodeCode(const GraphNode& node, int indent) {
        if (node.type == "reroute") {
            std::string out;
            for (const GraphConnection* conn : connectionsFrom(node.id))
                if (const GraphNode* child = findNode(conn->to)) out += nodeCode(*child, indent);
            return out;
        }
        if (node.type == "functionCall") {
            std::string functionName;
            for (const auto& property : node.properties)
                if (property.first == "function") functionName = property.second;
            const auto function = std::find_if(graph_.functions.begin(), graph_.functions.end(),
                [&](const auto& candidate) { return candidate.name == functionName; });
            std::string out;
            if (function == graph_.functions.end()) {
                out = indentOf(indent) + "-- [FQT-CG-004] Unknown function: " + functionName + "\n";
            } else {
                out = indentOf(indent) + buildLuaFunctionName(functionName) + "()\n";
            }
            for (const GraphConnection* conn : connectionsFrom(node.id))
                if (const GraphNode* child = findNode(conn->to)) out += nodeCode(*child, indent);
            return out;
        }
        const NodeDef* def = registry_.find(node.type);
        if (def == nullptr) def = tryBuildVariableNode(node);
        if (def == nullptr) {
            return indentOf(indent) + "-- [FQT-CG-001] Unknown node type: " +
                   node.type + "\n";
        }

        std::string out;

        if (isActionQueueType(node.type)) {
            for (const std::string& line : splitLines(actionQueueCode(node))) {
                if (!isBlank(line)) out += indentOf(indent) + line + "\n";
            }
            for (const GraphConnection* conn : connectionsFrom(node.id)) {
                if (const GraphNode* child = findNode(conn->to)) {
                    out += nodeCode(*child, indent);
                }
            }
            return out;
        }

        const bool linearAction = def->category == "action" && !def->hasBranching;
        if (linearAction) {
            // Emit this node's code, then children at the SAME indent (linear
            // sequencing, not nesting).
            std::string code = processTemplate(*def, node);
            replaceAll(code, "{CHILDREN}", "");
            code = trimTrailingWhitespace(std::move(code));
            for (const std::string& line : splitLines(code)) {
                if (!isBlank(line)) out += indentOf(indent) + line + "\n";
            }
            for (const GraphConnection* conn : connectionsFrom(node.id)) {
                if (const GraphNode* child = findNode(conn->to)) {
                    out += nodeCode(*child, indent);
                }
            }
            return out;
        }

        return structuredNode(node, *def, indent);
    }

    std::string structuredNode(const GraphNode& node, const NodeDef& def, int indent) {
        std::string code = processTemplate(def, node);
        const auto connections = connectionsFrom(node.id);

        if (def.hasBranching) {
            std::vector<std::string> labels = def.branchLabels;
            if (labels.empty()) labels = {"True", "False"};
            std::map<std::string, std::string> branches;
            for (const auto& label : labels) branches[label];
            for (const GraphConnection* conn : connections) {
                if (conn->fromPort.empty()) continue;
                const GraphNode* child = findNode(conn->to);
                if (child == nullptr) continue;
                const auto it = branches.find(conn->fromPort);
                if (it != branches.end()) {
                    it->second += nodeCode(*child, indent + 1);
                }
            }
            for (const auto& label : labels) {
                const std::string content =
                    branches[label].empty()
                        ? indentOf(indent + 1) + "-- " + lowered(label) + " branch"
                        : trimTrailingNewlines(branches[label]);
                replaceAll(code, "{" + label + "}", content);
                replaceAll(code, "{" + uppered(label) + "}", content);
            }
        } else if (!connections.empty()) {
            std::string children;
            for (const GraphConnection* conn : connections) {
                if (const GraphNode* child = findNode(conn->to)) {
                    children += nodeCode(*child, indent + 1);
                }
            }
            replaceAll(code, "{CHILDREN}", trimTrailingNewlines(children));
        } else {
            replaceAll(code, "{CHILDREN}", "");
        }

        // Lines already indented by child substitution are emitted as-is;
        // template lines get this node's indent.
        std::string out;
        for (const std::string& line : splitLines(code)) {
            if (isBlank(line)) continue;
            if (line[0] == ' ' || line[0] == '\t') {
                out += line + "\n";
            } else {
                out += indentOf(indent) + line + "\n";
            }
        }
        return out;
    }
};

// FSE-manifest warning pass: every emitted Quest:/Me: call should resolve to a
// manifest function (the manifest names carry overload suffixes that the Lua
// registration drops, so those alias back to their base name).
void validateCalls(const std::string& lua, const fse::Manifest& manifest,
                   std::vector<std::string>& warnings) {
    static const char* const kOverloadSuffixes[] = {"_NoPos", "_WithPos",
                                                    "_Blocking", "_NonBlocking"};
    std::map<std::string, std::set<std::string>> known;  // lua name -> scopes
    for (const auto& fn : manifest.functions) {
        known[fn.name].insert(fn.scope);
        for (const char* suffix : kOverloadSuffixes) {
            const std::string_view sv(fn.name);
            const std::string_view suf(suffix);
            if (sv.size() > suf.size() &&
                sv.substr(sv.size() - suf.size()) == suf) {
                known[fn.name.substr(0, sv.size() - suf.size())].insert(fn.scope);
            }
        }
    }

    std::set<std::string> reported;
    size_t pos = 0;
    while (pos < lua.size()) {
        size_t colon = lua.find(':', pos);
        if (colon == std::string::npos) break;
        pos = colon + 1;
        // receiver: identifier immediately before ':'
        size_t rs = colon;
        while (rs > 0 && (std::isalnum(static_cast<unsigned char>(lua[rs - 1])) ||
                          lua[rs - 1] == '_')) {
            --rs;
        }
        const std::string receiver = lua.substr(rs, colon - rs);
        if (receiver != "Quest" && receiver != "Me") continue;
        // method: identifier after ':', must be followed by '('
        size_t me = colon + 1;
        while (me < lua.size() && (std::isalnum(static_cast<unsigned char>(lua[me])) ||
                                   lua[me] == '_')) {
            ++me;
        }
        if (me == colon + 1 || me >= lua.size() || lua[me] != '(') continue;
        const std::string method = lua.substr(colon + 1, me - colon - 1);
        const std::string key = receiver + ":" + method;
        if (reported.count(key)) continue;
        reported.insert(key);

        const auto it = known.find(method);
        if (it == known.end()) {
            warnings.push_back("call not in FSE manifest: " + key);
            continue;
        }
        const std::string wantScope = receiver == "Me" ? "Entity" : "Quest";
        if (!it->second.count(wantScope)) {
            warnings.push_back("scope mismatch for " + key + ": manifest lists it as " +
                               *it->second.begin() + "-scope");
        }
    }
}

} // namespace

std::string generateBehaviorCode(const Graph& graph, const Registry& registry,
                                 int indent) {
    Compiler compiler(graph, registry);
    return compiler.behaviorCode(indent);
}

CompileResult compileEntityScript(const Graph& graph, const Registry& registry,
                                  const CompileOptions& options) {
    CompileResult result;
    std::string& sb = result.lua;
    auto line = [&](std::string_view text = {}) {
        sb += text;
        sb += '\n';
    };

    line("-- " + graph.scriptName + ".lua");
    line("-- Entity script for " + graph.questName);
    line("-- Generated by " + options.generatorName);
    line("-- Generator: " + options.generatorStamp);
    line();
    line("local Quest = nil");
    line("local Me = nil");
    line();
    line("function Init(questObject, meObject)");
    line("    Quest = questObject");
    line("    Me = meObject");
    line("    Quest:Log(\"" + graph.scriptName + ": Init called\")");
    if (graph.makeBehavioral) line("    Me:MakeBehavioral()");
    // AcquireControl wins over TakeExclusiveControl (the latter breaks
    // SpeakAndWait), matching FQT.
    if (graph.acquireControl) {
        line("    Me:AcquireControl()");
    } else if (graph.exclusiveControl) {
        line("    Me:TakeExclusiveControl()");
    }
    line("end");
    line();
    if (!graph.functions.empty()) {
        Compiler compiler(graph, registry);
        sb += compiler.functionDefinitions();
    }
    line("function Main(questObject, meObject)");
    line("    Quest = questObject");
    line("    Me = meObject");
    line("    Quest:Log(\"" + graph.scriptName + ": Main started\")");
    line("    local hero = Quest:GetHero()");
    if (graph.invulnerable) line("    Quest:EntitySetAsDamageable(Me, false)");
    if (graph.unkillable) line("    Quest:EntitySetAsKillable(Me, false)");
    if (graph.persistent) line("    Quest:SetThingPersistent(Me, true)");
    if (graph.killOnLevelUnload) line("    Me:SetToKillOnLevelUnload(true)");
    line();

    if (!graph.nodes.empty()) {
        line("    -- Main behavior loop");
        line("    while true do");
        sb += generateBehaviorCode(graph, registry, 2);
        line();
        line("        if Me:IsNull() then break end");
        line("        if not Quest:NewScriptFrame(Me) then break end");
        line("    end");
    } else if (graph.acquireControl || graph.exclusiveControl) {
        line("    -- Minimal behavior loop (add behavior nodes for triggers and actions)");
        line("    while true do");
        line("        -- Add trigger nodes in the visual editor to respond to hero interactions");
        line("        if Me:IsNull() then break end");
        line("        if not Quest:NewScriptFrame(Me) then break end");
        line("    end");
    }

    line();
    line("    Me:ReleaseControl()");
    line("end");

    if (options.validateAgainst != nullptr) {
        validateCalls(result.lua, *options.validateAgainst, result.warnings);
    }
    return result;
}

} // namespace forge::questnodes
