#include "vanilla_props.hpp"

#include <cctype>

namespace albion::editor {

namespace {
const VanillaField kFields[] = {
#include "vanilla_props.inc"
};

bool sameKey(const char* a, const std::string& b) {
    size_t i = 0;
    for (; a[i] && i < b.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    return !a[i] && i == b.size();
}
} // namespace

const VanillaField* vanillaField(const std::string& ctc, const std::string& key) {
    for (const auto& f : kFields)
        if (ctc == f.ctc && sameKey(f.key, key)) return &f;
    return nullptr;
}

} // namespace albion::editor
