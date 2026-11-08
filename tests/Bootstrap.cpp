#include "../src/plugin/Northstar.h"
#include "../src/plugin/Registration.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace tf2vrsc::northstar;
namespace fs = std::filesystem;
namespace
{
int checks = 0;
constexpr auto layerKey = L"Software\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit";
constexpr auto manifestName = L"XR_APILAYER_TF2VR_seated_controls.json";

void Check(bool condition, const char* message)
{
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

// Never register the test layer for the real user. The override is process
// local and also applies to registry calls made by the loaded plugin DLL.
struct Fixture
{
    HMODULE host = nullptr, plugin = nullptr;
    HKEY root = nullptr, user = nullptr, machine = nullptr, layers = nullptr;
    bool overridden = false, machineOverridden = false;
    std::wstring key;
    fs::path directory;
    ~Fixture()
    {
        if (plugin) FreeLibrary(plugin);
        if (host) FreeLibrary(host);
        if (layers) RegCloseKey(layers);
        if (machineOverridden) RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr);
        if (overridden) RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
        if (machine) RegCloseKey(machine);
        if (user) RegCloseKey(user);
        if (root) RegCloseKey(root);
        if (!key.empty()) RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
        std::error_code ignored;
        if (!directory.empty()) fs::remove_all(directory, ignored);
    }
};

LONG WINAPI Crash(EXCEPTION_POINTERS* exception)
{
    std::fprintf(stderr, "FAIL: bootstrap raised exception 0x%08lx at %p\n",
        exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress);
    std::fflush(stderr);
    ExitProcess(90);
}

template<typename T> T Export(HMODULE module, const char* name)
{
    auto result = reinterpret_cast<T>(GetProcAddress(module, name));
    Check(result != nullptr, name);
    return result;
}
}

