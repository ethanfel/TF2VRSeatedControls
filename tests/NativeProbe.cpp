#include "Adapter.h"
#include "Bridge.h"
#include "Profiles.h"
#include "Support.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

extern "C" std::uint64_t SeatedClassify(const float*, const float*, const float*, unsigned, std::uintptr_t);

static int checks = 0;
static void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
#include "ReloadFixture.h"

int main(int argc, char** argv) {
    using namespace seated;
    if (argc != 2) { std::fprintf(stderr, "Usage: native_probe <official Titanfall2VR.dll>\n"); return 2; }
    int size = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, nullptr, 0);
    std::wstring path(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, path.data(), size);
    const auto hash = FileSha256(path);
    const Profile* profile = nullptr;
    for (const auto& p : Profiles) if (hash == p.sha256) profile = &p;
    Check(profile != nullptr, "official binary matches a supported SHA-256");
    // Map only: do NOT run DllMain or resolve/load game dependencies. These two
    // interaction routines only use their arguments and constants in the image.
    // Reload tests additionally supply the required pure CRT math/memory imports.
    auto module = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    Check(module != nullptr, "binary maps without running the game");
    auto base = reinterpret_cast<unsigned char*>(module);
    ResolveTestMath(base);
    ReloadFixture closeReload(2), distantReload(8);
    closeReload.Run(base, *profile); distantReload.Run(base, *profile);
    if (!closeReload.Inserted()) closeReload.Dump(profile == &Profiles[1]);
    Check(closeReload.Inserted(), "original native reload accepts a close magazine");
    Check(!distantReload.Inserted(), "original native reload rejects an 8 cm offset");
    ReloadFixture angledOriginal(0);
    angledOriginal.Run(base, *profile, 70);
    Check(!angledOriginal.Inserted(), "original native reload rejects a 70 degree insertion");
    Adapter adapter;
    adapter.classify = reinterpret_cast<ClassifyFn>(base + profile->classify);
    adapter.update = reinterpret_cast<HoldFn>(base + profile->update);
    HoldState gun{{0,1},{0,0},1,-1}, grenade{{0,0},{0,0},-1,-1};
    int slot = 0;
    adapter.gun = &gun; adapter.grenade = &grenade; adapter.gunSlot = &slot;
    const float originalRadius = *reinterpret_cast<float*>(base + profile->radius);
    adapter.chestRadius = originalRadius * 1.75f;
    alignas(16) float position[4] = {originalRadius * 1.4f, 0, 0, 0};
    alignas(16) float chest[4] = {0,0,0,0};
    alignas(16) float holsters[16] = {100,100,100, 100,100,100, 100,100,100, 100,100,100, 1,0,0,0};
    Check(!Grenade(adapter.classify(position, holsters, chest, 16)), "original misses expanded chest zone");
    Check(Grenade(adapter.Classify(0, position, holsters, chest, 16, 100)), "adapter expands actual native chest test");
    Check(!Grenade(adapter.Classify(0, position, holsters, chest, 0, 100)), "native availability mask respected");
    position[0] = 100;
    Check(!Grenade(adapter.Classify(0, position, holsters, chest, 16, 100)), "distant hand remains unchanged");

    alignas(16) const float hipAnchors[15] = {100,100,100, 100,100,100, 0,15,-30, 0,-15,-30, 1,0,0};
    const auto originalAnchors = std::array<float,15>{100,100,100, 100,100,100, 0,15,-30, 0,-15,-30, 1,0,0};
    alignas(16) float rightHip[4] = {0,-15,-30,0}, leftHip[4] = {0,15,-30,0};
    alignas(16) const float farChest[4] = {100,100,200,0};
    Check(Slot(adapter.classify(leftHip, hipAnchors, farChest, 2)) == 1, "native secondary belongs to left hip");
    Check(Slot(adapter.classify(rightHip, hipAnchors, farChest, 2)) == -1, "native secondary is absent at right hip");
    Check(Slot(adapter.Classify(1, rightHip, hipAnchors, farChest, 2, 200, true)) == 1, "right hand grabs copied secondary at right hip");
    Check(Slot(adapter.Classify(0, rightHip, hipAnchors, farChest, 2, 200, true)) == 1, "left hand can also reach copied right holster");
    Check(Slot(adapter.Classify(1, leftHip, hipAnchors, farChest, 2, 200, true)) == 1, "original left secondary remains available");
    Check(!std::memcmp(hipAnchors, originalAnchors.data(), sizeof(hipAnchors)), "copy never edits native body anchors");
    Check(Slot(adapter.Classify(1, rightHip, hipAnchors, farChest, 1, 200, true)) == -1, "copy cannot invent an unowned secondary");
    Check(Slot(adapter.Classify(1, rightHip, hipAnchors, farChest, 10, 200, true)) == 3, "right-hip Arc Tool keeps priority");
    Check(Grenade(adapter.Classify(1, rightHip, hipAnchors, rightHip, 18, 200, true)), "native grenade target keeps priority over copy");
    Check(Slot(adapter.Classify(1, rightHip, hipAnchors, farChest, 2, 200, false)) == -1, "menus and inactive gameplay disable extra holster");
    Check(Slot(adapter.Classify(-1, rightHip, hipAnchors, farChest, 2, 200, true)) == -1, "unrecognized native callers retain original result");
    adapter.rightSecondaryHolster = false;
    Check(Slot(adapter.Classify(1, rightHip, hipAnchors, farChest, 2, 200, true)) == -1, "copy can be disabled independently");
    Check(Slot(adapter.Classify(1, leftHip, hipAnchors, farChest, 2, 200, true)) == 1, "disabling copy preserves original left holster");
    adapter.rightSecondaryHolster = true;
    rightHip[0] = 8; // Outside the native 18.5 cm forward half-extent.
    Check(Slot(adapter.Classify(1, rightHip, hipAnchors, farChest, 2, 200, true)) == -1, "copy retains native holster bounds");
    rightHip[0] = std::numeric_limits<float>::quiet_NaN();
    Check(Slot(adapter.Classify(1, rightHip, hipAnchors, farChest, 2, 200, true)) == -1, "invalid hand position cannot select copied secondary");
    rightHip[0] = 0;
    float rotatedAnchors[15]; std::copy_n(hipAnchors,15,rotatedAnchors);
    for (int i = 0; i < 12; i += 3) { rotatedAnchors[i] = 40-hipAnchors[i+1]; rotatedAnchors[i+1] = 70+hipAnchors[i]; }
    rotatedAnchors[12] = 0; rotatedAnchors[13] = 1;
    alignas(16) float rotatedHand[4] = {55,70,-30,0};
    Check(Slot(adapter.Classify(1, rotatedHand, rotatedAnchors, farChest, 2, 200, true)) == 1, "copy follows translated and rotated body anchors");
    HoldState drawn{{0,0},{0,0},-1,-1};
    const bool drawAllowed[2] = {true,true}, drawTarget[2] = {false,true}, drawTransfer[2] = {false,false};
    const float drawGrip[2] = {0,1}, drawThreshold[4] = {.75f,.25f,.75f,.25f};
    Check(adapter.update(&drawn,drawAllowed,drawGrip,drawTarget,drawTransfer,drawThreshold,false) == 1 && drawn.hand == 1,
          "copied target uses ordinary right-hand native weapon acquisition");

    const bool allowed[2] = {true,true}, targets[2] = {false,true}, transfer[2] = {false,false};
    const float grips[2] = {0,1}, thresholds[4] = {0.75f,0.25f,0.75f,0.25f};
    adapter.Request(1000);
    Check(Slot(adapter.Classify(1, position, holsters, chest, 3, 1001)) == 1, "primary requests secondary slot");
    Check(adapter.Update(&gun, allowed, grips, targets, transfer, thresholds, false, 1002) == 1,
          "native state machine emits same-hand acquire event");
    Check(gun.hand == 1 && gun.eventHand == 1, "native ownership stays in right hand");
    Check(adapter.Update(&gun, allowed, grips, targets, transfer, thresholds, false, 1003) == -1,
          "swap emits only one native acquire event");
    slot = 1;
    adapter.Request(1100);
    Check(Slot(adapter.Classify(1, position, holsters, chest, 3, 1101)) == 0, "secondary requests primary slot");
    adapter.Cancel();
    adapter.Request(1200);
    Check(Slot(adapter.Classify(1, position, holsters, chest, 2, 1201)) != 0, "missing target slot never fabricated");
    slot = 0;
    adapter.Request(1300);
    adapter.Classify(1, position, holsters, chest, 3, 1301);
    Check(adapter.Update(&gun, allowed, grips, targets, transfer, thresholds, false, 1600) == -1,
          "expired request never swaps late");
    grenade.hand = 0;
    adapter.Request(1700);
    Check(Slot(adapter.Classify(1, position, holsters, chest, 3, 1701)) != 1, "held grenade blocks swap");
    grenade.hand = -1;
    gun.hand = -1;
    adapter.Request(1800);
    Check(Slot(adapter.Classify(1, position, holsters, chest, 3, 1801)) != 1, "empty hand never fabricates a draw");
    gun.hand = 0; gun.pressed[0] = 1; gun.pressed[1] = 0;
    adapter.Request(1900);
    Check(Slot(adapter.Classify(0, position, holsters, chest, 3, 1901)) == 1, "left-hand weapon uses left hand");
    const float leftGrip[2] = {1,0};
    const bool leftTarget[2] = {true,false};
    Check(adapter.Update(&gun, allowed, leftGrip, leftTarget, transfer, thresholds, false, 1902) == 0,
          "native left-hand acquire event correct");
    Check(gun.hand == 0, "left-hand ownership retained");
    Check(BridgeInitialize(1.75f), "actual MinHook adapter installs on supported binary");
    BridgeGameplay(true);
    ReloadFixture comfortableReload(8), tooFarReload(30), noAmmoReload(8, 0);
    comfortableReload.Run(base, *profile); tooFarReload.Run(base, *profile); noAmmoReload.Run(base, *profile);
    if (!comfortableReload.Inserted()) comfortableReload.Dump(profile == &Profiles[1]);
    Check(comfortableReload.Inserted(), "hooked native reload accepts an 8 cm offset");
    Check(!tooFarReload.Inserted(), "hooked native reload rejects a distant magazine");
    Check(!noAmmoReload.Inserted(), "forgiving native reload cannot manufacture ammunition");
    ReloadFixture angledComfort(0), backwardsReload(0);
    angledComfort.Run(base, *profile, 70); backwardsReload.Run(base, *profile, 160);
    Check(angledComfort.Inserted(), "forgiving native reload accepts a 70 degree insertion");
    Check(!backwardsReload.Inserted(), "backwards magazine is still rejected");
    BridgeGameplay(false); BridgeGameplay(true);
    ReloadFixture supplyPreview(0);
    supplyPreview.state[0xa] = 0; supplyPreview.input[0xe0] = 1;
    ReloadFixture::Put(supplyPreview.input.data(),0xc,0.0f);
    auto tickReload = reinterpret_cast<void (*)(unsigned char*, const unsigned char*)>(base + profile->reload);
    tickReload(supplyPreview.state.data(),supplyPreview.input.data());
    Check(BridgeCues() == 1, "fresh magazine supply cues the free hand before gripping");
    tickReload(supplyPreview.state.data(),supplyPreview.input.data());
    Check(BridgeCues() == 0, "magazine supply cue does not repeat inside zone");
    BridgeGameplay(false); BridgeGameplay(true);
    ReloadFixture::Put(supplyPreview.input.data(),0x1c,0);
    tickReload(supplyPreview.state.data(),supplyPreview.input.data());
    Check(BridgeCues() == 0, "empty reserve does not offer magazine feedback");
    position[0] = originalRadius * 1.4f;
    Check(Grenade(adapter.classify(position, holsters, chest, 16)), "installed classifier detour expands chest pickup");
    gun.hand = 1; gun.pressed[0] = 0; gun.pressed[1] = 1;
    Check(adapter.update(&gun, allowed, grips, targets, transfer, thresholds, false) == -1,
          "installed update detour preserves ordinary holding and all seven arguments");

    // Reproduce the verified caller return address without executing the game's
    // surrounding render loop. This exercises request routing, both thunks and
    // the actual native state objects, rather than just the isolated Adapter.
    auto realGun = reinterpret_cast<HoldState*>(base + profile->gun);
    auto realGrenade = reinterpret_cast<HoldState*>(base + profile->grenade);
    *realGun = {{0,1},{0,0},1,-1};
    *realGrenade = {{0,0},{0,0},-1,-1};
    *reinterpret_cast<int*>(base + profile->gun + 12) = 0;
    unsigned char* returnSite = base + profile->rightReturn;
    unsigned char saved[5]; std::memcpy(saved, returnSite, sizeof(saved));
    DWORD protection = 0;
    Check(VirtualProtect(returnSite, 5, PAGE_EXECUTE_READWRITE, &protection) != 0, "prepare synthetic native caller");
    const unsigned char returnPad[] = {0x48,0x83,0xc4,0x28,0xc3}; // add rsp,28h; ret
    std::memcpy(returnSite, returnPad, sizeof(returnPad));
    FlushInstructionCache(GetCurrentProcess(), returnSite, sizeof(returnPad));
    // mov r10,[rsp+28h]; mov rax,[rsp+30h]; sub rsp,28h; push r10; jmp rax
    const unsigned char invoke[] = {0x4c,0x8b,0x54,0x24,0x28, 0x48,0x8b,0x44,0x24,0x30,
                                    0x48,0x83,0xec,0x28, 0x41,0x52, 0xff,0xe0};
    auto shim = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    Check(shim != nullptr, "allocate synthetic caller");
    std::memcpy(shim, invoke, sizeof(invoke));
    DWORD shimProtection = 0;
    Check(VirtualProtect(shim, 4096, PAGE_EXECUTE_READ, &shimProtection) != 0, "make synthetic caller executable");
    FlushInstructionCache(GetCurrentProcess(), shim, sizeof(invoke));
    using Invoke = std::uint64_t (*)(const float*, const float*, const float*, unsigned, void*, ClassifyFn);
    Check(Slot(reinterpret_cast<Invoke>(shim)(rightHip, hipAnchors, farChest, 2, returnSite, adapter.classify)) == 1,
          "installed bridge and thunk expose secondary on right hip");
    BridgeGameplay(false);
    Check(Slot(reinterpret_cast<Invoke>(shim)(rightHip, hipAnchors, farChest, 2, returnSite, adapter.classify)) == -1,
          "installed extra holster stops when gameplay loses focus");
    BridgeGameplay(true);
    BridgeRequestSwap();
    Check(Slot(reinterpret_cast<Invoke>(shim)(position, holsters, chest, 3, returnSite, adapter.classify)) == 1,
          "actual bridge routes Y request to the verified right-hand caller");
    Check(adapter.update(realGun, allowed, grips, targets, transfer, thresholds, false) == 1,
          "actual bridge acquires the other weapon in the original hand");
    Check(realGun->hand == 1 && realGun->eventHand == 1, "actual native ownership after swap");
    std::memcpy(returnSite, saved, sizeof(saved));
    FlushInstructionCache(GetCurrentProcess(), returnSite, sizeof(saved));
    DWORD unused = 0; VirtualProtect(returnSite, sizeof(saved), protection, &unused);
    VirtualFree(shim, 0, MEM_RELEASE);

    // Route the real supply query through its verified native caller, without
    // running the surrounding renderer. All other callers retain their result.
    using PouchFn = bool (*)(const float*, const float*, unsigned);
    const auto pouch = reinterpret_cast<PouchFn>(base + profile->pouch);
    float frontHand[4] = {100 + 30/2.54f, 100 + 18/2.54f, 100 - 30/2.54f, 0};
    Check(!pouch(holsters, frontHand, 0), "unrelated pouch-query caller is unchanged");
    returnSite = base + profile->pouchReturn;
    std::memcpy(saved, returnSite, sizeof(saved));
    Check(VirtualProtect(returnSite, 5, PAGE_EXECUTE_READWRITE, &protection) != 0, "prepare synthetic pouch caller");
    std::memcpy(returnSite, returnPad, sizeof(returnPad));
    FlushInstructionCache(GetCurrentProcess(), returnSite, sizeof(returnPad));
    const unsigned char pouchInvoke[] = {0x48,0x8b,0x44,0x24,0x28,0x48,0x83,0xec,0x28,0x41,0x51,0xff,0xe0};
    shim = VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(shim != nullptr, "allocate pouch caller");
    std::memcpy(shim,pouchInvoke,sizeof(pouchInvoke));
    Check(VirtualProtect(shim,4096,PAGE_EXECUTE_READ,&shimProtection) != 0,"make pouch caller executable");
    FlushInstructionCache(GetCurrentProcess(),shim,sizeof(pouchInvoke));
    using InvokePouch = bool (*)(const float*, const float*, unsigned, void*, PouchFn);
    Check(reinterpret_cast<InvokePouch>(shim)(holsters,frontHand,0,returnSite,pouch), "native reload supply accepts front pouch");
    BridgeGameplay(false);
    Check(!reinterpret_cast<InvokePouch>(shim)(holsters,frontHand,0,returnSite,pouch), "front supply disabled outside active gameplay");
    BridgeGameplay(true);
    std::memcpy(returnSite,saved,sizeof(saved)); FlushInstructionCache(GetCurrentProcess(),returnSite,sizeof(saved));
    VirtualProtect(returnSite,sizeof(saved),protection,&unused); VirtualFree(shim,0,MEM_RELEASE);

    // Pre-grab feedback uses the original grenade eligibility inputs.
    BridgeCues();
    const bool grenadeTargets[2] = {true,false};
    const float idleGrips[2] = {0,0};
    adapter.update(realGrenade, allowed, idleGrips, grenadeTargets, transfer, thresholds, false);
    Check(BridgeCues() == 1, "grenade eligibility cues the free left hand");
    adapter.update(realGrenade, allowed, idleGrips, grenadeTargets, transfer, thresholds, false);
    Check(BridgeCues() == 0, "native grenade cue does not repeat while staying in zone");
    BridgeShutdown();
    Check(!Grenade(adapter.classify(position, holsters, chest, 16)), "shutdown restores original native code");
    ReloadFixture restoredReload(8); restoredReload.Run(base,*profile);
    Check(!restoredReload.Inserted(), "shutdown restores original reload tolerances");
    ComfortConfig originalSettings; originalSettings.reloadDistance = originalSettings.reloadAngle = 1;
    originalSettings.rightSecondaryHolster = false;
    Check(BridgeInitialize(1.75f, originalSettings), "reload assistance can be disabled independently");
    BridgeGameplay(true);
    Check(Slot(SeatedClassify(rightHip, hipAnchors, farChest, 2, reinterpret_cast<std::uintptr_t>(base + profile->rightReturn))) == -1,
          "disabled setting reaches installed bridge without changing native right hip");
    ReloadFixture disabledReload(8); disabledReload.Run(base,*profile);
    Check(!disabledReload.Inserted(), "100 percent reload settings preserve original rejection");
    BridgeShutdown();
    std::printf("PASS: %s, %d checks against official native routines and hooks\n", profile->name, checks);
    FreeLibrary(module);
}
