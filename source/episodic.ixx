module;

#include <common.hxx>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <shlwapi.h>
#include <unordered_set>

export module episodic;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import options;
import patch;
import shared;

// EPISODIC: LoadDLCs, SCOSignatureFix, EpisodicVehicleSupport,
// EpisodicWeaponSupport, PoliceEpisodicWeaponSupport, TBoGTHeliHeightLimit and
// PipeBombDropIconSupport. The key family was spread over four handler modules
// (`loading`, `stability`, `vehicles`, `audio`); the episode gates all live here
// now.
export namespace Episodic
{
    void Prepare();

    void PublishLoadDLCs();

    void InstallLoadDLCs();
    void InstallSCOSignatureFix();
    void InstallEpisodicVehicleSupport();
    void InstallEpisodicWeaponSupport();
    void InstallPoliceEpisodicWeaponSupport();
    void InstallTBoGTHeliHeightLimit();
    void InstallPipeBombDropIconSupport();
}

// The DLC discovery pass's data (original sub_10023150): the five imported entries
// it calls through the original's own IAT slots, and the five literals it pushes.
// Each IAT operand is an absolute address, so the transcription names these slots
// and the instructions keep the original's `call ds:[...]` / `mov reg, ds:[...]`
// shapes.
extern "C"
{
    // dword_1015BDDC: the loader callback, an absolute address.
    uint32_t zpatch_dlc_load_callback = 0;
    // dword_1015804C / dword_101580D0: the register and apply entry points, which
    // the original stores as RVAs and adds the image base to at the call.
    uint32_t zpatch_dlc_register_rva = 0;
    uint32_t zpatch_dlc_apply_rva = 0;
    // dword_1015AD70: the image base the discovery adds those RVAs to.
    uint32_t zpatch_dlc_game_base = 0;

    uint32_t zpatch_dlc_get_module_file_name = 0;   // 0x10063004 GetModuleFileNameA
    uint32_t zpatch_dlc_path_remove_file_spec = 0;  // 0x10063250 PathRemoveFileSpecA
    uint32_t zpatch_dlc_path_file_exists = 0;       // 0x1006324C PathFileExistsA
    uint32_t zpatch_dlc_strcpy_s = 0;               // 0x10063388 strcpy_s
    uint32_t zpatch_dlc_strcat_s = 0;               // 0x100633A8 strcat_s

    // The original's own text: the two "content" layouts at 0x1012BD08/0x1012BDB0,
    // their legacy fallbacks at 0x1012BD60/0x1012BE08, and the episode format at
    // 0x1012BE54.
    const char zpatch_dlc_tlad_content[] = "\\dlc\\tlad\\content\\";
    const char zpatch_dlc_tlad_legacy[] = "\\TLAD\\";
    const char zpatch_dlc_tbogt_content[] = "\\dlc\\TBoGT\\content\\";
    const char zpatch_dlc_tbogt_legacy[] = "\\TBoGT\\";
    const char zpatch_dlc_episode_format[] = "\\Episode%d\\";

    // The bounded formatter's entry, published by `shared` (sub_10001280).
    extern uint32_t zpatch_format_entry;
}

// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488), so the readable code above them names
// each body through a plain prototype; only the definition in the tail keeps the
// attribute.
static void DlcFormat();
static void DlcDummyBody();
static void DlcStructFixupBody();
void* __cdecl DlcDiscoveryCpp(void* object);

// The test-only seam over the DLC discovery body: the module's own
// `DlcDiscoveryCpp`, driven by tests/dlc_full.cpp (ctest `zp_dlc_full`) with fake
// register/apply entries, and the two RVAs the seam stands in for. Neither writes
// a game byte.
export void* DlcDiscoveryForTest(void* object) noexcept;
export void SetDlcRvasForTest(uint32_t registerRva, uint32_t applyRva) noexcept;

export namespace Episodic
{
    // The `dlc_path` group's bound: the discovery body's path buffer is 0x104
    // bytes including the terminator.
    inline bool DlcPathFits(std::string_view path)
    {
        return !path.empty() && path.size() < 0x104;
    }
}

