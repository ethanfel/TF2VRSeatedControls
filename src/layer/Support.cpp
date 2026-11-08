#include "Support.h"
#include <bcrypt.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace seated {
std::wstring ModuleDirectory(const void* address) {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(address), &module);
    wchar_t path[32768] = {};
    const DWORD size = GetModuleFileNameW(module, path, 32768);
    if (!size || size == 32768) return {};
    std::wstring result(path, size);
    return result.substr(0, result.find_last_of(L"\\/"));
}

void Log(const char* format, ...) {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    const auto path = ModuleDirectory(reinterpret_cast<const void*>(&Log)) + L"\\TF2VRSeatedControls.log";
    FILE* file = _wfopen(path.c_str(), L"a");
    if (!file) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(file, "[%02u:%02u:%02u] ", t.wHour, t.wMinute, t.wSecond);
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fclose(file);
}

std::string FileSha256(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return {};
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 65536> buffer{};
    DWORD read = 0;
    while (ok) {
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) { ok = false; break; }
        if (!read) break;
        ok = BCryptHashData(hash, buffer.data(), read, 0) >= 0;
    }
    std::array<unsigned char, 32> digest{};
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(file);
    if (!ok) return {};
    std::string result;
    for (auto c : digest) {
        constexpr char hex[] = "0123456789abcdef";
        result += hex[c >> 4]; result += hex[c & 15];
    }
    return result;
}

namespace {
template<class T> bool ReadMemory(const void* source, T& dest) {
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), source, &dest, sizeof(T), &read) && read == sizeof(T);
}
}

bool GameVar::Read(int& value) {
    if (!value_) {
        const auto now = GetTickCount64();
        if (now < retry_) return false;
        retry_ = now + 1000;
        HMODULE module = GetModuleHandleW(L"vstdlib.dll");
        if (!module) return false;
        using Factory = void* (*)(const char*, int*);
        auto factory = reinterpret_cast<Factory>(GetProcAddress(module, "CreateInterface"));
        if (!factory) return false;
        void* cvar = factory("VEngineCvar007", nullptr);
        if (!cvar) return false;
        void** table = nullptr;
        if (!ReadMemory(cvar, table)) return false;
        using FindVar = void* (*)(void*, const char*);
        FindVar find = nullptr;
        if (!ReadMemory(table + 16, find) || !find) return false;
        auto var = static_cast<unsigned char*>(find(cvar, name_));
        if (!var) return false;
        const char* registered = nullptr;
        if (!ReadMemory(var + 0x18, registered) || !registered) return false;
        for (size_t i = 0; i <= std::strlen(name_); ++i) {
            char c = 0;
            if (!ReadMemory(registered + i, c) || c != name_[i]) return false;
        }
        value_ = var + 0x5c;
    }
    if (ReadMemory(value_, value)) return true;
    value_ = nullptr;
    return false;
}
}
