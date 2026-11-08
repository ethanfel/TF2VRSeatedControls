#pragma once
#include <array>
#include <cmath>
#include <cstring>
#include <windows.h>

// Synthetic inputs for the inspected native reload routine. No game entities,
// ammunition commands, assets or DLL initialization are needed.
struct ReloadFixture {
    std::array<unsigned char, 0x300> weapon{}, state{};
    std::array<unsigned char, 0x100> input{};
    int steps[2] = {0, 1};
    template<class T> static void Put(unsigned char* p, unsigned offset, T value) { std::memcpy(p + offset, &value, sizeof(value)); }
    static void Identity(unsigned char* p) {
        Put(p, 0, 1.0f); Put(p, 20, 1.0f); Put(p, 40, 1.0f);
    }
    explicit ReloadFixture(float distanceCm, int reserve = 30) {
        Put(state.data(), 0, weapon.data());
        Put(state.data(), 0x20, 0); Put(state.data(), 0x28, 30); Put(state.data(), 0x2c, -1);
        state[9] = 1; state[0xa] = 1;
        for (auto o : {0x30, 0x60, 0x90, 0xc0}) Identity(weapon.data() + o);
        for (auto o : {0x240, 0x250}) { Put(weapon.data(), o, steps); Put(weapon.data(), o + 8, 1); }
        weapon[0x282] = 1; // native axial alignment mode
        Put(input.data(), 0, .011f); input[4] = 1;
        Put(input.data(), 0xc, .9f); Put(input.data(), 0x10, .75f); Put(input.data(), 0x14, .25f);
        Put(input.data(), 0x18, 0); Put(input.data(), 0x1c, reserve);
        for (auto o : {0x20, 0x50, 0x80, 0xb0}) Identity(input.data() + o);
        Put(input.data(), 0x50 + 28, distanceCm / 2.54f);
    }
    void Run(unsigned char* base, const seated::Profile& p, float angleDegrees = 0) {
        auto tick = reinterpret_cast<void (*)(unsigned char*, const unsigned char*)>(base + p.reload);
        // The mod rotates a controller grip into magazine space. Establish that
        // neutral basis first; this fixture then varies only positional error.
        const auto before = state;
        const auto beforeInput = input;
        Put(input.data(), 0x50 + 28, 0.0f);
        tick(state.data(), input.data());
        const unsigned pose = p.reload == 0x52a10 ? 0x18c : 0x194;
        std::memcpy(weapon.data() + 0xc0, state.data() + pose, 48);
        state = before;
        input = beforeInput;
        const float angle = angleDegrees * 3.14159265f / 180.0f;
        Put(input.data(), 0x50 + 20, std::cos(angle)); Put(input.data(), 0x50 + 24, -std::sin(angle));
        Put(input.data(), 0x50 + 36, std::sin(angle)); Put(input.data(), 0x50 + 40, std::cos(angle));
        tick(state.data(), input.data());
    }
    bool Inserted() const { return state[8] != 0; }
    void Dump(bool beta) const {
        for (unsigned o : {0x20u,0xa4u,0xe8u,0xecu,0x148u,0x168u}) {
            unsigned index = o + (beta && o >= 0xa4 ? 8 : 0);
            float f; int v; std::memcpy(&f,state.data()+index,4); std::memcpy(&v,state.data()+index,4);
            std::printf("offset %x: %g / %d\n", index, f, v);
        }
    }
};

// DONT_RESOLVE_DLL_REFERENCES leaves imports unresolved. Supply only the pure
// CRT operations used by these geometry tests, without loading game modules.
inline void ResolveTestMath(unsigned char* base) {
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    auto imports = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    auto crt = LoadLibraryW(L"msvcrt.dll");
    for (; imports->Name; ++imports) {
        auto names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + imports->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + imports->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) continue;
            const auto name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            if (std::strcmp(name, "memmove") && std::strcmp(name, "memcpy") && std::strcmp(name, "acosf")) continue;
            auto address = GetProcAddress(crt, name);
            Check(address != nullptr, "test CRT math import resolves");
            DWORD old = 0, unused = 0;
            Check(VirtualProtect(slots, sizeof(*slots), PAGE_READWRITE, &old) != 0, "prepare test math import");
            slots->u1.Function = reinterpret_cast<ULONGLONG>(address);
            VirtualProtect(slots, sizeof(*slots), old, &unused);
        }
    }
}