namespace Episodic
{
    void Prepare()
    {
        // The discovery pass's five imported entries, filled before any tier runs.
        // The original calls them through its own IAT slots; this module's are filled
        // from its own imports so the transcription's operands are absolute on both
        // sides.
        zpatch_dlc_get_module_file_name = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&GetModuleFileNameA));
        zpatch_dlc_path_remove_file_spec = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&PathRemoveFileSpecA));
        zpatch_dlc_path_file_exists = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&PathFileExistsA));
        // `strcpy_s`/`strcat_s` are overloaded in C++ (the template form takes the
        // destination by reference), so the address is the C three-argument entry.
        using SafeCopyFn = int(__cdecl*)(char*, size_t, const char*);
        zpatch_dlc_strcpy_s = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(static_cast<SafeCopyFn>(&strcpy_s)));
        zpatch_dlc_strcat_s = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(static_cast<SafeCopyFn>(&strcat_s)));

        // LoadDLCs' two bodies and the discovery pass the fixup body calls (a
        // rebuild, so it carries no proof row).
        Bodies::Register(reinterpret_cast<uintptr_t>(&DlcDummyBody), 0x10023480u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&DlcStructFixupBody), 0x10023490u);
        // The C++ discovery takes the transcribed body's role; the reference keeps
        // the fixup body's call target provable against the original.
        Bodies::Register(reinterpret_cast<uintptr_t>(DlcDiscoveryCpp), 0x10023150u);
    }
}

// The DLC handler's per-build write table and the discovery pass's helpers.

    constexpr std::size_t kDlcTailNopLength = 12;

    enum class DlcWriteKind : uint8_t
    {
        jump_rva,
        call_rva,
        jump_body,
        call_body,
        nop_tail,
    };

    struct DlcWrite
    {
        uint32_t site;    // game RVA the write lands at
        DlcWriteKind kind;
        uint32_t target;  // game RVA, for the two *_rva kinds
    };

    struct DlcBuild
    {
        Build build;
        uint32_t register_rva;
        uint32_t apply_rva;
        uint32_t load_callback_rva;
        std::span<const DlcWrite> writes;
    };

    // The handler's write order, per build.
    constexpr DlcWrite kDlcWrites1060[] = {
        { 0x41450Eu, DlcWriteKind::jump_rva, 0x41452Cu },
        { 0x41453Au, DlcWriteKind::call_rva, 0x413E30u },
        { 0x41453Fu, DlcWriteKind::nop_tail, 0u },
        { 0x41445Cu, DlcWriteKind::jump_rva, 0x4144B8u },
        { 0x4137CDu, DlcWriteKind::call_body, 0u },
        { 0x4144D4u, DlcWriteKind::jump_body, 0u },
    };

    constexpr DlcWrite kDlcWrites1070[] = {
        { 0x4142AEu, DlcWriteKind::jump_rva, 0x4142CCu },
        { 0x4142DAu, DlcWriteKind::call_rva, 0x413BD0u },
        { 0x4142DFu, DlcWriteKind::nop_tail, 0u },
        { 0x4141FCu, DlcWriteKind::jump_rva, 0x414258u },
        { 0x41356Du, DlcWriteKind::call_body, 0u },
        { 0x414274u, DlcWriteKind::jump_body, 0u },
    };

    constexpr DlcWrite kDlcWrites1080[] = {
        { 0x49040Eu, DlcWriteKind::jump_rva, 0x49042Cu },
        { 0x49043Au, DlcWriteKind::call_rva, 0x48FBB0u },
        { 0x49043Fu, DlcWriteKind::nop_tail, 0u },
        { 0x49035Cu, DlcWriteKind::jump_rva, 0x4903B8u },
        { 0x48F558u, DlcWriteKind::call_body, 0u },
        { 0x4903D4u, DlcWriteKind::jump_body, 0u },
    };

    // The key's build mask is exactly these three.
    constexpr DlcBuild kDlcBuilds[] = {
        { Build::v1060, 0x4136E0u, 0x414080u, 0x414560u, kDlcWrites1060 },
        { Build::v1070, 0x413480u, 0x413E20u, 0x414300u, kDlcWrites1070 },
        { Build::v1080, 0x48F440u, 0x48FE00u, 0x490460u, kDlcWrites1080 },
    };

    // The original's `call 0x100603F1` is the CRT's scalar `operator new`, called
    // unchecked and never freed. The inline assembler cannot name it in a `call`, so
    // the transcription calls this cdecl forwarder: one `E8`, the same argument and
    // the same throwing allocator.
    void* __cdecl DlcAlloc(unsigned int size)
    {
        return ::operator new(size);
    }

    // The original's `call 0x10060D6E` is the CRT `memset` with the same order
    // (destination, value, count).
    void __cdecl DlcZeroBlock(void* destination, int value, unsigned int count)
    {
        std::memset(destination, value, count);
    }

    const DlcBuild* DlcBuildFor(Build build)
    {
        for (const auto& row : kDlcBuilds)
        {
            if (row.build == build)
                return &row;
        }
        return nullptr;
    }

