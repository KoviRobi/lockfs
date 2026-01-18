#include "lockfs.hpp"

#include "endian.hpp"

#include <cassert>
#include <cstdint>

#include <optional>

using namespace Serialisation;

std::optional<LockFs::Header> LockFs::Header::read(const FlashAddr address)
{
    uint8_t buf[sizeof(Header)];
    if (flashRead(address, sizeof(Header), buf))
    {
        size_t i = 0;
        const auto tag       = EL<uint8_t>::load(&buf[i]);
        const auto flags     = EL<uint8_t>::load(&buf[i += sizeof(tag)]);
        const auto revision  = EL<uint8_t>::load(&buf[i += sizeof(flags)]);
        const auto blockSize = EL<uint16_t>::load(&buf[i += sizeof(revision)]);
        const auto checksum  = EL<uint64_t>::load(&buf[i += sizeof(blockSize)]);
        return Header{checksum, blockSize, tag, flags, revision};
    }
    return std::optional<Header>{};
}

constexpr size_t headerSize =
    sizeof(LockFs::Header::tag) +
    sizeof(LockFs::Header::flags) +
    sizeof(LockFs::Header::revision) +
    sizeof(LockFs::Header::blockSize) +
    sizeof(LockFs::Header::checksum);

bool LockFs::Header::write(FlashAddr address) const
{
    uint8_t buf[headerSize];
    size_t i = 0;
    EL<uint8_t>::store(&buf[i], tag);
    EL<uint8_t>::store(&buf[i += sizeof(tag)], flags);
    EL<uint8_t>::store(&buf[i += sizeof(flags)], revision);
    EL<uint16_t>::store(&buf[i += sizeof(revision)], blockSize);
    EL<uint64_t>::store(&buf[i += sizeof(blockSize)], checksum);
    return flashWrite(buf, address);
}

bool LockFs::loadAll(LockFs::Context context)
{
    // Read
    for (FlashAddr i = 0; i < (size / maxBlockSize); ++i)
    {
        const FlashAddr addr = i * maxBlockSize;
        const auto hdr = Header::read(addr);
        if (!hdr.has_value())
        {
            // TODO: Bad blocks? Or just out of bounds
            return false;
        }
        if (!hdr->continuation())
        {
            if (context[i].current.erased() || hdr->newerThan(context[i].current))
            {
                context[i].current = *hdr;
                context[i].startBlock = addr;
                context[i].currentBlock = addr;
            }
        }
        else if (hdr->erased() && context.nextFreeBlock == Context::NO_FREE)
        {
            context.nextFreeBlock = addr;
        }
        // TODO: Unfinished blocks? Reduce revision in context[i]
    }
    // Lock
    for (FlashAddr i = 0; i < (size / maxBlockSize); ++i)
    {
        const FlashAddr addr = i * maxBlockSize;
        const auto hdr = Header::read(addr);
        if (
            !hdr->erased() &&
            hdr->tag == context[i].current.tag &&
            hdr->revision == context[i].current.revision
        )
        {
            flashLock(addr, hdr->tag <= maxNonvolatileTag);
        }
    }
    flashLockFreeze();
    return true;
}

std::optional<LockFs::RamHeader> LockFs::startWrite(LockFs::Context context, uint8_t tag, uint32_t size)
{
    // TODO: Preallocate? E.g. writing tags/revision then update context
    // (maybe just revision, in case a new revision gets written)?
    // To write:
    // - (checksum in write)
    // - (blockSize in write)
    // - (tag in write)
    // - revision
    const uint8_t revision = context[tag].current.erased() ? 0 :
        (context[tag].current.tag + 1);
    if (context.nextFreeBlock == Context::NO_FREE)
    {
        return std::optional<LockFs::RamHeader>{};
    }
    RamHeader header{
        .current = {
            .checksum  = init<decltype(Header::checksum)>(erasedValue),
            .blockSize = init<decltype(Header::blockSize)>(erasedValue),
            .tag       = init<decltype(Header::tag)>(erasedValue),
            .flags     = init<decltype(Header::flags)>(erasedValue),
            .revision  = revision,
        },
        .startBlock = context.nextFreeBlock,
        .currentBlock = context.nextFreeBlock,
        .size = size,
    };
    for (
        ;
        header.currentBlock != header.startBlock;
        header.currentBlock = (header.currentBlock + maxBlockSize) % size
    )
    {
        header.current.write(header.currentBlock);
    }
    header.currentBlock = header.startBlock;
    header.current.tag = tag;
    context[tag] = header;
    return header;
}

bool LockFs::write(LockFs::RamHeader & header, std::span<uint8_t> data)
{
    // TODO: Not block size but blockSize - header encoded size? This will
    // allow us to have 0xFF as a special value for not finished headers
    while (data.size() > 0)
    {
        if (header.current.blockSize < maxBlockSize - headerSize)
        {
            // Add data to current block, if it becomes full, write header
            // Note: Header::blockSize doesn't count header size so we
            // add it in
            const auto start = header.current.blockSize + headerSize;
            const size_t blockRemaining = maxBlockSize - start;
            const size_t toWrite = std::min(data.size(), blockRemaining);
            const auto end = decltype(Header::blockSize)(start + toWrite);
            header.current.blockSize = end;
            std::span src = data.subspan(toWrite);
            if (!flashWrite(src, header.currentBlock + start))
            {
                return false;
            }
        }
        else
        {
            #pragma GCC warning "TODO: Finish off block"
            // To write:
            // - checksum
            // - blockSize
            // - tag
            // - (revision in startWrite)
            // - (flags in finishWrite)
            // TODO: header.current.checksum;
            header.current.write(header.currentBlock);
            // Reset for next block
            header.current.checksum = 0;
            header.current.blockSize = 0;

            // Find a new block
            for (
                // Current block is full
                header.currentBlock += maxBlockSize;
                // Until we have tried everything
                header.currentBlock != header.startBlock;
                // Try next block
                header.currentBlock = (header.currentBlock + maxBlockSize) % size
            )
            {
                const auto maybe = Header::read(header.currentBlock);
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

bool LockFs::finishWrite(LockFs::RamHeader & header)
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
        auto maybe = Header::read(header.currentBlock);
        if (
            maybe.has_value() &&
            maybe->erased() &&
            maybe->revision == header.current.revision
        )
        {
            maybe->flags = Header::CONTINUATION_BIT & ~Header::ERASED_BIT;
            if (!maybe->write(header.currentBlock))
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
        header.currentBlock = (header.currentBlock + (size - maxBlockSize)) % size;
    }
    auto start = Header::read(header.startBlock);
    assert(start.has_value() && start->erased() && start->revision == header.current.revision);
    start->flags = static_cast<uint8_t>(~Header::ERASED_BIT);
    start->write(header.startBlock);

    return true;
}
