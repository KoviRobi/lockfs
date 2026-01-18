#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>

namespace LockFs
{

using FlashAddr = uint32_t;

constexpr unsigned long long operator""_KiB(unsigned long long n) { return n * 1024; }
constexpr unsigned long long operator""_Mib(unsigned long long n) { return n * ( 1024 *1024 / 8); }

// TODO: Parametrize these?
constexpr size_t maxBlockSize = 64_KiB;
constexpr size_t programOffset = 0x1'0000;
constexpr uintptr_t start = 0x6000'0000;
constexpr size_t size = 256_Mib;
constexpr uint8_t erasedValue = 0xFF;
static inline bool flashRead(FlashAddr address, uint32_t size, std::span<uint8_t> dest)
{
    const auto begin = reinterpret_cast<const uint8_t *>(address);
    const auto end = reinterpret_cast<const uint8_t *>(address + size);
    std::copy(begin + start, end + start, std::begin(dest));
    return true;
}
static inline bool flashWrite(std::span<const uint8_t> src, FlashAddr address)
{
    (void)src;
    (void)address;
    return true;
}
static inline bool flashLock(FlashAddr address, bool volatile_)
{
    // WRITE LOCK BITS 0b01
    // Volatile (0xE5)/non-volatile (0xE3)?
    // For first/last sectors, sub-sector volatilee
    (void)address;
    (void)volatile_;
    return true;
}
static inline bool flashLockFreeze()
{
    // WRITE GLOBAL FREEZE=0 (0xA6)
    return true;
}
constexpr uint8_t maxNonvolatileTag = 0;

// Serialised
struct Header
{
    // Checksum/hash over the block, used to detect bad blocks
    uint64_t checksum;
    // size of the 64KiB block (after header), not user specified
    uint16_t blockSize;
    // User specified (not erasedValue)
    uint8_t tag;
    // Flag (erased, continuation), not user specified
    // TODO: Maybe can get rid of erased/continuation because blockSize
    // is at most maxBlockSize - headerSize
    uint8_t flags;
    // Counter when uploading a newer version, not user specified
    uint8_t revision;

    // Bits for flags
    static constexpr uint8_t ERASED_BIT = 0x80;
    static constexpr uint8_t CONTINUATION_BIT = 0x40;

    // Returns {} on failure to read
    static std::optional<Header> read(const FlashAddr address);
    // Returns false on failure to write
    bool write(FlashAddr address) const;

    constexpr bool erased() const
    {
        return flags & ERASED_BIT;
    }

    constexpr bool continuation() const
    {
        return flags & CONTINUATION_BIT;
    }

    constexpr bool newerThan(const Header & other) const
    {
        int8_t distance = revision - other.revision;
        return distance > 0;
    }
};
// Not serialised
struct RamHeader
{
    Header current;
    FlashAddr startBlock;
    FlashAddr currentBlock;
    uint32_t size;
};

struct Context
{
    std::span<RamHeader> headers;
    FlashAddr nextFreeBlock = NO_FREE;

    static constexpr FlashAddr NO_FREE = static_cast<FlashAddr>(~0);

    decltype(headers[0]) operator[](size_t i) const { return headers[i]; }
    decltype(headers.begin()) begin() const { return headers.begin(); }
    decltype(headers.end()) end() const { return headers.end(); }
};

// Fills the headers (indexed by tag) and returns true if successful
bool loadAll(Context headers);

// Note: only one write in progress at a time for now.
// TODO: Preallocate? E.g. writing tags
std::optional<RamHeader> startWrite(Context headers, uint8_t tag, uint32_t size);
bool write(RamHeader & header, std::span<uint8_t> data);
bool finishWrite(RamHeader & header);

};
