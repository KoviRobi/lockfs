#include "python.hpp"

#include "lockfs/lockfs.hpp"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <string>

extern "C"
{
    extern const constexpr decltype(maxBlockSize_) maxBlockSize = ::maxBlockSize_;
    extern const constexpr decltype(size_) size = ::size_;
    extern const constexpr decltype(blocks_) blocks = ::blocks_;
    extern const constexpr Addr headerSize = Fs::Header::size;
}

FILE * out = stdout;

bool TimeoutStorage::flashRead(FlashAddr address, std::span<uint8_t> dest)
{
    assert(dest.size() <= size());
    while (dest.size() > 0)
    {
        if (timeout == 0)
        {
            return false;
        }
        dest[0] = backing[address];
        address = (address + 1) % size();
        dest = dest.subspan(1);
        --timeout;
    }
    return true;
}

bool TimeoutStorage::flashWrite(std::span<const uint8_t> src, FlashAddr address)
{
    assert(src.size() <= size());
    while (src.size() > 0)
    {
        if (timeout == 0)
        {
            return false;
        }
        assert(!locked[address / maxBlockSize()]);
        assert(
            (backing[address] == 0xFF) ||
            (backing[address] == src[0])
            // Otherwise writing to a non-erased block, unpredictable!
        );
        backing[address] = src[0];
        address = (address + 1) % size();
        src = src.subspan(1);
        --timeout;
    }
    return true;
}

bool TimeoutStorage::flashErase(FlashAddr block)
{
    assert(block % maxBlockSize() == 0);
    assert(!locked[block / maxBlockSize()]);
    const size_t len = std::min<size_t>(maxBlockSize(), timeout);
    std::ranges::fill(std::span{backing}.subspan(block, len), 0xFF);
    timeout -= len;
    return timeout > 0;
}

bool TimeoutStorage::flashLock(FlashAddr address, uint8_t tag)
{
    assert(!frozen);
    locked[address / maxBlockSize()] = 1;
    return true;
}

bool TimeoutStorage::flashLockFreeze()
{
    frozen = true;
    return true;
}

static_assert(LockFs::Storage<TimeoutStorage>);

bool flashRead(TimeoutStorage * ts, Addr addr, uint8_t * buf, size_t bufSize)
{
    return ts->flashRead(addr, std::span{buf, bufSize});
}

bool flashWrite(TimeoutStorage * ts, const uint8_t * buf, size_t size, Addr addr)
{
    return ts->flashWrite(std::span{buf, size}, addr);
}

bool flashErase(TimeoutStorage * ts, Addr block)
{
    return ts->flashErase(block);
}

bool flashLock(TimeoutStorage * ts, Addr addr, uint8_t tag)
{
    return ts->flashLock(addr, tag);
}

bool flashLockFreeze(TimeoutStorage * ts)
{
    return ts->flashLockFreeze();
}

[[gnu::format(printf, 3, 4)]]
int append(char * & buf, size_t & len, const char * fmt, ...)
{
    if (!buf) return 0;
    va_list args;
    va_start(args, fmt);
    int printed = vsnprintf(buf, len, fmt, args);
    va_end(args);
    if (printed > 0)
    {
        buf += printed;
        len -= printed;
        return printed;
    }
    else
    {
        return 0;
    }
}

int dumpTS(const TimeoutStorage * ts, char * buf, size_t len, const char * prefix)
{
    int printed = 0;
    printed += append(buf, len, "%sTimeoutStorage(%p){", prefix, ts);
    if (ts != nullptr)
    {
        printed += append(buf, len, "\n%s\ttimeout: %u,\n", prefix, (unsigned)ts->timeout);

        for (unsigned i = 0; i < ts->size(); i += ts->maxBlockSize())
        {
            unsigned end = std::min<unsigned>(ts->size(), i + ts->maxBlockSize());
            for (unsigned j = i; j < end; ++j)
            {
                const auto block = j / ts->maxBlockSize();
                const auto rem = j % ts->maxBlockSize();
                if (rem == 0) printed += append(buf, len, "%s\t%04X (%d): ", prefix, j, ts->locked[block]);
                else for (unsigned r = std::countr_zero(rem); r > 0; r >>= 1) printed += append(buf, len, " ");
                printed += append(buf, len, "%02X", ts->backing[j]);
            }
            printed += append(buf, len, "\t");
            for (size_t j = i; j < end; ++j)
            {
                const char c = (' ' <= ts->backing[j] && ts->backing[j] <= '~') ?
                        ts->backing[j] : '.';
                printed += append(buf, len, "%c", c);
            }
            printed += append(buf, len, "\n");
        }

        printed += append(buf, len, "%s\tfrozen: %s\n", prefix, ts->frozen ? "true" : "false");
        printed += append(buf, len, "%s\tcompute: %d\n", prefix, ts->computeChecksum(0, 0));
        printed += append(buf, len, "%s\tverify0: %d\n", prefix, ts->verifyChecksum(0, 0, 0));
    }
    printed += append(buf, len, "%s}", prefix);
    return printed;
}

