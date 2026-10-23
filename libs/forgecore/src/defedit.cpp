#include "forge/defedit.hpp"
#include "forge/defrelocate.hpp"
#include "forge/fmplinks.hpp"
#include "forge/unicode.hpp"

#include <cctype>
#include <array>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "forge/defdecode.hpp"

namespace forge::defedit {
CreatureBodyModelResult setCreatureBodyModel(bin::File& file,const defschema::Schema& schema,
        size_t entryIndex,size_t bodyPart,size_t alternative,uint32_t modelId,const big::File& graphics) {
    const auto refuse=[](const char* text){throw std::runtime_error(std::string("defedit body model: ")+text);};
    if(entryIndex>=file.entries().size())refuse("entry index out of range");
    const auto& entry=file.entries()[entryIndex];
    if(entry.definition!="CCreatureDef")refuse("requires CCreatureDef");
    const auto decoded=defdecode::decode(entry,schema);
    if(!decoded.clean() || defdecode::encode(decoded)!=entry.data)refuse("requires clean exact decode");
    const auto layout=fmplinks::creaturePrefixLayout(decoded.prefix);
    const auto slot=std::find_if(layout.bodyModels.begin(),layout.bodyModels.end(),[&](const auto& value){
        return value.bodyPart==bodyPart && value.alternative==alternative;
    });
    if(slot==layout.bodyModels.end())refuse("body part or alternative is absent");
    uint32_t old=0;
    for(size_t n=0;n<4;++n)old|=uint32_t(entry.data.at(slot->offset+n))<<(8*n);
    const big::Bank* meshes=nullptr;
    for(const auto& bank:graphics.banks())if(bank.name=="MBANK_ALLMESHES") {
        if(meshes)refuse("ambiguous mesh bank");meshes=&bank;
    }
    if(!meshes)refuse("missing mesh bank");
    for(const auto id:{old,modelId}) {
        const big::Entry* target=nullptr;
        for(const auto& mesh:meshes->entries)if(mesh.id==id) {
            if(target)refuse("ambiguous mesh ID");target=&mesh;
        }
        if(!target)refuse("missing mesh ID");
        if(target->type!=5)refuse("requires skinned type-5 mesh");
    }
    const CreatureBodyModelResult result{entryIndex,bodyPart,alternative,slot->offset,old,modelId};
    if(old!=modelId) {
        auto data=entry.data;
        for(size_t n=0;n<4;++n)data[slot->offset+n]=uint8_t(modelId>>(8*n));
        file.setEntryData(entryIndex,std::move(data));
    }
    return result;
}
namespace {

// Split "a,b,c" into pieces (no trimming beyond surrounding spaces per piece).
std::vector<std::string> splitCommas(std::string_view s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t comma = s.find(',', start);
        std::string_view piece =
            s.substr(start, comma == std::string_view::npos ? std::string_view::npos
                                                            : comma - start);
        // trim surrounding whitespace
        size_t a = 0, b = piece.size();
        while (a < b && std::isspace((unsigned char)piece[a])) ++a;
        while (b > a && std::isspace((unsigned char)piece[b - 1])) --b;
        out.emplace_back(piece.substr(a, b - a));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
    b.push_back(uint8_t(v >> 16));
    b.push_back(uint8_t(v >> 24));
}

void putFloat(std::vector<uint8_t>& b, float v) {
    uint32_t u;
    std::memcpy(&u, &v, 4);
    putU32(b, u);
}

int32_t parseI32(const std::string& s, const std::string& what) {
    try {
        size_t pos = 0;
        long long v = std::stoll(s, &pos, 0);
        if (pos != s.size() || v < std::numeric_limits<int32_t>::min() ||
            v > std::numeric_limits<int32_t>::max()) throw std::invalid_argument("integer range or trailing input");
        return int32_t(v);
    } catch (const std::exception&) {
        throw std::runtime_error("defedit: cannot parse '" + s +
                                 "' as an integer for " + what);
    }
}

uint32_t parseU32(const std::string& s, const std::string& what) {
    try {
        size_t pos = 0;
        const auto first=s.find_first_not_of(" \t\r\n\f\v");
        if(first!=std::string::npos && s[first]=='-') throw std::invalid_argument("negative unsigned value");
        unsigned long long v = std::stoull(s, &pos, 0);
        if (pos != s.size() || v > std::numeric_limits<uint32_t>::max())
            throw std::invalid_argument("unsigned range or trailing input");
        return uint32_t(v);
    } catch (const std::exception&) {
        throw std::runtime_error("defedit: cannot parse '" + s +
                                 "' as an unsigned integer for " + what);
    }
}

float parseFloat(const std::string& s, const std::string& what) {
    try {
        char* end=nullptr;
        errno=0;
        const float v=std::strtof(s.c_str(),&end);
        // C libraries may report ERANGE for a representable subnormal. Keep
        // that finite nonzero result, but refuse overflow and underflow to zero.
        if(end==s.c_str() || end!=s.c_str()+s.size() || !std::isfinite(v) ||
           (errno!=0 && (errno!=ERANGE || v==0)))
            throw std::invalid_argument("expected a complete finite float");
        return v;
    } catch (const std::exception&) {
        throw std::runtime_error("defedit: cannot parse '" + s +
                                 "' as a float for " + what);
    }
}

uint8_t parseColorByte(const std::string& s, const std::string& what) {
    long v = parseI32(s, what);
    if (v < 0 || v > 255)
        throw std::runtime_error("defedit: colour component '" + s +
                                 "' out of range 0-255 for " + what);
    return uint8_t(v);
}

bool parseBool(const std::string& s) {
    if (s == "1" || s == "true" || s == "TRUE" || s == "True") return true;
    if (s == "0" || s == "false" || s == "FALSE" || s == "False") return false;
    throw std::runtime_error("defedit: cannot parse '" + s +
                             "' as a bool (use 1/0/true/false)");
}

// Encode `valueString` into the on-disk byte form for the declared `type`.
// `oldValue` is the field's current bytes — used only to preserve exact
// layout: fixed widths and complete native NUL-terminated strings are required.
std::vector<uint8_t> encodeValue(const std::string& type,
                                 std::string_view valueString,
                                 const std::vector<uint8_t>& oldValue,
                                 const std::string& field) {
    const size_t fixedSize=(type=="int32" || type=="uint32" || type=="float" ||
                            type=="CDefString" || type=="CRGBColour" || type=="ColourRGBA")?4:
                           type=="bool"?1:type=="C2DVector"?8:type=="C3DVector"?12:0;
    // A tag-delimited range can include an unrecognized derived-class trailer.
    // Replacing that entire range with a scalar would silently discard it.
    if(fixedSize && oldValue.size()!=fixedSize)
        throw std::runtime_error("defedit: field "+field+" has "+std::to_string(oldValue.size())+
                                 " bytes, expected "+std::to_string(fixedSize)+
                                 " for "+type+"; refusing to discard unsupported payload bytes");
    const std::string vs(valueString);
    std::vector<uint8_t> out;

    if (type == "int32") {
        putU32(out, uint32_t(parseI32(vs, field)));
        return out;
    }
    if (type == "uint32") {
        putU32(out, parseU32(vs, field));
        return out;
    }
    if (type == "CDefString") {
        // Signed names-table offsets include -1 for no reference. Preserve
        // compatibility with explicit unsigned/hex representations of the bits.
        const auto first=vs.find_first_not_of(" \t\r\n\f\v");
        putU32(out,first!=std::string::npos && vs[first]=='-'?
            uint32_t(parseI32(vs,field)):parseU32(vs,field));
        return out;
    }
    if (type == "float") {
        putFloat(out, parseFloat(vs, field));
        return out;
    }
    if (type == "bool") {
        out.push_back(parseBool(vs) ? 1 : 0);
        return out;
    }
    if (type == "C2DVector") {
        auto parts = splitCommas(vs);
        if (parts.size() != 2)
            throw std::runtime_error("defedit: C2DVector " + field +
                                     " expects \"x,y\", got '" + vs + "'");
        putFloat(out, parseFloat(parts[0], field + ".x"));
        putFloat(out, parseFloat(parts[1], field + ".y"));
        return out;
    }
    if (type == "C3DVector") {
        auto parts = splitCommas(vs);
        if (parts.size() != 3)
            throw std::runtime_error("defedit: C3DVector " + field +
                                     " expects \"x,y,z\", got '" + vs + "'");
        putFloat(out, parseFloat(parts[0], field + ".x"));
        putFloat(out, parseFloat(parts[1], field + ".y"));
        putFloat(out, parseFloat(parts[2], field + ".z"));
        return out;
    }
    if (type == "CRGBColour" || type == "ColourRGBA") {
        auto parts = splitCommas(vs);
        if (parts.size() != 3 && parts.size() != 4)
            throw std::runtime_error("defedit: " + type + " " + field +
                                     " expects \"r,g,b\" or \"r,g,b,a\" (0-255), "
                                     "got '" + vs + "'");
        // Native CRGBColour binary I/O writes B, G, R, A (FableWin
        // WriteColour 03083120; ReadColour 03082170). Public input remains
        // r,g,b,a. The explicitly named ColourRGBA layout stays R,G,B,A.
        const auto red=parseColorByte(parts[0],field+".r");
        const auto green=parseColorByte(parts[1],field+".g");
        const auto blue=parseColorByte(parts[2],field+".b");
        out.push_back(type=="CRGBColour"?blue:red);
        out.push_back(green);
        out.push_back(type=="CRGBColour"?red:blue);
        out.push_back(parts.size()==4?parseColorByte(parts[3],field+".a"):uint8_t(255));
        return out;
    }
    if (type == "CCharString") {
        if(oldValue.empty() || oldValue.back()!=0 ||
           std::find(oldValue.begin(),oldValue.end()-1,uint8_t(0))!=oldValue.end()-1)
            throw std::runtime_error("defedit: CCharString "+field+
                " is not one complete NUL-terminated string; refusing to discard unsupported payload bytes");
        if(vs.find('\0')!=std::string::npos)
            throw std::runtime_error("defedit: CCharString "+field+" cannot contain an embedded NUL");
        // Null-terminated game bytes.
        for (char c : vs) out.push_back(uint8_t(c));
        out.push_back(0);
        return out;
    }
    if (type == "CWideString") {
        // Native TransferBinaryOut 01895C50 calls WriteNullTerminatedWString
        // 030830D0: UTF-16LE units followed by one zero unit, without a count.
        bool terminated=oldValue.size()>=2 && oldValue.size()%2==0 &&
            oldValue[oldValue.size()-2]==0 && oldValue.back()==0;
        if(terminated) for(size_t i=0;i+2<oldValue.size();i+=2)
            if(oldValue[i]==0 && oldValue[i+1]==0) {terminated=false;break;}
        if(!terminated) throw std::runtime_error("defedit: CWideString "+field+
            " is not one complete NUL-terminated UTF-16 string; refusing to discard unsupported payload bytes");
        size_t cursor=0;
        unicode::readUtf16z(oldValue,cursor); // Refuse malformed surrogate originals.
        unicode::appendUtf16z(out,vs);
        return out;
    }

    throw std::runtime_error("defedit: field " + field + " has unsupported type '" +
                             type + "' (supported: int32, uint32, float, bool, "
                             "C2DVector, C3DVector, CRGBColour, CWideString, "
                             "CDefString, CCharString)");
}

SetFieldResult setFieldImpl(bin::File& file, const defschema::Schema& schema,
                            size_t index, std::string_view fieldName,
                            std::string_view valueString) {
    if (index >= file.entries().size())
        throw std::runtime_error("defedit: entry index " + std::to_string(index) +
                                 " out of range");
    const bin::Entry& entry = file.entries()[index];

    const auto* def =
        defdecode::resolveType(schema, entry.definition, entry.data);
    if (def == nullptr)
        throw std::runtime_error(
            "defedit: no schema decodes def type '" + entry.definition +
            "' for entry " + std::to_string(index) +
            " — cannot locate fields by tag");

    auto decoded = defdecode::decode(entry.data, *def);
    if (!decoded.clean())
        throw std::runtime_error(
            "defedit: entry " + std::to_string(index) + " (" + entry.definition +
            (entry.name.empty() ? "" : " / " + entry.name) +
            ") did not decode cleanly — refusing to edit an unrecognized payload");

    defdecode::DecodedField* target = nullptr;
    for (auto& f : decoded.fields) {
        if (f.name == fieldName) { target = &f; break; }
    }
    if (target == nullptr)
        throw std::runtime_error(
            "defedit: field '" + std::string(fieldName) + "' not found in " +
            entry.definition + " (entry " + std::to_string(index) + ")");

    SetFieldResult r;
    r.entryIndex = index;
    r.definition = entry.definition;
    r.entryName = entry.name;
    r.field = std::string(fieldName);
    r.type = target->type;
    r.oldValue = defdecode::formatValue(*target);
    r.oldPayloadSize = entry.data.size();

    target->value =
        encodeValue(target->type, valueString, target->value, r.field);
    r.newValue = defdecode::formatValue(*target);

    std::vector<uint8_t> rebuilt = defdecode::encode(decoded);
    r.newPayloadSize = rebuilt.size();
    file.setEntryData(index, std::move(rebuilt));
    return r;
}

// Decode the named entry and hand back the decoded field (throws like setFieldImpl).
defdecode::DecodedField* locateField(const bin::File& file, const defschema::Schema& schema,
                                     std::string_view entryName, std::string_view fieldName,
                                     defdecode::Decoded& decoded, size_t& index) {
    if (entryName.empty())
        throw std::runtime_error("defedit: empty entry name never matches");
    const bin::Entry* entry = file.find(entryName);
    if (entry == nullptr)
        throw std::runtime_error("defedit: no entry named '" + std::string(entryName) + "'");
    index = size_t(entry - file.entries().data());
    const auto* def = defdecode::resolveType(schema, entry->definition, entry->data);
    if (def == nullptr)
        throw std::runtime_error("defedit: no schema decodes def type '" + entry->definition + "'");
    decoded = defdecode::decode(entry->data, *def);
    if (!decoded.clean())
        throw std::runtime_error("defedit: entry " + std::string(entryName) + " did not decode cleanly");
    for (auto& f : decoded.fields)
        if (f.name == fieldName) return &f;
    throw std::runtime_error("defedit: field '" + std::string(fieldName) + "' not found in " + entry->definition);
}

} // namespace

std::vector<uint8_t> getFieldBytes(const bin::File& file, const defschema::Schema& schema,
                                   std::string_view entryName, std::string_view fieldName) {
    defdecode::Decoded decoded;
    size_t index = 0;
    return locateField(file, schema, entryName, fieldName, decoded, index)->value;
}

SetFieldResult setFieldBytes(bin::File& file, const defschema::Schema& schema,
                             std::string_view entryName, std::string_view fieldName,
                             std::vector<uint8_t> valueBytes) {
    defdecode::Decoded decoded;
    size_t index = 0;
    defdecode::DecodedField* target = locateField(file, schema, entryName, fieldName, decoded, index);
    const bin::Entry& entry = file.entries()[index];
    SetFieldResult r;
    r.entryIndex = index;
    r.definition = entry.definition;
    r.entryName = entry.name;
    r.field = std::string(fieldName);
    r.type = target->type;
    r.oldValue = defdecode::formatValue(*target);
    r.oldPayloadSize = entry.data.size();
    target->value = std::move(valueBytes);
    r.newValue = defdecode::formatValue(*target);
    std::vector<uint8_t> rebuilt = defdecode::encode(decoded);
    r.newPayloadSize = rebuilt.size();
    file.setEntryData(index, std::move(rebuilt));
    return r;
}

SetFieldResult setField(bin::File& file, const defschema::Schema& schema,
                        std::string_view entryName, std::string_view fieldName,
                        std::string_view valueString) {
    if (entryName.empty())
        throw std::runtime_error(
            "defedit: empty entry name never matches; use the index overload for "
            "unnamed sub-defs");
    const bin::Entry* entry = file.find(entryName);
    if (entry == nullptr)
        throw std::runtime_error("defedit: no entry named '" +
                                 std::string(entryName) + "'");
    const size_t index = size_t(entry - file.entries().data());
    return setFieldImpl(file, schema, index, fieldName, valueString);
}

SetFieldResult setField(bin::File& file, const defschema::Schema& schema,
                        size_t entryIndex, std::string_view fieldName,
                        std::string_view valueString) {
    return setFieldImpl(file, schema, entryIndex, fieldName, valueString);
}

size_t cloneEntry(bin::File& file,size_t donorIndex,std::string newName) {
    return file.cloneEntry(donorIndex,std::move(newName));
}
size_t cloneCreature(bin::File& file,const defschema::Schema& schema,size_t donorIndex,std::string newName) {
    if(donorIndex>=file.entries().size() || file.entries()[donorIndex].definition!="CREATURE")
        throw std::runtime_error("creature clone: choose a Creature donor by its native index");
    if(file.entries().size()>=UINT32_MAX)
        throw std::runtime_error("creature clone: append index exceeds the game reference domain");
    const auto& donor=file.entries()[donorIndex];
    const auto decoded=defdecode::decode(donor,schema);
    if(!decoded.clean() || defdecode::encode(decoded)!=donor.data || donor.data.size()<5 ||
       decoded.fields.empty() || decoded.fields.front().name!="Components" ||
       std::any_of(decoded.fields.begin(),decoded.fields.end(),[](const auto& field){return !field.tagOk;}))
        throw std::runtime_error("creature clone: donor has no verified component layout");
    const size_t count=size_t(donor.data[3]) | size_t(donor.data[4])<<8;
    if(!count || decoded.prefixLen!=5+count*12)
        throw std::runtime_error("creature clone: donor component listing is empty or malformed");
    const auto landing=uint32_t(file.entries().size());
    // Compute and reject every unsupported candidate before appending. The exact
    // clone validates the new name, payload capacity and whole-bank ordinals.
    auto payload=defrelocate::relocate(donor,schema,{{{uint32_t(donorIndex),landing}}, {}});
    const auto appended=file.cloneEntry(donorIndex,std::move(newName));
    file.setEntryData(appended,std::move(payload));
    return appended;
}
size_t cloneEntry(bin::File& file,std::string_view donorName,std::string newName) {
    if(donorName.empty()) throw std::runtime_error("defedit clone: use an index for an unnamed donor");
    size_t selected=file.entries().size();
    for(size_t i=0;i<file.entries().size();++i) if(file.entries()[i].name==donorName) {
        if(selected!=file.entries().size()) throw std::runtime_error("defedit clone: donor name is ambiguous");
        selected=i;
    }
    if(selected==file.entries().size()) throw std::runtime_error("defedit clone: donor name was not found");
    return file.cloneEntry(selected,std::move(newName));
}

CreatureVisualGraph resolveCreatureVisuals(const bin::File& file,
                      const defschema::Schema& schema,size_t donorIndex) {
    const auto& entries=file.entries();
    auto read=[](const auto& data,size_t offset) {
        if(offset>data.size() || data.size()-offset<4)
            throw std::runtime_error("creature visual graph: truncated component word");
        return uint32_t(data[offset]) | uint32_t(data[offset+1])<<8 |
               uint32_t(data[offset+2])<<16 | uint32_t(data[offset+3])<<24;
    };
    auto rootLayout=[&](size_t index) {
        if(index>=entries.size() || entries[index].definition!="CREATURE")
            throw std::runtime_error("creature visual graph: donor/original parent must be a Creature index");
        const auto& entry=entries[index];const auto decoded=defdecode::decode(entry,schema);
        if(!decoded.clean() || defdecode::encode(decoded)!=entry.data || entry.data.size()<5 ||
           decoded.fields.empty() || decoded.fields.front().name!="Components" ||
           std::any_of(decoded.fields.begin(),decoded.fields.end(),[](const auto& field){return !field.tagOk;}))
            throw std::runtime_error("creature visual graph: unverified Creature component layout");
        const size_t count=size_t(entry.data[3]) | size_t(entry.data[4])<<8;
        if(!count || decoded.prefixLen!=5+12*count)
            throw std::runtime_error("creature visual graph: empty or malformed Creature listing");
        return count;
    };
    const auto count=rootLayout(donorIndex);
    const auto& root=entries[donorIndex];
    std::array<size_t,2> rows{};
    std::array<size_t,3> donors{donorIndex,0,0};
    constexpr std::array<const char*,2> kinds{"CCreatureDef","CAppearanceDef"};
    for(size_t k=0;k<kinds.size();++k) {
        size_t matches=0;
        const auto key=defdecode::fieldTag(kinds[k]);
        for(size_t row=0;row<count;++row) if(read(root.data,5+12*row)==key) {
            ++matches;rows[k]=row;donors[k+1]=read(root.data,9+12*row);
        }
        if(matches!=1 || donors[k+1]>=entries.size() || entries[donors[k+1]].definition!=kinds[k])
            throw std::runtime_error("creature visual graph: requires one correctly typed "+std::string(kinds[k])+" row");
        const auto owner=read(root.data,13+12*rows[k]);
        const auto ownerCount=rootLayout(owner);size_t ownerMatches=0;
        for(size_t row=0;row<ownerCount;++row) if(read(entries[owner].data,5+12*row)==key) {
            ++ownerMatches;
            if(read(entries[owner].data,9+12*row)!=donors[k+1])
                throw std::runtime_error("creature visual graph: original parent has a different visual component");
        }
        if(ownerMatches!=1)
            throw std::runtime_error("creature visual graph: original parent visual row is missing or ambiguous");
    }
    return {donorIndex,donors[1],donors[2],count,rows[0],rows[1]};
}

CreatureVisualCloneResult cloneCreatureVisuals(bin::File& file,const defschema::Schema& schema,
                      size_t donorIndex,std::string rootName,std::string bodyName,std::string appearanceName) {
    const auto& entries=file.entries();
    // CSubDefInfo persists signed-long definition/original-parent indices.
    if(entries.size()>size_t(INT32_MAX)-2u)
        throw std::runtime_error("private creature clone: append indices exceed the signed reference domain");
    const auto graph=resolveCreatureVisuals(file,schema,donorIndex);
    const auto count=graph.componentCount;
    const auto& root=entries[donorIndex];
    const std::array<size_t,2> rows{graph.bodyRow,graph.appearanceRow};
    const std::array<size_t,3> donors{graph.rootIndex,graph.bodyIndex,graph.appearanceIndex};
    std::array<std::string,3> names{std::move(rootName),std::move(bodyName),std::move(appearanceName)};
    std::array<uint32_t,3> keys{};
    for(size_t i=0;i<names.size();++i) {
        keys[i]=bin::nameCrc(names[i]);
        if(std::any_of(entries.begin(),entries.end(),[&](const auto& entry){return bin::nameCrc(entry.name)==keys[i];}) ||
           std::find(keys.begin(),keys.begin()+i,keys[i])!=keys.begin()+i)
            throw std::runtime_error("private creature clone: new names would introduce a compiled CRC ambiguity");
    }
    const auto first=uint32_t(entries.size());
    const defrelocate::Maps maps{{{uint32_t(donors[0]),first},
                                 {uint32_t(donors[1]),first+1},
                                 {uint32_t(donors[2]),first+2}}, {}};
    std::array<std::vector<uint8_t>,3> payloads;
    for(size_t i=0;i<donors.size();++i) {
        const auto& donor=entries[donors[i]];const auto decoded=defdecode::decode(donor,schema);
        if(!decoded.clean() || decoded.fields.empty() || defdecode::encode(decoded)!=donor.data ||
           std::any_of(decoded.fields.begin(),decoded.fields.end(),[](const auto& field){return !field.tagOk;}))
            throw std::runtime_error("private creature clone: component payload does not round-trip cleanly");
        payloads[i]=defrelocate::relocate(donor,schema,maps,true);
    }
    // CParentDefClassBase ownership is local to each selected row. Generic
    // reference relocation must not mark borrowed gameplay components as ours.
    // Keep every unselected triple exact, including data/parent IDs that happen
    // to equal a visual donor or the root; no value-wide old-parent rewrite.
    for(size_t row=0;row<count;++row) {
        const auto offset=5+12*row;
        if(row!=rows[0] && row!=rows[1])
            std::copy_n(root.data.begin()+offset,12,payloads[0].begin()+offset);
    }
    auto put=[&](size_t offset,uint32_t value) {
        for(size_t byte=0;byte<4;++byte) payloads[0][offset+byte]=uint8_t(value>>(8*byte));
    };
    for(size_t i=0;i<rows.size();++i) {put(9+12*rows[i],first+1+uint32_t(i));put(13+12*rows[i],first);}

    // Name, capacity and whole-bank ordinal checks run on this disposable copy.
    // A later bad component name/append cannot leave a half-created root behind.
    bin::File proposed=file;
    for(size_t i=0;i<donors.size();++i) {
        const auto appended=proposed.cloneEntry(donors[i],std::move(names[i]));
        if(appended!=size_t(first)+i) throw std::runtime_error("private creature clone: append slot mismatch");
        proposed.setEntryData(appended,std::move(payloads[i]));
    }
    static_assert(std::is_nothrow_move_assignable_v<bin::File>);
    file=std::move(proposed);
    return {first,size_t(first)+1,size_t(first)+2};
}

} // namespace forge::defedit
