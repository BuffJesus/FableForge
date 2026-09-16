#include "forge/thingplacer.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace forge::thingplacer {
namespace {

std::string boolText(bool value) { return value ? "TRUE" : "FALSE"; }

std::string quote(const std::string& value) {
    return std::string("\"") + value + "\"";
}

std::string stripQuotes(std::string value) {
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

bool parseUid(const std::string& text, uint64_t& out) {
    if (text.empty()) return false;
    uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    out = value;
    return true;
}

void appendLine(std::string& out, const std::string& body,
                const std::string& eol) {
    out += body;
    out += ';';
    out += eol;
}

void appendVec(std::string& out, const char* prefix, const Vec3& v,
               const std::string& eol) {
    appendLine(out, std::string(prefix) + "X " + formatFloat(v.x), eol);
    appendLine(out, std::string(prefix) + "Y " + formatFloat(v.y), eol);
    appendLine(out, std::string(prefix) + "Z " + formatFloat(v.z), eol);
}

} // namespace

std::string formatFloat(float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(value));
    std::string text(buffer);
    const size_t dot = text.find('.');
    if (dot == std::string::npos) return text + ".0";
    size_t last = text.size();
    while (last > dot + 2 && text[last - 1] == '0') --last;
    text.resize(last);
    // Retail never spells a negative zero.
    if (text == "-0.0") return "0.0";
    return text;
}

bool uidIsFree(const tng::File& file, uint64_t uid) {
    for (const tng::Thing& thing : file.things()) {
        const auto raw = thing.find("UID");
        if (!raw) continue;
        uint64_t existing = 0;
        if (parseUid(stripQuotes(*raw), existing) && existing == uid) return false;
    }
    return true;
}

uint64_t nextUid(const tng::File& file) {
    uint32_t highest = 0;
    bool any = false;
    for (const tng::Thing& thing : file.things()) {
        const auto raw = thing.find("UID");
        if (!raw) continue;
        uint64_t existing = 0;
        if (!parseUid(stripQuotes(*raw), existing)) continue;
        if ((existing >> 32) != kUidHighDword) continue;
        const uint32_t low = static_cast<uint32_t>(existing & 0xFFFFFFFFull);
        if (!any || low > highest) highest = low;
        any = true;
    }
    uint64_t candidate = (kUidHighDword << 32) |
                         static_cast<uint64_t>(any ? highest + 1u : 1u);
    while (!uidIsFree(file, candidate)) ++candidate;
    return candidate;
}

bool scriptNameIsFree(const tng::File& file, const std::string& scriptName) {
    if (scriptName.empty() || scriptName == "NULL") return true;
    for (const tng::Thing& thing : file.things()) {
        if (stripQuotes(thing.scriptName()) == scriptName) return false;
    }
    return true;
}

Vec3 forwardFromYawDegrees(float degrees) {
    const double radians =
        static_cast<double>(degrees) * 3.14159265358979323846 / 180.0;
    // Retail never writes a true unit basis vector: the editor quantizes the
    // whole vector to length kGroundUpZ (0.999994). Census over all 21,764
    // retail things -- "RHSetForwardX 1.0" and "-1.0" occur ZERO times, while
    // +/-0.999994 occur 504 and 522 times. It is not only the axis-aligned
    // case: 0.499997 = 0.5 * 0.999994, 0.999385 = cos(2deg) * 0.999994, and the
    // LookoutPoint signpost's (-0.978137, 0.207936) has magnitude 0.999994
    // exactly. So scale by it rather than emitting a unit vector.
    Vec3 forward;
    forward.x = static_cast<float>(std::cos(radians) * kGroundUpZ);
    forward.y = static_cast<float>(std::sin(radians) * kGroundUpZ);
    forward.z = 0.0f;
    return forward;
}

float terrainHeightAt(const lev::File& level, float localX, float localY) {
    const int maxX = level.cellsX() - 1;
    const int maxY = level.cellsY() - 1;
    if (!(localX >= 0.0f) || !(localY >= 0.0f) ||
        localX > static_cast<float>(maxX) || localY > static_cast<float>(maxY)) {
        throw std::runtime_error(
            "thingplacer: local position (" + formatFloat(localX) + ", " +
            formatFloat(localY) + ") is outside the " +
            std::to_string(level.cellsX()) + "x" + std::to_string(level.cellsY()) +
            " cell grid of " + level.source());
    }
    const int x0 = std::min(static_cast<int>(localX), maxX);
    const int y0 = std::min(static_cast<int>(localY), maxY);
    const int x1 = std::min(x0 + 1, maxX);
    const int y1 = std::min(y0 + 1, maxY);
    const float tx = localX - static_cast<float>(x0);
    const float ty = localY - static_cast<float>(y0);
    const float h00 = level.heightAt(x0, y0);
    const float h10 = level.heightAt(x1, y0);
    const float h01 = level.heightAt(x0, y1);
    const float h11 = level.heightAt(x1, y1);
    return h00 * (1.0f - tx) * (1.0f - ty) + h10 * tx * (1.0f - ty) +
           h01 * (1.0f - tx) * ty + h11 * tx * ty;
}

Vec3 worldToLocal(const Vec3& world, int mapX, int mapY) {
    Vec3 local = world;
    local.x = world.x - static_cast<float>(mapX);
    local.y = world.y - static_cast<float>(mapY);
    return local;
}

