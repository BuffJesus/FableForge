#include "forge/questcard.hpp"

#include <cstring>
#include <stdexcept>

#include "forge/defdecode.hpp"

namespace forge::questcard {
namespace {

uint32_t rdU32(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8 | uint32_t(b[p + 2]) << 16 |
           uint32_t(b[p + 3]) << 24;
}
void wrU32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
    b[p] = uint8_t(v);
    b[p + 1] = uint8_t(v >> 8);
    b[p + 2] = uint8_t(v >> 16);
    b[p + 3] = uint8_t(v >> 24);
}

// Overwrite one decoded field's value in place, tracking patched/preserved.
struct Patcher {
    defdecode::Decoded& d;
    AuthorResult& r;

    defdecode::DecodedField* field(const std::string& name) {
        for (auto& f : d.fields)
            if (f.name == name) return &f;
        return nullptr;
    }
    // Set a 4-byte scalar field (int32/uint32/CDefString ref).
    void setU32(const std::string& name, std::optional<uint32_t> v) {
        auto* f = field(name);
        if (f == nullptr) return;
        if (!v) { r.preserved.push_back(name); return; }
        // Clone-preserve if the donor value is not a clean 4-byte scalar.
        if (f->value.size() != 4) { r.preserved.push_back(name); return; }
        wrU32(f->value, 0, *v);
        r.patched.push_back(name);
    }
    void setBool(const std::string& name, std::optional<bool> v) {
        auto* f = field(name);
        if (f == nullptr) return;
        if (!v) { r.preserved.push_back(name); return; }
        if (f->value.size() != 1) { r.preserved.push_back(name); return; }
        f->value[0] = *v ? 1 : 0;
        r.patched.push_back(name);
    }
};

std::vector<uint8_t> patchCardData(const std::vector<uint8_t>& donorCardData,
                                   const defschema::Schema& schema,
                                   const CardPatch& patch, AuthorResult& result) {
    const auto* def = schema.find("CQuestCardDef");
    if (def == nullptr)
        throw std::runtime_error("questcard: schema has no CQuestCardDef");
    auto decoded = defdecode::decode(donorCardData, *def);
    if (!decoded.clean())
        throw std::runtime_error(
            "questcard: donor CQuestCardDef did not decode clean — refusing to "
            "patch an unrecognized payload");

    Patcher p{decoded, result};
    p.setU32("QuestName", patch.questName
                              ? std::optional<uint32_t>(uint32_t(*patch.questName))
                              : std::nullopt);
    p.setU32("QuestSummary",
             patch.questSummary
                 ? std::optional<uint32_t>(uint32_t(*patch.questSummary))
                 : std::nullopt);
    p.setU32("QuestObjective",
             patch.questObjective
                 ? std::optional<uint32_t>(uint32_t(*patch.questObjective))
                 : std::nullopt);
    p.setU32("SuccessSummary",
             patch.successSummary
                 ? std::optional<uint32_t>(uint32_t(*patch.successSummary))
                 : std::nullopt);
    p.setU32("RenownReward",
             patch.renownReward
                 ? std::optional<uint32_t>(uint32_t(*patch.renownReward))
                 : std::nullopt);
    p.setU32("GoldReward", patch.goldReward
                               ? std::optional<uint32_t>(uint32_t(*patch.goldReward))
                               : std::nullopt);
    p.setU32("NumBoasts", patch.numBoasts
                              ? std::optional<uint32_t>(uint32_t(*patch.numBoasts))
                              : std::nullopt);
    p.setU32("QuestEpilogue", patch.questEpilogue);
    p.setBool("IsCoreQuest", patch.isCoreQuest);
    p.setBool("IsVignette", patch.isVignette);
    p.setBool("IsExclusive", patch.isExclusive);
    p.setBool("CanPlayerCancel", patch.canPlayerCancel);
    for (const char* name : {"RegionName", "TeleporterRegionName", "InventoryCategory",
                             "RewardObjects", "MakeVignetteRouteAppearOnMinimap",
                             "Prerequisites"}) {
        result.preserved.push_back(name);
    }

    std::vector<uint8_t> output = defdecode::encode(decoded);
    if (output.size() != donorCardData.size())
        throw std::runtime_error("questcard: patched CQuestCardDef changed size "
                                 "(expected clone-identical length)");
    return output;
}

struct DonorPair {
    size_t objectIndex = 0;
    uint32_t cardIndex = 0;
};

DonorPair findDonorPair(const bin::File& file,
                        const std::string& donorObjectName) {
    const auto& entries = file.entries();
    size_t objectIndex = entries.size();
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name == donorObjectName) {
            objectIndex = i;
            break;
        }
    }
    if (objectIndex == entries.size())
        throw std::runtime_error("questcard: no donor OBJECT named " +
                                 donorObjectName);
    const auto& object = entries[objectIndex];
    if (object.data.size() < kObjectCardLinkOffset + 4)
        throw std::runtime_error("questcard: donor OBJECT payload too small");
    const uint32_t cardIndex = rdU32(object.data, kObjectCardLinkOffset);
    if (cardIndex >= entries.size() || entries[cardIndex].definition != "CQuestCardDef") {
        throw std::runtime_error(
            "questcard: donor OBJECT offset-81 does not point at a CQuestCardDef "
            "(got index " + std::to_string(cardIndex) + "); pick a standard "
            "OBJECT_QUEST_CARD_* donor such as OBJECT_QUEST_CARD_WASP_MENACE");
    }
    return {objectIndex, cardIndex};
}

} // namespace

