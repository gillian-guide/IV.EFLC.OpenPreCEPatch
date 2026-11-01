// SPDX-License-Identifier: GPL-3.0-or-later
//
// input
//
// One function per INI arm, in the original loader order. The bodies are the
// readable reconstruction's verified arms; only the wrapper and the arm trace
// are ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#include "opcodes.hxx"

export module opp.input;

import opp.pipeline;
import opp.patch;
import opp.game;
import opp.common;
import opp.settings;
import opp.log;

namespace opp::features
{

    // BorderlessWindowed / DoNotPauseOnMinimize live in the separate
    // XLivelessAddon companion repository.


    // publish DrivebyFOVFix

    void PublishDrivebyFOVFix(PatchContext& ctx)
    {

        TraceArm("publish DrivebyFOVFix");

        // DrivebyFOVFix: 2 slots
        if (ctx.Option("DrivebyFOVFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F3 0F 10 46 60 F3 0F 5C CA"); pattern.size() > 0)
            {
                zpatch_driveby_resume = MatchAddress(pattern, 5);
            }

            if (auto pattern = Pattern(image, "08 0B C2 8B F0 E8"); pattern.size() > 0)
            {
                zpatch_driveby_resolver = MatchTarget(pattern, 6, 10);
            }
        }

    }


    // DrivebyFOVFix

    void InstallDrivebyFOVFix(PatchContext& ctx)
    {

        TraceArm("DrivebyFOVFix");

        // DrivebyFOVFix: 1 sigjump
        if (ctx.Option("DrivebyFOVFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F3 0F 10 46 60 F3 0F 5C CA"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_driveby_fov_shim");
            }
        }

    }


    // publish MouseFix

    void PublishMouseFix(PatchContext& ctx)
    {

        TraceArm("publish MouseFix");

        // MouseFix: 6 slots
        if (ctx.Option("MouseFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C"); pattern.size() > 0)
            {
                zpatch_mf_x = *pattern.get(0).get<uint32_t>(13);
                zpatch_mf_y = *pattern.get(0).get<uint32_t>(20);
            }

            if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D"); pattern.size() > 0)
            {
                zpatch_mf_flag = *pattern.get(0).get<uint32_t>(20);
            }

            if (auto pattern = Pattern(image, "83 EC 08 DB 05 ? ? ? ? 68"); pattern.size() > 0)
            {
                zpatch_mf_time = MatchAddress(pattern);
            }

            if (auto pattern = Pattern(image, "53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46"); pattern.size() > 0)
            {
                zpatch_mf_resume_a = MatchAddress(pattern);
                zpatch_mf_resume_b = MatchAddress(pattern, 1878);
            }
        }

    }


    // MouseFix

    void InstallMouseFix(PatchContext& ctx)
    {

        TraceArm("MouseFix");

        // MouseFix: 8 siginstalls
        if (ctx.Option("MouseFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_mouse_axis_float");
            }

            if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_mouse_axis_neg");
            }

            if (auto pattern = Pattern(image, "83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // 66 0F 6E C0 = `movd xmm0, eax`: the SSE form the fix puts
                // where the x87 load was (the NOPs at +3/+31 remove the old one).
                constexpr opp::ops::InstructionWindow<4> kMovdXmm0Eax{
                    {0x66, 0x0F, 0x6E, 0xC0}};
                static_assert(opp::ops::PackedLowDword(kMovdXmm0Eax) == 0xC06E0F66u);
                WriteValue<uint32_t>(ctx, match + 5,
                                     opp::ops::PackedLowDword(kMovdXmm0Eax));
                WriteValue<uint32_t>(ctx, match + 33,
                                     opp::ops::PackedLowDword(kMovdXmm0Eax));
                NopAt(ctx, match + 3, 2);
                NopAt(ctx, match + 31, 2);
            }

            if (auto pattern = Pattern(image, "83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // 66 0F 6E C0 = `movd xmm0, eax`: the SSE form the fix puts
                // where the x87 load was (the NOPs at +3/+31 remove the old one).
                constexpr opp::ops::InstructionWindow<4> kMovdXmm0Eax{
                    {0x66, 0x0F, 0x6E, 0xC0}};
                static_assert(opp::ops::PackedLowDword(kMovdXmm0Eax) == 0xC06E0F66u);
                WriteValue<uint32_t>(ctx, match + 5,
                                     opp::ops::PackedLowDword(kMovdXmm0Eax));
                WriteValue<uint32_t>(ctx, match + 33,
                                     opp::ops::PackedLowDword(kMovdXmm0Eax));
                NopAt(ctx, match + 3, 2);
                NopAt(ctx, match + 31, 2);
            }

            if (auto pattern = Pattern(image, "83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // 66 0F 6E C8 = `movd xmm1, eax` (the second axis).
                constexpr opp::ops::InstructionWindow<4> kMovdXmm1Eax{
                    {0x66, 0x0F, 0x6E, 0xC8}};
                static_assert(opp::ops::PackedLowDword(kMovdXmm1Eax) == 0xC86E0F66u);
                WriteValue<uint32_t>(ctx, match + 5,
                                     opp::ops::PackedLowDword(kMovdXmm1Eax));
                NopAt(ctx, match + 3, 2);
            }

            if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match, "abi_mouse_axis_flag");
            }

            if (auto pattern = Pattern(image, "56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 51, "abi_mouse_axis_int");
                CallToBody(ctx, match + 90, "abi_mouse_axis_int");
            }

            if (auto pattern = Pattern(image, "53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 15, "abi_mouse_angle");
                JumpToBody(ctx, match + 1862, "abi_mouse_advance");
            }
        }

    }


    // VSyncFix

    void InstallVSyncFix(PatchContext& ctx)
    {

        TraceArm("VSyncFix");

        // VSyncFix: 2 literalstores
        if (ctx.Option("VSyncFix", "0"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1061,
                         GameBuild::v1100, GameBuild::v1110, GameBuild::v1120, GameBuild::v12043}))
            {

                if (auto pattern = Pattern(image, "8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    WriteValue<uint32_t>(ctx, match + 30, static_cast<uint32_t>(FindGlobalValue("VSyncFix")));
                }

                if (auto pattern = Pattern(image, "8B 10 50 8B 82 A8 00 00 00 FF D0 83"); pattern.size() > 0)
                {
                    auto* match = pattern.get(0).get<uint8_t>();

                    WriteValue<uint32_t>(ctx, match + 13, static_cast<uint32_t>(FindGlobalValue("VSyncFix")));
                }
            }
        }
    }

