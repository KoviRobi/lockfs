from __future__ import annotations

import enum
from ctypes import (
    CDLL,
    CFUNCTYPE,
    POINTER,
    Structure,
    c_bool,
    c_byte,
    c_char,
    c_char_p,
    c_size_t,
    c_uint8,
    c_uint32,
    c_void_p,
    create_string_buffer,
)
from pathlib import Path

build = Path(__file__).parent.parent / "build" / "libpython.so"

lib = CDLL(build)

Addr = c_uint32
BlockSize = c_uint8
Checksum = c_uint8
c_byte_p = POINTER(c_char)
ComputeChecksum = CFUNCTYPE(Checksum, c_byte_p, c_size_t)
VerifyChecksum = CFUNCTYPE(c_bool, c_byte_p, c_size_t, Checksum)


class TimeoutStorage(Structure):
    maxBlockSize = c_uint8.in_dll(lib, "maxBlockSize").value
    size = Addr.in_dll(lib, "size").value
    blocks = Addr.in_dll(lib, "blocks").value

    _fields_ = (
        ("computeChecksumCb", ComputeChecksum),
        ("verifyChecksumCb", VerifyChecksum),
        ("timeout", c_size_t),
        ("backing", c_byte * size),
        ("locked", c_bool * (size // 8)),
        ("frozen", c_bool),
    )

    def __init__(self, *args: object, **kw: object) -> None:
        super().__init__(*args, **kw)

        self.checksum = 1
        self.checksumOk = True
        self.computeChecksumCb = ComputeChecksum(lambda _d, _s: self.checksum)
        self.verifyChecksumCb = VerifyChecksum(lambda _d, _s, _c: self.checksumOk)

    def flashRead(self, addr: int, size: int) -> bytes | None:
        data = create_string_buffer(size)
        if lib.flashRead(TimeoutStorageP(self), addr, data, len(data)):
            return data.raw

    def flashWrite(self, data: bytes, addr: int) -> bool:
        return lib.flashWrite(TimeoutStorageP(self), data, len(data), addr)

    def flashErase(self, addr: int) -> bool:
        return lib.flashErase(TimeoutStorageP(self), addr)

    def flashLock(self, addr: int, tag: int) -> bool:
        return lib.flashLock(TimeoutStorageP(self), addr, tag)

    def flashLockFreeze(self) -> bool:
        return lib.flashLockFreeze(TimeoutStorageP(self))

    def dump(self, prefix: str = "") -> None:
        return print(self.__repr__(prefix))

    def __repr__(self, prefix: str = "") -> str:
        buf = create_string_buffer(1024)
        n = lib.dumpTS(TimeoutStorageP(self), buf, len(buf), prefix.encode())
        d = buf[:n]
        assert isinstance(d, bytes)
        return str(d.decode())


TimeoutStorageP = POINTER(TimeoutStorage)

lib.flashRead.argtypes = (TimeoutStorageP, Addr, c_byte_p, c_size_t)
lib.flashRead.restype = c_bool

lib.flashWrite.argtypes = (TimeoutStorageP, c_byte_p, c_size_t, Addr)
lib.flashWrite.restype = c_bool

lib.flashErase.argtypes = (TimeoutStorageP, Addr)
lib.flashErase.restype = c_bool

lib.flashLock.argtypes = (TimeoutStorageP, Addr, c_uint8)
lib.flashLock.restype = c_bool

lib.flashLockFreeze.argtypes = (TimeoutStorageP,)
lib.flashLockFreeze.restype = c_bool

lib.dumpTS.argtypes = (TimeoutStorageP, c_byte_p, c_size_t, c_char_p)
lib.dumpTS.restype = None


class Header(Structure):
    class Flags(enum.IntFlag):
        Erased = 0x80
        Continuation = 0x40

    Erased = Flags.Erased
    Continuation = Flags.Continuation

    class CFlags(c_uint8):
        def __repr__(self) -> str:
            return repr(Header.Flags(self.value))

    def dump(self, prefix: str = "") -> None:
        return print(self.__repr__(prefix))

    def __repr__(self, prefix: str = "") -> str:
        buf = create_string_buffer(1024)
        n = lib.dumpH(HeaderP(self), buf, len(buf), prefix.encode())
        d = buf[:n]
        assert isinstance(d, bytes)
        return str(d.decode())


Header._fields_ = (
    ("checksum", Checksum),
    ("blockSize", BlockSize),
    ("tag", c_uint8),
    ("flags", Header.CFlags),
    ("revision", c_uint8),
)

HeaderP = POINTER(Header)

lib.dumpH.argtypes = (HeaderP, c_byte_p, c_size_t, c_char_p)
lib.dumpH.restype = None


class RamHeader(Structure):
    _fields_ = (
        ("current", Header),
        ("startBlock", Addr),
        ("currentBlock", Addr),
        ("size", Addr),
    )

    def dump(self, prefix: str = "") -> None:
        return print(self.__repr__(prefix))

    def __repr__(self, prefix: str = "") -> str:
        buf = create_string_buffer(1024)
        n = lib.dumpRH(RamHeaderP(self), buf, len(buf), prefix.encode())
        d = buf[:n]
        assert isinstance(d, bytes)
        return str(d.decode())


RamHeaderP = POINTER(RamHeader)

lib.dumpRH.argtypes = (RamHeaderP, c_byte_p, c_size_t, c_char_p)
lib.dumpRH.restype = None

lib.context.argtypes = (RamHeaderP, c_size_t)
lib.context.restype = c_void_p


class ContextP(c_void_p):
    def __init__(self, size: int) -> None:
        self.RamHeaders = RamHeader * size
        self.headers = self.RamHeaders()
        void_p = lib.context(self.headers, len(self.headers))
        super().__init__(void_p)

    def __repr__(self, prefix: str = "") -> str:
        return "\n".join(
            f"{str(i) + ' ':-<80}\n" +
            hdr.__repr__(prefix=prefix+f"{i:<4}")
            for i, hdr in enumerate(ctx.headers)
        )


lib.create.argtypes = (TimeoutStorageP,)
lib.create.restype = c_void_p


class LockFsP(c_void_p):
    def __init__(self, storage: TimeoutStorage = TimeoutStorage()) -> None:
        void_p = lib.create(TimeoutStorageP(storage))
        super().__init__(void_p)

    def loadAll(self, context: ContextP) -> bool:
        return lib.loadAll(self, context)

    def startWrite(self, context: ContextP, tag: int, size: int) -> RamHeader | None:
        out = RamHeader()
        if lib.startWrite(self, context, tag, size, RamHeaderP(out)):
            return out

    def write(self, ramHeader: RamHeader, data: bytes) -> bool:
        return lib.write(self, RamHeaderP(ramHeader), data, len(data))

    def finishWrite(self, ramHeader: RamHeader) -> bool:
        return lib.finishWrite(self, RamHeaderP(ramHeader))

    def dump(self, prefix: str = "") -> None:
        return print(self.__repr__(prefix))

    def __repr__(self, prefix: str = "") -> str:
        buf = create_string_buffer(1024)
        n = lib.dumpFS(self, buf, len(buf), prefix.encode())
        d = buf[:n]
        assert isinstance(d, bytes)
        return str(d.decode())


lib.loadAll.argtypes = (LockFsP, ContextP)
lib.loadAll.restype = c_bool

lib.startWrite.argtypes = (LockFsP, ContextP, c_uint8, Addr, RamHeaderP)
lib.startWrite.restype = c_bool

lib.write.argtypes = (LockFsP, RamHeaderP, c_byte_p, c_size_t)
lib.write.restype = c_bool

lib.finishWrite.argtypes = (LockFsP, RamHeaderP)
lib.finishWrite.restype = c_bool

lib.dumpFS.argtypes = (LockFsP, c_byte_p, c_size_t, c_char_p)
lib.dumpFS.restype = None

ts = TimeoutStorage(timeout=TimeoutStorage.size)
ts.dump()
print("-" * 80)
for b in range(ts.blocks):
    print("Erase", b, ts.flashErase(b * ts.maxBlockSize))
ts.dump()
print("-" * 80)
ts.timeout = 128

ctx = ContextP(2)
print(ctx)
fs = LockFsP(ts)
print("Load all", fs.loadAll(ctx))

tag = 0
msg = b"hello"
print("timeout", ts.timeout)
ts.timeout = 128
print("startWrite")
rh = fs.startWrite(ctx, tag, len(msg))
assert rh
rh.dump()
print("timeout", ts.timeout)
print("Write:")
print("write", fs.write(rh, msg))
print("timeout", ts.timeout)
print("finishWrite", fs.finishWrite(rh))
ts.dump()
fs.dump()

ts.frozen = False
ctx = ContextP(2)
fs.loadAll(ctx)
print(ctx)
