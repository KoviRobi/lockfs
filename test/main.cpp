#include "lockfs/lockfs.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numeric>
#include <span>

struct TestStorage
{
    using FlashAddr = uint32_t;
    using BlockSize = uint8_t;
    using Checksum = uint8_t;
    static constexpr uint8_t maxBlockSize() { return 64; }
    static constexpr FlashAddr size() { return 1024; }
    static constexpr uint8_t blocks = 64 / 1024;

    std::fstream s;
    std::bitset<blocks> locked{};
    bool frozen = false;

    bool flashRead(FlashAddr address, std::span<uint8_t> dest)
    {
        assert(dest.size() <= size());
        s.seekp(address);
        s.read(reinterpret_cast<char *>(dest.data()), dest.size());
        return true;
    }

    bool flashWrite(std::span<const uint8_t> src, FlashAddr address)
    {
        for (
            FlashAddr addr = address;
            addr < address + size();
            addr = (addr + maxBlockSize()) % size()
        )
        {
            assert(!locked[addr / maxBlockSize()]);
        }
        s.write(reinterpret_cast<const char *>(src.data()), address);
        return true;
    }

    bool flashErase(FlashAddr block)
    {
        assert(!locked[block / maxBlockSize()]);
        FlashAddr end = block + maxBlockSize();
        for (; block < end; ++block)
        {
            s.put(0xFF);
        }
        return true;
    }

    bool flashLock(FlashAddr address, uint8_t tag)
    {
        assert(!frozen);
        locked[address / maxBlockSize()] = 1;
        return true;
    }

    bool flashLockFreeze()
    {
        frozen = true;
        return true;
    }

    Checksum computeChecksum(FlashAddr addr, BlockSize blockSize)
    {
        // Note: parity bit intentionally a bad checksum for ease of fuzzing
        std::array<uint8_t, maxBlockSize()> data;
        s.read(reinterpret_cast<char *>(data.data()), blockSize);
        return std::accumulate(data.begin(), data.end(), 0) & 1;
    }

    bool verifyChecksum(FlashAddr addr, BlockSize blockSize, Checksum expected)
    {
        return computeChecksum(addr, blockSize) == expected;
    }
};

static_assert(LockFs::Storage<TestStorage>);

int main()
{
    TestStorage storage;
    std::string filename{"test.bin"};
    storage.s.open(
        filename,
        storage.s.binary | storage.s.trunc | storage.s.in | storage.s.out
    );

    if (!storage.s.is_open())
    {
        std::cerr << "failed to open " << filename << "\n";
        return 1;
    }

    // Mock file based flash with 64 byte blocks, 1KiB size
    {
        storage.s.seekp(0);
        std::array<char, storage.size()> buf;
        std::fill(buf.begin(), buf.end(), 0xFF);
        storage.s.write(buf.data(), buf.size());
    }
}
