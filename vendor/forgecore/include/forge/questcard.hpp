#pragma once
// Headless authoring of CUSTOM QUEST CARDS by CLONING a retail donor.
//
// A usable custom quest card is a PAIR of game.bin entries:
//   1. an unnamed CQuestCardDef sub-def (132 B) — the card content
//      (QuestName / QuestSummary / GoldReward / IsCoreQuest ...), and
//   2. a named OBJECT entry "OBJECT_QUEST_CARD_<X>" (a CObjectDef, 458 B) — the
//      inventory object the runtime resolves by NAME. FSE's
//      Quest:AddQuestCard("OBJECT_QUEST_CARD_<X>", scriptName, ...) looks the
//      card up by this OBJECT name (fse_api_manifest.json: arg questCardObjectName),
//      so the OBJECT is what makes the card addressable from a quest script.
//
// The OBJECT points at its CQuestCardDef by GLOBAL ENTRY INDEX, stored as a plain
// little-endian u32 at payload offset 81 (value-verified across all retail cards:
// OAKVALE_INTRO->12295, PROTECT_FARM->12296, WASP_MENACE->12298, ...). Because
// addEntry() only APPENDS entries, every existing global index is preserved, so
// cloning is safe: the new CQuestCardDef lands at a fresh high index and the
// cloned OBJECT's offset-81 is patched to point at it.
//
// WHY CLONE (not encode from scratch): the engine writer CQuestCardDef::Transfer
// byte order for a full payload is unrecovered, and CObjectDef payloads carry
// scrambled CDefIndex/hash bytes forge cannot synthesize. But forge CAN decode a
// donor CQuestCardDef cleanly (all 18 fields are CRC-tag-anchored, leftover 0 —
// see defdecode), so we clone a donor's bytes and overwrite ONLY the scalar
// fields whose type is known, re-encoding through defdecode (tag+value), which
// round-trips donor bytes exactly for any field we do not touch.
//
// CONFIDENT fields (patched here, all 4-byte scalar refs / 1-byte bools):
//   QuestName QuestSummary QuestObjective SuccessSummary (int32 TEXT refs),
//   RenownReward GoldReward (int32), IsCoreQuest IsVignette IsExclusive
//   CanPlayerCancel (bool), NumBoasts (int32), QuestEpilogue (uint32).
// CLONE-PRESERVED (opaque, kept byte-identical to the donor): RegionName /
//   TeleporterRegionName (CDefString refs into the donor's region set),
//   InventoryCategory, RewardObjects (Vector_J), MakeVignetteRouteAppearOnMinimap,
//   Prerequisites (Map). Override RegionName/TeleporterRegionName only by cloning
//   a donor that already targets the region you want.
//
// ★ RUNTIME-REQUIRED (root cause of "card displays but all values EMPTY", decompiled
//   in docs/QUEST_CARD_EMPTY_FIX.md): the card's OBJECTIVE, GOLD and RENOWN lines on
//   the Current-quests screen are NOT read from this static def. The engine reads them
//   from the RUNTIME per-quest card THING (CTCQuestCard: ObjectiveName@+0x1c,
//   GoldReward@+0x2c, RenownReward@+0x30), written ONLY by a quest script calling
//   SetQuestCardObjective / SetQuestCardGoldReward / SetQuestCardRenownReward against
//   an ACTIVE card (resolved by GetActiveQuestCardFromScriptName via ScriptQuestName@
//   +0x28). So a def-only card with no companion quest shows blank objective/reward.
//   TITLE and SUMMARY *do* come from this def (GetQuestName reads def name id, etc.),
//   so those render — UNLESS the id is 0/invalid, which resolves to the engine's empty
//   default string (the CLI now rejects a 0 TextID for those fields). To populate the
//   runtime fields, ship a companion quest script: AddQuestCard(
//   "OBJECT_QUEST_CARD_<X>", scriptName, ...) -> activate -> SetQuestCard{Objective,GoldReward,
//   RenownReward}(scriptName, ...).

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "forge/bin.hpp"
#include "forge/defschema.hpp"

