// SPDX-License-Identifier: GPL-3.0-or-later
//
// input
//
// One function per INI arm, in the original loader order; the bodies are the
// readable reconstruction's verified arms, the wrapper and the arm trace ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"

export module opp.input;

import opp.patch;
import opp.game;
import opp.common;
import opp.config;
import opp.log;

// Hook bodies: the machine-level entry points input installs. Each keeps the
// original site's calling convention and register contract (its comment names
// the destination VA); 32-bit only - the x64 parity dump resolves names to the
// original VAs. The installers below take their addresses; the naked
// definitions are the marked tail section at the end of this file.
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

extern "C"
{

// sub_100241E0, the compiled helper abi_mouse_advance calls.
void zpatch_mf_advance_c() noexcept
{
    // sub_100241E0: when both mouse axes are zero, patch three 8-byte windows of
    // the resumed game function (dword_10159C40 + 52/94/107) with the "pull"
    // constants; otherwise NOP-ish them out. VirtualProtect failures are ignored
    // like the original (a null resume would fault; unreachable once installed).
    const auto base = static_cast<uintptr_t>(zpatch_mf_resume_b);
    // sub_100241E0 dereferences the resume slot unconditionally.
    const auto* x = Rebased<const int32_t*>(zpatch_mf_x);
    const auto* y = Rebased<const int32_t*>(zpatch_mf_y);
    // The axis globals hold mouse units; 1/128 converts a unit to an angle.
    constexpr double kMouseUnitsPerPixel = 1.0 / 128.0;
    static_assert(kMouseUnitsPerPixel == 0.0078125);
    // sub_100241E0 evaluates Y first, then X, and dereferences both unguarded.
    const bool zero = (static_cast<double>(*y) * kMouseUnitsPerPixel == 0.0) &&
                      (static_cast<double>(*x) * kMouseUnitsPerPixel == 0.0);

    // The three 8-byte windows sub_100241E0 writes into its resumed function
    // (dword_10159C40 + these offsets): with both axes zero they store the pull
    // angle into a vehicle field; otherwise they NOP the store and keep the
    // following instruction. A window is one write so the pair cannot be seen
    // half-applied.

    // The pull angle lands in the aim camera's pitch (+0x144) and yaw (+0x148)
    // fields. These arrays are raw instruction bytes, one 8-byte write each.
    // movss dword ptr [esi+0x144], xmm0
    constexpr uint8_t kWritePitch[8] = {0xF3, 0x0F, 0x11, 0x86, 0x44, 0x01, 0x00, 0x00};
    // movss dword ptr [esi+0x148], xmm1
    constexpr uint8_t kWriteYaw[8] = {0xF3, 0x0F, 0x11, 0x8E, 0x48, 0x01, 0x00, 0x00};
    // fstp dword ptr [esi+0x148] followed by the first two bytes of the next
    // instruction (8B 5C = mov ebx, [esp+...]).
    constexpr uint8_t kStoreYaw[8] = {0xD9, 0x9E, 0x48, 0x01, 0x00, 0x00, 0x8B, 0x5C};
    // fstp st(0), four NOPs and the same next-instruction bytes.
    constexpr uint8_t kDiscardYaw[8] = {0xDD, 0xD8, 0x90, 0x90, 0x90, 0x90, 0x8B, 0x5C};
    constexpr uint8_t kNopBytes[8] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};

    struct Window final
    {
        uintptr_t offset;
        const uint8_t* whenZero;
        const uint8_t* otherwise;
    };
    const std::array<Window, 3> windows = {{
        {52u, kWritePitch, kNopBytes},
        {94u, kWriteYaw, kNopBytes},
        {107u, kStoreYaw, kDiscardYaw},
    }};
    for (const Window& window : windows)
    {
        const uint8_t* value = zero ? window.whenZero : window.otherwise;
        injector::WriteMemoryRaw(injector::raw_ptr(base + window.offset),
                                 const_cast<uint8_t*>(value), sizeof(kNopBytes), true);
    }
    zpatch_cookie_tail();
}

