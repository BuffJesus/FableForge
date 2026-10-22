#include "forge/levstorage.hpp"
#include "forge/navpatch.hpp"
#include <algorithm>
#include <stdexcept>

namespace forge::levstorage {
namespace {
void require(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(std::string("levstorage: ")+message);
}
struct Reader {
    const std::vector<uint8_t>& b;
    size_t p=0;
    size_t take(size_t n) {
        require(p<=b.size() && n<=b.size()-p,"truncated storage");const size_t at=p;p+=n;return at;
    }
    uint8_t byte(){return b[take(1)];}
    uint32_t word(){const auto at=take(4);return uint32_t(b[at])|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24;}
    std::string text(size_t size) {
        const size_t at=take(size);
        require(std::find(b.begin()+at,b.begin()+p,0)==b.begin()+p,"embedded NUL in sound name");
        return std::string(reinterpret_cast<const char*>(b.data()+at),size);
    }
    std::vector<std::string> palette() {
        std::vector<std::string> names;
        for(size_t i=0;i<256;++i) {
            const size_t at=take(128);
            const auto begin=b.begin()+at,end=b.begin()+p,zero=std::find(begin,end,0);
            require(zero!=end,"unterminated palette name");
            require(std::all_of(zero,end,[](uint8_t v){return v==0;}),"unknown palette name padding");
            const auto value=word();
            // LoadFromFile 02232CC5/022338D3 gates on the old numeric word,
            // then resolves the name at 02232D07/0223390E. It never uses a
            // nonzero saved number as the final runtime definition identity.
            if(value) {
                require(zero!=begin,"nonzero palette slot has no name");
                names.emplace_back(begin,zero);
            }
        }
        return names;
    }
};
}
Review validate(const std::vector<uint8_t>& bytes) {
    Reader r{bytes};Review result;
    // PDB CFileFormatLevel is a five-byte serialisation header followed by
    // five file offsets. ThingDataOffset and NavigationDataOffset are legacy
    // fields, often stale; they are not definition IDs or runtime LEV spans.
    require(r.word()==25 && r.byte()==4,"unsupported level header");
    require(r.word()==25,"noncontiguous map header");
    r.word();const size_t brush=r.word();r.word();const size_t navigation=r.word();
    const auto mapSize=r.word();const auto mapVersion=r.byte();
    require((mapVersion==8 && mapSize==67614) || (mapVersion==9 && mapSize==67618),"unsupported map header");
    r.take(8);result.width=r.word();result.height=r.word();
    require(result.width && result.height && result.width<=4096 && result.height<=4096 &&
            result.width%32==0 && result.height%32==0,"unsupported level dimensions");
    require(r.byte()==1,"map without extra boundary cells is unsupported");
    result.groundThemes=r.palette();
    require(r.word()==3,"unsupported cell-map version");
    const auto sounds=r.word();require(sounds<=128,"sound palette exceeds signed-byte range");
    result.atmosThemes=r.palette();
    if(mapVersion==9)r.word(); // StaticMapChecksum, not a definition reference.
    require(r.p==25+mapSize,"map header length mismatch");
    for(uint32_t i=1;i<sounds;++i) {
        const auto length=r.word();require(length && length<=4096,"unsupported sound name length");
        result.soundThemes.push_back(r.text(length));
    }
    result.heightCells=size_t(result.width+1)*(result.height+1);
    for(size_t i=0;i<result.heightCells;++i) {
        require(r.word()==21 && r.byte()==7,"unsupported height-cell record");
        // CFileFormatHeightMapCell: float height, blend byte, three local
        // palette indices, two blends, passability/sound/minimap/shore/nav bytes.
        r.take(16);
    }
    result.gameCells=size_t(result.width/4)*(result.height/4);
    for(size_t i=0;i<result.gameCells;++i) {
        require(r.word()==11 && r.byte()==1,"unsupported game-cell record");
        r.take(5);const auto sound=r.byte();
        // Native 02233BBC sign-extends the byte, and 02233BC9..BD2 maps
        // every negative value to the implicit null sound. Preserve its bits.
        require(sound>=128 || sound==0 || sound<sounds,"game-cell sound index out of bounds");
    }
    require(r.p==brush,"brush pointer does not follow cells");
    require(r.word()==33801 && r.byte()==1,"unsupported brush header");
    require(r.word()==0,"nonempty script brushes require separate review");
    (void)r.palette(); // Empty brush storage has no runtime brush references.
    require(r.p==navigation,"navigation pointer does not follow brushes");
    const auto nav=navmesh::validateNavigationStorage(bytes,navigation,int(result.width),int(result.height));
    result.navigationSections=nav.sections.size();
    for(const auto& section:nav.sections)result.navigationNodes+=section.nodes.size();
    return result;
}
}
