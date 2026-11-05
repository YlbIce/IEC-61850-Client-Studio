#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include <functional>
std::wstring wide(const std::string &value);
std::string utf8(const std::wstring &value);
std::wstring executableDirectory();
struct Process {
    HANDLE process = nullptr, job = nullptr, output = nullptr, input = nullptr;
    Process() = default;
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;
    ~Process();
    void start(const std::wstring &executable, const std::vector<std::string> &args);
    std::string read(std::atomic<bool> &cancel, DWORD timeout = 120000, size_t maxBytes = 128 * 1024 * 1024);
    void stream(std::atomic<bool> &cancel, const std::function<void(const char *, size_t)> &consume,
                DWORD timeout = 300000);
    void stop();
    bool running() const;
};
