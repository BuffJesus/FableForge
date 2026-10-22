#pragma once
#include "forge/bwd.hpp"
#include "forge/worldmerge.hpp"
#include <map>
#include <string>
#include <vector>

namespace forge::worldmerge {
struct WldSource {
    bwd::File world;
    std::map<int,std::string> sourceLevels; // declared/local IDs -> BWD level paths
    std::string text; // validated source text; quest operations need rendering
    bool sparse=false;
    // Carry these with world into Layer; cross-source references resolve only
    // during merge, before rendering or publishing the final BWD/WLD.
    std::map<size_t,RegionMapNames> regionReferences;
};
// Discover validated map identities before compiling regions, so dependencies
// can refer to contributors on either side of the load order.
std::vector<std::string> declaredWldMapNames(const std::string& text);
// A sparse source overlays complete named map/region records onto a copy of
// the baseline. Untouched BWD records retain all flags/opaque bytes. New maps
// require dimensions; existing maps can use their baseline extents. A paired
// compiled source supplies authoritative records and its own map namespace.
// availableMaps supplies names for dependencies, never map fields or GTG IDs.
WldSource prepareWldSource(const bwd::File& baseline,const std::string& text,
                           const bwd::DimSource& dimensions,
                           const bwd::File* compiledSource=nullptr,
                           const std::vector<std::string>& availableMaps={});
// Preserve selected source comments/unknown record fields, then synchronize
// supported fields and IDs with the final compiled world. Quest additions and
// explicit [DELETE] entries compose in source order; omission never deletes.
std::string renderWldSources(const std::string& baseline,
                             const std::vector<std::string>& sources,
                             const bwd::File& finalWorld);
} // namespace forge::worldmerge