namespace
{
    class InputFeatures
    {
    public:
        InputFeatures()
        {
            RegisterFeature({Phase::publish, "PublishDrivebyFOVFix", &PublishDrivebyFOVFix});
            RegisterFeature({Phase::publish, "PublishMouseFix", &PublishMouseFix});
            RegisterFeature({Phase::install, "InstallDrivebyFOVFix", &InstallDrivebyFOVFix, {"PublishDrivebyFOVFix"}});
            RegisterFeature({Phase::install, "InstallMouseFix", &InstallMouseFix, {"PublishMouseFix"}});
            RegisterFeature({Phase::install, "InstallVSyncFix", &InstallVSyncFix});
        }
    } inputFeatures;
} // namespace

} // namespace opp::features

// ---------------------------------------------------------------------------
// Hook bodies: the machine-level entry points input installs.
//
// Each body keeps the original site's calling convention and register
// contract; the comment above it names the original destination VA. They are
// 32-bit only (the x64 parity dump resolves names to the original VAs).
// ---------------------------------------------------------------------------
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

extern "C"
{
constexpr uint32_t kModelTablePointerRva = 0x150FDD0u;
constexpr uint32_t kModelTableCountRva = 0x150FDD4u;
// One-shot diagnostic for DrivebyFOVFix: scans the resolver's registry table
// (base+0x150FDD0/count 0x150FDD4, build-9 RVAs) for the key 0x9399528. Naked so
// the shim can call it without disturbing the live xmm1..xmm7 (xmm0 is scratch).
// The scan result is not consumed.
extern "C" void abi_driveby_fov_note_impl()
{
    static bool logged = false;
    if (logged) return;
    logged = true;
    const uintptr_t game_base =
        static_cast<uintptr_t>(zpatch_driveby_resume) - 5u - 0x85031Fu;  // build-9 sig RVA
    uint32_t count = 0;
    uint32_t table = 0;
    // Sanity bounds for a mapped 32-bit image base: above the DOS header area
    // and below the user-mode limit.
    constexpr uintptr_t kImageBaseMin = 0x10000u;
    constexpr uintptr_t kImageBaseMax = 0x80000000u;
    if (game_base > kImageBaseMin && game_base < kImageBaseMax)
    {
        count = *reinterpret_cast<const uint32_t*>(game_base + kModelTableCountRva);
        table = *reinterpret_cast<const uint32_t*>(game_base + kModelTablePointerRva);
        // The resolver registry is a pointer plus a count; the caps reject a
        // half-initialised table (the original scans it unguarded).
        constexpr uint32_t kModelTableMin = 0x10000u;
        constexpr uint32_t kModelTableCountMax = 0x100000u;
        if (table > kModelTableMin && count > 0u && count < kModelTableCountMax)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t key = *reinterpret_cast<const uint32_t*>(
                    static_cast<uintptr_t>(table) + i * 8u);
                // The model the diagnostic looks for (the drive-by pose model).
                constexpr uint32_t kDrivebyModelHash = 0x9399528u;
                if (key == kDrivebyModelHash) break;
            }
        }
    }
}
static __declspec(naked) void abi_driveby_fov_note()
{
    __asm {
        sub esp, 70h
        __emit 0Fh
        __emit 11h
        __emit 4Ch
        __emit 24h
        __emit 00h                    // movups [esp], xmm1
        __emit 0Fh
        __emit 11h
        __emit 54h
        __emit 24h
        __emit 10h                    // movups [esp+0x10], xmm2
        __emit 0Fh
        __emit 11h
        __emit 5Ch
        __emit 24h
        __emit 20h                    // movups [esp+0x20], xmm3
        __emit 0Fh
        __emit 11h
        __emit 64h
        __emit 24h
        __emit 30h                    // movups [esp+0x30], xmm4
        __emit 0Fh
        __emit 11h
        __emit 6Ch
        __emit 24h
        __emit 40h                    // movups [esp+0x40], xmm5
        __emit 0Fh
        __emit 11h
        __emit 74h
        __emit 24h
        __emit 50h                    // movups [esp+0x50], xmm6
        __emit 0Fh
        __emit 11h
        __emit 7Ch
        __emit 24h
        __emit 60h                    // movups [esp+0x60], xmm7
        call abi_driveby_fov_note_impl
        __emit 0Fh
        __emit 10h
        __emit 4Ch
        __emit 24h
        __emit 00h                    // movups xmm1, [esp]
        __emit 0Fh
        __emit 10h
        __emit 54h
        __emit 24h
        __emit 10h                    // movups xmm2, [esp+0x10]
        __emit 0Fh
        __emit 10h
        __emit 5Ch
        __emit 24h
        __emit 20h                    // movups xmm3, [esp+0x20]
        __emit 0Fh
        __emit 10h
        __emit 64h
        __emit 24h
        __emit 30h                    // movups xmm4, [esp+0x30]
        __emit 0Fh
        __emit 10h
        __emit 6Ch
        __emit 24h
        __emit 40h                    // movups xmm5, [esp+0x40]
        __emit 0Fh
        __emit 10h
        __emit 74h
        __emit 24h
        __emit 50h                    // movups xmm6, [esp+0x50]
        __emit 0Fh
        __emit 10h
        __emit 7Ch
        __emit 24h
        __emit 60h                    // movups xmm7, [esp+0x60]
        add esp, 70h
        ret
    }
}

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
static __declspec(naked) void abi_driveby_fov_shim()
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
// ---------------------------------------------------------------------------
static void abi_mf_angle_step();

