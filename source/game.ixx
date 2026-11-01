// SPDX-License-Identifier: GPL-3.0-or-later
//
// Game identity: the executable's version, the compact build id the original's
// classifier uses, the game's joaat hash, and the PlayGTAIV process probe.

module;

#include "common.hxx"
#include "rebased.h"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <tlhelp32.h>
#include <winver.h>
#include <cwchar>
#include <new>
#include <bit>
#include <cstring>

export module opp.game;

import opp.common;

export namespace opp
{
    // The build classifier's shared helpers (declared here, not in common.hxx:
    // their definitions live in this module).
    const char* GameBuildName(GameBuild build) noexcept;
    bool BuildIs(GameBuild build, std::initializer_list<GameBuild> builds) noexcept;

    // 0x10003E50. "hash:NNNN" returns the parsed value (a 0 result falls back to
    // hashing); anything else is hashed.
    uint32_t GameHash(std::string_view text) noexcept;

    // 0x10003B80: concatenate the four decimal fields and parse the result as
    // base 10 (1.0.7.0 -> 1070).
    bool ComposeGameVersion(uint16_t major, uint16_t minor, uint16_t build,
                            uint16_t revision, uint32_t& output) noexcept;

    // The switch at 0x10055C70. Anything else is unknown and installs nothing.
    GameBuild ClassifyGameBuild(uint32_t compactVersion) noexcept;

    // Reads the running executable's VS_FIXEDFILEINFO.
    bool QueryGameVersion(uint32_t& compactVersion, GameBuild& build) noexcept;

    struct ProcessEntry
    {
        std::string_view executable;
        uint32_t processId{};
    };

    uint32_t SelectGameProcess(const ProcessEntry* entries, size_t count,
                                    std::string_view name) noexcept;

    // The pid of PlayGTAIV.exe, or 0.
    uint32_t FindGameProcess() noexcept;

    // 0x10003E20: true when the given module lacks the FusionFix bytes at
    // 0x20D3/0x20D7 (the original probes GTAIV.EFLC.FusionFix.asi).
    bool ModuleLacksSignature(const uint8_t* image, size_t imageSize) noexcept;

    // ---------------------------------------------------------------------
    // The ZMenuIV version guard: generic startup logic (the original rejects an
    // outdated ZMenuIV before the menu is built), so it lives with the other
    // process/module probes, not in the config hub.
    // ---------------------------------------------------------------------
    // 220625 (0x35DD1): the original's minimum accepted ZMenuIV version value.
    constexpr uint32_t kZMenuMinVersion = 0x35DD1u;
    constexpr const char* kZMenuModuleName = "ZMenuIV.asi";
    constexpr const char* kZMenuProcName = "GetZMenuVersion";
    constexpr const char* kZMenuRejectText =
        "Incompatible/outdated version of ZMenu detected! The game will now exit.";
    constexpr const char* kZMenuRejectCaption = "";
    constexpr uint32_t kZMenuRejectFlags = MB_ICONERROR;
    constexpr uint32_t kZMenuTerminateExitCode = 0u;

    enum class ZMenuGuardResult : uint8_t
    {
        module_absent,
        export_absent,
        version_ok,
        version_rejected,
    };

    constexpr bool ZMenuVersionAccepted(uint32_t version) noexcept
    {
        return version >= kZMenuMinVersion;
    }

    ZMenuGuardResult ZMenuVersionGuard(uint32_t& version) noexcept;
} // namespace opp

