#include "forge/defexternal.hpp"
#include "forge/terraintex.hpp"
#include "forge/textbig.hpp"
#include "forge/big.hpp"
#include "forge/lzo.hpp"
#include "forge/bwd.hpp"
#include "forge/wadstorage.hpp"
#include "forge/tng.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>

using Bytes=std::vector<uint8_t>;
static void require(bool value,const char* reason) {if(!value)throw std::runtime_error(reason);}
static void word(Bytes& bytes,uint32_t value) {
    for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(value>>(i*8)));
}
static void append(Bytes& bytes,const Bytes& more) {bytes.insert(bytes.end(),more.begin(),more.end());}
static void tngChecks() {
    using forge::defexternal::reviewTng;
    const std::string text="Version 2;\nXXXSectionStart NULL;\nNewThing Object;\nPlayer -1;\nUID 18446744073709551615;\nDefinitionType \"OBJECT_15360\";\nScriptName NULL;\nScriptData \"NULL\";\nThingGamePersistent FALSE;\nThingLevelPersistent TRUE;\nStartCTCPhysicsStandard;\nPositionX 15360.0;\nPositionY -15361.0;\nPositionZ 0.0;\nRHSetForwardX 1.0;\nEndCTCPhysicsStandard;\nStartCTCEditor;\nEndCTCEditor;\nStartCTCChest;\nContainerContents[0] \"15360\";\nChestOpen FALSE;\nEndCTCChest;\nHealth 1.0;\nEndThing;\nXXXSectionEnd;\n";
    require(reviewTng(text).definitionIndependent,"supported object TNG refused");
    const auto fields=forge::defexternal::inventoryTngDefinitionFields(text);
    require(fields.storage.definitionIndependent && fields.fields.size()==2 &&
            fields.fields[1].component=="CTCChest" && fields.fields[1].name=="15360" &&
            fields.fields[1].line==20,"container definition inventory lost source identity");
    const auto refusedFields=forge::defexternal::inventoryTngDefinitionFields(text+"Unknown 1;\n");
    require(!refusedFields.storage.definitionIndependent && refusedFields.fields.empty(),
            "refused TNG leaked definition references");
    require(reviewTng("Version 2;\n").definitionIndependent,"empty TNG refused");
    const auto located=reviewTng("\r\nVersion 2;\r\n\r\nUnknown 1;\r\n");
    require(!located.definitionIndependent && located.line==4 && located.directive=="Unknown",
            "TNG refusal location lost blank or CRLF lines");
    auto badPosition=text;const auto positionAt=badPosition.find("PositionX 15360.0;");
    badPosition.replace(positionAt,18,"PositionX nan;");
    const auto positionReview=reviewTng(badPosition);
    require(!positionReview.definitionIndependent && positionReview.line==12 &&
            positionReview.directive=="CTCPhysicsStandard.PositionX","component refusal location missing");
    const auto eofReview=reviewTng("Version 2;\nXXXSectionStart NULL;\n");
    require(eofReview.line==3 && eofReview.directive=="EOF","TNG EOF location wrong");
    const auto gtgLocation=forge::defexternal::reviewGtg(
        "NEWMAP 1\nVersion 2;\nENDMAP\n\nNEWMAP 7\n\nVersion 2;\nUnknown 1;\nENDMAP\n");
    require(!gtgLocation.definitionIndependent && gtgLocation.line==8 &&
            gtgLocation.directive=="map 7: Unknown","GTG body location not translated to source");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{
            {"NewThing Object;","NewThing Unknown;"},{"Player -1;","Player 128;"},
            {"UID 18446744073709551615;","UID 18446744073709551616;"},
            {"DefinitionType \"OBJECT_15360\";","DefinitionType 15360;"},
            {"ScriptName NULL;","ScriptName 15360;"},
            {"ScriptName NULL;","ScriptName 1ROLE;"},
            {"ScriptName NULL;","ScriptName \"ROLE\";"},
            {"ScriptData \"NULL\";","ScriptData \"15360\";"},
            {"PositionX 15360.0;","PositionX nan;"},
            {"PositionX 15360.0;","DefinitionID 15360;"},
            {"EndCTCPhysicsStandard;","EndCTCEditor;"},
            {"ContainerContents[0]","ContainerContents[1]"},
            {"ContainerContents[0]","ContainerContents[00]"},
            {"ChestOpen FALSE;","ChestOpen 15360;"},
            {"StartCTCEditor;","StartCTCEditor;\nUnknown 15360;"},
            {"EndThing;",""},{"XXXSectionEnd;",""},
            {"Health 1.0;","Health 1.0; Unknown 15360;"}}) {
        auto bad=text;const auto at=bad.find(from);require(at!=bad.npos,"missing TNG mutation target");
        bad.replace(at,from.size(),to);
        if(reviewTng(bad).definitionIndependent)throw std::runtime_error("malformed TNG admitted: "+from+" -> "+to);
    }
    auto generator=text;const auto at=generator.find("StartCTCChest;");
    generator.replace(at,generator.find("EndCTCChest;",at)+12-at,
        "StartCTCCreatureGenerator;\nCreatureFamilies[0] \"FAMILY_15360\";\nGenerationRadius 15360.0;\nSelfTrigger TRUE;\nActiveCreatureLimit 15360;\nScriptNameOfAllGeneratedCreatures \"\";\nEndCTCCreatureGenerator;");
    require(reviewTng(generator).definitionIndependent,"named creature family refused");
    for(const std::string type:{"Marker","Building","Village","Holy Site"}) {
        auto inherited=text;inherited.replace(inherited.find("NewThing Object;"),16,"NewThing "+type+";");
        const auto end=inherited.find("EndThing;");
        inherited.insert(end,"StartCTCWallMount;\nBoughtForAmount 15360;\nTrophyID 18446744073709551615;\nEndCTCWallMount;\n");
        require(reviewTng(inherited).definitionIndependent,"inherited loader or thing UID refused");
        const auto at=inherited.find("TrophyID 18446744073709551615;");
        inherited.replace(at,std::string("TrophyID 18446744073709551615;").size(),"TrophyID 18446744073709551616;");
        require(!reviewTng(inherited).definitionIndependent,"overflowing component UID accepted");
    }
    require(!reviewTng(text+std::string(1,'\0')).definitionIndependent,"NUL suffix accepted");
    auto base=text;base.replace(base.find("NewThing Object;"),16,"NewThing Thing;");
    require(reviewTng(base).definitionIndependent,"reviewed default Health refused for base Thing");
    auto defaults=base;defaults.insert(defaults.find("EndThing;"),"ObjectScale 1.0;\n");
    require(reviewTng(defaults).definitionIndependent,"reviewed legacy base Thing defaults refused");
    const auto defaultFields=forge::defexternal::inventoryTngDefinitionFields(defaults);
    require(defaultFields.storage.definitionIndependent && defaultFields.fields.size()==2,
            "base Thing scalar defaults changed definition inventory");
    for(const std::string key:{"Health","ObjectScale"}) {
        for(const std::string value:{"0","2","nan","inf","1e100","1junk"}) {
            auto bad=defaults;const auto needle=key+" 1.0;";
            bad.replace(bad.find(needle),needle.size(),key+" "+value+";");
            require(!reviewTng(bad).definitionIndependent,"nondefault/malformed base Thing scalar admitted");
        }
        auto duplicate=defaults;duplicate.insert(duplicate.find("EndThing;"),key+" 1.0;\n");
        require(!reviewTng(duplicate).definitionIndependent,"duplicate base Thing scalar admitted");
    }
    auto unsupported=defaults;unsupported.insert(unsupported.find("EndThing;"),"AngleXY 1.0;\n");
    require(!reviewTng(unsupported).definitionIndependent,"unreviewed base Thing physical field admitted");
    auto misplaced=defaults;misplaced.insert(misplaced.find("EndCTCPhysicsStandard;"),"Health 1.0;\n");
    require(!reviewTng(misplaced).definitionIndependent,"base Thing default admitted inside a component");
    base.erase(base.find("Health 1.0;\n"),12);
    require(reviewTng(base).definitionIndependent,"base Thing with reviewed components refused");
    auto created=base;created.insert(created.find("EndThing;"),
        "CreateTC \"CTCDNavigationSeed\";\nStartCTCDNavigationSeed;\nEndCTCDNavigationSeed;\nStartCTCDCameraPoint;\nEndCTCDCameraPoint;\n");
    require(reviewTng(created).definitionIndependent,"reviewed empty components/class name refused");
    const std::string scratchpad="Version 2;\nXXXSectionStart NULL;\nNewThing AICreature;\nPlayer -1;\nUID 1;\nDefinitionType \"CREATURE_TEST\";\nStartCTCAIScratchpad;\nEndCTCAIScratchpad;\nEndThing;\nXXXSectionEnd;\n";
    const auto scratchFields=forge::defexternal::inventoryTngDefinitionFields(scratchpad);
    require(scratchFields.storage.definitionIndependent && scratchFields.fields.size()==1 &&
            scratchFields.fields[0].name=="CREATURE_TEST","retail empty AI scratchpad refused or changed definition inventory");
    for(const char* field:{"ActiveGossipCategories 0;\n","IsGossipForPlayer 0;\n","Gossip[0] 15360;\n"}) {
        auto bad=scratchpad;bad.insert(bad.find("EndCTCAIScratchpad;"),field);
        const auto reviewed=forge::defexternal::inventoryTngDefinitionFields(bad);
        require(!reviewed.storage.definitionIndependent && reviewed.fields.empty(),
                "unreviewed AI scratchpad data admitted or leaked references");
    }
    auto badCreated=created;badCreated.insert(badCreated.find("EndCTCDNavigationSeed;"),"Unknown 15360;\n");
    require(!reviewTng(badCreated).definitionIndependent,"unknown seed field admitted");
    badCreated=created;badCreated.insert(badCreated.find("EndCTCDCameraPoint;"),"PointType 15360;\n");
    require(!reviewTng(badCreated).definitionIndependent,"unreviewed legacy camera integer admitted");
    badCreated=created;const auto creation=badCreated.find("CreateTC \"CTCDNavigationSeed\";");
    badCreated.replace(creation,std::string("CreateTC \"CTCDNavigationSeed\";").size(),"CreateTC 15360;");
    require(!reviewTng(badCreated).definitionIndependent,"numeric component class name admitted");
    for(const std::string kind:{"CTCPhysicsNavigator","CTCPhysicsLight"}) {
        auto inherited=text;
        for(size_t at=0;(at=inherited.find("CTCPhysicsStandard",at))!=inherited.npos;at+=kind.size())
            inherited.replace(at,18,kind);
        if(kind=="CTCPhysicsLight") {
            require(!reviewTng(inherited).definitionIndependent,"light inherited unsupported orientation");
            inherited.erase(inherited.find("RHSetForwardX 1.0;\n"),19);
        }
        require(reviewTng(inherited).definitionIndependent,"confirmed physics serializer inheritance refused");
    }
    auto camera=text;camera.insert(camera.find("EndThing;"),
        "StartCTCCameraPointScripted;\nCoordBase C3DCoordF(15360,-2,0.5);\nThingUID 18446744073709551615;\nTransitionTime 1.5;\nEndCTCCameraPointScripted;\n"
        "StartCTCLight;\nColour CRGBColour(255,0,128,255);\nInnerRadius 15360;\nInverted FALSE;\nEndCTCLight;\n");
    const auto preserved=camera;
    require(reviewTng(camera).definitionIndependent,"camera vectors or light colour refused");
    require(camera==preserved,"text domain review mutated source");
    for(const std::string overridden:{"", "Overridden FALSE;\n", "Overridden TRUE;\n"}) {
        auto legacy=text;legacy.insert(legacy.find("EndThing;"),
            "StartCTCLight;\n"+overridden+"Interted FALSE;\nInverted TRUE;\nEndCTCLight;\n");
        const auto source=legacy;
        require(reviewTng(legacy).definitionIndependent,"inert legacy light default refused");
        const auto inventory=forge::defexternal::inventoryTngDefinitionFields(legacy);
        require(inventory.storage.definitionIndependent && inventory.fields.size()==2 && legacy==source,
                "legacy light default changed text or definition references");
        for(const std::string value:{"TRUE","false","0","FALSEjunk"}) {
            auto bad=legacy;bad.replace(bad.find("Interted FALSE"),14,"Interted "+value);
            require(!reviewTng(bad).definitionIndependent,"nondefault/malformed legacy light value admitted");
        }
        auto duplicate=legacy;duplicate.insert(duplicate.find("EndCTCLight;"),"Interted FALSE;\n");
        require(!reviewTng(duplicate).definitionIndependent,"duplicate legacy light field admitted");
    }
    auto misplacedLight=text;misplacedLight.insert(misplacedLight.find("EndThing;"),"Interted FALSE;\n");
    require(!reviewTng(misplacedLight).definitionIndependent,"legacy light field admitted outside its component");
    auto legacyDoor=text;legacyDoor.insert(legacyDoor.find("EndThing;"),
        "StartCTCDoor;\nOpen TRUE;\nDoorTriggerType 1;\nEndCTCDoor;\n");
    const auto doorSource=legacyDoor;
    require(reviewTng(legacyDoor).definitionIndependent,"canonical inert door annotation refused");
    const auto doorInventory=forge::defexternal::inventoryTngDefinitionFields(legacyDoor);
    require(doorInventory.storage.definitionIndependent && doorInventory.fields.size()==2 && legacyDoor==doorSource,
            "inert door annotation changed text or definition inventory");
    for(const std::string value:{"0","2","15360","-1","01","TRUE","1junk"}) {
        auto bad=legacyDoor;bad.replace(bad.find("DoorTriggerType 1"),17,"DoorTriggerType "+value);
        require(!reviewTng(bad).definitionIndependent,"unreviewed legacy door annotation admitted");
    }
    auto duplicateDoor=legacyDoor;duplicateDoor.insert(duplicateDoor.find("EndCTCDoor;"),"DoorTriggerType 1;\n");
    require(!reviewTng(duplicateDoor).definitionIndependent,"duplicate legacy door annotation admitted");
    auto misplacedDoor=text;misplacedDoor.insert(misplacedDoor.find("EndThing;"),"DoorTriggerType 1;\n");
    require(!reviewTng(misplacedDoor).definitionIndependent,"legacy door annotation admitted outside CTCDoor");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{
        {"C3DCoordF(15360,-2,0.5)","C3DCoordF(1,2)"},
        {"C3DCoordF(15360,-2,0.5)","C3DCoordF(1,2,3,4)"},
        {"C3DCoordF(15360,-2,0.5)","C3DCoordF(1,nan,3)"},
        {"C3DCoordF(15360,-2,0.5)","C3DCoordF(1,2,3)opaque"},
        {"CRGBColour(255,0,128,255)","CRGBColour(15360,0,128,255)"},
        {"CRGBColour(255,0,128,255)","CRGBColour(-1,0,128,255)"},
        {"CRGBColour(255,0,128,255)","CRGBColour(1,2,3)"},
        {"ThingUID 18446744073709551615","ThingUID 18446744073709551616"},
        {"TransitionTime 1.5","TrackThing TRUE"},
        {"Inverted FALSE","Interted TRUE"}}) {
        auto bad=camera;const auto at=bad.find(from);require(at!=bad.npos,"missing tuple mutation target");
        bad.replace(at,from.size(),to);
        require(!reviewTng(bad).definitionIndependent,"malformed camera/light field admitted");
    }
    auto spline=text;spline.insert(spline.find("EndThing;"),
        "StartCTCCameraPointScriptedSpline;\nNumKeyCameras 1;\n"
        "KeyCameras[0].Position C3DCoordF(15360,0,0);\nKeyCameras[0].LookDirection C3DCoordF(1,0,0);\n"
        "KeyCameras[0].FOV 0.2;\nKeyCameras[0].ShuttleSpeed 1;\nKeyCameras[0].Duration 2;\n"
        "KeyCameras[0].PauseTime 0;\nKeyCameras[0].Event \"\";\nEndCTCCameraPointScriptedSpline;\n");
    require(reviewTng(spline).definitionIndependent,"complete spline keyframe refused");
    auto events=spline;events.replace(events.find("KeyCameras[0].Event \"\";"),23,"KeyCameras[0].Event \"15360\";");
    events.insert(events.find("EndCTCCameraPointScriptedSpline;"),"ValidAnims[0] \"ANIM_15360\";\n");
    require(reviewTng(events).definitionIndependent,"spline event/animation names mistaken for numeric references");
    auto badAnims=events;badAnims.replace(badAnims.find("ValidAnims[0]"),13,"ValidAnims[1]");
    require(!reviewTng(badAnims).definitionIndependent,"nonsequential spline animation names admitted");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{
        {"NumKeyCameras 1","NumKeyCameras -1"},{"NumKeyCameras 1","NumKeyCameras 2147483647"},
        {"KeyCameras[0].Position","KeyCameras[1].Position"},
        {"KeyCameras[0].Position","KeyCameras[00].Position"},
        {"KeyCameras[0].Position","KeyCameras[0]Position"},
        {"KeyCameras[0].Position","KeyCameras[0].Unknown"},
        {"KeyCameras[0].Duration 2;\n",""},
        {"KeyCameras[0].Event \"\"","KeyCameras[0].Event 15360"}}) {
        auto bad=spline;const auto at=bad.find(from);require(at!=bad.npos,"missing spline mutation target");
        bad.replace(at,from.size(),to);
        require(!reviewTng(bad).definitionIndependent,"incomplete or opaque spline admitted");
    }
    auto hook=text;hook.insert(hook.find("EndThing;"),
        "StartCTCActionUseScriptedHook;\nVersionNumber 1;\nUsable TRUE;\n"
        "ReversedOnMiniMap FALSE;\nHiddenOnMiniMap FALSE;\nForceConfirmation TRUE;\n"
        "TeleportToRegionEntrance TRUE;\nEntranceConnectedToUID 18446744073709551615;\n"
        "CameraTrackUID 0;\nSoundName \"\";\nAnimationName \"\";\nReplacementObject 0;\n"
        "EndCTCActionUseScriptedHook;\n");
    require(reviewTng(hook).definitionIndependent,"default-only scripted Hook refused");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{
        {"VersionNumber 1;\n",""},{"VersionNumber 1","VersionNumber 0"},
        {"VersionNumber 1","VersionNumber 2"},{"ReplacementObject 0","ReplacementObject 2856"},
        {"ReplacementObject 0","ReplacementObject -1"},{"ReplacementObject 0","ReplacementObject 00"},
        {"SoundName \"\"","SoundName \"SOUND_TEST\""},
        {"AnimationName \"\"","AnimationName \"ANIM_TEST\""},
        {"CameraTrackUID 0","CameraTrackUID 18446744073709551616"},
        {"EntranceConnectedToUID 18446744073709551615","EntranceConnectedToUID -1"},
        {"Usable TRUE","Usable 1"},{"ForceConfirmation TRUE","Unknown TRUE"}}) {
        auto bad=hook;const auto at=bad.find(from);require(at!=bad.npos,"missing Hook mutation target");
        bad.replace(at,from.size(),to);
        require(!reviewTng(bad).definitionIndependent,"unreviewed Hook value admitted");
    }
    auto owned=text;owned.insert(owned.find("EndThing;"),
        "StartCTCOwnedEntity;\nVersionNumber 1;\nOwnerUID 18446744073709551615;\nEndCTCOwnedEntity;\n"
        "StartCTCDoor;\nOpen FALSE;\nEndCTCDoor;\n"
        "StartCTCBuyableHouse;\nWifeLivingHere 18446744073709551615;\nCurrentDressLevel 15360;\nVirtualMoneyBags 15360;\nEndCTCBuyableHouse;\n"
        "StartCTCEnemy;\nFactionName \"15360\";\nEndCTCEnemy;\n"
        "StartCTCCreatureGeneratorCreator;\nFrameToRegenerate 15360;\nGeneratorUID 18446744073709551615;\nEndCTCCreatureGeneratorCreator;\n"
        "StartCTCVillage;\nFramePlayerLastSeenByGuard -15360;\nLimbo FALSE;\nEndCTCVillage;\n"
        "StartCTCShop;\nDayLastChecked 15360;\nEndCTCShop;\n"
        "StartCTCObjectAugmentations;\nSavedInGame TRUE;\nAugmentationDefNames[0] \"15360\";\nEndCTCObjectAugmentations;\n"
        "StartCTCSearchableContainer;\nContainerContents[0] \"OBJECT_15360\";\nNumberOfTimesToSearch 15360;\nEndCTCSearchableContainer;\n");
    require(reviewTng(owned).definitionIndependent,"ownership/container domains refused");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{
        {"VersionNumber 1;\n",""},{"VersionNumber 1","VersionNumber 0"},
        {"OwnerUID 18446744073709551615","OwnerUID 18446744073709551616"},
        {"WifeLivingHere 18446744073709551615","WifeLivingHere 18446744073709551616"},
        {"CurrentDressLevel 15360","CurrentDressLevel 2147483648"},
        {"GeneratorUID 18446744073709551615","GeneratorUID 18446744073709551616"},
        {"DayLastChecked 15360","SpecialItem 15360"},
        {"AugmentationDefNames[0] \"15360\"","AugmentationDefNames[0] 15360"},
        {"AugmentationDefNames[0]","AugmentationDefNames[1]"},
        {"FramePlayerLastSeenByGuard -15360","PlayersCrimes[0] 15360"},
        {"Open FALSE","DoorTriggerType 15360"},
        {"FactionName \"15360\"","FactionName 15360"},
        {"NumberOfTimesToSearch 15360","NumberOfTimesToSearch 2147483648"}}) {
        auto bad=owned;const auto at=bad.find(from);require(at!=bad.npos,"missing ownership mutation target");
        bad.replace(at,from.size(),to);
        require(!reviewTng(bad).definitionIndependent,"unsupported ownership/container field admitted");
    }
    auto track=text;track.replace(track.find("NewThing Object;"),16,"NewThing TrackNode;");
    for (const std::string name : {"NULL%DayOnly", "Q_TEST%NightOnly", "15360", "15360%DayOnly"}) {
        auto section=text;
        section.replace(section.find("XXXSectionStart NULL;"),21,"XXXSectionStart "+name+";");
        require(reviewTng(section).definitionIndependent,"native day/night or numeric section name refused");
    }
    for (const std::string name : {"%DayOnly", "Q_TEST%", "Q_TEST%dayonly", "Q_TEST%Unknown", "Q_TEST%DayOnly%NightOnly"}) {
        auto section=text;
        section.replace(section.find("XXXSectionStart NULL;"),21,"XXXSectionStart "+name+";");
        require(!reviewTng(section).definitionIndependent,"unreviewed section suffix admitted");
    }
    track.insert(track.find("EndThing;"),"LinkedToUID1 18446744073709551615;\nLinkedToUID2 0;\nStart TRUE;\nEnd FALSE;\nScriptName NULL;\n");
    require(reviewTng(track).definitionIndependent,"track links or repeated identical name refused");
    auto badTrack=track;badTrack.replace(badTrack.rfind("ScriptName NULL;"),16,"ScriptName Different;");
    require(!reviewTng(badTrack).definitionIndependent,"conflicting track name admitted");
    badTrack=track;badTrack.insert(badTrack.find("EndThing;"),"LinkedToFileID1 15360;\n");
    require(!reviewTng(badTrack).definitionIndependent,"unreviewed track file ID admitted");
    auto sw=text;sw.replace(sw.find("NewThing Object;"),16,"NewThing Switch;");
    sw.insert(sw.find("EndThing;"),"TriggerRadius 15360;\nTimeToChangeEnvironmentDef 1.0;\nTriggeredByThing NULL;\nEnvironmentDef ENVIRONMENT_15360;\n");
    require(reviewTng(sw).definitionIndependent,"switch scalar/name fields refused");
    const auto switchFields=forge::defexternal::inventoryTngDefinitionFields(sw);
    require(switchFields.fields.size()==3 && switchFields.fields[1].field=="EnvironmentDef" &&
            switchFields.fields[1].name=="ENVIRONMENT_15360" && switchFields.fields[1].conditional,
            "conditional switch environment query omitted");
    auto nullSwitch=sw; nullSwitch.replace(nullSwitch.find("ENVIRONMENT_15360"),17,"NULL");
    const auto nullFields=forge::defexternal::inventoryTngDefinitionFields(nullSwitch);
    require(nullFields.storage.definitionIndependent && nullFields.fields[1].name.empty() &&
            nullFields.fields[1].conditional,"switch NULL did not normalize to empty query");
    for(const std::string field:{"EnvironmentDef","TriggeredByThing"}) {
        const auto begin=sw.find('\n'+field+' ')+field.size()+2;
        const auto length=sw.find(';',begin)-begin;
        for(const std::string value:{"15360","0","1NAME","\"NULL\"","NAME-WITH-DASH","NAME.WITH.DOT"}) {
            auto bad=sw;bad.replace(begin,length,value);
            const auto reviewed=forge::defexternal::inventoryTngDefinitionFields(bad);
            require(!reviewed.storage.definitionIndependent && reviewed.fields.empty(),
                    "non-string switch token accepted or leaked a definition query");
        }
        auto valid=sw;valid.replace(begin,length,"_NAME_15360");
        require(reviewTng(valid).definitionIndependent,"native underscore-prefixed switch name refused");
    }
    sw.replace(sw.find("TriggerRadius 15360;"),20,"TriggerRadius nan;");
    require(!reviewTng(sw).definitionIndependent,"nonfinite switch radius admitted");
    auto creature=text;creature.replace(creature.find("NewThing Object;"),16,"NewThing AICreature;");
    creature.insert(creature.find("EndThing;"),"HomeBuildingUID 18446744073709551615;\nSpouseCreatureUID 15360;\nInitialPosX -15360;\nOverridingBrainName BRAIN_15360;\nCanBeMarried TRUE;\n");
    require(reviewTng(creature).definitionIndependent,"creature identity/scalar fields refused");
    const auto brainFields=forge::defexternal::inventoryTngDefinitionFields(creature);
    require(brainFields.fields.size()==3 && brainFields.fields[1].field=="OverridingBrainName" &&
            brainFields.fields[1].name=="BRAIN_15360" && brainFields.fields[1].conditional,
            "conditional creature brain query omitted");
    auto nullBrain=creature;nullBrain.replace(nullBrain.find("BRAIN_15360"),11,"NULL");
    const auto emptyBrain=forge::defexternal::inventoryTngDefinitionFields(nullBrain);
    require(emptyBrain.storage.definitionIndependent && emptyBrain.fields[1].name.empty(),
            "creature NULL brain not normalized");
    for(const std::string value:{"15360","0","1BRAIN","\"NULL\"","\"BRAIN_15360\"","\"\"","BRAIN-NAME","BRAIN.NAME"}) {
        auto bad=creature;bad.replace(bad.find("BRAIN_15360"),11,value);
        const auto reviewed=forge::defexternal::inventoryTngDefinitionFields(bad);
        require(!reviewed.storage.definitionIndependent && reviewed.fields.empty(),
                "non-string brain token accepted or leaked a definition query");
    }
    auto underscoreBrain=creature;underscoreBrain.replace(underscoreBrain.find("BRAIN_15360"),11,"_BRAIN_15360");
    const auto underscoreFields=forge::defexternal::inventoryTngDefinitionFields(underscoreBrain);
    require(underscoreFields.storage.definitionIndependent && underscoreFields.fields[1].name=="_BRAIN_15360",
            "native underscore-prefixed brain name refused");
    creature.insert(creature.find("EndThing;"),"StartCTCCreatureOpinionOfHero;\nForcedAttitude 18;\nNumberOfTimesHit 15360;\nEndCTCCreatureOpinionOfHero;\n");
    require(reviewTng(creature).definitionIndependent,"reviewed opinion default refused");
    creature.insert(creature.find("EndThing;"),"StartCTCTalk;\nEndCTCTalk;\n");
    require(reviewTng(creature).definitionIndependent,"empty talk state refused");
    creature.insert(creature.find("EndThing;"),"StartCTCRandomAppearanceMorph;\nSeed 15360;\nEndCTCRandomAppearanceMorph;\n");
    require(reviewTng(creature).definitionIndependent,"PRNG seed mistaken for definition ID");
    auto signedSeed=creature;signedSeed.replace(signedSeed.find("Seed 15360;"),11,"Seed -2147483648;");
    require(reviewTng(signedSeed).definitionIndependent,"native signed spelling of uint32 seed refused");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{
        {"HomeBuildingUID 18446744073709551615","HomeBuildingUID 18446744073709551616"},
        {"InitialPosX -15360","InitialPosX nan"},{"CanBeMarried TRUE","CanBeMarried 15360"},
        {"CanBeMarried TRUE","HasRelationships TRUE"},{"Seed 15360","Seed 4294967296"},
        {"Seed 15360","Seed 2147483648"},{"Seed 15360","Seed -2147483649"}}) {
        auto bad=creature;const auto at=bad.find(from);require(at!=bad.npos,"missing creature mutation target");
        bad.replace(at,from.size(),to);
        require(!reviewTng(bad).definitionIndependent,"unsupported creature field admitted");
    }
    auto badOpinion=creature;badOpinion.replace(badOpinion.find("ForcedAttitude 18;"),18,"ForcedAttitude 15360;");
    require(!reviewTng(badOpinion).definitionIndependent,"unreviewed opinion enum admitted");
    auto characters=text;characters.insert(characters.find("EndThing;"),
        "StartCTCActivationReceptorDoor;\nDeactivateAfterSetTime FALSE;\nFramesAfterActivationToDeactivate 15360;\nEndCTCActivationReceptorDoor;\n"
        "StartCTCWife;\nGiftToGiveDef 0;\nBooleanHusbandAppearances[3] FALSE;\nGiftGivingPriceValue 15360;\nEndCTCWife;\n"
        "StartCTCGuard;\nLastCrimeSeenSeverity 3;\nBribePool 15360;\nEndCTCGuard;\n"
        "StartCTCHero;\nLastWeaponEquippedID 18446744073709551615;\nhero_title_object_def_name \"15360\";\nEndCTCHero;\n"
        "StartCTCCreatureOpinionOfHero;\nForcedAttitude 17;\nEndCTCCreatureOpinionOfHero;\n");
    require(reviewTng(characters).definitionIndependent,"reviewed character enums/sentinel refused");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{
        {"GiftToGiveDef 0","GiftToGiveDef 15360"},{"GiftToGiveDef 0","GiftToGiveDef -1"},
        {"LastWeaponEquippedID 18446744073709551615","LastWeaponEquippedID 18446744073709551616"},
        {"hero_title_object_def_name \"15360\"","hero_title_object_def_name 15360"},
        {"BooleanHusbandAppearances[3]","BooleanHusbandAppearances[4]"},
        {"LastCrimeSeenSeverity 3","LastCrimeSeenSeverity 4"},
        {"LastCrimeSeenSeverity 3","LastCrimeSeenSeverity -1"},
        {"ForcedAttitude 17","ForcedAttitude 19"},{"ForcedAttitude 17","ForcedAttitude -1"},
        {"BribePool 15360","PendingCrimes[0] 15360"},
        {"DeactivateAfterSetTime FALSE","TriggerOnActivate TRUE"}}) {
        auto bad=characters;const auto at=bad.find(from);require(at!=bad.npos,"missing character mutation target");
        bad.replace(at,from.size(),to);
        require(!reviewTng(bad).definitionIndependent,"unreviewed character value admitted");
    }
    auto route=text;route.insert(route.find("EndThing;"),
        "StartCTCPreCalculatedNavigationRoute;\nPrecCalculatedNavigationRouteVersion 2;\n"
        "ThingToCalculateRouteToUID 18446744073709551615;\nNumberOfStepsOnRoute 1;\n"
        "NavPosition0 C2DCoordF(15360,-15360);\nNavLayer0 255;\nEndCTCPreCalculatedNavigationRoute;\n");
    require(reviewTng(route).definitionIndependent,"bounded route coordinates/layer refused");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{
        {"PrecCalculatedNavigationRouteVersion 2;\n",""},{"PrecCalculatedNavigationRouteVersion 2","PrecCalculatedNavigationRouteVersion 1"},
        {"NumberOfStepsOnRoute 1","NumberOfStepsOnRoute 2147483647"},
        {"NumberOfStepsOnRoute 1","NumberOfStepsOnRoute -1"},{"NavLayer0 255;\n",""},
        {"NavLayer0 255","NavLayer0 256"},{"NavLayer0 255","NavLayer0 -1"},
        {"NavPosition0","NavPosition1"},{"NavPosition0","NavPosition00"},
        {"C2DCoordF(15360,-15360)","C2DCoordF(1,2,3)"}}) {
        auto bad=route;const auto at=bad.find(from);require(at!=bad.npos,"missing route mutation target");
        bad.replace(at,from.size(),to);
        require(!reviewTng(bad).definitionIndependent,"malformed route admitted");
    }
    auto shape=text;shape.insert(shape.find("EndThing;"),
        "StartCTCShapeManager;\nIsCoordsRelativeToMap TRUE;\nNumShapes 1;\n"
        "Shape[0].Type \"SHAPE_TYPE_LINE\";\nShape[0].size() 2;\n"
        "Shape[0].pos[0].X 15360;\nShape[0].pos[0].Y -15360;\nShape[0].pos[0].Z 0;\n"
        "Shape[0].pos[1].X 1;\nShape[0].pos[1].Y 2;\nShape[0].pos[1].Z 3;\nEndCTCShapeManager;\n");
    require(reviewTng(shape).definitionIndependent,"complete shape refused");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{
        {"NumShapes 1","NumShapes -1"},{"NumShapes 1","NumShapes 2147483647"},
        {"Shape[0].size() 2","Shape[0].size() 2147483647"},
        {"Shape[0].size() 2","Shape[0].size() -1"},
        {"SHAPE_TYPE_LINE","SHAPE_TYPE_UNKNOWN"},
        {"Shape[0].Type \"SHAPE_TYPE_LINE\";\n",""},
        {"Shape[0].pos[1].Z 3;\n",""},
        {"Shape[0].pos[1].Z 3","Shape[1].pos[1].Z 3"},
        {"Shape[0].pos[1].Z 3","Shape[0].pos[2].Z 3"},
        {"Shape[0].pos[1].Z 3","Shape[0].pos[01].Z 3"},
        {"Shape[0].pos[1].Z 3","Shape[0].pos[1].W 3"},
        {"Shape[0].pos[1].Z 3","Shape[0].pos[1].Z nan"}}) {
        auto bad=shape;const auto at=bad.find(from);require(at!=bad.npos,"missing shape mutation target");
        bad.replace(at,from.size(),to);
        require(!reviewTng(bad).definitionIndependent,"malformed shape admitted");
    }
    const auto gtg="NEWMAP 15360\n"+text+"ENDMAP\n";
    require(forge::defexternal::reviewGtg(gtg).definitionIndependent,"GTG local map slot mistaken for definition ID");
    for(const auto& bad:std::vector<std::string>{gtg+gtg,"NEWMAP 0\n"+text+"ENDMAP\n",
            "NEWMAP 2147483648\n"+text+"ENDMAP\n","NEWMAP 1\n"+text,gtg+"Opaque 15360;\n",
            "NEWMAP 1\nVersion 2;\nUnknown 15360;\nENDMAP\n"})
        require(!forge::defexternal::reviewGtg(bad).definitionIndependent,"unsupported GTG admitted");
}
static void wldChecks() {
    using forge::defexternal::reviewWld;
    const std::string header="START_INITIAL_QUESTS;\nQuest_15360;\nEND_INITIAL_QUESTS;\nMapUIDCount 15360;\nThingManagerUIDCount 18446744073709551615;\n";
    const std::string map="NewMap 1;\nMapX -15360;\nMapY 15361;\nLevelName \"15360.lev\";\nLevelScriptName \"15361\";\nMapUID 15360;\nIsSea FALSE;\nLoadedOnPlayerProximity TRUE;\nEndMap;\n";
    const std::string region="NewRegion 1;\nRegionName \"15360\";\nNewDisplayName \"TEXT_15360\";\nRegionDef \"REGION_15360\";\nAppearOnWorldMap;\nMiniMapGraphic MINIMAP_15360;\nMiniMapScale 1.0;\nMiniMapRegionExitTextOffsetX[Exit_15360] -15360.5;\nContainsMap \"15360.lev\";\nSeesMap \"15360.lev\";\nEndRegion;\n";
    const auto world=header+map+region;
    require(reviewWld(world).definitionIndependent,"known WLD domains refused");
    require(reviewWld(header).definitionIndependent,"empty WLD refused");
    for(const std::string quest:{"15360","1QUEST","\"Quest_15360\""}) {
        auto bad=header;bad.replace(bad.find("Quest_15360"),11,quest);
        const auto review=reviewWld(bad);
        require(!review.definitionIndependent && review.line==2 && review.directive==quest,
                "invalid initial quest token admitted or misplaced");
    }
    auto underscoreQuest=header;underscoreQuest.replace(underscoreQuest.find("Quest_15360"),11,"_Quest_15360");
    require(reviewWld(underscoreQuest).definitionIndependent,"underscore initial quest refused");
    const auto location=reviewWld(header+"MapUIDCount invalid;\n");
    require(!location.definitionIndependent && location.line==6 && location.directive=="MapUIDCount",
            "WLD refusal location wrong");
    for(const auto& [from,to]:std::vector<std::pair<std::string,std::string>>{
            {"MapX -15360;","Unknown 15360;"},{"MapX -15360;","MapX 12garbage;"},
            {"IsSea FALSE;","IsSea MAYBE;"},{"EndMap;","EndRegion;"},
            {"MapY 15361;","MapX 15361;"},{"NewMap 1;","NewMap 2;"},
            {"EndMap;",""},{"EndRegion;",""},{"MapUID 15360;","MapUID 4294967296;"},
            {"MiniMapScale 1.0;","MiniMapScale nan;"},{"MiniMapScale 1.0;","MiniMapScale 1e100;"},
            {"MiniMapGraphic MINIMAP_15360;","MiniMapGraphic 15360;"},
            {"MiniMapGraphic MINIMAP_15360;","MiniMapGraphic 1MINIMAP;"},
            {"OffsetX[Exit_15360]","OffsetX[Exit_15360]junk"},
            {"RegionDef \"REGION_15360\";","RegionDef 15360;"},
            {"MapX -15360;","MapX 1; Unknown 15360;"}}) {
        auto bad=world;const auto at=bad.find(from);require(at!=bad.npos,"missing WLD mutation target");
        bad.replace(at,from.size(),to);
        if(reviewWld(bad).definitionIndependent)throw std::runtime_error("malformed WLD admitted: "+from+" -> "+to);
    }
    require(!reviewWld(world+"Opaque 15360;\n").definitionIndependent,"unknown WLD suffix admitted");
    require(!reviewWld(world+std::string(1,'\0')).definitionIndependent,"NUL WLD suffix admitted");
    require(!reviewWld("START_INITIAL_QUESTS;\n").definitionIndependent,"unclosed quest list admitted");
    auto underscore=world;
    underscore.replace(underscore.find("MINIMAP_15360"),std::string("MINIMAP_15360").size(),"_MINIMAP_15360");
    require(reviewWld(underscore).definitionIndependent,"underscore minimap name refused");
}
static void bwdChecks() {
    using forge::defexternal::reviewBwd;
    forge::bwd::File world;
    forge::bwd::MapInfo map;map.levelName="15360.lev";map.scriptName="15361";
    map.left=15360;map.right=15361;map.top=-15360;map.bottom=15360;
    map.mapUid=uint64_t(15361)<<32|15360;world.addMap(map);
    forge::bwd::Region region;region.name="REGION";region.regionDef="15360";
    region.displayName="TEXT_15360";region.minimapGraphic="15361";
    region.contains={1};region.sees={1};region.mmOffX=15360;region.wmOffY=15361;
    region.minimapScale[0]=0;region.minimapScale[1]=60; // integer candidate inside raw float bits
    forge::bwd::RegionExit exit;exit.name="15360";exit.vec[1]=60;region.exits.push_back(exit);
    world.addRegion(region);
    const auto raw=world.serialize(),original=raw;
    require(reviewBwd(raw).definitionIndependent,"world numeric domains mistaken for definition IDs");
    require(raw==original,"compiled world review modified input");
    for(size_t n=0;n<raw.size();++n)
        require(!reviewBwd(Bytes(raw.begin(),raw.begin()+n)).definitionIndependent,"truncated world accepted");
    auto bad=raw;bad.push_back(0);require(!reviewBwd(bad).definitionIndependent,"opaque world suffix accepted");
    for(int32_t slot:{-1,0,2,15360}) {
        auto changed=world;changed.regions()[0].contains={slot};
        require(!reviewBwd(changed.serialize()).definitionIndependent,"invalid contains slot accepted");
        changed=world;changed.regions()[0].sees={slot};
        require(!reviewBwd(changed.serialize()).definitionIndependent,"invalid sees slot accepted");
    }
    auto changed=world;changed.maps()[0].used=2;
    require(!reviewBwd(changed.serialize()).definitionIndependent,"unknown world map flag accepted");
    changed=world;changed.regions()[0].soundThemes=2;
    require(!reviewBwd(changed.serialize()).definitionIndependent,"unknown world region flag accepted");
    changed=world;changed.regions()[0].regionDef=std::string("A\0B",3);
    require(!reviewBwd(changed.serialize()).definitionIndependent,"embedded world NUL accepted");
    forge::bwd::File empty;require(reviewBwd(empty.serialize()).definitionIndependent,"empty known world refused");
}
static Bytes chunk(const char* tag,const Bytes& body) {
    Bytes bytes(tag,tag+4);word(bytes,uint32_t(body.size()));append(bytes,body);return bytes;
}
static Bytes physicsImage(const Bytes& children) {
    Bytes bytes;word(bytes,0x3e3e3e3e);append(bytes,Bytes{'3','D','M','F'});
    word(bytes,1);word(bytes,0);append(bytes,chunk("3DRT",children));word(bytes,0);return bytes;
}
static void physicsChecks() {
    using forge::defexternal::reviewBigEntry;
    auto accepts=[](const Bytes& bytes) {
        return reviewBigEntry("MBANK_ALLMESHES",3,{},bytes).definitionIndependent;
    };
    Bytes material{'M',0};word(material,15360);material.push_back(1);material.resize(material.size()+24);
    Bytes extension;word(extension,1);word(extension,15361);append(material,chunk("MTLE",extension));
    Bytes map;word(map,0);append(map,Bytes{'D',0,'f','i','l','e',0});append(material,chunk("MMAP",map));
    Bytes children=chunk("MTLS",chunk("MTRL",material));
    Bytes helper;word(helper,3);helper.resize(helper.size()+60);helper.push_back(1);
    helper.resize(helper.size()+20);append(helper,Bytes{'H',0});append(children,chunk("HLPR",chunk("HDMY",helper)));
    Bytes vertices;word(vertices,1);vertices.resize(vertices.size()+32);
    Bytes primitive;word(primitive,15360);append(primitive,chunk("VERT",vertices));
    Bytes tris;word(tris,1);tris.resize(tris.size()+6);append(primitive,chunk("TRIS",tris));
    Bytes smoothing;word(smoothing,1);word(smoothing,15360);word(smoothing,0);
    append(primitive,chunk("SMTH",smoothing));append(children,chunk("PRIM",primitive));
    auto raw=physicsImage(children);const auto before=raw;
    require(accepts(raw),"complete physics local domains refused");
    require(raw==before,"physics review modified input");
    Bytes compressed;word(compressed,uint32_t(raw.size()));append(compressed,forge::lzo::compress(raw.data(),raw.size()));
    require(accepts(compressed),"compressed physics refused");
    compressed.push_back(0);require(!accepts(compressed),"physics compressed tail accepted");
    require(!reviewBigEntry("MBANK_ALLMESHES",3,Bytes(4),raw).definitionIndependent,"physics opaque info accepted");
    raw.back()=1;require(!accepts(raw),"physics unknown footer accepted");
    Bytes overflow;word(overflow,UINT32_MAX);
    require(!accepts(physicsImage(chunk("VERT",overflow))),"physics unbounded array accepted");
    smoothing.back()=1;
    require(!accepts(physicsImage(chunk("SMTH",smoothing))),"nonzero smoothing extension accepted");
    require(!accepts(physicsImage(chunk("CLTH",Bytes(8)))),"unreviewed cloth accepted");
    require(!accepts(physicsImage(chunk("BONE",Bytes(63)))),"short bone matrix accepted");
    helper[0]=4;require(!accepts(physicsImage(chunk("HDMY",helper))),"unknown helper version accepted");
    auto deep=chunk("TRFM",Bytes(48));
    for(unsigned i=0;i<18;++i)deep=chunk("HLPR",deep);
    require(!accepts(physicsImage(deep)),"unbounded physics recursion accepted");
    auto cloth=[](std::string_view source,uint8_t compiled=0) {
        Bytes body;word(body,2);append(body,Bytes{'C',0});word(body,15360);
        body.insert(body.end(),source.begin(),source.end());body.push_back(0);body.push_back(compiled);
        return physicsImage(chunk("CLTH",body));
    };
    const std::string declarations=
        "Particle name=P0 x=15360 y=0 z=0 alpha=1\n"
        "Particle name=P1 x=0 y=1 z=0 alpha=0.5\n"
        "Vertex name=V0 x=0 y=0 z=1\nTexCoord u=0 v=1\n";
    const std::string program=declarations+
        "Triangle a=P0 b=P1 c=V0 ta=0 tb=0 tc=0\n"
        "Distance a=P0 b=P1 dist=2.5 strength=0.7\n"
        "SetRenderMode bezier=0\nSetDragging enable=1 rotational=0 strength=0.25\n"
        "SetAcceleration enable=1 damping=0.5\nSetWindStrength strength=1.17\nSetGravityStrength strength=0\n";
    require(accepts(cloth(program)),"supported local cloth language refused");
    require(!accepts(cloth(program,1)),"compiled cloth silently accepted");
    require(!accepts(cloth(declarations+"Distance a=P0 b=V0 dist=1 strength=1\n")),
            "distance constraint accepted a non-particle symbol");
    require(!accepts(cloth(declarations+"Triangle a=P0 b=P1 c=MISSING ta=0 tb=0 tc=0\n")),
            "undefined cloth symbol accepted");
    require(!accepts(cloth(declarations+"Triangle a=P0 b=P1 c=V0 ta=0 tb=1 tc=0\n")),
            "cloth UV index exceeded its local table");
    require(!accepts(cloth(program+"Unknown def=15360\n")),"unknown cloth command accepted");
    require(!accepts(cloth("SetWindStrength strength=1 def=15360\n")),"unknown cloth parameter accepted");
    require(!accepts(cloth("SetWindStrength strength=1 strength=2\n")),"duplicate cloth parameter accepted");
    require(!accepts(cloth("SetWindStrength strength=nan\n")),"nonfinite cloth number accepted");
    require(!accepts(cloth("SetRenderMode bezier=2\n")),"invalid cloth boolean accepted");
    require(!accepts(cloth("SetWindStrength\n")),"missing cloth parameter accepted");
    require(!accepts(cloth(declarations+"Particle name=P0 x=0 y=0 z=0 alpha=1\n")),
            "duplicate cloth symbol accepted");
}
static void half(Bytes& b,uint16_t v) {b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));}
static Bytes meshBlock(const Bytes& raw,bool compressed=true) {
    require(raw.size()>=3,"test block too short");Bytes b;
    if(raw.size()>3) {
        if(compressed) {
            auto packed=forge::lzo::compress(raw.data(),raw.size()-3);
            half(b,uint16_t(packed.size()));append(b,packed);
        } else {half(b,0);b.insert(b.end(),raw.begin(),raw.end()-3);}
    }
    b.insert(b.end(),raw.end()-3,raw.end());return b;
}
static Bytes meshInfo(const Bytes& payload) {
    Bytes b;word(b,15360);b.resize(44);word(b,1);word(b,uint32_t(payload.size()));
    word(b,0);word(b,1);word(b,15361);return b;
}
static void meshChecks() {
    auto accepts=[](const Bytes& payload) {
        return forge::defexternal::reviewBigEntry("MBANK_ALLMESHES",1,meshInfo(payload),payload).definitionIndependent;
    };
    Bytes b{'M',0,0};b.resize(b.size()+40);
    half(b,1);half(b,0);half(b,3);half(b,0);half(b,0);
    Bytes helper;word(helper,15360);helper.resize(16);word(helper,0xffffffff);
    append(b,meshBlock(helper));append(b,Bytes{'H','P',0});
    word(b,1);word(b,1);word(b,0);word(b,0);b.resize(b.size()+5+48);
    word(b,0);append(b,Bytes{'m',0});for(int i=0;i<5;++i)word(b,15360+i);
    b.resize(b.size()+8+5);
    word(b,0);const auto repeats=b.size();word(b,1);b.resize(b.size()+20);
    word(b,1);word(b,0);word(b,3);word(b,1);word(b,3);
    const auto format=b.size();word(b,4);word(b,1);word(b,0);
    word(b,1);word(b,0);b.resize(b.size()+3);word(b,0); // static block
    b.resize(b.size()+32);word(b,12);word(b,0);
    const auto vertexBlock=b.size();append(b,meshBlock(Bytes(36)));
    append(b,meshBlock(Bytes{0,0,1,0,2,0},false));
    const auto clothCount=b.size();word(b,0);
    const auto original=b;require(accepts(b),"complete mesh storage refused");
    require(b==original,"mesh review modified input");
    auto info=meshInfo(b);auto pair=b;append(pair,b);
    require(forge::defexternal::reviewBigEntry("MBANK_ENGINE",1,info,b).definitionIndependent,"engine mesh refused");
    require(forge::defexternal::reviewBigEntry("MBANK_ALLMESHES",5,info,pair).definitionIndependent,"ghost mesh refused");
    pair.push_back(0);require(!forge::defexternal::reviewBigEntry("MBANK_ALLMESHES",5,info,pair).definitionIndependent,"ghost tail accepted");
    info.push_back(0);require(!forge::defexternal::reviewBigEntry("MBANK_ALLMESHES",1,info,b).definitionIndependent,"mesh info tail accepted");
    b[format]=99;require(!accepts(b),"unknown vertex layout accepted");b=original;
    for(size_t i=0;i<4;++i)b[repeats+i]=255;
    require(!accepts(b),"unbounded repeats accepted");b=original;
    b[49]=1;require(!accepts(b),"unreviewed volume accepted");b=original;
    b[51]=1;require(!accepts(b),"unreviewed generator accepted");b=original;
    b[2]=2;require(!accepts(b),"nonboolean mesh flag accepted");b=original;
    // Add a byte inside the declared compressed packet after its end marker.
    const size_t n=size_t(b[vertexBlock])|(size_t(b[vertexBlock+1])<<8);
    b.insert(b.begin()+vertexBlock+2+n,0);b[vertexBlock]=uint8_t(n+1);b[vertexBlock+1]=uint8_t((n+1)>>8);
    require(!accepts(b),"LZO packet suffix accepted");b=original;
    for(size_t n=0;n<b.size();++n)require(!accepts(Bytes(b.begin(),b.begin()+n)),"truncated mesh accepted");
    Bytes program;word(program,333);word(program,0);program.resize(program.size()+12);word(program,0);
    program.resize(program.size()+19); // gravity/wind, dragging, acceleration, damping
    for(int i=0;i<7;++i)word(program,0); // all geometry/index arrays
    program.resize(program.size()+5);word(program,0);word(program,0);word(program,0);
    auto withCloth=[&](const Bytes& source) {
        Bytes result(original.begin(),original.begin()+clothCount);word(result,1);
        word(result,0);word(result,0);word(result,uint32_t(source.size()));append(result,meshBlock(source));return result;
    };
    require(accepts(withCloth(program)),"complete compiled cloth refused");
    auto constrained=program;Bytes constraints;word(constraints,2);word(constraints,1);word(constraints,16);
    word(constraints,0);word(constraints,1);word(constraints,15360);word(constraints,0);
    constrained[4]=uint8_t(constraints.size());
    constrained[20]=2;constrained.insert(constrained.begin()+24,32,0);
    constrained.insert(constrained.begin()+8,constraints.begin(),constraints.end());
    require(accepts(withCloth(constrained)),"local cloth constraint refused");
    constrained[8+12]=2;
    require(!accepts(withCloth(constrained)),"cloth constraint exceeded local particle table");
    constrained[8+12]=0;constrained[8]=6;
    require(!accepts(withCloth(constrained)),"unknown constraint opcode accepted");
    program[0]=0;require(!accepts(withCloth(program)),"unknown cloth version accepted");program[0]=77;
    program.push_back(0);require(!accepts(withCloth(program)),"compiled cloth suffix accepted");program.pop_back();
    program[program.size()-12]=1;require(!accepts(withCloth(program)),"unreviewed exported cloth particle accepted");
}
static void lightingTgaChecks() {
    Bytes pixels(18);pixels[2]=2;pixels[12]=2;pixels[14]=1;pixels[16]=24;
    pixels.insert(pixels.end(),{0,60,0,255,255,255}); // Includes an ID-like colour.
    pixels.resize(pixels.size()+8);
    constexpr char signature[]="TRUEVISION-XFILE.";
    pixels.insert(pixels.end(),signature,signature+sizeof(signature));
    auto accepts=[](const Bytes& b){return forge::defexternal::reviewLightingTga(b).definitionIndependent;};
    const auto original=pixels;
    require(accepts(pixels) && pixels==original,"complete lighting TGA refused or modified");
    for(size_t cut=0;cut<pixels.size();++cut)
        require(!accepts(Bytes(pixels.begin(),pixels.begin()+cut)),"truncated lighting TGA accepted");
    for(const auto [at,value]:std::vector<std::pair<size_t,uint8_t>>{
        {0,1},{1,1},{2,10},{3,1},{8,1},{12,0},{14,0},{16,32},{17,32},{24,1},{32,0}}) {
        auto bad=pixels;bad[at]=value;require(!accepts(bad),"unsupported lighting TGA storage accepted");
    }
    pixels.push_back(0);require(!accepts(pixels),"lighting TGA trailing bytes accepted");
}
static int audit(const std::filesystem::path& root) {
    nlohmann::json report{{"scope","read-only supported external BIG-entry layouts; not installation admission"},
        {"root",root.string()},{"files",nlohmann::json::array()}};
    for(const char* relative:{"data/graphics/pc/textures.big","data/graphics/pc/frontend.big",
                             "data/graphics/graphics.big","data/lang/English/text.big"}) {
        const auto file=forge::big::File::open(root/relative);
        nlohmann::json row{{"file",relative},{"accepted",0},{"refusals",nlohmann::json::array()}};
        for(const auto& bank:file.banks())for(const auto& entry:bank.entries) {
            const auto result=forge::defexternal::reviewBigEntry(bank.name,entry.type,entry.subHeader,file.entryData(entry));
            if(result.definitionIndependent) row["accepted"]=row["accepted"].get<size_t>()+1;
            else row["refusals"].push_back({{"bank",bank.name},{"type",entry.type},{"id",entry.id},
                {"name",entry.name},{"reason",result.reason}});
        }
        report["files"].push_back(std::move(row));
    }
    std::cout<<report.dump(2)<<'\n';return 0;
}
int main(int argc,char** argv) {try {
    if((argc==3 || (argc==4 && std::string_view(argv[3])=="--symbols")) && std::string_view(argv[1])=="--tng-inventory") {
        const bool symbols=argc==4;
        namespace fs=std::filesystem;const fs::path path=argv[2];
        nlohmann::json report={{"files",0},{"accepted",0},{"refused",0},{"complete",true},
            {"refusalReasons",nlohmann::json::object()},{"refusals",nlohmann::json::array()}};
        if(symbols) {
            report["definition_reference_scope"]=forge::defexternal::tngDefinitionInventoryScope;
            report["definition_references"]=nlohmann::json::array();
            report["component_definition_references"]=nlohmann::json::array();
        }
        auto review=[&](const std::string& name,const std::string& text) {
            auto extension=fs::path(name).extension().string();
            std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char ch){return char(std::tolower(ch));});
            const auto result=extension==".gtg"?forge::defexternal::reviewGtg(text):forge::defexternal::reviewTng(text);
            report["files"]=report["files"].get<size_t>()+1;
            const char* count=result.definitionIndependent?"accepted":"refused";
            report[count]=report[count].get<size_t>()+1;
            if(!result.definitionIndependent) {
                report["complete"]=false;auto& reasons=report["refusalReasons"];
                reasons[result.reason]=reasons.value(result.reason,size_t(0))+1;
                report["refusals"].push_back({{"path",name},{"reason",result.reason},
                    {"line",result.line},{"directive",result.directive}});
            } else if(symbols) {
                const auto inventory=extension==".gtg"?forge::defexternal::inventoryGtgDefinitionFields(text):
                    forge::defexternal::inventoryTngDefinitionFields(text);
                for(const auto& field:inventory.fields) {
                    nlohmann::json value={{"file",name},{"line",field.line},
                        {"thing_type",field.thingType},{"definition",field.name}};
                    if(field.conditional)value["conditional"]=true;
                    if(field.component.empty() && field.field!="DefinitionType")value["field"]=field.field;
                    if(field.worldMapSlot)value["world_map_slot"]=field.worldMapSlot;
                    if(field.component.empty()) report["definition_references"].push_back(std::move(value));
                    else {
                        value["component"]=field.component;value["field"]=field.field;
                        report["component_definition_references"].push_back(std::move(value));
                    }
                }
            }
        };
        auto isText=[](const fs::path& file) {
            auto ext=file.extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char ch){return char(std::tolower(ch));});
            return ext==".tng" || ext==".gtg";
        };
        if(fs::is_directory(path)) {
            std::vector<fs::path> paths;
            for(const auto& entry:fs::recursive_directory_iterator(path))if(entry.is_regular_file() && isText(entry.path()))paths.push_back(entry.path());
            std::sort(paths.begin(),paths.end());
            for(const auto& file:paths) {
                std::ifstream input(file,std::ios::binary);require(bool(input),"cannot read thing text");
                review(file.string(),std::string((std::istreambuf_iterator<char>(input)),{}));
            }
        } else {
            const auto metadata=forge::wadstorage::validateEnvelope(path);
            const auto archive=forge::wad::Archive::open(path);
            for(const auto& entry:metadata.metadata.entries)if(isText(entry.name)) {
                const auto bytes=archive.read(entry);review(entry.name,std::string(bytes.begin(),bytes.end()));
            }
        }
        std::cout<<report.dump()<<'\n';return 0; // Inventory, not an admission gate.
    }
    if(argc==3 && std::string_view(argv[1])=="--tng") {
        std::ifstream file(argv[2],std::ios::binary);require(bool(file),"cannot open TNG");
        const std::string text((std::istreambuf_iterator<char>(file)),{});
        const auto result=forge::defexternal::reviewTng(text);
        std::cout<<nlohmann::json{{"definitionIndependent",result.definitionIndependent},{"reason",result.reason}}.dump()<<'\n';
        return result.definitionIndependent?0:1;
    }
    tngChecks();
    if(argc==3 && std::string_view(argv[1])=="--wld") {
        std::ifstream file(argv[2],std::ios::binary);require(bool(file),"cannot open WLD");
        const std::string text((std::istreambuf_iterator<char>(file)),{});
        const auto result=forge::defexternal::reviewWld(text);
        std::cout<<nlohmann::json{{"definitionIndependent",result.definitionIndependent},{"reason",result.reason}}.dump()<<'\n';
        return result.definitionIndependent?0:1;
    }
    wldChecks();
    if(argc==3 && std::string_view(argv[1])=="--bwd") {
        std::ifstream input(argv[2],std::ios::binary);if(!input)throw std::runtime_error("cannot read BWD input");
        const Bytes bytes((std::istreambuf_iterator<char>(input)),{});
        const auto result=forge::defexternal::reviewBwd(bytes);
        const nlohmann::json report={{"definition_independent",result.definitionIndependent},{"reason",result.reason}};
        std::cout<<report.dump(2)<<'\n';return result.definitionIndependent?0:1;
    }
    if(argc==3 && std::string_view(argv[1])=="--lighting-tga") {
        std::ifstream input(argv[2],std::ios::binary);if(!input)throw std::runtime_error("cannot read lighting TGA");
        const Bytes bytes((std::istreambuf_iterator<char>(input)),{});
        const auto result=forge::defexternal::reviewLightingTga(bytes);
        std::cout<<nlohmann::json{{"definition_independent",result.definitionIndependent},{"reason",result.reason}}.dump()<<'\n';
        return result.definitionIndependent?0:1;
    }
    if(argc==2)return audit(argv[1]);
    if(argc!=1)return 2;
    physicsChecks();
    meshChecks();
    bwdChecks();
    lightingTgaChecks();
    using forge::defexternal::reviewBigEntry;
    require(!reviewBigEntry("UNKNOWN",0,{},{}).definitionIndependent,"empty unknown entry accepted");
    require(!reviewBigEntry("MBANK_ALLMESHES",1,Bytes(64),{}).definitionIndependent,"mesh implicitly accepted");
    Bytes texture(34);texture[0]=8;texture[2]=8;texture[6]=8;texture[8]=8;texture[10]=1;
    texture[12]=forge::terraintex::kFormatDXT5;texture[16]=3;texture[17]=4;texture[20]=64;
    std::copy_n(forge::terraintex::pixelFormatTail(forge::terraintex::kFormatDXT5),6,texture.begin()+28);
    Bytes pixels(85);const auto original=texture;
    require(reviewBigEntry("GBANK_MAIN_PC",0,texture,pixels).definitionIndependent,"valid DXT5 refused");
    require(texture==original && pixels==Bytes(85),"review modified inputs");
    texture.push_back(0);
    require(!reviewBigEntry("GBANK_MAIN_PC",0,texture,pixels).definitionIndependent,"opaque texture info tail accepted");
    require(!reviewBigEntry("GBANK_MAIN_PC",99,original,pixels).definitionIndependent,"unknown texture type accepted");
    pixels.pop_back();require(!reviewBigEntry("GBANK_MAIN_PC",0,original,pixels).definitionIndependent,"short texture accepted");
    forge::textbig::Entry text;text.content="15360";text.identifier="SAMPLE";text.tags={{0,"ANIM:TEST"}};
    auto payload=forge::textbig::encode(text);
    require(reviewBigEntry("TEXT_ENGLISH_MAIN",0,Bytes(4),payload).definitionIndependent,"text content confused with definition IDs");
    payload.push_back(0);require(!reviewBigEntry("TEXT_ENGLISH_MAIN",0,Bytes(4),payload).definitionIndependent,"text tail accepted");
    text.type=1;text.groupMembers={15360,15361};payload=forge::textbig::encode(text);
    require(reviewBigEntry("TEXT_ENGLISH_MAIN",1,{},payload).definitionIndependent,"local text IDs treated as definition IDs");
    require(!reviewBigEntry("TEXT_ENGLISH_MAIN",1,Bytes(4),payload).definitionIndependent,"opaque group info accepted");
    payload.pop_back();require(!reviewBigEntry("TEXT_ENGLISH_MAIN",1,{},payload).definitionIndependent,"truncated group accepted");
    Bytes narrators;word(narrators,2);narrators.insert(narrators.end(),{'A',0,'B',0});
    require(reviewBigEntry("TEXT_ENGLISH_MAIN",2,narrators,{}).definitionIndependent,"narrator strings refused");
    narrators.pop_back();require(!reviewBigEntry("TEXT_ENGLISH_MAIN",2,narrators,{}).definitionIndependent,"unterminated narrator accepted");
    // Minimal complete raw 3DAF root with no tracks, then its observed zero footer.
    Bytes animation;word(animation,0x3e3e3e3e);animation.insert(animation.end(),{'3','D','A','F'});
    word(animation,0);word(animation,0);animation.insert(animation.end(),{'A','N','R','T'});
    word(animation,5);animation.push_back(0);word(animation,0);word(animation,0);
    require(reviewBigEntry("MBANK_ALLMESHES",9,Bytes(24),animation).definitionIndependent,"complete animation refused");
    require(!reviewBigEntry("MBANK_ALLMESHES",9,Bytes(25),animation).definitionIndependent,"opaque animation info accepted");
    animation[20]=100;require(!reviewBigEntry("MBANK_ALLMESHES",9,Bytes(24),animation).definitionIndependent,"malformed animation accepted");
    std::cout<<"external definition-domain review checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
