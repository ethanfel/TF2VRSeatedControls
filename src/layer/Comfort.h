#pragma once
#include "Controls.h"
#include <cstring>

namespace seated {
struct ComfortConfig {
    bool frontPouch = true;
    float pouchForward = 30.0f / 2.54f;
    float pouchDrop = 30.0f / 2.54f;
    float pouchRadius = 12.0f / 2.54f;
    float reloadDistance = 1.5f;
    float reloadAngle = 1.25f;
};

inline bool FrontPouch(const float* holsters, const float* hand, const ComfortConfig& config) {
    float forward[2] = {holsters[12], holsters[13]};
    const float length = std::hypot(forward[0], forward[1]);
    if (!std::isfinite(length) || length < 0.5f) return false;
    forward[0] /= length; forward[1] /= length;
    for (int side : {-1, 1}) {
        float center[3];
        center[0] = (holsters[0] + holsters[3]) * .5f + forward[0] * config.pouchForward - side * forward[1] * (18.0f / 2.54f);
        center[1] = (holsters[1] + holsters[4]) * .5f + forward[1] * config.pouchForward + side * forward[0] * (18.0f / 2.54f);
        center[2] = (holsters[2] + holsters[5]) * .5f - config.pouchDrop;
        float d2 = 0;
        for (int i = 0; i < 3; ++i) { const float d = hand[i] - center[i]; d2 += d * d; }
        if (std::isfinite(d2) && d2 <= config.pouchRadius * config.pouchRadius) return true;
    }
    return false;
}

// A brief entry cue, with exit debounce so boundary jitter cannot buzz repeatedly.
class EntryCue {
public:
    bool Update(bool inside, std::uint64_t now) {
        if (inside) {
            outsideAt_ = 0;
            if (!latched_) { latched_ = true; return true; }
        } else if (latched_) {
            if (!outsideAt_) outsideAt_ = now;
            if (now - outsideAt_ >= 150) latched_ = false;
        }
        return false;
    }
    void Reset() { latched_ = false; outsideAt_ = 0; }
private:
    bool latched_ = false;
    std::uint64_t outsideAt_ = 0;
};

class ToggleCrouch {
public:
    struct Output { float y; bool jump; };
    Output Update(float y, bool jump, bool enabled, std::uint64_t now) {
        if (!enabled || !std::isfinite(y)) { Reset(); return {y, jump}; }
        if (y > -.35f) armed_ = true;
        if (y < -.7f && armed_) { ducked_ = !ducked_; armed_ = false; }
        if (jump && !previousJump_ && ducked_) {
            ducked_ = false; pending_ = true; jumpAt_ = now + 150;
        }
        previousJump_ = jump;
        bool outputJump = jump;
        if (pending_) {
            outputJump = now >= jumpAt_;
            if (now >= jumpAt_ + 60) pending_ = false;
        }
        // Preserve turning (X is handled by the caller); suppress the raw down
        // direction after toggling off until the stick returns to neutral.
        return {ducked_ ? -1.0f : std::max(y, 0.0f), outputJump};
    }
    void Reset() { ducked_ = false; armed_ = false; previousJump_ = true; pending_ = false; }
private:
    bool ducked_ = false, armed_ = false, previousJump_ = false, pending_ = false;
    std::uint64_t jumpAt_ = 0;
};
}