// The rebased accessors (moved here from rebased.h with rebased.cpp). They are
// global-scope functions, not `opp::` members: the arms and dllmain call them
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
        // The game's name-hash step multipliers (recovered constants: the
        // incremental hash multiplies by 0x401 and finishes by 0x8001).
        constexpr uint32_t kHashMultiplier = 0x401u;
        constexpr uint32_t kHashFinalMultiplier = 0x8001u;
        // The incremental tail constant the original's hash applies before the
        // final shift/multiply (recovered, no derivation).
        constexpr uint32_t kHashTailMultiplier = 9u;
        constexpr unsigned kHashShiftA = 6;
        constexpr unsigned kHashShiftB = 11;
        constexpr uint32_t kHashBias = 2;
        // The two dwords FusionFix's version probe compares at
        // module+0x20D3/+0x20D7; opaque recovered values (no derivation).
        constexpr uint32_t kCompatibilityFirstValue = 0xC7C47589u;
        constexpr uint32_t kCompatibilitySecondValue = 0x00002446u;
        constexpr uint32_t kVsFixedFileInfoSignature = VS_FFI_SIGNATURE;
        constexpr size_t kDwordLength = sizeof(uint32_t);

        bool IsWhite(char c) noexcept
        {
            constexpr std::string_view kWhitespace{" \t\r\n\f\v"};
            return kWhitespace.find(c) != std::string_view::npos;
        }

        uint32_t ParseDecOrHex(std::string_view text) noexcept
        {
            size_t pos = 0;
            while (pos < text.size() && IsWhite(text[pos]))
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

                value = (value + c) * kHashMultiplier;
                value = (value >> kHashShiftA) ^ value;
            }

            value *= kHashTailMultiplier;
            value = (value >> kHashShiftB) ^ value;
            value *= kHashFinalMultiplier;
            return value > 1u ? value : value + kHashBias;
        }

        uint32_t ReadImageDword(const uint8_t* image, size_t offset) noexcept
        {
            uint32_t value = 0;
            for (size_t i = 0; i < kDwordLength; ++i)
                value |= static_cast<uint32_t>(image[offset + i]) << (8u * i);
            return value;
        }
    } // namespace

    uint32_t GameHash(std::string_view text) noexcept
    {
        if (text.size() >= 5 && (text[0] == 'h' || text[0] == 'H') &&
            (text[1] == 'a' || text[1] == 'A') && (text[2] == 's' || text[2] == 'S') &&
            (text[3] == 'h' || text[3] == 'H') && text[4] == ':')
        {
            // The shipped 32-bit strtoul call accepts whitespace, a sign and a
            // numeric prefix; only a zero result falls back to hashing the text.
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
        // The original classifier's switch as one table: the compact version is
        // the enum's numeric value, and the name is what the log/dump prints.
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

    uint32_t SelectGameProcess(const ProcessEntry* entries, size_t count,
                                    std::string_view name) noexcept
    {
        if (!entries)
            return 0;

        for (size_t i = 0; i < count; ++i)
        {
            if (entries[i].executable.size() == name.size() &&
                std::memcmp(entries[i].executable.data(), name.data(), name.size()) == 0)
                return entries[i].processId;
        }
        return 0;
    }

    uint32_t FindGameProcess() noexcept
    {
        // UNICODE builds map the unsuffixed names to the W variants, matching the
        // original ANSI-name call site's case-sensitive full-name comparison.
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
                if (std::wcscmp(entry.szExeFile, L"PlayGTAIV.exe") == 0)
                {
                    found = entry.th32ProcessID;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }
        CloseHandle(snapshot);
        return found;
    }

    bool ModuleLacksSignature(const uint8_t* image, size_t imageSize) noexcept
    {
        // Machine code at 0x10003e2f / 0x10003e3b shows both comparisons are
        // 32-bit:
        //   cmp DWORD PTR [eax+0x20d3], 0xC7C47589
        //   cmp DWORD PTR [eax+0x20d7], 0x00002446
        // Offsets of the two probe dwords inside the FusionFix module.
        constexpr size_t kCompatibilityFirstOffset = 0x20d3;
        constexpr size_t kCompatibilitySecondOffset = 0x20d7;

        if (!image || imageSize < kCompatibilitySecondOffset + kDwordLength)
            return false;

        return ReadImageDword(image, kCompatibilityFirstOffset) != kCompatibilityFirstValue ||
               ReadImageDword(image, kCompatibilitySecondOffset) != kCompatibilitySecondValue;
    }

    bool QueryGameVersion(uint32_t& compactVersion, GameBuild& build) noexcept
    {
        compactVersion = 0;
        build = GameBuild::unknown;

        char executable[MAX_PATH]{};
        const DWORD pathLength = GetModuleFileNameA(nullptr, executable, MAX_PATH);
        if (pathLength == 0 || pathLength >= MAX_PATH)
            return false;

        DWORD ignored = 0;
        const DWORD infoSize = GetFileVersionInfoSizeA(executable, &ignored);
        if (infoSize == 0)
            return false;

        auto* info = new (std::nothrow) uint8_t[infoSize];
        if (!info)
            return false;

        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT fixedSize = 0;
        const BOOL queried =
            GetFileVersionInfoA(executable, ignored, infoSize, info) &&
            VerQueryValueA(info, "\\", reinterpret_cast<void**>(&fixed), &fixedSize);

        bool result = false;
        if (queried && fixed && fixedSize >= sizeof(VS_FIXEDFILEINFO) &&
            fixed->dwSignature == kVsFixedFileInfoSignature)
        {
            const auto major = static_cast<uint16_t>(fixed->dwProductVersionMS >> 16u);
            const auto minor = static_cast<uint16_t>(fixed->dwProductVersionMS);
            const auto patch = static_cast<uint16_t>(fixed->dwProductVersionLS >> 16u);
            const auto revision = static_cast<uint16_t>(fixed->dwProductVersionLS);

            result = ComposeGameVersion(major, minor, patch, revision, compactVersion);
            if (result)
                build = ClassifyGameBuild(compactVersion);
        }

        delete[] info;
        return result;
    }
} // namespace opp

// ---------------------------------------------------------------------------
// Rebased globals: the live globals the hook bodies address by name.
// ---------------------------------------------------------------------------

// C linkage so the naked LoadDLCs body can address it from inline asm.
extern "C"
{
    uint32_t zpatch_dlc_load_callback = 0;              // dword_1015BDDC
    uint32_t zpatch_blackscreen_resume = 0;             // dword_1015AD58
    uint32_t zpatch_blackscreen_value_ptr = 0;          // dword_1015AD5C
    uint32_t zpatch_path_node_resume_ok = 0;            // dword_1015BDFC
    uint32_t zpatch_path_node_resume_fail = 0;          // dword_1015BE00
    uint32_t zpatch_plane_crash_resume_a = 0;           // dword_1015BE18
    uint32_t zpatch_plane_crash_resume_b = 0;           // dword_1015BE14
    uint32_t zpatch_wanted_level_resume = 0;            // dword_1015AD6C
    uint32_t zpatch_group_hack_resume = 0;              // dword_1015AD60
    uint32_t zpatch_smpa60_resume_a = 0;                // dword_10159C70
    uint32_t zpatch_smpa60_resume_b = 0;                // dword_10159C68
    uint32_t zpatch_smpa60_resume_c = 0;                // dword_10159C6C (tail shim)
    uint32_t zpatch_freeze_gun_resume = 0;              // dword_1015AD64
    uint32_t zpatch_player_ped_object = 0;              // dword_10159D28
    uint32_t zpatch_player_ped_computed = 0;            // dword_10159D20
    uint32_t zpatch_player_ped_resume = 0;              // dword_10159D2C
    uint32_t zpatch_model_lookup = 0;                   // dword_10159C0C (shared)
    uint32_t zpatch_emissive_ecx = 0;                   // dword_10159CC0
    uint32_t zpatch_emissive_edx = 0;                   // dword_10159CCC
    uint32_t zpatch_emissive_const = 0;                 // dword_10159CC4
    uint32_t zpatch_emissive_resume = 0;                // dword_10159CC8
    uint32_t zpatch_carparts_resume_if = 0;             // dword_1015BDC8 (match+2)
    uint32_t zpatch_carparts_resume_else = 0;           // dword_1015BDC0 (match+0x45)
    uint32_t zpatch_super_lod_fail = 0;                 // dword_101580AC
    uint32_t zpatch_super_lod_ok = 0;                   // dword_101580B0
    uint32_t zpatch_super_lod_lookup = 0;               // dword_101580B4
    uint32_t zpatch_episode_switch_resume_a = 0;        // dword_10159BBC
    uint32_t zpatch_episode_switch_resume_b = 0;        // dword_10159BB4
    uint32_t zpatch_episode_switch_resume_c = 0;        // dword_10159BB8
    uint32_t zpatch_episode_flag = 2;                   // unk_10158128 (int 2)
    uint32_t zpatch_bike_value = 0;                     // dword_1015AD7C
    uint32_t zpatch_bike_saved = 0;                     // dword_1015AD80
    uint32_t zpatch_bike_resume = 0;                    // dword_1015AD84
    uint32_t zpatch_collision_deadlock_should_walk = 0; // dword_1015BDD4
    uint32_t zpatch_collision_deadlock_iterations = 0;  // dword_1015BDD8
    uint32_t zpatch_driveby_resolver = 0;               // dword_1015BDE0
    uint32_t zpatch_driveby_resume = 0;                 // dword_1015BDF4
    // The drive-by FOV the game loads (dword_101580C4), stored as the dword the
    // game reads; the float constant and its bits live in opp.common.
    uint32_t zpatch_driveby_fov = std::bit_cast<uint32_t>(opp::kDrivebyFovDegrees);
    uint32_t zpatch_driveby_cached = 0;           // dword_1015BDF8
    uint8_t zpatch_driveby_done = 0;              // byte_1015AD4A
    uint32_t zpatch_nolivery_name = 0;            // dword_10159BA8 (char*)
    uint32_t zpatch_nolivery_index = 0;           // dword_10159BB0
    uint32_t zpatch_nolivery_hash = 0;            // dword_10159B98
    uint32_t zpatch_nolivery_resume = 0;          // dword_10159B9C
    uint32_t zpatch_nolivery_fn = 0;              // dword_10159BA4
    uint32_t zpatch_nolivery_resume_d = 0;        // dword_10159BAC
    uint32_t zpatch_nolivery_arg = 0;             // dword_10159B94
    uint32_t zpatch_nolivery_count = 0;           // dword_10159B90
    uint32_t zpatch_nolivery_bufptr = 0;          // dword_10159BA0 (char*)
    uint32_t zpatch_nolivery_dest = 0;            // dword_10159B8C (char*)
    uint32_t zpatch_crash_msg_getter = 0;         // dword_1015AD68
    uint32_t zpatch_newfiles_register_slot = 0;   // dword_10159C08
    uint32_t zpatch_benchmark_resume_a = 0;       // dword_1015AD50
    uint32_t zpatch_benchmark_resume_b = 0;       // dword_1015AD54
    uint32_t zpatch_bds_hash_a = 0;               // dword_10159C1C
    uint32_t zpatch_bds_hash_b = 0;               // dword_10159C24
    uint32_t zpatch_bds_resume_a = 0;             // dword_10159C20
    uint32_t zpatch_bds_resume_b = 0;             // dword_10159C28
    uint32_t zpatch_copsirens_resume = 0;         // dword_1015937C
    uint32_t zpatch_copsirens_resume_b = 0;       // dword_10159374
    uint32_t zpatch_copsirens_index = 0;          // dword_10159370
    uint32_t zpatch_copsirens_index_b = 0;        // dword_1015936C
    uint32_t zpatch_copsirens_flag = 0;           // dword_10159B80
    uint32_t zpatch_copsirens_flag_b = 0;         // dword_10159378
    uint32_t zpatch_ide_string = 0;               // dword_1015BE08
    uint32_t zpatch_ide_model = 0;                // dword_1015BE0C
    uint32_t zpatch_ide_hook_slot = 0;            // dword_1015BE10
    uint32_t zpatch_copsirens_models[512] = {};   // dword_10159380
    uint32_t zpatch_popuplights_models[512] = {}; // dword_1015CE28
    uint32_t zpatch_wheelangle_models[512] = {};  // dword_1015A538
    uint32_t zpatch_wheelangle_angles[512] = {};  // dword_10159D38
    uint32_t zpatch_speedup_t4c = 0;              // dword_1015934C
    uint32_t zpatch_speedup_t50 = 0;              // dword_10159350
    uint32_t zpatch_speedup_t54 = 0;              // dword_10159354
    uint32_t zpatch_speedup_t58 = 0;              // dword_10159358
    uint32_t zpatch_speedup_t5c = 0;              // dword_1015935C
    uint32_t zpatch_speedup_t60 = 0;              // dword_10159360
    uint32_t zpatch_speedup_t64 = 0;              // dword_10159364
    uint32_t zpatch_speedup_t68 = 0;              // dword_10159368
    // OverrideGTADatPath: the original pushed its own "common:/data/gta.dat" global;
    // we push this buffer instead (filled from OverrideGTADatPathTo at load).
    char zpatch_override_gta_dat_path[260] = "common:/data/gta.dat";
    // PopupHeadlightsFlag (see rebased.h). dword_101580CC is pre-set to pi/2 on
    // disk and rewritten by the shim's checker on every call.
    uint32_t zpatch_popup_model = 0;
    uint32_t zpatch_popup_flag = 0;
    uint32_t zpatch_popup_index = 0;
    // The popup-light fallback angle (dword_101580CC), stored as the dword the
    // game reads; the float constant and its bits live in opp.common.
    uint32_t zpatch_popup_angle = std::bit_cast<uint32_t>(opp::kHalfPi);
    uint32_t zpatch_popup_resume_a = 0;
    uint32_t zpatch_popup_resume_b = 0;
    uint32_t zpatch_popup_hashes[512] = {};
    float zpatch_popup_angles[512] = {};
    // ForceCarHeadlightShadows: dword_101580C0 is pre-set to 2.0f on disk.
    uint32_t zpatch_fchs_param = 0;
    uint32_t zpatch_fchs_resume = 0;
    // The headlight-cone fallback offset, stored as the dword the game reads;
    // the float constant and its bits live in opp.common.
    uint32_t zpatch_fchs_offset = std::bit_cast<uint32_t>(opp::kDefaultConeOffset);
    uint32_t zpatch_fchs_hashes[512] = {};
    float zpatch_fchs_offsets[512] = {};
    // BadPlayerModelCrashFix scratch.
    uint32_t zpatch_bpm_obj = 0;
    uint32_t zpatch_bpm_model_index = 0;
    uint32_t zpatch_bpm_resume = 0;
    // NewSavesCompatibility state.
    uint32_t zpatch_new_saves_src = 0;
    uint32_t zpatch_new_saves_resume = 0;
    uint32_t zpatch_new_saves_chunk_len = 0;
    uint8_t zpatch_new_saves_chunk9_new = 0;
    uint8_t zpatch_new_saves_chunk13_new = 0;
    uint8_t zpatch_new_saves_chunk16_new = 0;
    // Loader-preamble resolutions (unconditional in the original).
    uint32_t zpatch_pre_ped_array = 0;
    uint32_t zpatch_pre_fn_59c74 = 0;
    uint32_t zpatch_pre_fn_59b88 = 0;
    uint32_t zpatch_pre_fn_5d63c = 0;
    uint32_t zpatch_pre_fn_59cdc = 0;
    // Hash salts: the on-disk (probe-hit) values of the original's ten dwords.
    // The ten hash salts the build probe mixes into its fingerprint (the
    // preamble overwrites them per build). They are recovered constants with no
    // derivation: each is a constant of the original's fingerprint table.
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
    uint8_t zpatch_flag_death_music = 0u;      // byte_10159B86
    uint8_t zpatch_flag_patch4_modchecks = 0u; // byte_10159C14
    uint8_t zpatch_flag_gfwl_popup = 0u;       // byte_10159B87
    uint8_t zpatch_flag_debug_spawns = 0u;     // byte_10159C15
    // RestoreDeathMusic's lazily resolved site.
    uint32_t zpatch_death_music_site = 0;
    // NewSavesCompatibility's element counter.
    uint32_t zpatch_new_saves_element_count = 0;
    uint32_t zpatch_new_saves_loaddata = 0;
    uint32_t zpatch_new_saves_orig_handler = 0;
    uint32_t zpatch_gfwl_resume = 0;
    uint32_t zpatch_carparts_shadow_fn = 0; // dword_1015B5B8
    uint32_t zpatch_carparts_resume_2 = 0;  // dword_1015BDCC
    uint32_t zpatch_car_dynamic_d = 0;      // dword_1015BDC4
    // DualVehicleHeadlights.
    uint32_t zpatch_dual_bcc = 0;      // dword_10159BCC
    uint32_t zpatch_dual_bd0 = 0;      // dword_10159BD0
    uint32_t zpatch_dual_bd4 = 0;      // dword_10159BD4
    uint32_t zpatch_dual_be0 = 0;      // dword_10159BE0
    uint32_t zpatch_dual_be4 = 0;      // dword_10159BE4
    uint32_t zpatch_dual_light_a = 1u; // dword_1015802C (original on-disk 1)
    uint32_t zpatch_dual_light_b = 2u; // dword_10158030 (original on-disk 2)
    uint32_t zpatch_dual_r_eax = 0;    // dword_10159BC4
    uint32_t zpatch_dual_r_ebx = 0;    // dword_10159BEC
    uint32_t zpatch_dual_r_ecx = 0;    // dword_10159BD8
    uint32_t zpatch_dual_r_edx = 0;    // dword_10159BF0
    uint32_t zpatch_dual_r_esi = 0;    // dword_10159BC8
    uint32_t zpatch_dual_r_edi = 0;    // dword_10159BE8
    uint32_t zpatch_dual_r_ebp = 0;    // dword_10159BDC
    uint32_t zpatch_dual_r_esp = 0;    // dword_10159BF4
    // LocalSettingsPath.
    char zpatch_local_ansi_cwd[260] = {};
    wchar_t zpatch_local_wide_cwd[260] = {};
    char zpatch_local_iv_suffix[8] = "\\GTA IV";
    uint32_t zpatch_local_saves_slot = 0;
    uint32_t zpatch_local_path_08 = 0;
    uint32_t zpatch_local_path_10 = 0;
    uint32_t zpatch_local_episode_ptr = 0;
    uint32_t zpatch_local_path_18 = 0;
    uint32_t zpatch_local_path_1c = 0;
    // FreezeCarFix / DebugPrintSpawns.
    // -1: "no spawn case recorded yet" (a four-byte kind field, so the sentinel
    // is all ones).
    uint32_t zpatch_spawns_kind = 0xFFFFFFFFu;
    uint32_t zpatch_spawns_class = 0;
    uint32_t zpatch_spawns_skip = 0;
    uint32_t zpatch_spawns_orig_a = 0;
    uint32_t zpatch_spawns_orig_b = 0;
    uint32_t zpatch_spawns_case0_resume = 0;
    uint32_t zpatch_spawns_case1_resume = 0;
    uint32_t zpatch_spawns_case2_resume = 0;
    uint32_t zpatch_spawns_case3_resume = 0;
    uint32_t zpatch_spawns_case4_resume = 0;
    uint32_t zpatch_spawns_case5_resume = 0;
    uint32_t zpatch_spawns_case6_resume = 0;
    uint32_t zpatch_spawns_case7_resume = 0;
    uint32_t zpatch_spawns_case8_resume = 0;
    uint32_t zpatch_spawns_case9_resume = 0;
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
    // assembler cannot name the @-decorated import pointer); the hook bodies fill it at load.
    uint32_t zpatch_spawns_getmodulehandlew = 0;
    uint32_t zpatch_garage_resume_a = 0;
    uint32_t zpatch_garage_resume_b = 0;
    uint32_t zpatch_garage_entry_resume_a = 0;
    float zpatch_garage_scale = 0.0f;
    uint32_t zpatch_steer_vehicle = 0;
    uint32_t zpatch_steer_resume = 0;
    uint32_t zpatch_steer_vtable_fn = 0;
    float zpatch_engine_last_timestep = 0.0f;
    float zpatch_engine_phase = 0.0f;
    uint8_t zpatch_engine_descending = 0;
    uint8_t zpatch_flag_steering = 0u; // byte_1015AD40 (on-disk 0)
    uint8_t zpatch_flag_engine = 0u;   // byte_1015AD41 (on-disk 0)
    uint8_t zpatch_flag_plane = 0u;    // byte_10159C17 (on-disk 0)
    // MouseFix (see the hook bodies). The constants are not const so the
    // inline assembler can address them as plain external symbols; nothing writes
    // them. Values are the original .rdata bits (hex floats keep them exact).
    uint32_t zpatch_mf_x = 0;
    uint32_t zpatch_mf_y = 0;
    uint32_t zpatch_mf_flag = 0;
    uint32_t zpatch_mf_time = 0;
    uint32_t zpatch_mf_resume_a = 0;
    uint32_t zpatch_mf_resume_b = 0;
    float zpatch_mf_xmm_save[8] = {};
    double zpatch_mf_scale = 0.0078125;
    double zpatch_mf_scale_255 = 255.0;
    float zpatch_mf_sign = 127.5f;
    float zpatch_mf_mul_neg1 = -1.0f;
    float zpatch_mf_step_scale = 0.1f;
    double zpatch_mf_mul_neg1_d = -1.0;
    // MouseFix's timing constants: 0x1.333334015bc21p-4 is 0.075 (the original
    // global's exact double bits), and the two pi values are its angle wrapping.
    double zpatch_mf_time_mul = 0x1.333334015bc21p-4;
    double zpatch_mf_time_add = 0.025;
    double zpatch_mf_pi = 0x1.921fb5a7ed197p+1;
    double zpatch_mf_two_pi = 0x1.921fb6134ce3ep+2;
    double zpatch_mf_neg_pi = -0x1.921fb5a7ed197p+1;
    // `xorps xmm, xmmword ptr [mask]` needs a 16-byte aligned address: the
    // original's .rdata constants are naturally aligned, so the masks are
    // alignas(16) (a misaligned mask faults with #GP/AV).
    // SSE sign-bit masks: 0x80 in the top byte of each 32-bit lane flips the sign.
    alignas(16) unsigned char zpatch_mf_neg_mask[16] = {0, 0, 0, 0x80, 0, 0, 0, 0x80,
                                                        0, 0, 0, 0x80, 0, 0, 0, 0x80};
    // The same mask for two 64-bit lanes (the double sign bit is byte 7/15).
    alignas(16) unsigned char zpatch_mf_double_sign[16] = {0, 0, 0, 0, 0, 0, 0, 0x80,
                                                           0, 0, 0, 0, 0, 0, 0, 0x80};
    // OutOfCommissionFix (see the hook bodies).
    uint32_t zpatch_ooc_resume = 0;
    uint32_t zpatch_ooc_logger = 0;
    uint32_t zpatch_ooc_orig = 0;
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
    // CutsceneFixes chain B (see the hook bodies): the worker's two publishes, the body's
    // frame-time accumulator (the original's .data BSS, past the raw data, so 0) and
    // the read-only constants it compares against (original .rdata/.data bits).
    uint32_t zpatch_cutscene_target = 0;                    // dword_1015BDE4
    uint32_t zpatch_cutscene_poke = 0;                      // dword_1015BDF0
    uint64_t zpatch_cutscene_accum = 0;                     // qword_1015BDE8 (BSS: starts 0)
    // The cutscene-shadow comparison constants, stored as the qwords the asm
    // reads; the double constants and their bits live in opp.common.
    uint64_t zpatch_cutscene_zero = std::bit_cast<uint64_t>(opp::kCutsceneZero);
    uint64_t zpatch_cutscene_limit = std::bit_cast<uint64_t>(opp::kCutsceneLimit);
    uint32_t zpatch_cutscene_abs_mask = 0x7FFFFFFFu;  // 0x101523D0: clears the sign bit
    uint32_t zpatch_cutscene_eps_a = std::bit_cast<uint32_t>(opp::kCutsceneEpsilon);
    uint32_t zpatch_cutscene_eps_b = std::bit_cast<uint32_t>(opp::kCutsceneHalf);
    // NewMemorySystem: the allocator singleton the handler's second scan publishes
    // (the immediate of `mov esi, 0x19cf9f0`); the E8 body writes our vtable there.
    uint32_t zpatch_nms_object = 0; // dword_10159CD8
}
namespace
{
// Rebased originals the handlers address directly: StartupTimeFix pushes the
// address of g_startup_time (the original pushed dword_101580d8).
uint32_t g_startup_time = 0;
uint32_t g_dlc_register_rva = 0;
uint32_t g_dlc_apply_rva = 0;
// VSyncFix: the game code reads/writes this dword through the operand the
// handler patches (the original pushed &unk_10159BC0).
uint32_t g_vsync_flag = 0;
} // namespace

// Copies `path` into the buffer the retargeted push reads, clamped and terminated.
void SetOverrideGtaDatPath(std::string_view path) noexcept
{
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
    return g_dlc_register_rva;
}
uint32_t& DlcApplyRva() noexcept
{
    return g_dlc_apply_rva;
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
} // namespace

namespace
{
    struct ValueBinding final
    {
        std::string_view name;
        void* address;
    };

    // The original-global publish targets keyed by INI option (the sig-install
    // `literal` rows). The addresses are stable for the process lifetime.
    constexpr ValueBinding kValueBindings[] = {
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
    constexpr ValueBinding kSymbolBindings[] = {
        {"zpatch_local_iv_suffix", zpatch_local_iv_suffix},
        {"zpatch_episode_flag", &zpatch_episode_flag},
    };

    template <size_t N>
    uintptr_t FindBinding(const ValueBinding (&bindings)[N], std::string_view name) noexcept
    {
        for (const ValueBinding& binding : bindings)
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

// Slot publishes are keyed by the original global's name; a name that is not
// in the table is a slot this tree does not publish (0 = skip, counted).
namespace
{
    struct SlotBinding final
    {
        std::string_view name;
        void* address;
    };

    // The original-global -> rebuilt-global map: the key is the symbol name
    // the recovered handler publishes, the value is this tree's rebased global.
    // A missing name is a publish this tree does not implement.
    constexpr SlotBinding kSlotBindings[] = {
        // LoadDLCs: the callback slot receives the game's loader address; the two
        // constants are the register/apply RVAs the discovery routine adds to the
        // module base at use time.
        {"dword_1015BDDC", &zpatch_dlc_load_callback},
        {"dword_101580D0", &g_dlc_apply_rva},
        {"dword_1015804C", &g_dlc_register_rva},
        {"dword_1015AD58", &zpatch_blackscreen_resume},
        {"dword_1015AD5C", &zpatch_blackscreen_value_ptr},
        {"dword_1015BDFC", &zpatch_path_node_resume_ok},
        {"dword_1015BE00", &zpatch_path_node_resume_fail},
        {"dword_1015BE18", &zpatch_plane_crash_resume_a},
        {"dword_1015BE14", &zpatch_plane_crash_resume_b},
        {"dword_1015AD6C", &zpatch_wanted_level_resume},
        {"dword_1015AD60", &zpatch_group_hack_resume},
        {"dword_10159C70", &zpatch_smpa60_resume_a},
        {"dword_10159C68", &zpatch_smpa60_resume_b},
        {"dword_10159C6C", &zpatch_smpa60_resume_c},
        {"dword_1015AD64", &zpatch_freeze_gun_resume},
        {"dword_10159D28", &zpatch_player_ped_object},
        {"dword_10159D20", &zpatch_player_ped_computed},
        {"dword_10159D2C", &zpatch_player_ped_resume},
        {"dword_10159C0C", &zpatch_model_lookup},
        {"dword_10159CC0", &zpatch_emissive_ecx},
        {"dword_10159CCC", &zpatch_emissive_edx},
        {"dword_10159CC4", &zpatch_emissive_const},
        {"dword_10159CC8", &zpatch_emissive_resume},
        {"dword_1015BDC8", &zpatch_carparts_resume_if},
        {"dword_1015BDC0", &zpatch_carparts_resume_else},
        {"dword_101580AC", &zpatch_super_lod_fail},
        {"dword_101580B0", &zpatch_super_lod_ok},
        {"dword_101580B4", &zpatch_super_lod_lookup},
        {"dword_10159BBC", &zpatch_episode_switch_resume_a},
        {"dword_10159BB4", &zpatch_episode_switch_resume_b},
        {"dword_10159BB8", &zpatch_episode_switch_resume_c},
        {"dword_1015AD7C", &zpatch_bike_value},
        {"dword_1015AD80", &zpatch_bike_saved},
        {"dword_1015AD84", &zpatch_bike_resume},
        {"dword_1015BDD4", &zpatch_collision_deadlock_should_walk},
        {"dword_1015BDE0", &zpatch_driveby_resolver},
        {"dword_1015BDF4", &zpatch_driveby_resume},
        {"dword_1015BDE4", &zpatch_cutscene_target},
        {"dword_1015BDF0", &zpatch_cutscene_poke},
        {"dword_10159CD8", &zpatch_nms_object},
        {"dword_10159B9C", &zpatch_nolivery_resume},
        {"dword_10159BA4", &zpatch_nolivery_fn},
        {"dword_10159BAC", &zpatch_nolivery_resume_d},
        {"dword_1015AD68", &zpatch_crash_msg_getter},
        {"dword_10159C08", &zpatch_newfiles_register_slot},
        {"dword_1015AD50", &zpatch_benchmark_resume_a},
        {"dword_1015AD54", &zpatch_benchmark_resume_b},
        {"dword_10159C1C", &zpatch_bds_hash_a},
        {"dword_10159C24", &zpatch_bds_hash_b},
        {"dword_10159C20", &zpatch_bds_resume_a},
        {"dword_10159C28", &zpatch_bds_resume_b},
        {"dword_1015937C", &zpatch_copsirens_resume},
        {"dword_10159374", &zpatch_copsirens_resume_b},
        {"dword_1015BE10", &zpatch_ide_hook_slot},
        {"dword_1015934C", &zpatch_speedup_t4c},
        {"dword_10159350", &zpatch_speedup_t50},
        {"dword_10159354", &zpatch_speedup_t54},
        {"dword_10159358", &zpatch_speedup_t58},
        {"dword_1015935C", &zpatch_speedup_t5c},
        {"dword_10159360", &zpatch_speedup_t60},
        {"dword_10159364", &zpatch_speedup_t64},
        {"dword_10159368", &zpatch_speedup_t68},
        // PopupHeadlightsFlag resumes (match+12 / match+10) and the checker's tables.
        {"dword_1015D628", &zpatch_popup_resume_a},
        {"dword_1015D62C", &zpatch_popup_resume_b},
        // ForceCarHeadlightShadows resume (match+12 of its second pattern).
        {"dword_1015B5BC", &zpatch_fchs_resume},
        // BadPlayerModelCrashFix resume (match+10).
        {"dword_10159CB8", &zpatch_bpm_resume},
        // NewSavesCompatibility: Src is captured from the game's push immediate;
        // dword_1015AD88 is the tramp resume.
        {"Src", &zpatch_new_saves_src},
        {"dword_1015AD88", &zpatch_new_saves_resume},
        {"dword_10159C18", &zpatch_gfwl_resume},
        {"dword_1015B5B8", &zpatch_carparts_shadow_fn},
        {"dword_1015BDC4", &zpatch_car_dynamic_d},
        {"dword_1015BDCC", &zpatch_carparts_resume_2},
        // DualVehicleHeadlights's five published game pointers.
        {"dword_10159BCC", &zpatch_dual_bcc},
        {"dword_10159BD0", &zpatch_dual_bd0},
        {"dword_10159BD4", &zpatch_dual_bd4},
        {"dword_10159BE0", &zpatch_dual_be0},
        {"dword_10159BE4", &zpatch_dual_be4},
        // LocalSettingsPath's five game path pointers.
        {"dword_10159D08", &zpatch_local_path_08},
        {"dword_10159D10", &zpatch_local_path_10},
        {"dword_10159D14", &zpatch_local_episode_ptr},
        {"dword_10159D18", &zpatch_local_path_18},
        {"dword_10159D1C", &zpatch_local_path_1c},
        // FreezeCarFix / DebugPrintSpawns resume slots.
        {"dword_10159CB0", &zpatch_spawns_orig_a},
        {"dword_10159C98", &zpatch_spawns_orig_b},
        {"dword_10159CBC", &zpatch_spawns_case0_resume},
        {"dword_10159C9C", &zpatch_spawns_case1_resume},
        {"dword_10159CA0", &zpatch_spawns_case2_resume},
        {"dword_10159CA8", &zpatch_spawns_case3_resume},
        {"dword_10159C7C", &zpatch_spawns_case4_resume},
        {"dword_10159C88", &zpatch_spawns_case5_resume},
        {"dword_10159CAC", &zpatch_spawns_case6_resume},
        {"dword_10159C90", &zpatch_spawns_case7_resume},
        {"dword_10159C94", &zpatch_spawns_case8_resume},
        {"dword_10159C84", &zpatch_spawns_case9_resume},
        // GarageModelFix resumes.
        {"dword_10159CD4", &zpatch_garage_resume_a},
        {"dword_10159CD0", &zpatch_garage_resume_b},
        // Trio resumes / call target.
        {"dword_1015AD44", &zpatch_steer_resume},
        {"dword_1015AD4C", &zpatch_steer_vtable_fn},
        // MouseFix publishes (the callback's slot writes).
        {"dword_10159C50", &zpatch_mf_x},
        {"dword_10159C3C", &zpatch_mf_y},
        {"dword_10159C5C", &zpatch_mf_flag},
        {"dword_10159C34", &zpatch_mf_time},
        {"dword_10159C60", &zpatch_mf_resume_a},
        {"dword_10159C40", &zpatch_mf_resume_b},
        // OutOfCommissionFix publishes.
        {"dword_10158034", &zpatch_ooc_resume},
        {"dword_10159BF8", &zpatch_ooc_logger},
        {"dword_10159C04", &zpatch_ooc_orig},
    };
} // namespace

uintptr_t FindGlobalSlot(std::string_view slot_name) noexcept
{
    for (const SlotBinding& binding : kSlotBindings)
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
        const HMODULE module = GetModuleHandleA(kZMenuModuleName);
        if (!module)
            return ZMenuGuardResult::module_absent;

        const auto getVersion = GameFunction<GetZMenuVersionFn>(
            reinterpret_cast<uintptr_t>(GetProcAddress(module, kZMenuProcName)));
        if (!getVersion)
            return ZMenuGuardResult::export_absent;

        uint32_t out = 0;
        (void)getVersion(&version, &out); // the original ignores the result
        return ZMenuVersionAccepted(version) ? ZMenuGuardResult::version_ok
                                             : ZMenuGuardResult::version_rejected;
    }
} // namespace opp
