#pragma once
#include "Controls.h"
#include <cstdint>

namespace seated {
using ClassifyFn = std::uint64_t (*)(const float*, const float*, const float*, unsigned);
using HoldFn = int (*)(HoldState*, const bool*, const float*, const bool*, const bool*, const float*, bool);

// Called on the mod's interaction thread. The OpenXR thread only queues requests
// through Bridge, never reads or writes these native interaction objects.
class Adapter {
public:
    ClassifyFn classify = nullptr;
    HoldFn update = nullptr;
    HoldState* gun = nullptr;
    HoldState* grenade = nullptr;
    const int* gunSlot = nullptr;
    float chestRadius = 0;

    void Request(std::uint64_t now) { expires_ = now + 250; targetHand_ = -1; }
    void Cancel() { expires_ = 0; targetHand_ = -1; }
    std::uint64_t Classify(int hand, const float* position, const float* holsters,
                           const float* chest, unsigned mask, std::uint64_t now) {
        auto result = classify(position, holsters, chest, mask);
        if (expires_ && now > expires_) Cancel();
        if (expires_ && hand == gun->hand && hand >= 0 && hand <= 1 &&
            (*gunSlot == 0 || *gunSlot == 1) && grenade->hand == -1) {
            const int target = 1 - *gunSlot;
            if (mask & (1u << target)) {
                targetHand_ = hand;
                return static_cast<std::uint64_t>(target);
            }
            Cancel(); // Never grab a world weapon or Arc Tool instead.
        }
        return ExpandChest(result, position, chest, mask, chestRadius);
    }
    int Update(HoldState* state, const bool* allowed, const float* grips,
               const bool* targets, const bool* transfer, const float* thresholds,
               bool autoTransfer, std::uint64_t now) {
        if (state == gun && expires_) {
            const int hand = targetHand_;
            const bool valid = now <= expires_ && hand >= 0 && hand <= 1 &&
                state->hand == hand && grenade->hand == -1 && allowed[hand] &&
                targets[hand] && grips[hand] >= thresholds[hand * 2];
            Cancel();
            if (valid) {
                // Re-enter the original acquisition path in the SAME hand. The
                // caller generates its own ticket and the original draw command;
                // it continues to own ammunition, acknowledgements and gun poses.
                state->hand = -1;
                state->pressed[hand] = 0;
                bool onlyThisHand[2] = {false, false};
                onlyThisHand[hand] = true;
                return update(state, onlyThisHand, grips, onlyThisHand, transfer, thresholds, false);
            }
        }
        return update(state, allowed, grips, targets, transfer, thresholds, autoTransfer);
    }
private:
    std::uint64_t expires_ = 0;
    int targetHand_ = -1;
};
}
