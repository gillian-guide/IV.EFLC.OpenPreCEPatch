// SPDX-License-Identifier: GPL-3.0-or-later
//
// graphics
//
// One function per INI arm, in the original loader order; the bodies are the
// readable reconstruction's verified arms, the wrapper and the arm trace ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"

export module opp.graphics;

import opp.patch;
import opp.game;
import opp.log;

import opp.common;
import opp.config;

// Hook bodies: the declarations the installers take the address of; their definitions are
// the tail section at the bottom of this file, after every installer. Each keeps the
// original site's calling convention and register contract (its comment names the
// destination VA), and the byte-exact markers stay here for tools/opcep_bodycheck.py.
// 32-bit only - the x64 parity dump resolves names to the original VAs.
// abi_emissive_lerp_shim is the one body that is not byte-exact (the original's helper is
// inlined into it).
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

extern "C"
{

void abi_emissive_lerp_shim();
// @exact 0x100068A0
void abi_carparts_shadow_2();
// @exact 0x100068D0
void abi_carparts_shadow_2b();
// @exact 0x10006880
void abi_carparts_shadow();
// @exact 0x1001ADB0
void abi_fchs_shim();
// @exact 0x100064F0
void abi_dual_headlights_new();
// @exact 0x10006630
void abi_dual_headlights_old();
// @exact 0x1001B2E0
void abi_car_decals_fix();
// @exact 0x10005AF0
void abi_building_dynamic_shadows();
// @exact 0x100388C0
void abi_cutscene_chain_b();

// ForceCarHeadlightShadows checker (original sub_1001AD20), called between
// pushad/popad: walk the [HeadlightConeOffsets] table (dword_1015ADB8) and
// publish the first matching offset (dword_1015B5C0) into dword_101580C0;
// default 2.0f. The lookup and the object's model (+0x2E) are unguarded.
void abi_fchs_check() noexcept
{
    const uint32_t* const hashes = zpatch_fchs_hashes;
    uint32_t hash = hashes[0];
    zpatch_fchs_offset = std::bit_cast<uint32_t>(kDefaultConeOffset);
    if (hash == 0u) return;
    const auto lookup =
        Rebased<int(__cdecl*)(uint32_t, int32_t*)>(zpatch_model_lookup);
    // 0x1001AD5C `call dword_10159C0C` and 0x1001AD62 `movsx eax,word [v2+2Eh]`
    // are unguarded in the original.
    const auto* base = Rebased<const uint8_t*>(zpatch_fchs_param);
    if (lookup == nullptr || base == nullptr)
        return;  // a preamble/parameter miss is a clean skip
    static uint32_t cache_hash[512] = {};
    static int32_t cache_index[512] = {};
    static uint8_t cache_valid[512] = {};
    const auto resolve = [&](uint32_t value) -> int32_t
    {
        const size_t slot = (value * 2654435761u) >> 23;
        if (cache_valid[slot] != 0u && cache_hash[slot] == value)
            return cache_index[slot];
        int32_t resolved = -1;
        lookup(value, &resolved);
        cache_hash[slot] = value;
        cache_index[slot] = resolved;
        cache_valid[slot] = 1;
        return resolved;
    };
    for (uint32_t i = 0;;)
    {
        const int32_t index = resolve(hash);
        if (static_cast<int32_t>(
                *reinterpret_cast<const int16_t*>(base + kModelIndexOffset)) == index)
        {
            float value = 0.0f;
            std::memcpy(&value, &zpatch_fchs_offsets[i], sizeof(value));
            std::memcpy(&zpatch_fchs_offset, &value, sizeof(value));
            return;
        }
        ++i;
        // The original walks into adjacent .data instead; layout-specific and
        // unreproducible.
        if (i == 512u) return;
        hash = hashes[i];
        if (hash == 0u) return;
    }
}

// Sets the constants the two cone-draw bodies read (the original's sub_100064D0).
extern "C" void __cdecl abi_dual_headlights_init()
{
    zpatch_dual_light_a = 1;
    zpatch_dual_light_b = 2;
}

// RestoreDeathMusic (original sub_10050A50): the death-music gate, gated on
// byte_10159B86 and answered through the shipped hash-salt script calls.
extern "C" uint8_t abi_death_music_enabled() noexcept
{
    if (!zpatch_flag_death_music) return 0;
    uint32_t queried = 0;
    const uint32_t query_args[2] = {
        zpatch_script_call(zpatch_salt_5c, 0, nullptr),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&queried))};
    zpatch_script_call(zpatch_salt_54, 2, query_args);
    if (zpatch_script_call_bool(zpatch_salt_28, 1, &queried) != 0) return 1;
    if (zpatch_script_call_bool(zpatch_salt_a8, 0, nullptr) != 0 &&
        zpatch_script_call_bool(zpatch_salt_50, 0, nullptr) != 0)
        return 1;
    return 0;
}

// RestoreDeathMusic (original sub_10050AD0): resolve "6A 06 68 ? ? ? ? 8D 8C"
// once and randomize the push immediate to a track 1..5 (rand() % 5 + 1).
extern "C" char abi_death_music_install() noexcept
{
    if (!abi_death_music_enabled()) return 0;
    if (zpatch_death_music_site == 0)
    {
        const auto* module = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
        if (module)
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            if (dos->e_magic == IMAGE_DOS_SIGNATURE)
            {
                const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
                    module + dos->e_lfanew);
                if (nt->Signature == IMAGE_NT_SIGNATURE)
                {
                    Match match;
                    const ModuleView view = ExecutableView({module, nt->OptionalHeader.SizeOfImage});
                    if (FindPattern(view, "6A 06 68 ? ? ? ? 8D 8C", match))
                        zpatch_death_music_site = static_cast<uint32_t>(
                            reinterpret_cast<uintptr_t>(match.address));
                }
            }
        }
    }
    if (zpatch_death_music_site != 0)
    {
        // The original randomises the track push immediate to 1..5:
        // `rand() % 5 + 1`.
        auto* site = Rebased<uint8_t*>(
            static_cast<uintptr_t>(zpatch_death_music_site) + 1);
        injector::WriteMemory<uint8_t>(  // push imm8
            injector::raw_ptr(site),
            static_cast<uint8_t>(std::rand() % 5 + 1),
            true);
    }
    return 1;
}
}  // extern "C"

#endif  // !defined(_WIN64)

namespace opp::features
{

    // BuildingAlphaFix

    class BuildingAlphaFix
    {
    public:
        BuildingAlphaFix()
        {
            InstallEvent() += []
            {
                TraceArm("InstallBuildingAlphaFix");

                // BuildingAlphaFix: 1 literal, 1 siginstall. Patch 6+ only (the LOD
                // fade-in path it fixes does not exist before 1060).
                if (Game::Option("BuildingAlphaFix", "1") &&
                    BuildIs(Game::Get().build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                        GameBuild::v1080, GameBuild::v1100, GameBuild::v1110,
                                        GameBuild::v1120, GameBuild::v1130, GameBuild::v12043}))
                {
                    const GameImage& image = Game::Get().image;
                    // The `0F 84`/`0F 85` branch pair starts seven bytes in, after
                    // `F7 46 24 <imm32>`.
                    constexpr ptrdiff_t kBranchOffset = 7;

                    if (auto pattern = find_pattern("F7 46 24 00 00 00 40 0F 84"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        if (InImage(image, reinterpret_cast<uintptr_t>(match + kBranchOffset)))
                            injector::MakeJMP(match + kBranchOffset, reinterpret_cast<void*>(BranchTarget(match, kBranchOffset)), true);
                    }

                    if (auto pattern = find_pattern("F7 46 24 00 00 00 40 0F 85"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        // NOP only the six-byte `jnz` at the branch and leave the `test` to
                        // set the flags, which is what the untaken branch would have
                        // produced. (The original 13-byte NOP covered the test too, so
                        // the following code saw stale flags.)
                        injector::MakeNOP(match + kBranchOffset, 6, true);
                    }
                }
            };
        }
    } buildingAlphaFix;


