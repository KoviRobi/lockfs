# Lock FS (lite)

An almost file system for embedded microcontrollers based on flash
locking/sector protect.

## Overview

This is not a full file system, it's goals are:

- Create file
- Read file
- Erase file
- Power loss safety
- Wear levelling
- Bad block detect

In particular you will see it doesn't include write/append/modify,
or directories. If you want those, I recommend something else, like
[littlefs](https://github.com/littlefs-project/littlefs).

- [ ] Compare with spiffs also

The use-case is microcontrollers, which have mostly read-only
firmware/assets/bootloader for which they want locking/sector
protection. It is using sector-based locking, where whole sectors can
be locked at a time (some NOR flash only supports locking from one end
to some power of 2 size).

### Power loss safety

The most compelling argument I have come across using the locking/sector
protect features of NOR flash is stray writes as things are unexpectedly
powered down (power yanked), which can end up being the commands for
write or erase (especially unfortunate if you have issued a write enable
command just before). I don't have a case study to hand (if you do please
file an issue!), but anecdotal evidence of previous bugs with this being
the most likely culprit.

### Overview

The design is simple, based on the observations:

- We only have sector granularity erases, so a single table would lose
any safety. (Note: some flash chips have sub-sector erases, or smaller
first/last sectors).
- Random reads are almost as fast as sequential, so we can scatter the
metadata in sector headers. (Maybe some difference due to larger reads
with caching in the underlying hardware, but still pretty fast.)
- We only need streaming (loading the entire file), so random reads from
the filesystem can be slow (or not implemented, so use streaming to the
required point).

Based on these, we can just have a header on every block, with the header
containing some metadata (e.g. some tag identifying the file in an enum,
a revision for uploading versions which should replace it).

Then in the bootloader we can just scan the flash, populate a table
of the current versions of each tag, and lock those areas of memory
(another sequential scan).

In the application, we can then do pre-emptive/background erases for
the non-locked areas.

We achieve power-loss safety by having a flag for full write finished,
which we only write at the end, back to front. The first block is
also otherwise identified, this way we only have the blocks locked if
everything was written successfully. Then during next reboot we will
lock it, and leave the other one unlocked ready for erase.

If we detect a flash block with a checksum that is marked as finished,
but doesn't match, then it is a bad block, we will not erase it but lock
it. This way any future writes will avoid it.

We can get wear levelling by using the next free block after the tag with
the highest revision (it isn't 100% perfect, e.g. after wrap-arounds,
but should be good enough).
