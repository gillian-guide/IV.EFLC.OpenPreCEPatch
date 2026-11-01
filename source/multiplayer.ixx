// SPDX-License-Identifier: GPL-3.0-or-later
//
// multiplayer
//
// One function per INI arm, in the original loader order. The bodies are the
// readable reconstruction's verified arms; only the wrapper and the arm trace
// are ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#include "opcodes.hxx"

#include <algorithm>

export module opp.multiplayer;

import opp.pipeline;
import opp.patch;
import opp.game;
import opp.common;
import opp.catalogue_data;
import opp.settings;
import opp.log;

namespace opp::features
{

    // BetterMPSync

    void InstallBetterMPSync(PatchContext& ctx)
    {

        TraceArm("BetterMPSync");

        // BetterMPSync: 1 siginstall
        if (ctx.Option("BetterMPSync", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 90 E8 00 00 00 55 8B CE FF"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 6, "abi_better_mp_sync");
            }
        }

    }


    // publish FreezeCarFix

    void PublishFreezeCarFix(PatchContext& ctx)
    {

        TraceArm("publish FreezeCarFix");

        // FreezeCarFix: 12 slots
        if (ctx.Option("FreezeCarFix", "1") &&
            Pattern(ctx.image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0"); pattern.size() > 0)
            {
                zpatch_spawns_orig_a = MatchTarget(pattern, 8, 12);
            }

            if (auto pattern = Pattern(image, "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B"); pattern.size() > 0)
            {
                zpatch_spawns_orig_b = MatchTarget(pattern, 12, 16);
            }

            if (auto pattern = Pattern(image, "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00"); pattern.size() > 0)
            {
                zpatch_spawns_case9_resume = MatchTarget(pattern, 247, 251);
                zpatch_spawns_case6_resume = MatchTarget(pattern, 181, 185);
                zpatch_spawns_case1_resume = MatchTarget(pattern, 62, 66);
                zpatch_spawns_case2_resume = MatchTarget(pattern, 87, 91);
                zpatch_spawns_case4_resume = MatchTarget(pattern, 137, 141);
                zpatch_spawns_case7_resume = MatchTarget(pattern, 203, 207);
                zpatch_spawns_case3_resume = MatchTarget(pattern, 112, 116);
                zpatch_spawns_case5_resume = MatchTarget(pattern, 159, 163);
                zpatch_spawns_case8_resume = MatchTarget(pattern, 225, 229);
                zpatch_spawns_case0_resume = MatchTarget(pattern, 37, 41);
            }
        }

    }


    // FreezeCarFix

    void InstallFreezeCarFix(PatchContext& ctx)
    {

        TraceArm("FreezeCarFix");

        // FreezeCarFix: 7 siginstalls
        if (ctx.Option("FreezeCarFix", "1") &&
            Pattern(ctx.image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 7, "abi_spawns_probe");
            }

            if (auto pattern = Pattern(image, "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 11, "abi_spawns_guard");
            }

            if (auto pattern = Pattern(image, "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // Each offset is the call instruction of one jump-table case;
                // the body name carries that case's value (0x24..0xF6 are the
                // case ids the original's dispatch compares).
                struct SpawnCase final
                {
                    uintptr_t call_offset;
                    const char* body;
                };
                constexpr SpawnCase kSpawnCases[] = {
                    {36, "abi_spawns_case_0x24"},
                    {61, "abi_spawns_case_0x3D"},
                    {86, "abi_spawns_case_0x56"},
                    {111, "abi_spawns_case_0x6F"},
                    {136, "abi_spawns_case_0x88"},
                    {158, "abi_spawns_case_0x9E"},
                    {180, "abi_spawns_case_0xB4"},
                    {202, "abi_spawns_case_0xCA"},
                    {224, "abi_spawns_case_0xE0"},
                    {246, "abi_spawns_case_0xF6"},
                };
                for (const SpawnCase& spawn : kSpawnCases)
                    CallToBody(ctx, match + spawn.call_offset, spawn.body);
            }

            if (auto pattern = Pattern(image, "88 87 40 01 00 00 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 6, "abi_spawns_probe");
            }

            if (auto pattern = Pattern(image, "88 5C 24 1F 8B 54 24 28 52 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 9, "abi_spawns_probe");
                CallToBody(ctx, match + 254, "abi_spawns_probe");
            }

            if (auto pattern = Pattern(image, "38 98 8C 00 00 00 75 2D 57 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 9, "abi_spawns_probe");
            }

            if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallToBody(ctx, match + 7, "abi_spawns_probe");
            }
        }

    }


    // publish FreezeGunFix

    void PublishFreezeGunFix(PatchContext& ctx)
    {

        TraceArm("publish FreezeGunFix");

        // FreezeGunFix: 1 slot
        if (ctx.Option("FreezeGunFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF"); pattern.size() > 0)
            {
                zpatch_freeze_gun_resume = MatchAddress(pattern, -12);
            }
        }

    }


    // FreezeGunFix

    void InstallFreezeGunFix(PatchContext& ctx)
    {

        TraceArm("FreezeGunFix");

        // FreezeGunFix: 1 siginstall
        if (ctx.Option("FreezeGunFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpToBody(ctx, match - 21, "abi_freeze_gun_shim");
            }
        }

    }


    // RemoveAllWeaponsProtection

    void InstallRemoveAllWeaponsProtection(PatchContext& ctx)
    {

        TraceArm("RemoveAllWeaponsProtection");

        // RemoveAllWeaponsProtection: 1 literal
        if (ctx.Option("RemoveAllWeaponsProtection", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E9 ? ? ? ? 57 8D 8F ? ? 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match + 5, 12);
            }
        }

    }


    // TeleportProtection

    void InstallTeleportProtection(PatchContext& ctx)
    {

        TraceArm("TeleportProtection");

        // TeleportProtection: 1 literal
        if (ctx.Option("TeleportProtection", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // `ret 8`: C2 08 00, through the guarded helper.
                MakeReturn(ctx, match, 8);
            }
        }

    }

namespace
{
    class MultiplayerFeatures
    {
    public:
        MultiplayerFeatures()
        {
            RegisterFeature({Phase::publish, "PublishFreezeGunFix", &PublishFreezeGunFix});
            RegisterFeature({Phase::publish, "PublishFreezeCarFix", &PublishFreezeCarFix});
            RegisterFeature({Phase::install, "InstallBetterMPSync", &InstallBetterMPSync});
            RegisterFeature({Phase::install, "InstallFreezeGunFix", &InstallFreezeGunFix, {"PublishFreezeGunFix"}});
            RegisterFeature({Phase::install, "InstallRemoveAllWeaponsProtection", &InstallRemoveAllWeaponsProtection});
            RegisterFeature({Phase::install, "InstallTeleportProtection", &InstallTeleportProtection});
            RegisterFeature({Phase::install, "InstallFreezeCarFix", &InstallFreezeCarFix, {"PublishFreezeCarFix"}});
        }
    } multiplayerFeatures;
} // namespace

} // namespace opp::features

// ---------------------------------------------------------------------------
// The multiplayer model-name catalogue (folded in from catalogue.*): the hash
// index over the generated table in opp.catalogue_data. The spawn diagnostics
// print names from here; the index is built once.
// ---------------------------------------------------------------------------

export namespace opp
{
    size_t ModelCatalogSize() noexcept;
    std::string_view ModelCatalogName(size_t index) noexcept;

    // 0x10003F00: first match in the original table order; "**UNKNOWN**" on a
    // miss.
    std::string_view ModelNameFromHash(uint32_t hash) noexcept;
}

namespace opp
{
    namespace
    {
        // ModelNameIndex: sorted (hash, first table index) pairs. The original
        // scans all 38,455 names per diagnostic lookup; the table is static, so
        // build the index once. A fixed array keeps this free of CRT/TLS-backed
        // static initialisation, which is unsafe before the ASI's CRT is ready.
        constexpr size_t kModelIndexCapacity = 40000;

        struct ModelHashEntry
        {
            uint32_t hash;
            uint32_t index;
        };

        ModelHashEntry g_modelIndex[kModelIndexCapacity];
        size_t g_modelIndexCount = 0;
        volatile LONG g_modelIndexState = 0;  // 0 uninitialised, 1 building, 2 ready

        bool BuildModelHashIndex() noexcept
        {
            for (;;)
            {
                const LONG observed =
                    InterlockedCompareExchange(&g_modelIndexState, 1, 0);
                if (observed == 2)
                    return g_modelIndexCount != 0;
                if (observed == 1)
                {
                    Sleep(0);
                    continue;
                }
                const size_t count = ModelNameCount();
                if (count > kModelIndexCapacity)
                {
                    InterlockedExchange(&g_modelIndexState, 2);
                    return false;
                }
                for (size_t i = 0; i < count; ++i)
                    g_modelIndex[i] = {GameHash(ModelNameAt(i)),
                                       static_cast<uint32_t>(i)};
                std::sort(g_modelIndex, g_modelIndex + count,
                          [](const ModelHashEntry& left, const ModelHashEntry& right) {
                              if (left.hash != right.hash) return left.hash < right.hash;
                              return left.index < right.index;
                          });
                g_modelIndexCount = count;
                InterlockedExchange(&g_modelIndexState, 2);
                return true;
            }
        }
    } // namespace

    size_t ModelCatalogSize() noexcept
    {
        return ModelNameCount();
    }

    std::string_view ModelCatalogName(size_t index) noexcept
    {
        const char* name = ModelNameAt(index);
        return name != nullptr ? std::string_view(name) : "**UNKNOWN**";
    }

    std::string_view ModelNameFromHash(uint32_t hash) noexcept
    {
        if (BuildModelHashIndex())
        {
            size_t low = 0;
            size_t high = g_modelIndexCount;
            while (low < high)
            {
                const size_t mid = low + (high - low) / 2;
                if (g_modelIndex[mid].hash < hash)
                    low = mid + 1;
                else
                    high = mid;
            }
            if (low < g_modelIndexCount && g_modelIndex[low].hash == hash)
            {
                const char* name = ModelNameAt(g_modelIndex[low].index);
                return name != nullptr ? std::string_view(name) : "**UNKNOWN**";
            }
            return "**UNKNOWN**";
        }

        // Index unavailable (catalogue grew past the fixed capacity): keep the
        // original lookup as the fallback.
        const size_t count = ModelNameCount();
        for (size_t i = 0; i < count; ++i)
        {
            if (GameHash(ModelNameAt(i)) == hash)
                return ModelNameAt(i);
        }
        return "**UNKNOWN**";
    }
} // namespace opp

// ---------------------------------------------------------------------------
// Hook bodies: the machine-level entry points multiplayer installs.
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

// The MP-only diagnostic name lookup of sub_10003F00 walks the original ASI's
// own 38455-entry model-name table (0x100DE770); single-player never reaches
// this path (the caller is gated on GTAC.dll).
extern "C" const char* zpatch_bpm_model_name(uint32_t hash) noexcept
{
    // Return the first table name whose GameHash matches; "**UNKNOWN**" on a miss.
    return ModelNameFromHash(hash).data();
}

// FreezeCarFix / DebugPrintSpawns helper shims: the original's helpers take
// their single argument in ECX or EDX. The probe body calls these by name.
static const char* zpatch_spawns_name_ecx();  // sub_10003F00 twin, arg in ECX
static __declspec(naked) const char* zpatch_spawns_name_ecx()
{
    __asm {
        push ecx
        call zpatch_bpm_model_name
        add  esp, 4
        ret
    }
}

static const char* zpatch_spawns_who_edx();  // sub_1005B290 twin, arg in EDX
namespace
{
const char* zpatch_spawns_who_c(uint32_t id) noexcept
{
    // The original reads the native hash from [0x101580C8] (zpatch_salt_c8); the
    // loader's probe-miss fallback rewrites that slot on 1000/1010/1040.
    return reinterpret_cast<const char*>(static_cast<uintptr_t>(
        zpatch_script_call(zpatch_salt_c8, 1, &id)));
}
}  // namespace
static __declspec(naked) const char* zpatch_spawns_who_edx()
{
    __asm {
        push edx
        call zpatch_spawns_who_c
        add  esp, 4
        ret
    }
}

static void zpatch_spawns_print_edx();  // sub_1005B530 twin, arg in EDX
namespace
{
void zpatch_spawns_print_c(const char* text) noexcept
{
    const uint32_t arg = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(text));
    zpatch_script_call(kPrintScriptHash, 1, &arg);
}
}  // namespace
static __declspec(naked) void zpatch_spawns_print_edx()
{
    __asm {
        push edx
        call zpatch_spawns_print_c
        add  esp, 4
        ret
    }
}

// The spawn probe's GetModuleHandleW call goes through our own slot (MSVC's
// inline assembler cannot name the @-decorated import pointer).
struct SpawnsGetModuleHandleInit final
{
    SpawnsGetModuleHandleInit() noexcept
    {
        zpatch_spawns_getmodulehandlew = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&GetModuleHandleW));
    }
};
SpawnsGetModuleHandleInit g_spawns_getmodulehandle_init;

