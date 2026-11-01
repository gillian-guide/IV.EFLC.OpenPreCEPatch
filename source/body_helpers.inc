// SPDX-License-Identifier: GPL-3.0-or-later
//
// Body helpers that more than one feature module's bodies call.
//
// These were TU-local functions in the single hook-bodies translation unit. They are
// included *inside* each module that needs them rather than exported from opp.patch: the
// compiler inlines them into the bodies they guard, and moving them out of line would
// change the bodies' machine code. `static` keeps one copy per importing module, as
// before. Included from the module purview after `import opp.patch;`/`import opp.log;`.

#pragma once

#if !defined(_WIN64)

// The runtime-pointer failsafe: a missing pointer makes the body continue with
// the value the game would have had without the fix, and logs one line.
[[maybe_unused]] static bool zpatch_failsafe_skip(const char* what, bool missing) noexcept
{
    if (!missing)
        return false;
    opp::Log("failsafe: %s is missing, continuing without the fix", what);
    return true;
}

// Byte/word/dword stores through VirtualProtect. As in every original handler,
// the result is ignored and the write happens anyway (an unmapped target faults).
[[maybe_unused]] static void wp_byte(void* address, uint8_t value) noexcept
{
    injector::WriteMemory<uint8_t>(injector::raw_ptr(address), value, true);
}

[[maybe_unused]] static void wp_dword(void* address, uint32_t value) noexcept
{
    injector::WriteMemory<uint32_t>(injector::raw_ptr(address), value, true);
}

[[maybe_unused]] static void wp_word(void* address, uint16_t value) noexcept
{
    injector::WriteMemory<uint16_t>(injector::raw_ptr(address), value, true);
}

[[maybe_unused]] static const uint8_t* scan_game(std::string_view signature) noexcept
{
    const auto* module = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
    if (!module) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    Match match;
    const ModuleView view = ExecutableView({module, nt->OptionalHeader.SizeOfImage});
    if (!FindPattern(view, signature, match)) return nullptr;
    return match.address;
}

// The NewFilesCompatibility / HighFPSSpeedupFix scan helper (the original unit
// had the same scan routine under this name, returning the raw address).
[[maybe_unused]] static uintptr_t newfiles_find(std::string_view signature)
{
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
    const auto* dos = base ? reinterpret_cast<const IMAGE_DOS_HEADER*>(base) : nullptr;
    const auto* nt = dos && dos->e_magic == IMAGE_DOS_SIGNATURE
                         ? reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew)
                         : nullptr;
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    Match match;
    if (!FindPattern(ExecutableView({base, nt->OptionalHeader.SizeOfImage}), signature, match)) return 0;
    return reinterpret_cast<uintptr_t>(match.address);
}

// The original guards its one-time pattern scans with the MSVC thread-safe
// static protocol. Its epoch (*(TLS[_tls_index]+4)) is loader state that stays
// non-negative when the ASI is mapped without CRT TLS init, which made the
// scan skip; this keeps the same one-time semantics with a tri-state guard
// (0 = uninitialised, -1 = initialising, 1 = done).
[[maybe_unused]] static bool zpatch_tls_static_header(uint32_t& guard) noexcept
{
    for (;;)
    {
        const LONG observed = InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(&guard), -1, 0);
        if (observed == 0) return true;
        if (observed == -1)
        {
            Sleep(1);  // another thread owns the one-time scan
            continue;
        }
        return false;
    }
}

[[maybe_unused]] static void zpatch_tls_static_footer(uint32_t& guard) noexcept
{
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&guard), 1);
}

// The original helpers compiled with /GS end with
//   mov ecx,[ebp-N]; xor ecx,ebp; call <cookie check>; ...
// and the shim resumes *without* restoring EFLAGS (pushal/popal do not), so the
// resume sees the flags from `cmp ecx,[__security_cookie]`; keep that tail.
[[maybe_unused]] static void zpatch_cookie_tail() noexcept
{
    __asm {
        mov ecx, dword ptr [__security_cookie]
        call zpatch_spawns_cookie_check_ecx
    }
}

[[maybe_unused]] static uint8_t zpatch_script_call_bool(uint32_t hash, uint32_t nargs,
                                       const uint32_t* args) noexcept
{
    return static_cast<uint8_t>(zpatch_script_call(hash, nargs, args));
}

#endif  // !defined(_WIN64)
