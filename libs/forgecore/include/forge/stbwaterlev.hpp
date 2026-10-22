#pragma once
#include "forge/lev.hpp"
#include "forge/stbwater.hpp"
#include "forge/terraintex.hpp"

namespace forge::stbwater {

using LevelWaterPalette=std::array<ThemeWaterSlot,256>;
// Use the existing Forge name fallback for stale LEV definition indices. A bake
// must resolve every referenced named theme; unlike the preview it cannot guess
// dry water for an unknown definition. Unnamed slots retain native global0.
inline LevelWaterPalette levelWaterPalette(const lev::File& level,const terraintex::ThemeLibrary& library) {
    LevelWaterPalette palette{};
    const terraintex::ThemeEntry* fallback=nullptr;
    for(const auto& theme:library.themes())if(theme.classIndex==0 && theme.decoded){fallback=&theme;break;}
    if(!fallback)throw std::invalid_argument("water bake requires the native class0 theme");
    palette.fill({0,0,fallback->waterType,fallback->waterHeight});
    for(const auto& row:terraintex::resolvePalette(level,library)) {
        const terraintex::ThemeEntry* theme=nullptr;
        if(!row.paletteName.empty()) {
            const auto* named=library.byName(row.paletteName);
            if(named && named->decoded && (!row.resolved || !row.nameMatches))theme=named;
        }
        if(!theme && row.resolved)theme=library.byDefIndex(row.defIndex);
        if(!theme) {
            if(row.cellCount)throw std::invalid_argument("water bake has unresolved theme: "+row.paletteName);
            continue;
        }
        if(theme->defIndex==0 || theme->defIndex>uint32_t(INT32_MAX) || !std::isfinite(theme->waterHeight))
            throw std::invalid_argument("water bake has invalid resolved theme");
        if(row.slot<0 || row.slot>=256)throw std::invalid_argument("water palette slot outside byte format");
        palette[size_t(row.slot)]={int32_t(theme->defIndex),theme->classIndex,theme->waterType,theme->waterHeight};
    }
    return palette;
}

struct LoadedWaterMap {
    const lev::File* level=nullptr; // caller keeps immutable LEVs alive
    uint16_t worldX=0,worldY=0;
    LevelWaterPalette palette{};
};

// Fresh bakes can keep all source maps loaded instead of depending on the
// development bank's sparse edge cache. Indices remain native world-map slots;
// a null level is an absent slot. This class reads source data and never saves it.
class LoadedWaterWorld {
public:
    explicit LoadedWaterWorld(std::vector<LoadedWaterMap> maps,
        std::optional<WaterWorldGrid> existingGrid=std::nullopt,
        Precision precision=Precision::Float24,HeightRounding rounding=HeightRounding::NearestEven)
        :maps_(std::move(maps)),precision_(precision),rounding_(rounding) {
        if(precision!=Precision::Float24 && precision!=Precision::Float53)
            throw std::invalid_argument("invalid loaded water world precision");
        (void)quantizeMapHeight(0,rounding); // validate explicit rounding before storing callbacks
        std::vector<std::optional<WaterMapDomain>> placements;
        for(const auto& map:maps_) {
            if(!map.level){domains_.push_back({});placements.push_back(std::nullopt);continue;}
            // Native PeekWidth/Height exclude the LEV's extra overlap vertex.
            const int width=map.level->width(),height=map.level->height();
            if(width<1 || height<1 || width>65535 || height>65535)
                throw std::invalid_argument("loaded water map dimensions exceed native format");
            WaterMapDomain domain{uint16_t(width),uint16_t(height),map.worldX,map.worldY,[](int,int){return true;}};
            domains_.push_back(domain);placements.push_back(domain);
        }
        if(existingGrid)grid_=std::move(*existingGrid);
        else if(auto built=buildWaterWorldGrid(placements))grid_=std::move(*built);
        // Validate even an explicitly supplied empty placement grid.
        (void)grid_.mapAt(grid_.originX,grid_.originY);
    }

    const WaterMapDomain& domain(size_t map) const {
        if(map>=maps_.size() || !maps_[map].level)throw std::invalid_argument("missing loaded water map");
        return domains_[map];
    }
    WaterMapSample resolve(size_t map,int x,int y) const {
        return resolveWaterMapSample(domains_,map,x,y,[this](int i,int j){return grid_.mapAt(i,j);});
    }
    RawMapWaterQueries rawQueries(size_t map) const {
        if(map>=maps_.size() || !maps_[map].level)throw std::invalid_argument("missing loaded water map");
        RawMapWaterQueries query;
        query.fast=[this,map](int x,int y){return themeHasWaterFast(cell(map,x,y));};
        query.slow=[this,map](int x,int y){return themeHasWater(cell(map,x,y),precision_);};
        query.type=[this,map](int x,int y){return themeWaterType(cell(map,x,y));};
        query.depth=[this,map](int x,int y){return themeWaterDepth(cell(map,x,y),precision_);};
        query.height=[this,map](int x,int y){
            const auto p=resolve(map,x,y);
            return quantizeMapHeight(maps_[p.map].level->heightAt(p.x,p.y),rounding_);
        };
        return query; // world and source LEVs must outlive callbacks
    }
    std::vector<std::array<int32_t,2>> collectShorePoints(size_t map,
        std::vector<std::array<int32_t,2>>& accumulated) const {
        const auto raw=rawQueries(map);
        ShoreMapQueries query;
        query.hasWater=[&raw](int x,int y,int radius){return interpolatedHasWater(x,y,radius,raw);};
        query.depth=[this,&raw](int x,int y,int radius){return interpolatedWaterLevel(x,y,radius,raw,false,precision_);};
        query.hasType7=[&raw](int x,int y){return raw.type(x,y)==7;};
        const auto& domain=domains_[map];
        return collectMapShorePoints(domain.width,domain.height,domain.originX,domain.originY,query,accumulated);
    }
    Queries backgroundQueries(size_t map,
        std::function<std::array<float,12>(int16_t,int16_t,float)> shore) const {
        const auto raw=rawQueries(map);
        Queries query;
        query.hasWater=[raw](int x,int y,int radius){return interpolatedHasWater(x,y,radius,raw,true);};
        query.waterType=[raw](int x,int y,int radius){return interpolatedWaterType(x,y,radius,raw);};
        query.waterHeight=[this,raw](int x,int y,int radius){return interpolatedWaterLevel(x,y,radius,raw,true,precision_);};
        query.shore=std::move(shore);
        return query;
    }

private:
    ThemeWaterCell cell(size_t map,int x,int y) const {
        const auto p=resolve(map,x,y);
        const auto& source=maps_[p.map];
        ThemeWaterCell cell;
        cell.blends={source.level->themeStrengthAt(p.x,p.y,0),source.level->themeStrengthAt(p.x,p.y,1)};
        for(int slot=0;slot<3;++slot)cell.slots[size_t(slot)]=source.palette[source.level->themeIndexAt(p.x,p.y,slot)];
        return cell;
    }
    std::vector<LoadedWaterMap> maps_;
    std::vector<WaterMapDomain> domains_;
    WaterWorldGrid grid_;
    Precision precision_;
    HeightRounding rounding_;
};

} // namespace forge::stbwater
