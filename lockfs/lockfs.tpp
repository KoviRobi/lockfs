#include "lockfs.hpp"

#include "endian.hpp"
#include "flash_interface.hpp"

#include <cassert>
#include <cstdint>

#include <optional>

using namespace Serialisation;

template<LockFs::Storage Storage>
std::optional<typename LockFs::LockFs<Storage>::Header>
LockFs::LockFs<Storage>::Header::read(Storage & s, const FlashAddr address)
{
    uint8_t buf[sizeof(Header)];
    if (s.flashRead(address, buf))
    {
        size_t i = 0;
        const auto tag       = EL<uint8_t>::load(&buf[i]);
        const auto flags     = EL<uint8_t>::load(&buf[i += sizeof(tag)]);
        const auto revision  = EL<uint8_t>::load(&buf[i += sizeof(flags)]);
        const auto blockSize = EL<BlockSize>::load(&buf[i += sizeof(revision)]);
        const auto checksum  = EL<Checksum>::load(&buf[i += sizeof(blockSize)]);
        return Header{
            .checksum = checksum,
            .blockSize = blockSize,
            .tag = tag,
            .flags = flags,
            .revision = revision
        };
    }
    return std::optional<Header>{};
}

template<LockFs::Storage Storage>
bool LockFs::LockFs<Storage>::Header::write(Storage & s, FlashAddr address) const
{
    uint8_t buf[Header::size];
    size_t i = 0;
    EL<uint8_t>::store(&buf[i], tag);
    EL<uint8_t>::store(&buf[i += sizeof(tag)], flags);
    EL<uint8_t>::store(&buf[i += sizeof(flags)], revision);
    EL<uint16_t>::store(&buf[i += sizeof(revision)], blockSize);
    EL<Checksum>::store(&buf[i += sizeof(blockSize)], checksum);
    return s.flashWrite(buf, address);
}

template<LockFs::Storage Storage>
bool LockFs::LockFs<Storage>::loadAll(LockFs::Context & context)
{
    std::optional<FlashAddr> freeBlockRunStart{};
    for (RamHeader & rh : context.headers)
    {
        rh.current.flags = Header::ERASED_BIT;
    }
    // Read
    for (FlashAddr i = 0; i < (s->size() / s->maxBlockSize()); ++i)
    {
        const FlashAddr addr = i * s->maxBlockSize();
        const auto hdr = Header::read(*s, addr);
        if (!hdr.has_value())
        {
            // TODO: Bad blocks? Or just out of bounds
            return false;
        }
        if (hdr->erased())
        {
            // By using the last free block, we make it more likely
            // that we cycle through the flash rather than just swapping
            // betweek two values
            freeBlockRunStart = freeBlockRunStart.value_or(addr);
        }
        else if (hdr->tag < context.headers.size())
        {
            // End of run
            if (freeBlockRunStart.has_value())
            {
                context.nextFreeBlock = freeBlockRunStart;
                freeBlockRunStart.reset();
            }

            const auto tag = hdr->tag;
            if (hdr->continuation())
            {
                context.headers[tag].size += hdr->blockSize;
            }
            else
            {
                if (context.headers[tag].current.erased() ||
                    hdr->newerThan(context.headers[tag].current))
                {
                    context.headers[tag].current = *hdr;
                    context.headers[tag].startBlock = addr;
                    context.headers[tag].currentBlock = addr;
                    context.headers[tag].size = hdr->blockSize;
                }
            }
        }
        // TODO: Unfinished blocks? Reduce revision in context.headers[tag]?
    }
    if (freeBlockRunStart.has_value())
    {
        context.nextFreeBlock = freeBlockRunStart;
        freeBlockRunStart.reset();
    }
    // Lock
    for (FlashAddr i = 0; i < (s->size() / s->maxBlockSize()); ++i)
    {
        const FlashAddr addr = i * s->maxBlockSize();
        const auto hdr = Header::read(*s, addr);
        if (
            !hdr->erased() &&
            hdr->tag == context.headers[i].current.tag &&
            hdr->revision == context.headers[i].current.revision
        )
        {
            if (!s->flashLock(addr, hdr->tag))
            {
                return false;
            }
        }
    }
    return s->flashLockFreeze();
}

