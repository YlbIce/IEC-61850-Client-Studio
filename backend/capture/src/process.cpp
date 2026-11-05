#include "process.h"
#include <stdexcept>
#include <chrono>
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid UTF-8");
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(), n);
    return w;
}
std::string utf8(const std::wstring &w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring executableDirectory() {
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    return std::wstring(path, n).substr(0, std::wstring(path, n).find_last_of(L"\\/"));
}
static std::wstring quote(const std::wstring &s) {
    std::wstring r = L"\"";
    size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            slashes++;
            continue;
        }
        if (c == L'"')
            r.append(slashes * 2 + 1, L'\\');
        else
            r.append(slashes, L'\\');
        slashes = 0;
        r += c;
    }
    r.append(slashes * 2, L'\\');
    return r + L'"';
}
Process::~Process() {
    stop();
}
void Process::stop() {
    if (job) {
        TerminateJobObject(job, 1);
        CloseHandle(job);
        job = nullptr;
    }
    if (process) {
        WaitForSingleObject(process, 3000);
        CloseHandle(process);
        process = nullptr;
    }
    if (output) {
        CloseHandle(output);
        output = nullptr;
    }
    if (input) {
        CloseHandle(input);
        input = nullptr;
    }
}
bool Process::running() const {
    return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
}
void Process::start(const std::wstring &exe, const std::vector<std::string> &args) {
    stop();
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE write = nullptr;
    if (!CreatePipe(&output, &write, &sa, 0))
        throw std::runtime_error("CreatePipe failed");
    SetHandleInformation(output, HANDLE_FLAG_INHERIT, 0);
    HANDLE nullFile = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nullFile;
    si.hStdOutput = write;
    si.hStdError = nullFile;
    std::wstring cmd = quote(exe);
    for (auto &a : args)
        cmd += L" " + quote(wide(a));
    PROCESS_INFORMATION pi{};
    job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi);
    CloseHandle(write);
    CloseHandle(nullFile);
    if (!ok)
        throw std::runtime_error("Cannot launch protocol tool (Windows error " +
                                 std::to_string(GetLastError()) + ")");
    process = pi.hProcess;
    if (!AssignProcessToJobObject(job, process)) {
        TerminateProcess(process, 1);
        CloseHandle(pi.hThread);
        throw std::runtime_error("Cannot isolate protocol tool job");
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
}
std::string Process::read(std::atomic<bool> &cancel, DWORD timeout, size_t maxBytes) {
    std::string text;
    stream(
        cancel,
        [&](const char *bytes, size_t length) {
            if (text.size() + length > maxBytes)
                throw std::runtime_error("Analysis output limit exceeded; narrow the display filter");
            text.append(bytes, length);
        },
        timeout);
    return text;
}
void Process::stream(std::atomic<bool> &cancel, const std::function<void(const char *, size_t)> &consume,
                     DWORD timeout) {
    ULONGLONG begin = GetTickCount64();
    for (;;) {
        if (cancel) {
            stop();
            throw std::runtime_error("Operation cancelled");
        }
        if (GetTickCount64() - begin > timeout) {
            stop();
            throw std::runtime_error("Protocol tool timed out");
        }
        DWORD available = 0;
        if (PeekNamedPipe(output, nullptr, 0, nullptr, &available, nullptr) && available) {
            char b[65536];
            DWORD n = 0;
            ReadFile(output, b, std::min<DWORD>(available, sizeof(b)), &n, nullptr);
            consume(b, n);
            continue;
        }
        if (!running())
            break;
        Sleep(5);
    }
    DWORD code = 0;
    GetExitCodeProcess(process, &code);
    if (code)
        throw std::runtime_error("Protocol tool rejected input/filter or failed (exit " +
                                 std::to_string(code) + ")");
}
