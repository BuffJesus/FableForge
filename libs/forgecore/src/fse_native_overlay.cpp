#include "forge/fse_native_overlay.hpp"

#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace forge::fse {
namespace {

uint32_t hexU32(const json& value, const char* field,
                const std::string& sourceName) {
    if (!value.is_string()) {
        throw std::runtime_error("fse native overlay: " + std::string(field) +
                                 " is not a string in " + sourceName);
    }
    const std::string text = value.get<std::string>();
    size_t consumed = 0;
    unsigned long parsed = 0;
    try {
        parsed = std::stoul(text, &consumed, 0);
    } catch (const std::exception&) {
        throw std::runtime_error("fse native overlay: invalid " +
                                 std::string(field) + " in " + sourceName);
    }
    if (consumed != text.size() ||
        parsed > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("fse native overlay: invalid " +
                                 std::string(field) + " in " + sourceName);
    }
    return static_cast<uint32_t>(parsed);
}

NativeCandidate parseCandidate(const json& item,
                               const std::string& sourceName) {
    if (!item.is_object()) {
        throw std::runtime_error("fse native overlay: candidate is not an object in " +
                                 sourceName);
    }
    NativeCandidate out;
    out.address = hexU32(item.at("address"), "address", sourceName);
    out.rva = hexU32(item.at("rva"), "rva", sourceName);
    out.qualifiedName = item.value("qualifiedName", std::string());
    out.module = item.value("module", std::string());
    out.callingConvention = item.value("callingConvention", std::string());
    out.returnType = item.value("returnType", std::string());
    out.parameterCount = item.value("parameterCount", 0);
    out.parameterTypesText = item.value("parameterTypesText", std::string());
    out.prototypeComplete = item.value("prototypeComplete", false);
    out.engineImplementationVerified =
        item.value("engineImplementationVerified", false);
    out.ownerRelevance = item.value("ownerRelevance", std::string("none"));
    if (const auto evidence = item.find("evidence");
        evidence != item.end() && evidence->is_array()) {
        for (const auto& value : *evidence) {
            if (value.is_string()) out.evidence.push_back(value.get<std::string>());
        }
    }
    return out;
}

size_t countValue(const json& object, const char* key) {
    const auto value = object.value(key, uint64_t{0});
    if (value > std::numeric_limits<size_t>::max()) {
        throw std::runtime_error("fse native overlay: summary count is too large");
    }
    return static_cast<size_t>(value);
}

} // namespace

const NativeCandidate* NativeFunction::recommended() const {
    if (recommendedAddress == 0) return nullptr;
    for (const auto& candidate : candidates) {
        if (candidate.address == recommendedAddress) return &candidate;
    }
    return nullptr;
}

const NativeFunction* NativeOverlay::find(std::string_view scope,
                                          std::string_view name) const {
    for (const auto& function : functions) {
        if (function.scope == scope && function.name == name) return &function;
    }
    return nullptr;
}

NativeOverlay loadNativeOverlay(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("fse native overlay: cannot open " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadNativeOverlayText(buffer.str(), path.string());
}

NativeOverlay loadNativeOverlayText(const std::string& text,
                                    const std::string& sourceName) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        throw std::runtime_error("fse native overlay: malformed JSON in " + sourceName);
    }
    NativeOverlay overlay;
    overlay.schemaVersion = root.value("schemaVersion", std::string());
    overlay.sourceDigest = root.value("sourceDigest", std::string());
    if (overlay.schemaVersion.rfind("1.", 0) != 0) {
        throw std::runtime_error("fse native overlay: unsupported schema " +
                                 overlay.schemaVersion + " in " + sourceName);
    }

    if (const auto target = root.find("target");
        target != root.end() && target->is_object()) {
        overlay.executable = target->value("executable", std::string());
        overlay.imageBase = hexU32(target->at("imageBase"), "imageBase", sourceName);
        overlay.architecture = target->value("architecture", std::string());
    }
    if (const auto policy = root.find("policy");
        policy != root.end() && policy->is_object()) {
        overlay.recommendationIsNotHookApproval =
            policy->value("recommendationIsNotHookApproval", true);
        overlay.engineVerificationIsNotBindingVerification =
            policy->value("engineVerificationIsNotBindingVerification", true);
    }
    if (const auto summary = root.find("summary");
        summary != root.end() && summary->is_object()) {
        overlay.summary.fseFunctions = countValue(*summary, "fseFunctions");
        overlay.summary.uniqueExactNameMatches =
            countValue(*summary, "uniqueExactNameMatches");
        overlay.summary.ambiguousExactNameMatches =
            countValue(*summary, "ambiguousExactNameMatches");
        overlay.summary.unmatched = countValue(*summary, "unmatched");
        overlay.summary.ownerAlignedRecommendations =
            countValue(*summary, "ownerAlignedRecommendations");
        overlay.summary.verifiedRecommendedBindings =
            countValue(*summary, "verifiedRecommendedBindings");
        overlay.summary.verifiedEngineFunctions =
            countValue(*summary, "verifiedEngineFunctions");
        overlay.summary.hookApprovedBindings =
            countValue(*summary, "hookApprovedBindings");
    }

    if (const auto functions = root.find("functions");
        functions != root.end() && functions->is_array()) {
        for (const auto& item : *functions) {
            NativeFunction function;
            function.name = item.value("name", std::string());
            function.scope = item.value("scope", std::string());
            function.matchStatus = item.value("matchStatus", std::string());
            if (const auto confidence = item.find("recommendationConfidence");
                confidence != item.end() && confidence->is_string()) {
                function.recommendationConfidence = confidence->get<std::string>();
            }
            function.hookApproved = item.value("hookApproved", false);
            const auto recommendation = item.find("recommendedAddress");
            if (recommendation != item.end() && recommendation->is_string()) {
                function.recommendedAddress =
                    hexU32(*recommendation, "recommendedAddress", sourceName);
            }
            if (const auto candidates = item.find("candidates");
                candidates != item.end() && candidates->is_array()) {
                for (const auto& candidate : *candidates) {
                    function.candidates.push_back(
                        parseCandidate(candidate, sourceName));
                }
            }
            if (function.recommendedAddress != 0 && function.recommended() == nullptr) {
                throw std::runtime_error(
                    "fse native overlay: recommendation is not a candidate for " +
                    function.scope + "::" + function.name);
            }
            overlay.functions.push_back(std::move(function));
        }
    }
    if (const auto verified = root.find("verifiedEngineFunctions");
        verified != root.end() && verified->is_array()) {
        for (const auto& candidate : *verified) {
            overlay.verifiedEngineFunctions.push_back(
                parseCandidate(candidate, sourceName));
        }
    }
    return overlay;
}

} // namespace forge::fse
