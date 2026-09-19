// forge::questproject -- whole-quest Lua compiler. Byte-compatible with FQT's
// CodeGenerator.GenerateQuestScript (see questproject.hpp). Structure mirrors FQT:
// header, Init, Main, OnPersist, then thread bodies (EntitySpawner if entities
// spawn/target, FQT_StartScreen if the start screen is enabled,
// MonitorQuestCompletion always, then user threads).

#include "forge/questproject.hpp"

#include <cctype>
#include <sstream>

#include "forge/qst.hpp"

namespace forge::questproject {

namespace {

std::string luaEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

std::string lowerBool(const std::string& v) {
    std::string out = v.empty() ? "false" : v;
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string defaultForType(const std::string& type) {
    if (type == "int") return "0";
    if (type == "float") return "0";
    if (type == "string") return "";
    return "false";  // bool
}

// FQT AppendRuntimeQuestCardConfiguration: objective + gold + renown, at `indent`.
void appendCardConfig(std::string& sb, const QuestProject& q, const char* indent) {
    if (q.questCardObject.empty()) return;
    if (!q.objectiveText.empty()) {
        const std::string region1 =
            !q.objectiveRegion1.empty() ? q.objectiveRegion1
            : (!q.regions.empty() ? q.regions.front() : std::string());
        sb += indent;
        sb += "Quest:SetQuestCardObjective(\"" + q.name + "\", \"" + luaEscape(q.objectiveText) +
              "\", \"" + region1 + "\", \"" + q.objectiveRegion2 + "\")\n";
    }
    if (q.rewards.gold > 0) {
        sb += indent;
        sb += "Quest:SetQuestGoldReward(\"" + q.name + "\", " + std::to_string(q.rewards.gold) + ")\n";
    }
    if (q.rewards.renown > 0) {
        sb += indent;
        sb += "Quest:SetQuestRenownReward(\"" + q.name + "\", " + std::to_string(q.rewards.renown) + ")\n";
    }
}

// FQT: quest.Regions.FirstOrDefault() ?? "Oakvale" (thread region binding).
std::string threadRegion(const QuestProject& q) {
    return q.regions.empty() ? std::string("Oakvale") : q.regions.front();
}

bool needsContainerEntity(const QuestProject& q) {
    return q.rewards.container && !q.rewards.container->items.empty() &&
           !q.rewards.container->autoGiveOnComplete;
}

bool spawns(const QuestEntity& e) { return e.spawnMethod != SpawnMethod::BindExisting; }
bool targets(const QuestEntity& e) { return e.isQuestTarget || e.showOnMinimap; }

bool needsEntitySpawner(const QuestProject& q) {
    for (const auto& e : q.entities)
        if (spawns(e) || targets(e)) return true;
    return false;
}

// FQT GenerateCreateCreatureAtMarker / GenerateCreateObjectAtMarker. Returns the
// multi-line block WITHOUT a trailing newline (caller adds "\n\n").
std::string spawnAtMarker(const QuestEntity& e) {
    const std::string& s = e.scriptName;
    const bool obj = e.entityType == EntityType::Object;
    const char* call = obj ? "CreateObject" : "CreateCreature";
    const char* noun = obj ? "object" : "entity";
    std::string b;
    b += "    local marker_" + s + " = Quest:GetThingWithScriptName(\"" + e.spawnMarker + "\")\n";
    b += "    local pos_" + s + " = nil\n";
    b += "    if marker_" + s + " ~= nil and marker_" + s + ".GetPos ~= nil then\n";
    b += "        local ok, result = pcall(function() return marker_" + s + ":GetPos() end)\n";
    b += "        if ok then\n";
    b += "            pos_" + s + " = result\n";
    b += "        end\n";
    b += "    end\n";
    b += "    if pos_" + s + " == nil then\n";
    b += "        local hero = Quest:GetHero()\n";
    b += "        if hero ~= nil and hero.GetPos ~= nil then\n";
    b += "            local ok, result = pcall(function() return hero:GetPos() end)\n";
    b += "            if ok then\n";
    b += "                pos_" + s + " = result\n";
    b += "            end\n";
    b += "        end\n";
    b += "    end\n";
    b += "    if pos_" + s + " ~= nil then\n";
    b += "        pcall(function() Quest:" + std::string(call) + "(\"" + e.defName + "\", pos_" + s +
         ", \"" + s + "\") end)\n";
    b += "    else\n";
    b += "        Quest:Log(\"FQT: Failed to resolve spawn position for " + s + "\")\n";
    b += "    end\n";
    b += "\n";
    b += "    -- Brief pause to allow " + std::string(noun) + " to fully spawn\n";
    b += "    Quest:Pause(0.1)\n";
    b += "    if not Quest:NewScriptFrame() then return end";
    return b;
}

// FQT GenerateCreateCreatureAtPosition / GenerateCreateObjectAtPosition.
std::string spawnAtPosition(const QuestEntity& e) {
    const std::string& s = e.scriptName;
    const bool obj = e.entityType == EntityType::Object;
    const char* call = obj ? "CreateObject" : "CreateCreature";
    std::ostringstream x, y, z;
    x << e.spawnX;
    y << e.spawnY;
    z << e.spawnZ;
    std::string b;
    b += "    local pos_" + s + " = {x=" + x.str() + ", y=" + y.str() + ", z=" + z.str() + "}\n";
    b += "    local " + s + " = Quest:" + std::string(call) + "(\"" + e.defName + "\", pos_" + s +
         ", \"" + s + "\")\n";
    b += "    Quest:Pause(0.1)\n";
    b += "    if not Quest:NewScriptFrame() then return end";
    return b;
}

std::string spawnEntityCode(const QuestEntity& e) {
    if (e.spawnMethod == SpawnMethod::BindExisting) return {};
    if (e.spawnMethod == SpawnMethod::AtPosition) return spawnAtPosition(e);
    // AtMarker, CreateCreature, and (best-effort) OnEntity route through the marker
    // spawn. OnEntity is not yet fixture-verified.
    return spawnAtMarker(e);
}

// FQT GenerateContainerSpawnPosition, at `indent`.
void containerSpawnPosition(std::string& sb, const ContainerReward& c, const char* indent) {
    switch (c.spawnLocation) {
        case ContainerSpawnLocation::NearMarker:
            sb += indent; sb += "local marker = Quest:GetThingWithScriptName(\"" + c.spawnReference + "\")\n";
            sb += indent; sb += "local containerPos\n";
            sb += indent; sb += "if marker ~= nil then\n";
            sb += indent; sb += "    containerPos = marker:GetPos()\n";
            sb += indent; sb += "else\n";
            sb += indent; sb += "    local hero = Quest:GetHero()\n";
            sb += indent; sb += "    containerPos = hero:GetPos()\n";
            sb += indent; sb += "end\n";
            break;
        case ContainerSpawnLocation::NearEntity:
            sb += indent; sb += "local entity = Quest:GetThingWithScriptName(\"" + c.spawnReference + "\")\n";
            sb += indent; sb += "local containerPos\n";
            sb += indent; sb += "if entity ~= nil then\n";
            sb += indent; sb += "    containerPos = entity:GetPos()\n";
            sb += indent; sb += "else\n";
            sb += indent; sb += "    local hero = Quest:GetHero()\n";
            sb += indent; sb += "    containerPos = hero:GetPos()\n";
            sb += indent; sb += "end\n";
            break;
        case ContainerSpawnLocation::FixedPosition: {
            std::ostringstream x, y, z;
            x << c.x; y << c.y; z << c.z;
            sb += indent;
            sb += "local containerPos = {x=" + x.str() + ", y=" + y.str() + ", z=" + z.str() + "}\n";
            break;
        }
    }
}

// FQT GenerateTraditionalContainerReward, emitted inside MonitorQuestCompletion
// (12-space base indent). Starts with a blank line like FQT.
void generateContainerReward(std::string& sb, const QuestProject& q) {
    const ContainerReward& c = *q.rewards.container;
    sb += "\n";
    sb += "            -- Container reward: Spawn and populate container\n";
    sb += "            Quest:Log(\"Attempting to spawn container...\")\n";
    containerSpawnPosition(sb, c, "            ");
    sb += "            Quest:Log(\"Creating container: " + c.containerDefName + " at position...\")\n";
    sb += "            local container = Quest:CreateObject(\"" + c.containerDefName +
          "\", containerPos, \"" + c.containerScriptName + "\")\n";
    sb += "            Quest:Log(\"CreateObject returned: \" .. tostring(container))\n";
    sb += "            if container ~= nil then\n";
    sb += "                Quest:Log(\"Container spawned successfully!\")\n";
    sb += "                Quest:EntitySetTargetable(container, true)\n";
    sb += "                Quest:Log(\"Container set as targetable\")\n";
    if (c.autoGiveOnComplete) {
        for (const auto& item : c.items)
            sb += "                Quest:GiveHeroObject(\"" + item + "\", 1)\n";
    } else {
        sb += "                -- Container uses entity script for multi-item rewards on manual open\n";
        if (c.highlightContainer)
            sb += "                Quest:SetThingHasInformation(container, true)\n";
    }
    sb += "            else\n";
    sb += "                Quest:Log(\"WARNING: Failed to spawn container!\")\n";
    sb += "            end\n";
}

}  // namespace

questnodes::CompileResult compileQuestScript(const QuestProject& q,
                                             const QuestCompileOptions& opt) {
    questnodes::CompileResult result;
    std::string& sb = result.lua;
    auto line = [&](const std::string& t = std::string()) { sb += t; sb += '\n'; };

    // ---- Header ----
    line("-- " + q.name + ".lua");
    line("-- Generated by " + opt.generatorName);
    line("-- Generator: " + opt.generatorStamp);
    line();
    line("Quest = nil");
    line();

    // ---- Init ----
    line("function Init(questObject)");
    line("    Quest = questObject");
    line("    Quest:Log(\"" + q.name + ": Init phase started.\")");
    for (const auto& region : q.regions)
        line("    Quest:AddQuestRegion(\"" + q.name + "\", \"" + region + "\")");
    line("    Quest:SetQuestWorldMapOffset(\"" + q.name + "\", " +
         std::to_string(q.worldMapOffsetX) + ", " + std::to_string(q.worldMapOffsetY) + ")");
    if (!q.questCardObject.empty()) {
        line("    -- Configure quest card");
        if (q.isGuildQuest)
            line("    Quest:AddGuildQuestCard(\"" + q.questCardObject + "\", \"" + q.name +
                 "\", false, false)");
        else
            line("    Quest:AddQuestCard(\"" + q.questCardObject + "\", \"" + q.name +
                 "\", false, false)");
        appendCardConfig(sb, q, "    ");
        line();
    }
    line("    Quest:SetStateBool(\"QuestCompleted\", false)");
    line("    Quest:SetStateBool(\"FQT_StartScreenShown\", false)");
    line(std::string("    Quest:SetStateBool(\"FQT_DebugStartScreen\", ") +
         (opt.startScreenDebug ? "true" : "false") + ")");
    line(std::string("    Quest:SetStateBool(\"FQT_DebugStartScreenBanner\", ") +
         (opt.startScreenDebugBanner ? "true" : "false") + ")");
    for (const auto& st : q.states) {
        const std::string val = st.defaultValue.empty() ? defaultForType(st.type) : st.defaultValue;
        if (st.type == "int")
            line("    Quest:SetStateInt(\"" + st.name + "\", " + val + ")");
        else if (st.type == "float")
            line("    Quest:SetStateFloat(\"" + st.name + "\", " + val + ")");
        else if (st.type == "string")
            line("    Quest:SetStateString(\"" + st.name + "\", \"" + val + "\")");
        else
            line("    Quest:SetStateBool(\"" + st.name + "\", " + lowerBool(val) + ")");
    }
    line("end");
    line();

    // ---- Main ----
    const std::string region = threadRegion(q);
    line("function Main(questObject)");
    line("    Quest = questObject");
    line("    Quest:Log(\"" + q.name + ": Main() started.\")");
    line("    Quest:Log(\"" + opt.generatorStamp + ": generator active\")");
    line();
    if (!q.entities.empty() || needsContainerEntity(q)) {
        line("    -- Bind entity scripts");
        for (const auto& e : q.entities)
            line("    Quest:AddEntityBinding(\"" + e.scriptName + "\", \"" + q.name + "/Entities/" +
                 e.scriptName + "\")");
        if (needsContainerEntity(q)) {
            const auto& c = *q.rewards.container;
            line("    Quest:AddEntityBinding(\"" + c.containerScriptName + "\", \"" + q.name +
                 "/Entities/" + c.containerScriptName + "\")");
        }
        line("    Quest:FinalizeEntityBindings()");
        line();
    }
    line("    -- Start quest threads");
    if (q.useQuestStartScreen)
        line("    Quest:CreateThread(\"FQT_StartScreen\", {region=\"" + region + "\"})");
    if (needsEntitySpawner(q))
        line("    Quest:CreateThread(\"EntitySpawner\", {region=\"" + region + "\"})");
    line("    Quest:CreateThread(\"MonitorQuestCompletion\", {region=\"" + region + "\"})");
    for (const auto& th : q.threads)
        line("    Quest:CreateThread(\"" + th.functionName + "\", {region=\"" + th.region + "\"})");
    line("end");
    line();

    // ---- OnPersist ----
    line("function OnPersist(questObject, context)");
    line("    Quest = questObject");
    line("    Quest:PersistTransferBool(context, \"QuestCompleted\")");
    for (const auto& st : q.states) {
        if (!st.persist) continue;
        if (st.type == "int")
            line("    Quest:PersistTransferInt(context, \"" + st.name + "\")");
        else if (st.type == "float")
            line("    Quest:PersistTransferFloat(context, \"" + st.name + "\")");
        else if (st.type == "string")
            line("    Quest:PersistTransferString(context, \"" + st.name + "\")");
        else
            line("    Quest:PersistTransferBool(context, \"" + st.name + "\")");
    }
    line("end");
    line();

    // ---- EntitySpawner thread ----
    if (needsEntitySpawner(q)) {
        line("function EntitySpawner(questObject)");
        line("    Quest = questObject");
        line("    -- Thread is region-bound to " + region + " - FSE auto-waits for region load");
        line("    Quest:Log(\"" + q.name + ": EntitySpawner executing (region is loaded).\")");
        line();
        line("    -- Brief pause to let game settle after region load");
        line("    Quest:Pause(0.5)");
        line("    if not Quest:NewScriptFrame() then return end");
        line();
        bool anySpawn = false;
        for (const auto& e : q.entities) if (spawns(e)) { anySpawn = true; break; }
        if (anySpawn) {
            line("    -- Spawn entities");
            for (const auto& e : q.entities) {
                if (!spawns(e)) continue;
                const std::string code = spawnEntityCode(e);
                if (!code.empty()) { sb += code; sb += "\n\n"; }
            }
        }
        bool anyTarget = false;
        for (const auto& e : q.entities) if (targets(e)) { anyTarget = true; break; }
        if (anyTarget) {
            line("    -- Set up quest target highlighting and minimap markers");
            for (const auto& e : q.entities) {
                if (!targets(e)) continue;
                line("    local " + e.scriptName + " = Quest:GetThingWithScriptName(\"" +
                     e.scriptName + "\")");
                line("    if " + e.scriptName + " ~= nil then");
                if (e.isQuestTarget)
                    line("        Quest:SetThingHasInformation(" + e.scriptName + ", true)");
                if (e.showOnMinimap)
                    line("        Quest:MiniMapAddMarker(" + e.scriptName + ", \"" + e.scriptName + "\")");
                line("    end");
            }
            line();
        }
        line("    Quest:Log(\"" + q.name + ": EntitySpawner completed.\")");
        line("end");
        line();
    }

    // ---- FQT_StartScreen thread ----
    if (q.useQuestStartScreen) {
        line("function FQT_StartScreen(questObject)");
        line("    Quest = questObject");
        line("    local debugStart = Quest:GetStateBool(\"FQT_DebugStartScreen\")");
        line("    local alreadyShown = Quest:GetStateBool(\"FQT_StartScreenShown\")");
        line("    if alreadyShown == nil then alreadyShown = false end");
        line("    if alreadyShown then");
        line("        if debugStart then Quest:Log(\"FQT: start screen already shown\") end");
        line("        return");
        line("    end");
        line("    Quest:Pause(0.5)");
        line("    if not Quest:NewScriptFrame() then return end");
        line("    local questName = \"" + q.name + "\"");
        line("    local activeQuest = Quest:GetActiveQuestName()");
        line("    if debugStart then Quest:Log(\"FQT: active quest before start = \" .. tostring(activeQuest)) end");
        line("    if activeQuest == nil or activeQuest == \"\" or activeQuest ~= questName then");
        line("        Quest:ActivateQuest(questName)");
        line("        Quest:Pause(0.1)");
        line("        if not Quest:NewScriptFrame() then return end");
        line("        activeQuest = Quest:GetActiveQuestName()");
        line("        if debugStart then Quest:Log(\"FQT: active quest after activate = \" .. tostring(activeQuest)) end");
        line("    end");
        line("    Quest:SetHeroGuideShowsQuestCards(true)");
        appendCardConfig(sb, q, "    ");
        line("    if debugStart then Quest:Log(\"FQT: configured active quest card\") end");
        line("    Quest:Log(\"FQT: KickOffQuestStartScreen called\")");
        line("    Quest:KickOffQuestStartScreen(questName, true, true)");
        line("    if Quest:GetStateBool(\"FQT_DebugStartScreenBanner\") then");
        line("        Quest:AddScreenTitleMessage(\"FQT: Start screen invoked\", 3.0, true)");
        line("    end");
        line("    Quest:SetStateBool(\"FQT_StartScreenShown\", true)");
        line("    if debugStart then Quest:Log(\"FQT: start screen kicked off\") end");
        line("end");
        line();
    }

    // ---- MonitorQuestCompletion thread ----
    line("function MonitorQuestCompletion(questObject)");
    line("    Quest = questObject");
    line("    while true do");
    line("        if Quest:GetStateBool(\"QuestCompleted\") then");
    line("            Quest:Log(\"" + q.name + ": Quest completed, giving rewards...\")");
    line();
    {
        bool anyTarget = false;
        for (const auto& e : q.entities) if (targets(e)) { anyTarget = true; break; }
        if (anyTarget) {
            line("            -- Clear quest target highlighting and minimap markers");
            for (const auto& e : q.entities) {
                if (!targets(e)) continue;
                line("            local " + e.scriptName + " = Quest:GetThingWithScriptName(\"" +
                     e.scriptName + "\")");
                line("            if " + e.scriptName + " ~= nil and not " + e.scriptName +
                     ":IsNull() then");
                if (e.isQuestTarget)
                    line("                Quest:ClearThingHasInformation(" + e.scriptName + ")");
                if (e.showOnMinimap)
                    line("                Quest:MiniMapRemoveMarker(" + e.scriptName + ")");
                line("            end");
            }
            line();
        }
    }
    line("            -- Give rewards");
    if (q.rewards.gold > 0)
        line("            Quest:GiveHeroGold(" + std::to_string(q.rewards.gold) + ")");
    if (q.rewards.experience > 0)
        line("            Quest:GiveHeroExperience(" + std::to_string(q.rewards.experience) + ")");
    if (q.rewards.renown > 0)
        line("            Quest:GiveHeroRenownPoints(" + std::to_string(q.rewards.renown) + ")");
    if (q.rewards.morality != 0.0f) {
        std::ostringstream m;
        m << q.rewards.morality;
        line("            -- Give morality reward (engine uses 2000-point scale)");
        line("            Quest:GiveHeroMorality(" + m.str() + " / 2000)");
    }
    for (const auto& item : q.rewards.items)
        line("            Quest:GiveHeroObject(\"" + item + "\", 1)");
    if (q.rewards.container && !q.rewards.container->items.empty())
        generateContainerReward(sb, q);
    line();
    line("            -- Complete and deactivate quest");
    line("            Quest:SetQuestAsCompleted(\"" + q.name + "\", true, false, false)");
    line("            Quest:DeactivateQuestLater(\"" + q.name + "\", 10)");
    line("            break");
    line("        end");
    line("        Quest:Pause(0.5)");
    line("        if not Quest:NewScriptFrame() then break end");
    line("    end");
    line("end");
    line();

    // ---- User threads ----
    for (const auto& th : q.threads) {
        line("function " + th.functionName + "(questObject)");
        line("    Quest = questObject");
        if (!th.description.empty()) line("    -- " + th.description);
        line("    -- TODO: Implement thread logic");
        line("end");
        line();
    }

    return result;
}

std::string questRegistrationSnippet(const QuestProject& q) {
    std::string sb;
    sb += q.name + " = {\n";
    sb += "    name = \"" + q.name + "\",\n";
    sb += "    file = \"" + q.name + "/" + q.name + "\",\n";
    sb += "    id = " + std::to_string(q.id) + ",\n";
    sb += "},\n";
    return sb;
}

questnodes::CompileResult compileMasterQuestScript(const MasterQuest& m,
                                                   const QuestCompileOptions& opt) {
    questnodes::CompileResult result;
    std::string& sb = result.lua;
    auto line = [&](const std::string& t = std::string()) { sb += t; sb += '\n'; };

    line("-- " + m.name + ".lua");
    line("-- Generated by " + opt.generatorName);
    line("-- Generator: " + opt.generatorStamp);
    line("--");
    line("-- Global-state mediator quest. Hosts and persists cross-quest global");
    line("-- flags so they survive save/load (Fable globals are not auto-persisted;");
    line("-- a dormant->active master with OnPersist is the persistence host).");
    line("-- Register with the LOWEST quests.lua id so it loads first.");
    line();
    line("Quest = nil");
    line();

    // Init: default every global.
    line("function Init(questObject)");
    line("    Quest = questObject");
    line("    Quest:Log(\"" + m.name + ": Init - defaulting globals\")");
    for (const auto& g : m.globals) {
        const std::string val = g.defaultValue.empty() ? defaultForType(g.type) : g.defaultValue;
        if (g.type == "int")
            line("    Quest:SetGlobalInt(\"" + g.name + "\", " + val + ")");
        else if (g.type == "string")
            line("    Quest:SetGlobalString(\"" + g.name + "\", \"" + val + "\")");
        else
            line("    Quest:SetGlobalBool(\"" + g.name + "\", " + lowerBool(val) + ")");
    }
    line("end");
    line();

    // Main: the master hosts globals but runs no active logic.
    line("function Main(questObject)");
    line("    Quest = questObject");
    line("    Quest:Log(\"" + m.name + ": Main - global-state host active\")");
    line("end");
    line();

    // OnPersist: for each global, read -> transfer (load-or-save) -> write back.
    line("function OnPersist(questObject, context)");
    line("    Quest = questObject");
    for (const auto& g : m.globals) {
        const std::string& n = g.name;
        if (g.type == "int") {
            line("    local value = Quest:GetGlobalInt(\"" + n + "\")");
            line("    value = Quest:PersistTransferInt(context, \"" + n + "\", value)");
            line("    Quest:SetGlobalInt(\"" + n + "\", value)");
        } else if (g.type == "string") {
            line("    local value = Quest:GetGlobalString(\"" + n + "\")");
            line("    value = Quest:PersistTransferString(context, \"" + n + "\", value)");
            line("    Quest:SetGlobalString(\"" + n + "\", value)");
        } else {
            line("    local value = Quest:GetGlobalBool(\"" + n + "\")");
            line("    value = Quest:PersistTransferBool(context, \"" + n + "\", value)");
            line("    Quest:SetGlobalBool(\"" + n + "\", value)");
        }
    }
    line("end");
    line();

    return result;
}

std::string masterRegistrationSnippet(const MasterQuest& m) {
    std::string sb;
    sb += m.name + " = {\n";
    sb += "    name = \"" + m.name + "\",\n";
    sb += "    file = \"" + m.name + "/" + m.name + "\",\n";
    sb += "    id = " + std::to_string(m.id) + ",\n";
    sb += "},\n";
    return sb;
}

MasterQstOutcome ensureMasterQuestActive(forge::qst::File& qstFile,
                                         const std::string& masterName) {
    const forge::qst::Statement* s = qstFile.findQuest(masterName);
    if (s == nullptr) {
        qstFile.addQuest(masterName, true);
        return MasterQstOutcome::Added;
    }
    if (s->active()) return MasterQstOutcome::AlreadyActive;
    qstFile.setQuestActive(masterName, true);
    return MasterQstOutcome::Activated;
}

}  // namespace forge::questproject
