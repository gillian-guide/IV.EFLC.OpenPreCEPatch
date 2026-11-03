// SPDX-License-Identifier: GPL-3.0-or-later
//
// Game identity and the install driver: the executable's version, the compact build id
// the original's classifier uses, the game's joaat hash, the PlayGTAIV process probe, the
// FusionFix build probe, the original-global publish slots, and the preamble/launcher
// steps of the tiered install pass.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string_view>
#include <tlhelp32.h>
#include <winver.h>

export module opp.game;

import opp.common;
import opp.config;
import opp.log;
import opp.patch;

export namespace opp
{
    // The build classifier's helpers.
    const char* GameBuildName(GameBuild build) noexcept;
    bool BuildIs(GameBuild build, std::initializer_list<GameBuild> builds) noexcept;

    // 0x10003E50: "hash:NNNN" returns the parsed value (a 0 result falls back to
    // hashing); anything else is hashed.
    uint32_t GameHash(std::string_view text) noexcept;

    // 0x10003B80: concatenate the four decimal fields and parse the result as
    // base 10 (1.0.7.0 -> 1070).
    bool ComposeGameVersion(uint16_t major, uint16_t minor, uint16_t build,
                            uint16_t revision, uint32_t& output) noexcept;

    // The switch at 0x10055C70. Anything else is unknown and installs nothing.
    GameBuild ClassifyGameBuild(uint32_t compactVersion) noexcept;

    // Reads the running executable's VS_FIXEDFILEINFO product version.
    bool QueryGameVersion(uint32_t& compactVersion, GameBuild& build) noexcept;

    // The version gate: the 1.2.x range the original refuses, minus the one
    // Complete Edition build this reimplementation has recovered gates for.
    constexpr uint32_t kSupportedCompleteEdition = 12043; // 1.2.0.43
    constexpr bool IsUnsupportedGameVersion(uint32_t compactVersion) noexcept
    {
        return compactVersion >= 1201 &&   // 1.2.0.1
               compactVersion <= 12999 &&  // 1.2.9.99
               compactVersion != kSupportedCompleteEdition;
    }

    // The ASI loader probe. "Modern" is Ultimate ASI Loader >= 6.9.0: that release
    // added the IsUltimateASILoader export (2023-10-24, first tag v6.9.0) and the
    // extended version resource, and it is the marker FusionFix's own
    // compatibility check trusts. Any other loader (or an older one) cannot be
    // identified, so it is reported as old.
    struct AsiLoaderInfo
    {
        bool detected{};      // a loader exporting the marker is on the stack / loaded
        bool versionKnown{};  // the loader has a parseable VS_FIXEDFILEINFO
        uint32_t versionMs{};
        uint32_t versionLs{};
        std::string name;     // e.g. "dinput8.dll"
        std::string version;  // e.g. "9.7.4"
    };

    bool AsiLoaderVersionAccepted(uint32_t versionMs, uint32_t versionLs) noexcept;
    AsiLoaderInfo QueryAsiLoader() noexcept;

    // Claims this process's single-instance marker. False when another copy of
    // this module already initialized in this process (the second copy of an ASI
    // loaded by two loaders, or a renamed duplicate).
    bool ClaimSingleInstance(const wchar_t* name) noexcept;

    // The pid of PlayGTAIV.exe, or 0.
    uint32_t FindGameProcess() noexcept;

    // Applies every subscribed arm for `build`, tier by tier. When `observer` is
    // set, its hooks bracket every arm, in install order.
    void PatchGame(GameBuild build, GameImage image, const Config& config,
                   const PatchObserver& observer = {});

    // The loader's unconditional preamble: seven scans whose results the option
    // bodies read, plus the salt fallback when the build probe misses.
    struct PreambleReport
    {
        size_t resolved{};
        size_t missing{};
    };

    PreambleReport FindGamePointers(GameImage image) noexcept;

    // True when the game was started through PlayGTAIV.exe (the original's
    // process probe). The offline parity dump cannot have that process, so the
    // dump pins the answer from OPCEP_LAUNCHER_PROBE=1.
    bool LauncherProbe() noexcept;

    // Test seam: force the launcher probe (-1 = use the real process/env probe).
    // The native tests use it; the shipped ASI never sets it.
    void SetLauncherProbeOverride(bool forced) noexcept;

    // LaunchWithPlayGTAIV: rewrites the game's two "LaunchGTAIV.exe" name slots
    // to the PlayGTAIV launcher name. Runs after every arm, through the same
    // guarded writes as the arms.
    void ApplyLaunchWithPlayGTAIV(PatchContext& ctx);

    // The mapped extent of a loaded module (its PE SizeOfImage), so a probe can
    // bound its reads; a zero view when the headers are unreadable.
    ModuleView ModuleImage(HMODULE module) noexcept;

    // 0x10003E20: true when the module lacks the FusionFix build-fingerprint
    // dwords (the original probes GTAIV.EFLC.FusionFix.asi).
    bool FusionFixLacksSignature(const ModuleView& module) noexcept;

    // The FusionFix compatibility probe: a build of the fix we cannot share a
    // module with keeps its own bytes at 0x20D3/0x20D7.
    bool FusionFixGateClosed();

    // The ZMenuIV version guard: generic startup logic (the original rejects an
    // outdated ZMenuIV before the menu is built), so it lives with the other
    // process/module probes, not in the config hub.

    enum class ZMenuGuardResult : uint8_t
    {
        module_absent,
        export_absent,
        version_ok,
        version_rejected,
    };

    constexpr bool ZMenuVersionAccepted(uint32_t version) noexcept
    {
        return version >= 220625;  // ZMenuIV 0x35DD1
    }

    ZMenuGuardResult ZMenuVersionGuard(uint32_t& version) noexcept;
} // namespace opp

// The rebased accessors: global scope because the arms and dllmain call them
// unqualified, exactly as they called the rebased.h declarations.
export uintptr_t FindGlobalValue(std::string_view option) noexcept;
export uintptr_t FindGlobalSymbol(std::string_view symbol) noexcept;
export uintptr_t FindGlobalSlot(std::string_view slot_name) noexcept;
export void SetOverrideGtaDatPath(std::string_view path) noexcept;
export uint32_t& StartupTimeGlobal() noexcept;
export uint32_t& DlcRegisterRva() noexcept;
export uint32_t& DlcApplyRva() noexcept;

namespace opp
{
    namespace
    {
        // The shipped 32-bit strtoul: leading whitespace, an optional sign, then a
        // decimal / octal / hex prefix. A non-numeric tail is ignored, an overflow
        // saturates, and a non-numeric string is 0.
        uint32_t ParseDecOrHex(std::string_view text) noexcept
        {
            constexpr std::string_view kWhitespace{" \t\r\n\f\v"};

            size_t pos = 0;
            while (pos < text.size() && kWhitespace.find(text[pos]) != std::string_view::npos)
                ++pos;

            bool negative = false;
            if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
            {
                negative = text[pos] == '-';
                ++pos;
            }

            unsigned base = 10;
            if (pos < text.size() && text[pos] == '0')
            {
                base = 8;
                if (pos + 2 < text.size() &&
                    (text[pos + 1] == 'x' || text[pos + 1] == 'X') &&
                    ((text[pos + 2] >= '0' && text[pos + 2] <= '9') ||
                     (text[pos + 2] >= 'a' && text[pos + 2] <= 'f') ||
                     (text[pos + 2] >= 'A' && text[pos + 2] <= 'F')))
                {
                    pos += 2;
                    base = 16;
                }
            }

            uint32_t value = 0;
            bool any = false;
            bool overflow = false;
            for (; pos < text.size(); ++pos)
            {
                const unsigned char c = static_cast<unsigned char>(text[pos]);
                const unsigned digit = c >= '0' && c <= '9'   ? c - '0'
                                       : c >= 'a' && c <= 'z' ? c - 'a' + 10
                                       : c >= 'A' && c <= 'Z' ? c - 'A' + 10
                                                              : 36;
                if (digit >= base)
                    break;

                any = true;
                if (value > (UINT32_MAX - digit) / base)
                    overflow = true;
                else if (!overflow)
                    value = value * base + digit;
            }

            if (!any)
                return 0;
            if (overflow)
                return UINT32_MAX;
            return negative ? 0u - value : value;
        }

