#pragma once
#include <windows.h>
#include <string>

namespace seated {
std::wstring ModuleDirectory(const void* address);
void Log(const char* format, ...);
std::string FileSha256(const std::wstring& path);

class GameVar {
public:
    explicit GameVar(const char* name) : name_(name) {}
    bool Read(int& value);
private:
    const char* name_;
    void* value_ = nullptr;
    ULONGLONG retry_ = 0;
};
}