namespace forge::questcard {

// Fields to overwrite on the cloned CQuestCardDef. Any std::nullopt is left at
// the donor's value (clone-preserved).
struct CardPatch {
    std::optional<int32_t> questName;        // TEXT ref
    std::optional<int32_t> questSummary;     // TEXT ref
    std::optional<int32_t> questObjective;   // TEXT ref
    std::optional<int32_t> successSummary;   // TEXT ref
    std::optional<int32_t> renownReward;
    std::optional<int32_t> goldReward;
    std::optional<int32_t> numBoasts;
    std::optional<uint32_t> questEpilogue;
    std::optional<bool> isCoreQuest;
    std::optional<bool> isVignette;
    std::optional<bool> isExclusive;
    std::optional<bool> canPlayerCancel;
};

struct AuthorResult {
    size_t cardDefIndex = 0;   // global entry index of the new CQuestCardDef
    size_t objectIndex = 0;    // global entry index of the new OBJECT
    std::string objectName;    // "OBJECT_QUEST_CARD_<X>"
    std::string donorCard;     // donor CQuestCardDef (name or index used)
    std::string donorObject;   // donor OBJECT_QUEST_CARD_* name
    bool appended = true;      // false when an existing donor pair was patched in place
    std::vector<std::string> patched;    // fields actually overwritten
    std::vector<std::string> preserved;  // known fields left at donor value
};

// Offset of the u32 CQuestCardDef global-index link inside an OBJECT_QUEST_CARD
// payload (value-verified against retail).
inline constexpr size_t kObjectCardLinkOffset = 81;

// Offset of the u32 "source OBJECT" back-reference inside the CQuestCardDef
// component record (nameHash 0xB4C3A48A) of an OBJECT_QUEST_CARD payload. In
// every retail card this equals the OBJECT's OWN global entry index (value-
// verified across 100+ cards: WASP_MENACE@3713 -> 3713, ARENA@3715 -> 3715,
// ...). Cloning a donor leaves this at the DONOR's index; it MUST be retargeted
// to the new OBJECT's landing index or the clone's card component points at a
// different pre-existing thing.
inline constexpr size_t kObjectSelfRefOffset = 85;

// The engine ships a BLANK, content-free quest card — OBJECT_QUEST_CARD_TEMPLATE
// (+ its neutral CQuestCardDef: all TextIDs 0, RegionName/TeleporterRegionName
// null, rewards 0, RewardObjects/Prerequisites empty). It is NOT a real quest's
// card, so basing a from-scratch card on it inherits no shipped-quest identity
// (region/category/text). This is the canonical "author without cloning a real
// card" base. Verified: its OBJECT payload is byte-identical to every shipped
// card except name + offsets 81/85 + one shared sub-def ref @0x174.
inline constexpr const char* kTemplateObjectName = "OBJECT_QUEST_CARD_TEMPLATE";

// The template OBJECT carries a 1-byte "this is the dummy template" marker at
// payload offset 1 (value 1). All four shipped REAL cards clear it to 0. A
// from-scratch card must match the real cards, not the template.
inline constexpr size_t kObjectTemplateFlagOffset = 1;

// Clone `donorObjectName`'s OBJECT + the CQuestCardDef it links to, patch the
// clone's known fields per `patch`, add both as new entries named
// "OBJECT_QUEST_CARD_<newName>", and relink. Mutates `file` in place; caller
// saves. `schema` must contain CQuestCardDef. Throws std::runtime_error on a
// missing donor / non-clean donor decode.
AuthorResult author(bin::File& file, const defschema::Schema& schema,
                    const std::string& donorObjectName,
                    const std::string& newName, const CardPatch& patch);

// Author a brand-new quest card FROM SCRATCH — no real-card donor. Clones the
// engine's neutral OBJECT_QUEST_CARD_TEMPLATE (see kTemplateObjectName), patches
// the caller's fields, appends the pair as "OBJECT_QUEST_CARD_<newName>",
// retargets offsets 81/85, and clears the template marker byte so the result
// behaves like a shipped card. All quest content (title/summary/rewards/flags)
// comes from `patch`; nothing is inherited from any shipped quest. Equivalent to
// author() with the template as donor, plus the marker-byte fix. Throws on a
// missing template (non-retail game.bin) or non-clean template decode.
AuthorResult authorFromScratch(bin::File& file, const defschema::Schema& schema,
                               const std::string& newName, const CardPatch& patch);

// A companion ForgeFSE quest is what makes a card's RUNTIME content appear: the
// Current-quests screen reads objective/gold/renown from the ACTIVE card thing,
// populated only by a quest script calling the Set* setters (see
// docs/QUEST_CARD_EMPTY_FIX.md). generateCompanionScript() emits a minimal,
// working content-first quest for a card: it registers the card, sets its
// runtime fields, and kicks off the start screen. Title/summary still come from
// the static def; this fills in the rest with NO cloning of a shipped quest.
struct CompanionScriptSpec {
    std::string questName;            // FSE quest name (script + registry key)
    std::string objectName;           // "OBJECT_QUEST_CARD_<X>" the card resolves by
    int32_t questId = 50100;          // unique id in FSE/quests.lua (<100-slot pool)
    std::optional<int32_t> gold;      // runtime gold shown on the card
    std::optional<int32_t> renown;    // runtime renown shown on the card
    std::optional<std::string> objectiveSymbol;  // TEXT_* tag for the objective line
    std::optional<std::string> region;           // quest region (optional)
};

struct CompanionScript {
    std::string luaScript;      // <questName>/<questName>.lua contents
    std::string questsLua;      // <questName>/quests.lua registration snippet
    std::string qstLine;        // AddQuest("<questName>", TRUE); for FinalAlbion.qst
    std::string masterActivate; // quest:ActivateQuest("<questName>") for FSE_Master.lua
};

// Generate the companion-quest files + the two shared-registry lines a modder must
// add (FinalAlbion.qst + FSE_Master.lua). Pure string generation, no I/O.
CompanionScript generateCompanionScript(const CompanionScriptSpec& spec);

// Patch an EXISTING donor card definition in place without adding game.bin or
// names.bin rows. This is the compatibility-first path for retail: appended
// definition rows have not yet been proven accepted by the engine, while an
// existing OBJECT_QUEST_CARD_* identity is already loadable. The caller must
// use result.objectName (the donor name) from Lua and accepts replacing that
// donor's presentation. The OBJECT payload and every global index stay fixed.
AuthorResult overwriteDonor(bin::File& file, const defschema::Schema& schema,
                            const std::string& donorObjectName,
                            const CardPatch& patch);

} // namespace forge::questcard