        uint32_t HashText(std::string_view text) noexcept
        {
            // The game's name-hash step. Ids 0 and 1 are reserved, so the
            // original bumps them out of the way.
            const bool quoted = !text.empty() && text.front() == '"';
            if (quoted)
                text.remove_prefix(1);

            uint32_t value = 0;
            for (unsigned char c : text)
            {
                if (quoted && c == '"')
                    break;
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<unsigned char>(c + ('a' - 'A'));
                else if (c == '\\')
                    c = '/';

                value = (value + c) * 0x401u;
                value = (value >> 6) ^ value;
            }

            value *= 9u;
            value = (value >> 11) ^ value;
            value *= 0x8001u;
            return value > 1u ? value : value + 2;
        }

        // Reads a module's VS_FIXEDFILEINFO. False when the module has no version
        // resource or the resource is not a fixed-file-info block.
        bool ReadFixedFileInfo(HMODULE module, VS_FIXEDFILEINFO& output) noexcept
        {
            constexpr wchar_t kRootKey[] = L"\\";

            wchar_t path[MAX_PATH]{};
            if (GetModuleFileNameW(module, path, MAX_PATH) == 0)
                return false;

            DWORD ignored = 0;
            const DWORD size = GetFileVersionInfoSizeW(path, &ignored);
            if (size == 0)
                return false;
            std::vector<uint8_t> data(size);
            if (!GetFileVersionInfoW(path, ignored, size, data.data()))
                return false;

            VS_FIXEDFILEINFO* fixed = nullptr;
            UINT length = 0;
            if (!VerQueryValueW(data.data(), kRootKey, reinterpret_cast<void**>(&fixed), &length) ||
                fixed == nullptr || length < sizeof(VS_FIXEDFILEINFO))
                return false;
            output = *fixed;
            return true;
        }
    } // namespace

    uint32_t GameHash(std::string_view text) noexcept
    {
        if (text.size() >= 5 && (text[0] == 'h' || text[0] == 'H') &&
            (text[1] == 'a' || text[1] == 'A') && (text[2] == 's' || text[2] == 'S') &&
            (text[3] == 'h' || text[3] == 'H') && text[4] == ':')
        {
            // Only a zero result falls back to hashing the text.
            const uint32_t parsed = ParseDecOrHex(text.substr(5));
            if (parsed != 0)
                return parsed;
        }
        return HashText(text);
    }

    bool ComposeGameVersion(uint16_t major, uint16_t minor, uint16_t build,
                            uint16_t revision, uint32_t& output) noexcept
    {
        const uint16_t parts[] = {major, minor, build, revision};
        uint64_t value = 0;
        for (const uint16_t part : parts)
        {
            uint32_t scale = 10;
            for (uint16_t n = part; n >= 10; n = static_cast<uint16_t>(n / 10))
                scale *= 10;

            value = value * scale + part;
            if (value > UINT32_MAX)
                return false;
        }

        output = static_cast<uint32_t>(value);
        return true;
    }

    namespace
    {
        // The original classifier's switch as one table. The key is the compact
        // file version (1.0.7.0 -> 1070); the enum value is the build ORDINAL the
        // option build masks index, not the version. The name is what the log and
        // the dump print.
        struct BuildRow final
        {
            uint32_t compact_version;
            GameBuild build;
            const char* name;
        };
        constexpr BuildRow kBuildRows[] = {
            {1000, GameBuild::v1000, "1000"},
            {1004, GameBuild::v1004, "1004"},
            {1040, GameBuild::v1040, "1040"},
            {1060, GameBuild::v1060, "1060"},
            {1061, GameBuild::v1061, "1061"},
            {1070, GameBuild::v1070, "1070"},
            {1080, GameBuild::v1080, "1080"},
            {1100, GameBuild::v1100, "1100"},
            {1110, GameBuild::v1110, "1110"},
            {1120, GameBuild::v1120, "1120"},
            {1130, GameBuild::v1130, "1130"},
            {12043, GameBuild::v12043, "12043"},
        };
        static_assert(kBuildRows[sizeof(kBuildRows) / sizeof(kBuildRows[0]) - 1]
                          .compact_version == kSupportedCompleteEdition);
    } // namespace

    GameBuild ClassifyGameBuild(uint32_t version) noexcept
    {
        for (const BuildRow& row : kBuildRows)
        {
            if (row.compact_version == version)
                return row.build;
        }
        return GameBuild::unknown;
    }

    bool BuildIs(GameBuild build, std::initializer_list<GameBuild> builds) noexcept
    {
        for (const GameBuild candidate : builds)
        {
            if (candidate == build)
                return true;
        }
        return false;
    }

    const char* GameBuildName(GameBuild build) noexcept
    {
        for (const BuildRow& row : kBuildRows)
        {
            if (row.build == build)
                return row.name;
        }
        return "unknown";
    }

