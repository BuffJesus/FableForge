// Native cutscene macro verbs recognized by RunCutsceneMacro_Func (0x00CBFB7D)
// in retail Fable: The Lost Chapters. Extracted from the binary's strncmp
// dispatch chain (evidence: FableTLC/ghidra_out/cutscene_native_verbs.txt,
// runcutscenemacro_full.c). Do not hand-edit; regenerate from the RE output.
//
// Dispatch is case-sensitive strncmp PREFIX match: a command's verb token is
// handled if some entry here is a prefix of it. Entity-scoped commands
// (NAME.Verb) are matched from the '.' onward, so those entries start with '.'.
#ifndef FORGE_CUTSCENE_VERBS_HPP
#define FORGE_CUTSCENE_VERBS_HPP

#include <array>
#include <cstddef>
#include <string_view>

namespace forge::cutscene {

inline constexpr std::array<std::string_view, 184> kNativeVerbs = {{
    ".AddScriptedMode",
    ".AILevel",
    ".ClearCommands",
    ".Collide",
    ".DataSpeak",
    ".Decapitate",
    ".DialogadSpeak",
    ".DialogSpeak",
    ".DoBossFight",
    ".Drawable",
    ".EntitySetMaxRunningSpeed",
    ".EntitySetMaxWalkingSpeed",
    ".FadeCross",
    ".FadeIn",
    ".FadeOut",
    ".FightStop",
    ".FightWith",
    ".FollowNavRoute",
    ".FollowThing",
    ".HoldInHand",
    ".InteractiveSpeak",
    ".InteractiveSpeakGroup",
    ".Killable",
    ".LookAt",
    ".LookAtNothing",
    ".LookInDirection",
    ".LookToCamera",
    ".LookToThing",
    ".ModifyHealth",
    ".PlayAnimation",
    ".PlayCombatAnim",
    ".PlayLoopingAnim",
    ".PreloadAnim",
    ".Release",
    ".RemoveScriptedMode",
    ".ResetPos",
    ".RunTo",
    ".SetAlpha",
    ".SetAppearanceSeed",
    ".SetAttackable",
    ".SetBound",
    ".SetDamageable",
    ".SetDrunk",
    ".SetFree",
    ".SetPushable",
    ".SetScared",
    ".Sheathe",
    ".SlideTeleport",
    ".SneakTo",
    ".Speak",
    ".StopFollowingThing",
    ".SummonerAttack",
    ".Teleport",
    ".TeleportInFrontOf",
    ".TurnInto",
    ".WaitForAnimationEvent",
    ".WaitForUnderRadius",
    ".WaitPlayAnimation",
    ".WaitTask",
    ".WalkTo",
    ".WalkUpToThing",
    "AnimationPause",
    "AskQuestion",
    "AToSkip",
    "CacheMusic",
    "CameraEffect",
    "CameraFOVLookBetween",
    "CameraFOVLookBetweenPos",
    "CameraLookAt",
    "CameraLookBetween",
    "CameraPath",
    "CameraPause",
    "CameraPreload",
    "CameraRig",
    "CameraRotateThing",
    "CameraShake",
    "Collide",
    "Create",
    "CreateEffect",
    "CreateLight",
    "CreateNear",
    "CreditScreen",
    "CrowdAcquire",
    "CrowdAnimate",
    "CrowdClearActions",
    "CrowdCollide",
    "CrowdCombatAnimate",
    "CrowdCreate",
    "CrowdCreateMixed",
    "CrowdKill",
    "CrowdLookAt",
    "CrowdLookTo",
    "CrowdMove",
    "CrowdRipplePosition",
    "CrowdTeleport",
    "CrowdTeleportRipple",
    "DebugCamera",
    "DoCameraPreloading",
    "DoCharacterPreload",
    "DoOneFrame",
    "DoScriptFrame",
    "DrawThing",
    "DummyEffect",
    "EnableBlackScreenSubtitles",
    "EnableSounds",
    "ExitGame",
    "FadeIn",
    "FadeOut",
    "FadeThingIn",
    "FadeThingOut",
    "FallbackAcquire",
    "Fullscreen",
    "GameInfo",
    "GamePause",
    "Get",
    "GiveGold",
    "GiveHero",
    "GiveHeroExpression",
    "GiveHeroHealth",
    "GiveHeroMorality",
    "HeroHair",
    "HeroTattoo",
    "HeroWear",
    "HideBodies",
    "HUD",
    "KeepEntityMap",
    "LiftRock",
    "LookAtNothing",
    "MuteSounds",
    "NoDialogCam",
    "NoLoadUseCamera",
    "ObjectCreate",
    "PauseThing",
    "PlayAVI",
    "PlayMusic",
    "PlayObjectAnim",
    "PlaySound",
    "Print",
    "PutInFrontOf",
    "PutInHeroHands",
    "PutUpYourSwords",
    "RegisterActor",
    "RegisterScript",
    "Remove",
    "RemoveAll",
    "RemoveAllThings",
    "RemoveEffect",
    "RemoveExtras",
    "RemoveHeroClothes",
    "RemoveHeroWeapons",
    "ResetCamera",
    "ReturnFollowers",
    "ScriptEntity",
    "ScriptFrame",
    "SetChestOpen",
    "SetDoorOpen",
    "SetFlag",
    "SetGravityOnThing",
    "SetHeroWeapon",
    "SetHomePosThing",
    "SetLightScene",
    "SetThingConscious",
    "SetTime",
    "SlideTeleport",
    "SmashWindows",
    "StartProgressSpinner",
    "StartTimeCode",
    "StayFadedOut",
    "StopProgressSpinner",
    "TakeFromHero",
    "TakeObjectFromHero",
    "TeleportFollowers",
    "TeleportThing",
    "TeleportToHSP",
    "TintScreenOut",
    "TintScreenTo",
    "UseCamera",
    "UseCameraFOVMarkerList",
    "UseTheme",
    "WaitActiveDialog",
    "WaitBossFight",
    "WaitFlag",
    "WaitForCamera",
    "WaitForMessageCamera",
}};

// The verb token of a command is the text before the first space or comma.
inline std::string_view verbToken(std::string_view command) {
    const size_t end = command.find_first_of(" ,");
    return end == std::string_view::npos ? command : command.substr(0, end);
}

// Reproduce RunCutsceneMacro_Func's dispatch: a case-sensitive strncmp PREFIX
// match. An entity-scoped command (NAME.Verb) is matched from the first '.'
// onward (native entries for those start with '.'); a global command is matched
// whole. Returns the longest native verb that is a prefix of the compared token,
// or an empty view if the engine would silently ignore the command.
inline std::string_view resolveVerb(std::string_view token) {
    const size_t dot = token.find('.');
    const bool scoped = dot != std::string_view::npos;
    const std::string_view compared = scoped ? token.substr(dot) : token;
    std::string_view best;
    for (const auto& verb : kNativeVerbs) {
        if (verb.empty()) continue;
        if ((verb.front() == '.') != scoped) continue;
        if (compared.size() >= verb.size() &&
            compared.substr(0, verb.size()) == verb && verb.size() > best.size()) {
            best = verb;
        }
    }
    return best;
}

// Length of the token the engine actually compared (whole verb for a global
// command, the `.Verb` slice for an entity command). A native match shorter than
// this means dispatch only succeeded because strncmp stopped early (prefix slop).
inline size_t comparedTokenLength(std::string_view token) {
    const size_t dot = token.find('.');
    return dot == std::string_view::npos ? token.size() : token.size() - dot;
}

}  // namespace forge::cutscene

#endif  // FORGE_CUTSCENE_VERBS_HPP
