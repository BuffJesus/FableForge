#include "forge/fmplinks.hpp"
#include "forge/defdecode.hpp"
#include <charconv>
#include <cstring>
#include <set>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace forge::fmplinks {
namespace {
using Json = nlohmann::json;
[[noreturn]] void fail(const std::string& reason) {
    throw std::runtime_error("FMP links: " + reason);
}

// MS-NRBF records are parsed as an object graph. Class names are data only.
// The accepted subset covers legacy NameValueCollection/ArrayList metadata.
class Reader {
    struct Member { std::string name; uint8_t type, primitive = 0; };
    struct Class { std::string name; std::vector<Member> members; };
    std::span<const uint8_t> bytes_;
    size_t pos_ = 0, records_ = 0;
    unsigned depth_ = 0;
    std::map<int32_t, Json> objects_;
    std::map<int32_t, Class> classes_;
    std::set<int32_t> libraries_;
    static constexpr size_t limit = 32768;

    std::span<const uint8_t> take(size_t n) {
        if (n > bytes_.size() - pos_) fail("truncated metadata");
        auto result = bytes_.subspan(pos_, n); pos_ += n; return result;
    }
    uint8_t byte() { return take(1)[0]; }
    int32_t integer() { int32_t n; auto b = take(4); std::memcpy(&n, b.data(), 4); return n; }
    size_t count() { auto n = integer(); if (n < 0 || size_t(n) > limit) fail("invalid collection count"); return size_t(n); }
    std::string string() {
        uint32_t n = 0;
        for (unsigned i = 0; i < 5; ++i) {
            const auto v = byte();
            if (i == 4 && (v & 0xf0)) fail("invalid string length");
            n |= uint32_t(v & 0x7f) << (i * 7);
            if (!(v & 0x80)) {
                if (n > 1024 * 1024) fail("metadata string too large");
                auto b = take(n); return std::string(b.begin(), b.end());
            }
        }
        fail("invalid string length");
    }
    void beginObject(int32_t id) {
        if (id <= 0 || objects_.size() >= limit || !objects_.emplace(id, nullptr).second)
            fail("invalid or duplicate object ID");
    }
    Json reference(int32_t id) { return Json{{"$ref", id}}; }
    Json primitive(uint8_t type) {
        size_t n = 0;
        switch (type) {
        case 1: case 2: case 10: n = 1; break;
        case 7: case 14: n = 2; break;
        case 8: case 11: case 15: n = 4; break;
        case 6: case 9: case 12: case 13: case 16: n = 8; break;
        default: fail("unsupported primitive type " + std::to_string(type));
        }
        auto b = take(n);
        uint64_t value = 0;
        for (size_t i = 0; i < n; ++i) value |= uint64_t(b[i]) << (8 * i);
        if (type == 1) { if (value > 1) fail("invalid Boolean"); return value != 0; }
        if (type == 8) return int32_t(value);
        return value;
    }
    void additional(Member& member) {
        switch (member.type) {
        case 0: case 7: member.primitive = byte(); break;
        case 1: case 2: case 5: case 6: break;
        case 3: string(); break;
        case 4: string(); if (!libraries_.contains(integer())) fail("unknown class library"); break;
        default: fail("unsupported member type");
        }
    }
    Json members(int32_t id, const Class& cls) {
        Json fields = Json::object();
        for (const auto& member : cls.members) {
            auto v = member.type == 0 ? primitive(member.primitive) : value();
            if (v.is_object() && v.contains("$nulls")) fail("null run outside an array");
            if (fields.contains(member.name)) fail("duplicate member name");
            fields[member.name] = std::move(v);
        }
        objects_.at(id) = Json{{"class", cls.name}, {"members", std::move(fields)}};
        return reference(id);
    }
    Json value() {
        if (++records_ > limit * 2 || depth_ >= 64) fail("metadata complexity limit");
        struct Depth { unsigned& n; Depth(unsigned& v): n(v) { ++n; } ~Depth() { --n; } } depth(depth_);
        const auto type = byte();
        if (type == 12) {
            auto id = integer(); string();
            if (id <= 0 || !libraries_.insert(id).second) fail("invalid library ID");
            return value();
        }
        if (type == 4 || type == 5) {
            auto id = integer(); beginObject(id);
            Class cls; cls.name = string(); auto n = count();
            for (size_t i = 0; i < n; ++i) cls.members.push_back({string(), 0});
            for (auto& member : cls.members) member.type = byte();
            for (auto& member : cls.members) additional(member);
            if (type == 5 && !libraries_.contains(integer())) fail("unknown class library");
            classes_.emplace(id, std::move(cls));
            return members(id, classes_.at(id));
        }
        if (type == 1) {
            auto id = integer(); beginObject(id); auto metadata = integer();
            auto found = classes_.find(metadata);
            if (found == classes_.end()) fail("unknown class metadata");
            return members(id, found->second);
        }
        if (type == 6) {
            auto id = integer(); beginObject(id); objects_.at(id) = string(); return reference(id);
        }
        if (type == 9) return reference(integer());
        if (type == 10) return nullptr;
        if (type == 13 || type == 14) {
            auto n = type == 13 ? size_t(byte()) : count();
            if (!n) fail("empty null run");
            return Json{{"$nulls", n}};
        }
        if (type == 16 || type == 17) {
            auto id = integer(); beginObject(id); auto n = count();
            Json values = Json::array();
            while (values.size() < n) {
                auto item = value();
                if (item.is_object() && item.contains("$nulls")) {
                    auto run = item.at("$nulls").get<size_t>();
                    if (run > n - values.size()) fail("array null run exceeds length");
                    while (run--) values.push_back(nullptr);
                } else values.push_back(std::move(item));
            }
            objects_.at(id) = std::move(values); return reference(id);
        }
        if (type == 8) return primitive(byte());
        fail("unsupported NRBF record " + std::to_string(type));
    }
    const Json& resolve(const Json& input) const {
        const Json* result = &input;
        for (unsigned i = 0; i < 64; ++i) {
            if (!result->is_object() || !result->contains("$ref")) return *result;
            auto found = objects_.find(result->at("$ref").get<int32_t>());
            if (found == objects_.end()) fail("dangling object reference");
            result = &found->second;
        }
        fail("cyclic object reference");
    }
public:
    explicit Reader(std::span<const uint8_t> bytes): bytes_(bytes) {
        if (bytes.size() > 16 * 1024 * 1024) fail("metadata too large");
    }
    std::vector<Link> links() {
        if (byte() != 0) fail("missing NRBF stream header");
        auto root = integer(); integer();
        if (integer() != 1 || integer() != 0) fail("unsupported NRBF version");
        while (pos_ < bytes_.size() && bytes_[pos_] != 11) value();
        if (byte() != 11 || pos_ != bytes_.size()) fail("invalid metadata end");
        const auto& object = resolve(reference(root));
        if (!object.is_object() || object.value("class", "") != "System.Collections.Specialized.NameValueCollection")
            fail("metadata is not a NameValueCollection");
        const auto& m = object.at("members");
        const auto& keys = resolve(m.at("Keys")); const auto& values = resolve(m.at("Values"));
        if (!keys.is_array() || !values.is_array() || keys.size() != values.size() ||
            !m.at("Count").is_number_integer() || m.at("Count").get<int64_t>() != int64_t(keys.size()))
            fail("invalid NameValueCollection dimensions");
        std::set<std::string> paths; std::vector<Link> result;
        for (size_t i = 0; i < keys.size(); ++i) {
            const auto& key = resolve(keys[i]); const auto& array = resolve(values[i]);
            if (!key.is_string() || !array.is_object() || array.value("class", "") != "System.Collections.ArrayList")
                fail("invalid link pair");
            const auto& list = array.at("members"); const auto& items = resolve(list.at("_items"));
            if (!items.is_array() || items.empty() || list.at("_size") != 1)
                fail("unsupported target list for " + key.get<std::string>() + "; expected one target");
            const auto& target = resolve(items[0]);
            if (!target.is_string()) fail("link target is not a string");
            Link link{key.get<std::string>(), target.get<std::string>()};
            if (link.path.empty() || link.target.empty() || link.path.find('\0') != std::string::npos ||
                link.target.find('\0') != std::string::npos || !paths.insert(link.path).second)
                fail("empty, duplicate or invalid link");
            result.push_back(std::move(link));
        }
        return result;
    }
};
uint32_t number(std::string_view text) {
    uint32_t n; auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), n);
    if (ec != std::errc{} || end != text.data() + text.size()) fail("invalid link path index");
    return n;
}
uint32_t read32(std::span<const uint8_t> bytes, size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) fail("reference outside payload");
    uint32_t n; std::memcpy(&n, bytes.data() + offset, 4); return n;
}
} // namespace