int main(int argc, char** argv)
{
    if (argc != 3) { std::fprintf(stderr, "Usage: bootstrap_tests <host.dll> <plugin.dll>\n"); return 2; }
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    SetUnhandledExceptionFilter(Crash);
    try
    {
        Fixture test;
        auto suffix = std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        test.directory = fs::temp_directory_path() / (L"TF2VRSC-bootstrap-" + suffix);
        fs::create_directories(test.directory / "layer");
        auto pluginPath = test.directory / "TF2VRSeatedControls.dll";
        auto manifest = test.directory / "layer" / manifestName;
        fs::copy_file(fs::u8path(argv[2]), pluginPath);
        std::ofstream(manifest) << "{}";
        test.key = L"Software\\TF2VRSC-Bootstrap-Test-" + suffix;
        Check(RegCreateKeyExW(HKEY_CURRENT_USER, test.key.c_str(), 0, nullptr, 0,
            KEY_ALL_ACCESS, nullptr, &test.root, nullptr) == ERROR_SUCCESS, "isolated registry root");
        Check(RegCreateKeyExW(test.root, L"User", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &test.user, nullptr) == ERROR_SUCCESS,
            "isolated user hive");
        Check(RegCreateKeyExW(test.root, L"Machine", 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &test.machine, nullptr) == ERROR_SUCCESS,
            "isolated machine hive");
        Check(RegOverridePredefKey(HKEY_CURRENT_USER, test.user) == ERROR_SUCCESS, "isolate plugin user registry writes");
        test.overridden = true;
        Check(RegOverridePredefKey(HKEY_LOCAL_MACHINE, test.machine) == ERROR_SUCCESS, "isolate plugin machine registry writes");
        test.machineOverridden = true;
        test.host = LoadLibraryW(fs::u8path(argv[1]).c_str());
        test.plugin = LoadLibraryW(pluginPath.c_str());
        Check(test.host && test.plugin, "load host and actual Release plugin DLLs");
        auto count = Export<unsigned (*)()>(test.host, "GetLogCount");
        auto level = Export<int (*)()>(test.host, "GetLastLevel");
        auto message = Export<const char* (*)()>(test.host, "GetLastMessage");
        auto reset = Export<void (*)()>(test.host, "ResetLogs");
        auto factory = Export<CreateInterfaceFn>(test.plugin, "CreateInterface");
        int status = -1;
        auto id = static_cast<IPluginId*>(factory("PluginId001", &status));
        Check(id && status == 0, "PluginId001");
        Check(std::strcmp(id->GetString(PluginString::LogName), "TF2VRSC") == 0, "log prefix across DLL boundary");
        Check(id->GetField(PluginField::Context) == 2, "client context across DLL boundary");
        auto callbacks = static_cast<IPluginCallbacks*>(factory("PluginCallbacks001", &status));
        Check(callbacks && status == 0, "PluginCallbacks001");
        Check(factory("Unknown", &status) == nullptr && status == 1, "unknown interface rejected");
        Check(factory(nullptr, &status) == nullptr && status == 1, "null interface rejected");
        PluginNorthstarData data{test.plugin, sizeof(PluginNorthstarData)};
        auto init = [&] { reset(); callbacks->Init(test.host, &data, false); };
        init();
        const bool wine = tf2vrsc::IsWine();
        const char* registrationText = wine ? "registered in this Wine/Proton prefix (HKLM)" : "registered for this Windows user";
        if (count() != 1 || level() != 1 || !std::strstr(message(), registrationText))
            std::fprintf(stderr, "First startup: count=%u level=%d message=%s\n", count(), level(), message());
        Check(count() == 1 && level() == 1 && std::strstr(message(), registrationText),
            "first startup reaches external logger and reports registration");
        Check(RegOpenKeyExW(wine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, layerKey, 0, KEY_ALL_ACCESS, &test.layers) == ERROR_SUCCESS,
            "implicit layer registered in the hive the loader can read");
        auto read = [&](const wchar_t* name, DWORD& value) {
            DWORD type = 0, size = sizeof(value);
            auto result = RegQueryValueExW(test.layers, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
            return result == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(value);
        };
        DWORD value = 99;
        Check(read(manifest.c_str(), value) && value == 0, "correct manifest registered and enabled");
        init();
        Check(count() == 1 && level() == 1 && std::strstr(message(), "did not load this session"),
            "repeat startup logs already registered state");
        DWORD disabled = 1;
        Check(RegSetValueExW(test.layers, manifest.c_str(), 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&disabled), sizeof(disabled)) == ERROR_SUCCESS, "disable test registration");
        init();
        Check(count() == 1 && level() == 0 && std::strstr(message(), "leaving it disabled"), "disabled startup logs safely");
        Check(read(manifest.c_str(), value) && value == 1, "disabled state preserved");
        auto stale = std::wstring(L"C:\\old-copy\\") + manifestName;
        Check(RegSetValueExW(test.layers, stale.c_str(), 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&disabled), sizeof(disabled)) == ERROR_SUCCESS, "seed stale copy");
        Check(RegSetValueExW(test.layers, L"unrelated.json", 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&disabled), sizeof(disabled)) == ERROR_SUCCESS, "seed unrelated layer");
        init();
        Check(count() == 2, "stale-copy and status messages both reach host logger");
        Check(!read(stale.c_str(), value), "stale own registration removed");
        Check(read(L"unrelated.json", value) && value == 1, "unrelated registration preserved");

        // Exercise both platform policies regardless of the host test OS.
        auto readHive = [&](HKEY hive, const wchar_t* name, DWORD& output) {
            DWORD type = 0, size = sizeof(output);
            return RegGetValueW(hive, layerKey, name, RRF_RT_REG_DWORD, &type, &output, &size) == ERROR_SUCCESS;
        };
        tf2vrsc::UnregisterImplicitLayer(HKEY_CURRENT_USER, layerKey, manifestName);
        tf2vrsc::UnregisterImplicitLayer(HKEY_LOCAL_MACHINE, layerKey, manifestName);
        auto registered = tf2vrsc::RegisterForEnvironment(manifest.wstring(), false);
        Check(registered.result == tf2vrsc::RegistrationResult::Registered, "native Windows registration succeeds");
        Check(readHive(HKEY_CURRENT_USER, manifest.c_str(), value) && value == 0, "native Windows uses HKCU");
        Check(!readHive(HKEY_LOCAL_MACHINE, manifest.c_str(), value), "native Windows never creates HKLM registration");
        HKEY legacy = nullptr;
        Check(RegOpenKeyExW(HKEY_CURRENT_USER, layerKey, 0, KEY_SET_VALUE, &legacy) == ERROR_SUCCESS, "open legacy user key");
        auto disabledStatus = RegSetValueExW(legacy, manifest.c_str(), 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&disabled), sizeof(disabled));
        DWORD enabled = 0;
        auto unrelatedStatus = RegSetValueExW(legacy, L"unrelated-user.json", 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&enabled), sizeof(enabled));
        RegCloseKey(legacy);
        Check(disabledStatus == ERROR_SUCCESS && unrelatedStatus == ERROR_SUCCESS, "seed legacy disabled state and unrelated layer");
        if (wine) init(); // Exercise the actual DLL's migration on Wine as well.
        else tf2vrsc::RegisterForEnvironment(manifest.wstring(), true);
        Check(readHive(HKEY_LOCAL_MACHINE, manifest.c_str(), value) && value == 1, "Wine migration preserves explicit disable");
        Check(!readHive(HKEY_CURRENT_USER, manifest.c_str(), value), "Wine migration removes duplicate own HKCU entry");
        Check(readHive(HKEY_CURRENT_USER, L"unrelated-user.json", value) && value == 0, "migration preserves unrelated HKCU entries");
        tf2vrsc::RegisterForEnvironment(manifest.wstring(), false);
        registered = tf2vrsc::RegisterForEnvironment(manifest.wstring(), true);
        Check(registered.result == tf2vrsc::RegistrationResult::DisabledByUser &&
            readHive(HKEY_LOCAL_MACHINE, manifest.c_str(), value) && value == 1,
            "existing disabled HKLM value is never reenabled by stale HKCU state");
        fs::remove(manifest);
        init();
        Check(count() == 1 && level() == 2 && std::strstr(message(), "manifest missing"), "missing manifest error logs safely");
        callbacks->Finalize();
        callbacks->OnSqvmCreated(nullptr);
        callbacks->OnSqvmDestroying(nullptr);
        callbacks->OnLibraryLoaded(test.host, "bootstrap_host.dll");
        callbacks->RunFrame();
        Check(callbacks->Unload(), "remaining callback slots and unload are callable");
        std::printf("PASS: %d bootstrap checks using actual plugin and a separate host DLL\n", checks);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
    return 0;
}
