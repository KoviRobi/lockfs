#pragma once

#include "lockfs/flash_interface.hpp"
#include "lockfs/lockfs.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

using Addr = uint32_t;

constexpr uint8_t maxBlockSize_ = 8;
constexpr Addr size_ = 64;
constexpr Addr blocks_ = size_ / maxBlockSize_;

// Has a timeout and simulates power-off after that many steps
struct TimeoutStorage
{
    using FlashAddr = Addr;
    using BlockSize = uint8_t;
    using Checksum = uint8_t;
    static constexpr uint8_t maxBlockSize() { return ::maxBlockSize_; }
    static constexpr FlashAddr size() { return ::size_; }

    Checksum (*computeChecksum)(FlashAddr addr, BlockSize size);
    bool (*verifyChecksum)(FlashAddr addr, BlockSize size, Checksum);
    size_t timeout;
    uint8_t backing[::size_];
    bool locked[::blocks_];
    bool frozen = false;

    bool flashRead(FlashAddr address, std::span<uint8_t> dest);
    bool flashWrite(std::span<const uint8_t> src, FlashAddr address);
    bool flashErase(FlashAddr block);
    bool flashLock(FlashAddr address, uint8_t tag);
    bool flashLockFreeze();
};

static_assert(LockFs::Storage<TimeoutStorage>);

using Addr = TimeoutStorage::FlashAddr;
using Fs = LockFs::LockFs<TimeoutStorage>;

extern "C"
{
    bool flashRead(TimeoutStorage * ts, Addr src, uint8_t * dest, size_t dlen);
    bool flashWrite(TimeoutStorage * ts, const uint8_t * src, size_t slen, Addr dest);
    bool flashErase(TimeoutStorage * ts, Addr block);
    bool flashLock(TimeoutStorage * ts, Addr block, uint8_t tag);
    bool flashLockFreeze(TimeoutStorage * ts);

    // These functions allow us to verify the Python structures are as expected
    int dumpTS(const TimeoutStorage * ts, char * buf, size_t len, const char * prefix);
    int dumpH(const Fs::Header * h, char * buf, size_t len, const char * prefix);
    int dumpRH(const Fs::RamHeader * rh, char * buf, size_t len, const char * prefix);
    int dumpFS(const Fs * fs, char * buf, size_t len, const char * prefix);

    Fs::Context * context(Fs::RamHeader * buf, size_t size);
    Fs * create(TimeoutStorage * ts);
    bool loadAll(Fs * fs, Fs::Context * ctx);
    bool startWrite(Fs * fs, Fs::Context ctx, uint8_t tag, Addr size, Fs::RamHeader * out);
    bool write(Fs * fs, Fs::RamHeader * rh, const uint8_t * src, size_t len);
    bool finishWrite(Fs * fs, Fs::RamHeader * rh);
};