// @exact 0x100057f0 BetterMPSync: xor eax,eax / ret.
int __cdecl abi_better_mp_sync() { return 0; }

// @exact 0x10036eb0 FreezeGunFix: eax = object; [eax+0x38] is the ammo/clip
// pointer and [eax+0x40] the handle. If the handle is valid (not the debug-heap
// poison 0xCDCDCDCD, not -1, not 0) push edi and tail-jump into the original
// path, else return 0. The null path pops esi and does ret 8 because the site
// sits just after the enclosing function's push esi.
static __declspec(naked) void abi_freeze_gun_shim()
{
    __asm {
        mov ecx, DWORD PTR [eax + 0x38]
        test ecx, ecx
        mov esi, DWORD PTR [eax + 0x40]
        cmp esi, 0xCDCDCDCD
        je freeze_gun_null
        cmp esi, -1
        je freeze_gun_null
        cmp esi, 0
        je freeze_gun_null
        push edi
        jmp DWORD PTR [zpatch_freeze_gun_resume]
    freeze_gun_null:
        xor eax, eax
        pop esi
        ret 8
    }
}

// FreezeCarFix / DebugPrintSpawns bodies: the sub_100372F0 guard, the ten
// factory-dispatch thunks and the sub_10037510 spawn probe (installer 0x10037C70;
// the FreezeCarFix loader arm gates the install).
// @exact 0x100372F0
static __declspec(naked) void abi_spawns_guard()
{
    // Keeps the original callee's EBP frame and ret 0Ch cleanup, so it stays naked.
    __asm {
        push ebp
        mov ebp, esp
        cmp dword ptr [zpatch_spawns_skip], 0
        jne spawns_guard_taken
        push dword ptr [ebp + 10h]
        mov eax, dword ptr [zpatch_spawns_orig_b]
        push dword ptr [ebp + 0Ch]
        push dword ptr [ebp + 8]
        call eax
        mov dword ptr [zpatch_spawns_skip], 0
        pop ebp
        ret 0Ch
    spawns_guard_taken:
        xor eax, eax
        mov dword ptr [zpatch_spawns_skip], eax
        pop ebp
        ret 0Ch
        __emit 0CCh
    }
}

