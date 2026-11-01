// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation unit for opp.patch: the naked body helpers that more
// than one module calls.
//
// MSVC writes a C-linkage naked definition in an interface unit into the
// module's BMI, and every importer then fails with C2488 ('naked' can only
// be applied to non-member function definitions). An implementation unit is
// never imported, so the bodies keep external linkage here and the wrappers
// and feature bodies in the interface unit call them through hooks.h.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#if !defined(_WIN64)
#include "driveby_seh.h"
#endif

module opp.patch;

#if !defined(_WIN64)
extern "C"
{
// Unmangled so the inline asm can call it (the declaration is in namespace
// zpatch).
__declspec(naked) uint32_t zpatch_script_resolve(uint32_t)
{
    // Naked: EDX carries the caller's block, so push it explicitly and keep the
    // original's cleanup.
    __asm {
        push edx
        push dword ptr [esp + 8]
        call zpatch_script_resolve_seh
        add  esp, 8
        ret
    }
}
__declspec(naked) uint32_t zpatch_script_resolve_with_block(uint32_t,
                                                                  void*)
{
    // Naked: hash goes in ECX, block in EDX; ESI must survive the call.
    __asm {
        push esi
        mov  esi, dword ptr [esp + 8]
        mov  ecx, esi
        mov  edx, dword ptr [esp + 0Ch]
        mov  eax, DWORD PTR [zpatch_driveby_resolver]
        call eax
        pop  esi
        ret
    }
}

static uint32_t __cdecl zpatch_resolver_call(uint32_t hash,
                                                  uint32_t edx_arg)
{
    return zpatch_script_resolve_with_block(
        hash, reinterpret_cast<void*>(static_cast<uintptr_t>(edx_arg)));
}

extern "C" __declspec(noinline) uint32_t __cdecl
zpatch_script_resolve_seh(uint32_t hash, uint32_t edx_arg)
{
    return detail::driveby_resolve_seh(&zpatch_resolver_call, hash, edx_arg);
}

extern "C" __declspec(noinline) uint32_t __cdecl
zpatch_driveby_resolve_seh(uint32_t hash, uint32_t edx_arg)
{
    return zpatch_script_resolve_seh(hash, edx_arg);
}

// sub_1005FE7B: the /GS cookie check (cookie in ECX). It is the last call on
// the probe's return path, so it must preserve EAX (the model index); the
// original is `cmp ecx,[0x1015800C]; jne fail; ret`.
extern "C" void zpatch_spawns_cookie_check_ecx();
extern "C" uintptr_t __security_cookie;
__declspec(naked) void zpatch_spawns_cookie_check_ecx()
{
    __asm {
        cmp ecx, dword ptr [__security_cookie]
        jne spawns_cookie_fail
        ret
    spawns_cookie_fail:
        mov ecx, 2
        int 29h
    }
}

// RemoveRGSCLoginRequirement (compatibility) installs this body and
// NewMemorySystem's allocator vtable (performance) names it, so it lives here
// with the other external-linkage naked bodies.
__declspec(naked) int __cdecl abi_rgsc_login_removed()
{
    __asm {
        mov al, 1
        ret
    }
}
}  // extern "C"
#endif  // !defined(_WIN64)