    uint32_t FindGameProcess() noexcept
    {
        // UNICODE builds map the unsuffixed names to the W variants, matching the
        // original ANSI-name call site's case-sensitive full-name comparison.
        constexpr wchar_t kExecutable[] = L"PlayGTAIV.exe";

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return 0;

        uint32_t found = 0;
        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (std::wcscmp(entry.szExeFile, kExecutable) == 0)
                {
                    found = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }

    ModuleView ModuleImage(HMODULE module) noexcept
    {
        const auto* base = reinterpret_cast<const uint8_t*>(module);
        if (base == nullptr)
            return {};
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return {};
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return {};
        return {base, nt->OptionalHeader.SizeOfImage};
    }

    namespace
    {
        // GTAIV.EFLC.FusionFix.asi's build fingerprint: the two dwords its own
        // compatibility probe compares, at fixed image offsets. The prefix is the
        // image before the probe; packing to 1 keeps the offsets exact (the first
        // probe dword is not 4-byte aligned inside the fix).
#pragma pack(push, 1)
        struct FusionFixVersionProbe final
        {
            static constexpr size_t kBuildHashOffset = 0x20D3;
            static constexpr size_t kBuildFlagsOffset = 0x20D7;

            std::byte image_prefix[kBuildHashOffset];
            uint32_t build_hash;   // +kBuildHashOffset
            uint32_t build_flags;  // +kBuildFlagsOffset
        };
#pragma pack(pop)
        static_assert(offsetof(FusionFixVersionProbe, build_hash) ==
                      FusionFixVersionProbe::kBuildHashOffset);
        static_assert(offsetof(FusionFixVersionProbe, build_flags) ==
                      FusionFixVersionProbe::kBuildFlagsOffset);
        static_assert(sizeof(FusionFixVersionProbe) ==
                      FusionFixVersionProbe::kBuildFlagsOffset + sizeof(uint32_t));
    } // namespace

    bool FusionFixLacksSignature(const ModuleView& module) noexcept
    {
        // The values the fix's probe compares against its own image; opaque
        // recovered dwords.
        if (module.base == nullptr || module.size < sizeof(FusionFixVersionProbe))
            return false;
        const auto& probe = *reinterpret_cast<const FusionFixVersionProbe*>(module.base);
        return probe.build_hash != 0xC7C47589u ||
               probe.build_flags != 0x00002446u;
    }

    bool QueryGameVersion(uint32_t& compactVersion, GameBuild& build) noexcept
    {
        compactVersion = 0;
        build = GameBuild::unknown;

        VS_FIXEDFILEINFO info{};
        if (!ReadFixedFileInfo(nullptr, info) || info.dwSignature != VS_FFI_SIGNATURE)
            return false;

        // The classifier keys on the product version (dwProductVersion*), not on
        // the file version the module-version text prints.
        const auto major = static_cast<uint16_t>(info.dwProductVersionMS >> 16u);
        const auto minor = static_cast<uint16_t>(info.dwProductVersionMS);
        const auto patch = static_cast<uint16_t>(info.dwProductVersionLS >> 16u);
        const auto revision = static_cast<uint16_t>(info.dwProductVersionLS);
        if (!ComposeGameVersion(major, minor, patch, revision, compactVersion))
            return false;
        build = ClassifyGameBuild(compactVersion);
        return true;
    }
} // namespace opp

// The publish contract: one row per original global the recovered handlers
// publish, keyed by the IDA symbol name the original carried. The list is the
// single source of truth for both the slot definitions and kSlotBindings, so a
// name and the slot it resolves to cannot drift apart; a name this tree does not
// publish is a name that is not in the list.
//
// Notes on three rows that are not self-explanatory: the LoadDLCs callback slot
// receives the game's loader address, and the two RVAs are what the discovery
// routine adds to the module base at use time; "Src" is the push immediate
// NewSavesCompatibility captures; dword_10159CD8 is the allocator singleton the
// NewMemorySystem handler's second scan publishes (the immediate of
// `mov esi, 0x19cf9f0`, the game's g_systemAllocator), and its E8 body writes
// our vtable there.
#define OPCEP_SLOT_ROWS(X)                                    \
    X(dword_1015BDDC, zpatch_dlc_load_callback)               \
    X(dword_101580D0, zpatch_dlc_apply_rva)                   \
    X(dword_1015804C, zpatch_dlc_register_rva)                \
    X(dword_1015AD58, zpatch_blackscreen_resume)              \
    X(dword_1015AD5C, zpatch_blackscreen_value_ptr)           \
    X(dword_1015BDFC, zpatch_path_node_resume_ok)             \
    X(dword_1015BE00, zpatch_path_node_resume_fail)           \
    X(dword_1015BE18, zpatch_plane_crash_resume_a)            \
    X(dword_1015BE14, zpatch_plane_crash_resume_b)            \
    X(dword_1015AD6C, zpatch_wanted_level_resume)             \
    X(dword_1015AD60, zpatch_group_hack_resume)               \
    X(dword_10159C70, zpatch_smpa60_resume_a)                 \
    X(dword_10159C68, zpatch_smpa60_resume_b)                 \
    X(dword_10159C6C, zpatch_smpa60_resume_c)                 \
    X(dword_1015AD64, zpatch_freeze_gun_resume)               \
    X(dword_10159D28, zpatch_player_ped_object)               \
    X(dword_10159D20, zpatch_player_ped_computed)             \
    X(dword_10159D2C, zpatch_player_ped_resume)               \
    X(dword_10159C0C, zpatch_model_lookup)                    \
    X(dword_10159CC0, zpatch_emissive_ecx)                    \
    X(dword_10159CCC, zpatch_emissive_edx)                    \
    X(dword_10159CC4, zpatch_emissive_const)                  \
    X(dword_10159CC8, zpatch_emissive_resume)                 \
    X(dword_1015BDC8, zpatch_carparts_resume_if)              \
    X(dword_1015BDC0, zpatch_carparts_resume_else)            \
    X(dword_101580AC, zpatch_super_lod_fail)                  \
    X(dword_101580B0, zpatch_super_lod_ok)                    \
    X(dword_101580B4, zpatch_super_lod_lookup)                \
    X(dword_10159BBC, zpatch_episode_switch_resume_a)         \
    X(dword_10159BB4, zpatch_episode_switch_resume_b)         \
    X(dword_10159BB8, zpatch_episode_switch_resume_c)         \
    X(dword_1015AD7C, zpatch_bike_value)                      \
    X(dword_1015AD80, zpatch_bike_saved)                      \
    X(dword_1015AD84, zpatch_bike_resume)                     \
    X(dword_1015BDD4, zpatch_collision_deadlock_should_walk)  \
    X(dword_1015BDE0, zpatch_driveby_resolver)                \
    X(dword_1015BDF4, zpatch_driveby_resume)                  \
    X(dword_1015BDE4, zpatch_cutscene_target)                 \
    X(dword_1015BDF0, zpatch_cutscene_poke)                   \
    X(dword_10159CD8, zpatch_nms_object)                      \
    X(dword_10159B9C, zpatch_nolivery_resume)                 \
    X(dword_10159BA4, zpatch_nolivery_fn)                     \
    X(dword_10159BAC, zpatch_nolivery_resume_d)               \
    X(dword_1015AD68, zpatch_crash_msg_getter)                \
    X(dword_10159C08, zpatch_newfiles_register_slot)          \
    X(dword_1015AD50, zpatch_benchmark_resume_a)              \
    X(dword_1015AD54, zpatch_benchmark_resume_b)              \
    X(dword_10159C1C, zpatch_bds_hash_a)                      \
    X(dword_10159C24, zpatch_bds_hash_b)                      \
    X(dword_10159C20, zpatch_bds_resume_a)                    \
    X(dword_10159C28, zpatch_bds_resume_b)                    \
    X(dword_1015937C, zpatch_copsirens_resume)                \
    X(dword_10159374, zpatch_copsirens_resume_b)              \
    X(dword_1015BE10, zpatch_ide_hook_slot)                   \
    X(dword_1015934C, zpatch_speedup_t4c)                     \
    X(dword_10159350, zpatch_speedup_t50)                     \
    X(dword_10159354, zpatch_speedup_t54)                     \
    X(dword_10159358, zpatch_speedup_t58)                     \
    X(dword_1015935C, zpatch_speedup_t5c)                     \
    X(dword_10159360, zpatch_speedup_t60)                     \
    X(dword_10159364, zpatch_speedup_t64)                     \
    X(dword_10159368, zpatch_speedup_t68)                     \
    X(dword_1015D628, zpatch_popup_resume_a)                  \
    X(dword_1015D62C, zpatch_popup_resume_b)                  \
    X(dword_1015B5BC, zpatch_fchs_resume)                     \
    X(dword_10159CB8, zpatch_bpm_resume)                      \
    X(Src, zpatch_new_saves_src)                              \
    X(dword_1015AD88, zpatch_new_saves_resume)                \
    X(dword_10159C18, zpatch_gfwl_resume)                     \
    X(dword_1015B5B8, zpatch_carparts_shadow_fn)              \
    X(dword_1015BDC4, zpatch_car_dynamic_d)                   \
    X(dword_1015BDCC, zpatch_carparts_resume_2)               \
    X(dword_10159BCC, zpatch_dual_bcc)                        \
    X(dword_10159BD0, zpatch_dual_bd0)                        \
    X(dword_10159BD4, zpatch_dual_bd4)                        \
    X(dword_10159BE0, zpatch_dual_be0)                        \
    X(dword_10159BE4, zpatch_dual_be4)                        \
    X(dword_10159D08, zpatch_local_path_08)                   \
    X(dword_10159D10, zpatch_local_path_10)                   \
    X(dword_10159D14, zpatch_local_episode_ptr)               \
    X(dword_10159D18, zpatch_local_path_18)                   \
    X(dword_10159D1C, zpatch_local_path_1c)                   \
    X(dword_10159CB0, zpatch_spawns_orig_a)                   \
    X(dword_10159C98, zpatch_spawns_orig_b)                   \
    X(dword_10159CBC, zpatch_spawns_case0_resume)             \
    X(dword_10159C9C, zpatch_spawns_case1_resume)             \
    X(dword_10159CA0, zpatch_spawns_case2_resume)             \
    X(dword_10159CA8, zpatch_spawns_case3_resume)             \
    X(dword_10159C7C, zpatch_spawns_case4_resume)             \
    X(dword_10159C88, zpatch_spawns_case5_resume)             \
    X(dword_10159CAC, zpatch_spawns_case6_resume)             \
    X(dword_10159C90, zpatch_spawns_case7_resume)             \
    X(dword_10159C94, zpatch_spawns_case8_resume)             \
    X(dword_10159C84, zpatch_spawns_case9_resume)             \
    X(dword_10159CD4, zpatch_garage_resume_a)                 \
    X(dword_10159CD0, zpatch_garage_resume_b)                 \
    X(dword_1015AD44, zpatch_steer_resume)                    \
    X(dword_1015AD4C, zpatch_steer_vtable_fn)                 \
    X(dword_10159C50, zpatch_mf_x)                            \
    X(dword_10159C3C, zpatch_mf_y)                            \
    X(dword_10159C5C, zpatch_mf_flag)                         \
    X(dword_10159C34, zpatch_mf_time)                         \
    X(dword_10159C60, zpatch_mf_resume_a)                     \
    X(dword_10159C40, zpatch_mf_resume_b)                     \
    X(dword_10158034, zpatch_ooc_resume)                      \
    X(dword_10159BF8, zpatch_ooc_logger)                      \
    X(dword_10159C04, zpatch_ooc_orig)

// C linkage: the hook bodies of every feature module address these by name from
// inline asm, and rebased.h declares them in the global module fragment.
extern "C"
{
    // One zero-initialised dword per published row; an arm raises the slot to the
    // value it publishes and the loader never clears one.
#define OPCEP_SLOT_DEFINE(original, slot) uint32_t slot = 0;
    OPCEP_SLOT_ROWS(OPCEP_SLOT_DEFINE)
#undef OPCEP_SLOT_DEFINE

    // The game's episode selector (the original's unk_10158128, on-disk value 2);
    // the plugin-data publish hands the game this slot's address.
    uint32_t zpatch_episode_flag = 2;

    // CollisionDeadlockFix's walk counter, published beside the should-walk flag.
    uint32_t zpatch_collision_deadlock_iterations = 0;

    // DriveByFix: the FOV the game loads (float bits in opp.common), the cached
    // result and the one-shot flag.
    uint32_t zpatch_driveby_fov = std::bit_cast<uint32_t>(opp::kDrivebyFovDegrees);
    uint32_t zpatch_driveby_cached = 0;
    uint8_t zpatch_driveby_done = 0;

    // NoLiveryLimit scratch; the char* slots hold game pointers.
    uint32_t zpatch_nolivery_name = 0;
    uint32_t zpatch_nolivery_index = 0;
    uint32_t zpatch_nolivery_hash = 0;
    uint32_t zpatch_nolivery_arg = 0;
    uint32_t zpatch_nolivery_count = 0;
    uint32_t zpatch_nolivery_bufptr = 0;
    uint32_t zpatch_nolivery_dest = 0;

    // The original's per-model lookup capacity: a zero hash ends the walk and the
    // INI fills every slot, so the capacity is load-bearing for both fill and read.
    constexpr size_t kModelLookupSlots = 512;

    // CopSirensFlag: the shim's scratch and the siren, popup-light and wheel-angle
    // model tables.
    uint32_t zpatch_copsirens_index = 0;
    uint32_t zpatch_copsirens_index_b = 0;
    uint32_t zpatch_copsirens_flag = 0;
    uint32_t zpatch_copsirens_flag_b = 0;
    uint32_t zpatch_ide_string = 0;
    uint32_t zpatch_ide_model = 0;
    uint32_t zpatch_copsirens_models[kModelLookupSlots] = {};
    uint32_t zpatch_popuplights_models[kModelLookupSlots] = {};
    uint32_t zpatch_wheelangle_models[kModelLookupSlots] = {};
    uint32_t zpatch_wheelangle_angles[kModelLookupSlots] = {};

    // OverrideGTADatPath: the original pushed its own "common:/data/gta.dat"
    // global; we push this buffer instead (filled from OverrideGTADatPathTo).
    char zpatch_override_gta_dat_path[MAX_PATH] = "common:/data/gta.dat";

    // PopupHeadlightsFlag: the shim's scratch, its per-model tables and the
    // fallback angle the game's fld operand loads (dword_101580CC is pre-set to
    // pi/2 on disk and rewritten by the shim's checker on every call).
    uint32_t zpatch_popup_model = 0;
    uint32_t zpatch_popup_flag = 0;
    uint32_t zpatch_popup_index = 0;
    uint32_t zpatch_popup_angle = std::bit_cast<uint32_t>(opp::kHalfPi);
    uint32_t zpatch_popup_hashes[kModelLookupSlots] = {};
    float zpatch_popup_angles[kModelLookupSlots] = {};

    // ForceCarHeadlightShadows: the shim's object slot, its per-model tables and
    // the fallback offset the game reads (dword_101580C0 is pre-set to 2.0f on
    // disk).
    uint32_t zpatch_fchs_param = 0;
    uint32_t zpatch_fchs_offset = std::bit_cast<uint32_t>(opp::kDefaultConeOffset);
    uint32_t zpatch_fchs_hashes[kModelLookupSlots] = {};
    float zpatch_fchs_offsets[kModelLookupSlots] = {};

    // BadPlayerModelCrashFix scratch.
    uint32_t zpatch_bpm_obj = 0;
    uint32_t zpatch_bpm_model_index = 0;

    // NewSavesCompatibility's chunk probes: 1 when the save's chunk at that index
    // has the new format's size. Chunk 13 gates the p3 hook's 12-byte reads, chunk
    // 16 selects the 3694-byte read, chunk 9 gates the P1 branch.
    uint32_t zpatch_new_saves_chunk_len = 0;
    uint8_t zpatch_new_saves_chunk9_new = 0;
    uint8_t zpatch_new_saves_chunk13_new = 0;
    uint8_t zpatch_new_saves_chunk16_new = 0;

    // The loader preamble's unconditionally resolved pointers: the shared model
    // lookup, the script resolver, the ped array and four crash-fix functions.
    uint32_t zpatch_pre_ped_array = 0;
    uint32_t zpatch_pre_fn_59c74 = 0;
    uint32_t zpatch_pre_fn_59b88 = 0;
    uint32_t zpatch_pre_fn_5d63c = 0;
    uint32_t zpatch_pre_fn_59cdc = 0;

    // The ten build-fingerprint salts. The loader rewrites them only when its
    // build probe misses, so these are the probe-hit set; only the native
    // scripting layer reads them, and each value is recovered (no derivation).
    // The identifiers still carry the original dword offsets (renaming them is a
    // separate pass across rebased.h and every scripting consumer).
    uint32_t zpatch_salt_28 = 0x6A6B4F18u;
    uint32_t zpatch_salt_3c = 0x022B2DA9u;
    uint32_t zpatch_salt_40 = 0x4BF36A32u;
    uint32_t zpatch_salt_50 = 0x3B0C6738u;
    uint32_t zpatch_salt_54 = 0x511454A9u;
    uint32_t zpatch_salt_5c = 0x62E319C6u;
    uint32_t zpatch_salt_a8 = 0x79A95BF9u;
    uint32_t zpatch_salt_b8 = 0x4E61480Au;
    uint32_t zpatch_salt_c8 = 0x570F5725u;
    uint32_t zpatch_salt_d4 = 0x75E005F1u;

    // Module-presence flags (set by the loader, exactly like the original).
    uint8_t zpatch_xliveless_present = 0;
    uint8_t zpatch_citizen_present = 0;
    uint32_t zpatch_xlive_base = 0;

    // Original one-time scan caches and the shared ASI TlsIndex.
    uint32_t zpatch_gfwl_scan_guard = 0;
    uint32_t zpatch_gfwl_site = 0;
    uint32_t zpatch_speedup_scan_guard = 0;
    uint32_t zpatch_speedup_clock = 0;
    uint32_t zpatch_tls_index = 0;

    // Live option-flag globals. Every one starts at 0: the loader arms only raise
    // a flag to 1, so an option that is off leaves it at 0.
    uint8_t zpatch_flag_death_music = 0u;
    uint8_t zpatch_flag_patch4_modchecks = 0u;
    uint8_t zpatch_flag_gfwl_popup = 0u;
    uint8_t zpatch_flag_debug_spawns = 0u;

    // RestoreDeathMusic's lazily resolved site and NewSavesCompatibility's
    // remaining state.
    uint32_t zpatch_death_music_site = 0;
    uint32_t zpatch_new_saves_element_count = 0;
    uint32_t zpatch_new_saves_loaddata = 0;
    uint32_t zpatch_new_saves_orig_handler = 0;

    // DualVehicleHeadlights: the two on-disk constants and the register scratch.
    uint32_t zpatch_dual_light_a = 1u;
    uint32_t zpatch_dual_light_b = 2u;
    uint32_t zpatch_dual_r_eax = 0;
    uint32_t zpatch_dual_r_ebx = 0;
    uint32_t zpatch_dual_r_ecx = 0;
    uint32_t zpatch_dual_r_edx = 0;
    uint32_t zpatch_dual_r_esi = 0;
    uint32_t zpatch_dual_r_edi = 0;
    uint32_t zpatch_dual_r_ebp = 0;
    uint32_t zpatch_dual_r_esp = 0;

    // LocalSettingsPath: the CWD captured at load, the "\GTA IV" folder suffix the
    // game's path helper is retargeted to push, and the 4-byte call slot the game
    // calls through (original off_10158064 = sub_10051320).
    char zpatch_local_ansi_cwd[MAX_PATH] = {};
    wchar_t zpatch_local_wide_cwd[MAX_PATH] = {};
    char zpatch_local_iv_suffix[8] = "\\GTA IV";
    uint32_t zpatch_local_saves_slot = 0;

    // FreezeCarFix / DebugPrintSpawns state. zpatch_spawns_kind is "no spawn case
    // recorded yet": it is a four-byte kind field, so the sentinel is all ones.
    uint32_t zpatch_spawns_kind = 0xFFFFFFFFu;
    uint32_t zpatch_spawns_class = 0;
    uint32_t zpatch_spawns_skip = 0;
    char zpatch_fmt_spawned_car[] = "%s spawned a car (%s)";
    char zpatch_fmt_nonspawned_car[] = "%s spawned a non-car car (%s)";
    char zpatch_fmt_spawned_heli[] = "%s spawned a heli (%s)";
    char zpatch_fmt_nonspawned_heli[] = "%s spawned a non-heli heli (%s)";
    char zpatch_fmt_spawned_boat[] = "%s spawned a boat (%s)";
    char zpatch_fmt_nonspawned_boat[] = "%s spawned a non-boat boat (%s)";
    char zpatch_fmt_spawned_bike[] = "%s spawned a bike (%s)";
    char zpatch_fmt_nonspawned_bike[] = "%s spawned a non-bike bike (%s)";
    char zpatch_fmt_spawned_train[] = "%s spawned a train (%s)";
    char zpatch_fmt_nonspawned_train[] = "%s spawned a non-train train (%s)";
    char zpatch_fmt_spawned_plane[] = "%s spawned a plane (%s)";
    char zpatch_fmt_nonspawned_plane[] = "%s spawned a non-plane plane (%s)";
    char zpatch_fmt_spawned_ped[] = "%s spawned a ped (%s)";
    char zpatch_fmt_nonspawned_ped[] = "%s spawned a non-ped ped (%s)";
    char zpatch_fmt_spawned_vehicle_object[] = "%s spawned a vehicle object (%s)";
    char zpatch_fmt_spawned_object[] = "%s spawned an object (%s)";
    char zpatch_fmt_spawned_generic[] = "%s spawned (%s)";
    char zpatch_fmt_used_nonped[] = "%s used a non-ped model (%s)";
    wchar_t zpatch_str_gtac[] = L"GTAC.dll";
    // The spawn probe's GetModuleHandleW call goes through this slot (MSVC's inline
    // assembler cannot name the @-decorated import pointer); the body fills it at load.
    uint32_t zpatch_spawns_getmodulehandlew = 0;

    // GarageModelFix's remaining slot and scratch.
    uint32_t zpatch_garage_entry_resume_a = 0;
    float zpatch_garage_scale = 0.0f;

    // The steering trio's call target and the engine fix's state.
    uint32_t zpatch_steer_vehicle = 0;
    float zpatch_engine_last_timestep = 0.0f;
    float zpatch_engine_phase = 0.0f;
    uint8_t zpatch_engine_descending = 0;
    uint8_t zpatch_flag_steering = 0u;
    uint8_t zpatch_flag_engine = 0u;
    uint8_t zpatch_flag_plane = 0u;

    // MouseFix's numeric pool. The globals are not const because the inline
    // assembler addresses them as plain external symbols and nothing writes them.
    // They are one derivation: the raw mouse count is scaled into an axis by
    // 1/128, the axis range is 255 with 255/2 as its centre, and 0.1 is the
    // sensitivity step. The timing and wrap doubles are the original's 8-digit
    // decimals, which are not the nearest doubles of 0.075/0.025/pi/2pi, so their
    // bit patterns are pinned.
    constexpr double kMouseFrameStep = 0.075000003;
    constexpr double kMouseWrapHalfTurn = 3.1415927;
    constexpr double kMouseWrapFullTurn = 6.2831855;
    static_assert(std::bit_cast<uint64_t>(kMouseFrameStep) == 0x3FB333334015BC21ull);
    static_assert(std::bit_cast<uint64_t>(kMouseWrapHalfTurn) == 0x400921FB5A7ED197ull);
    static_assert(std::bit_cast<uint64_t>(kMouseWrapFullTurn) == 0x401921FB6134CE3Eull);

    float zpatch_mf_xmm_save[8] = {};
    double zpatch_mf_scale = 1.0 / 128.0;  // raw count -> axis
    double zpatch_mf_scale_255 = 255.0;    // axis range
    float zpatch_mf_sign = 127.5f;         // 255 / 2, the axis centre
    float zpatch_mf_mul_neg1 = -1.0f;
    float zpatch_mf_step_scale = 0.1f;
    double zpatch_mf_mul_neg1_d = -1.0;
    double zpatch_mf_time_mul = kMouseFrameStep;
    double zpatch_mf_time_add = 0.025;
    double zpatch_mf_pi = kMouseWrapHalfTurn;
    double zpatch_mf_two_pi = kMouseWrapFullTurn;
    double zpatch_mf_neg_pi = -kMouseWrapHalfTurn;

    // `xorps xmm, xmmword ptr [mask]` needs a 16-byte aligned address: the
    // original's .rdata constants are naturally aligned, so the masks are
    // alignas(16) (a misaligned mask faults with #GP/AV). The sign bit is 0x80 in
    // the top byte of each lane: four 32-bit lanes, then two 64-bit lanes.
    constexpr unsigned char kSignBit = 0x80;
    alignas(16) unsigned char zpatch_mf_neg_mask[16] = {0, 0, 0, kSignBit, 0, 0, 0, kSignBit,
                                                        0, 0, 0, kSignBit, 0, 0, 0, kSignBit};
    alignas(16) unsigned char zpatch_mf_double_sign[16] = {0, 0, 0, 0, 0, 0, 0, kSignBit,
                                                           0, 0, 0, 0, 0, 0, 0, kSignBit};

    // OutOfCommissionFix state. The two format globals are the slices of the
    // original's "memory:$<ptr>,<size>,0:" report: fmt_b supplies the "me"/"y"
    // around the %s that prints fmt_a's "mor".
    uint32_t zpatch_ooc_state = 0;
    uint32_t zpatch_ooc_input_bound = 0;
    uint32_t zpatch_ooc_output_begin = 0;
    uint32_t zpatch_ooc_output_cursor = 0;
    uint32_t zpatch_ooc_output_end = 0;
    uint32_t zpatch_ooc_table_size = 0;
    uint32_t zpatch_ooc_archive_offset = 0;
    uint32_t zpatch_ooc_vector_begin = 0;
    uint32_t zpatch_ooc_vector_end = 0;
    uint32_t zpatch_ooc_vector_capacity = 0;
    char zpatch_ooc_fmt_a[] = "mor";
    char zpatch_ooc_fmt_b[] = "me%sy%c%c%p,%d,%c%c%s";

    // CutsceneFixes chain B: the body's frame-time accumulator (the original's
    // .data BSS, so 0) and the read-only constants it compares against, stored as
    // the width the asm reads (the doubles live in opp.common).
    uint64_t zpatch_cutscene_accum = 0;
    uint64_t zpatch_cutscene_zero = std::bit_cast<uint64_t>(opp::kCutsceneZero);
    uint64_t zpatch_cutscene_limit = std::bit_cast<uint64_t>(opp::kCutsceneLimit);
    uint32_t zpatch_cutscene_abs_mask = 0x7FFFFFFFu;  // clears the sign bit
    uint32_t zpatch_cutscene_eps_a = std::bit_cast<uint32_t>(opp::kCutsceneEpsilon);
    uint32_t zpatch_cutscene_eps_b = std::bit_cast<uint32_t>(opp::kCutsceneHalf);
}

namespace
{
    // StartupTimeFix pushes the address of g_startup_time (the original pushed
    // dword_101580d8); VSyncFix reads/writes g_vsync_flag through the operand its
    // handler patches (the original pushed &unk_10159BC0).
    uint32_t g_startup_time = 0;
    uint32_t g_vsync_flag = 0;
} // namespace

// Copies `path` into the buffer the retargeted push reads, clamped and terminated.
void SetOverrideGtaDatPath(std::string_view path) noexcept
{
    // The original's default: the game's own catalog path.
    if (path.empty())
        path = "common:/data/gta.dat";
    const size_t n = path.size() < sizeof(zpatch_override_gta_dat_path) - 1
        ? path.size()
        : sizeof(zpatch_override_gta_dat_path) - 1;
    std::memcpy(zpatch_override_gta_dat_path, path.data(), n);
    zpatch_override_gta_dat_path[n] = '\0';
}

// Accessors for the rebased globals whose address a handler publishes.
uint32_t& StartupTimeGlobal() noexcept
{
    return g_startup_time;
}
uint32_t& DlcRegisterRva() noexcept
{
    return zpatch_dlc_register_rva;
}
uint32_t& DlcApplyRva() noexcept
{
    return zpatch_dlc_apply_rva;
}

// Rebases a value that addresses the original module, keyed by INI option.
// opp_record_dump_pointer (dump-tool only) is declared by opp.common; the dump
// tool defines it with C linkage.
namespace
{
    uintptr_t DumpTrack(uintptr_t value) noexcept
    {
#if defined(OPCEP_DUMP_TOOL)
        if (value != 0)
            opp_record_dump_pointer(static_cast<uint32_t>(value));
#endif
        return value;
    }

