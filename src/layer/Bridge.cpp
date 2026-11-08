#include "Bridge.h"
#include "Adapter.h"
#include "Profiles.h"
#include "Support.h"
#include "NativeComfort.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

extern "C" void ClassifyThunk();
extern "C" void UpdateThunk();

namespace seated {
namespace {
std::mutex mutex;
Adapter adapter;
std::atomic<bool> ready{false};
std::atomic<bool> gameplayReady{false};
std::atomic<std::uint64_t> requested{0};
std::atomic<bool> cancelled{false};
const Profile* profile = nullptr;
unsigned char* base = nullptr;
bool attempted = false;
}

extern "C" std::uint64_t SeatedClassify(const float* position, const float* holsters, const float* chest,
                                      unsigned mask, std::uintptr_t returnAddress) {
    const auto caller = returnAddress - reinterpret_cast<std::uintptr_t>(base);
    std::lock_guard lock(mutex);
    const auto now = GetTickCount64();
    if (cancelled.exchange(false)) adapter.Cancel();
    const auto request = requested.exchange(0);
    if (request && now >= request && now - request <= 250) adapter.Request(request);
    const int hand = caller == profile->leftReturn ? 0 : caller == profile->rightReturn ? 1 : -1;
    return adapter.Classify(hand, position, holsters, chest, mask, now, gameplayReady.load());
}

extern "C" int SeatedUpdate(HoldState* state, const bool* allowed, const float* grips, const bool* targets,
                            const bool* transfer, const float* thresholds, bool autoTransfer) {
    std::lock_guard lock(mutex);
    if (cancelled.exchange(false)) adapter.Cancel();
    if (state == adapter.grenade) {
        for (int hand = 0; hand < 2; ++hand)
            ComfortCue(hand, state->hand == -1 && allowed[hand] && targets[hand] && adapter.gun->hand != hand, true);
    }
    return adapter.Update(state, allowed, grips, targets, transfer, thresholds, autoTransfer, GetTickCount64());
}

bool BridgeInitialize(float multiplier, const ComfortConfig& comfort) {
    if (attempted) return ready.load();
    HMODULE module = GetModuleHandleW(L"Titanfall2VR.dll");
    if (!module) return false;
    attempted = true;
    wchar_t path[32768] = {};
    const DWORD length = GetModuleFileNameW(module, path, 32768);
    if (!length || length == 32768) return false;
    const auto digest = FileSha256(path);
    for (const auto& p : Profiles) if (digest == p.sha256) profile = &p;
    if (!profile) {
        Log("Unsupported Titanfall2VR.dll SHA-256 %s; original controls retained.", digest.c_str());
        return false;
    }
    base = reinterpret_cast<unsigned char*>(module);
    // Also reject another hook already installed at either entry point.
    constexpr unsigned char classifier[] = {0x4c,0x8b,0xdc,0x48,0x81,0xec,0x98,0,0,0};
    constexpr unsigned char hold[] = {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x7c,0x24,0x18};
    if (std::memcmp(base + profile->classify, classifier, sizeof(classifier)) ||
        std::memcmp(base + profile->update, hold, sizeof(hold))) {
        Log("Interaction entry points differ from verified build; original controls retained.");
        return false;
    }
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    void* classify = base + profile->classify;
    void* update = base + profile->update;
    if (MH_CreateHook(classify, reinterpret_cast<void*>(&ClassifyThunk), reinterpret_cast<void**>(&adapter.classify)) != MH_OK) return false;
    if (MH_CreateHook(update, reinterpret_cast<void*>(&UpdateThunk), reinterpret_cast<void**>(&adapter.update)) != MH_OK) {
        MH_RemoveHook(classify); return false;
    }
    adapter.gun = reinterpret_cast<HoldState*>(base + profile->gun);
    adapter.grenade = reinterpret_cast<HoldState*>(base + profile->grenade);
    adapter.gunSlot = reinterpret_cast<int*>(base + profile->gun + 12);
    adapter.chestRadius = *reinterpret_cast<const float*>(base + profile->radius) * std::clamp(multiplier, 1.0f, 2.5f);
    adapter.rightSecondaryHolster = comfort.rightSecondaryHolster;
    if (!ComfortInitialize(base, *profile, comfort)) {
        MH_RemoveHook(classify); MH_RemoveHook(update);
        Log("Comfort entry points could not be verified/installed; original controls retained.");
        return false;
    }
    MH_QueueEnableHook(classify);
    MH_QueueEnableHook(update);
    if (MH_ApplyQueued() != MH_OK) {
        MH_DisableHook(classify); MH_DisableHook(update);
        MH_RemoveHook(classify); MH_RemoveHook(update);
        ComfortShutdown();
        Log("Could not install interaction hooks; original controls retained.");
        return false;
    }
    ready = true;
    Log("Adapter active for %s; chest pickup radius multiplier %.2f; right secondary holster %d.",
        profile->name, multiplier, comfort.rightSecondaryHolster);
    return true;
}

bool BridgeReady() { return ready.load(); }
bool BridgeGameplay(bool active) {
    bool allowed = active && ready;
    if (allowed) {
        unsigned char dialogue = 1;
        SIZE_T count = 0;
        allowed = ReadProcessMemory(GetCurrentProcess(), base + profile->dialogue, &dialogue, sizeof(dialogue), &count) && count == sizeof(dialogue) && !dialogue;
    }
    gameplayReady = allowed;
    ComfortGameplay(allowed);
    return allowed;
}
unsigned BridgeCues() { return ComfortCues(); }
bool BridgeCanSwap() {
    if (!ready) return false;
    unsigned char dialogue = 1;
    int hand = -1;
    SIZE_T read = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), base + profile->dialogue, &dialogue, sizeof(dialogue), &read) || read != sizeof(dialogue)) return false;
    if (!ReadProcessMemory(GetCurrentProcess(), base + profile->gun + 4, &hand, sizeof(hand), &read) || read != sizeof(hand)) return false;
    // Empty-handed Y and conversation choices retain the original immediate action.
    return !dialogue && (hand == 0 || hand == 1);
}
void BridgeRequestSwap() { if (ready) requested = GetTickCount64(); }
void BridgeCancel() { requested = 0; cancelled = true; }
void BridgeShutdown() {
    gameplayReady = false;
    BridgeCancel();
    if (!ready.exchange(false)) return;
    ComfortShutdown();
    MH_QueueDisableHook(base + profile->classify);
    MH_QueueDisableHook(base + profile->update);
    MH_ApplyQueued();
    MH_RemoveHook(base + profile->classify);
    MH_RemoveHook(base + profile->update);
    // Do not uninitialize other users of MinHook or change their hooks.
    attempted = false;
    adapter = {};
    Log("Interaction hooks removed.");
}
}