namespace
{
    // EpisodicWeaponSupport: the eight episode gates in front of the episodic
    // weapons. The key's build mask is the original's ten-build set (options.inc),
    // so the arm needs no gate of its own.
    struct EpisodeGateSite final
    {
        std::string_view signature;
        ptrdiff_t offset;
        size_t length;
    };

    constexpr EpisodeGateSite kEpisodicWeaponSites[] = {
        { "75 5B 83 3D ? ? ? ? 00 75", 0, 2 },
        { "83 3D ? ? ? ? 02 0F 85 8C 01 00 00", 7, 6 },
        { "83 3D ? ? ? ? 02 0F 8C D5 00", 0, 13 },
        { "83 3D ? ? ? ? 02 75 20 8B CD E8", 7, 2 },
        { "83 3D ? ? ? ? 02 75 50", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 21 8B 44 24 0C", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 32 8B 41", 7, 2 },
        { "83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C", 7, 2 },
    };

    // One recovered sub-gate: is the current build's id a bit of `mask`?
    bool CurrentBuildInMask(uint32_t mask)
    {
        return (mask & (1u << static_cast<uint32_t>(Loader::Current()))) != 0;
    }

    // A captured pattern operand is a 32-bit VA of the image *as loaded*: the
    // loader has already applied the image's relocations, so the dword is a live
    // address and the cast is the whole conversion.
    template <typename T>
    T* GamePointer(uint32_t value)
    {
        return reinterpret_cast<T*>(static_cast<uintptr_t>(value));
    }

    // EpisodicVehicleSupport's own episode word: the game's unk_10158128, an int
    // holding 2, which every `cmp [episode], 2` below tests. The arm hands the game
    // this module's word instead, so the four 1.0.7.0/1.0.8.0 sites read a value
    // this module owns; the dump canonicalises the four published operands.
    uint32_t zpatch_episode_flag = 2;
}

namespace Episodic
{

    void PublishLoadDLCs()
    {
        if (!Loader::Enabled("LoadDLCs"))
            return;

        const DlcBuild* const row = DlcBuildFor(Loader::Current());
        if (row == nullptr)
            return;

        const auto base = static_cast<uint32_t>(Game::Get().ImageBase());
        zpatch_dlc_game_base = base;
        zpatch_dlc_register_rva = row->register_rva;
        zpatch_dlc_apply_rva = row->apply_rva;
        zpatch_dlc_load_callback = base + row->load_callback_rva;
    }

    // SCOSignatureFix: the instruction 32 bytes before the anchor becomes a short
    // jump, so the signature check that follows is skipped. The `sco_anchor`
    // group instead scans a signature that ends on the `jz` itself; a build where
    // the old -32 distance does not hold would otherwise flip an unrelated `jz`.
    void InstallSCOSignatureFix()
    {
        if (!Loader::Enabled("SCOSignatureFix"))
            return;

        // `mov ds:X,edi` (6 bytes) + `test ebp,ebp` (2) put the `jz` opcode
        // at +8. The signature ends on the branch it flips, instead of scanning
        // a downstream anchor and relying on a fixed -32 distance.
        auto anchored = find_pattern("89 3D ? ? ? ? 85 ED 74 1E");
        if (anchored.empty())
            return;
        injector::WriteMemory<std::byte>(
            anchored.get_first<std::byte>(8), Bytes::ShortJmp, true);
    }

