module;

#include <common.hxx>
#include <cstring>
#include <shlwapi.h>

export module episodic;

import bodies;
import bytes;
import common;
import game;
import loader;
import patch;

// The EPISODIC category: the DLC and episode gates the original's loader applies,
// the episode-only vehicle and weapon paths, and the signature check that keeps
// the episode archives loading. One arm per key, each anchored to its own
// recovered signature (or, for LoadDLCs, to the recovered per-build RVA rows).

// ---------------------------------------------------------------------------
// LoadDLCs, as the original handler sub_100234E0 applies it.
//
// The handler does not scan: per build it stores three game entry points and then
// rewrites six sites, each at a fixed RVA, with the instruction it computes in
// place (an optional jump/call to another fixed RVA, a jump/call to one of the two
// bodies below, or a twelve-byte NOP fill). The rows were read from the handler
// itself, which switches on the build id; 1.0.8.0 is the `cmp eax, 9` block and
// its `lea`/`add` immediates are the RVAs recorded here. The sites were checked
// against GTAIV_1060.exe, GTAIV_1070.exe and GTAIV.exe: each carries the shape the
// row replaces (`E8` for a call site, `E8` for the two rewritten call sites, `B8`
// for the struct-fixup site, and the register/callback body of the loader for the
// tail NOPs).
//
// The two bodies: 0x10023480 replaces the loader callback the register function
// calls (it reports success), and 0x10023490 replaces the DLC manager's
// initialisation tail, clearing five request slots and - the first time - running
// the discovery pass before marking the object loaded.

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
}

// The bounded formatter's entry, published by source/stability.ixx (sub_10001280).

extern "C"
{
    extern uint32_t zpatch_format_entry;
}

// The discovery pass's data (original sub_10023150): the five imported entries it
// calls through the original's own IAT slots, and the five literals it pushes. Each
// IAT operand is an absolute address, so the transcription names these slots and the
// instructions keep the original's `call ds:[...]` / `mov reg, ds:[...]` shapes.

extern "C"
{
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
}

// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488: "'naked' can only be applied to non-member
// function definitions"), so the readable code above them names each body through a
// plain prototype; only the definition in the tail keeps the attribute.
static void DlcDiscovery();
static void DlcDummyBody();
static void DlcStructFixupBody();

namespace
{
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

}

namespace
{
    // EpisodicVehicleSupport's own episode word: the game's unk_10158128, an int
    // holding 2, which every `cmp [episode], 2` below tests. The arm hands the game
    // this module's word instead, so the four 1.0.7.0/1.0.8.0 sites read a value
    // this module owns; the dump canonicalises the four published operands.
    uint32_t zpatch_episode_flag = 2;

    // One site of the episode gate: the recovered signature, the offset from its
    // anchor the NOP starts at, and how many bytes it covers. A row NOPs either
    // the comparison itself or the branch after the six-byte `cmp [episode],2`.
    struct EpisodeGateSite final
    {
        std::string_view signature;
        ptrdiff_t offset;
        size_t length;
    };

    // EpisodicWeaponSupport: the eight episode gates in front of the episodic
    // weapons. The key's build mask is the original's ten-build set (options.inc),
    // so the arm needs no gate of its own.
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

