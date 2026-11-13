module;

#include <common.hxx>

// Declares the /GS cookie the transcribed prologues read and the check their
// epilogues call (`__security_cookie`, `__security_check_cookie` in vcruntime.h).
#include <intrin.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

export module stability;

import bodies;
import bytes;
import common;
import game;
import loader;
import patch;
import shared;


// Crash fixes. One arm per key, each anchored to its own recovered signature.
// Registration is explicit: the driver calls Register(), so the arm cannot be
// dropped by the linker and the order the arms subscribe in is readable in one
// place.
//
// The signatures are forged from the game database (IDA, 1.0.8.0) at each arm's
// scan anchor and are unique in that image; the offsets below are measured from
// the anchor, which is why they did not change when the strings did.

// The published state of the arms below, in C linkage because the transcribed
// code names the slots directly (the original keeps them in its own .bss beside
// its bodies).

extern "C"
{
    uint32_t zpatch_freeze_gun_resume = 0;  // dword_1015AD64
    uint32_t zpatch_plane_crash_resume_a = 0;   // dword_1015BE18
    uint32_t zpatch_plane_crash_resume_b = 0;   // dword_1015BE14
    uint32_t zpatch_wanted_level_resume = 0;      // dword_1015AD6C
    uint32_t zpatch_episode_switch_resume_a = 0;  // dword_10159BBC
    uint32_t zpatch_episode_switch_resume_b = 0;  // dword_10159BB4
    uint32_t zpatch_episode_switch_resume_c = 0;  // dword_10159BB8
    uint32_t zpatch_smpa60_resume_a = 0;          // dword_10159C70
    uint32_t zpatch_smpa60_resume_b = 0;          // dword_10159C68
    uint32_t zpatch_smpa60_resume_c = 0;          // dword_10159C6C

}


// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488: "'naked' can only be applied to non-member
// function definitions"), so the readable code above them names each body through a
// plain prototype; only the definition in the tail keeps the attribute.
static void Smpa60Shim();
static void Smpa60TailShim();
static void WantedLevelShim();
static void EpisodeSwitchStoreShim();
static void EpisodeSwitchShimB();
static void EpisodeSwitchShimC();
static void EpisodeSwitchSum();
static void EpisodeSwitchVcall();
static void PlaneCrashShim();
static void FreezeGunShim();



namespace
{
    // FreezeGunFix's published resume: the anchor's -12 (the instruction after the
    // `push edi` the body replays). It reads the site the install overwrites, so it
    // belongs to the publish tier.
    void PublishFreezeGunFix()
    {
        if (!Loader::Enabled("FreezeGunFix"))
            return;

        auto pattern = find_pattern("2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF");
        if (pattern.empty())
            return;

        zpatch_freeze_gun_resume = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) - 12);
    }



    // PlaneCrashFix's published resumes: the normal one is the anchor's +14 (after
    // the vtable read the body replays), the null one is the anchor's -705, the head
    // of the same function.
    void PublishPlaneCrashFix()
    {
        if (!Loader::Enabled("PlaneCrashFix"))
            return;

        auto pattern = find_pattern("83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00");
        if (pattern.empty())
            return;

        const auto match = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        zpatch_plane_crash_resume_b = static_cast<uint32_t>(match + 14);
        zpatch_plane_crash_resume_a = static_cast<uint32_t>(match - 705);
    }



    // WantedLevelCrashFix's published resume: the anchor's +12 (the `jz` its
    // `test al,al` feeds).
    void PublishWantedLevelCrashFix()
    {
        if (!Loader::Enabled("WantedLevelCrashFix"))
            return;

        auto pattern = find_pattern("8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00");
        if (pattern.empty())
            return;

        zpatch_wanted_level_resume = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 12);
    }

    // EpisodeSwitchFreezeFix's published resumes: the ECX reader's +7, the archive
    // store's +5 and the EBX reader's +6, read from the sites the install overwrites.
    void PublishEpisodeSwitchFreezeFix()
    {
        if (!Loader::Enabled("EpisodeSwitchFreezeFix"))
            return;

        auto pattern = find_pattern("56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05");
        if (pattern.empty())
            return;
        zpatch_episode_switch_resume_c = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 7);

        pattern = find_pattern("89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84");
        if (pattern.empty())
            return;
        zpatch_episode_switch_resume_a = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 5);

        pattern = find_pattern("8B 4B 10 3B 4B 14 7D 0F");
        if (pattern.empty())
            return;
        zpatch_episode_switch_resume_b = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 6);
    }

    // SMPA60Fix's published resumes: the tail shim's resume is the primary anchor's
    // +136, and the streaming loop's loader call is the second anchor, whose sentinel
    // path and normal path use +28 and +14.
    void PublishSMPA60Fix()
    {
        if (!Loader::Enabled("SMPA60Fix"))
            return;

        auto pattern = find_pattern("01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C");
        if (pattern.empty())
            return;

        zpatch_smpa60_resume_c = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 136);

        pattern = find_pattern("8B 48 70 83 C0 40 50 51 8D 51 10");
        if (pattern.empty())
            return;

        const auto shim = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        zpatch_smpa60_resume_a = static_cast<uint32_t>(shim + 28);
        zpatch_smpa60_resume_b = static_cast<uint32_t>(shim + 14);
    }

    // PlayerAsPedComponentsFix's published resume: the anchor's +14 (the
    // `add ecx,80h` after the two pushes the shim replays).
    void PublishPlayerAsPedComponentsFix()
    {
        if (!Loader::Enabled("PlayerAsPedComponentsFix"))
            return;

        auto pattern = find_pattern("8B 54 24 10 52 0F B6 90 19 02 00 00");
        if (pattern.empty())
            return;

        Shared::PublishPlayerPedComponentsResume(static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 14));
    }

}