namespace
{
void zpatch_mf_advance_c() noexcept
{
    // sub_100241E0: when both mouse axes are zero, patch three 8-byte windows of
    // the resumed game function (dword_10159C40 + 52/94/107) with the "pull"
    // constants; otherwise NOP-ish them out. VirtualProtect failures are ignored
    // like the original (a null resume would fault; unreachable once installed).
    const auto base = static_cast<uintptr_t>(zpatch_mf_resume_b);
    // sub_100241E0 dereferences the resume slot unconditionally.
    const auto* x = reinterpret_cast<const int32_t*>(
        static_cast<uintptr_t>(zpatch_mf_x));
    const auto* y = reinterpret_cast<const int32_t*>(
        static_cast<uintptr_t>(zpatch_mf_y));
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
    constexpr uintptr_t kMouseAdvancePitchWindow = 52u;
    constexpr uintptr_t kMouseAdvanceYawWindow = 94u;
    constexpr uintptr_t kMouseAdvanceStoreWindow = 107u;

    // The fields the pull angle lands in: pitch first, yaw one float later.
    constexpr uint32_t kVehiclePitchField = 0x144u;
    constexpr uint32_t kVehicleYawField = kVehiclePitchField + sizeof(float);
    static_assert(kVehicleYawField == 0x148u);

    constexpr opp::ops::InstructionWindow<8> kWritePitch =
        opp::ops::MovssFieldStore(0x86, kVehiclePitchField);  // movss [esi+pitch], xmm0
    static_assert(opp::ops::PackedLowDword(kWritePitch) == 0x86110FF3u);
    static_assert(opp::ops::PackedHighDword(kWritePitch) == kVehiclePitchField);

    constexpr opp::ops::InstructionWindow<8> kWriteYaw =
        opp::ops::MovssFieldStore(0x8E, kVehicleYawField);  // movss [esi+yaw], xmm1
    static_assert(opp::ops::PackedLowDword(kWriteYaw) == 0x8E110FF3u);
    static_assert(opp::ops::PackedHighDword(kWriteYaw) == kVehicleYawField);

    // fstp dword ptr [esi+yaw] followed by the first two bytes of the next
    // instruction (8B 5C = mov ebx, [esp+...]).
    // `fstp dword ptr [esi+0x148]` followed by the next instruction's bytes.
    constexpr opp::ops::InstructionWindow<8> kStoreYaw{
        {0xD9, 0x9E, 0x48, 0x01, 0x00, 0x00, 0x8B, 0x5C}};
    static_assert(opp::ops::PackedLowDword(kStoreYaw) == 0x01489ED9u);
    static_assert(opp::ops::PackedHighDword(kStoreYaw) == 0x5C8B0000u);

    // fstp st(0) + four NOPs + the same next-instruction bytes.
    // `fstp st(0)` + four NOPs + the same next-instruction bytes.
    constexpr opp::ops::InstructionWindow<8> kDiscardYaw{
        {0xDD, 0xD8, 0x90, 0x90, 0x90, 0x90, 0x8B, 0x5C}};
    static_assert(opp::ops::PackedLowDword(kDiscardYaw) == 0x9090D8DDu);
    static_assert(opp::ops::PackedHighDword(kDiscardYaw) == 0x5C8B9090u);

    constexpr opp::ops::InstructionWindow<8> kNop = opp::ops::NopWindow<8>();
    struct Window final
    {
        uintptr_t offset;
        opp::ops::InstructionWindow<8> whenZero;
        opp::ops::InstructionWindow<8> otherwise;
    };
    const Window windows[3] = {
        {kMouseAdvancePitchWindow, kWritePitch, kNop},
        {kMouseAdvanceYawWindow, kWriteYaw, kNop},
        {kMouseAdvanceStoreWindow, kStoreYaw, kDiscardYaw},
    };
    for (const Window& window : windows)
    {
        const opp::ops::InstructionWindow<8>& value = zero ? window.whenZero : window.otherwise;
        injector::WriteMemoryRaw(injector::raw_ptr(base + window.offset),
                                 const_cast<uint8_t*>(value.bytes.data()), value.Size(), true);
    }
    zpatch_cookie_tail();
}
}  // namespace

