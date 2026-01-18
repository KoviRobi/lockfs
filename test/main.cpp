#include "lockfs/lockfs.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>

struct TestStorage
{
    using FlashAddr = uint32_t;
    static constexpr size_t maxBlockSize = 64;
    static constexpr size_t size = 1024;
    static constexpr uint8_t erasedValue = 0xFF;

    std::fstream s;
    std::bitset<size / maxBlockSize> locked{};
    bool frozen = false;

    bool flashRead(FlashAddr address, uint32_t size, std::span<uint8_t> dest)
    {
        assert(size <= dest.size());
        s.seekp(address);
        s.read(reinterpret_cast<char *>(dest.data()), size);
        return true;
    }

    bool flashWrite(std::span<const uint8_t> src, FlashAddr address)
    {
        for (
            FlashAddr addr = address;
            addr < address + size;
            addr = (addr + maxBlockSize) % size
        )
        {
            assert(!locked[addr / maxBlockSize]);
        }
        s.write(reinterpret_cast<const char *>(src.data()), address);
        return true;
    }

    bool flashLock(FlashAddr address, bool volatile_)
    {
        assert(!frozen);
        locked[address / maxBlockSize] = 1;
        return true;
    }

    bool flashLockFreeze()
    {
        frozen = true;
        return true;
    }
};

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
        std::array<char, storage.size> buf;
        std::fill(buf.begin(), buf.end(), storage.erasedValue);
        storage.s.write(buf.data(), buf.size());
    }
}