    void InstallEpisodicWeaponSupport()
    {
        if (!Loader::Enabled("EpisodicWeaponSupport"))
            return;

        for (const auto& site : kEpisodicWeaponSites)
        {
            auto pattern = find_pattern(site.signature);
            if (!pattern.empty())
                injector::MakeNOP(pattern.get_first(site.offset), site.length, true);
        }
    }

    // PoliceEpisodicWeaponSupport: the police-vehicle episode gate; the two-byte
    // branch after the `cmp [episode],2` at the anchor is NOPed.
    void InstallPoliceEpisodicWeaponSupport()
    {
        if (!Loader::Enabled("PoliceEpisodicWeaponSupport"))
            return;

        auto pattern = find_pattern("39 2D ? ? ? ? 75 3E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(6), 2, true);
    }

    // TBoGTHeliHeightLimit: the helicopter ceiling's episode gate -- the branch
    // after the `cmp [episode],2` at the anchor is NOPed, so the ceiling applies
    // on every episode.
    void InstallTBoGTHeliHeightLimit()
    {
        if (!Loader::Enabled("TBoGTHeliHeightLimit"))
            return;

        auto pattern = find_pattern("83 3D ? ? ? ? 02 7C 0A F3 0F");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);
    }

    // PipeBombDropIconSupport: the nine-byte check before the anchor is deleted, so
    // the pipe-bomb drop icon is accepted on the episodic builds.
    void InstallPipeBombDropIconSupport()
    {
        if (!Loader::Enabled("PipeBombDropIconSupport"))
            return;

        auto pattern = find_pattern("83 7D 18 1C 75 14");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(-9), 9, true);
    }

    // EpisodicVehicleSupport: the episode test at each site is deleted, so every
    // vehicle check that is gated on "not episodic" runs on the episodic builds.
    //
    // Two parts, because the original's handler has two:
    //   * 1.0.7.0 and 1.0.8.0 each retarget the absolute operand of two
    //     `cmp dword ptr [episode], 2` sites (the first also a `jnz`, the second a
    //     `jne rel32`) at this module's episode word. The original's recovered table
    //     does that as four *RVA installs* -- a rebased pointer store at a fixed RVA
    //     (build 8: 0x21500 and 0x7D91AE; build 9: 0x95040 and 0x684ABE), not a
    //     pattern hit. The store value proves it: it is this module's own pointer,
    //     which a signature could not name. Spelling the game's absolute operand out
    //     in a signature would also bake in the preferred-base VA, which the loader
    //     rewrites on every run (the executable carries DYNAMIC_BASE), so such an
    //     arm could never match the live image and would never fire in the game;
    //   * every gated build then gets the 23 signature-driven NOP rows, in the
    //     original's row order. The first two rows write every hit of a multi-hit
    //     scan (two each on this image) and two rows carry a second window.
    void InstallEpisodicVehicleSupport()
    {
        if (!Loader::Enabled("EpisodicVehicleSupport"))
            return;

        const auto flag = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_episode_flag));
        struct FlagStore final
        {
            Build build;
            uint32_t rva;
        };
        static constexpr FlagStore kEpisodeFlagStores[] = {
            { Build::v1070, 0x021500u },
            { Build::v1070, 0x7D91AEu },
            { Build::v1080, 0x095040u },
            { Build::v1080, 0x684ABEu },
        };
        for (const auto& store : kEpisodeFlagStores)
        {
            if (Loader::Current() != store.build)
                continue;

            auto* const site =
                reinterpret_cast<std::byte*>(Game::Get().ImageBase() + store.rva);
            if (!Failsafe::Site("EpisodicVehicleSupport episode word",
                    reinterpret_cast<uintptr_t>(site), sizeof(uint32_t)))
                continue;
            injector::WriteMemory<uint32_t>(site, flag, true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(site));
        }

        // Row 1: the `jl` of a loop that only runs on the episodic builds. Two
        // sites, both rewritten (the original writes hits[0] and hits[1]).
        auto pattern = find_pattern<2>("7C 18 0F BF 4E 2E");
        if (ExpectMatchCount(pattern, 2, "EpisodicVehicleSupport component window"))
        {
            for (size_t hit = 0; hit < pattern.size(); ++hit)
                injector::MakeNOP(pattern.get(hit).get<std::byte>(-6), 8, true);
        }

        // Row 2: the same `cmp [episode], 2` shape twice in two functions.
        pattern = find_pattern<2>("83 3D ?? ?? ?? ?? 02 75 17 F3 0F 10 44 24 08");
        if (ExpectMatchCount(pattern, 2, "EpisodicVehicleSupport episode test"))
        {
            for (size_t hit = 0; hit < pattern.size(); ++hit)
                injector::MakeNOP(pattern.get(hit).get<std::byte>(0), 9, true);
        }

        // Rows 3-20: one window each.
        pattern = find_pattern("39 35 ?? ?? ?? ?? 75 0E 80 3D");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(6), 2, true);

        struct EpisodeWindow
        {
            const char* signature;
            int offset;
            uint8_t length;
            int secondOffset;
            uint8_t secondLength;
        };
        static constexpr EpisodeWindow kEpisodeWindows[] = {
            { "74 17 83 3D ?? ?? ?? ?? 02 75 0E F3 0F 10 05", 2, 9, 0, 0 },
            { "7C 22 83 F8 28 75 1D", 0, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 0F 8C 0E 05 00 00", 0, 13, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 08 3B 05", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 17 F3 0F 10 44 24 0C", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 1F 8B 44 24 04", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 21 8B 96 20 08 00 00", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 24 F6 87 6C 02 00 00 04", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 35 0F BF 46 2E", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 38 8B 16", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 11 8B 4E", 7, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 1F 8B", 7, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 26 8B 85 40 0B 00 00", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 35 0F BF 43 2E", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 43", 7, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 4A 0F BF 46 2E", 0, 13, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 89 44 24 38", 11, 6, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 8A 0D", 0, 7, 17, 2 },
            { "83 3D ?? ?? ?? ?? 02 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 44 24 20 75 4C", 21, 2, 0, 0 },
            { "8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18", -15, 7, 10, 2 },
        };
        for (const auto& row : kEpisodeWindows)
        {
            pattern = find_pattern(row.signature);
            if (pattern.empty())
                continue;
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeNOP(match + row.offset, row.length, true);
            if (row.secondLength != 0)
                injector::MakeNOP(match + row.secondOffset, row.secondLength, true);
        }
    }

    void InstallLoadDLCs()
    {
        if (!Loader::Enabled("LoadDLCs"))
            return;

        {
            // The discovery body builds every candidate path in a 0x104-byte
            // buffer; a module path that already fills it truncates silently and
            // registers the wrong content directory. Skip the arm instead.
            char modulePath[0x104] = {};
            const DWORD length = GetModuleFileNameA(nullptr, modulePath, sizeof(modulePath));
            if (length == 0 || !Episodic::DlcPathFits(std::string_view(modulePath, length)))
            {
                Log::Print("dlc_path: module path length {} does not fit 0x104; skipped", length);
                return;
            }
        }

        const DlcBuild* const row = DlcBuildFor(Loader::Current());
        if (row == nullptr)
            return;

        auto* const base = reinterpret_cast<std::byte*>(Game::Get().ImageBase());
        for (const auto& write : row->writes)
        {
            auto* site = base + write.site;
            const size_t width = write.kind == DlcWriteKind::nop_tail ? kDlcTailNopLength : 5;
            if (!Failsafe::Site("LoadDLCs site", reinterpret_cast<uintptr_t>(site), width))
                continue;
            switch (write.kind)
            {
            case DlcWriteKind::jump_rva:
                injector::MakeJMP(site, base + write.target, true);
                break;
            case DlcWriteKind::call_rva:
                injector::MakeCALL(site, base + write.target, true);
                break;
            case DlcWriteKind::jump_body:
                if (!Failsafe::Body("LoadDLCs struct fixup",
                        reinterpret_cast<uintptr_t>(&DlcStructFixupBody)))
                    continue;
                injector::MakeJMP(site, reinterpret_cast<void*>(&DlcStructFixupBody), true);
                break;
            case DlcWriteKind::call_body:
                if (!Failsafe::Body("LoadDLCs dummy",
                        reinterpret_cast<uintptr_t>(&DlcDummyBody)))
                    continue;
                injector::MakeCALL(site, reinterpret_cast<void*>(&DlcDummyBody), true);
                break;
            case DlcWriteKind::nop_tail:
                injector::MakeNOP(site, kDlcTailNopLength, true);
                break;
            }
        }
    }

}