RoutedPosition routeWorldPosition(const Vec3& world,
                                  const std::vector<MapTile>& tiles) {
    if (!std::isfinite(world.x) || !std::isfinite(world.y) ||
        !std::isfinite(world.z))
        throw std::runtime_error("thingplacer: world position is not finite");
    std::optional<RoutedPosition> result;
    for (size_t i = 0; i < tiles.size(); ++i) {
        const auto& tile = tiles[i];
        if (tile.width <= 0 || tile.height <= 0)
            throw std::runtime_error("thingplacer: map tile has invalid extent: " +
                                     tile.levelName);
        const double maxX = double(tile.mapX) + tile.width;
        const double maxY = double(tile.mapY) + tile.height;
        if (double(world.x) < tile.mapX || double(world.x) >= maxX ||
            double(world.y) < tile.mapY || double(world.y) >= maxY) continue;
        if (result)
            throw std::runtime_error("thingplacer: world position belongs to overlapping tiles");
        result = RoutedPosition{i, worldToLocal(world, tile.mapX, tile.mapY)};
    }
    if (!result)
        throw std::runtime_error("thingplacer: world position is outside every map tile");
    return *result;
}

std::string serialize(const Placement& p, const std::string& eol) {
    if (p.thingType.empty()) {
        throw std::runtime_error("thingplacer: thingType is required");
    }
    if (p.definitionType.empty()) {
        throw std::runtime_error("thingplacer: definitionType is required");
    }

    std::string out;
    appendLine(out, "NewThing " + p.thingType, eol);
    appendLine(out, "Player " + std::to_string(p.player), eol);
    appendLine(out, "UID " + std::to_string(p.uid.value_or(0)), eol);
    appendLine(out, "DefinitionType " + quote(p.definitionType), eol);
    if (p.scriptedHook) {
        appendLine(out, "CreateTC " + quote("CTCActionUseScriptedHook"), eol);
    }
    appendLine(out, "ScriptName " + (p.scriptName.empty() ? std::string("NULL")
                                                          : p.scriptName), eol);
    appendLine(out, "ScriptData " +
                        quote(p.scriptData.empty() ? std::string("NULL")
                                                   : p.scriptData), eol);
    appendLine(out, "ThingGamePersistent " + boolText(p.thingGamePersistent), eol);
    appendLine(out, "ThingLevelPersistent " + boolText(p.thingLevelPersistent), eol);

    appendLine(out, "StartCTCPhysicsStandard", eol);
    appendVec(out, "Position", p.position, eol);
    appendVec(out, "RHSetForward", p.forward, eol);
    appendVec(out, "RHSetUp", p.up, eol);
    appendLine(out, "EndCTCPhysicsStandard", eol);

    if (p.targetable) {
        appendLine(out, "StartCTCTargeted", eol);
        appendLine(out, "Targetable TRUE", eol);
        appendLine(out, "EndCTCTargeted", eol);
    }
    if (!p.gameTextDefName.empty()) {
        appendLine(out, "StartCTCActionUseReadable", eol);
        appendLine(out, "GameTextDefName " + quote(p.gameTextDefName), eol);
        appendLine(out, "EndCTCActionUseReadable", eol);
    }
    appendLine(out, "StartCTCEditor", eol);
    appendLine(out, "EndCTCEditor", eol);
    if (p.scriptedHook) {
        appendLine(out, "StartCTCActionUseScriptedHook", eol);
        appendLine(out, "Usable TRUE", eol);
        appendLine(out, "ReversedOnMiniMap FALSE", eol);
        appendLine(out, "HiddenOnMiniMap FALSE", eol);
        appendLine(out, "VersionNumber 1", eol);
        appendLine(out, "ForceConfirmation FALSE", eol);
        appendLine(out, "TeleportToRegionEntrance FALSE", eol);
        appendLine(out, "SoundName " + quote(p.hookSoundName), eol);
        appendLine(out, "AnimationName " + quote(p.hookAnimationName), eol);
        appendLine(out, "ReplacementObject 0", eol);
        appendLine(out, "EndCTCActionUseScriptedHook", eol);
    }
    if (p.health) appendLine(out, "Health " + formatFloat(*p.health), eol);
    appendLine(out, "EndThing", eol);
    return out;
}

PlaceResult place(tng::File& file, Placement placement) {
    if (placement.uid) {
        if (!uidIsFree(file, *placement.uid)) {
            throw std::runtime_error("thingplacer: UID " +
                                     std::to_string(*placement.uid) +
                                     " is already used in " + file.source());
        }
    } else {
        placement.uid = nextUid(file);
    }
    if (!scriptNameIsFree(file, placement.scriptName)) {
        throw std::runtime_error("thingplacer: ScriptName " +
                                 placement.scriptName + " is already used in " +
                                 file.source());
    }

    PlaceResult result;
    result.uid = *placement.uid;
    result.block = serialize(placement, "\r\n");
    result.thingIndex = file.insertThingBlock(placement.section, result.block);
    return result;
}

Placement readableSign(const std::string& definitionType,
                       const std::string& gameTextDefName,
                       float localX, float localY, float yawDegrees,
                       const lev::File* level) {
    Placement p;
    p.thingType = "Object";
    p.definitionType = definitionType;
    p.gameTextDefName = gameTextDefName;
    p.position.x = localX;
    p.position.y = localY;
    p.position.z =
        level != nullptr ? terrainHeightAt(*level, localX, localY) : 0.0f;
    p.forward = forwardFromYawDegrees(yawDegrees);
    p.up = Vec3{0.0f, 0.0f, kGroundUpZ};
    p.targetable = true;
    p.health = 6000.0f;
    return p;
}

} // namespace forge::thingplacer