    // One named publish target: the original's symbol (or the INI option, for the
    // value table) and this tree's global.
    struct NamedSlot final
    {
        std::string_view name;
        void* address;
    };

    // The original-global publish targets keyed by INI option (the sig-install
    // `literal` rows). The addresses are stable for the process lifetime.
    constexpr NamedSlot kValueBindings[] = {
        {"StartupTimeFix", &g_startup_time},
        {"VSyncFix", &g_vsync_flag},
        {"OverrideGTADatPath", zpatch_override_gta_dat_path},
        // PopupHeadlightsFlag: the `fld` operand at +82 loads our pi/2 float.
        {"PopupHeadlightsFlag", &zpatch_popup_angle},
        // ForceCarHeadlightShadows: the operand at +211 loads our 2.0f dword.
        {"ForceCarHeadlightShadows", &zpatch_fchs_offset},
        // LocalSettingsPath: the path-helper call goes through our 4-byte slot.
        {"LocalSettingsPath", &zpatch_local_saves_slot},
    };

    // Named plugin-data symbols the sig-install `plugin_data_ptr` kind addresses
    // (a 4-byte value written at the site: the ADDRESS of the named global).
    constexpr NamedSlot kSymbolBindings[] = {
        {"zpatch_local_iv_suffix", zpatch_local_iv_suffix},
        {"zpatch_episode_flag", &zpatch_episode_flag},
    };

