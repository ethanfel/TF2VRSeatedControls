#include "NativeComfort.h"
#include "Support.h"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>

extern "C" void PouchThunk();
extern "C" void ReloadThunk();
namespace seated {
namespace {
using PouchFn = bool (*)(const float*, const float*, unsigned);
using ReloadFn = void (*)(unsigned char*, const unsigned char*);
PouchFn pouch = nullptr;
ReloadFn reload = nullptr;
unsigned char* image = nullptr;
const Profile* build = nullptr;
ComfortConfig config;
std::vector<void*> hooks;
unsigned char* stubs = nullptr;
std::atomic<bool> gameplay{false};
std::atomic<unsigned> cues{0};
std::mutex cueMutex;
EntryCue entries[2][2];

template<class T> T Read(const unsigned char* p, unsigned offset) {
    T value; std::memcpy(&value, p + offset, sizeof(value)); return value;
}
bool Hook(void* target, void* detour, void** original) {
    if (MH_CreateHook(target, detour, original) != MH_OK) return false;
    hooks.push_back(target); return true;
}
// Replace ONLY this instruction's constant load. Shared .rdata constants remain
// untouched. The tiny detour changes the same XMM register as the original and
// preserves flags, all other registers and the stack. MinHook suspends threads
// while enabling/disabling the group.
bool Threshold(unsigned rva, unsigned reg, float expected, float value, unsigned index) {
    auto source = image + rva;
    const unsigned char opcode[] = {0xf3, 0x0f, 0x10, static_cast<unsigned char>(reg ? 0x0d : 0x05)};
    if (std::memcmp(source, opcode, 4)) return false;
    const auto displacement = Read<std::int32_t>(source, 4);
    if (std::abs(Read<float>(source + 8 + displacement, 0) - expected) > .0001f) return false;
    auto stub = stubs + index * 32;
    std::memcpy(stub, opcode, 4);
    const std::int32_t local = 16; // constant at +24, instruction ends at +8
    std::memcpy(stub + 4, &local, 4);
    const unsigned char jump[] = {0xff, 0x25, 0, 0, 0, 0};
    std::memcpy(stub + 8, jump, 6);
    const auto next = source + 8;
    std::memcpy(stub + 14, &next, 8);
    std::memcpy(stub + 24, &value, 4);
    void* unused = nullptr;
    return Hook(source, stub, &unused);
}
}

extern "C" bool SeatedPouch(const float* holsters, const float* hand, unsigned slot, std::uintptr_t caller) {
    const bool original = pouch(holsters, hand, slot);
    if (!gameplay || !config.frontPouch || slot || caller != reinterpret_cast<std::uintptr_t>(image) + build->pouchReturn) return original;
    // An additional supply point: shoulder grabs and weapon holsters stay valid.
    return original || FrontPouch(holsters, hand, config);
}

extern "C" void SeatedReload(unsigned char* state, const unsigned char* input) {
    const int hand = input[8] ? 1 : 0; // native reload input is the off hand
    bool valid = false;
    if (gameplay && input[4] && input[0xe0] && !state[0xa] && !state[0xb] && !state[0x12]) {
        const auto weapon = Read<const unsigned char*>(state, 0);
        const int type = Read<int>(weapon, 0xc);
        const int clip = Read<int>(input, 0x18), reserve = Read<int>(input, 0x1c);
        const int capacity = Read<int>(state, 0x28), stage = Read<int>(state, 0x20);
        if (type == 2 || reserve > 0) {
            if (stage < 0) valid = type != 2 && clip < capacity;
            else {
                const auto steps = Read<const unsigned char*>(weapon, state[0xf] ? 0x250 : 0x240);
                const int count = Read<int>(weapon, state[0xf] ? 0x258 : 0x248);
                if (steps && stage < count) {
                    const bool magazineStep = Read<int>(steps, stage * 8) == 0;
                    valid = magazineStep && ((!weapon[0x280] || !state[8]) ? (clip < capacity || !state[8]) : true);
                }
            }
        }
    }
    ComfortCue(hand, valid, false);
    reload(state, input);
}

void ComfortCue(int hand, bool valid, bool grenade) {
    if (hand < 0 || hand > 1) return;
    std::lock_guard lock(cueMutex);
    if (!gameplay) { entries[hand][grenade ? 1 : 0].Reset(); return; }
    if (entries[hand][grenade ? 1 : 0].Update(valid, GetTickCount64())) cues.fetch_or(1u << hand);
}
void ComfortGameplay(bool active) {
    gameplay = active;
    if (!active) {
        cues = 0;
        std::lock_guard lock(cueMutex);
        for (auto& hand : entries) for (auto& entry : hand) entry.Reset();
    }
}
unsigned ComfortCues() { return cues.exchange(0); }

bool ComfortInitialize(unsigned char* base, const Profile& profile, const ComfortConfig& settings) {
    image = base; build = &profile; config = settings;
    constexpr unsigned char pouchEntry[] = {0x48,0x83,0xec,0x38,0x0f,0x10,0x02};
    constexpr unsigned char reloadEntry[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x18};
    if (std::memcmp(base + profile.pouch, pouchEntry, sizeof(pouchEntry)) ||
        std::memcmp(base + profile.reload, reloadEntry, sizeof(reloadEntry))) return false;
    stubs = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!stubs) return false;
    const float distance = std::clamp(config.reloadDistance, 1.0f, 2.0f);
    const float angle = std::clamp(config.reloadAngle, 1.0f, 1.5f);
    bool ok = Hook(base + profile.pouch, reinterpret_cast<void*>(&PouchThunk), reinterpret_cast<void**>(&pouch)) &&
        Hook(base + profile.reload, reinterpret_cast<void*>(&ReloadThunk), reinterpret_cast<void**>(&reload)) &&
        Threshold(profile.reloadDistance, 1, 6.5f/2.54f, 6.5f/2.54f*distance, 0) &&
        Threshold(profile.reloadAngle, 1, 1.04719758f, 1.04719758f*angle, 1) &&
        Threshold(profile.guideDistance, 0, 8.0f/2.54f, 8.0f/2.54f*distance, 2) &&
        Threshold(profile.guideAngle, 0, 1.91986215f, std::min(2.443461f, 1.91986215f*angle), 3) &&
        Threshold(profile.seatDistance, 0, .1f/2.54f, (distance > 1.0f ? .8f : .1f)/2.54f, 4);
    DWORD previous = 0;
    ok = ok && VirtualProtect(stubs, 4096, PAGE_EXECUTE_READ, &previous);
    if (ok) {
        FlushInstructionCache(GetCurrentProcess(), stubs, 4096);
        for (auto target : hooks) if (MH_QueueEnableHook(target) != MH_OK) ok = false;
        ok = ok && MH_ApplyQueued() == MH_OK;
    }
    if (!ok) { ComfortShutdown(); return false; }
    Log("Comfort hooks active: front supply %d; reload distance %.2fx, angle %.2fx.", config.frontPouch, distance, angle);
    return true;
}
void ComfortShutdown() {
    ComfortGameplay(false);
    for (auto target : hooks) MH_QueueDisableHook(target);
    MH_ApplyQueued();
    for (auto target : hooks) MH_RemoveHook(target);
    hooks.clear();
    if (stubs) VirtualFree(stubs, 0, MEM_RELEASE);
    stubs = nullptr;
}
}