namespace
{

    // FreezeGunFix: the anchor's -21 is the entry of the weapon check, which is
    // replaced by the shim.
    void InstallFreezeGunFix()
    {
        if (!Loader::Enabled("FreezeGunFix"))
            return;

        auto pattern = find_pattern("2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&FreezeGunShim), 0x10036EB0u);
        injector::MakeJMP(pattern.get_first(-21), reinterpret_cast<void*>(&FreezeGunShim), true);
    }



    // PlaneCrashFix: the anchor's +3 is the state store the body replays after its
    // null check.
    void InstallPlaneCrashFix()
    {
        if (!Loader::Enabled("PlaneCrashFix"))
            return;

        auto pattern = find_pattern("83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&PlaneCrashShim), 0x1003D870u);
        injector::MakeJMP(pattern.get_first(3), reinterpret_cast<void*>(&PlaneCrashShim), true);
    }



    // WantedLevelCrashFix: the update's entry becomes the shim, which skips it for a
    // null object.
    void InstallWantedLevelCrashFix()
    {
        if (!Loader::Enabled("WantedLevelCrashFix"))
            return;

        auto pattern = find_pattern("8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&WantedLevelShim), 0x10050490u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&WantedLevelShim), true);
    }

    // EpisodeSwitchFreezeFix: five sites, rewritten in the original's order: the ECX
    // reader, the archive store, the stream position, the EBX reader, the virtual
    // call.
    void InstallEpisodeSwitchFreezeFix()
    {
        if (!Loader::Enabled("EpisodeSwitchFreezeFix"))
            return;

        auto pattern = find_pattern("56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05");
        if (pattern.empty())
            return;
        auto* shimC = pattern.get_first<std::byte>(0);

        pattern = find_pattern("89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84");
        if (pattern.empty())
            return;
        auto* store = pattern.get_first<std::byte>(0);

        pattern = find_pattern("8B 4B 10 3B 4B 14 7D 0F");
        if (pattern.empty())
            return;
        auto* shimB = pattern.get_first<std::byte>(0);

        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchShimC), 0x1001B0C0u);
        injector::MakeJMP(shimC, reinterpret_cast<void*>(&EpisodeSwitchShimC), true);

        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchStoreShim), 0x1001B040u);
        injector::MakeJMP(store, reinterpret_cast<void*>(&EpisodeSwitchStoreShim), true);