    template <size_t N>
    uintptr_t FindBinding(const NamedSlot (&bindings)[N], std::string_view name) noexcept
    {
        for (const NamedSlot& binding : bindings)
        {
            if (binding.name == name)
                return DumpTrack(reinterpret_cast<uintptr_t>(binding.address));
        }
        return 0;
    }
} // namespace

uintptr_t FindGlobalValue(std::string_view option) noexcept
{
    return FindBinding(kValueBindings, option);
}

uintptr_t FindGlobalSymbol(std::string_view symbol) noexcept
{
    return FindBinding(kSymbolBindings, symbol);
}

// Slot publishes are keyed by the original global's name. The rows come from
// OPCEP_SLOT_ROWS, so the key and the address it maps to are written once.
namespace
{
    constexpr NamedSlot kSlotBindings[] = {
#define OPCEP_SLOT_ROW(original, slot) {#original, &slot},
        OPCEP_SLOT_ROWS(OPCEP_SLOT_ROW)
#undef OPCEP_SLOT_ROW
    };
#undef OPCEP_SLOT_ROWS

    static_assert(sizeof(kSlotBindings) / sizeof(kSlotBindings[0]) == 107,
                  "the publish contract is 107 original globals");

    // Two original names resolving to one slot would publish the same address
    // under different keys; the row list must stay one-to-one.
    template <size_t N>
    consteval bool SlotNamesUnique(const NamedSlot (&slots)[N]) noexcept
    {
        for (size_t i = 0; i < N; ++i)
            for (size_t j = i + 1; j < N; ++j)
                if (slots[i].name == slots[j].name)
                    return false;
        return true;
    }
    static_assert(SlotNamesUnique(kSlotBindings));
} // namespace

uintptr_t FindGlobalSlot(std::string_view slot_name) noexcept
{
    for (const NamedSlot& binding : kSlotBindings)
    {
        if (slot_name == binding.name)
            return reinterpret_cast<uintptr_t>(binding.address);
    }
    return 0;
}

namespace opp
{
    // GetProcAddress's export: int __cdecl(uint32_t* version, uint32_t* out).
    using GetZMenuVersionFn = int(__cdecl*)(uint32_t*, uint32_t*);