    // SCOSignatureFix: the instruction 32 bytes before the anchor becomes a short
    // jump, so the signature check that follows is skipped.
    void InstallSCOSignatureFix()
    {
        if (!Loader::Enabled("SCOSignatureFix"))
            return;

        auto pattern = find_pattern("8B 6C 24 1C 66 89 6E 14 64 8B 0D 2C 00 00 00 8B");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(-32), Bytes::ShortJmp, true);
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
            injector::WriteMemory<uint32_t>(site, flag, true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(site));
        }

        // Row 1: the `jl` of a loop that only runs on the episodic builds. Two
        // sites, both rewritten (the original writes hits[0] and hits[1]).
        auto pattern = find_pattern<2>("7C 18 0F BF 4E 2E");
        for (size_t hit = 0; hit < pattern.size(); ++hit)
            injector::MakeNOP(pattern.get(hit).get<std::byte>(-6), 8, true);

        // Row 2: the same `cmp [episode], 2` shape twice in two functions.
        pattern = find_pattern<2>("83 3D ?? ?? ?? ?? 02 75 17 F3 0F 10 44 24 08");
        for (size_t hit = 0; hit < pattern.size(); ++hit)
            injector::MakeNOP(pattern.get(hit).get<std::byte>(0), 9, true);

        // Rows 3-20: one window each.
        pattern = find_pattern("39 35 ?? ?? ?? ?? 75 0E 80 3D");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(6), 2, true);

        pattern = find_pattern("74 17 83 3D ?? ?? ?? ?? 02 75 0E F3 0F 10 05");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(2), 9, true);

        pattern = find_pattern("7C 22 83 F8 28 75 1D");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 2, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 0F 8C 0E 05 00 00");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 13, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 08 3B 05");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 17 F3 0F 10 44 24 0C");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 1F 8B 44 24 04");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 21 8B 96 20 08 00 00");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 24 F6 87 6C 02 00 00 04");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 35 0F BF 46 2E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 75 38 8B 16");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 11 8B 4E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 1F 8B");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 26 8B 85 40 0B 00 00");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 35 0F BF 43 2E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 9, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 43");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(7), 2, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 7C 4A 0F BF 46 2E");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(0), 13, true);

        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 89 44 24 38");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(11), 6, true);

        // Row 21: the branch and the byte it would skip to (a second window at +17).
        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 8A 0D");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeNOP(match, 7, true);
            injector::MakeNOP(match + 17, 2, true);
        }

        // Row 22: the float load's branch.
        pattern = find_pattern("83 3D ?? ?? ?? ?? 02 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 44 24 20 75 4C");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(21), 2, true);

        // Row 23: the loop head and its `jl` (a second window at +10).
        pattern = find_pattern("8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeNOP(match - 15, 7, true);
            injector::MakeNOP(match + 10, 2, true);
        }
    }

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

    void InstallLoadDLCs()
    {
        if (!Loader::Enabled("LoadDLCs"))
            return;

        const DlcBuild* const row = DlcBuildFor(Loader::Current());
        if (row == nullptr)
            return;

        auto* const base = reinterpret_cast<std::byte*>(Game::Get().ImageBase());
        for (const auto& write : row->writes)
        {
            auto* site = base + write.site;
            switch (write.kind)
            {
            case DlcWriteKind::jump_rva:
                injector::MakeJMP(site, base + write.target, true);
                break;
            case DlcWriteKind::call_rva:
                injector::MakeCALL(site, base + write.target, true);
                break;
            case DlcWriteKind::jump_body:
                injector::MakeJMP(site, reinterpret_cast<void*>(&DlcStructFixupBody), true);
                break;
            case DlcWriteKind::call_body:
                injector::MakeCALL(site, reinterpret_cast<void*>(&DlcDummyBody), true);
                break;
            case DlcWriteKind::nop_tail:
                injector::MakeNOP(site, kDlcTailNopLength, true);
                break;
            }
        }
    }
}

export void RegisterEpisodic()
{
    // The discovery pass's five imported entries, filled before any tier runs. The
    // original calls them through its own IAT slots; this module's are filled from
    // its own imports so the transcription's operands are absolute on both sides.
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

    // The hook bodies carry their recovered VAs so the dump can express every
    // branch in the original's coordinates.
    Bodies::Register(reinterpret_cast<uintptr_t>(&DlcDummyBody), 0x10023480u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&DlcStructFixupBody), 0x10023490u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&DlcDiscovery), 0x10023150u);

    // The arms keep the position they occupied in the original driver.
    Loader::AddPublish(Loader::Entry::Loading, 5, PublishLoadDLCs);

    Loader::AddInstall(Loader::Entry::Stability, 11, InstallSCOSignatureFix);
    Loader::AddInstall(Loader::Entry::Vehicles, 9, InstallEpisodicVehicleSupport);
    Loader::AddInstall(Loader::Entry::Vehicles, 11, InstallPipeBombDropIconSupport);
    Loader::AddInstall(Loader::Entry::Loading, 15, InstallLoadDLCs);
    Loader::AddInstall(Loader::Entry::Audio, 6, InstallEpisodicWeaponSupport);
    Loader::AddInstall(Loader::Entry::Audio, 7, InstallPoliceEpisodicWeaponSupport);
    Loader::AddInstall(Loader::Entry::Audio, 8, InstallTBoGTHeliHeightLimit);
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------


namespace
{
    // sub_10001280, transcribed in source/stability.ixx (docs/rebuilt.md). The
    // original's `call 0x10001280` is an immediate, so this six-byte forwarder keeps
    // that shape while a module interface cannot export the naked body itself.
    static __declspec(naked) void DlcFormat()
    {
        __asm
        {
            jmp DWORD PTR [zpatch_format_entry]
        }
    }
}