std::vector<Link> readMetadata(std::span<const uint8_t> bytes) {
    try { return Reader(bytes).links(); }
    catch (const nlohmann::json::exception& e) { fail(std::string("invalid metadata shape: ") + e.what()); }
}
RecordLinks readGameLinks(const big::File& package) {
    RecordLinks result;
    for (const auto* bankName : {"GameBINLinkMetaData", "ObjectLinks"}) {
        const auto* bank = package.findBank(bankName); if (!bank) continue;
        for (const auto& entry : bank->entries) {
            if (entry.name.empty() || result.contains(entry.name)) fail("unnamed or duplicate metadata entry");
            try { result.emplace(entry.name, readMetadata(package.entryData(entry))); }
            catch (const std::runtime_error& e) {
                std::string reason = e.what();
                if (reason.starts_with("FMP links: ")) reason.erase(0, 11);
                fail(entry.name + ": " + reason);
            }
        }
    }
    return result;
}

Location locate(const bin::Entry& entry, const defschema::Schema& schema, const Link& link) {
    const auto decoded = defdecode::decode(entry, schema);
    if (!decoded.clean()) fail(entry.name + ": cannot locate links in an unclean decode");
    std::vector<std::string_view> parts;
    std::string_view path = link.path;
    for (;;) {
        auto dot = path.find('.'); parts.push_back(path.substr(0, dot));
        if (dot == path.npos) break; path.remove_prefix(dot + 1);
    }
    if (parts.size() == 4 && parts[0].empty() && parts[1] == "CDefs") {
        if (entry.data.size() < 5) fail("missing CDef listing");
        const size_t count = size_t(entry.data[3]) | size_t(entry.data[4]) << 8;
        const auto index = number(parts[2]);
        if (index >= count || decoded.prefixLen != 5 + count * 12) fail("invalid CDef listing path");
        size_t member; Destination destination = Destination::GameEntry;
        if (parts[3] == "CDefName") { member = 0; destination = Destination::NameCrc; }
        else if (parts[3] == "CDefDataEntry") member = 4;
        else if (parts[3] == "InheritedEntryID") member = 8;
        else fail("unsupported CDef member " + std::string(parts[3]));
        return {5 + size_t(index) * 12 + member, 0, destination};
    }
    if (parts.size() < 2) fail("unsupported link path " + link.path);
    // ChocolateBox's decimal Control.ID uses the reverse byte order of the
    // little-endian runtime field tag (confirmed by its Control.ReadIn/Write).
    const auto id = number(parts[0]);
    const uint32_t tag = (id >> 24) | ((id >> 8) & 0xff00u) |
                         ((id << 8) & 0xff0000u) | (id << 24);
    size_t offset = decoded.prefixLen;
    for (size_t i = 0; i < decoded.fields.size(); ++i) {
        const auto& field = decoded.fields[i]; offset += 4;
        if (field.tag == tag) {
            if (parts.size() == 2 && !parts[1].empty() && field.value.size() == 4) {
                if (field.type == "CDefString") return {offset, i + 1, Destination::NameOffset};
                if (field.type == "int32" || field.type == "uint32" || field.type == "CDefIndex" || field.type == "J")
                    return {offset, i + 1, Destination::GameEntry};
            }
            // Legacy array names themselves contain dots, e.g.
            // CameraList.Add(CAMERA). The last two tokens identify its item.
            if (parts.size() >= 4 && !parts.back().empty() && !parts[1].empty() &&
                (field.type == "Vector_J" || field.type == "Vector_VCDefIndex__" ||
                 field.type == "Vector_VCDefString__")) {
                if (field.value.size() < 4) fail("invalid reference vector path");
                const size_t count = read32(field.value, 0), index = number(parts[parts.size() - 2]);
                if (count > (field.value.size() - 4) / 4 || field.value.size() != 4 + count * 4 || index >= count)
                    fail("invalid reference vector path");
                const auto destination = field.type == "Vector_VCDefString__" ?
                    Destination::NameOffset : Destination::GameEntry;
                return {offset + 4 + index * 4, i + 1, destination};
            }
            fail("unsupported reference field " + entry.name + ": " + field.name);
        }
        offset += field.value.size();
    }
    fail("unknown field in " + entry.name + ": " + link.path);
}

std::vector<uint8_t> relink(const bin::Entry& entry, const bin::File& targets,
                           const defschema::Schema& schema, std::span<const Link> links,
                           bool deferMissing) {
    auto data = entry.data; std::set<size_t> offsets;
    for (const auto& link : links) {
        const auto location = locate(entry, schema, link);
        if (!offsets.insert(location.offset).second) fail("multiple links address the same reference");
        std::optional<uint32_t> target;
        if (location.destination == Destination::GameEntry) {
            const auto* found = targets.find(link.target);
            if (found) target = uint32_t(found - targets.entries().data());
        } else {
            const auto name = targets.originalNameOffset(link.target);
            if (name) target = location.destination == Destination::NameCrc ? defdecode::fieldTag(link.target) : *name;
        }
        if (!target) {
            if (deferMissing) continue;
            fail(entry.name + ": missing target " + link.target + " for " + link.path);
        }
        read32(data, location.offset);
        std::memcpy(data.data() + location.offset, &*target, 4);
    }
    return data;
}
} // namespace forge::fmplinks
