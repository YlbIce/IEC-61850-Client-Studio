#include "packet_store.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace capture {
namespace {
std::runtime_error failure(const char* operation) {
    return std::runtime_error(std::string("Packet index ") + operation + " failed (Windows " +
                              std::to_string(GetLastError()) + ")");
}

HANDLE temporaryFile() {
    wchar_t directory[32768]{};
    DWORD length = GetTempPathW(static_cast<DWORD>(std::size(directory)), directory);
    if (!length || length >= std::size(directory)) throw failure("temporary directory lookup");
    static std::atomic<std::uint64_t> sequence{0};
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        const auto path = std::wstring(directory) + L"iec61850-capture-" + std::to_wstring(GetCurrentProcessId()) +
                          L"-" + std::to_wstring(counter.QuadPart) + L"-" +
                          std::to_wstring(sequence.fetch_add(1)) + L".jsonl";
        // CREATE_NEW prevents replacing another file. No sharing; all access uses this handle.
        HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_RANDOM_ACCESS,
                                    nullptr);
        if (handle != INVALID_HANDLE_VALUE) return handle;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
            throw failure("temporary file creation");
    }
    throw std::runtime_error("Cannot allocate a unique packet index file");
}

void seek(HANDLE handle, std::uint64_t offset) {
    LARGE_INTEGER position;
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(handle, position, nullptr, FILE_BEGIN)) throw failure("seek");
}

void write(HANDLE handle, const char* data, std::size_t length) {
    while (length) {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(length, 1024 * 1024));
        if (!WriteFile(handle, data, chunk, &written, nullptr)) throw failure("write");
        if (!written) throw std::runtime_error("Packet index write made no progress");
        length -= written; data += written;
    }
}

void read(HANDLE handle, char* data, std::size_t length) {
    while (length) {
        DWORD count = 0;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(length, 1024 * 1024));
        if (!ReadFile(handle, data, chunk, &count, nullptr)) throw failure("read");
        if (!count) throw std::runtime_error("Packet index is truncated");
        length -= count; data += count;
    }
}
}

struct PacketStore::Impl {
    struct Entry { std::uint64_t offset; std::uint32_t length; };
    mutable std::mutex mutex;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::vector<Entry> index;
    std::uint64_t end = 0, maximum;
    std::uint32_t maximumRecord;

    Impl(std::uint64_t maxBytes, std::uint32_t maxRecordBytes)
        : maximum(maxBytes), maximumRecord(maxRecordBytes) {
        if (!maximumRecord || maximum < maximumRecord || maximum > static_cast<std::uint64_t>(INT64_MAX))
            throw std::invalid_argument("Invalid packet index storage limits");
    }
    ~Impl() { if (file != INVALID_HANDLE_VALUE) CloseHandle(file); }
};

PacketStore::PacketStore(std::uint64_t maxBytes, std::uint32_t maxRecordBytes)
    : impl(std::make_unique<Impl>(maxBytes, maxRecordBytes)) {}
PacketStore::~PacketStore() = default;

void PacketStore::append(const json& record) {
    std::string line = record.dump();
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (line.size() > impl->maximumRecord) throw std::runtime_error("A decoded packet exceeds the " + std::to_string(impl->maximumRecord) + " byte record limit");
    if (line.size() + 1 > impl->maximum - impl->end)
        throw std::runtime_error("Packet analysis exceeded its temporary disk budget");
    if (impl->file == INVALID_HANDLE_VALUE) impl->file = temporaryFile();
    seek(impl->file, impl->end);
    // Publish the index only after all bytes are written. A partial tail is never visible.
    write(impl->file, line.data(), line.size());
    write(impl->file, "\n", 1);
    impl->index.push_back({impl->end, static_cast<std::uint32_t>(line.size())});
    impl->end += line.size() + 1;
}

std::size_t PacketStore::size() const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->index.size();
}

PacketStore::json PacketStore::at(std::size_t index) const {
    std::string record;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (index >= impl->index.size()) throw std::out_of_range("Packet index is out of range");
        const auto entry = impl->index[index];
        record.resize(entry.length);
        seek(impl->file, entry.offset);
        read(impl->file, record.data(), record.size());
    }
    return json::parse(record);
}

std::uint64_t PacketStore::bytes() const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->end;
}

void PacketStore::clear() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (impl->file != INVALID_HANDLE_VALUE) CloseHandle(impl->file);
    impl->file = INVALID_HANDLE_VALUE;
    std::vector<Impl::Entry>().swap(impl->index);
    impl->end = 0;
}
}
