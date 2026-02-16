#pragma once

#include "flash_interface.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdint>
#include <optional>
#include <span>

namespace LockFs
{
    template<Storage Storage>
    struct LockFs
    {
        using BlockSize = Storage::BlockSize;
        using FlashAddr = Storage::FlashAddr;
        using Checksum = Storage::Checksum;

        Storage * s;

        // Serialised
        struct Header
        {
            // Checksum/hash over the block, used to detect bad blocks
            Checksum checksum;
            // size of the 64KiB block (after header), not user specified
            BlockSize blockSize;
            // User specified
            uint8_t tag;
            // Flag (erased, continuation), not user specified
            // TODO: Finished/closed flag?
            uint8_t flags;
            // Counter when uploading a newer version, not user specified
            uint8_t revision;

            // Serialised size
            static constexpr FlashAddr size =
                sizeof(Header::tag) +
                sizeof(Header::flags) +
                sizeof(Header::revision) +
                sizeof(Header::blockSize) +
                sizeof(Header::checksum);

            // Bits for flags
            static constexpr uint8_t ERASED_BIT = 0x80;
            static constexpr uint8_t CONTINUATION_BIT = 0x40;

            // Returns {} on failure to read
            static std::optional<Header> read(Storage & s, const FlashAddr address);
            // Returns false on failure to write
            bool write(Storage & s, FlashAddr address) const;

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
            FlashAddr size;

#if 0
            struct Iter
            {
                struct value_type
                {
                    FlashAddr block;
                    FlashAddr blockSize;
                };
                using difference_type_t = std::ptrdiff_t;

                RamHeader * rh;
                FlashAddr block;

                Iter & operator++();
                Iter operator++(int);
                value_type operator*() const
                {
                    return std::span{block, rh->current.blockSize};
                }
            };
            static_assert(std::input_iterator<Iter>);
#endif
        };

        struct Context
        {
            std::span<RamHeader> headers;
            std::optional<FlashAddr> nextFreeBlock;
        };

        // Fills the headers in the context (indexed by tag) and returns true if successful
        bool loadAll(Context & context);

        // Note: only one write in progress at a time for now.
        std::optional<RamHeader> startWrite(Context headers, uint8_t tag, FlashAddr size);
        bool write(RamHeader & header, std::span<const uint8_t> data);
        bool finishWrite(RamHeader & header);
    };
};

#include "lockfs.tpp"