export void* DlcDiscoveryForTest(void* object) noexcept
{
    return DlcDiscoveryCpp(object);
}

export void SetDlcRvasForTest(uint32_t registerRva, uint32_t applyRva) noexcept
{
    zpatch_dlc_register_rva = registerRva;
    zpatch_dlc_apply_rva = applyRva;
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

    // sub_10001280, transcribed in source/shared.ixx. The
    // original's `call 0x10001280` is an immediate, so this six-byte forwarder keeps
    // that shape while a module interface cannot export the naked body itself.
    static __declspec(naked) void DlcFormat()
    {
        __asm
        {
            jmp DWORD PTR [zpatch_format_entry]
        }
    }

// The 0x104-byte path plus the two tail fields the register call reads.
constexpr size_t kDlcPathCapacity = 0x104u;
constexpr size_t kDlcRequestFlagOffset = 0x158;
constexpr size_t kDlcRequestModeOffset = 0x166;
constexpr size_t kDlcRequestSize = 360;

struct DlcRegisterRequest
{
    char path[kDlcPathCapacity];
    uint8_t reserved[kDlcRequestFlagOffset - kDlcPathCapacity];
    uint32_t flag;
    uint8_t reserved2[kDlcRequestModeOffset -
                       (kDlcRequestFlagOffset + sizeof(uint32_t))];
    uint8_t mode;
    uint8_t tail[kDlcRequestSize - (kDlcRequestModeOffset + sizeof(uint8_t))];
};
static_assert(sizeof(DlcRegisterRequest) == kDlcRequestSize);

// The apply function's calling convention is the game's: `sub_88FE00` is
// `__thiscall` (`push ecx; push ebx; mov ebx, ecx; movzx ecx, [ebx+150h]`), so
// the DLC manager arrives in ECX and the 0x168-byte request pointer on the stack.
// The oracle's discovery calls it that way (`0x10023278`: `mov ecx, [manager]`,
// `push request`, `call [apply]`). A cdecl call leaves ECX with the caller's
// garbage, which is what faulted at `0x88FE04` on 2026-11-12.
using DlcRegisterFn = char(__stdcall*)(int, const char*, int);
using DlcApplyFn = void(__thiscall*)(void*, const void*);

// The apply function returns its second value in EDX. Capture it for the one
// caller that observes it (an existing \Episode254\). The manager goes to ECX and
// the request pointer on the stack; the callee pops it (`ret 4`), and the
// `mov esp, ebp` below discards any imbalance regardless.
static __declspec(naked) void DlcApplyCapture(void* object, const void* request,
    DlcApplyFn apply, uint32_t* out)
{
    __asm
    {
        push ebp
        mov ebp, esp
        push DWORD PTR [ebp + 0Ch]
        mov ecx, DWORD PTR [ebp + 8]
        call DWORD PTR [ebp + 10h]
        mov eax, DWORD PTR [ebp + 14h]
        mov DWORD PTR [eax], edx
        mov esp, ebp
        pop ebp
        ret
    }
}

// The discovery post step, rebuilt on the checked path API: it enumerates
// \dlc\tlad\content\ / \TLAD\, \dlc\TBoGT\content\ / \TBoGT\ and
// \Episode3\..\Episode254\ next to the executable, registers each existing
// folder once through the published game RVAs, applies it to the object, and
// sets the object's post-discovery mode byte. The original's EDX result is kept.
void* __cdecl DlcDiscoveryCpp(void* object)
{
    char modulePath[kDlcPathCapacity]{};
    const DWORD moduleLength = GetModuleFileNameA(nullptr, modulePath, sizeof(modulePath));
    if (moduleLength == 0 || moduleLength >= sizeof(modulePath))
        return object;
    if (zpatch_dlc_register_rva == 0 || zpatch_dlc_apply_rva == 0)
        return object;

    std::error_code ignored;
    std::filesystem::path base(modulePath);
    base.remove_filename();
    const auto gameBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    const auto registerFn = reinterpret_cast<DlcRegisterFn>(
        gameBase + zpatch_dlc_register_rva);
    const auto applyFn = reinterpret_cast<DlcApplyFn>(
        gameBase + zpatch_dlc_apply_rva);

    uint32_t lastApplyEdx = 0;
    bool episode254 = false;
    const auto tryPaths = [&](const std::filesystem::path& primary,
                              const std::filesystem::path& fallback, int id)
    {
        std::filesystem::path candidate = base / primary;
        if (!std::filesystem::exists(candidate, ignored))
        {
            if (primary == fallback)
                return;
            candidate = base / fallback;
            if (!std::filesystem::exists(candidate, ignored))
                return;
        }

        const std::string text = candidate.string();
        DlcRegisterRequest request{};
        if (text.size() >= sizeof(request.path))
            return;
        strcpy_s(request.path, sizeof(request.path), text.c_str());
        request.mode = 1;
        request.flag = 1;
        registerFn(id, request.path, 1);
        DlcApplyCapture(object, &request, applyFn, &lastApplyEdx);
        if (id == 2 * 254)
            episode254 = true;
    };

    tryPaths("dlc\\tlad\\content\\", "TLAD\\", 2);
    tryPaths("dlc\\TBoGT\\content\\", "TBoGT\\", 4);
    for (int index = 3; index < 255; ++index)
    {
        char suffix[64]{};
        std::snprintf(suffix, sizeof(suffix), "\\Episode%d\\", index);
        tryPaths(std::filesystem::path(suffix + 1), std::filesystem::path(suffix + 1), 2 * index);
    }

    *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(object) + 0xA4) = 1;
    *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(object) + 0x166) = 2;
    if (episode254)
        __asm mov edx, lastApplyEdx
    else
        __asm mov edx, 40h
    return object;
}

