#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace seated {

// A short action fires on release. A long action fires once at the threshold,
// and stays pressed until release; changing context can never turn it into a tap.
class TapHold {
public:
    struct Output { bool original = false; bool swap = false; };
    Output Update(bool pressed, bool enabled, std::uint64_t now, unsigned holdMs) {
        if (mode_ == Mode::Released) {
            if (!pressed) return {};
            started_ = now;
            mode_ = enabled ? Mode::Pending : Mode::Passthrough;
        }
        if (mode_ == Mode::Passthrough || mode_ == Mode::Long) {
            if (!pressed) mode_ = Mode::Released;
            return {pressed, false};
        }
        if (mode_ == Mode::Cancelled) {
            if (!pressed) mode_ = Mode::Released;
            return {};
        }
        if (!enabled) {
            mode_ = pressed ? Mode::Cancelled : Mode::Released;
            return {};
        }
        if (now - started_ >= holdMs) {
            // If the first sample after crossing the threshold is a release,
            // emit the menu press for this sample, then release it next sample.
            mode_ = Mode::Long;
            return {true, false};
        }
        if (!pressed) {
            mode_ = Mode::Released;
            return {false, true};
        }
        return {};
    }
    void Cancel() { mode_ = Mode::Cancelled; }
private:
    enum class Mode { Released, Pending, Long, Passthrough, Cancelled };
    Mode mode_ = Mode::Released;
    std::uint64_t started_ = 0;
};

struct HoldState {
    std::uint8_t pressed[2];
    std::uint8_t padding[2];
    std::int32_t hand;
    std::int32_t eventHand;
};
static_assert(sizeof(HoldState) == 12);

// Native classifier returns an eight-byte aggregate: slot, grenade flag, padding.
inline int Slot(std::uint64_t candidate) { return static_cast<std::int32_t>(candidate & 0xffffffffu); }
inline bool Grenade(std::uint64_t candidate) { return ((candidate >> 32) & 0xffu) != 0; }
inline constexpr std::uint64_t GrenadeCandidate = 0x1ffffffffULL;

inline std::uint64_t ExpandChest(std::uint64_t original, const float* hand,
                                const float* chest, unsigned mask, float radius) {
    // Preserve every existing holster target and all original availability checks.
    if (Slot(original) >= 0 || Grenade(original) || !(mask & 16u)) return original;
    float distance2 = 0;
    for (int i = 0; i != 3; ++i) {
        const float d = hand[i] - chest[i];
        if (!std::isfinite(d)) return original;
        distance2 += d * d;
    }
    return distance2 <= radius * radius ? GrenadeCandidate : original;
}
}
