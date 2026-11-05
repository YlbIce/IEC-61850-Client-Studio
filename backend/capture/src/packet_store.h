#pragma once
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace capture {
// Append-only JSONL storage. Only a 16-byte offset/length index stays in memory.
// Owns a private temporary file that Windows removes even if the helper is killed.
// Operations are serialized; destruction requires callers to have stopped using the store.
class PacketStore final {
public:
    using json = nlohmann::ordered_json;
    explicit PacketStore(std::uint64_t maxBytes = 8ull * 1024 * 1024 * 1024,
                         std::uint32_t maxRecordBytes = 32u * 1024 * 1024);
    ~PacketStore();
    PacketStore(const PacketStore&) = delete;
    PacketStore& operator=(const PacketStore&) = delete;
    PacketStore(PacketStore&&) = delete;
    PacketStore& operator=(PacketStore&&) = delete;

    void append(const json& record);
    std::size_t size() const;
    json at(std::size_t index) const;
    std::uint64_t bytes() const;
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