// @exact 0x10024060 axis-float helper: X/Y * 0.0078125 returned in st(0).
static __declspec(naked) void abi_mouse_axis_float()
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
        __emit 0CCh
    }
}
// @exact 0x100240C0 negated axis helper: -127.5 * value.
static __declspec(naked) void abi_mouse_axis_neg()
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
        __emit 0CCh
    }
}
// @exact 0x10024120 int axis helper: (int)(value * 0.0078125 * 255).
static __declspec(naked) void abi_mouse_axis_int()
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
        __emit 0CCh
    }
}
// @exact 0x10024170 flagged float helper: negated variant that flips sign again
// when the published game byte is non-zero.
static __declspec(naked) void abi_mouse_axis_flag()
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
        __emit 0CCh
    }
}
// @exact 0x10024440 internal FPU helper called by abi_mouse_angle: advances the
// per-vehicle yaw (+0x148) and pitch (+0x144) by the mouse delta scaled by the
// frame-time factor, wrapping yaw into [-pi, pi]. Reads ECX (`this`).
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
        __emit 0CCh
    }
}
// @exact 0x10024560 MouseFix body A (installed at game 0xA81B10): saves xmm0-7
// through the plugin's scratch slots, advances the angles, restores them,
// replays the hooked function's 15-byte prologue and tail-jumps to the resume
// the handler published (match, game 0xA81B1F).
static __declspec(naked) void abi_mouse_angle()
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
        __emit 0CCh
    }
}
// @exact 0x100243A0 MouseFix body B (installed at game 0xA82265): same xmm
// save/restore around the advance helper (sub_100241E0), then replays the three
// replaced instructions and tail-jumps to the published resume (match+0x756,
// game 0xA82275).
static __declspec(naked) void abi_mouse_advance()
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
        __emit 0CCh
    }
}
}  // extern "C"