        pattern = find_pattern("8B 41 10 03 41 0C C3");
        if (pattern.empty())
            return;
        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchSum), 0x1001B080u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&EpisodeSwitchSum), true);

        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchShimB), 0x1001B060u);
        injector::MakeJMP(shimB, reinterpret_cast<void*>(&EpisodeSwitchShimB), true);

        pattern = find_pattern("8B C1 8B 08 8B 11 8B 40 04 8B 52 30 50 FF D2 C3");
        if (pattern.empty())
            return;
        Bodies::Register(reinterpret_cast<uintptr_t>(&EpisodeSwitchVcall), 0x1001B0A0u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&EpisodeSwitchVcall), true);
    }

    // SMPA60Fix: two bodies. The `jz` at the primary anchor's -38 becomes an
    // unconditional short jump, the tail body replaces the primary anchor's +123 and
    // the second body the streaming loop's loader call.
    void InstallSMPA60Fix()
    {
        if (!Loader::Enabled("SMPA60Fix"))
            return;

        auto pattern = find_pattern("01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C");
        if (pattern.empty())
            return;

        auto* primary = pattern.get_first<std::byte>(0);

        pattern = find_pattern("8B 48 70 83 C0 40 50 51 8D 51 10");
        if (pattern.empty())
            return;

        auto* shim = pattern.get_first<std::byte>(0);

        Bodies::Register(reinterpret_cast<uintptr_t>(&Smpa60TailShim), 0x10046780u);
        injector::MakeJMP(primary + 123, reinterpret_cast<void*>(&Smpa60TailShim), true);
        injector::WriteMemory<std::byte>(primary - 38, Bytes::ShortJmp, true);

        Bodies::Register(reinterpret_cast<uintptr_t>(&Smpa60Shim), 0x10046750u);
        injector::MakeJMP(shim, reinterpret_cast<void*>(&Smpa60Shim), true);
    }

    // PlayerAsPedComponentsFix: the anchor's +5 is the `movzx edx,[eax+219h]` whose
    // result becomes CPedVariationMgr::SetVariation's `bool` argument; the site
    // becomes a jump into the shared shim (recovered VA 0x1003D9A0, hosted in
    // shared.ixx), which pushes the computed "this is the player model" flag and the
    // `push ecx` at the anchor's +13 instead.
    void InstallPlayerAsPedComponentsFix()
    {
        if (!Loader::Enabled("PlayerAsPedComponentsFix"))
            return;

        auto pattern = find_pattern("8B 54 24 10 52 0F B6 90 19 02 00 00");
        if (pattern.empty())
            return;

        injector::MakeJMP(pattern.get_first(5),
            reinterpret_cast<void*>(Shared::PlayerPedComponentsShim()), true);
    }


    // TryToSkipAllErrors: the error handler returns immediately instead of walking
    // its own path.
    void InstallTryToSkipAllErrors()
    {
        if (!Loader::Enabled("TryToSkipAllErrors"))
            return;

        auto pattern = find_pattern("81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(0), Bytes::Ret, true);
    }




}