template<LockFs::Storage Storage>
std::optional<typename LockFs::LockFs<Storage>::RamHeader>
LockFs::LockFs<Storage>::startWrite(LockFs::Context context, uint8_t tag, FlashAddr size)
{
    // To write (reserving blocks so that multiple writes can be in
    // progress):
    // - (checksum in write)
    // - (blockSize in write)
    // - tag
    // - revision
    // - (flags in finishWrite)
    const uint8_t revision = context.headers[tag].current.erased() ? 0 :
        (context.headers[tag].current.tag + 1);
    // Out of space
    if (!context.nextFreeBlock.has_value())
    {
        return {};
    }
    RamHeader header{
        .current = {
            .checksum  = init<decltype(Header::checksum)>(0xFF),
            .blockSize = init<decltype(Header::blockSize)>(0xFF),
            .tag       = tag,
            .flags     = init<decltype(Header::flags)>(0xFF),
            .revision  = revision,
        },
        .startBlock = context.nextFreeBlock.value(),
        .currentBlock = context.nextFreeBlock.value(),
        .size = size,
    };
    // Reserve blocks
    while (header.size > 0)
    {
        const auto maybe = Header::read(*s, header.currentBlock);
        if (maybe.has_value() && maybe->erased())
        {
            header.current.write(*s, header.currentBlock);
            const FlashAddr dataSize = s->maxBlockSize() - Header::size;
            header.size -= std::min<FlashAddr>(dataSize, header.size);
        }
        header.currentBlock = (header.currentBlock + s->maxBlockSize()) % s->size();
        // Out of space
        if (header.currentBlock == header.startBlock)
        {
            return {};
        }
    }
    header.size = size;
    header.currentBlock = header.startBlock;
    header.current.blockSize = 0;
    context.headers[tag] = header;
    return header;
}

template<LockFs::Storage Storage>
bool LockFs::LockFs<Storage>::write(LockFs::RamHeader & header, std::span<const uint8_t> data)
{
    while (data.size() > 0)
    {
        if (header.current.blockSize < s->maxBlockSize() - Header::size)
        {
            // Add data to current block, if it becomes full, write header
            // Note: Header::blockSize doesn't count header size so we
            // add it in
            const auto begin = header.current.blockSize + Header::size;
            const size_t blockRemaining = s->maxBlockSize() - begin;
            const size_t toWrite = std::min(data.size(), blockRemaining);
            const auto end = begin + toWrite;
            header.current.blockSize += decltype(Header::blockSize)(toWrite);
            // Split data
            std::span src = data.first(toWrite);
            data = data.subspan(toWrite);
            if (!s->flashWrite(src, header.currentBlock + begin))
            {
                return false;
            }
        }
        else
        {
            // To write:
            // - checksum
            // - blockSize
            // - (tag in startWrite)
            // - (revision in startWrite)
            // - (flags in finishWrite)
            header.current.checksum = s->computeChecksum(
                header.currentBlock + Header::size,
                header.current.blockSize
            );
            header.current.write(*s, header.currentBlock);
            // Reset for next block
            header.current.blockSize = 0;

            // Find a new block
            for (
                // Current block is full
                header.currentBlock += s->maxBlockSize();
                // Until we have tried everything
                header.currentBlock != header.startBlock;
                // Try next block
                header.currentBlock = (header.currentBlock + s->maxBlockSize()) % s->size()
            )
            {
                const auto maybe = Header::read(*s, header.currentBlock);
                if (
                    maybe.has_value() &&
                    maybe->erased() &&
                    maybe->revision == header.current.revision
                )
                {
                    break;
                }
            }
            // We have ran out of blocks
            if (header.currentBlock == header.startBlock)
            {
                return false;
            }
        }
    }
    return true;
}

template<LockFs::Storage Storage>
bool LockFs::LockFs<Storage>::finishWrite(LockFs::RamHeader & header)
{
    // Finish blocks
    // To write:
    // - (checksum in write)
    // - (blockSize in write)
    // - (tag in write)
    // - (revision in startWrite)
    // - flags
    while (header.currentBlock != header.startBlock)
    {
        auto maybe = Header::read(*s, header.currentBlock);
        if (
            maybe.has_value() &&
            maybe->erased() &&
            maybe->revision == header.current.revision
        )
        {
            maybe->flags = Header::CONTINUATION_BIT & ~Header::ERASED_BIT;
            if (!maybe->write(*s, header.currentBlock))
            {
                return false;
            }
        }
        // Note:
        //     x = (x + (lim - y)) % lim
        // is the same as
        //    x = (x - y) % max
        // except it is always positive (C op% has the sign of the LHS
        // operand not the RHS operand).
        header.currentBlock = (header.currentBlock + (s->size() - s->maxBlockSize())) % s->size();
    }
    auto start = Header::read(*s, header.startBlock);
    assert(start.has_value() && start->erased() && start->revision == header.current.revision);
    start->flags = static_cast<uint8_t>(~Header::ERASED_BIT);
    start->write(*s, header.startBlock);

    return true;
}
