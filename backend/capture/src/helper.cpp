#include <windows.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
using json = nlohmann::json;
int main() {
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    wchar_t path[32768];
    GetModuleFileNameW(nullptr, path, 32768);
    std::wstring dir = path;
    dir = dir.substr(0, dir.find_last_of(L"\\/"));
    HMODULE dll = LoadLibraryExW((dir + L"\\iec61850-capture.dll").c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    auto version =
        dll ? reinterpret_cast<unsigned (*)()>(GetProcAddress(dll, "capture_abi_version")) : nullptr;
    auto create = dll ? reinterpret_cast<void *(*)()>(GetProcAddress(dll, "capture_create")) : nullptr;
    auto request =
        dll ? reinterpret_cast<const char *(*)(void *, const char *)>(GetProcAddress(dll, "capture_request"))
            : nullptr;
    auto cancel = dll ? reinterpret_cast<void (*)(void *)>(GetProcAddress(dll, "capture_cancel")) : nullptr;
    auto destroy = dll ? reinterpret_cast<void (*)(void *)>(GetProcAddress(dll, "capture_destroy")) : nullptr;
    void *context =
        version && version() >> 16 == 1 && create && request && cancel && destroy ? create() : nullptr;
    std::mutex mutex, output;
    std::condition_variable changed;
    std::deque<std::string> queue;
    bool done = false;
    auto send = [&](const std::string &line) {
        std::lock_guard<std::mutex> lock(output);
        std::cout << line << std::endl;
    };
    std::thread worker([&] {
        for (;;) {
            std::string line;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return done || !queue.empty(); });
                if (done)
                    break;
                line = std::move(queue.front());
                queue.pop_front();
            }
            try {
                auto q = json::parse(line);
                if (context)
                    send(request(context, line.c_str()));
                else if (q.value("command", "") == "capabilities")
                    send(json({{"id", q.value("id", json())},
                               {"result",
                                {{"available", false},
                                 {"analysis", false},
                                 {"capture", false},
                                 {"reason", "Optional iec61850-capture.dll is missing, incompatible, or "
                                            "could not load"}}}})
                             .dump());
                else
                    send(json({{"id", q.value("id", json())},
                               {"error",
                                {{"code", "UNAVAILABLE"},
                                 {"message", "Optional capture library unavailable"}}}})
                             .dump());
            } catch (const std::exception &e) {
                send(json({{"error", {{"code", "INVALID_REQUEST"}, {"message", e.what()}}}}).dump());
            }
        }
    });
    std::string line;
    while (std::getline(std::cin, line)) {
        json requestId;
        if (line.size() > 1024 * 1024) {
            send("{\"error\":{\"code\":\"REQUEST_TOO_LARGE\",\"message\":\"Request exceeds 1 MiB\"}}");
            continue;
        }
        try {
            auto q = json::parse(line);
            requestId = q.value("id", json());
            if (q.value("command", "") == "cancel") {
                if (context)
                    cancel(context);
                send(json({{"id", q.value("id", json())}, {"result", {{"cancelled", true}}}}).dump());
                continue;
            }
        } catch (...) {
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (queue.size() >= 32) {
                send(json({{"id", requestId},
                           {"error", {{"code", "BUSY"}, {"message", "Too many pending operations"}}}})
                         .dump());
                continue;
            }
            queue.push_back(line);
        }
        changed.notify_one();
    }
    if (context)
        cancel(context);
    {
        std::lock_guard<std::mutex> lock(mutex);
        done = true;
    }
    changed.notify_one();
    worker.join();
    if (context)
        destroy(context);
    if (dll)
        FreeLibrary(dll);
    return 0;
}