AuthorResult author(bin::File& file, const defschema::Schema& schema,
                    const std::string& donorObjectName,
                    const std::string& newName, const CardPatch& patch) {
    AuthorResult r;
    r.donorObject = donorObjectName;
    r.objectName = "OBJECT_QUEST_CARD_" + newName;

    // --- Locate the donor OBJECT and the CQuestCardDef it links to. ---------
    const DonorPair donor = findDonorPair(file, donorObjectName);
    const uint32_t donorCardIndex = donor.cardIndex;
    // Snapshot bytes before any addEntry() reallocates the entry vector.
    const std::vector<uint8_t> donorCardData =
        file.entries()[donorCardIndex].data;
    const std::vector<uint8_t> donorObjData = file.entries()[donor.objectIndex].data;
    r.donorCard = std::to_string(donorCardIndex);

    // --- Clone + patch the CQuestCardDef. ----------------------------------
    std::vector<uint8_t> newCard = patchCardData(donorCardData, schema, patch, r);

    r.cardDefIndex = file.addEntry("CQuestCardDef", "", std::move(newCard));

    // --- Clone the OBJECT, relink offset-81 + self back-ref, rename, add. ---
    // The OBJECT lands at the next free index (the card was just added, so this
    // OBJECT is the entry immediately after it).
    const uint32_t newObjectIndex = static_cast<uint32_t>(file.entries().size());
    std::vector<uint8_t> newObj = donorObjData;
    // off 81: link to the freshly-cloned CQuestCardDef.
    wrU32(newObj, kObjectCardLinkOffset, uint32_t(r.cardDefIndex));
    // off 85: retarget the CQuestCardDef component record's source-OBJECT back-
    // reference from the donor's self index to THIS clone's landing index.
    // Without this the record points at the donor object (a different, pre-
    // existing thing) and GiveHeroQuestCardDirectly cannot Create the card.
    wrU32(newObj, kObjectSelfRefOffset, newObjectIndex);
    r.objectIndex = file.addEntry("OBJECT", r.objectName, std::move(newObj));

    return r;
}

AuthorResult authorFromScratch(bin::File& file, const defschema::Schema& schema,
                               const std::string& newName,
                               const CardPatch& patch) {
    // Base on the engine's neutral template, not a real quest's card. author()
    // clones the template OBJECT + its (content-free) CQuestCardDef, patches the
    // caller's fields, appends both, and retargets offsets 81/85.
    AuthorResult r = author(file, schema, kTemplateObjectName, newName, patch);
    r.donorObject = kTemplateObjectName;  // documents the base, not a hijacked card

    // Clear the template marker byte (offset 1) so the new card is not treated as
    // the dummy template. Every shipped real card has 0 here; the template has 1.
    std::vector<uint8_t> obj = file.entries()[r.objectIndex].data;
    if (obj.size() > kObjectTemplateFlagOffset)
        obj[kObjectTemplateFlagOffset] = 0;
    file.setEntryData(r.objectIndex, std::move(obj));
    return r;
}