    ZMenuGuardResult ZMenuVersionGuard(uint32_t& version) noexcept
    {
        version = 0;
        const HMODULE module = GetModuleHandleA("ZMenuIV.asi");
        if (!module)
            return ZMenuGuardResult::module_absent;

        const auto getVersion = GameFunction<GetZMenuVersionFn>(
            reinterpret_cast<uintptr_t>(GetProcAddress(module, "GetZMenuVersion")));
        if (!getVersion)
            return ZMenuGuardResult::export_absent;

        uint32_t out = 0;
        (void)getVersion(&version, &out); // the original ignores the result
        return ZMenuVersionAccepted(version) ? ZMenuGuardResult::version_ok
                                             : ZMenuGuardResult::version_rejected;
    }

    namespace
    {
        bool HasAsiLoaderMarker(HMODULE module) noexcept
        {
            // The export Ultimate ASI Loader >= 6.9.0 added.
            return module != nullptr && GetProcAddress(module, "IsUltimateASILoader") != nullptr;
        }

        HMODULE CurrentModule() noexcept
        {
            HMODULE self = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    reinterpret_cast<LPCWSTR>(&CurrentModule), &self))
                return nullptr;
            return self;
        }

        // The loader that called us, when it is still on the stack (UAL calls
        // InitializeASI from its own frames, and LoadLibrary keeps the caller's
        // frames above DllMain).
        HMODULE AsiLoaderOnStack(HMODULE self) noexcept
        {
            constexpr USHORT kMaxCapturedFrames = 64;

            void* frames[kMaxCapturedFrames]{};
            const USHORT count =
                CaptureStackBackTrace(1, kMaxCapturedFrames, frames, nullptr);
            for (USHORT i = 0; i < count; ++i)
            {
                HMODULE module = nullptr;
                if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       reinterpret_cast<LPCWSTR>(frames[i]), &module) &&
                    module != self && HasAsiLoaderMarker(module))
                    return module;
            }
            return nullptr;
        }

        // Fallback for an entry point that no loader frame is visible from: any
        // module in the process carrying the marker. The loader is not told apart
        // there, but "a modern UAL is installed" is still answered.
        HMODULE AnyAsiLoaderModule(HMODULE self) noexcept
        {
            HMODULE modules[512]{};
            DWORD needed = 0;
            if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof modules, &needed))
                return nullptr;
            const DWORD count = needed / sizeof(HMODULE);
            for (DWORD i = 0; i < count && i < _countof(modules); ++i)
            {
                if (modules[i] != self && HasAsiLoaderMarker(modules[i]))
                    return modules[i];
            }
            return nullptr;
        }

        std::string ModuleBaseName(HMODULE module) noexcept
        {
            // The name is reported in the ANSI code page: anything above ASCII is
            // replaced.
            wchar_t path[MAX_PATH]{};
            if (GetModuleFileNameW(module, path, MAX_PATH) == 0)
                return {};
            const wchar_t* base = path;
            for (const wchar_t* p = path; *p != L'\0'; ++p)
            {
                if (*p == L'\\' || *p == L'/')
                    base = p + 1;
            }
            std::string out;
            for (const wchar_t* p = base; *p != L'\0'; ++p)
                out.push_back(*p < 128 ? static_cast<char>(*p) : '?');
            return out;
        }

        std::string ModuleVersionText(HMODULE module, uint32_t& versionMs,
                                      uint32_t& versionLs) noexcept
        {
            versionMs = 0;
            versionLs = 0;

            VS_FIXEDFILEINFO info{};
            if (!ReadFixedFileInfo(module, info))
                return {};

            versionMs = info.dwFileVersionMS;
            versionLs = info.dwFileVersionLS;
            char text[40]{};
            std::snprintf(text, sizeof text, "%u.%u.%u.%u", HIWORD(versionMs),
                          LOWORD(versionMs), HIWORD(versionLs), LOWORD(versionLs));
            return text;
        }
    } // namespace

    bool AsiLoaderVersionAccepted(uint32_t versionMs, uint32_t versionLs) noexcept
    {
        // Ultimate ASI Loader >= 6.9.0 (2023-10-24), the release that added the
        // IsUltimateASILoader export. The MS dword carries major/minor, the LS the
        // build, so any 6.9.x build is accepted.
        constexpr uint32_t kMinimumVersionMs = 0x00060009;

        return versionMs > kMinimumVersionMs ||
               (versionMs == kMinimumVersionMs && versionLs >= 0u);
    }

    AsiLoaderInfo QueryAsiLoader() noexcept
    {
        AsiLoaderInfo info;
        const HMODULE self = CurrentModule();

        HMODULE loader = AsiLoaderOnStack(self);
        if (loader == nullptr)
            loader = AnyAsiLoaderModule(self);
        if (loader == nullptr)
            return info;

        info.detected = true;
        info.name = ModuleBaseName(loader);
        info.version = ModuleVersionText(loader, info.versionMs, info.versionLs);
        info.versionKnown = info.versionMs != 0 || info.versionLs != 0;
        return info;
    }

    bool ClaimSingleInstance(const wchar_t* name) noexcept
    {
        wchar_t mutexName[128]{};
        std::swprintf(mutexName, _countof(mutexName), L"Local\\%ls-%lu", name,
                      GetCurrentProcessId());

        const HANDLE mutex = CreateMutexW(nullptr, FALSE, mutexName);
        if (mutex == nullptr)
            return true; // the marker cannot be created; never block the game on it
        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(mutex);
            return false;
        }
        // Intentionally never closed: the object must live for the whole process.
        return true;
    }
} // namespace opp