int dumpH(const Fs::Header * h, char * buf, size_t len, const char * prefix)
{
    int printed = 0;
    printed += append(buf, len, "%sHeader(%p){\n", prefix, h);
    printed += append(buf, len, "%s\tchecksum:  %u\n", prefix, h->checksum);
    printed += append(buf, len, "%s\tblockSize: %u\n", prefix, h->blockSize);
    printed += append(buf, len, "%s\ttag:       %u\n", prefix, h->tag);
    printed += append(buf, len, "%s\tflags:     ", prefix);
    {
        const char * sep = "";
        if (h->flags & Fs::Header::ERASED_BIT) { printed += append(buf, len, "%sErased", sep); sep = "|"; }
        if (h->flags & Fs::Header::CONTINUATION_BIT) { printed += append(buf, len, "%sContinuation", sep); sep = "|"; }
        if (h->flags == 0) { printed += append(buf, len, "%s(none)", sep); sep = "|"; }
        printed += append(buf, len, "\n");
    }
    printed += append(buf, len, "%s\trevision:  %d\n", prefix, h->revision);
    printed += append(buf, len, "%s}", prefix);
    return printed;
}

int dumpRH(const Fs::RamHeader * rh, char * buf, size_t len, const char * prefix)
{
    int printed = 0;
    printed += append(buf, len, "%sRamHeader(%p){\n", prefix, rh);
    if (prefix)
    {
        std::string s = prefix;
        s += "\t";
        int p = dumpH(&rh->current, buf, len, s.c_str());
        printed += p;
        buf += p;
        len -= p;
    }
    else
    {
        printed += dumpH(&rh->current, nullptr, 0, "\t");
    }
    printed += append(buf, len, "\n");
    printed += append(buf, len, "%s\tstartBlock:   %d\n", prefix, rh->startBlock);
    printed += append(buf, len, "%s\tcurrentBlock: %d\n", prefix, rh->currentBlock);
    printed += append(buf, len, "%s\tsize:         %d\n", prefix, rh->size);
    printed += append(buf, len, "%s}", prefix);
    return printed;
}

int dumpFS(const Fs * fs, char * buf, size_t len, const char * prefix)
{
    int printed = 0;
    printed += append(buf, len, "%sLockFs(%p){\n", prefix, fs);
    if (prefix)
    {
        std::string s = prefix;
        s += "\t";
        int p = dumpTS(fs->s, buf, len, s.c_str());
        printed += p;
        buf += p;
        len -= p;
    }
    else
    {
        int p = dumpTS(fs->s, buf, len, "\t");
        printed += p;
        buf += p;
        len -= p;
    }
    printed += append(buf, len, "\n");
    printed += append(buf, len, "%s}", prefix);
    return printed;
}

Fs::Context * context(Fs::RamHeader * buf, size_t size)
{
    return new Fs::Context{.headers = std::span{buf, size}};
}

Fs * create(TimeoutStorage * ts)
{
    return new Fs{.s = ts};
}

bool loadAll(Fs * fs, Fs::Context * ctx)
{
    return fs->loadAll(*ctx);
}

bool startWrite(Fs * fs, Fs::Context ctx, uint8_t tag, Addr size, Fs::RamHeader * out)
{
    const auto opt = fs->startWrite(ctx, tag, size);
    if (opt.has_value())
    {
        *out = *opt;
        return true;
    }
    return false;
}

bool write(Fs * fs, Fs::RamHeader * rh, const uint8_t * src, size_t len)
{
    return fs->write(*rh, std::span{src, len});
}

bool finishWrite(Fs * fs, Fs::RamHeader * rh)
{
    return fs->finishWrite(*rh);
}