    // BuildingDynamicShadows

    class BuildingDynamicShadows
    {
    public:
        BuildingDynamicShadows()
        {
            PublishEvent() += []
            {
                TraceArm("PublishBuildingDynamicShadows");

                // BuildingDynamicShadows: 4 slots
                if (Game::Option("BuildingDynamicShadows", "1"))
                {
                    if (auto pattern = find_pattern("8B 47 28 C1 E8 06 83 E0 0F 83 C0"); pattern.size() > 0)
                    {
                        // Two resumes inside the shadow-size function (offsets 15 and
                        // 112); the hash rows' dword operand is named below.
                        zpatch_bds_resume_a = MatchAddress(pattern, 15);
                        zpatch_bds_resume_b = MatchAddress(pattern, 112);
                    }

                    // The two hash slots take the same field of the FIRST and the SECOND
                    // match (recovered rows: dword_10159C1C index 0, dword_10159C24 index 1).
                    // The two matches hold different pointers (0xD8FA2C / 0xDCF48C on 1080),
                    // so publishing both from the first is not the original's state.
                    auto hashes = find_pattern<0>("8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06");
                    // Exactly the two recovered hash rows; a third or a missing one is a
                    // view mismatch, so neither slot is published (a half-pair is worse).
                    if (hashes.size() > 0 &&
                        ExpectMatchCount(hashes, 2, "BuildingDynamicShadows hash pair"))
                    {
                        // The `mov dword ptr [esi+28h], imm32` operand at +20.
                        constexpr ptrdiff_t kHashOperand = 20;
                        zpatch_bds_hash_a = *hashes.get_first<uint32_t>(kHashOperand);
                        zpatch_bds_hash_b = *hashes.get(1).get<uint32_t>(kHashOperand);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallBuildingDynamicShadows");

                // BuildingDynamicShadows: 1 sigjump, 1 sigwrite
                if (Game::Option("BuildingDynamicShadows", "1"))
                {
                    {
                        if (auto pattern = find_pattern("50 68 04 01 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
                        {
                            // The low byte of the `push 0x104` immediate: 0x104 -> 0x100
                            // (the shadow size the arm forces for dynamic shadows).
                            injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(2), 0x00u, true);
                        }
                    }

                    if (auto pattern = find_pattern("8B 47 28 C1 E8 06 83 E0 0F 83 C0"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_building_dynamic_shadows), true);
                    }
                }
            };
        }
    } buildingDynamicShadows;


    // CarDecalsFix

    class CarDecalsFix
    {
    public:
        CarDecalsFix()
        {
            InstallEvent() += []
            {
                TraceArm("InstallCarDecalsFix");

                // CarDecalsFix: 1 siginstall
                if (Game::Option("CarDecalsFix", "1"))
                {
                    if (auto pattern = find_pattern("8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_car_decals_fix), true);
                    }
                }
            };
        }
    } carDecalsFix;


    // CarDynamicShadowFix

    class CarDynamicShadowFix
    {
    public:
        CarDynamicShadowFix()
        {
            PublishEvent() += []
            {
                TraceArm("PublishCarDynamicShadowFix");

                // CarDynamicShadowFix: 5 slots
                if (Game::Option("CarDynamicShadowFix", "0"))
                {
                    // The two recovered spawner rows, in the original's order. The
                    // resume is the instruction after the first E8; shadow_fn is that
                    // call's target.
                    struct CarShadowRow final
                    {
                        const char* signature;
                        ptrdiff_t resume_offset;
                        ptrdiff_t call_operand;
                    };
                    constexpr CarShadowRow kRows[] = {
                        {"56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8",
                         11, 7},
                        {"56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8",
                         13, 9},
                    };
                    for (const CarShadowRow& row : kRows)
                    {
                        if (auto pattern = find_pattern(row.signature); pattern.size() > 0)
                        {
                            zpatch_carparts_resume_2 = MatchAddress(pattern, row.resume_offset);
                            zpatch_carparts_shadow_fn = BranchDestination(
                                pattern.get_first<uint8_t>(), row.call_operand);
                        }
                    }

                    if (auto pattern = find_pattern("8B 45 10 8B 16 8B 92 8C 00 00 00"); pattern.size() > 0)
                    {
                        // The match is the published "dynamic shadow entry" function.
                        zpatch_car_dynamic_d = MatchAddress(pattern);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallCarDynamicShadowFix");

                // CarDynamicShadowFix: 3 siginstalls, 2 sigwrites
                if (Game::Option("CarDynamicShadowFix", "0"))
                {
                    if (auto pattern = find_pattern("56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_carparts_shadow_2), true);
                    }

                    if (auto pattern = find_pattern("56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_carparts_shadow_2b), true);
                    }

                    if (auto pattern = find_pattern("83 F8 14 7D 05 83 F8 06 7D 14"); pattern.size() > 0)
                    {
                        // `jz +0x18` at +3 becomes an unconditional jump to match+30
                        // (the original's shadow-class skip).
                        injector::MakeJMP(pattern.get_first<uint8_t>(3), reinterpret_cast<void*>(pattern.get_first<uint8_t>(30)), true);
                    }

                    {
                        if (auto pattern = find_pattern("83 E0 0F 83 F8 03 75 17 F6 86 6C 02 00 00 04"); pattern.size() > 0)
                        {
                            // The `jnz +0x17` at +6 becomes an unconditional short jmp:
                            // the original's shadow-class skip (CarDynamicShadowFix).
                            injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(6), opp::ops::kJmpRel8, true);
                        }
                    }

                    {
                        if (auto pattern = find_pattern("50 68 04 01 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
                        {
                            // The low byte of the `push 0x104` immediate: 0x104 -> 0x100
                            // (the shadow size the arm forces for dynamic shadows).
                            injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(2), 0x00u, true);
                        }
                    }

                    // Two further writes the original makes under this arm; they are in its
                    // write log for a CarDynamicShadowFix-only cell but not in the recovered
                    // row tables, so the reconstruction does not make them either. They are
                    // the arm's @noFCHS variant: with ForceCarHeadlightShadows on, the
                    // original takes the other path and makes neither write (probed:
                    // CarDynamic+FCHS writes only 0x8F7422; CarDynamic alone also writes
                    // 0x8F73EC and 0x8F719F).
                    //
                    //  * `0F 84 <rel32>` (jz) becomes `E9 <rel32+1>`: a jmp to the same
                    //    target. The displacement grows by one because the jz encoding is one
                    //    byte longer than the jmp (match+6+rel == match+5+newRel).
                    //  * `push 0x504` becomes `push 0x100` (the shadow size the arm forces),
                    //    written as a dword over the immediate like the original does.
                    if (!Game::Option("ForceCarHeadlightShadows", "1"))
                    {
                        if (auto pattern = find_pattern("0F 84 9E 00 00 00 80 7D 28"); pattern.size() > 0)
                        {
                            auto* match = pattern.get_first<uint8_t>();

                            // `0F 84 rel32` (6 B) -> `E9 rel32` (5 B): the same target,
                            // the next instruction's first byte untouched. BranchTarget
                            // computes the 6-byte Jcc destination.
                            injector::MakeJMP(match, reinterpret_cast<void*>(BranchTarget(match, 0)), true);
                        }

                        if (auto pattern = find_pattern("51 68 04 05 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
                        {
                            // `push 0x504` becomes `push 0x100`: the shadow size. The
                            // immediate is the second byte of the six-byte push.
                            injector::WriteMemory<uint32_t>(pattern.get_first<uint8_t>(2), 0x100u, true);
                        }
                    }
                }
            };
        }
    } carDynamicShadowFix;


    // CarDynamicShadowFixNoE

    class CarDynamicShadowFixNoE
    {
    public:
        CarDynamicShadowFixNoE()
        {
            InstallEvent() += []
            {
                TraceArm("InstallCarDynamicShadowFixNoE");

                // CarDynamicShadowFix@noE: 1 siginstall
                if (Game::Option("CarDynamicShadowFix", "1"))
                {
                    if (auto pattern = find_pattern("83 F8 14 7D 05 83 F8 06 7D 14"); pattern.size() > 0)
                    {
                        // The six-byte `jz` at +62 the @noE variant NOPs.
                        injector::MakeNOP(pattern.get_first<uint8_t>(62), 6, true);
                    }
                }
            };
        }
    } carDynamicShadowFixNoE;


    // CarPartsShadowFix

    class CarPartsShadowFix
    {
    public:
        CarPartsShadowFix()
        {
            PublishEvent() += []
            {
                TraceArm("PublishCarPartsShadowFix");

                // CarPartsShadowFix: 2 slots
                if (Game::Option("CarPartsShadowFix", "1") || Game::Option("CarDynamicShadowFix", "1"))
                {
                    if (auto pattern = find_pattern("85 DB 7E 41 55 EB 0A"); pattern.size() > 0)
                    {
                        // The two resumes the shim tail-jumps through, its if/else exits:
                        // the `jle` at +2 and the else path at +69.
                        zpatch_carparts_resume_if = MatchAddress(pattern, 2);
                        zpatch_carparts_resume_else = MatchAddress(pattern, 69);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallCarPartsShadowFix");

                // CarPartsShadowFix: 1 siginstall, 1 sigjump
                if (Game::Option("CarPartsShadowFix", "1") || Game::Option("CarDynamicShadowFix", "1"))
                {
                    if (auto pattern = find_pattern("85 DB 7E 41 55 EB 0A"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        // The six bytes before the match are the prologue the shim
                        // replaces.
                        injector::MakeJMP(match - 6, OPP_BODY(abi_carparts_shadow), true);
                    }
                }
            };
        }
    } carPartsShadowFix;


    // CarPartsShadowFixStandalone

    class CarPartsShadowFixStandalone
    {
    public:
        CarPartsShadowFixStandalone()
        {
            InstallEvent() += []
            {
                TraceArm("InstallCarPartsShadowFixStandalone");

                // CarPartsShadowFix@standalone: 2 sigwrites
                if (Game::Option("CarPartsShadowFix", "1"))
                {
                    {
                        if (auto pattern = find_pattern("83 F8 04 C6 44 24 0E 00 75 0F"); pattern.size() > 0)
                        {
                            // The `jnz +0x0F` at +8 becomes a short jmp (the car-parts
                            // shadow fix's unconditional skip).
                            injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(8), opp::ops::kJmpRel8, true);
                        }
                    }

                    {
                        if (auto pattern = find_pattern("83 FA 04 75 0C"); pattern.size() > 0)
                        {
                            // The same rewrite on the second check (`jnz +0x0C` at +3).
                            injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(3), opp::ops::kJmpRel8, true);
                        }
                    }
                }
            };
        }
    } carPartsShadowFixStandalone;


    // CutsceneFixes: the three original sites (two call rewrites at the camera
    // call sites plus the branch byte inside the camera callee). Shared by the
    // default arm and by the opt-in callee hook's fallback.
    void ApplyCutsceneSites()
    {
        if (auto pattern = find_pattern("8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8"); pattern.size() > 0)
        {
            auto* match = pattern.get_first<uint8_t>();

            // The two E8 call sites are 5 bytes before their matches.
            injector::MakeCALL(match - 5, OPP_BODY(abi_cutscene_chain_b), true);
        }

        if (auto pattern = find_pattern("8B CF 88 44 24 0F E8 ? ? ? ? A1"); pattern.size() > 0)
        {
            auto* match = pattern.get_first<uint8_t>();

            injector::MakeCALL(match - 5, OPP_BODY(abi_cutscene_chain_b), true);
        }

        {
            // 1080 has exactly one match of the primary pattern (0xC9A840); the
            // alternate is only for builds where it is absent. Falling back on
            // "<= 1 match" threw that single match away and applied nothing.
            auto pattern = find_pattern("74 24 8B 44 24 2C 83 F8 03");
            if (pattern.size() == 0)
                pattern = find_pattern("74 0B 83 7C 24 2C 03");
            if (pattern.size() > 0)
                // `EB` replaces the `jz` opcode at the match: the original's
                // unconditional-skip rewrite.
                injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(),
                                               opp::ops::kJmpRel8, true);
        }
    }

    // One hook at the shared camera callee instead of the three sites. The
    // shipped body already takes its call target and poke address from globals,
    // so instead of transcribing 0x2D2 bytes we relocate the callee into private
    // memory, re-point its 13 direct calls and bake the branch fix into the
    // copy. The body then calls the copy, and the game's two call sites reach
    // the body through the five bytes at the callee entry.
    //
    // Any shape mismatch falls back to the three original sites, so the fix is
    // never lost.
    namespace
    {
        bool CutsceneBytesMatch(const uint8_t* at, const uint8_t* bytes, size_t count) noexcept
        {
            return std::memcmp(at, bytes, count) == 0;
        }

        // 0xC9A640 on 1.0.8.0.
        // Recovered offsets in the cutscene shadow function: the shape poke is
        // at +0x4A, its `call rel32` operand at +0x4F (poke-1 = +0x4E), the
        // second `jz` site at +0x200 and the copied body is 0x2D2 bytes.
        constexpr size_t kCutsceneCalleeSize = 0x2D2u;
        constexpr uint32_t kCutsceneShapePoke = 0x4Au;
        constexpr uint32_t kCutscenePokeOffset = kCutsceneShapePoke + kBranchLength;
        static_assert(kCutscenePokeOffset == 0x4Fu);
        constexpr uint32_t kCutsceneBranchOffset = 0x200u;
        // The 13 `call rel32` offsets inside the copied cutscene shadow body:
        // zpatch_relocate_calls re-points each at the same absolute target it
        // had in the game image after the copy moves.
        constexpr uint32_t kCutsceneCalls[] = {
            0x00Eu, 0x01Du, 0x092u, 0x0E9u, 0x0F1u, 0x101u, 0x118u,
            0x127u, 0x13Cu, 0x233u, 0x24Cu, 0x286u, 0x29Au,  // 13 offsets
        };

        // The copy's own `call rel32`s only reach their targets if it lives
        // within 2 GB of the game image, so ask for an address near it instead
        // of letting VirtualAlloc pick (on 64-bit that can be gigabytes away).
        void* AllocateNear(const GameImage& image, size_t size)
        {
            // The fallback scan steps 16 MiB at a time up to 1 GiB, the range
            // the original's pointer search covered.
            constexpr uintptr_t kStep = 0x1000000u;
            constexpr uintptr_t kReach = 0x40000000u;
            constexpr uintptr_t kPageMask = 0xFFFFu;  // 64 KiB granularity
            const auto base = reinterpret_cast<uintptr_t>(image.base);
            const uintptr_t first =
                (base + image.size + kPageMask) & ~static_cast<uintptr_t>(kPageMask);
            for (uintptr_t candidate = first; candidate < first + kReach;
                 candidate += kStep)
            {
                if (void* const block = VirtualAlloc(
                        reinterpret_cast<void*>(candidate), size,
                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                    return block;
            }
            // Below the image as well, in case the region above is occupied.
            for (uintptr_t candidate = first - kStep;
                 candidate > kStep && candidate > base - kReach; candidate -= kStep)
            {
                if (void* const block = VirtualAlloc(
                        reinterpret_cast<void*>(candidate), size,
                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                    return block;
            }
            return nullptr;
        }

        void* CutsceneCalleeCopy(const GameImage& image, uintptr_t callee)
        {
            // 55 8B EC 83 E4 = `push ebp; mov ebp,esp; and esp,-16`. The fifth
            // byte is the `and` opcode: the F0 immediate is not compared here
            // because the whole prologue is copied unchanged (the immediate
            // relocates with it); this window only proves the function shape.
            static constexpr uint8_t kPrologue[5] = {0x55u, 0x8Bu, 0xECu, 0x83u, 0xE4u};
            // C6 44 24 1C 01 = `mov byte ptr [esp+0x1C], 1`: the shape poke itself.
            static constexpr uint8_t kPokeSite[5] = {0xC6u, 0x44u, 0x24u, 0x1Cu, 0x01u};
            // 74 24 8B 44 24 2C 83 F8 = `jz +0x24; mov eax,[esp+0x2C]; cmp eax,imm8`.
            // The imm8 is 3 (`cmp eax,3` at 0xC9A848 on 1.0.8.0) and is copied with
            // the body, so the 8-byte window stops at the `cmp` opcode.
            static constexpr uint8_t kBranchSite[8] = {0x74u, 0x24u, 0x8Bu,
                                                       0x44u, 0x24u, 0x2Cu,
                                                       0x83u, 0xF8u};
            if (callee == 0u || !InImage(image, callee) ||
                !InImage(image, callee + kCutsceneCalleeSize - 1u))
            {
                Log("CutsceneFixes: callee %p is outside the image, keeping the 3 sites",
                    reinterpret_cast<void*>(callee));
                return nullptr;
            }
            const auto* source = reinterpret_cast<const uint8_t*>(callee);
            if (!CutsceneBytesMatch(source, kPrologue, sizeof(kPrologue)) ||
                !CutsceneBytesMatch(source + kCutsceneShapePoke, kPokeSite, sizeof(kPokeSite)) ||
                !CutsceneBytesMatch(source + kCutsceneBranchOffset, kBranchSite,
                                    sizeof(kBranchSite)))
            {
                Log("CutsceneFixes: callee %p does not match the 1.0.8.0 shape, keeping the 3 sites",
                    reinterpret_cast<void*>(callee));
                return nullptr;
            }
            auto* copy = static_cast<uint8_t*>(
                AllocateNear(image, kCutsceneCalleeSize));
            if (copy == nullptr)
            {
                Log("CutsceneFixes: no memory for the callee copy, keeping the 3 sites");
                return nullptr;
            }
            if (!zpatch_relocate_calls(
                    copy,
                    std::span<const uint8_t>(source, kCutsceneCalleeSize),
                    kCutsceneCalls))
            {
                VirtualFree(copy, 0, MEM_RELEASE);
                Log("CutsceneFixes: callee calls do not relocate, keeping the 3 sites");
                return nullptr;
            }
            // Site 2: the `jz` opcode becomes `EB` (the branch is taken when the
            // camera is called back), with its displacement kept.
            copy[kCutsceneBranchOffset] = opp::ops::kJmpRel8;
            Log("CutsceneFixes: hooked callee %p, copy %p",
                reinterpret_cast<void*>(callee), static_cast<void*>(copy));
            return copy;
        }
    } // namespace

    class CutsceneFixes
    {
    public:
        CutsceneFixes()
        {
            PublishEvent() += []
            {
                TraceArm("PublishCutsceneFixes");

                // CutsceneFixes: 2 slots
                if (Game::Option("CutsceneFixes", "1"))
                {
                    if (auto pattern = find_pattern("8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8"); pattern.size() > 0)
                    {
                        // The `call [imm32]` operand at -4 names the camera callee; the
                        // poke target is that call's continuation (+79).
                        constexpr ptrdiff_t kCalleeOperand = -4;
                        zpatch_cutscene_target = MatchAddress(pattern) +
                                                 *pattern.get_first<uint32_t>(kCalleeOperand);
                        zpatch_cutscene_poke = MatchTarget(pattern, kCalleeOperand, 79);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallCutsceneFixes");

                if (!Game::Option("CutsceneFixes", "1"))
                    return;

                // One hook at the shared camera callee instead of the three original
                // sites. The publish arm derived the callee from the call site's rel32;
                // any shape mismatch, a failed allocation or a relocation that does not
                // fit applies the three sites instead, so the fix is never lost.
                const auto callee = static_cast<uintptr_t>(zpatch_cutscene_target);
                if (void* const copy = CutsceneCalleeCopy(Game::Get().image, callee))
                {
                    zpatch_cutscene_target =
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(copy));
                    zpatch_cutscene_poke = static_cast<uint32_t>(
                        reinterpret_cast<uintptr_t>(copy) + kCutscenePokeOffset);
                    injector::MakeJMP(reinterpret_cast<void*>(callee), OPP_BODY(abi_cutscene_chain_b), true);
                    return;
                }
                ApplyCutsceneSites();
            };
        }
    } cutsceneFixes;


    // DualVehicleHeadlights

    class DualVehicleHeadlights
    {
    public:
        DualVehicleHeadlights()
        {
            PublishEvent() += []
            {
                TraceArm("PublishDualVehicleHeadlights");

                // DualVehicleHeadlights: 5 slots
                if (Game::Option("DualVehicleHeadlights", "1"))
                {
                    if (auto pattern = find_pattern("8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
                    {
                        // The recovered slots inside the dual-headlights body: the
                        // stored pointer at +207, the two call targets at +140/+331 and
                        // +151 (operands), and the absolute slot at +338.
                        auto* match = pattern.get_first<uint8_t>();
                        zpatch_dual_bcc = *pattern.get_first<uint32_t>(207);
                        zpatch_dual_bd4 = BranchDestination(match, 331);
                        zpatch_dual_bd0 = BranchDestination(match, 140);
                        zpatch_dual_be4 = BranchDestination(match, 151);
                        zpatch_dual_be0 = MatchAddress(pattern, 338);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallDualVehicleHeadlights");

                // DualVehicleHeadlights (handler 0x10006770): one E9 at the game match.
                // The original picks the old body for build ids 1..6 and the new body for
                // 7+ (0x10006630 / 0x100064F0); the bodies are not interchangeable. The
                // pre-fix layout registered both bodies ungated: the old arm ran first and
                // overwrote the five bytes the new arm's scan needs, so 1.0.8.0 (build 9)
                // silently got the old body. Keep the build choice explicit and install
                // the site once.
                if (Game::Option("DualVehicleHeadlights", "1"))
                {
                    if (auto pattern = find_pattern("8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();
                        const bool old_body = BuildIs(
                            Game::Get().build, {GameBuild::v1000, GameBuild::v1040, GameBuild::v1004});
                        void* const body =
                            old_body ? OPP_BODY(abi_dual_headlights_old) : OPP_BODY(abi_dual_headlights_new);

                        injector::MakeJMP(match, body, true);
                    }
                }
            };
        }
    } dualVehicleHeadlights;


    // EmissiveLerpFix

    class EmissiveLerpFix
    {
    public:
        EmissiveLerpFix()
        {
            PublishEvent() += []
            {
                TraceArm("PublishEmissiveLerpFix");

                // EmissiveLerpFix: 2 slots
                if (Game::Option("EmissiveLerpFix", "1"))
                {
                    if (auto pattern = find_pattern("F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1"); pattern.size() > 0)
                    {
                        // The const float and the resume the shim published: 16 and 12
                        // bytes before the match.
                        zpatch_emissive_const = MatchAddress(pattern, -16);
                        zpatch_emissive_resume = MatchAddress(pattern, -12);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallEmissiveLerpFix");

                // EmissiveLerpFix: 1 siginstall
                if (Game::Option("EmissiveLerpFix", "1"))
                {
                    if (auto pattern = find_pattern("F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        // The shim replaces the 24 bytes before the match (its prologue).
                        injector::MakeJMP(match - 24, OPP_BODY(abi_emissive_lerp_shim), true);
                    }
                }
            };
        }
    } emissiveLerpFix;


    // ForceCarHeadlightShadows

    class ForceCarHeadlightShadows
    {
    public:
        ForceCarHeadlightShadows()
        {
            PublishEvent() += []
            {
                TraceArm("PublishForceCarHeadlightShadows");

                // ForceCarHeadlightShadows: 1 slot
                if (Game::Option("ForceCarHeadlightShadows", "1"))
                {
                    if (auto pattern = find_pattern("55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B"); pattern.size() > 0)
                    {
                        // `and esp,-16` (4 bytes) + `sub esp,0xD4` (8 bytes) = the 12
                        // bytes the shim's prologue replay covers.
                        zpatch_fchs_resume = MatchAddress(pattern, 12);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallForceCarHeadlightShadows");

                // ForceCarHeadlightShadows: 2 siginstalls, 5 sigwrites
                if (Game::Option("ForceCarHeadlightShadows", "1"))
                {
                    if (auto pattern = find_pattern("3C 01 75 50 84 DB 75 79"); pattern.size() > 0)
                    {
                        // The original's ForceCarHeadlightShadows arm writes two `jmp`
                        // opcodes (+6, +379), re-points the branch at +95 to match+476 and
                        // stores our cone-offset address at +211.
                        injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(6), opp::ops::kJmpRel8, true);
                        injector::MakeJMP(pattern.get_first<uint8_t>(95), reinterpret_cast<void*>(pattern.get_first<uint8_t>(476)), true);
                        injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(379), opp::ops::kJmpRel8, true);
                        injector::WriteMemory<uint32_t>(pattern.get_first<uint8_t>(211), AddressOf(&zpatch_fchs_offset), true);
                    }

                    if (auto pattern = find_pattern("55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeJMP(match, OPP_BODY(abi_fchs_shim), true);
                    }

                    {
                        if (auto pattern = find_pattern("8D 4C 24 50 51 68 04 05 00 00"); pattern.size() > 0)
                        {
                            // The `push 0x504` immediate bytes are re-stored unchanged
                            // (04 05), exactly as the original writes them.
                            injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(6), 0x04u, true);
                            injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(7), 0x05u, true);
                        }
                    }

                    {
                        if (auto pattern = find_pattern("8D 44 24 50 50 68 04 01 00 00"); pattern.size() > 0)
                        {
                            // The second `push 0x104` site: both immediate bytes already
                            // are 04 01 and are re-stored 1:1 (the original writes them).
                            injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(6), 0x04u, true);
                            injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(7), 0x01u, true);
                        }
                    }

                    {
                        if (auto pattern = find_pattern("F3 0F 11 44 24 1C 0F 84 9E 00 00 00"); pattern.size() > 0)
                        {
                            // Restore `jz +0x9E` (0F 84 9E 00 00) as the recovered five-byte
                            // window. The sixth byte of the rel32 is deliberately left alone:
                            // the original never writes it.
                            constexpr uint8_t kJzPastShadow[5] = {0x0F, 0x84, 0x9E, 0x00, 0x00};
                            injector::WriteMemoryRaw(pattern.get_first<uint8_t>(6), const_cast<void*>(static_cast<const void*>(kJzPastShadow)), sizeof(kJzPastShadow), true);
                        }
                    }
                }
            };
        }
    } forceCarHeadlightShadows;


    // ForceShadowsOnObjects

    class ForceShadowsOnObjects
    {
    public:
        ForceShadowsOnObjects()
        {
            InstallEvent() += []
            {
                TraceArm("InstallForceShadowsOnObjects");

                // ForceShadowsOnObjects: 1 literal
                if (Game::Option("ForceShadowsOnObjects", "1"))
                {
                    if (auto pattern = find_pattern("F7 47 40 00 10 00 00 74 07 81 4E 24 00 00"); pattern.size() > 0)
                    {
                        // The `jz` at +7 becomes an unconditional skip.
                        injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(7), opp::ops::kJmpRel8, true);
                    }
                }
            };
        }
    } forceShadowsOnObjects;


    // InfiniteCarBottomDistance

    class InfiniteCarBottomDistance
    {
    public:
        InfiniteCarBottomDistance()
        {
            InstallEvent() += []
            {
                TraceArm("InstallInfiniteCarBottomDistance");

                // InfiniteCarBottomDistance: 1 sigwrite
                if (Game::Option("InfiniteCarBottomDistance", "1"))
                {
                    {
                        if (auto pattern = find_pattern("0F 2F D0 76 25 0F 28 C2"); pattern.size() > 0)
                        {
                            // `EB 2A` (jmp +0x2A) replaces the `76 25` (jbe) at +3.
                            injector::WriteMemory<uint16_t>(pattern.get_first<uint8_t>(3), 0x2AEBu, true);
                        }
                    }
                }
            };
        }
    } infiniteCarBottomDistance;


    // NoImpostorHeightCutoff

    class NoImpostorHeightCutoff
    {
    public:
        NoImpostorHeightCutoff()
        {
            InstallEvent() += []
            {
                TraceArm("InstallNoImpostorHeightCutoff");

                // NoImpostorHeightCutoff: 2 matchliterals
                if (Game::Option("NoImpostorHeightCutoff", "1"))
                {
                    {
                        // The original NOPs the recovered literal sites: 1.0.0.0 has one,
                        // every later build has two. A different count is a view mismatch,
                        // so the arm installs nothing (a partial set is worse than none).
                        const size_t expected =
                            Game::Get().build == GameBuild::v1000 ? 1u : 2u;
                        if (auto pattern = find_pattern<0>("76 0B F3 0F 11 8C 24 88 00 00 00 EB 09"); pattern.size() > 0 &&
                            ExpectMatchCount(pattern, expected, "NoImpostorHeightCutoff literals"))
                        {
                            for (size_t i = 0; i < expected; ++i)
                                injector::MakeNOP(pattern.get(i).get<uint8_t>(), kWordLength, true);
                        }
                    }
                }
            };
        }
    } noImpostorHeightCutoff;


    // NoLODLightHeightCutoff

    class NoLODLightHeightCutoff
    {
    public:
        NoLODLightHeightCutoff()
        {
            InstallEvent() += []
            {
                TraceArm("InstallNoLODLightHeightCutoff");

                // NoLODLightHeightCutoff: 1 literalstore
                if (Game::Option("NoLODLightHeightCutoff", "0"))
                {
                    if (auto pattern = find_pattern("76 08 F3 0F 11 4C 24 1C EB 06 F3 0F 11 44 24 1C 89 5C 24 64"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeNOP(match, kWordLength, true);
                    }
                }
            };
        }
    } noLODLightHeightCutoff;


    // RemoveStaticCarShadows

    class RemoveStaticCarShadows
    {
    public:
        RemoveStaticCarShadows()
        {
            InstallEvent() += []
            {
                TraceArm("InstallRemoveStaticCarShadows");

                // RemoveStaticCarShadows: 1 literal
                if (Game::Option("RemoveStaticCarShadows", "0"))
                {
                    if (auto pattern = find_pattern("F3 0F 11 4C 24 0C 76 5B D9 45 14"); pattern.size() > 0)
                    {
                        // The `jbe` (`76 5B`) sits six bytes into the match; the fix
                        // replaces its opcode with a short `jmp`.
                        injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(6), opp::ops::kJmpRel8, true);
                    }
                }
            };
        }
    } removeStaticCarShadows;


    // RemoveStaticCutsceneCarShadows

    class RemoveStaticCutsceneCarShadows
    {
    public:
        RemoveStaticCutsceneCarShadows()
        {
            InstallEvent() += []
            {
                TraceArm("InstallRemoveStaticCutsceneCarShadows");

                // RemoveStaticCutsceneCarShadows: 1 literal
                if (Game::Option("RemoveStaticCutsceneCarShadows", "0"))
                {
                    if (auto pattern = find_pattern("F3 0F 11 44 24 0C 76 5B D9 45 14"); pattern.size() > 0)
                    {
                        // The `jbe` (`76 5B`) sits six bytes into the match; the fix
                        // replaces its opcode with a short `jmp`.
                        injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(6), opp::ops::kJmpRel8, true);
                    }
                }
            };
        }
    } removeStaticCutsceneCarShadows;


    // ReversingLightFix

    class ReversingLightFix
    {
    public:
        ReversingLightFix()
        {
            InstallEvent() += []
            {
                TraceArm("InstallReversingLightFix");

                // ReversingLightFix: 1 literal
                if (Game::Option("ReversingLightFix", "1"))
                {
                    if (auto pattern = find_pattern("11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10"); pattern.size() > 0)
                    {
                        // 0x60 is `pushad` (readable's kPushadOpcode): the original
                        // replaces the byte at +8 with it.
                        injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(8), 96, true);  // pushad
                    }
                }
            };
        }
    } reversingLightFix;

    // PipeBombDropIconSupport

    class PipeBombDropIconSupport
    {
    public:
        PipeBombDropIconSupport()
        {
            InstallEvent() += []
            {
                TraceArm("InstallPipeBombDropIconSupport");

                // PipeBombDropIconSupport: 1 literal
                if (Game::Option("PipeBombDropIconSupport", "1"))
                {
                    if (BuildIs(Game::Get().build,
                                {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
                                 GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
                                 GameBuild::v1130, GameBuild::v12043}))
                    {

                        if (auto pattern = find_pattern("83 7D 18 1C 75 14"); pattern.size() > 0)
                        {
                            auto* match = pattern.get_first<uint8_t>();

                            // The nine-byte argument-setup sequence before the
                            // `cmp [ebp+18], 1C` check is NOP'd by the original.
                            injector::MakeNOP(match - 9, 9, true);
                        }
                    }
                }
            };
        }
    } pipeBombDropIconSupport;


    // RestoreDeathMusic

    class RestoreDeathMusic
    {
    public:
        RestoreDeathMusic()
        {
            InstallEvent() += []
            {
                TraceArm("InstallRestoreDeathMusic");

                // RestoreDeathMusic: 3 siginstalls. The first two call sites are 13
                // bytes before their matches (the E8 replaced there); the third is the
                // call at the match itself.
                if (Game::Option("RestoreDeathMusic", "0"))
                {
                    if (auto pattern = find_pattern("0F 57 C0 F3 0F 11 86 20 02 00 00"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeCALL(match - 13, OPP_BODY(abi_death_music_install), true);
                    }

                    if (auto pattern = find_pattern("C7 86 04 02 00 00 04 00 00 00 E9"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeCALL(match - 13, OPP_BODY(abi_death_music_enabled), true);
                    }

                    if (auto pattern = find_pattern("E8 ? ? ? ? 84 C0 74 C6 8B BE"); pattern.size() > 0)
                    {
                        auto* match = pattern.get_first<uint8_t>();

                        injector::MakeCALL(match, OPP_BODY(abi_death_music_enabled), true);
                    }
                }
            };
        }
    } restoreDeathMusic;


} // namespace opp::features

// ---- hook bodies ----
//
// The machine-level entry points graphics installs: every __asm/__declspec(naked)
// definition, in one block after the installers. Each keeps the original site's calling
// convention and register contract (its comment names the destination VA); the C++ bodies
// they call stay above, in the readable part. 32-bit only - the x64 parity dump resolves
// names to the original VAs.

#if !defined(_WIN64)

// The C++ bodies the shims call stay above, in the readable part; the inline
// assembler resolves them by name in this translation unit.
extern "C" void abi_fchs_check() noexcept;
extern "C" void __cdecl abi_dual_headlights_init();

extern "C"
{

// CutsceneFixes chain B's VirtualProtect slot: the body loads this slot instead
// of the @-decorated import pointer, which the inline assembler cannot name
// (same value).
uint32_t zpatch_cutscene_virtualprotect = 0;
struct CutsceneVirtualProtectInit final
{
    CutsceneVirtualProtectInit() noexcept
    {
        zpatch_cutscene_virtualprotect = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&VirtualProtect));
    }
};
CutsceneVirtualProtectInit g_cutscene_virtualprotect_init;

// 0x1001af60 EmissiveLerpFix: register-exact transcription of the original
// tramp. It saves ecx/edx, runs sub_1001AF20 (inlined: the integer state
// machine over dword_10159CC0/CCC), then does the SSE tail and tail-jumps to
// the resume (match-12, game 0x7F8BAD).
//
// The tail must stay bit-exact.
//   cvtsi2ss xmm0, edx           merges, keeping xmm0[127:32] from the caller
//   mulss    xmm0, [0x10159cc4]  multiplies by the slot's BYTES (match-16),
//                                not a dereference
//   xmm1..xmm7 untouched         the game keeps them live across the site
// MSVC inline asm cannot spell cvtsi2ss/mulss, so the four-byte encodings are
// emitted. Not byte-exact (the helper is inlined).
__declspec(naked) void abi_emissive_lerp_shim()
{
    __asm {
        push eax
        mov DWORD PTR [zpatch_emissive_ecx], ecx
        mov DWORD PTR [zpatch_emissive_edx], edx
        // --- inlined sub_1001AF20 (0x1001af20) ---
        mov ecx, DWORD PTR [zpatch_emissive_ecx]
        xor edx, edx
        mov eax, DWORD PTR [zpatch_emissive_edx]
        shl ecx, 4
        sub ecx, DWORD PTR [zpatch_emissive_ecx]
        mov DWORD PTR [zpatch_emissive_edx], 0
        lea eax, [eax + ecx*4]
        mov ecx, 0Ch
        add eax, -405
        cmp eax, 2EDh
        __emit 0Fh
        __emit 46h
        __emit 0D1h                  // cmovbe edx, ecx
        mov DWORD PTR [zpatch_emissive_ecx], edx
        // --- reload ecx/edx exactly like the original tramp ---
        mov ecx, DWORD PTR [zpatch_emissive_ecx]
        mov edx, DWORD PTR [zpatch_emissive_edx]
        __emit 0F3h
        __emit 0Fh
        __emit 2Ah
        __emit 0C2h                  // cvtsi2ss xmm0, edx (merge upper bits)
        mov eax, OFFSET zpatch_emissive_const
        __emit 0F3h
        __emit 0Fh
        __emit 59h
        __emit 00h                   // mulss xmm0, DWORD PTR [eax]
        pop eax
        jmp DWORD PTR [zpatch_emissive_resume]
    }
}

// @exact 0x100068A0 CarPartsShadowFix's other slot body: calls the published
// thunk [dword_1015B5B8] twice with (esi, 2|0, ebx, ebx, edi), pops the 20
// argument bytes and tails to [dword_1015BDCC]. The NOPs are original bytes.
__declspec(naked) void abi_carparts_shadow_2()
{
    __asm {
        push esi
        nop
        push 2
        nop
        push ebx
        nop
        push ebx
        nop
        push edi
        nop
        call dword ptr [zpatch_carparts_shadow_fn]
        push esi
        nop
        push 0
        nop
        push ebx
        nop
        push ebx
        nop
        push edi
        nop
        call dword ptr [zpatch_carparts_shadow_fn]
        nop
        add  esp, 14h
        nop
        jmp  dword ptr [zpatch_carparts_resume_2]
        __emit 0CCh   // body delimiter for verify_hook_bodies
    }
}

// @exact 0x100068D0 CarDynamicShadowFix body B (pattern-B variant, unused on
// 1.0.8.0 where pattern A is found): same double call through
// [dword_1015B5B8] with (esi, 2, 0, 0, edi) / (esi, 0, 0, 0, edi), 20 argument
// bytes popped, tail-jump to [dword_1015BDCC].
__declspec(naked) void abi_carparts_shadow_2b()
{
    __asm {
        push esi
        nop
        push 2
        nop
        push 0
        nop
        push 0
        nop
        push edi
        nop
        call dword ptr [zpatch_carparts_shadow_fn]
        nop
        push esi
        nop
        push 0
        nop
        push 0
        nop
        push 0
        nop
        push edi
        nop
        call dword ptr [zpatch_carparts_shadow_fn]
        nop
        add  esp, 14h
        nop
        jmp  dword ptr [zpatch_carparts_resume_2]
        __emit 0CCh   // body delimiter for verify_hook_bodies
    }
}

// @exact 0x10006880 CarPartsShadowFix shim (NOPs are original bytes). The site is
// inside sub_40C9F0, the D3D render-state restore: dword_1745F24 holds the saved
// {grcRenderState, value} pairs (+0x18) and their count (+0x1C), and the function
// replays each pair through g_pD3DDevice->SetRenderState. The shim's own work is
// the null guard on dword_1745F24 before the count read.
//   cmp edi,0 / nop / je else / nop
//   movzx ebx,word [edi+1Ch] / nop / xor esi,esi / nop
//   test ebx,ebx / nop / jmp [dword_1015BDC8]   ; resume = match+2
// else: nop / jmp [dword_1015BDC0]              ; resume = match+0x45
// The EBX/ESI setup and its flags are consumed by the resumed code; edi and the
// rest of the register state must reach the resume untouched.
__declspec(naked) void abi_carparts_shadow()
{
    __asm {
        cmp  edi, 0
        nop
        je   carparts_else
        nop
        movzx ebx, word ptr [edi + 1Ch]
        nop
        xor  esi, esi
        nop
        test ebx, ebx
        nop
        jmp  dword ptr [zpatch_carparts_resume_if]
    carparts_else:
        nop
        jmp  dword ptr [zpatch_carparts_resume_else]
        __emit 0CCh   // body delimiter for verify_hook_bodies
    }
}

// @exact 0x1001ADB0 ForceCarHeadlightShadows thunk (installer E9s the game
// function at pattern "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B",
// game 0x8F7590, resume = match+12).
//   mov [dword_1015BDD0],ecx; pushad; call sub_1001AD20; popad
//   push ebp / mov ebp,esp / and esp,-16 / sub esp,0xD4  ; replayed prologue
//   jmp [dword_1015B5BC]
// The replay is essential: those 12 bytes belong to the function the E9
// replaced, so the resume must run with the frame already established.
__declspec(naked) void abi_fchs_shim()
{
    __asm {
        mov  dword ptr [zpatch_fchs_param], ecx
        pushad
        call abi_fchs_check
        popad
        push ebp
        mov  ebp, esp
        and  esp, 0FFFFFFF0h
        sub  esp, 0D4h
        jmp  dword ptr [zpatch_fchs_resume]
    }
}

// DualVehicleHeadlights (handler 0x10006770, E9 at the game match): bodies
// 0x100064F0 (new builds) / 0x10006630 (builds 1..6) duplicate the headlight-
// cone draw call with the game's custom register convention (args in
// eax/edx/ecx/ebx/ebp/edi/esi plus stack floats), saving every GPR and drawing
// slots 1 and 2 through the published pointers before tailing to match+338.
// abi_dual_headlights_init sets the constants the original's sub_100064D0 sets.

// @exact 0x100064F0
__declspec(naked) void abi_dual_headlights_new()
{
    __asm {
        pushad
        call abi_dual_headlights_init
        popad
        mov DWORD PTR [zpatch_dual_r_eax], eax
        mov DWORD PTR [zpatch_dual_r_ebx], ebx
        mov DWORD PTR [zpatch_dual_r_ecx], ecx
        mov DWORD PTR [zpatch_dual_r_edx], edx
        mov DWORD PTR [zpatch_dual_r_esi], esi
        mov DWORD PTR [zpatch_dual_r_edi], edi
        mov DWORD PTR [zpatch_dual_r_ebp], ebp
        mov DWORD PTR [zpatch_dual_r_esp], esp
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        push eax
        mov eax, DWORD PTR [esp + 30h]
        fld DWORD PTR [esp + 1Ch]
        push 1
        push DWORD PTR [zpatch_dual_light_a]
        mov ecx, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add eax, 30h
        fld DWORD PTR [esp + 38h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp]
        push eax
        push ecx
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        mov eax, DWORD PTR [zpatch_dual_r_eax]
        mov ebx, DWORD PTR [zpatch_dual_r_ebx]
        mov ecx, DWORD PTR [zpatch_dual_r_ecx]
        mov edx, DWORD PTR [zpatch_dual_r_edx]
        mov esi, DWORD PTR [zpatch_dual_r_esi]
        mov edi, DWORD PTR [zpatch_dual_r_edi]
        mov ebp, DWORD PTR [zpatch_dual_r_ebp]
        mov esp, DWORD PTR [zpatch_dual_r_esp]
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        fld DWORD PTR [esp + 18h]
        mov ecx, DWORD PTR [esp + 28h]
        push eax
        push 1
        push DWORD PTR [zpatch_dual_light_b]
        mov eax, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add ecx, 30h
        fld DWORD PTR [esp + 38h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp]
        push ecx
        push eax
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        jmp DWORD PTR [zpatch_dual_be0]
        __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
    }
}

// @exact 0x10006630
__declspec(naked) void abi_dual_headlights_old()
{
    __asm {
        pushad
        call abi_dual_headlights_init
        popad
        mov DWORD PTR [zpatch_dual_r_eax], eax
        mov DWORD PTR [zpatch_dual_r_ebx], ebx
        mov DWORD PTR [zpatch_dual_r_ecx], ecx
        mov DWORD PTR [zpatch_dual_r_edx], edx
        mov DWORD PTR [zpatch_dual_r_esi], esi
        mov DWORD PTR [zpatch_dual_r_edi], edi
        mov DWORD PTR [zpatch_dual_r_ebp], ebp
        mov DWORD PTR [zpatch_dual_r_esp], esp
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        push eax
        mov eax, DWORD PTR [esp + 30h]
        fld DWORD PTR [esp + 20h]
        push 1
        push DWORD PTR [zpatch_dual_light_a]
        mov ecx, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add eax, 30h
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 34h]
        fstp DWORD PTR [esp]
        push eax
        push ecx
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        mov eax, DWORD PTR [zpatch_dual_r_eax]
        mov ebx, DWORD PTR [zpatch_dual_r_ebx]
        mov ecx, DWORD PTR [zpatch_dual_r_ecx]
        mov edx, DWORD PTR [zpatch_dual_r_edx]
        mov esi, DWORD PTR [zpatch_dual_r_esi]
        mov edi, DWORD PTR [zpatch_dual_r_edi]
        mov ebp, DWORD PTR [zpatch_dual_r_ebp]
        mov esp, DWORD PTR [zpatch_dual_r_esp]
        push 0
        call DWORD PTR [zpatch_dual_bd0]
        add esp, 4
        push eax
        mov ecx, edi
        call DWORD PTR [zpatch_dual_be4]
        fld DWORD PTR [esp + 1Ch]
        mov ecx, DWORD PTR [esp + 28h]
        push eax
        push 1
        push DWORD PTR [zpatch_dual_light_b]
        mov eax, DWORD PTR [ebp + 24h]
        sub esp, 10h
        fstp DWORD PTR [esp + 0Ch]
        add ecx, 30h
        fld DWORD PTR [esp + 30h]
        fstp DWORD PTR [esp + 8]
        fld DWORD PTR [esp + 2Ch]
        fstp DWORD PTR [esp + 4]
        fld DWORD PTR [esp + 34h]
        fstp DWORD PTR [esp]
        push ecx
        push eax
        movsx edx, BYTE PTR [edi + 40h]
        mov esi, DWORD PTR [edi + 48h]
        mov edi, DWORD PTR [zpatch_dual_bcc]
        lea ecx, [esp + 54h]
        call DWORD PTR [zpatch_dual_bd4]
        add esp, 24h
        jmp DWORD PTR [zpatch_dual_be0]
        __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
    }
}

// @exact 0x1001B2E0 CarDecalsFix: the original scans for
// "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00" (unique on
// 1.0.8.0 at game 0xAF7F50, a VA: image base 0x400000) and E9s the first match
// to this replacement of the game function's entry.
//   a2 = [esp+8]; if ((a2[0x28] & 0x3C0) == 0x80) { a1 = [esp+4];
//      if (a1[0x38]) return a1[0x34]; } else { a1 = [esp+4]; }
//   return a1[0x30];   (__stdcall, ret 8)
__declspec(naked) void abi_car_decals_fix()
{
    __asm {
        mov eax, dword ptr [esp + 8]
        mov ecx, dword ptr [eax + 28h]
        and ecx, 3C0h
        cmp ecx, 80h
        jne car_decals_other
        mov eax, dword ptr [esp + 4]
        cmp dword ptr [eax + 38h], 0
        je  car_decals_load
        mov eax, dword ptr [eax + 34h]
        ret 8
    car_decals_other:
        mov eax, dword ptr [esp + 4]
    car_decals_load:
        mov eax, dword ptr [eax + 30h]
        ret 8
    }
}

// BuildingDynamicShadows: shim installed at the sig
// `8B 47 28 C1 E8 06 83 E0 0F 83 C0`. If [edi] is one of the two handler-
// published model hashes it takes the special resume, else it derives the shadow
// level `(([edi+0x28]>>6)&0xf)-1` and takes the normal resume (match+0xf /
// match+0x70).
// @exact 0x10005AF0
__declspec(naked) void abi_building_dynamic_shadows()
{
    __asm {
        mov eax, DWORD PTR [edi]
        cmp eax, DWORD PTR [zpatch_bds_hash_a]
        je bds_special
        cmp eax, DWORD PTR [zpatch_bds_hash_b]
        je bds_special
        mov eax, DWORD PTR [edi + 28h]
        shr eax, 6
        and eax, 0Fh
        add eax, -1
        jmp DWORD PTR [zpatch_bds_resume_a]
    bds_special:
        jmp DWORD PTR [zpatch_bds_resume_b]
    }
}

// The worker's sub_10003980: the resolver call with the salt in ECX and ESI.
static __declspec(naked) uint32_t zpatch_cutscene_scene()
{
    __asm {
        push esi
        mov esi, ecx
        mov eax, dword ptr [zpatch_driveby_resolver]
        call eax
        pop esi
        ret
    }
}

// The worker's sub_1005FE7B cookie check (cookie in ECX), same body as the
// spawns probe's: it must preserve EAX/AL because the cutscene body returns
// its result in AL across this call.
static __declspec(naked) void zpatch_cutscene_cookie_check()
{
    __asm {
        jmp zpatch_spawns_cookie_check_ecx
    }
}

// 0x100388C0 CutsceneFixes chain B (original sub_100388C0..0x10038B3F, the dest
// of the E8 installs the worker 0x10038B40 writes at the two camera call sites).
// thiscall char(float). Reads a scene float through the loader-published resolver
// (salt dword_1015803C, ecx/esi), pokes the camera function's `mov byte
// [esp+0x1C],imm` at target+0x4E, calls it and, once the accumulated frame time
// passes 0.3333, repeats with the immediate 0 and restores the pre-call object
// when the position floats differ beyond (0.05,0.05,0.05,0.5,0.5,0.5).
// @exact 0x100388C0
__declspec(naked) void abi_cutscene_chain_b()
{
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0A18h
        mov eax, dword ptr [__security_cookie]
        xor eax, ebp
        mov dword ptr [ebp - 4], eax
        push esi
        mov esi, ecx
        mov ecx, dword ptr [zpatch_salt_3c]
        push edi
        mov dword ptr [ebp - 0A10h], esi
        call zpatch_cutscene_scene
        lea edi, [ebp - 504h]
        mov ecx, dword ptr [eax + 0Eh]
        mov eax, dword ptr [ecx + eax + 18h]
        mov ecx, 140h
        rep movsd
        movss xmm0, dword ptr [eax + 8]
        lea eax, [ebp - 0A08h]
        mov esi, dword ptr [zpatch_cutscene_poke]
        mov edi, dword ptr [zpatch_cutscene_virtualprotect]
        dec esi
        cvtps2pd xmm0, xmm0
        push eax
        push 40h
        push 1
        push esi
        addsd xmm0, qword ptr [zpatch_cutscene_accum]
        movsd qword ptr [zpatch_cutscene_accum], xmm0
        call edi
        lea eax, [ebp - 0A08h]
        mov byte ptr [esi], 1
        push eax
        push dword ptr [ebp - 0A08h]
        push 1
        push esi
        call edi
        mov eax, dword ptr [zpatch_cutscene_target]
        movss xmm0, dword ptr [ebp + 8]
        push ecx
        mov ecx, dword ptr [ebp - 0A10h]
        mov dword ptr [ebp - 0A14h], eax
        movss dword ptr [esp], xmm0
        call dword ptr [ebp - 0A14h]
        fstp qword ptr [ebp - 0A18h]
        movsd xmm0, qword ptr [ebp - 0A18h]
        ucomisd xmm0, qword ptr [zpatch_cutscene_zero]
        lahf
        test ah, 44h
        jnp ccb_al_zero
        mov al, 1
        jmp ccb_al_done
    ccb_al_zero:
        xor al, al
    ccb_al_done:
        mov edx, dword ptr [ebp - 0A10h]
        lea edi, [ebp - 0A04h]
        movsd xmm0, qword ptr [zpatch_cutscene_limit]
        mov ecx, 140h
        comisd xmm0, qword ptr [zpatch_cutscene_accum]
        mov esi, edx
        mov byte ptr [ebp - 0A09h], al
        rep movsd
        ja ccb_return_al
        mov edi, edx
        lea eax, [ebp - 0A08h]
        push eax
        mov ecx, 140h
        lea esi, [ebp - 504h]
        rep movsd
        mov esi, dword ptr [zpatch_cutscene_poke]
        mov edi, dword ptr [zpatch_cutscene_virtualprotect]
        dec esi
        push 40h
        push 1
        push esi
        call edi
        lea eax, [ebp - 0A08h]
        mov byte ptr [esi], 0
        push eax
        push dword ptr [ebp - 0A08h]
        push 1
        push esi
        call edi
        mov eax, dword ptr [zpatch_cutscene_target]
        movss xmm0, dword ptr [ebp + 8]
        push ecx
        mov ecx, dword ptr [ebp - 0A10h]
        mov dword ptr [ebp - 0A14h], eax
        movss dword ptr [esp], xmm0
        call dword ptr [ebp - 0A14h]
        fstp qword ptr [ebp - 0A18h]
        movsd xmm0, qword ptr [ebp - 0A18h]
        xorps xmm3, xmm3
        ucomisd xmm0, xmm3
        lahf
        test ah, 44h
        jnp ccb_dl_zero
        mov dl, 1
        jmp ccb_dl_done
    ccb_dl_zero:
        xor dl, dl
    ccb_dl_done:
        mov eax, dword ptr [ebp - 0A10h]
        lea edi, [ebp - 504h]
        movss xmm1, dword ptr [zpatch_cutscene_abs_mask]
        mov ecx, 140h
        movss xmm2, dword ptr [zpatch_cutscene_eps_a]
        mov esi, eax
        rep movsd
        movss xmm0, dword ptr [ebp - 4E4h]
        subss xmm0, dword ptr [ebp - 9E4h]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, dword ptr [ebp - 4E0h]
        subss xmm0, dword ptr [ebp - 9E0h]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, dword ptr [ebp - 4DCh]
        subss xmm0, dword ptr [ebp - 9DCh]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, dword ptr [ebp - 4C4h]
        subss xmm0, dword ptr [ebp - 9C4h]
        movss xmm2, dword ptr [zpatch_cutscene_eps_b]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, dword ptr [ebp - 4C0h]
        subss xmm0, dword ptr [ebp - 9C0h]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, dword ptr [ebp - 4BCh]
        subss xmm0, dword ptr [ebp - 9BCh]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        pop edi
        mov al, dl
        pop esi
        mov ecx, dword ptr [ebp - 4]
        xor ecx, ebp
        call zpatch_cutscene_cookie_check
        mov esp, ebp
        pop ebp
        ret 4
    ccb_restore:
        mov edi, eax
        movsd qword ptr [zpatch_cutscene_accum], xmm3
        mov al, byte ptr [ebp - 0A09h]
        lea esi, [ebp - 0A04h]
        mov ecx, 140h
        rep movsd
    ccb_return_al:
        mov ecx, dword ptr [ebp - 4]
        pop edi
        xor ecx, ebp
        pop esi
        call zpatch_cutscene_cookie_check
        mov esp, ebp
        pop ebp
        ret 4
        __emit 0CCh
    }
}

}  // extern "C"

#endif  // !defined(_WIN64)