// replacement. It sets only AL, so a caller's `test al, al` sees success while the
// upper 24 bits of EAX stay whatever the caller had.
static __declspec(naked) void DlcDummyBody()
{
    __asm
    {
        mov al, 1
        ret 8
    }
}

// sub_10023490, recovered VA 0x10023490 (19 instructions): the DLC manager's
// initialisation tail. It clears the five request slots the site's loop cleared,
// marks the object initialised when it already was, and otherwise runs the
// loader callback and the discovery pass before setting the loaded byte. Entered
// through the site's jump, so the ESI object is live and the epilogue returns to
// the game function's caller.
static __declspec(naked) void DlcStructFixupBody()
{
    __asm
    {
        mov BYTE PTR [esi], 0
        mov BYTE PTR [esi + 40h], 0
        mov BYTE PTR [esi + 80h], 0
        mov BYTE PTR [esi + 0C0h], 0
        mov BYTE PTR [esi + 100h], 0
        cmp BYTE PTR [esi + 167h], 0
        je dlc_fixup_load
        mov BYTE PTR [esi + 168h], 1
    dlc_fixup_exit:
        pop esi
        pop ebx
        add esp, 14h
        ret
    dlc_fixup_load:
        mov ecx, esi
        call DWORD PTR [zpatch_dlc_load_callback]
        push esi
        call DlcDiscoveryCpp
        pop esi
        mov BYTE PTR [esi + 0A4h], 1
        jmp dlc_fixup_exit
    }
}
