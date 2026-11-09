"""Read the x86 minidump of the GTA IV crash and print where the fault happened.

A minidump is a header plus a directory of streams; for this job three matter:
the exception stream (the code, the faulting address and the thread CONTEXT with
the registers), the module list (so an address can be named) and the memory
ranges (so the faulting thread's stack can be walked by hand). No debugger is
needed, and the stack walk here is deliberately naive: it prints the first words
above ESP that land inside a loaded module, which is enough to see who called
into a data address.
"""

import struct
import sys
from pathlib import Path

MINIDUMP_SIGNATURE = 0x504D444D  # 'MDMP'
EXCEPTION_STREAM = 6
MODULE_LIST_STREAM = 4
MEMORY_LIST_STREAM = 5
MEMORY64_LIST_STREAM = 9

# x86 CONTEXT: offsets of the registers this script reads.
CONTEXT_EIP = 0xB8
CONTEXT_ESP = 0xC4
CONTEXT_EBP = 0xB4


def read_streams(data):
    signature, version, count, directory_rva = struct.unpack_from("<IIII", data, 0)
    if signature != MINIDUMP_SIGNATURE:
        raise SystemExit("not a minidump")
    streams = {}
    for index in range(count):
        kind, size, rva = struct.unpack_from("<III", data, directory_rva + index * 12)
        streams.setdefault(kind, (rva, size))
    return streams


def modules(data, rva, size):
    count = struct.unpack_from("<I", data, rva)[0]
    out = []
    offset = rva + 4
    for index in range(count):
        base = struct.unpack_from("<Q", data, offset)[0]
        module_size = struct.unpack_from("<I", data, offset + 8)[0]
        name_rva = struct.unpack_from("<I", data, offset + 20)[0]
        name = ""
        if name_rva:
            length = struct.unpack_from("<I", data, name_rva)[0]
            name = data[name_rva + 4:name_rva + 4 + length].decode("utf-16-le", "replace")
        out.append((base & 0xFFFFFFFF, module_size, name))
        offset += 108
    return out


def describe(address, module_list):
    for base, size, name in module_list:
        if base <= address < base + size:
            return f"{Path(name).name}+0x{address - base:X}"
    return f"0x{address:08X}"


def exception(data, rva, size):
    thread_id, align = struct.unpack_from("<II", data, rva)
    # MINIDUMP_EXCEPTION is 152 bytes (code, flags, record, 64-bit address,
    # parameter count, alignment, fifteen 64-bit parameters), so the thread
    # context descriptor follows the record at +8+152.
    code, flags, record, address, params = struct.unpack_from("<IIQQI", data, rva + 8)
    context_size, context_rva = struct.unpack_from("<II", data, rva + 8 + 152)
    eip, esp, ebp = (struct.unpack_from("<I", data, context_rva + offset)[0]
                     for offset in (CONTEXT_EIP, CONTEXT_ESP, CONTEXT_EBP))
    return {"thread": thread_id, "code": code, "address": address,
            "eip": eip, "esp": esp, "ebp": ebp, "context": context_rva}


def memory_ranges(data, streams):
    ranges = {}
    if MEMORY64_LIST_STREAM in streams:
        rva, _ = streams[MEMORY64_LIST_STREAM]
        count, base_rva = struct.unpack_from("<QQ", data, rva)
        cursor = base_rva
        for index in range(count):
            start, size = struct.unpack_from("<QQ", data, cursor)
            cursor += 16
            ranges[start & 0xFFFFFFFF] = (cursor, size)
            cursor += size
    elif MEMORY_LIST_STREAM in streams:
        rva, _ = streams[MEMORY_LIST_STREAM]
        count = struct.unpack_from("<I", data, rva)[0]
        for index in range(count):
            start, size, block_rva = struct.unpack_from("<QII", data, rva + 4 + index * 16)
            ranges[start & 0xFFFFFFFF] = (block_rva, size)
    return ranges


def bytes_at(data, ranges, address, count):
    for start, (rva, size) in ranges.items():
        if start <= address and address + count <= start + size:
            offset = rva + (address - start)
            return data[offset:offset + count]
    return None


def main() -> int:
    path = Path(sys.argv[1])
    data = path.read_bytes()
    streams = read_streams(data)
    module_list = modules(data, *streams[MODULE_LIST_STREAM])
    info = exception(data, *streams[EXCEPTION_STREAM])
    ranges = memory_ranges(data, streams)

    print(f"dump      {path.name}")
    print(f"thread    {info['thread']}")
    print(f"exception 0x{info['code']:08X} at 0x{info['address']:08X}")
    print(f"eip       0x{info['eip']:08X}  {describe(info['eip'], module_list)}")
    print(f"esp       0x{info['esp']:08X}  ebp 0x{info['ebp']:08X}")
    print()
    print("stack words that land in a loaded module (candidate return addresses):")

    raw = bytes_at(data, ranges, info["esp"], 0x400)
    if raw is None:
        print("  the dump has no memory for this stack range")
        return 1
    shown = 0
    for offset in range(0, len(raw) - 4, 4):
        value = struct.unpack_from("<I", raw, offset)[0]
        for base, size, name in module_list:
            if base <= value < base + size:
                print(f"  esp+0x{offset:03X} -> 0x{value:08X}  {describe(value, module_list)}")
                shown += 1
                break
        if shown >= 24:
            break
    return 0


if __name__ == "__main__":
    sys.exit(main())