CompanionScript generateCompanionScript(const CompanionScriptSpec& spec) {
    const std::string& q = spec.questName;
    const std::string& obj = spec.objectName;
    std::string lua;
    auto line = [&](const std::string& s) { lua += s; lua += '\n'; };

    line("-- " + q + ".lua");
    line("-- Companion quest for " + obj +
         ", generated by `forge quest card`.");
    line("-- The Current-quests screen reads objective/gold/renown from the ACTIVE");
    line("-- card thing, populated only by the Set* setters below (title + summary");
    line("-- come from the static card def). See docs/QUEST_CARD_EMPTY_FIX.md.");
    line("");
    line("Quest = nil");
    line("");
    line("function Init(questObject)");
    line("    Quest = questObject");
    line("    Quest:Log(\"" + q + ": Init\")");
    if (spec.region)
        line("    Quest:AddQuestRegion(\"" + q + "\", \"" + *spec.region + "\")");
    line("    Quest:SetStateBool(\"QuestCompleted\", false)");
    line("end");
    line("");
    line("function Main(questObject)");
    line("    Quest = questObject");
    line("    -- Register the card in the Guild/Logbook (title + summary from the def).");
    line("    Quest:AddQuestCard(\"" + obj + "\", \"" + q + "\", false, false)");
    line("    -- Populate the runtime fields the Current-quests screen reads.");
    if (spec.objectiveSymbol)
        line("    Quest:SetQuestCardObjective(\"" + q + "\", \"" +
             *spec.objectiveSymbol + "\", \"" +
             (spec.region ? *spec.region : std::string()) + "\", \"\")");
    if (spec.gold)
        line("    Quest:SetQuestGoldReward(\"" + q + "\", " +
             std::to_string(*spec.gold) + ")");
    if (spec.renown)
        line("    Quest:SetQuestRenownReward(\"" + q + "\", " +
             std::to_string(*spec.renown) + ")");
    line("    Quest:KickOffQuestStartScreen(\"" + q + "\", true, true)");
    line("    Quest:Log(\"" + q + ": quest card configured\")");
    line("end");
    line("");
    line("function OnPersist(questObject, context)");
    line("    Quest = questObject");
    line("    Quest:PersistTransferBool(context, \"QuestCompleted\")");
    line("end");

    std::string reg;
    auto rline = [&](const std::string& s) { reg += s; reg += '\n'; };
    rline("-- Add this entry to FSE/quests.lua (id must be UNIQUE, <100-slot pool).");
    rline("-- " + q + " = {");
    rline("--     name = \"" + q + "\",");
    rline("--     file = \"" + q + "/" + q + "\",");
    rline("--     id = " + std::to_string(spec.questId) + ",");
    rline("--     entity_scripts = {}");
    rline("-- },");

    CompanionScript out;
    out.luaScript = std::move(lua);
    out.questsLua = std::move(reg);
    out.qstLine = "AddQuest(\"" + q + "\", TRUE);";
    out.masterActivate = "quest:ActivateQuest(\"" + q + "\")";
    return out;
}

AuthorResult overwriteDonor(bin::File& file, const defschema::Schema& schema,
                            const std::string& donorObjectName,
                            const CardPatch& patch) {
    const DonorPair donor = findDonorPair(file, donorObjectName);
    AuthorResult result;
    result.appended = false;
    result.objectName = donorObjectName;
    result.donorObject = donorObjectName;
    result.objectIndex = donor.objectIndex;
    result.cardDefIndex = donor.cardIndex;
    result.donorCard = std::to_string(donor.cardIndex);
    const auto original = file.entries()[donor.cardIndex].data;
    file.setEntryData(donor.cardIndex,
                      patchCardData(original, schema, patch, result));
    return result;
}

} // namespace forge::questcard