// The entry points the installers take by address; the bodies are the 32-bit
// tail section at the end of this file.
void abi_driveby_fov_shim();
void abi_mouse_axis_float();
void abi_mouse_axis_neg();
void abi_mouse_axis_int();
void abi_mouse_axis_flag();
void abi_mouse_angle();
void abi_mouse_advance();
}  // extern "C"


#endif  // !defined(_WIN64)

namespace opp::features
{

    // BorderlessWindowed / DoNotPauseOnMinimize live in the separate
    // XLivelessAddon companion repository.


    // DrivebyFOVFix

    class DrivebyFOVFix
    {
    public:
        DrivebyFOVFix()
        {
            PublishEvent() += []
            {
                TraceArm("PublishDrivebyFOVFix");

                // DrivebyFOVFix: 2 slots
                if (Game::Option("DrivebyFOVFix", "1"))
                {
                    if (auto pattern = find_pattern("F3 0F 10 46 60 F3 0F 5C CA"); pattern.size() > 0)
                    {
                        // `movss xmm0, [esi+0x60]` is the five bytes the shim replaces;
                        // the resume is the instruction after them.
                        zpatch_driveby_resume = MatchAddress(pattern, 5);
                    }

                    if (auto pattern = find_pattern("08 0B C2 8B F0 E8"); pattern.size() > 0)
                    {
                        // The E8 at +5 calls the resolver; its rel32 operand is at +6.
                        zpatch_driveby_resolver = BranchDestination(
                            pattern.get_first<uint8_t>(), 6);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallDrivebyFOVFix");

                // DrivebyFOVFix: 1 sigjump
                if (Game::Option("DrivebyFOVFix", "1"))
                {
                    if (auto pattern = find_pattern("F3 0F 10 46 60 F3 0F 5C CA"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_driveby_fov_shim), true);
                    }
                }
            };
        }
    } drivebyFOVFix;


    // MouseFix

    namespace
    {
        // 66 0F 6E C0 = movd xmm0, eax, the SSE store the fix writes where the
        // x87 loads were.
        constexpr uint32_t kMovdXmm0Eax = 0xC06E0F66u;

        // The mouse axis sites, relative to the match: the 4-byte `cvtsi2ss`
        // that the fix replaces with a movd, and the 2-byte slot in front of it
        // (the `neg eax` at +3) that the fix NOPs out. The B sites are the
        // second axis in the same handler.
        constexpr ptrdiff_t kMouseSseStoreA = 5;   // cvtsi2ss xmm0/xmm1, eax
        constexpr ptrdiff_t kMouseSseStoreB = 33;  // the second axis
        constexpr ptrdiff_t kMouseSseNopA = 3;     // neg eax
        constexpr ptrdiff_t kMouseSseNopB = 31;    // the second axis

        // One x87-to-SSE transition: the store(s) are written before either NOP,
        // as in the original handler. `store_b`/`nop_b` are 0 for the single-axis
        // sites, where `store` is that site's own movd.
        void ApplyXmmTransition(uint8_t* match, uint32_t store,
                                ptrdiff_t store_a, ptrdiff_t store_b,
                                ptrdiff_t nop_a, ptrdiff_t nop_b)
        {
            injector::WriteMemory<uint32_t>(match + store_a, store, true);
            if (store_b != 0)
                injector::WriteMemory<uint32_t>(match + store_b, store, true);
            injector::MakeNOP(match + nop_a, 2, true);
            if (nop_b != 0)
                injector::MakeNOP(match + nop_b, 2, true);
        }
    } // namespace