namespace opp
{
    void PatchGame(GameBuild build, GameImage image, const Config& config,
                   const PatchObserver& observer)
    {
        PatchContext ctx{image, reinterpret_cast<uintptr_t>(image.base), build, config};

        const auto run = [&](Event<PatchContext&>& event, const char* tier) {
            Log("tier %s: %zu arms", tier, event.size());
            const size_t applied = ArmWritesApplied();
            const size_t skipped = ArmWritesSkipped();
            event.SetObserver(observer, tier);
            event.Execute(ctx);
            event.SetObserver({});
            Log("tier %s: %zu applied, %zu skipped", tier, ArmWritesApplied() - applied,
                ArmWritesSkipped() - skipped);
        };

        run(PublishEvent(), "publish");
        run(InstallEvent(), "install");
        run(PostEvent(), "post");

        // The launcher write is gated by a process probe, not by an arm, so it
        // runs after the tiers but is still bracketed for the dump.
        if (observer.begin != nullptr)
            observer.begin(observer.user, "ApplyLaunchWithPlayGTAIV");
        ApplyLaunchWithPlayGTAIV(ctx);
        if (observer.end != nullptr)
            observer.end(observer.user, "ApplyLaunchWithPlayGTAIV");

        // One line for the whole pass: the number of writes that reached the
        // image and the number the arm failsafes refused (missing body, site
        // outside the image). A healthy install reports 0 skipped.
        Log("install: %zu writes applied, %zu skipped", ArmWritesApplied(), ArmWritesSkipped());
    }
} // namespace opp

namespace opp
{
    namespace
    {
        // -1 = ask the process (or the dump's env override), 0/1 = forced.
        int g_launcherProbeOverride = -1;
    }

    void SetLauncherProbeOverride(bool forced) noexcept
    {
        g_launcherProbeOverride = forced ? 1 : 0;
    }

    bool LauncherProbe() noexcept
    {
        if (g_launcherProbeOverride >= 0)
            return g_launcherProbeOverride != 0;
#if defined(OPCEP_DUMP_TOOL)
        // The offline harness has no PlayGTAIV.exe process; the parity driver
        // pins the probe so the launcher writes stay observable.
        constexpr DWORD kLauncherProbeValueCapacity = 8;
        char value[kLauncherProbeValueCapacity]{};
        const DWORD length = GetEnvironmentVariableA(
            "OPCEP_LAUNCHER_PROBE", value, kLauncherProbeValueCapacity);
        return length > 0 && value[0] == '1';
#else
        return FindGameProcess() != 0;
#endif
    }

    void ApplyLaunchWithPlayGTAIV(PatchContext& ctx)
    {
        // The original handler installs this on five builds only; the string
        // exists twice on each of them (and once on the EFLC builds, which the
        // original never patched, so the gate stays).
        if (!ctx.Option("LaunchWithPlayGTAIV", "0"))
            return;
        if (!BuildIs(ctx.build, {GameBuild::v1000, GameBuild::v1040, GameBuild::v1060,
                                 GameBuild::v1070, GameBuild::v1080}))
            return;
        if (!LauncherProbe())
            return;

        // Exactly the game's launcher-name string ("LaunchGTAIV.exe\0"); rows
        // LaunchName in tests/parity/address_sites.tsv prove two hits per gated
        // build. The replacement is the 16 bytes the original strcpy_s leaves in
        // the slot: "PlayGTAIV.exe\0" followed by the surviving "e\0" of the old
        // name.
        constexpr char kLaunchNameSignature[] =
            "4C 61 75 6E 63 68 47 54 41 49 56 2E 65 78 65 00";
        constexpr char kLauncherNamePatch[16] = {
            'P', 'l', 'a', 'y', 'G', 'T', 'A', 'I', 'V', '.', 'e', 'x', 'e', '\0', 'e', '\0'};
        // The original's handler writes exactly its two table slots. Requiring
        // the exact count keeps a modded image with a third "LaunchGTAIV.exe\0"
        // untouched instead of writing a slot the original never wrote.
        constexpr size_t kLauncherSlotCount = 2;

        std::vector<const uint8_t*> matches;
        FindAllPatterns({ctx.image.base, ctx.image.size}, kLaunchNameSignature, matches);
        if (matches.size() != kLauncherSlotCount)
        {
            Log("launcher: %zu LaunchGTAIV name slot(s), expected %zu: skipped",
                matches.size(), kLauncherSlotCount);
            return;
        }
        for (const uint8_t* site : matches)
            WriteRaw(ctx, site, kLauncherNamePatch, sizeof(kLauncherNamePatch));
        Log("launcher: %zu PlayGTAIV name slot(s) patched", matches.size());
    }
} // namespace opp

namespace opp
{
    bool FusionFixGateClosed()
    {
        const HMODULE module = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi");
        if (module == nullptr)
            return false;
        return FusionFixLacksSignature(ModuleImage(module));
    }
} // namespace opp

namespace opp
{
    namespace
    {
        uint32_t PointerValue(const void* pointer)
        {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
        }
    } // namespace

    PreambleReport FindGamePointers(GameImage image) noexcept
    {
        PreambleReport report;
        if (!image.base || image.size == 0)
            return report;

        const ModuleView view = ExecutableView({image.base, image.size});
        const auto resolve = [&](std::string_view signature) -> const uint8_t* {
            Match found;
            return FindPattern(view, signature, found) ? found.address : nullptr;
        };
        // The seven counted scans; the build-probe salt fallback below is not one.
        const auto scan = [&](std::string_view signature) -> const uint8_t* {
            if (const uint8_t* hit = resolve(signature))
            {
                ++report.resolved;
                return hit;
            }
            ++report.missing;
            return nullptr;
        };

        if (const uint8_t* hit = scan("08 0B C2 8B F0 E8"))
        {
            // The E8 at the hit's offset+5 calls the script resolver; its rel32
            // operand sits at +6.
            zpatch_driveby_resolver = BranchDestination(hit, 6u);
        }

        // Byte distance from each recovered scan hit back to that function's
        // entry; the original's preamble published the entry address.
        if (const uint8_t* hit = scan("85 F6 74 30 8B 16 8B 42 0C 8B CE FF D0 3C 02 75 23"))
            zpatch_pre_fn_59c74 = PointerValue(hit) - 42u;

        if (const uint8_t* hit =
                scan("8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"))
            zpatch_model_lookup = PointerValue(hit);

        if (const uint8_t* hit = scan("85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07"))
            zpatch_pre_ped_array = ReadDword(hit - sizeof(uint32_t));

        if (const uint8_t* hit =
                scan("56 8B F1 8B 06 8B 90 A0 00 00 00 57 FF D2 85 C0 74 1C"))
            zpatch_pre_fn_59b88 = PointerValue(hit);

        if (const uint8_t* hit =
                scan("0F 10 5E 14 F3 0F 10 66 18 F3 0F 10 4C 24 0C F3"))
            zpatch_pre_fn_5d63c = PointerValue(hit) - 48u;

        if (const uint8_t* hit =
                scan("0F 10 16 F3 0F 10 5E 04 F3 0F 10 4C 24 0C F3 0F 10 66 08 F2 0F 5A C0"))
            zpatch_pre_fn_59cdc = PointerValue(hit) - 48u;

        // Build-probe signature: absent on an unknown build, so all ten hash salts
        // fall back to these values.
        if (resolve("68 ? ? ? ? 68 A9 54 14 51 E8") == nullptr)
        {
            // The recovered fingerprint constants of the original's build table:
            // no derivation exists, the preamble's build switch selects the row.
            zpatch_salt_28 = 0x931F73A2u;
            zpatch_salt_3c = 0xA4EA0691u;
            zpatch_salt_40 = 0xC9DBDA90u;
            zpatch_salt_50 = 0x6B86410Eu;
            zpatch_salt_54 = 0x6DCD115Au;
            zpatch_salt_5c = 0x1CC648EAu;
            zpatch_salt_a8 = 0x7F6A60D3u;  // recovered fingerprint constant
            zpatch_salt_b8 = 0x62BFDB37u;  // recovered fingerprint constant
            zpatch_salt_c8 = 0x406B4B20u;  // recovered fingerprint constant
            zpatch_salt_d4 = 0x09399528u;  // recovered fingerprint constant
        }
        return report;
    }
} // namespace opp

