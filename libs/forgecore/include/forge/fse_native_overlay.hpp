#pragma once
// Reader for the generated FableTLC RE-to-FSE discovery overlay.
//
// A recommended candidate is not a hook-approved binding. Retail/lift evidence
// verifies the engine implementation at an address, not its relationship to an
// FSE Lua function. Consumers must keep those two facts separate.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace forge::fse {

struct NativeCandidate {
    uint32_t address = 0;
    uint32_t rva = 0;
    std::string qualifiedName;
    std::string module;
    std::string callingConvention;
    std::string returnType;
    int parameterCount = 0;
    std::string parameterTypesText;
    bool prototypeComplete = false;
    bool engineImplementationVerified = false;
    std::string ownerRelevance; // "strong" | "moderate" | "none"
    std::vector<std::string> evidence;
};

struct NativeFunction {
    std::string name;
    std::string scope; // "Entity" | "Quest"
    std::string matchStatus;
    uint32_t recommendedAddress = 0;
    std::string recommendationConfidence;
    bool hookApproved = false;
    std::vector<NativeCandidate> candidates;

    const NativeCandidate* recommended() const;
};

struct NativeOverlaySummary {
    size_t fseFunctions = 0;
    size_t uniqueExactNameMatches = 0;
    size_t ambiguousExactNameMatches = 0;
    size_t unmatched = 0;
    size_t ownerAlignedRecommendations = 0;
    size_t verifiedRecommendedBindings = 0;
    size_t verifiedEngineFunctions = 0;
    size_t hookApprovedBindings = 0;
};

struct NativeOverlay {
    std::string schemaVersion;
    std::string sourceDigest;
    std::string executable;
    uint32_t imageBase = 0;
    std::string architecture;
    bool recommendationIsNotHookApproval = true;
    bool engineVerificationIsNotBindingVerification = true;
    NativeOverlaySummary summary;
    std::vector<NativeFunction> functions;
    std::vector<NativeCandidate> verifiedEngineFunctions;

    const NativeFunction* find(std::string_view scope,
                               std::string_view name) const;
};

// Throws std::runtime_error on unreadable or malformed input.
NativeOverlay loadNativeOverlay(const std::filesystem::path& path);
NativeOverlay loadNativeOverlayText(const std::string& json,
                                    const std::string& sourceName = {});

} // namespace forge::fse
