#pragma once
// forge::questproject -- WHOLE-QUEST Lua compiler (FQT CodeGenerator.GenerateQuestScript
// equivalent). Complements forge::questnodes, which compiles per-entity behavior
// graphs. Together they cover both ForgeFSE host kinds: LuaQuestHost (this, quest
// orchestration) and LuaEntityHost (questnodes, per-thing behavior).
//
// Output is byte-compatible with FQT's snapshot fixtures
// (FableQuestTool.Tests/Fixtures/Snapshots/QuestScript_*.lua) when fed the
// FQT-compat generator identity via QuestCompileOptions.
//
// DEFERRED (Phase 1.1): FQT's WorldMapCoordinateService region->offset auto-lookup.
// If worldMapOffsetX/Y are left 0 this emits (0, 0); set them explicitly to match a
// specific region's coordinates (bypasses the table, byte-identical for that path).
// Also deferred this slice: quest entities + EntitySpawner thread + container rewards.

#include <optional>
#include <string>
#include <vector>

#include "forge/questnodes.hpp"  // reuse CompileResult

namespace forge::questproject {

// Mirrors FQT ContainerReward.ContainerSpawnLocation.
enum class ContainerSpawnLocation { NearMarker, NearEntity, FixedPosition };

// Mirrors FQT ContainerReward -- a chest spawned at quest completion holding
// multiple items (auto-given, or opened manually via a bound entity script).
struct ContainerReward {
    std::string containerDefName = "OBJECT_CHEST";
    std::string containerScriptName = "QuestRewardContainer";
    ContainerSpawnLocation spawnLocation = ContainerSpawnLocation::NearMarker;
    std::string spawnReference;  // marker/entity script name for Near* locations
    float x = 0.0f, y = 0.0f, z = 0.0f;  // FixedPosition
    std::vector<std::string> items;
    bool highlightContainer = true;
    bool autoGiveOnComplete = true;
};

// Mirrors FQT QuestRewards (ability rewards deferred).
struct QuestRewards {
    int gold = 0;
    int experience = 0;
    int renown = 0;
    float morality = 0.0f;
    std::vector<std::string> items;
    std::optional<ContainerReward> container;
};

// Mirrors FQT QuestEntity.EntityType / SpawnMethod.
enum class EntityType { Creature, Object, Effect, Light };
enum class SpawnMethod { AtMarker, AtPosition, OnEntity, CreateCreature, BindExisting };

// Mirrors FQT QuestEntity (per-thing behavior graph handled by forge::questnodes;
// this carries only the quest-level placement/spawn/marker metadata).
struct QuestEntity {
    std::string scriptName;
    std::string defName;
    EntityType entityType = EntityType::Creature;
    bool isQuestTarget = false;
    bool showOnMinimap = false;
    SpawnMethod spawnMethod = SpawnMethod::AtMarker;
    std::string spawnRegion;
    std::string spawnMarker = "MK_OVID_DAD";
    float spawnX = 0.0f, spawnY = 0.0f, spawnZ = 0.0f;
};

// Mirrors FQT QuestState. `type` is one of bool|int|float|string; `defaultValue`
// is rendered verbatim into the SetState* call (already type-formatted by caller).
struct QuestState {
    std::string name;
    std::string type = "bool";
    bool persist = true;
    std::string defaultValue;  // empty => type default (false/0/0.0/"")
};

// Mirrors FQT QuestThread (user-defined background thread; body is a stub).
struct QuestThread {
    std::string functionName;
    std::string region;
    std::string description;
};

// Mirrors FQT QuestProject (entities deferred to a later slice).
struct QuestProject {
    std::string name = "NewQuest";
    int id = 50000;
    std::string displayName = "New Quest";
    std::string description;
    std::vector<std::string> regions;
    std::string questCardObject = "OBJECT_QUEST_CARD_GENERIC";
    std::string objectiveText;
    std::string objectiveRegion1;
    std::string objectiveRegion2;
    int worldMapOffsetX = 0;
    int worldMapOffsetY = 0;
    bool useQuestStartScreen = false;
    bool useQuestEndScreen = false;
    bool isStoryQuest = false;
    bool isGoldQuest = false;
    bool giveCardDirectly = false;
    bool isGuildQuest = false;
    bool isEnabled = true;
    QuestRewards rewards;
    std::vector<QuestState> states;
    std::vector<QuestEntity> entities;
    std::vector<QuestThread> threads;
};

// --- Global state / FSE_Master mediator -------------------------------------
// Fable globals (Set/GetGlobalBool/Int/String) are NOT auto-persisted -- only a
// quest's OnPersist writes to the save. The community convention is a dedicated
// dormant->active "FSE_Master" quest whose only job is to persist the globals so
// they survive save/load, registered with a LOWER quests.lua id than any quest
// that reads them (so it loads first). FableForge automates the whole ritual:
// generate FSE_Master.lua, flip its .qst line to TRUE, and register it.
//
// NOTE: globals support bool/int/string only (there is no SetGlobalFloat API).
struct GlobalState {
    std::string name;
    std::string type = "bool";  // bool | int | string
    std::string defaultValue;   // empty => type default (false/0/"")
};

struct MasterQuest {
    std::string name = "FSE_Master";
    int id = 50000;  // must be the LOWEST quests.lua id so it loads first
    std::vector<GlobalState> globals;
};

struct QuestCompileOptions {
    // Header identity. Defaults identify FableForge; the snapshot tests pass FQT's
    // own values to compare against FQT fixtures.
    std::string generatorName = "FableForge (FQT-compatible codegen)";
    std::string generatorStamp = "forgecore-questproject-1";
    // Generator-level debug toggles (FQT CodeGenerator fields; default off).
    bool startScreenDebug = false;
    bool startScreenDebugBanner = false;
};

// Compile a whole-quest project to its Lua script (Init/Main/OnPersist + the
// FQT_StartScreen and MonitorQuestCompletion threads + user threads).
questnodes::CompileResult compileQuestScript(const QuestProject& quest,
                                             const QuestCompileOptions& options = {});

// The quests.lua registration snippet for this quest (name/file/id [+ entity
// scripts, deferred]). FQT GenerateQuestLuaEntry-equivalent.
std::string questRegistrationSnippet(const QuestProject& quest);

// Compile the FSE_Master mediator quest to Lua: Init defaults every global,
// OnPersist load-or-restores each via Get -> PersistTransfer -> SetGlobal
// (PersistTransfer* returns the persisted value; verified against ForgeFSE).
questnodes::CompileResult compileMasterQuestScript(const MasterQuest& master,
                                                   const QuestCompileOptions& options = {});

// quests.lua registration snippet for the master (its id must be the lowest).
std::string masterRegistrationSnippet(const MasterQuest& master);

}  // namespace forge::questproject

// --- .qst automation (declared here to avoid a forge/qst.hpp include in the
// public header; defined in questproject.cpp). Ensures the master quest is
// registered AND active in a parsed .qst File.
namespace forge::qst { class File; }
namespace forge::questproject {

enum class MasterQstOutcome { AlreadyActive, Activated, Added };

// Ensure `masterName` is present and active (AddQuest(..., TRUE)) in `qstFile`.
// Flips FALSE->TRUE in place (byte-preserving) or appends it if missing.
MasterQstOutcome ensureMasterQuestActive(forge::qst::File& qstFile,
                                         const std::string& masterName = "FSE_Master");

}  // namespace forge::questproject