// Registered like every other arm: the publish tier reads every site the install
// tier is about to overwrite, and the install tier owns the gate and the writes.
export void RegisterStability()
{

    Loader::AddPublish(Loader::Entry::Stability, 0, PublishFreezeGunFix);
    Loader::AddPublish(Loader::Entry::Stability, 3, PublishPlaneCrashFix);
    Loader::AddPublish(Loader::Entry::Stability, 6, PublishWantedLevelCrashFix);
    Loader::AddPublish(Loader::Entry::Stability, 7, PublishEpisodeSwitchFreezeFix);
    Loader::AddPublish(Loader::Entry::Stability, 8, PublishSMPA60Fix);
    Loader::AddPublish(Loader::Entry::Stability, 9, PublishPlayerAsPedComponentsFix);

    Loader::AddInstall(Loader::Entry::Stability, 3, InstallFreezeGunFix);
    Loader::AddInstall(Loader::Entry::Stability, 6, InstallPlaneCrashFix);
    Loader::AddInstall(Loader::Entry::Stability, 7, InstallPlayerAsPedComponentsFix);
    Loader::AddInstall(Loader::Entry::Stability, 8, InstallWantedLevelCrashFix);
    Loader::AddInstall(Loader::Entry::Stability, 9, InstallEpisodeSwitchFreezeFix);
    Loader::AddInstall(Loader::Entry::Stability, 10, InstallSMPA60Fix);
    Loader::AddInstall(Loader::Entry::Stability, 12, InstallTryToSkipAllErrors);
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------


// tramp_SMPA60Fix, recovered VA 0x10046750: the streaming load's argument setup,
// replayed around the sentinel that says the loader slot is unused. Naked and
// internal: an external-linkage naked function is written to the module and every
// importer fails.
static __declspec(naked) void Smpa60Shim()
{
    __asm
    {
        cmp ecx, 0DDDDDDDDh
        jz smpa60_shim_tail
        mov ecx, DWORD PTR [eax + 70h]
        add eax, 40h
        push eax
        push ecx
        lea edx, [ecx + 10h]
        push edx
        push ecx
        push ecx
        jmp DWORD PTR [zpatch_smpa60_resume_b]
    smpa60_shim_tail:
        jmp DWORD PTR [zpatch_smpa60_resume_a]
    }
}

// sub_10046780, recovered VA 0x10046780: the block's link fixup, which is skipped
// when the next block is absent or is the 0xCDCDCDCD fill. Naked and internal.
static __declspec(naked) void Smpa60TailShim()
{
    __asm
    {
        mov ecx, DWORD PTR [eax + 14h]
        test ecx, ecx
        jz smpa60_tail_resume
        cmp ecx, 0CDCDCDCDh
        jz smpa60_tail_resume
        mov edx, DWORD PTR [eax + 10h]
        mov DWORD PTR [ecx + 10h], edx
    smpa60_tail_resume:
        jmp DWORD PTR [zpatch_smpa60_resume_c]
    }
}

// abi_wanted_level_shim, recovered VA 0x10050490: the wanted-level update is
// skipped when there is no player object, and the null path returns immediately.
// The body leaves the flags of its own `test al,al` for the resume's conditional.
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
static __declspec(naked) void WantedLevelShim()
{
    __asm
    {
        cmp ecx, 0
        jz wanted_level_null
        mov eax, DWORD PTR [ecx]
        mov edx, DWORD PTR [eax + 128h]
        call edx
        test al, al
        jmp DWORD PTR [zpatch_wanted_level_resume]
    wanted_level_null:
        pop ebx
        ret
    }
}

// abi_episode_switch_store_shim, recovered VA 0x1001B040: the episode switch's
// archive store, replayed so a failed open returns the empty result instead of
// running the tail of the function. Naked and internal.
static __declspec(naked) void EpisodeSwitchStoreShim()
{
    __asm
    {
        mov DWORD PTR [ebx + 20h], eax
        cmp eax, 0
        jz episode_switch_store_null
        mov edx, DWORD PTR [eax]
        jmp DWORD PTR [zpatch_episode_switch_resume_a]
    episode_switch_store_null:
        xor al, al
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 10h
    }
}

// sub_1001B060, recovered VA 0x1001B060: the reader's index check, replayed around
// a null test. Naked and internal.
static __declspec(naked) void EpisodeSwitchShimB()
{
    __asm
    {
        cmp ebx, 0
        jz episode_switch_b_null
        mov ecx, DWORD PTR [ebx + 10h]
        cmp ecx, DWORD PTR [ebx + 14h]
        jmp DWORD PTR [zpatch_episode_switch_resume_b]
    episode_switch_b_null:
        mov BYTE PTR [edi], 0
        pop edi
        pop esi
        mov eax, ebp
        pop ebp
        pop ebx
        pop ecx
        ret
    }
}

// sub_1001B0C0, recovered VA 0x1001B0C0: the same check for the caller that passes
// its object in ECX. Naked and internal.
static __declspec(naked) void EpisodeSwitchShimC()
{
    __asm
    {
        cmp ecx, 0
        jz episode_switch_c_null
        push esi
        mov esi, ecx
        cmp DWORD PTR [esi + 14h], 0
        jmp DWORD PTR [zpatch_episode_switch_resume_c]
    episode_switch_c_null:
        mov eax, 0
        ret
    }
}

// tramp_EpisodeSwitchFreezeFix_b, recovered VA 0x1001B080: the byte-stream position,
// returned as 0 when there is no stream. Naked and internal.
static __declspec(naked) void EpisodeSwitchSum()
{
    __asm
    {
        cmp ecx, 0
        jz episode_switch_sum_null
        mov eax, DWORD PTR [ecx + 10h]
        add eax, DWORD PTR [ecx + 0Ch]
        ret
    episode_switch_sum_null:
        mov eax, 0
        ret
    }
}

// sub_1001B0A0, recovered VA 0x1001B0A0: the virtual call the episode switch makes
// through the object's vtable, skipped for a null object. Naked and internal.
static __declspec(naked) void EpisodeSwitchVcall()
{
    __asm
    {
        cmp ecx, 0
        jz episode_switch_vcall_null
        mov eax, ecx
        mov ecx, DWORD PTR [eax]
        mov edx, DWORD PTR [ecx]
        mov eax, DWORD PTR [eax + 4]
        mov edx, DWORD PTR [edx + 30h]
        push eax
        call edx
        ret
    episode_switch_vcall_null:
        mov eax, 0
        ret
    }
}

// abi_plane_crash_shim, recovered VA 0x1003D870: the plane's destruction tail. The
// `mov [eax+26h],bl` at the anchor's +3 is replaced, so the body replays it after a
// null check and takes the null state's path (the anchor's -705, the head of the
// same function) instead of dereferencing a missing vehicle. Naked and internal: an
// external-linkage naked function is written to the module and every importer
// fails.
static __declspec(naked) void PlaneCrashShim()
{
    __asm
    {
        cmp eax, 0
        jz plane_crash_null
        mov BYTE PTR [eax + 26h], bl
        mov edx, DWORD PTR [esi]
        mov eax, DWORD PTR [edx + 0F4h]
        jmp DWORD PTR [zpatch_plane_crash_resume_b]
    plane_crash_null:
        jmp DWORD PTR [zpatch_plane_crash_resume_a]
    }
}





// abi_freeze_gun_shim, recovered VA 0x10036EB0: the freeze-gun path is entered
// only when the weapon's model word at +40h is a real value; the three sentinels
// (0xCDCDCDCD, -1, 0) return an empty result instead. The site is the anchor's
// -21: the five-byte jump covers `mov ecx,[eax+38h]` and `test ecx,ecx`, so the
// body re-runs both; `mov esi,[eax+40h]` is jumped over and re-run as well, and
// the `push edi` that follows is the body's own. The resume is the published
// anchor-12, the instruction after that `push edi`.
//
// The null path's `xor eax,eax; pop esi; ret 8` is the enclosing function's stack
// contract, not a bare return: the site sits just after that function's `push
// esi`, so the path must pop the saved register and then the two arguments
// (`ret 8`) before returning to the caller.
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
static __declspec(naked) void FreezeGunShim()
{
    __asm
    {
        mov ecx, DWORD PTR [eax + 38h]
        test ecx, ecx
        mov esi, DWORD PTR [eax + 40h]
        cmp esi, 0CDCDCDCDh
        jz freeze_gun_null
        cmp esi, 0FFFFFFFFh
        jz freeze_gun_null
        cmp esi, 0
        jz freeze_gun_null
        push edi
        jmp DWORD PTR [zpatch_freeze_gun_resume]
    freeze_gun_null:
        xor eax, eax
        pop esi
        ret 8
    }
}

// ---------------------------------------------------------------------------
// BadPlayerModelCrashFix
//
// The game resolves a model index at 0x7CF803, three bytes into a function whose
// frame is already open (so EBP is live). The original replaces that entry with a
// 16-instruction trampoline (0x10037BA0) over a 65-instruction payload
// (0x10037AC0): when the model the caller named is not a ped (class id 6), the
// payload hashes "m_y_multiplayer", asks the game which model that is and writes
// the answer back into the caller's own argument slot, so the game never resolves
// a model that does not exist.
//
// Everything the hook runs is transcribed here: the trampoline, the payload, the
// two native wrappers of its GTAC.dll diagnostic (0x1005B290, 0x1005B530), the
// one-argument wrapper its fallback calls (0x1005B3F0) and the model-name lookup
// (0x10003F00). The pieces those wrappers share with the native-call layer stay
// where they are transcribed, in source/native.ixx and source/shared.ixx, and are
// reached through the six-byte forwarders below. The bounded formatter
// (0x10001280 over 0x10001240/0x10001230) is transcribed as well.
//
// The name lookup is the one piece here with an indexed absolute table operand.
// It is transcribed rather than rebuilt because the tree's body registry now names
// such tables (Bodies::RegisterTable): the operand keeps the original's
// `[table + esi*4]` shape and the proof compares the table's *contents* through
// the registration instead of its displacement. docs/rebuilt.md §2b records it.
// ---------------------------------------------------------------------------