// sub_10023150, recovered VA 0x10023150 (223 instructions to its `ret`): the DLC
// discovery, transcribed instruction for instruction. For each of the two named DLC
// folders it picks one path - the "content" layout if it exists, else the legacy
// fallback - and probes \Episode3\ .. \Episode254\ next to the executable,
// registering every folder that exists and applying it to the object the body was
// entered with (`[ebp+8]`, the argument the fixup body pushes).
//
// It is transcribable: unlike the rest of the rebuilt set its instruction stream has
// no EH frame, no TLS static-init guard and no indexed absolute operand -- the
// /GS cookie is ordinary instructions, every call is a direct `E8` or an absolute
// IAT slot, and every buffer is ESP/EBP-relative. The proof row is why the C++ rebuild
// is gone; docs/rebuilt.md records the evidence and what is left unproven.
//
// Two details are the original's and not the rebuild's invention:
// - The register call is `call esi` with ESI = image base + the stored register RVA
//   and ECX left over from the preceding `strcpy_s`; the callee uses ECX only through
//   the loader callback this key replaces and a branch its non-zero flag skips, so
//   the value is never dereferenced. The transcription leaves ECX alone, exactly as
//   the original does, rather than inventing a `this`.
// - The episode id pushed for the register call is `lea eax,[edi+edi]` -- twice the
//   loop counter -- and the loop runs edi = 3 .. 254 inclusive, which is the original
//   `inc edi / cmp edi,0FFh / jl`.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void DlcDiscovery()
{
    __asm
    {
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        sub esp, 2C4h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, esp
        mov DWORD PTR [esp + 2C0h], eax
        mov eax, DWORD PTR [ebp + 8]
        push ebx
        push esi
        push edi
        push 104h
        mov DWORD PTR [esp + 14h], eax
        call DlcAlloc
        add esp, 4
        mov esi, eax
        mov DWORD PTR [esp + 14h], esi
        push 104h
        push esi
        push 0
        call DWORD PTR [zpatch_dlc_get_module_file_name]
        push esi
        call DWORD PTR [zpatch_dlc_path_remove_file_spec]
        mov ebx, DWORD PTR [zpatch_dlc_strcpy_s]
        lea eax, [esp + 180h]
        push esi
        push 104h
        push eax
        call ebx
        mov edi, DWORD PTR [zpatch_dlc_strcat_s]
        lea eax, [esp + 18Ch]
        add esp, 0Ch
        push OFFSET zpatch_dlc_tlad_content
        push 104h
        push eax
        call edi
        add esp, 0Ch
        lea eax, [esp + 180h]
        push eax
        call DWORD PTR [zpatch_dlc_path_file_exists]
        test eax, eax
        jne dlc_tlad_exists
        push esi
        lea eax, [esp + 184h]
        push 104h
        push eax
        call ebx
        add esp, 0Ch
        lea eax, [esp + 180h]
        push OFFSET zpatch_dlc_tlad_legacy
        push 104h
        push eax
        call edi
        add esp, 0Ch
        // The original's `jne 0x1002320E` lands here, on the *second* probe: a
        // "content" layout that exists is probed once more, and a failed probe still
        // skips the registration. The fallback path reaches the same instruction by
        // fallthrough, which is why the label sits on the probe and not on the
        // register block.
    dlc_tlad_exists:
        lea eax, [esp + 180h]
        push eax
        call DWORD PTR [zpatch_dlc_path_file_exists]
        test eax, eax
        je dlc_next
        mov ecx, DWORD PTR [zpatch_dlc_game_base]
        lea eax, [esp + 18h]
        mov esi, DWORD PTR [zpatch_dlc_register_rva]
        push 168h
        push 0
        push eax
        lea esi, [esi + ecx]
        call DlcZeroBlock
        add esp, 0Ch
        lea eax, [esp + 180h]
        push eax
        lea eax, [esp + 1Ch]
        push 104h
        push eax
        call ebx
        add esp, 0Ch
        mov BYTE PTR [esp + 17Eh], 1
        lea eax, [esp + 18h]
        mov DWORD PTR [esp + 170h], 1
        push 1
        push eax
        push 2
        call esi
        mov ecx, DWORD PTR [esp + 10h]
        lea eax, [esp + 18h]
        push eax
        mov eax, DWORD PTR [zpatch_dlc_apply_rva]
        add eax, DWORD PTR [zpatch_dlc_game_base]
        call eax
        mov esi, DWORD PTR [esp + 14h]
    dlc_next:
        push esi
        lea eax, [esp + 184h]
        push 104h
        push eax
        call ebx
        add esp, 0Ch
        lea eax, [esp + 180h]
        push OFFSET zpatch_dlc_tbogt_content
        push 104h
        push eax
        call edi
        add esp, 0Ch
        lea eax, [esp + 180h]
        push eax
        call DWORD PTR [zpatch_dlc_path_file_exists]
        test eax, eax
        jne dlc_tbogt_exists
        push esi
        lea eax, [esp + 184h]
        push 104h
        push eax
        call ebx
        add esp, 0Ch
        lea eax, [esp + 180h]
        push OFFSET zpatch_dlc_tbogt_legacy
        push 104h
        push eax
        call edi
        add esp, 0Ch
        // Same join as TLaD's: `jne 0x100232F8` lands on this second probe, and the
        // fallback reaches it by fallthrough.
    dlc_tbogt_exists:
        lea eax, [esp + 180h]
        push eax
        call DWORD PTR [zpatch_dlc_path_file_exists]
        test eax, eax
        je dlc_episodes
        mov ecx, DWORD PTR [zpatch_dlc_game_base]
        lea eax, [esp + 18h]
        mov esi, DWORD PTR [zpatch_dlc_register_rva]
        push 168h
        push 0
        push eax
        lea esi, [esi + ecx]
        call DlcZeroBlock
        add esp, 0Ch
        lea eax, [esp + 180h]
        push eax
        lea eax, [esp + 1Ch]
        push 104h
        push eax
        call ebx
        add esp, 0Ch
        mov BYTE PTR [esp + 17Eh], 1
        lea eax, [esp + 18h]
        mov DWORD PTR [esp + 170h], 1
        push 1
        push eax
        push 4
        call esi
        mov ecx, DWORD PTR [esp + 10h]
        lea eax, [esp + 18h]
        push eax
        mov eax, DWORD PTR [zpatch_dlc_apply_rva]
        add eax, DWORD PTR [zpatch_dlc_game_base]
        call eax
        mov esi, DWORD PTR [esp + 14h]
    dlc_episodes:
        mov edi, 3
    dlc_episode_loop:
        push esi
        lea eax, [esp + 184h]
        push 104h
        push eax
        call ebx
        add esp, 0Ch
        lea eax, [esp + 288h]
        push edi
        push OFFSET zpatch_dlc_episode_format
        push 40h
        push eax
        call DlcFormat
        lea eax, [esp + 298h]
        push eax
        lea eax, [esp + 194h]
        push 104h
        push eax
        call DWORD PTR [zpatch_dlc_strcat_s]
        add esp, 1Ch
        lea eax, [esp + 180h]
        push eax
        call DWORD PTR [zpatch_dlc_path_file_exists]
        test eax, eax
        je dlc_episode_next
        mov ecx, DWORD PTR [zpatch_dlc_game_base]
        lea eax, [esp + 18h]
        mov esi, DWORD PTR [zpatch_dlc_register_rva]
        push 168h
        push 0
        push eax
        lea esi, [esi + ecx]
        call DlcZeroBlock
        add esp, 0Ch
        lea eax, [esp + 180h]
        push eax
        lea eax, [esp + 1Ch]
        push 104h
        push eax
        call ebx
        add esp, 0Ch
        mov BYTE PTR [esp + 17Eh], 1
        lea eax, [esp + 18h]
        mov DWORD PTR [esp + 170h], 1
        push 1
        push eax
        lea eax, [edi + edi]
        push eax
        call esi
        mov ecx, DWORD PTR [esp + 10h]
        lea eax, [esp + 18h]
        push eax
        mov eax, DWORD PTR [zpatch_dlc_apply_rva]
        add eax, DWORD PTR [zpatch_dlc_game_base]
        call eax
        mov esi, DWORD PTR [esp + 14h]
    dlc_episode_next:
        inc edi
        cmp edi, 0FFh
        jl dlc_episode_loop
        mov eax, DWORD PTR [esp + 10h]
        mov ecx, DWORD PTR [esp + 2CCh]
        pop edi
        pop esi
        pop ebx
        xor ecx, esp
        mov BYTE PTR [eax + 166h], 2
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10023480, recovered VA 0x10023480 (2 instructions): the loader callback's
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
        call DlcDiscovery
        pop esi
        mov BYTE PTR [esi + 0A4h], 1
        jmp dlc_fixup_exit
    }
}
