/**

# LockFS flash-side interface

This contains a C++ 20 concept that you should implement your flash
interface against.

*/
#pragma once

#include <concepts>
#include <cstdint>
#include <span>

namespace LockFs
{

template<typename T>
concept Storage = requires (
        T t,
        T::FlashAddr addr,
        T::FlashAddr size,
        T::BlockSize blockSize,
        std::span<const uint8_t> src,
        std::span<uint8_t> dest,
        uint8_t tag,
        T::Checksum checksum)
{
    typename T::FlashAddr;
    typename T::BlockSize;
    typename T::Checksum;
    { t.maxBlockSize() } -> std::same_as<typename T::BlockSize>;
    { t.size() } -> std::same_as<typename T::FlashAddr>;
    // Read data from addr of size into dest (dest must be at least size
    // bytes). Returns false on failure.
    { t.flashRead(addr, dest) } -> std::same_as<bool>;
    // Write data from src into addr. Returns false on failure.
    { t.flashWrite(src, addr) } -> std::same_as<bool>;
    // Erase the given block
    { t.flashErase(addr) } -> std::same_as<bool>;
    // Lock the given maxBlockSize, the block's tag is passed in,
    // in case the implementor wants to do special type of locking
    // (e.g. permanent locking).
    { t.flashLock(addr, tag) } -> std::same_as<bool>;
    // Ensure the locks are persisting until reboot
    { t.flashLockFreeze() } -> std::same_as<bool>;

    // Compute the checksum over the given data
    { t.computeChecksum(addr, blockSize) } -> std::same_as<typename T::Checksum>;
    // Verify the given checksum is valid
    { t.verifyChecksum(addr, blockSize, checksum) } -> std::same_as<bool>;
};

};
