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
    return nullptr;
}
