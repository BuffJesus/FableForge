#pragma once
// forge::controls -- read and edit CControlsDef input bindings.
//
// CControlsDef.Controls is a Vector<CActionInputControl>: [u32 count][count x 28B].
// Each 28-byte record is a FIXED, fully-persisted flat struct -- on-disk encoding
// confirmed against two retail game.bin builds (see FableTLC docs/FINDINGS.md
// "ON-DISK ENCODING -- empirically resolved"):
//   +0  i32 GameAction
//   +4  i32 ControllerType     1=pad, 2=keyboard, 3=mouse
//   +8  i32 keyVal             device slot read when ControllerType==2
//   +12 i32 xboxVal            device slot read when ControllerType==1
//   +16 i32 mouseVal           device slot read when ControllerType==3
//   +20 f32 dirX               analog/direction (persisted; can be non-zero)
//   +24 f32 dirY
// Only the device slot matching ControllerType holds the binding; the other two
// are 0. Remapping is therefore a pure game.bin data edit: rewrite ControllerType
// and the matching device value in place. The engine resolves nothing by field
// name here -- these are raw fixed-offset ints -- so no CRC/tag work is needed
// beyond locating the "Controls" field value (which forge::defdecode already does).

#include <cstdint>
#include <cstring>
#include <vector>

namespace forge::controls {

constexpr std::size_t kRecordSize = 28;

enum ControllerType : std::int32_t { PAD = 1, KEYBOARD = 2, MOUSE = 3 };

struct InputBinding {
    std::int32_t action = 0;
    std::int32_t type = 0;   // ControllerType
    std::int32_t key = 0;
    std::int32_t xbox = 0;
    std::int32_t mouse = 0;
    float dirX = 0.0f;
    float dirY = 0.0f;

    // The active device value for this binding's ControllerType (0 if unknown).
    std::int32_t device() const {
        return type == PAD ? xbox : type == KEYBOARD ? key : type == MOUSE ? mouse : 0;
    }
};

// True iff `value` is a well-formed [u32 count][count x 28B] Controls buffer.
inline bool valid(const std::vector<std::uint8_t>& value) {
    if (value.size() < 4) return false;
    std::uint32_t count;
    std::memcpy(&count, value.data(), 4);
    return value.size() - 4 == static_cast<std::size_t>(count) * kRecordSize;
}

// Parse a Controls field value into bindings. Returns empty if malformed.
inline std::vector<InputBinding> parse(const std::vector<std::uint8_t>& value) {
    std::vector<InputBinding> out;
    if (!valid(value)) return out;
    std::uint32_t count;
    std::memcpy(&count, value.data(), 4);
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* p = value.data() + 4 + static_cast<std::size_t>(i) * kRecordSize;
        InputBinding b;
        std::memcpy(&b.action, p, 4);
        std::memcpy(&b.type, p + 4, 4);
        std::memcpy(&b.key, p + 8, 4);
        std::memcpy(&b.xbox, p + 12, 4);
        std::memcpy(&b.mouse, p + 16, 4);
        std::memcpy(&b.dirX, p + 20, 4);
        std::memcpy(&b.dirY, p + 24, 4);
        out.push_back(b);
    }
    return out;
}

// Rebind `action` to (newType, newDevice), mutating `value` in place. Sets
// ControllerType and the matching device slot, ZEROING the other two device slots
// so exactly one is active (matches retail). dirX/dirY are left untouched.
//
// NOTE: an action may hold MORE THAN ONE binding record (e.g. a stance bound to
// several buttons). This rebinds EVERY record whose GameAction == action and
// returns how many were changed (0 if the action is absent or `value` is
// malformed / newType out of range). Pass a specific record via `occurrence` >= 0
// to change only the Nth matching record.
inline int setBinding(std::vector<std::uint8_t>& value, std::int32_t action,
                      std::int32_t newType, std::int32_t newDevice,
                      int occurrence = -1) {
    if (newType < PAD || newType > MOUSE) return 0;
    if (!valid(value)) return 0;
    std::uint32_t count;
    std::memcpy(&count, value.data(), 4);
    int changed = 0;
    int seen = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint8_t* p = value.data() + 4 + static_cast<std::size_t>(i) * kRecordSize;
        std::int32_t a;
        std::memcpy(&a, p, 4);
        if (a != action) continue;
        int idx = seen++;
        if (occurrence >= 0 && idx != occurrence) continue;
        std::int32_t key = 0, xbox = 0, mouse = 0;
        if (newType == KEYBOARD) key = newDevice;
        else if (newType == PAD) xbox = newDevice;
        else mouse = newDevice;
        std::memcpy(p + 4, &newType, 4);
        std::memcpy(p + 8, &key, 4);
        std::memcpy(p + 12, &xbox, 4);
        std::memcpy(p + 16, &mouse, 4);
        ++changed;
    }
    return changed;
}

}  // namespace forge::controls
