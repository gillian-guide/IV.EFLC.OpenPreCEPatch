// SPDX-License-Identifier: GPL-3.0-or-later
//
// The write-set dump does not execute hook bodies. It resolves a body name to
// the ORIGINAL destination VA the reference build used, which is exactly what
// an installer's rel32 must point at for a byte-for-byte comparison. The real
// 32-bit registry lives in source/hooks.cpp and is deliberately not linked into
// a 64-bit host.

#include <cstdint>
#include <cstring>

namespace
{
    struct DumpAbi
    {
        const char* name;
        uintptr_t va;
    };

    constexpr DumpAbi kDumpAbi[] = {
#include "dump_abi.inc"
    };

    // Bodies that exist only behind an opt-in key have no original destination
    // VA. Give them a fixed out-of-image address so the dump records a real
    // rel32 install (and the matrix can attribute its 4 bytes) instead of a
    // null target.
    constexpr DumpAbi kDumpOnlyAbi[] = {
        {"abi_skip_intro_screens", 0x0F000000u},
        // Opt-in prologue body (garage_entry): a new entry point with no
        // original destination VA, so the dump records a real rel32 install.
        {"abi_garage_entry_a", 0x0F000001u},
        {"abi_nms_init_thunk", 0x0F000002u},
    };
} // namespace

extern "C" void* AbiByName(const char* name) noexcept
{
    if (name == nullptr)
        return nullptr;
    for (const DumpAbi& row : kDumpAbi)
    {
        if (std::strcmp(row.name, name) == 0)
            return reinterpret_cast<void*>(row.va);
    }
    for (const DumpAbi& row : kDumpOnlyAbi)
    {
        if (std::strcmp(row.name, name) == 0)
            return reinterpret_cast<void*>(row.va);
    }
    return nullptr;
}
