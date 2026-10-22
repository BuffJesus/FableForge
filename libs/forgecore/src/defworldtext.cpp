#include "forge/defexternal.hpp"
#include "forge/wld.hpp"
#include "forge/tng.hpp"
#include "textformat.hpp"
#include <charconv>
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>

namespace forge::defexternal {
namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
template<class T> T number(std::string_view value) {
    require(!value.empty(),"missing number");T result{};
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    require(parsed.ec!=std::errc::result_out_of_range,"numeric value outside supported range");
    require(parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size(),"invalid numeric value");
    return result;
}
bool token(std::string_view value) {
    return !value.empty() && value.find_first_not_of(
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")==value.npos;
}
bool identifier(std::string_view value) {
    return token(value) && !(value.front()>='0' && value.front()<='9');
}
bool supportedSectionName(std::string_view value) {
    // LoadEntitiesFromScript 01D38DA0 reads this as text up to ';' at
    // 01D38EED, unlike the string-token consumer of a thing's ScriptName.
    // GetFullSectionNameForThing 01C56240 appends the native exact-case
    // DayOnly/NightOnly suffix. Retain the existing bare-token subset.
    const auto percent = value.find('%');
    if (percent == value.npos) return token(value);
    const auto suffix = value.substr(percent+1);
    return token(value.substr(0,percent)) && (suffix=="DayOnly" || suffix=="NightOnly");
}
void validateQuoted(std::string_view value) {
    require(value.size()>=2 && value.front()=='"' && value.back()=='"' &&
            value.substr(1,value.size()-2).find('"')==value.npos,"invalid quoted name");
}


}
Review reviewWld(std::string_view text) {
    size_t sourceLine=0;std::string directive;
    try {
        // Retail CWorldMap::LoadFromFile 00507C30: integer placement/UIDs,
        // quoted names, float minimap settings and named Contains/Sees links.
        // Native string parser accepts broader whitespace; this deliberately
        // supports only the observed one-directive-per-line storage subset.
        enum class State {Top,Quests,Map,Region};State state=State::Top;
        std::set<std::string> fields,globals,quests;
        uint32_t maps=0,regions=0;
        const auto lines=textformat::splitRawLines(std::string(text));
        for(const auto& raw:lines) {
            ++sourceLine;directive.clear();
            for(unsigned char ch:raw)require((ch>=32 && ch<=126)||ch=='\t'||ch=='\r'||ch=='\n',"unsupported text byte");
            auto line=textformat::trim(raw);if(line.empty())continue;
            require(line.back()==';',"missing directive terminator");line.remove_suffix(1);
            require(line.find(';')==line.npos,"multiple directives on one line");
            std::string key,value;require(textformat::parseLogicalLine(line,key,value),"empty directive");
            directive=key;
            if(state==State::Quests) {
                require(value.empty(),"unexpected quest argument");
                if(key=="END_INITIAL_QUESTS")state=State::Top;
                else {
                    // The text-mode CWorldMap loader reads every top-level
                    // token via ReadAsString 03057BD0 at 01C79BA1, including
                    // this preamble. Numeric/quoted items are not strings.
                    require(identifier(key),"initial quest name must be an unquoted identifier");
                    require(quests.insert(key).second,"duplicate initial quest name");
                }
                continue;
            }
            if(state==State::Top) {
                if(key=="START_INITIAL_QUESTS") {
                    require(value.empty() && globals.insert(key).second,"invalid initial quest section");state=State::Quests;
                } else if(key=="MapUIDCount" || key=="ThingManagerUIDCount") {
                    require(globals.insert(key).second,"duplicate UID counter");(void)number<uint64_t>(value);
                } else if(key=="NewMap" || key=="NewRegion") {
                    auto& count=key=="NewMap"?maps:regions;
                    require(number<uint32_t>(value)==uint64_t(count)+1,"nonsequential local slot");++count;
                    state=key=="NewMap"?State::Map:State::Region;fields.clear();
                } else throw std::runtime_error("unknown top-level directive: "+key);
                continue;
            }
            if((state==State::Map && key=="EndMap") || (state==State::Region && key=="EndRegion")) {
                require(value.empty(),"unexpected block terminator argument");
                if(state==State::Map)for(const char* required:{"MapX","MapY","LevelName","LevelScriptName","MapUID","IsSea","LoadedOnPlayerProximity"})
                    require(fields.contains(required),"missing map field");
                else for(const char* required:{"RegionName","NewDisplayName","RegionDef"})
                    require(fields.contains(required),"missing region field");
                state=State::Top;continue;
            }
            if(state==State::Region && (key=="ContainsMap" || key=="SeesMap")) {validateQuoted(value);continue;}
            require(fields.insert(key).second,"duplicate block field");
            if(state==State::Map) {
                if(key=="MapX" || key=="MapY") (void)number<int32_t>(value);
                else if(key=="MapUID") (void)number<uint32_t>(value);
                else if(key=="LevelName" || key=="LevelScriptName")validateQuoted(value);
                else if(key=="IsSea" || key=="LoadedOnPlayerProximity")require(value=="TRUE"||value=="FALSE","invalid boolean");
                else throw std::runtime_error("unknown map directive: "+key);
            } else {
                if(key=="RegionName" || key=="NewDisplayName" || key=="RegionDef")validateQuoted(value);
                else if(key=="AppearOnWorldMap")require(value.empty(),"unexpected flag value");
                // CWorldMap::LoadFromFile 01C79720 calls ReadAsString 03057BD0
                // at 01C7B447 for this field; numeric/quoted items are not strings.
                else if(key=="MiniMapGraphic")require(identifier(value),"minimap graphic name must be an unquoted identifier");
                else {
                    bool known=key=="MiniMapScale" || key=="MiniMapOffsetX" || key=="MiniMapOffsetY" ||
                        key=="WorldMapOffsetX" || key=="WorldMapOffsetY" || key=="NameGraphicOffsetX" || key=="NameGraphicOffsetY";
                    for(std::string_view prefix:{"MiniMapRegionExitTextOffsetX[","MiniMapRegionExitTextOffsetY["})
                        if(key.starts_with(prefix) && key.back()==']' && token(std::string_view(key).substr(prefix.size(),key.size()-prefix.size()-1)))known=true;
                    require(known,"unknown region directive");require(std::isfinite(number<float>(value)),"nonfinite map setting");
                }
            }
        }
        ++sourceLine;directive="EOF";
        require(state==State::Top,"unterminated world block");
        require(globals.contains("MapUIDCount") && globals.contains("ThingManagerUIDCount") &&
                globals.contains("START_INITIAL_QUESTS"),"missing world header");
        const auto parsed=wld::File::parseText(std::string(text));
        require(parsed.serialize()==text && parsed.maps().size()==maps && parsed.regions().size()==regions,
                "world parser disagreement");
        return {true,"complete supported WLD directive domains; symbolic references require resolution"};
    }catch(const std::exception& e){return {false,std::string("WLD: ")+e.what(),sourceLine,directive};}
}
static void validateTuple(std::string_view value,bool colour,size_t dimensions=3) {
    // CPersistTraits<C3DVector>::TransferIn 0309D6C0 uses three floats;
    // CRGBColour 0309F610 uses four unsigned bytes. Component-wise dotted
    // updates and other native parser forms remain outside this subset.
    const std::string_view prefix=colour?"CRGBColour(":dimensions==2?"C2DCoordF(":"C3DCoordF(";
    require(value.starts_with(prefix) && value.back()==')',"unsupported tuple syntax");
    value.remove_prefix(prefix.size());value.remove_suffix(1);
    const size_t count=colour?4:dimensions;
    for(size_t i=0;i<count;++i) {
        const auto comma=value.find(',');
        require((i+1==count)==(comma==value.npos),"wrong tuple arity");
        const auto item=textformat::trim(value.substr(0,comma));
        if(colour)require(number<uint32_t>(item)<=255,"colour exceeds unsigned byte");
        else require(std::isfinite(number<float>(item)),"nonfinite coordinate");
        if(comma!=value.npos)value.remove_prefix(comma+1);
    }
}
Review reviewTng(std::string_view text) {
    size_t sourceLine=0;std::string directive;
    try {
        // CThing::ConstructFromLevelScript 01CEEC80 / SerialiseThingInfo
        // 01CF2E30; CThingPhysical 01F1F560 and CThingObject 01D84F60.
        // CTCBase::BeginSerialising 02190FD0 isolates the named component.
        bool version=false,section=false,thing=false,physical=false;size_t things=0;
        std::string component,thingType,scriptName;std::set<std::string> fields,components,sections;
        std::set<std::string> thingFields;
        // Native OnSerialise methods, with UID consumers/remapping retained:
        // 02576E70, 0263A190, 01DFFBA0, 02765B50; scalar serializers
        // 0262D210, 02543BB0, 02634560, 02635610, 0278D9C0, 025A52D0,
        // 027833C0, 02540F50, 0249E8F0, 02789090, 026522F0, 026DAD90,
        // 01E91530. Codes: boolean, float, signed integer, UID, quoted name.
        static const auto scalarComponents=[] {
          std::map<std::string,std::map<std::string,char>> result={
            // OnSerialise 025559D0 consumes no editor properties; these
            // retained authored booleans are inert in the reviewed loader.
            {"CTCEditor",{{"LockedInPlace",'b'},{"AttachToGround",'b'},{"Freezed",'b'}}},
            // CTCDCameraPoint 025C4060 also has legacy PointType storage;
            // this subset intentionally excludes that unresolved integer.
            {"CTCDCameraPoint",{{"CutInto",'b'},{"CutOutOf",'b'},{"TestAngleBeforeActivation",'b'}}},
            // Constructor 01BA8C04 installs vtable 04098FDC; slot 1 is
            // CTCBase::OnSerialise 02190CB0, an original-byte no-op.
            {"CTCDNavigationSeed",{}},
            // Retail ArenaHallOfHeroes authors empty scratchpads. Native
            // OnSerialise 01E81B80 owns dynamic gossip maps; their nonempty
            // representation remains unreviewed and every field is refused.
            {"CTCAIScratchpad",{}},
            {"CTCActivationReceptorCreatureGenerator",{{"DeactivateAfterSetTime",'b'},
                {"FramesAfterActivationToDeactivate",'k'},{"ActivateOnActivate",'b'},{"TriggerOnActivate",'b'}}},
            // Door ctor 01BA9A63 installs 040995DC; slot 1 inherits the
            // reviewed ActivationReceptorBase serializer 02637DC0.
            {"CTCActivationReceptorDoor",{{"DeactivateAfterSetTime",'b'},{"FramesAfterActivationToDeactivate",'k'}}},
            {"CTCVillageMember",{{"VillageUID",'u'}}},
            {"CTCActivationTrigger",{{"ReceptorUID",'u'}}},
            {"CTCInventoryItem",{{"InventoryUID",'u'}}},
            // Hero 02473F20 remaps the equipped thing UID and looks up the
            // title by name; neither stored field is a numeric game slot.
            {"CTCHero",{{"LastWeaponEquippedID",'u'},{"hero_title_object_def_name",'q'}}},
            // OwnedEntity 0258AC00 supports version 1 and remaps OwnerUID.
            {"CTCOwnedEntity",{{"SwitchableNavigationTCAdded",'b'},{"OwnerUID",'u'},{"VersionNumber",'1'}}},
            // Hook serializer 0253D390 supports version 1 and remaps both UIDs.
            // Keep opaque replacement/name fields at their constructor defaults
            // (0253C860); this does not classify nonzero ReplacementObject.
            {"CTCActionUseScriptedHook",{{"Usable",'b'},{"ReversedOnMiniMap",'b'},
                {"HiddenOnMiniMap",'b'},{"VersionNumber",'1'},{"ForceConfirmation",'b'},
                {"TeleportToRegionEntrance",'b'},{"EntranceConnectedToUID",'u'},{"CameraTrackUID",'u'},
                {"SoundName",'n'},{"AnimationName",'n'},{"ReplacementObject",'r'}}},
            // OnSerialise 0257E9F0 consumes Open/OldOpen only. Original
            // DemonDoor_Guild retains DoorTriggerType 1 as an inert authored
            // annotation, not an override of CDoorDef::TriggerType.
            {"CTCDoor",{{"Open",'b'},{"OldOpen",'b'},{"DoorTriggerType",'D'}}},
            // BuyableHouse 024B2EA0: WifeLivingHere uses GFSerialisePointer
            // 0287E370 (uint64 UID). Dressing level 024B1D00 compares local
            // Dress suffix digits; it is not a global definition index.
            {"CTCBuyableHouse",{{"WifeLivingHere",'u'},{"OwnedByPlayer",'b'},
                {"IsScripted",'b'},{"Rented",'b'},{"DayNextRentIsDue",'i'},
                {"CurrentDressLevel",'i'},{"VirtualMoneyBags",'i'},
                {"HasPreviouslyBeenBuyable",'b'},{"IsResidential",'b'}}},
            // Enemy 02605760 loads a faction string; SetFaction 02605700
            // resolves it through CFactionManager, never as a numeric slot.
            {"CTCEnemy",{{"FriendsWithEverythingFlag",'b'},{"EnableFollowersEnemyProxy",'b'},{"FactionName",'q'}}},
            // 02723880: level load consumes no fields; game-state branch
            // stores a frame counter and remapped generator thing identity.
            {"CTCCreatureGeneratorCreator",{{"FrameToRegenerate",'i'},{"GeneratorUID",'u'}}},
            // Village 0215BCB2: only known flags/frame/fine fields here;
            // PlayersCrimes integers still require consumer-domain review.
            {"CTCVillage",{{"HasBeenInitiallyPopulated",'b'},{"FramePlayerLastSeenByGuard",'i'},
                {"AmountOfFineAlreadyPaid",'i'},{"Limbo",'b'},{"IsEnemyBecauseOfCrime",'b'},
                {"CurrentIsHeroCriminal",'b'}}},
            // Shop 02622E10: numeric SpecialItem is deliberately excluded.
            {"CTCShop",{{"DayLastChecked",'i'},{"GoldDonated",'i'},{"DaysSinceSpecialItemPlaced",'i'}}},
            // Opinion 01F3E310: ForcedAttitude uses EOpinionAttitudeType
            // TransferIn 019EAB0A. PDB enum values are 0..17 plus default 18.
            // COpinionSet 01F40850 has no additional level-load properties.
            {"CTCCreatureOpinionOfHero",{{"InteractedFlag",'b'},{"GreetedFlag",'b'},
                {"LastOpinionReactionFrame",'i'},{"NumberOfTimesHit",'f'},
                {"ToleranceToBeingHitOverride",'f'},{"FrameToDecayNumberOfTimesHit",'i'},
                {"ForcedAttitude",'a'},{"HeroOpinionEnemy",'b'}}},
            // Talk 023E6600 is empty for level load; its game-state fields
            // are a boolean and a dialogue voice name.
            {"CTCTalk",{{"IsPureAISpeaker",'b'},{"DialogueVoice",'q'}}},
            // 0252E4E0 stores uint32 Seed. UpdateMorphSet 0252DBD0 passes
            // it to 0289AC90/0289AD40, whose original instructions pass the
            // same pointer to GFRandom 018CF370 as PRNG state.
            {"CTCRandomAppearanceMorph",{{"Seed",'k'}}},
            {"CTCPreCalculatedNavigationRoute",{{"ThingToCalculateRouteToUID",'u'}}},
            {"CTCShapeManager",{{"IsCoordsRelativeToMap",'b'}}},
            // Guard 026DE780 uses counters and PDB ECrimeSeverity 0..3.
            {"CTCGuard",{{"FramePendingCrimesAdded",'i'},{"FrameLastBribeAdded",'i'},
                {"FrameLastCrimeSeen",'i'},{"FrameLastReceivedApology",'i'},
                {"BribePool",'i'},{"LastCrimeSeenSeverity",'e'}}},
            {"CTCWallMount",{{"BoughtForAmount",'i'},{"TrophyID",'u'}}},
            {"CTCStockItem",{{"ForSale",'b'},{"Stealable",'b'},{"Price",'i'}}},
            {"CTCActionUseBed",{{"UseableByHero",'b'},{"OwnedByHero",'b'}}},
            {"CTCDRegionEntrance",{{"Active",'b'}}},
            {"CTCDRegionExit",{{"Active",'b'},{"Radius",'f'},{"MessageRadius",'f'},
                {"ReversedOnMiniMap",'b'},{"HiddenOnMiniMap",'b'},{"EntranceConnectedToUID",'u'}}},
            {"CTCDiggingSpot",{{"Hidden",'b'},{"Enabled",'b'}}},
            {"CTCExplodingObject",{{"MaxDamage",'f'},{"Radius",'f'},{"FireDamage",'i'},
                {"TriggeredOnCreatureProximity",'b'},{"TriggerRadius",'f'}}},
            {"CTCInfoDisplay",{{"TextTag",'q'},{"TextTagBack",'q'},{"Radius",'f'},{"DisplayTime",'f'}}},
            {"CTCActionUseReadable",{{"GameTextDefName",'q'}}},
            {"CTCTrophy",{{"BestWitnessesAheadToDate",'i'},{"Mountable",'b'}}},
            {"CTCCarriedActionUseRead",{{"AlreadyRead",'b'}}},
            {"CTCFishingSpot",{{"Enabled",'b'}}},
            // 02228D20 reads Inverted, never the legacy spelling Interted.
            // 02FEBDC0 uses exact whole-string lookup; preserve only the
            // observed FALSE annotation without aliasing it to Inverted.
            {"CTCLight",{{"Active",'b'},{"Overridden",'b'},{"Colour",'c'},
                {"InnerRadius",'f'},{"OuterRadius",'f'},{"Flicker",'f'},{"Inverted",'b'},{"Interted",'F'}}},
            {"CTCSpotLight",{{"Overridden",'b'},{"Colour",'c'},{"InnerRadius",'f'},
                {"OuterRadius",'f'},{"Angle",'f'},{"Width",'f'},{"Flicker",'f'}}},
            {"CTCBoastingArea",{{"Radius",'f'}}},
            {"CTCStealableItemLocation",{{"RadiusToBeWithin",'f'},{"RadiusToTakeItemsBackTo",'f'}}},
            {"CTCTeleporter",{{"TeleportActive",'b'},{"TeleportActivated",'b'}}}
          };
          // Camera base 024F3E20 dispatches OnSerialseLeaf at vtable +98h.
          // Leaves: Track 025C5160, FixedPoint 025C5CB0, GeneralCase
          // 025C5EF0, Scripted 025C7110 (ThingUID uses native UID remapping).
          const std::map<std::string,char> cameraBase={
            {"CutInto",'b'},{"CutOutOf",'b'},{"TestAngleBeforeActivation",'b'},
            {"SelfTerminate",'b'},{"HeroIsSubject",'b'},{"FOV",'f'},
            {"IsCoordBaseRelativeToParent",'b'},{"CoordBase",'v'},{"CoordAxisUp",'v'},
            {"CoordAxisFwd",'v'},{"UsingRelativeCoords",'b'},{"UsingRelativeOrientation",'b'}};
          for(const char* kind:{"CTCCameraPointTrack","CTCCameraPointFixedPoint","CTCCameraPointGeneralCase","CTCCameraPointScripted","CTCCameraPointScriptedSpline"})
            result[kind]=cameraBase;
          for(const char* kind:{"CTCCameraPointTrack","CTCCameraPointGeneralCase"})
            for(const char* field:{"StringLength","CageRadius"})result[kind][field]='f';
          for(const char* field:{"HeightOffset","AutoGoBehindTime"})result["CTCCameraPointGeneralCase"][field]='f';
          for(const char* field:{"AllowRightStickZoom","AllowRightStickRotation","AllowZTarget","AutoGoBehind"})result["CTCCameraPointGeneralCase"][field]='b';
          result["CTCCameraPointFixedPoint"]["TrackThing"]='b';
          for(const char* field:{"LookVector.X","LookVector.Y","LookVector.Z"})result["CTCCameraPointFixedPoint"][field]='f';
          for(const char* field:{"LookDirection.X","LookDirection.Y","LookDirection.Z", "LookDirectionEnd.X","LookDirectionEnd.Y","LookDirectionEnd.Z",
                                "StartPos.X","StartPos.Y","StartPos.Z","EndPos.X","EndPos.Y","EndPos.Z","TransitionTime"})
            result["CTCCameraPointScripted"][field]='f';
          result["CTCCameraPointScripted"]["ThingUID"]='u';
          for(const char* field:{"TimeToPlay","Tension"})result["CTCCameraPointScriptedSpline"][field]='f';
          // Wife 0248DC80 has counters, flags and scalar settings. Its gift
          // field is a game definition index; only the cleared sentinel from
          // ClearGiftToGiveDef 02C62010 is independent of relocation.
          auto& wife=result["CTCWife"];
          wife["GiftToGiveDef"]='0';
          for(const char* field:{"CourtingBlocked","PermittedToRegionFollow","DivorcedHero","JustMarried",
                                "NeedsToChangeBrain","HasBeenInLoveWithPlayer","ReceivedWeddingRing"})wife[field]='b';
          for(unsigned i=0;i<4;++i)wife["BooleanHusbandAppearances["+std::to_string(i)+"]"]='b';
          for(const char* field:{"FrameGotMarriedToThePlayer","FrameToCheckAppearanceChanges","FrameLastAwareOfHusband",
              "FrameLastReducedOpinion","FrameLastEvaluatedGiftOpinion","FrameLastConsideredGivingGift",
              "FrameLastEvaluatedLoveAttitude","FrameEnteredAttitudeHate","FrameLastGaveDivorceWarning",
              "FrameEnteredLoveWithHusbandPresentAtHome","FrameLastGaveSexOffer","GiftGivingPriceValue",
              "HouseDressingLevelLastCommentedOn","FrameLastReceivedNiceGift","FrameLastCulledGiftsReceived"})wife[field]='i';
          for(const char* field:{"GiftGivingOpinionDistanceFromMax","LastFatnessChangePoint","LoveAttitudeValue"})wife[field]='f';
          return result;
        }();
        uint32_t arrayIndex=0;
        int32_t splineCount=-1;
        std::map<uint32_t,std::set<std::string>> splineFields;
        int32_t routeCount=-1;
        std::map<uint32_t,uint8_t> routeFields;
        struct ShapeFields {int32_t size=-1;bool type=false;std::map<uint32_t,uint8_t> points;};
        int32_t shapeCount=-1;std::map<uint32_t,ShapeFields> shapeFields;
        // AICreature 01D6E630 -> CreatureBase 01D8A560 -> GameObject.
        // InitialActivate 01D71CE0 remaps all five relationship identities
        // and resolves them with GetPThingWithUID.
        static const std::set<std::string> creatureUids={"HomeBuildingUID","WorkBuildingUID",
            "FatherCreatureUID","MotherCreatureUID","SpouseCreatureUID"};
        static const std::set<std::string> creatureFlags={"EnableCreatureAutoPlacing","HasInformation",
            "WanderWithInformation","WaveWithInformation","ContinueAIWithInformation","AllowedToFollowHero",
            "RegionFollowingOverriddenFromScript","RespondingToFollowAndWait","CanBeCourted","CanBeMarried"};
        auto boolean=[](const std::string& value){require(value=="TRUE"||value=="FALSE","invalid boolean");};
        auto floating=[](const std::string& value){require(std::isfinite(number<float>(value)),"nonfinite scalar");};
        for(const auto& raw:textformat::splitRawLines(std::string(text))) {
            ++sourceLine;directive.clear();
            for(unsigned char ch:raw)require((ch>=32 && ch<=126)||ch=='\t'||ch=='\r'||ch=='\n',"unsupported text byte");
            auto line=textformat::trim(raw);if(line.empty())continue;
            require(line.back()==';',"missing directive terminator");line.remove_suffix(1);
            require(line.find(';')==line.npos,"multiple directives on one line");
            std::string key,value;require(textformat::parseLogicalLine(line,key,value),"empty directive");
            directive=component.empty()?key:component+"."+key;
            if(!version){require(key=="Version" && value=="2","unsupported TNG version");version=true;continue;}
            if(!thing) {
                if(key=="XXXSectionStart") {
                    require(!section && supportedSectionName(value) && sections.insert(value).second,"invalid section start");section=true;
                } else if(key=="XXXSectionEnd") {
                    require(section && value.empty(),"invalid section end");section=false;
                } else if(key=="NewThing") {
                    // Marker/Building/Village constructor vtables use loader
                    // 027EBDD0 -> CThingPhysical. HolySite 027F8160 adds only
                    // lookup of the already named thing definition.
                    require(section && (value=="Object" || value=="Marker" || value=="Building" ||
                            value=="Village" || value=="Holy Site" || value=="Thing" || value=="TrackNode" || value=="Switch" ||
                            value=="AICreature"),"unsupported thing type or missing section");
                    // InitThingTypes 01D3D170 explicitly registers "Thing"
                    // with CThing::Alloc, whose loader was reviewed above.
                    physical=value!="Thing";thingType=value;scriptName.clear();thing=true;++things;thingFields.clear();components.clear();
                } else throw std::runtime_error("unknown TNG top-level directive: "+key);
                continue;
            }
            if(component.empty()) {
                if(key=="EndThing") {
                    require(value.empty(),"unexpected thing terminator value");
                    for(const char* required:{"Player","UID","DefinitionType"})require(thingFields.contains(required),"missing thing identity");
                    thing=false;continue;
                }
                if(key.starts_with("StartCTC")) {
                    component=key.substr(5);
                    require(value.empty() && components.insert(component).second,"duplicate or malformed component");
                    require(component=="CTCPhysicsStandard" || component=="CTCPhysicsNavigator" || component=="CTCPhysicsLight" ||
                            component=="CTCTargeted" || component=="CTCEditor" ||
                            component=="CTCAtmosPlayer" || component=="CTCDParticleEmitter" ||
                            component=="CTCObjectAugmentations" ||
                            component=="CTCContainerRewardHero" || component=="CTCChest" || component=="CTCSearchableContainer" || component=="CTCCreatureGenerator" ||
                            scalarComponents.contains(component),
                            ("unsupported component: "+component).c_str());
                    fields.clear();arrayIndex=0;splineCount=-1;splineFields.clear();
                    routeCount=-1;routeFields.clear();shapeCount=-1;shapeFields.clear();continue;
                }
                if(key=="CreateTC") {
                    // CThing::CreateSerialiseCreatableTCs 01CF0320 reads a
                    // component class name and calls AddTC, not a def lookup.
                    validateQuoted(value);const auto name=std::string_view(value).substr(1,value.size()-2);
                    require(token(name) && name.starts_with("CTC"),"invalid component class name");continue;
                }
                const bool first=thingFields.insert(key).second;
                // TrackNode 027FC620 reparses ScriptName for its track name.
                // Native saves can repeat it. Admit only identical repeats.
                require(first || (thingType=="TrackNode" && key=="ScriptName" && value==scriptName),"duplicate thing field");
                if(key=="Player") {const auto v=number<int32_t>(value);require(v>=-128 && v<=127,"player exceeds signed byte");}
                else if(key=="UID")(void)number<uint64_t>(value);
                else if(key=="DefinitionType")validateQuoted(value);
                else if(key=="ScriptName") {require(identifier(value),"script name must be an unquoted identifier");scriptName=value;}
                else if(key=="ScriptData")require(value=="\"NULL\"","opaque script data");
                else if(key=="ThingGamePersistent" || key=="ThingLevelPersistent")boolean(value);
                // Base CThing 01CEEC80 does not consume these physical fields.
                // CTCBase 02190FD0 gives each component only its own block.
                // Preserve the observed inert defaults, without assigning
                // physical semantics to nondefault values on a plain Thing.
                else if(thingType=="Thing" && (key=="Health" || key=="ObjectScale"))
                    require(number<float>(value)==1.f,"nondefault legacy base Thing scalar");
                else if(physical && (key=="Health" || key=="ObjectScale" || key=="AngleXY" || key=="AngleYZ" || key=="AngleXZ"))floating(value);
                else if(physical && key=="CanComeBetweenCameraAndHero")(void)number<int32_t>(value);
                else if(thingType=="TrackNode" && (key=="LinkedToUID1" || key=="LinkedToUID2"))(void)number<uint64_t>(value);
                else if(thingType=="TrackNode" && (key=="Start" || key=="End"))boolean(value);
                // Switch 027F9220 inherits GameObject/Physical and adds two
                // float controls plus names. Legacy TrackNode FileID fields
                // remain unsupported until their consumers are classified.
                else if(thingType=="Switch" && (key=="TriggerRadius" || key=="TimeToChangeEnvironmentDef"))floating(value);
                // Switch loader 027F9220 also uses ReadAsString for both names.
                else if(thingType=="Switch" && (key=="TriggeredByThing" || key=="EnvironmentDef"))
                    require(identifier(value),"switch name must be an unquoted identifier starting with a letter or underscore");
                else if(thingType=="AICreature" && creatureUids.contains(key))(void)number<uint64_t>(value);
                else if(thingType=="AICreature" && creatureFlags.contains(key))boolean(value);
                else if(thingType=="AICreature" && (key=="InitialPosX" || key=="InitialPosY" || key=="InitialPosZ"))floating(value);
                // Loader 01D6E630 uses ReadAsString 03057BD0, whose item
                // dispatcher treats leading digits as numbers and quotes as
                // symbols. It does not use ReadNextItemAsQuotedString.
                else if(thingType=="AICreature" && key=="OverridingBrainName")
                    require(identifier(value),
                            "brain name must be an unquoted identifier starting with a letter or underscore");
                else throw std::runtime_error("unsupported thing field: "+key);
                continue;
            }
            if(key=="End"+component) {
                require(value.empty(),"unexpected component terminator value");
                if(component=="CTCOwnedEntity")require(fields.contains("VersionNumber"),"missing ownership version");
                if(component=="CTCActionUseScriptedHook")require(fields.contains("VersionNumber"),"missing Hook version");
                if(component=="CTCPreCalculatedNavigationRoute") {
                    require(fields.contains("PrecCalculatedNavigationRouteVersion") && routeCount>=0 &&
                            routeFields.size()==size_t(routeCount),"incomplete navigation route");
                    for(const auto& [slot,mask]:routeFields)require(mask==3,"missing navigation route field");
                }
                if(component=="CTCShapeManager") {
                    require(shapeCount>=0 && shapeFields.size()==size_t(shapeCount),"incomplete shape list");
                    for(const auto& [slot,shape]:shapeFields) {
                        require(shape.type && shape.size>=0 && shape.points.size()==size_t(shape.size),"incomplete shape points");
                        for(const auto& [point,mask]:shape.points)require(mask==7,"missing shape coordinate");
                    }
                }
                if(component=="CTCCameraPointScriptedSpline") {
                    require(splineCount>=0 && splineFields.size()==size_t(splineCount),"incomplete spline keyframes");
                    for(const auto& [index,frame]:splineFields)
                        for(const char* field:{"Position","LookDirection","FOV","ShuttleSpeed","Duration","PauseTime","Event"})
                            require(frame.contains(field),"missing spline keyframe field");
                }
                component.clear();continue;
            }
            require(fields.insert(key).second,"duplicate component field");
            if(component=="CTCShapeManager") {
                // ShapeManager 025D6A50 -> CShape::StatOnSerialise 025D9B20.
                // Initializers 03FF3F60/03FF3F90 bind CLOSED and LINE tags to
                // the two native factory branches. Point coordinates are floats.
                if(key=="NumShapes") {shapeCount=number<int32_t>(value);require(shapeCount>=0,"negative shape count");continue;}
                constexpr std::string_view prefix="Shape[";
                if(key.starts_with(prefix)) {
                    auto tail=std::string_view(key).substr(prefix.size());const auto end=tail.find(']');
                    require(end!=tail.npos,"invalid shape index");
                    const auto slotText=tail.substr(0,end);const auto slot=number<uint32_t>(slotText);
                    require(shapeCount>=0 && slot<uint32_t(shapeCount) && std::to_string(slot)==slotText,"invalid shape slot");
                    auto& shape=shapeFields[slot];tail.remove_prefix(end+1);
                    if(tail==".Type") {
                        require(value=="\"SHAPE_TYPE_CLOSED\"" || value=="\"SHAPE_TYPE_LINE\"","unsupported shape type");shape.type=true;
                    } else if(tail==".size()") {
                        shape.size=number<int32_t>(value);require(shape.size>=0,"negative shape point count");
                    } else {
                        require(tail.starts_with(".pos["),"unsupported shape property");tail.remove_prefix(5);
                        const auto close=tail.find(']');require(close!=tail.npos,"invalid shape point index");
                        const auto pointText=tail.substr(0,close);const auto point=number<uint32_t>(pointText);
                        require(shape.size>=0 && point<uint32_t(shape.size) && std::to_string(point)==pointText,"invalid shape point slot");
                        tail.remove_prefix(close+1);require(tail==".X" || tail==".Y" || tail==".Z","unsupported shape coordinate");
                        floating(value);shape.points[point]|=tail==".X"?1:tail==".Y"?2:4;
                    }
                    continue;
                }
            }
            if(component=="CTCPreCalculatedNavigationRoute") {
                // 026C1580/026C19F0: version 2 local map coordinates plus
                // byte navigation layers. Tuple TransferIn is 0309C9D0.
                if(key=="PrecCalculatedNavigationRouteVersion") {require(value=="2","unsupported route version");continue;}
                if(key=="NumberOfStepsOnRoute") {
                    routeCount=number<int32_t>(value);require(routeCount>=0,"negative route count");continue;
                }
                const bool position=key.starts_with("NavPosition"),layer=key.starts_with("NavLayer");
                if(position || layer) {
                    const auto slotText=std::string_view(key).substr(position?11:8);
                    const auto slot=number<uint32_t>(slotText);
                    require(routeCount>=0 && slot<uint32_t(routeCount) && std::to_string(slot)==slotText,"invalid navigation route slot");
                    if(position)validateTuple(value,false,2);
                    else require(number<uint32_t>(value)<=255,"navigation layer exceeds byte");
                    routeFields[slot]|=position?1:2;continue;
                }
            }
            if(component=="CTCCameraPointScriptedSpline") {
                // OnSerialseLeaf 025CB1E0 reads a signed count followed by
                // KeyCameras[index].field, with defaults only for roll and
                // animation speed. No allocation is based on an input count.
                if(key=="NumKeyCameras") {
                    splineCount=number<int32_t>(value);require(splineCount>=0,"negative spline count");continue;
                }
                constexpr std::string_view animPrefix="ValidAnims[";
                if(key.starts_with(animPrefix) && key.back()==']') {
                    // 025CB1E0 writes bank entry symbols from animation IDs;
                    // these stored elements are symbolic names, not indices.
                    const auto slotText=std::string_view(key).substr(animPrefix.size(),key.size()-animPrefix.size()-1);
                    require(slotText==std::to_string(arrayIndex++),"nonsequential animation name slot");
                    validateQuoted(value);continue;
                }
                constexpr std::string_view prefix="KeyCameras[";
                if(key.starts_with(prefix)) {
                    const auto end=key.find(']',prefix.size());
                    require(end!=key.npos && key.substr(end,2)=="].","invalid spline keyframe name");
                    const auto slotText=std::string_view(key).substr(prefix.size(),end-prefix.size());
                    const auto slot=number<uint32_t>(slotText);
                    require(splineCount>=0 && slot<uint32_t(splineCount) && std::to_string(slot)==slotText,"invalid spline keyframe slot");
                    const auto field=key.substr(end+2);
                    require(splineFields[slot].insert(field).second,"duplicate spline keyframe field");
                    if(field=="Position" || field=="LookDirection")validateTuple(value,false);
                    else if(field=="FOV" || field=="ShuttleSpeed" || field=="Duration" || field=="PauseTime" ||
                            field=="AnimationSpeed" || field=="RollAngle")floating(value);
                    // UpdateCameraParams 025C98E0 queues the event string;
                    // HasMessage 025C9550 compares and erases matching names.
                    else if(field=="Event")validateQuoted(value);
                    else throw std::runtime_error("unsupported spline keyframe field");
                    continue;
                }
            }
            if(const auto found=scalarComponents.find(component);found!=scalarComponents.end()) {
                const auto field=found->second.find(key);require(field!=found->second.end(),("unsupported component scalar: "+component+"."+key).c_str());
                switch(field->second) {
                    case '0':require(value=="0","numeric definition reference requires relocation");break;
                    case '1':require(value=="1","unsupported component version");break;
                    case 'r':require(value=="0","nondefault Hook replacement requires semantic review");break;
                    case 'n':require(value=="\"\"","nondefault Hook name requires semantic review");break;
                    case 'a':require(number<uint32_t>(value)<=18,"unsupported opinion attitude");break;
                    case 'e':require(number<uint32_t>(value)<=3,"unsupported crime severity");break;
                    case 'b':boolean(value);break;
                    case 'F':require(value=="FALSE","nondefault legacy light annotation");break;
                    case 'D':require(value=="1","unreviewed legacy door annotation");break;
                    case 'f':floating(value);break;
                    case 'i':(void)number<int32_t>(value);break;
                    // Native unsigned-long traits 0189B010/0189B040 use
                    // signed-long text transfer in both directions. Negative
                    // decimal spelling preserves the uint32 storage bits.
                    case 'k':(void)number<int32_t>(value);break;
                    case 'u':(void)number<uint64_t>(value);break;
                    case 'q':validateQuoted(value);break;
                    case 'v':validateTuple(value,false);break;
                    case 'c':validateTuple(value,true);break;
                    default:throw std::runtime_error("unknown scalar domain");
                }
                continue;
            }
            // Reviewed native serializers: PhysicsStandard 02527070/base
            // 0246E620, Targeted 02538F20, AtmosPlayer 02796F90, particle
            // 01D64C00, container 02AE5910, Chest 025A2060, generator 02514FD0.
            // DefIndex text TransferIn 03178B80 resolves a string by name.
            if(component=="CTCPhysicsStandard" || component=="CTCPhysicsNavigator" || component=="CTCPhysicsLight") {
                // Original constructor vtables: Navigator slot 1 inherits
                // Standard 02527070; Light inherits Base 0246E620.
                require(key=="PositionX"||key=="PositionY"||key=="PositionZ"||
                        (component!="CTCPhysicsLight" && (key=="RHSetForwardX"||key=="RHSetForwardY"||key=="RHSetForwardZ"||
                        key=="RHSetUpX"||key=="RHSetUpY"||key=="RHSetUpZ")),"unsupported physics field");floating(value);
            } else if(component=="CTCTargeted") {require(key=="Targetable","unsupported targeted field");boolean(value);}
            else if(component=="CTCAtmosPlayer") {require(key=="AtmosName","unsupported atmosphere field");validateQuoted(value);}
            else if(component=="CTCDParticleEmitter") {
                if(key=="IndependantObject")boolean(value);
                else {require(key=="ParticleTypeName","unsupported particle field");validateQuoted(value);}
            } else if(component=="CTCContainerRewardHero" || component=="CTCChest" || component=="CTCSearchableContainer" ||
                      component=="CTCCreatureGenerator" || component=="CTCObjectAugmentations") {
                // Augmentation serializer 025EA5D0 resolves each string with
                // GetDef(name); the stored array is not a numeric ID vector.
                const std::string prefix=component=="CTCCreatureGenerator"?"CreatureFamilies[":
                    component=="CTCObjectAugmentations"?"AugmentationDefNames[":"ContainerContents[";
                if(key.starts_with(prefix) && key.back()==']') {
                    require(std::string_view(key).substr(prefix.size(),key.size()-prefix.size()-1)==std::to_string(arrayIndex++),"nonsequential array slot");
                    validateQuoted(value);
                } else if(component=="CTCChest" && key=="ChestOpen")boolean(value);
                else if(component=="CTCObjectAugmentations" && key=="SavedInGame")boolean(value);
                else if(component=="CTCSearchableContainer" && key=="NumberOfTimesToSearch")(void)number<int32_t>(value);
                else if(component=="CTCCreatureGenerator") {
                    if(key=="GenerationRadius" || key=="SelfTriggerRadius")floating(value);
                    else if(key=="SelfTrigger" || key=="TriggerOnActivate")boolean(value);
                    else if(key=="SelfTriggerResetInterval" || key=="ActiveCreatureLimit" || key=="TotalGenerationLimit" || key=="NumTriggers")
                        (void)number<int32_t>(value);
                    else {require(key=="ScriptNameOfAllGeneratedCreatures","unsupported generator field");validateQuoted(value);}
                } else throw std::runtime_error("unsupported container field");
            } else throw std::runtime_error("unsupported editor field");
        }
        ++sourceLine;directive="EOF";
        require(version && !section && !thing && component.empty(),"unterminated TNG structure");
        const auto parsed=tng::File::parseText(std::string(text));
        require(parsed.serialize()==text && parsed.things().size()==things,"TNG parser disagreement");
        return {true,"complete supported TNG subset domains; symbolic references require resolution"};
    }catch(const std::exception& e){return {false,std::string("TNG: ")+e.what(),sourceLine,directive};}
}
TngDefinitionInventory inventoryTngDefinitionFields(std::string_view text) {
    TngDefinitionInventory result{reviewTng(text), {}};
    if (!result.storage.definitionIndependent) return result;
    const auto parsed = tng::File::parseText(std::string(text));
    for (const auto& thing : parsed.things()) {
        auto append = [&](const std::string& component, const tng::Property& field) {
            result.fields.push_back({thing.type, component, field.key,
                textformat::stripQuotes(field.value), field.line + 1});
        };
        for (const auto& field : thing.properties) {
            if (field.key == "DefinitionType") append({}, field);
            // Loader 027F9220 stores EnvironmentDef at +0x11c (NULL -> empty).
            // TriggerSetEnvironment 027FAD80 resolves this name only on trigger.
            // AICreature loader 01D6E630 similarly stores NULL as empty at
            // +0x1f8. InitialiseFromDef 01D75CE0 queries nonempty names unless
            // the debug brain override has replaced this value.
            else if ((thing.type == "Switch" && field.key == "EnvironmentDef") ||
                     (thing.type == "AICreature" && field.key == "OverridingBrainName")) {
                append({}, field);
                auto& reference = result.fields.back();
                if (reference.name == "NULL") reference.name.clear();
                reference.conditional = true;
            }
        }
        for (const auto& component : thing.ctcBlocks) {
            for (const auto& field : component.properties) {
                // Native name consumers: AtmosPlayer 02796F90, generator
                // 02514FD0 -> CDefIndex TransferIn 03178B80, Hero 02473F20,
                // container 02AE5910 -> CCreateIndicesFromItemNames 02AE65A0.
                const auto& name = component.name;
                const bool container = name == "CTCContainerRewardHero" ||
                    name == "CTCChest" || name == "CTCSearchableContainer";
                if ((name == "CTCAtmosPlayer" && field.key == "AtmosName") ||
                    (name == "CTCHero" && field.key == "hero_title_object_def_name") ||
                    (name == "CTCCreatureGenerator" && field.key.starts_with("CreatureFamilies[")) ||
                    (container && field.key.starts_with("ContainerContents[")))
                    append(name, field);
            }
        }
    }
    return result;
}

Review reviewGtg(std::string_view text) {
    size_t sourceLine=0;std::string directive;
    try {
        // CWorldMap::LoadAllLoadableGlobalThingsFromSingleFile 01C80E90
        // reads NEWMAP / signed integer / body through ENDMAP, then calls
        // CThingManager::LoadLoadableGlobalThingsFromFile 01D3BD20 ->
        // LoadFromFile 01D33D90. Original-byte string/call checks agree.
        bool inMap=false;std::set<int32_t> maps;std::string body;size_t bodyStart=0;int32_t mapSlot=0;
        for(const auto& raw:textformat::splitRawLines(std::string(text))) {
            ++sourceLine;directive.clear();
            const auto line=textformat::trim(raw);
            if(line.empty()){if(inMap)body+=raw;continue;}
            if(!inMap) {
                directive="NEWMAP";
                require(line.starts_with("NEWMAP "),"expected GTG map start");
                const auto slot=number<int32_t>(textformat::trim(line.substr(7)));
                require(slot>0 && maps.insert(slot).second,"invalid or duplicate GTG map slot");
                inMap=true;body.clear();bodyStart=sourceLine;mapSlot=slot;
            } else if(line=="ENDMAP") {
                const auto reviewed=reviewTng(body);
                if(!reviewed.definitionIndependent)
                    return {false,"GTG: "+reviewed.reason,reviewed.line?bodyStart+reviewed.line:sourceLine,
                            "map "+std::to_string(mapSlot)+": "+reviewed.directive};
                inMap=false;
            } else body+=raw;
        }
        ++sourceLine;directive="EOF";
        require(!inMap,"unterminated GTG map");
        return {true,"complete supported GTG bodies; world slots and symbolic references require resolution"};
    }catch(const std::exception& e){return {false,std::string("GTG: ")+e.what(),sourceLine,directive};}
}
TngDefinitionInventory inventoryGtgDefinitionFields(std::string_view text) {
    TngDefinitionInventory result{reviewGtg(text), {}};
    if (!result.storage.definitionIndependent) return result;
    size_t sourceLine = 0, bodyStart = 0;
    int32_t mapSlot = 0;
    std::string body;
    for (const auto& raw : textformat::splitRawLines(std::string(text))) {
        ++sourceLine;
        const auto line = textformat::trim(raw);
        if (!mapSlot) {
            if (line.empty()) continue;
            mapSlot = number<int32_t>(textformat::trim(line.substr(7)));
            bodyStart = sourceLine;
            body.clear();
        } else if (line == "ENDMAP") {
            auto inventory = inventoryTngDefinitionFields(body);
            for (auto& field : inventory.fields) {
                field.line += bodyStart;
                field.worldMapSlot = mapSlot;
                result.fields.push_back(std::move(field));
            }
            mapSlot = 0;
        } else body += raw;
    }
    return result;
}
}