#if !defined(_WIN64)

// ---- hook bodies ----
//
// The machine-level entry points this module owns: the LocalSettingsPath detours and
// the helpers they run between pushad/popad, in one block after the readable part.
// Each body keeps the original site's register contract (its @exact comment names the
// destination VA); 32-bit only - the x64 parity dump resolves names to the original VA.

extern "C"
{
// The 0x104-byte path buffer the original's LocalSettingsPath helpers use.
constexpr size_t kLocalPathCapacity = 0x104u;

// LocalSettingsPath: the CWD captured at load, the "\GTA IV" folder suffix the
// game's path helper is retargeted to push, the four-byte call slot the game
// calls through (original off_10158064 = sub_10051320), and the five path
// pointers the detour rewrites.
int __stdcall zpatch_local_copy_saves(uint32_t, uint32_t, uint32_t, uint32_t,
                                      char* dest) noexcept
{
    if (dest != nullptr)
        std::snprintf(dest, kLocalPathCapacity, "%s\\saves", zpatch_local_ansi_cwd);
    return 0;
}

// Captures the CWD and fills the call slot the retargeted game call goes
// through (original off_10158064 = sub_10051320).
void zpatch_local_settings_capture_cwd() noexcept
{
    GetCurrentDirectoryA(kLocalPathCapacity, zpatch_local_ansi_cwd);
    GetCurrentDirectoryW(kLocalPathCapacity, zpatch_local_wide_cwd);
    zpatch_local_saves_slot = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&zpatch_local_copy_saves));
}

namespace
{
// The /GS cookie tail the original helpers end with (body_helpers.inc's copy cannot
// be used here: opp.patch imports this module).
void zpatch_cookie_tail() noexcept
{
    __asm {
        mov ecx, dword ptr [__security_cookie]
        call zpatch_spawns_cookie_check_ecx
    }
}
void zpatch_build_ansi_path(char* out, const char* name) noexcept
{
    const char prefix[] = "\\saves\\";
    size_t o = 0;
    for (const char* s = prefix; *s; ++s) out[o++] = *s;
    for (const char* s = name; *s; ++s) out[o++] = *s;
    out[o++] = '\\';
    out[o] = '\0';
}
void zpatch_build_wide_path(wchar_t* out, const wchar_t* name) noexcept
{
    const wchar_t prefix[] = L"\\saves\\";
    size_t o = 0;
    for (const wchar_t* s = prefix; *s; ++s) out[o++] = *s;
    for (const wchar_t* s = name; *s; ++s) out[o++] = *s;
    out[o++] = L'\\';
    out[o] = 0;
}
// The original copies with strcpy_s/wcscpy_s (cap 0x200); these write the same
// bytes without the static CRT's secure-copy path.
void zpatch_copy_path(char* dst, size_t cap, const char* src) noexcept
{
    size_t n = 0;
    while (n + 1 < cap && src[n] != '\0')
    {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}
void zpatch_copy_wpath(wchar_t* dst, size_t cap, const wchar_t* src) noexcept
{
    size_t n = 0;
    while (n + 1 < cap && src[n] != 0)
    {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = 0;
}
}  // namespace

// The detours' helper (original sub_10050D10): rewrites the game's four
// just-computed settings/save path buffers to "<...>\saves\<episode>\" (the
// first ANSI destination is hardcoded "GTA IV"). Called between pushad/popad.
void zpatch_local_settings_step() noexcept
{
    // The 0x200 cap the original's strcpy_s/wcscpy_s path copies use, the game's
    // episode selector values (1 = TLAD, 2 = TBoGT; anything else is the base
    // game), and the capacities of the three path buffers the step builds.
    constexpr size_t kSettingsPathCapacity = 0x200u;

    const int episode = zpatch_local_episode_ptr
                            ? *reinterpret_cast<const int*>(
                                  static_cast<uintptr_t>(
                                      zpatch_local_episode_ptr))
                            : 0;
    const char* ansi_name = "GTA IV";
    const wchar_t* wide_name = L"GTA IV";
    if (episode == 1)
    {
        ansi_name = "TLaD";
        wide_name = L"TLaD";
    }
    else if (episode == 2)
    {
        ansi_name = "TBoGT";
        wide_name = L"TBoGT";
    }
    char ansi_iv[16];
    char ansi_episode[32];
    wchar_t wide_episode[32];
    // sub_10050D10 copies the literal "\saves\GTA IV\" into ansi_iv, independent
    // of the episode.
    zpatch_build_ansi_path(ansi_iv, "GTA IV");
    zpatch_build_ansi_path(ansi_episode, ansi_name);
    zpatch_build_wide_path(wide_episode, wide_name);
    if (zpatch_local_path_18)
        zpatch_copy_path(reinterpret_cast<char*>(static_cast<uintptr_t>(
                             zpatch_local_path_18)),
                         kSettingsPathCapacity, ansi_iv);
    if (zpatch_local_path_08)
        zpatch_copy_path(reinterpret_cast<char*>(static_cast<uintptr_t>(
                             zpatch_local_path_08)),
                         kSettingsPathCapacity, ansi_episode);
    if (zpatch_local_path_1c)
        zpatch_copy_wpath(reinterpret_cast<wchar_t*>(static_cast<uintptr_t>(
                              zpatch_local_path_1c)),
                          kSettingsPathCapacity, wide_episode);
    if (zpatch_local_path_10)
        zpatch_copy_wpath(reinterpret_cast<wchar_t*>(static_cast<uintptr_t>(
                              zpatch_local_path_10)),
                          kSettingsPathCapacity, wide_episode);
    zpatch_cookie_tail();
}

// @exact 0x100512F0 LocalSettingsPath detour: entered via E9 at the path-setup
// function's epilogue (game 0x856EE6, swallowing pop edi/esi/ebx; ret plus one
// int3 pad), runs the path-rewrite helper between pushad/popad and replays it.
// Contract: no arguments; entered by JMP, never called; pushad/popad preserve
// every GPR and EFLAGS, and the three pops + ret reproduce the overwritten
// epilogue so the caller's stack frame is unchanged.
[[maybe_unused]] static __declspec(naked) void abi_local_settings_detour()
{
    __asm {
        pushad
        call zpatch_local_settings_step
        popad
        pop edi
        pop esi
        pop ebx
        ret
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py (unreachable)
    }
}
// @exact 0x10051300 LocalSettingsPath fallback detour (pattern absent on
// 1.0.8.0): same helper, then re-issues the original 5-arg call through ESI and
// the two-pop epilogue. Contract: as above; ESI holds the call target the
// original epilogue's caller set, and the four pushed zeros + 0x1C replay its
// arguments.
[[maybe_unused]] static __declspec(naked) void abi_local_settings_fallback_detour()
{
    __asm {
        pushad
        call zpatch_local_settings_step
        popad
        push 0
        push 0
        push 1Ch
        push 0
        call esi
        pop esi
        pop edi
        ret
        __emit 0CCh   // body delimiter for tools/opcep_bodycheck.py (unreachable)
    }
}
}  // extern "C"

#endif  // !defined(_WIN64)