// The ten factory-dispatch thunks (P3 sites). NOTE: case 1 (+0x3D) and case 2
// (+0x56) both set selector 3 in the original bytes; kept 1:1.

// @exact 0x10037330
// Spawn probes: the game's 10 class cases all do the same thing. The macro
// keeps the shim body in one place; only the kind and resume differ.
#define SPAWNS_CASE(name, kind, resume)        \
    static __declspec(naked) void name() {               \
        __asm push ebp                              \
        __asm mov ebp, esp                          \
        __asm mov ecx, dword ptr [ebp + 0Ch]        \
        __asm movsx eax, cl                         \
        __asm mov dword ptr [zpatch_spawns_kind], kind \
        __asm mov dword ptr [zpatch_spawns_class], eax \
        __asm mov dword ptr [ebp + 0Ch], ecx        \
        __asm pop ebp                               \
        __asm jmp dword ptr [resume]                \
    }

// The macro's second argument is the selector value the case stores in
// [zpatch_spawns_kind] (the body-name suffix is the jump-table index; cases 1
// and 2 both write 3 in the original bytes, kept 1:1).
SPAWNS_CASE(abi_spawns_case_0x24, 5, zpatch_spawns_case0_resume)
SPAWNS_CASE(abi_spawns_case_0x3D, 3, zpatch_spawns_case1_resume)
SPAWNS_CASE(abi_spawns_case_0x56, 3, zpatch_spawns_case2_resume)
SPAWNS_CASE(abi_spawns_case_0x6F, 0, zpatch_spawns_case3_resume)
SPAWNS_CASE(abi_spawns_case_0x88, 4, zpatch_spawns_case4_resume)
SPAWNS_CASE(abi_spawns_case_0x9E, 6, zpatch_spawns_case5_resume)
SPAWNS_CASE(abi_spawns_case_0xB4, 7, zpatch_spawns_case6_resume)
SPAWNS_CASE(abi_spawns_case_0xCA, 1, zpatch_spawns_case7_resume)
SPAWNS_CASE(abi_spawns_case_0xE0, 2, zpatch_spawns_case8_resume)
SPAWNS_CASE(abi_spawns_case_0xF6, 8, zpatch_spawns_case9_resume)