namespace
{
// The name -> body rows this module registers. The reference VA is the
// original handler destination (0 when the body has none); it is provenance,
// not an install address.
const opp::HookBodyRow kInputHookBodies[] = {
    {"abi_driveby_fov_helper", 0u, reinterpret_cast<opp::HookBody>(&abi_driveby_fov_helper)},
    {"abi_driveby_fov_note", 0u, reinterpret_cast<opp::HookBody>(&abi_driveby_fov_note)},
    {"abi_driveby_fov_note_impl", 0u, reinterpret_cast<opp::HookBody>(&abi_driveby_fov_note_impl)},
    {"abi_driveby_fov_shim", 0x100064B0u, reinterpret_cast<opp::HookBody>(&abi_driveby_fov_shim)},
    {"abi_mf_angle_step", 0x10024440u, reinterpret_cast<opp::HookBody>(&abi_mf_angle_step)},
    {"abi_mouse_advance", 0x100243A0u, reinterpret_cast<opp::HookBody>(&abi_mouse_advance)},
    {"abi_mouse_angle", 0x10024560u, reinterpret_cast<opp::HookBody>(&abi_mouse_angle)},
    {"abi_mouse_axis_flag", 0x10024170u, reinterpret_cast<opp::HookBody>(&abi_mouse_axis_flag)},
    {"abi_mouse_axis_float", 0x10024060u, reinterpret_cast<opp::HookBody>(&abi_mouse_axis_float)},
    {"abi_mouse_axis_int", 0x10024120u, reinterpret_cast<opp::HookBody>(&abi_mouse_axis_int)},
    {"abi_mouse_axis_neg", 0x100240C0u, reinterpret_cast<opp::HookBody>(&abi_mouse_axis_neg)},
};

struct InputHookBodyRegistration
{
    InputHookBodyRegistration() noexcept
    {
        opp::RegisterHookBodies(kInputHookBodies);
    }
} g_inputHookBodies;
}  // namespace

#endif  // !defined(_WIN64)