    class MouseFix
    {
    public:
        MouseFix()
        {
            PublishEvent() += []
            {
                TraceArm("PublishMouseFix");

                // MouseFix: 6 slots
                if (Game::Option("MouseFix", "1"))
                {
                    // The two axis pointers are the `mov esi, imm32` / `mov edi, imm32`
                    // operands; the flag pointer is the `cmp [imm32], ...` operand.

                    if (auto pattern = find_pattern("51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C"); pattern.size() > 0)
                    {
                        zpatch_mf_x = *pattern.get_first<uint32_t>(13);
                        zpatch_mf_y = *pattern.get_first<uint32_t>(20);
                    }

                    if (auto pattern = find_pattern("8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D"); pattern.size() > 0)
                    {
                        zpatch_mf_flag = *pattern.get_first<uint32_t>(20);
                    }

                    if (auto pattern = find_pattern("83 EC 08 DB 05 ? ? ? ? 68"); pattern.size() > 0)
                    {
                        // The match is GET_MOUSE_SENSITIVITY's entry (sub esp,8; fld
                        // dword [imm32], game 0xB4D3A0); abi_mf_angle_step calls it
                        // through the slot.
                        zpatch_mf_time = MatchAddress(pattern);
                    }

                    if (auto pattern = find_pattern("53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46"); pattern.size() > 0)
                    {
                        // Body A is installed 15 bytes before the match and resumes at
                        // it; body B is installed at +1862 and resumes at +1878.
                        zpatch_mf_resume_a = MatchAddress(pattern);
                        zpatch_mf_resume_b = MatchAddress(pattern, 1878);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallMouseFix");

                // MouseFix: 8 siginstalls
                if (Game::Option("MouseFix", "1"))
                {
                    if (auto pattern = find_pattern("51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_mouse_axis_float), true);
                    }

                    if (auto pattern = find_pattern("8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_mouse_axis_neg), true);
                    }

                    if (auto pattern = find_pattern("83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07"); pattern.size() > 0)
                    {
                        ApplyXmmTransition(pattern.get_first<uint8_t>(), kMovdXmm0Eax,
                                           kMouseSseStoreA, kMouseSseStoreB,
                                           kMouseSseNopA, kMouseSseNopB);
                    }

                    if (auto pattern = find_pattern("83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07"); pattern.size() > 0)
                    {
                        ApplyXmmTransition(pattern.get_first<uint8_t>(), kMovdXmm0Eax,
                                           kMouseSseStoreA, kMouseSseStoreB,
                                           kMouseSseNopA, kMouseSseNopB);
                    }

                    if (auto pattern = find_pattern("83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46"); pattern.size() > 0)
                    {
                        // 66 0F 6E C8 = movd xmm1, eax: this site's SSE form.
                        ApplyXmmTransition(pattern.get_first<uint8_t>(), 0xC86E0F66u,
                                           kMouseSseStoreA, 0, kMouseSseNopA, 0);
                    }

                    if (auto pattern = find_pattern("8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_mouse_axis_flag), true);
                    }

                    if (auto pattern = find_pattern("56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00"); pattern.size() > 0)
                    {
                        // The two int-axis conversion calls in the function.
                        injector::MakeCALL(pattern.get_first<uint8_t>(51), OPP_BODY(abi_mouse_axis_int), true);
                        injector::MakeCALL(pattern.get_first<uint8_t>(90), OPP_BODY(abi_mouse_axis_int), true);
                    }

                    if (auto pattern = find_pattern("53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        // Body A is installed 15 bytes before the match (replaying its
                        // 15-byte prologue); body B at +1862 (replaying three bytes).
                        injector::MakeJMP(match - 15, OPP_BODY(abi_mouse_angle), true);
                        injector::MakeJMP(pattern.get_first<uint8_t>(1862), OPP_BODY(abi_mouse_advance), true);
                    }
                }
            };
        }
    } mouseFix;


    // VSyncFix

    class VSyncFix
    {
    public:
        VSyncFix()
        {
            InstallEvent() += []
            {
                TraceArm("InstallVSyncFix");

                // VSyncFix: 2 literalstores
                if (Game::Option("VSyncFix", "0"))
                {
                    if (BuildIs(Game::Get().build,
                                {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1061,
                                 GameBuild::v1100, GameBuild::v1110, GameBuild::v1120, GameBuild::v12043}))
                    {

                        // The two recovered operand slots the handler points at its
                        // VSync flag dword (the windows are pre-write).

                        if (auto pattern = find_pattern("8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74"); pattern.size() > 0)
                        {
                            injector::WriteMemory<uint32_t>(pattern.get_first<uint8_t>(30), static_cast<uint32_t>(
                                                     FindGlobalValue("VSyncFix")), true);
                        }

                        if (auto pattern = find_pattern("8B 10 50 8B 82 A8 00 00 00 FF D0 83"); pattern.size() > 0)
                        {
                            injector::WriteMemory<uint32_t>(pattern.get_first<uint8_t>(13), static_cast<uint32_t>(
                                                     FindGlobalValue("VSyncFix")), true);
                        }
                    }
                }
            };
        }
    } vSyncFix;


} // namespace opp::features

// 32-bit hook bodies
//
// The naked entry points input installs, in one section after the readable
// installers. Each is a register-exact transcription of its original body; its
// byte-exact marker names the original, its comment names the destination VA.
#if !defined(_WIN64)

extern "C"
{

// 0x100064b0 DrivebyFOVFix: computes the drive-by FOV once and feeds it to the
// resume as xmm0. The original is `pushal; call sub_10006450; popal;
// movss xmm0,[0x101580c4]; jmp [0x1015bdf4]`; sub_10006450 resolves the handle
// dword_101580D4 (salt 0x09399528) through dword_1015BDE0, walks two structures,
// caches the entry and reads the float at +0x60.
// The resolver result and cached entry are dereferenced unguarded, like the
// original (0x1000646E / 0x10006490); 45.0f is its own first instruction
// (0x10006457). Kept separate and naked so the shim's call keeps the stack shape.
static __declspec(naked) void abi_driveby_fov_helper()
{
    __asm {
        cmp BYTE PTR [zpatch_driveby_done], 0
        mov DWORD PTR [zpatch_driveby_fov], 42340000h  // 45.0f default
        jne drv_have
        mov eax, DWORD PTR [zpatch_salt_d4]
        push edx
        push eax
        call zpatch_driveby_resolve_seh
        add esp, 8
        // The resolver is SEH-wrapped (a null preamble pointer or a miss returns
        // 0); the original then dereferences that 0. The 45.0f default is kept
        // and the resume runs un-patched instead.
        test eax, eax
        je   drv_done
        mov ecx, DWORD PTR [eax + 0Eh]    // AVs when the handle did not resolve
        add eax, 0Eh
        add ecx, eax
        mov BYTE PTR [zpatch_driveby_done], 1
        mov eax, DWORD PTR [ecx + 0Fh]    // AVs on a garbage chain
        mov eax, DWORD PTR [eax + ecx + 14h]
        mov DWORD PTR [zpatch_driveby_cached], eax
        jmp drv_read
    drv_have:
        mov eax, DWORD PTR [zpatch_driveby_cached]
    drv_read:
        test eax, eax
        je   drv_done
        mov eax, DWORD PTR [eax]          // AVs when the cached entry is null
        test eax, eax
        je drv_done
        mov edx, OFFSET zpatch_driveby_fov
        __emit 0F3h
        __emit 0Fh
        __emit 10h
        __emit 40h
        __emit 60h                   // movss xmm0, DWORD PTR [eax + 0x60]
        __emit 0F3h
        __emit 0Fh
        __emit 11h
        __emit 02h                   // movss DWORD PTR [edx], xmm0
    drv_done:
        ret
    }
}
// 0x100064b0 shim: the original restores every GPR before the SSE load; EAX is
// scratch (pushed/popped), so no register the resume consumes is clobbered.
__declspec(naked) void abi_driveby_fov_shim()
{
    __asm {
        pushad
        call abi_driveby_fov_helper
        popad
        push eax
        mov eax, OFFSET zpatch_driveby_fov
        __emit 0F3h
        __emit 0Fh
        __emit 10h
        __emit 00h                   // movss xmm0, DWORD PTR [eax]
        pop eax
        jmp DWORD PTR [zpatch_driveby_resume]
    }
}

// MouseFix (callback 0x10024600): the four axis helpers the callback E9s the
// game's mouse-read functions to (0x10024060/C0/120/170), the two bodies its
// module scan installs (0x10024560 at match-0xF of game 0xA81B1F, 0x100243A0 at
// match+0x746 of the same scan) and the internal 0x10024440 helper it calls.
// Naked: they return in st(0), keep the xmm save/restore contract and round the
// doubles exactly. The one compiled helper (advance = original 0x100241E0) has
// no register contract.
// @exact 0x10024060 axis-float helper: X/Y * 0.0078125 returned in st(0).
__declspec(naked) void abi_mouse_axis_float()
{
    __asm {
        push ebp
        mov  ebp, esp
        mov  eax, dword ptr [ebp + 0Ch]
        sub  esp, 8
        test eax, eax
        jnz  mf_axis_float_y
        mov  eax, dword ptr [zpatch_mf_x]
        movd xmm0, dword ptr [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, qword ptr [zpatch_mf_scale]
        movsd qword ptr [ebp - 8], xmm0
        fld  qword ptr [ebp - 8]
        mov  esp, ebp
        pop  ebp
        ret
    mf_axis_float_y:
        cmp  eax, 1
        jnz  mf_axis_float_zero
        mov  eax, dword ptr [zpatch_mf_y]
        movd xmm0, dword ptr [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, qword ptr [zpatch_mf_scale]
        movsd qword ptr [ebp - 8], xmm0
        fld  qword ptr [ebp - 8]
        mov  esp, ebp
        pop  ebp
        ret
    mf_axis_float_zero:
        fldz
        mov  esp, ebp
        pop  ebp
        ret
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py
    }
}
// @exact 0x100240C0 negated axis helper: -127.5 * value.
__declspec(naked) void abi_mouse_axis_neg()
{
    __asm {
        push ebp
        mov  ebp, esp
        mov  eax, dword ptr [ebp + 0Ch]
        test eax, eax
        jnz  mf_axis_neg_y
        mov  eax, dword ptr [zpatch_mf_x]
    mf_axis_neg_xy:
        movd xmm0, dword ptr [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, qword ptr [zpatch_mf_scale]
        jmp  mf_axis_neg_scale
    mf_axis_neg_y:
        cmp  eax, 1
        jnz  mf_axis_neg_zero
        mov  eax, dword ptr [zpatch_mf_y]
        jmp  mf_axis_neg_xy
    mf_axis_neg_zero:
        xorps xmm0, xmm0
    mf_axis_neg_scale:
        movss xmm1, dword ptr [zpatch_mf_sign]
        xorps xmm1, xmmword ptr [zpatch_mf_neg_mask]
        cvtpd2ps xmm0, xmm0
        mulss xmm1, xmm0
        movss dword ptr [ebp + 0Ch], xmm1
        mov  eax, dword ptr [ebp + 0Ch]
        pop  ebp
        ret
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py
    }
}
// @exact 0x10024120 int axis helper: (int)(value * 0.0078125 * 255).
__declspec(naked) void abi_mouse_axis_int()
{
    __asm {
        push ebp
        mov  ebp, esp
        mov  eax, dword ptr [ebp + 0Ch]
        test eax, eax
        jnz  mf_axis_int_y
        mov  eax, dword ptr [zpatch_mf_x]
    mf_axis_int_xy:
        movd xmm0, dword ptr [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, qword ptr [zpatch_mf_scale]
        mulsd xmm0, qword ptr [zpatch_mf_scale_255]
        cvttsd2si eax, xmm0
        pop  ebp
        ret
    mf_axis_int_y:
        cmp  eax, 1
        jnz  mf_axis_int_zero
        mov  eax, dword ptr [zpatch_mf_y]
        jmp  mf_axis_int_xy
    mf_axis_int_zero:
        xorps xmm0, xmm0
        mulsd xmm0, qword ptr [zpatch_mf_scale_255]
        cvttsd2si eax, xmm0
        pop  ebp
        ret
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py
    }
}
// @exact 0x10024170 flagged float helper: negated variant that flips sign again
// when the published game byte is non-zero.
__declspec(naked) void abi_mouse_axis_flag()
{
    __asm {
        push ebp
        mov  ebp, esp
        mov  eax, dword ptr [ebp + 0Ch]
        test eax, eax
        jnz  mf_axis_flag_y
        mov  eax, dword ptr [zpatch_mf_x]
    mf_axis_flag_xy:
        movd xmm0, dword ptr [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, qword ptr [zpatch_mf_scale]
        jmp  mf_axis_flag_scale
    mf_axis_flag_y:
        cmp  eax, 1
        jnz  mf_axis_flag_zero
        mov  eax, dword ptr [zpatch_mf_y]
        jmp  mf_axis_flag_xy
    mf_axis_flag_zero:
        xorps xmm0, xmm0
    mf_axis_flag_scale:
        movss xmm1, dword ptr [zpatch_mf_sign]
        xorps xmm1, xmmword ptr [zpatch_mf_neg_mask]
        mov  eax, dword ptr [zpatch_mf_flag]
        cvtpd2ps xmm0, xmm0
        cmp  byte ptr [eax], 0
        mulss xmm1, xmm0
        movss dword ptr [ebp + 0Ch], xmm1
        jz   mf_axis_flag_store
        mulss xmm1, dword ptr [zpatch_mf_mul_neg1]
        movss dword ptr [ebp + 0Ch], xmm1
    mf_axis_flag_store:
        mov  eax, dword ptr [ebp + 0Ch]
        pop  ebp
        ret
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py
    }
}
// @exact 0x10024440 internal FPU helper called by abi_mouse_angle: advances the
// per-vehicle yaw (+0x148) and pitch (+0x144) by the mouse delta scaled by the
// game's mouse sensitivity (GET_MOUSE_SENSITIVITY, game 0xB4D3A0; its result is
// the slot named zpatch_mf_time), wrapping yaw into [-pi, pi]. Reads ECX (`this`).
static __declspec(naked) void abi_mf_angle_step()
{
    __asm {
        push ebp
        mov  ebp, esp
        and  esp, 0FFFFFFF8h
        sub  esp, 0Ch
        push esi
        mov  esi, ecx
        call dword ptr [zpatch_mf_time]
        movss xmm2, dword ptr [zpatch_mf_step_scale]
        mov  eax, dword ptr [zpatch_mf_x]
        fstp qword ptr [esp + 8]
        movsd xmm0, qword ptr [esp + 8]
        mulsd xmm0, qword ptr [zpatch_mf_time_mul]
        movd xmm4, dword ptr [eax]
        mov  eax, dword ptr [zpatch_mf_y]
        cvtdq2pd xmm4, xmm4
        addsd xmm0, qword ptr [zpatch_mf_time_add]
        movd xmm3, dword ptr [eax]
        mulsd xmm4, qword ptr [zpatch_mf_scale]
        mov  eax, dword ptr [zpatch_mf_flag]
        cvtdq2pd xmm3, xmm3
        cmp  byte ptr [eax], 0
        xorps xmm4, xmmword ptr [zpatch_mf_double_sign]
        mulsd xmm3, qword ptr [zpatch_mf_scale]
        cvtpd2ps xmm0, xmm0
        xorps xmm3, xmmword ptr [zpatch_mf_double_sign]
        mulss xmm2, xmm0
        mulss xmm2, dword ptr [esi + 60h]
        jz   mf_angle_step_skip
        mulsd xmm3, qword ptr [zpatch_mf_mul_neg1_d]
    mf_angle_step_skip:
        movss xmm0, dword ptr [esi + 144h]
        cvtps2pd xmm2, xmm2
        cvtps2pd xmm0, xmm0
        movaps xmm1, xmm2
        mulsd xmm2, xmm4
        mulsd xmm1, xmm3
        addsd xmm1, xmm0
        cvtpd2ps xmm0, xmm1
        movss dword ptr [esi + 144h], xmm0
        movss xmm0, dword ptr [esi + 148h]
        cvtps2pd xmm0, xmm0
        addsd xmm2, xmm0
        cvtpd2ps xmm0, xmm2
        cvtps2pd xmm1, xmm0
        comisd xmm1, qword ptr [zpatch_mf_pi]
        jbe  mf_angle_step_low
        subsd xmm1, qword ptr [zpatch_mf_two_pi]
        cvtpd2ps xmm0, xmm1
        movss dword ptr [esi + 148h], xmm0
        pop  esi
        mov  esp, ebp
        pop  ebp
        ret
    mf_angle_step_low:
        movsd xmm0, qword ptr [zpatch_mf_neg_pi]
        comisd xmm0, xmm1
        jbe  mf_angle_step_store
        addsd xmm1, qword ptr [zpatch_mf_two_pi]
    mf_angle_step_store:
        cvtpd2ps xmm0, xmm1
        movss dword ptr [esi + 148h], xmm0
        pop  esi
        mov  esp, ebp
        pop  ebp
        ret
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py
    }
}
// @exact 0x10024560 MouseFix body A (installed at game 0xA81B10): saves xmm0-7
// through the plugin's scratch slots, advances the angles, restores them,
// replays the hooked function's 15-byte prologue and tail-jumps to the resume
// the handler published (match, game 0xA81B1F).
__declspec(naked) void abi_mouse_angle()
{
    __asm {
        pushad
        movss dword ptr [zpatch_mf_xmm_save], xmm0
        movss dword ptr [zpatch_mf_xmm_save + 4], xmm1
        movss dword ptr [zpatch_mf_xmm_save + 8], xmm2
        movss dword ptr [zpatch_mf_xmm_save + 12], xmm3
        movss dword ptr [zpatch_mf_xmm_save + 16], xmm4
        movss dword ptr [zpatch_mf_xmm_save + 20], xmm5
        movss dword ptr [zpatch_mf_xmm_save + 24], xmm6
        movss dword ptr [zpatch_mf_xmm_save + 28], xmm7
        call abi_mf_angle_step
        movss xmm0, dword ptr [zpatch_mf_xmm_save]
        movss xmm1, dword ptr [zpatch_mf_xmm_save + 4]
        movss xmm2, dword ptr [zpatch_mf_xmm_save + 8]
        movss xmm3, dword ptr [zpatch_mf_xmm_save + 12]
        movss xmm4, dword ptr [zpatch_mf_xmm_save + 16]
        movss xmm5, dword ptr [zpatch_mf_xmm_save + 20]
        movss xmm6, dword ptr [zpatch_mf_xmm_save + 24]
        movss xmm7, dword ptr [zpatch_mf_xmm_save + 28]
        popad
        push ebp
        mov  ebp, esp
        and  esp, 0FFFFFFF0h
        xorps xmm0, xmm0
        sub  esp, 94h
        jmp  dword ptr [zpatch_mf_resume_a]
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py
    }
}
// @exact 0x100243A0 MouseFix body B (installed at game 0xA82265): same xmm
// save/restore around the advance helper (sub_100241E0), then replays the three
// replaced instructions and tail-jumps to the published resume (match+0x756,
// game 0xA82275).
__declspec(naked) void abi_mouse_advance()
{
    __asm {
        pushad
        movss dword ptr [zpatch_mf_xmm_save], xmm0
        movss dword ptr [zpatch_mf_xmm_save + 4], xmm1
        movss dword ptr [zpatch_mf_xmm_save + 8], xmm2
        movss dword ptr [zpatch_mf_xmm_save + 12], xmm3
        movss dword ptr [zpatch_mf_xmm_save + 16], xmm4
        movss dword ptr [zpatch_mf_xmm_save + 20], xmm5
        movss dword ptr [zpatch_mf_xmm_save + 24], xmm6
        movss dword ptr [zpatch_mf_xmm_save + 28], xmm7
        call zpatch_mf_advance_c
        movss xmm0, dword ptr [zpatch_mf_xmm_save]
        movss xmm1, dword ptr [zpatch_mf_xmm_save + 4]
        movss xmm2, dword ptr [zpatch_mf_xmm_save + 8]
        movss xmm3, dword ptr [zpatch_mf_xmm_save + 12]
        movss xmm4, dword ptr [zpatch_mf_xmm_save + 16]
        movss xmm5, dword ptr [zpatch_mf_xmm_save + 20]
        movss xmm6, dword ptr [zpatch_mf_xmm_save + 24]
        movss xmm7, dword ptr [zpatch_mf_xmm_save + 28]
        popad
        mulss xmm0, xmm2
        addss xmm0, xmm3
        movss xmm3, dword ptr [esi + 150h]
        jmp  dword ptr [zpatch_mf_resume_b]
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py
    }
}

}  // extern "C"

#endif  // !defined(_WIN64)