// sub_10037510, the spawn probe (P1/P4/P5/P6/P7 sites): byte-exact naked body,
// 394 instructions, 0x10037510..0x10037ABC. ECX at the [vtable+0xC] call is the
// selector, exactly like the original - do not "fix" it.
// @exact 0x10037510
static __declspec(naked) void abi_spawns_probe()
{
    __asm {

        push ebp
        mov ebp, esp
        sub esp, 4Ch
        mov eax, dword ptr [__security_cookie]
        xor eax, ebp
        mov dword ptr [ebp - 8], eax
        push esi
        mov esi, dword ptr [zpatch_pre_ped_array]
        xor eax, eax
        push edi
        mov edi, dword ptr [ebp + 8]
        __emit 0Fh
        __emit 1Fh
        __emit 00h
L_10037530:
        mov ecx, dword ptr [esi + eax*4]
        test ecx, ecx
        je L_10037540
        cmp dword ptr [ecx + 3Ch], edi
        je L_100375FA
L_10037540:
        inc eax
        cmp eax, 7918h
        jl L_10037530
        or eax, 0FFFFFFFFh
L_1003754B:
        mov ecx, dword ptr [zpatch_spawns_kind]
        cwde
        mov dword ptr [zpatch_spawns_kind], 0FFFFFFFFh
        mov esi, dword ptr [esi + eax*4]
        cmp eax, -1
        je L_10037AA2
        test esi, esi
        je L_10037AA2
        cmp esi, 0Ch
        je L_10037AA2
        cmp esi, 0CDCDCDCDh
        je L_10037AA2
        mov eax, dword ptr [esi]
        test eax, eax
        je L_10037AA2
        cmp eax, 0Ch
        je L_10037AA2
        cmp eax, 0CDCDCDCDh
        je L_10037AA2
        test ecx, ecx
        jne L_10037635
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_10037602
        cmp dword ptr [esi + 6Ch], 0
        jne L_10037602
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_car
        jmp L_10037A8C
L_100375FA:
        movzx eax, ax
        jmp L_1003754B
L_10037602:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_car
        jmp L_10037A27
L_10037635:
        cmp ecx, 1
        jne L_100376C0
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_1003768D
        cmp dword ptr [esi + 6Ch], 4
        jne L_1003768D
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_heli
        jmp L_10037A8C
L_1003768D:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_heli
        jmp L_10037A27
L_100376C0:
        cmp ecx, 2
        jne L_1003774B
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_10037718
        cmp dword ptr [esi + 6Ch], 2
        jne L_10037718
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_boat
        jmp L_10037A8C
L_10037718:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_boat
        jmp L_10037A27
L_1003774B:
        cmp ecx, 6
        jne L_100377D6
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_100377A3
        cmp dword ptr [esi + 6Ch], 1
        jne L_100377A3
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_bike
        jmp L_10037A8C
L_100377A3:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_bike
        jmp L_10037A27
L_100377D6:
        cmp ecx, 7
        jne L_10037861
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_1003782E
        cmp dword ptr [esi + 6Ch], 3
        jne L_1003782E
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_train
        jmp L_10037A8C
L_1003782E:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_train
        jmp L_10037A27
L_10037861:
        cmp ecx, 8
        jne L_100378EC
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_100378B9
        cmp dword ptr [esi + 6Ch], 5
        jne L_100378B9
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_plane
        jmp L_10037A8C
L_100378B9:
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_plane
        jmp L_10037A27
L_100378EC:
        cmp ecx, 3
        jne L_1003796D
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 6
        je L_1003792D
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_nonspawned_ped
        jmp L_10037A27
L_1003792D:
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_ped
        jmp L_10037A8C
L_1003796D:
        cmp ecx, 4
        jne L_100379EB
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 5
        jne L_100379AB
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_vehicle_object
        jmp L_10037A27
L_100379AB:
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_object
        jmp L_10037A8C
L_100379EB:
        cmp ecx, 5
        jne L_10037AA2
        mov eax, dword ptr [eax + 0Ch]
        call eax
        cmp al, 6
        je L_10037A59
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037A3D
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_used_nonped
L_10037A27:
        lea eax, [ebp - 4Ch]
        push 40h
        push eax
        call zpatch_spawns_sprintf
        add esp, 14h
        lea edx, [ebp - 4Ch]
        call zpatch_spawns_print_edx
L_10037A3D:
        mov dword ptr [zpatch_spawns_skip], 1
        xor eax, eax
        pop edi
        pop esi
        mov ecx, dword ptr [ebp - 8]
        xor ecx, ebp
        call zpatch_spawns_cookie_check_ecx
        mov esp, ebp
        pop ebp
        ret
L_10037A59:
        cmp byte ptr [zpatch_flag_debug_spawns], 0
        je L_10037AA2
        push OFFSET zpatch_str_gtac
        call dword ptr [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je L_10037AA2
        mov ecx, edi
        call zpatch_spawns_name_ecx
        mov edx, dword ptr [zpatch_spawns_class]
        mov esi, eax
        call zpatch_spawns_who_edx
        push esi
        push eax
        push OFFSET zpatch_fmt_spawned_generic
L_10037A8C:
        lea eax, [ebp - 4Ch]
        push 40h
        push eax
        call zpatch_spawns_sprintf
        add esp, 14h
        lea edx, [ebp - 4Ch]
        call zpatch_spawns_print_edx
L_10037AA2:
        mov eax, dword ptr [zpatch_spawns_orig_a]
        push edi
        call eax
        mov ecx, dword ptr [ebp - 8]
        add esp, 4
        xor ecx, ebp
        pop edi
        pop esi
        call zpatch_spawns_cookie_check_ecx
        mov esp, ebp
        pop ebp
        ret
        __emit 0CCh
    }
}
}  // extern "C"

