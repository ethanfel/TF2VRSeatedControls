#include "../src/plugin/Northstar.h"
#include <cstdio>
#include <cstring>

using namespace tf2vrsc::northstar;

namespace
{
unsigned logCount = 0;
int lastLevel = -1;
char lastMessage[2048] = {};

class HostSys : public ISys
{
public:
    void Log(int64_t, LogLevel level, char* message) override
    {
        ++logCount;
        lastLevel = static_cast<int>(level);
        std::snprintf(lastMessage, sizeof(lastMessage), "%s", message);
    }
    void Unload(int64_t) override {}
    void Reload(int64_t) override {}
};
HostSys sys;
}

// This must be a separate DLL: the production plugin cannot see its ISys
// implementation while compiling/optimizing, just as with real Northstar.
extern "C" __declspec(dllexport) void* CreateInterface(const char* name, int* status)
{
    void* result = name && std::strcmp(name, "NSSys001") == 0 ? static_cast<ISys*>(&sys) : nullptr;
    if (status) *status = result ? 0 : 1;
    return result;
}
extern "C" __declspec(dllexport) unsigned GetLogCount() { return logCount; }
extern "C" __declspec(dllexport) int GetLastLevel() { return lastLevel; }
extern "C" __declspec(dllexport) const char* GetLastMessage() { return lastMessage; }
extern "C" __declspec(dllexport) void ResetLogs()
{
    logCount = 0;
    lastLevel = -1;
    lastMessage[0] = '\0';
}