namespace
{
// The name -> body rows this module registers. The reference VA is the
// original handler destination (0 when the body has none); it is provenance,
// not an install address.
const opp::HookBodyRow kMultiplayerHookBodies[] = {
    {"abi_better_mp_sync", 0x100057F0u, reinterpret_cast<opp::HookBody>(&abi_better_mp_sync)},
    {"abi_freeze_gun_shim", 0x10036EB0u, reinterpret_cast<opp::HookBody>(&abi_freeze_gun_shim)},
    {"abi_spawns_guard", 0x100372F0u, reinterpret_cast<opp::HookBody>(&abi_spawns_guard)},
    {"abi_spawns_probe", 0x10037510u, reinterpret_cast<opp::HookBody>(&abi_spawns_probe)},
    {"abi_spawns_case_0x24", 0x10037330u, reinterpret_cast<opp::HookBody>(&abi_spawns_case_0x24)},
    {"abi_spawns_case_0x3D", 0x100373F0u, reinterpret_cast<opp::HookBody>(&abi_spawns_case_0x3D)},
    {"abi_spawns_case_0x56", 0x10037420u, reinterpret_cast<opp::HookBody>(&abi_spawns_case_0x56)},
    {"abi_spawns_case_0x6F", 0x10037390u, reinterpret_cast<opp::HookBody>(&abi_spawns_case_0x6F)},
    {"abi_spawns_case_0x88", 0x10037450u, reinterpret_cast<opp::HookBody>(&abi_spawns_case_0x88)},
    {"abi_spawns_case_0x9E", 0x10037480u, reinterpret_cast<opp::HookBody>(&abi_spawns_case_0x9E)},
    {"abi_spawns_case_0xB4", 0x100374E0u, reinterpret_cast<opp::HookBody>(&abi_spawns_case_0xB4)},
    {"abi_spawns_case_0xCA", 0x10037360u, reinterpret_cast<opp::HookBody>(&abi_spawns_case_0xCA)},
    {"abi_spawns_case_0xE0", 0x100373C0u, reinterpret_cast<opp::HookBody>(&abi_spawns_case_0xE0)},
    {"abi_spawns_case_0xF6", 0x100374B0u, reinterpret_cast<opp::HookBody>(&abi_spawns_case_0xF6)},
};

struct MultiplayerHookBodyRegistration
{
    MultiplayerHookBodyRegistration() noexcept
    {
        opp::RegisterHookBodies(kMultiplayerHookBodies);
    }
} g_multiplayerHookBodies;
}  // namespace

#endif  // !defined(_WIN64)
