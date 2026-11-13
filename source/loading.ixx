module;

#include <common.hxx>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <shlwapi.h>

export module loading;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import patch;
import shared;

// The load-time arms: the fixes the original applies while the game is loading,
// one arm per INI key, each anchored to its own recovered signature.

// RemoveGFWLUpdatePopup, as the original handler sub_10005500 applies it: the
// update window's date arithmetic at game 0x7CBF70 is replaced by a stub that
// first disables the four xlive.dll update-window flag slots, then replays the
// four instructions the jump overwrites and resumes at the anchor's +12.
// The published state of the arms below, in C linkage because the transcribed
// code names the slots directly (the original keeps them in its own .bss beside
// its bodies).

extern "C"
{
    uint32_t zpatch_gfwl_resume = 0;  // dword_10159C18
    uint32_t zpatch_xlive_base = 0;   // dword_10159C10
}


// The published state of the two build-gated arms below, in C linkage because the
// transcribed code names the slots directly. Each comment names the original's own
// resume word, the dword its body's `jmp` reads (0x10005659 and 0x10005679).

extern "C"
{
    uint32_t zpatch_benchmark_resume_a = 0;  // dword_1015AD50
    uint32_t zpatch_benchmark_resume_b = 0;  // dword_1015AD54
}

namespace
{
    // VSyncFix: both call shapes load the vsync flag from an operand the original
    // retargets to its own dword. The key's build mask excludes 1.0.8.0, where
    // neither anchor exists, so the arm is inert on that image.
    uint32_t zpatch_vsync_flag = 0;

    // OverrideGTADatPath's sibling key: the path the game's gta.dat push is
    // retargeted to. The original's buffer is 64 bytes and its static initializer
    // holds the stock path; the loader overwrites it from OverrideGTADatPathTo
    // with strcpy_s, so a value of 64 bytes or more faults through the CRT's
    // invalid-parameter path (preserved). Absent or "0" leaves the initializer.
    char zpatch_dat_path[64] = "common:/data/gta.dat";
}

extern "C"
{
    uint32_t zpatch_skip_intro_match = 0;    // dword_1015AD74
    uint32_t zpatch_skip_menu_resume = 0;    // dword_10159348
    uint8_t zpatch_skip_menu_flag = 0;       // byte_101A8450
}

// SavegameFix's four replacement functions, transcribed from the original's
// bodies. They replace game functions the save pipeline calls; each keeps the
// original's __stdcall argument cleanup.

extern "C"
{
    uintptr_t zpatch_savegame_close_handle = 0;   // &CloseHandle
    uintptr_t zpatch_savegame_create_mutex = 0;   // &CreateMutexA
}

// LocalSettingsPath, as the original handler sub_100513A0 applies it:
//
//   * two operand writes at the game's local-settings function (game 0x61E770):
//     the `call dword ptr [SHGetFolderPathA]` slot becomes this module's four-byte
//     call slot (original off_10158064, which pointed the game at its replacement
//     path helper sub_10051320), and the `push "Rockstar Games\GTA IV"` argument
//     becomes the "\GTA IV" suffix this module owns;
//   * a jump at the epilogue of the wide path initializer (game 0x856EE6) to the
//     detour body, and the same at the fallback epilogue (a site that does not
//     exist on 1.0.8.0);
//   * five captured game pointers the detour's step helper rewrites.

namespace
{
    // The size the original's strcpy_s/wcscpy_s copies pass, and the size of the
    // CWD buffer its replacement path helper formats into.
    constexpr std::size_t kLocalPathCapacity = 0x200u;
    constexpr std::size_t kLocalCwdCapacity = 260u;

    // The published state, in C linkage because the detour body's helper reads the
    // slots the way the original's bodies read its own .bss.
    char zpatch_local_ansi_cwd[kLocalCwdCapacity] = {};
    char zpatch_local_iv_suffix[8] = "\\GTA IV";
    uint32_t zpatch_local_saves_slot = 0;
    char* zpatch_local_path_08 = nullptr;          // szPathLocalAppdata
    wchar_t* zpatch_local_path_10 = nullptr;       // wszPathLocalAppdataW_EP
    const int* zpatch_local_episode_ptr = nullptr; // __dwCurrentEpisode
    char* zpatch_local_path_18 = nullptr;          // szPathLocalAppdata_EP
    wchar_t* zpatch_local_path_1c = nullptr;       // wszPathLocalAppdataW
}


// ---------------------------------------------------------------------------
// NewFilesCompatibility, as the original handler applies it: the two sides are the
// script native it registers and the radio-HUD texture lookups. The two radio-HUD
// update calls become this module's bodies and the registrar's own `ret` becomes a
// jump to the registration body. Both arms are gated on the two modules the original
// refuses to run beside. What the three radio-HUD pieces resolve to - the current
// texture dictionary, pgDictionary::Lookup and CSprite2d::SetTexture - is recorded in
// docs/rebuilt.md 2m, since none of them can be driven offline.

extern "C"
{
    // dword_10159C08: the game's file-registration entry, published from the
    // `call` operand the registrar site carries.
    uint32_t zpatch_newfiles_register_slot = 0;
}

// ---------------------------------------------------------------------------
// NewSavesCompatibility, as the original handler sub_10052200 applies it: the
// savegame-load entry is jumped to a trampoline that runs the prepare step, then
// replays the thirteen bytes the jump swallowed and resumes at the entry's +0xD.
//
// The two steps behind the trampoline are compiled C in the original and are
// rebuilt here with the same call sequence, the same conditions and the same
// writes:
//
//   * sub_10051FD0 (prepare) asks the game's own save-directory function for the
//     directory, builds "<directory>\<name>" from the buffer the handler captured,
//     and hands that path to the converter;
//   * sub_10051B30 (converter) opens the file, checks the 0x110-byte header's
//     "SAVE" magic at +0xC, walks the eighteen 9-byte chunk headers (allocating,
//     reading and freeing each payload), keeps three chunk sizes, and then patches
//     the three game sites whose reads depend on the save layout: the branch
//     opcode at 0x872185, the read call + pushed read size + branch at 0x7D8C50,
//     and the second LoadData call at 0x8DA090.
//
// The converter takes from the game exactly what the original takes: the
// directory function (called, not imitated) and the save-name buffer named by the
// handler's first anchor. Everything else is the save file itself, so the three
// writes are a deterministic function of the file's chunk sizes and of signatures
// that occur exactly once wherever they exist (checked against every image in
// game_refs).
//
// The two hooks the converter installs are compiled C in the original as well:
// sub_10051A90 reads the caller's bytes into its own 0xE78-byte scratch and keeps
// only the return value, and sub_10051AD0 consumes the newer save's extra twelve
// bytes on both sides of the original LoadData call. Both are rebuilt here with
// the same stack buffers and the same return-value rules.

extern "C"
{
    // The published state, by the names the original keeps in its own .bss.
    uint32_t zpatch_new_saves_src = 0;          // dword_1015AD9C: the game's save-name buffer
    uint32_t zpatch_new_saves_resume = 0;       // dword_1015AD88: the load entry's +0xD
    uint32_t zpatch_new_saves_loaddata = 0;     // dword_1015AD94: the game's LoadData entry
    uint32_t zpatch_new_saves_p3_original = 0;  // dword_1015AD8C: the second LoadData call
    uint8_t zpatch_new_saves_force_read = 0;    // byte_1015AD98: chunk 9 has the newer size
    uint8_t zpatch_new_saves_new_chunk16 = 0;   // byte_1015AD9A: chunk 16 has the newer size
    uint8_t zpatch_new_saves_extra_block = 0;   // byte_1015AD99: chunk 13 has the newer size
}


// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488: "'naked' can only be applied to non-member
// function definitions"), so the readable code above them names each body through a
// plain prototype; only the definition in the tail keeps the attribute.
static void GfwlStubBody();
static void RgscLoginRemovedBody();
static void BenchmarkBodyA();
static void BenchmarkBodyB();
static void SkipIntroBody();
static void SkipMenuBody();
static void SavegameNullBody();
static void SavegameCloseBody();
static void SavegameMutexBody();
static void SavegameCopyBody();
static void LocalSettingsDetourBody();
static void LocalSettingsFallbackBody();
static void NewfilesRegisterBody();
static void NewSavesReadHook();
static void NewSavesSecondLoadHook();
static void NewSavesTrampoline();

// The original's FusionFix probe (sub_10003E20): a loaded
// GTAIV.EFLC.FusionFix.asi whose two signature dwords do not match the known
// build closes SkipIntro/SkipMenu (FusionFix owns those fixes there). A loaded
// module probes the dwords unconditionally, as the original does.
static bool FusionFixGateClosed()
{
    const HMODULE module = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi");
    if (module == nullptr)
        return false;

    const auto* image = reinterpret_cast<const std::byte*>(module);
    uint32_t first{};
    uint32_t second{};
    std::memcpy(&first, image + 0x20D3, sizeof(first));
    std::memcpy(&second, image + 0x20D7, sizeof(second));
    return first != 0xC7C47589u || second != 0x00002446u;
}

// sub_10005340's RemoveGFWLUpdatePopup branch: store the "update disabled" dword
// into the four xlive.dll flag slots. `zpatch_xlive_base` is the module the
// loader captured; when xlive.dll is absent it is null and the write lands on the
// low 0x66F770 page, which is the original's fault (preserved, not guarded).
static void GfwlPatch()
{
    for (const uint32_t offset : { 0x66F770u, 0x66F774u, 0x6B55D0u, 0x6B55F0u })
        injector::WriteMemory<uint32_t>(zpatch_xlive_base + offset, 0x10000E83u, true);
}

// The inline assembler refuses `call memcpy` (it is a compiler intrinsic), so the
// copy path calls this thin wrapper instead; the original calls the CRT directly.
// The call shape is the same (`call imm`), the caller cleans the three arguments,
// and the copy itself is the CRT's.
static void __cdecl SavegameCopyBlock(void* destination, const void* source, std::size_t size)
{
    std::memcpy(destination, source, size);
}

namespace
{
    // A captured pattern operand is a 32-bit VA of the image *as loaded* (the same
    // helper as misc.ixx's): the loader has already applied the image's relocations,
    // so the dword is a live address and the cast is the whole conversion. The game
    // runs relocated, so subtracting the preferred base here would add the run's
    // whole relocation distance; the offline dump relocates its copy too.
    template <typename T>
    T* GamePointer(uint32_t value)
    {
        return reinterpret_cast<T*>(static_cast<uintptr_t>(value));
    }

    // The path text the step helper stores: "\saves\<name>\".
    void BuildAnsiPath(char* out, const char* name)
    {
        std::size_t offset = 0;
        for (const char character : std::string_view("\\saves\\"))
            out[offset++] = character;
        for (const char* character = name; *character != '\0'; ++character)
            out[offset++] = *character;
        out[offset++] = '\\';
        out[offset] = '\0';
    }

    void BuildWidePath(wchar_t* out, const wchar_t* name)
    {
        std::size_t offset = 0;
        for (const wchar_t character : std::wstring_view(L"\\saves\\"))
            out[offset++] = character;
        for (const wchar_t* character = name; *character != L'\0'; ++character)
            out[offset++] = *character;
        out[offset++] = L'\\';
        out[offset] = L'\0';
    }

    // The original copies with strcpy_s/wcscpy_s at the same capacity; these write
    // the same bytes without the secure-copy path.
    void CopyText(char* destination, std::size_t capacity, const char* source)
    {
        std::size_t length = 0;
        while (length + 1 < capacity && source[length] != '\0')
        {
            destination[length] = source[length];
            ++length;
        }
        destination[length] = '\0';
    }

    void CopyWideText(wchar_t* destination, std::size_t capacity, const wchar_t* source)
    {
        std::size_t length = 0;
        while (length + 1 < capacity && source[length] != L'\0')
        {
            destination[length] = source[length];
            ++length;
        }
        destination[length] = L'\0';
    }
}

// The detour body's helper (original sub_10050D10): rewrite the four settings and
// save path buffers the initializer has just computed to the relative
// "\saves\<episode>\" form. The first ANSI destination is hardcoded "GTA IV"; the
// other three follow the episode counter. Called between pusha/popa, so it may
// clobber the general registers.
static void LocalSettingsStep()
{
    const int episode = zpatch_local_episode_ptr != nullptr ? *zpatch_local_episode_ptr : 0;
    const char* ansiName = "GTA IV";
    const wchar_t* wideName = L"GTA IV";
    if (episode == 1)
    {
        ansiName = "TLaD";
        wideName = L"TLaD";
    }
    else if (episode == 2)
    {
        ansiName = "TBoGT";
        wideName = L"TBoGT";
    }

    char ansiIv[32];
    char ansiEpisode[32];
    wchar_t wideEpisode[32];
    BuildAnsiPath(ansiIv, "GTA IV");
    BuildAnsiPath(ansiEpisode, ansiName);
    BuildWidePath(wideEpisode, wideName);

    if (zpatch_local_path_18 != nullptr)
        CopyText(zpatch_local_path_18, kLocalPathCapacity, ansiIv);
    if (zpatch_local_path_08 != nullptr)
        CopyText(zpatch_local_path_08, kLocalPathCapacity, ansiEpisode);
    if (zpatch_local_path_1c != nullptr)
        CopyWideText(zpatch_local_path_1c, kLocalPathCapacity, wideEpisode);
    if (zpatch_local_path_10 != nullptr)
        CopyWideText(zpatch_local_path_10, kLocalPathCapacity, wideEpisode);
}

namespace
{
    // The replacement for the game's SHGetFolderPathA call at 0x61E770 (the
    // original's sub_10051320, __stdcall with five arguments): the fifth argument
    // is the destination, which receives "<cwd>\saves". The original builds the
    // text through the CRT and copies it with strcpy_s(dest, 0x104, ...); this
    // writes the same bytes with a bounded loop.
    int __stdcall LocalSettingsCopySaves(uint32_t, uint32_t, uint32_t, uint32_t, char* destination)
    {
        if (destination != nullptr)
        {
            std::size_t length = 0;
            while (length + 1 < kLocalCwdCapacity && zpatch_local_ansi_cwd[length] != '\0')
            {
                destination[length] = zpatch_local_ansi_cwd[length];
                ++length;
            }
            for (const char character : std::string_view("\\saves"))
            {
                if (length + 1 >= kLocalCwdCapacity)
                    break;
                destination[length++] = character;
            }
            destination[length] = '\0';
        }
        return 0;
    }

    // The original's static initializer captures the CWD and points its call slot
    // at the replacement helper; the handler only retargets the game's call to that
    // slot. Captured at load, before the game runs.
    void CaptureLocalSettings()
    {
        GetCurrentDirectoryA(static_cast<DWORD>(kLocalCwdCapacity), zpatch_local_ansi_cwd);
        zpatch_local_saves_slot =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&LocalSettingsCopySaves));
    }

}


namespace
{
    // The original's gate: NewFilesCompatibility is skipped entirely when GTAC.dll
    // or JacksIVFixes.asi is loaded, because both install the same feature.
    bool ConflictingFileModulePresent()
    {
        return GetModuleHandleA("GTAC.dll") != nullptr || GetModuleHandleA("JacksIVFixes.asi") != nullptr;
    }

    // sub_10003E50 again (source/shared.ixx): the bodies hash the file name with
    // the original's own resolver, so a "hash:NNNN" name resolves as it does there.
    uint32_t ResolveFileName(const char* name)
    {
        return reinterpret_cast<uint32_t(__cdecl*)(const char*)>(Shared::ValueResolver())(name);
    }

    // sub_100515D0, recovered VA 0x100515D0 (5 instructions): the registered
    // callback. It marks the object it is handed and returns it.
    void* __cdecl NewfilesCallback(void** object)
    {
        void* const result = *object;
        *reinterpret_cast<int*>(result) = 1;
        return result;
    }

    // sub_100515E0, recovered VA 0x100515E0 (6 instructions): register the
    // callback under the original's key through the published entry. A slot that
    // was never published is a null call, as the original's `call dword_10159C08`
    // is.
    void __cdecl NewfilesRegister()
    {
        using RegisterFn = void(__cdecl*)(uint32_t, void*);
        reinterpret_cast<RegisterFn>(zpatch_newfiles_register_slot)(
            0x2C89A1F9u, reinterpret_cast<void*>(&NewfilesCallback));
    }

    // The three helper scans the update bodies use. The original resolves each one
    // once behind a function-local static guard and re-reads the game state on
    // every call; a scan that misses leaves a null the body then calls or
    // dereferences, which is the original's fault and is preserved.
    uintptr_t FirstMatch(std::string_view signature)
    {
        auto pattern = find_pattern(signature);
        return pattern.empty() ? 0u : reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
    }

    // sub_10051730, recovered VA 0x10051730: the game's *current texture
    // dictionary*, not a file registry. The scan lands on `mov esi,g_pCurrentTxd`
    // inside CSprite2d::SetTexture, so the operand is that global and the
    // dereference returns the TXD the game has just made current. It is scanned
    // once and dereferenced on every call because the game swaps the dictionary as
    // it streams. A miss reads through the null the scan left behind, as the
    // original's `mov eax,[esi]` with ESI = 0 does. The game-facing behaviour of
    // this half is documented in docs/rebuilt.md 2m, which also records that it
    // cannot be driven from a synthetic image.
    void* NewfilesObject()
    {
        static const uintptr_t match = FirstMatch(
            "8B 35 ? ? ? ? 6A 00 50 E8 ? ? ? ? 83 C4 08 50 8B CE E8 ? ? ? ? 85 C0 89 07 5E 74 05");
        const uint32_t global = match != 0 ? *reinterpret_cast<const uint32_t*>(match + 2u) : 0u;
        return *reinterpret_cast<void**>(global);
    }

    // sub_10051610, recovered VA 0x10051610: ask the container whether the hash is
    // registered. The original tries one function signature and, on a miss, one
    // older build's; both are cached, and a double miss calls null.
    int NewfilesCallObject(void* object, uint32_t hash)
    {
        static const uintptr_t function = []() -> uintptr_t
        {
            if (const auto direct = FirstMatch("85 C9 53 55 56 57 74 40 8B 6C 24 14 8D 64 24 00"); direct != 0)
                return direct;
            return FirstMatch("53 55 8B 6C 24 0C 56 57 EB 06");
        }();
        using Fn = int(__fastcall*)(void*, int, uint32_t);
        return reinterpret_cast<Fn>(function)(object, 0, hash);
    }

    // sub_100517E0, recovered VA 0x100517E0: append the name to the container. The
    // scan lands twenty bytes inside the function, so its start is match - 0x14.
    int NewfilesCallAdd(void* self, void* name)
    {
        static const uintptr_t match = FirstMatch("8B 0F 85 C9 74 ? 0F B7 41 0A 66 85 C0");
        static const uintptr_t function = match != 0 ? match - 20u : 0u;
        using Fn = int(__fastcall*)(void*, int, void*);
        return reinterpret_cast<Fn>(function)(self, 0, name);
    }
}

// sub_10051890, recovered VA 0x10051890: the first file type's update, called
// with the object in ECX. Compiled in the original and rebuilt here: a name at
// +0x10 means "new file"; resolve it through the container's registry and append
// it to the +8 list when it is known. AL is the original's answer; the site's
// caller discards EAX.
static char __fastcall NewfilesUpdateABody(void* self, int)
{
    auto* base = static_cast<uint8_t*>(self);
    if (base[0x10] == 0)
        return 0;

    void* const object = NewfilesObject();
    if (NewfilesCallObject(object, ResolveFileName(reinterpret_cast<const char*>(base + 0x10))))
        NewfilesCallAdd(base + 8, base + 0x10);
    return 1;
}

// sub_100518D0, recovered VA 0x100518D0: the second file type's update, the same
// shape with the name at +0x10F and the list at +4.
static char __fastcall NewfilesUpdateBBody(void* self, int)
{
    auto* base = static_cast<uint8_t*>(self);
    if (base[0x10F] == 0)
        return 0;

    void* const object = NewfilesObject();
    if (NewfilesCallObject(object, ResolveFileName(reinterpret_cast<const char*>(base + 0x10F))))
        NewfilesCallAdd(base + 4, base + 0x10F);
    return 1;
}

namespace
{
    // The save image: an 0x110-byte header whose dword at +0xC is "SAVE", then
    // eighteen chunks of a 9-byte header whose dword at +5 is the chunk's total
    // size, followed by that minus nine payload bytes.
    constexpr uint32_t kNewSavesMagic = 0x45564153u;  // "SAVE"
    constexpr std::size_t kNewSavesHeaderSize = 0x110;
    constexpr std::size_t kNewSavesMagicOffset = 0x0C;
    constexpr std::size_t kNewSavesChunkCount = 18;
    constexpr std::size_t kNewSavesChunkHeaderSize = 9;
    constexpr std::size_t kNewSavesChunkSizeOffset = 5;

    // The three chunk sizes that identify the newer layout, and the read size the
    // converter writes for it: 0xE77 is the newer chunk 16 and 0xE6E its payload,
    // nine bytes less because the chunk header is not part of what the game reads.
    constexpr uint32_t kNewSavesChunk9Size = 0x4C45;
    constexpr uint32_t kNewSavesChunk13Size = 0x1741;
    constexpr uint32_t kNewSavesChunk16Size = 0x0E77;
    constexpr uint32_t kNewSavesReadSizeOld = 0x0CBE;
    constexpr uint32_t kNewSavesReadSizeNew = 0x0E6E;

    // The branch in front of the read site: `jne +0x26`, and the NOP pair that
    // replaces it when the newer chunk 16 is present.
    constexpr uint16_t kNewSavesBranchJne = 0x2675;
    constexpr uint16_t kNewSavesBranchNop = 0x9090;

    // The save-directory buffer (original byte_101A8308, 312 bytes up to the next
    // .bss object). The two hooks' scratch is their own frames, so no constant here
    // describes them: sub_10051A90's 0xE7C-byte frame and sub_10051AD0's `push 0Ch`
    // are written into the transcriptions.
    constexpr std::size_t kNewSavesDirectorySize = 312;

    // Where the writes land inside their anchors: the read call and its operand,
    // the pushed read size, the branch in front of the push, and the second
    // LoadData call's operand.
    constexpr std::ptrdiff_t kNewSavesCallSite = 6;
    constexpr std::ptrdiff_t kNewSavesCallOperand = 7;
    constexpr std::ptrdiff_t kNewSavesReadSizeOperand = 1;
    constexpr std::ptrdiff_t kNewSavesBranchSite = -5;
    constexpr std::ptrdiff_t kNewSavesLoadOperand = 0x1E;

    // `jz rel8`: the branch opcode the first site is restored to when the save is
    // not the newer layout (Bytes::ShortJmp is the other operand).
    constexpr std::byte kNewSavesJumpIfZero{ 0x74 };

    // The original resolves each converter site once, through a function-local
    // static whose initialisation the compiler guards with the TLS block (the
    // `_Init_thread_header`/`_Init_thread_footer` pairs in sub_10051B30). A miss
    // is cached as a null site, so a build without the pattern never scans again.
    uintptr_t NewSavesBranchSite()
    {
        static const uintptr_t site = FirstMatch("74 37 80 3D ? ? ? ? 00 75 2E 8D 84 ? ? ? ? 00 6A 1E");
        return site;
    }

    uintptr_t NewSavesReadSite()
    {
        static const uintptr_t site = FirstMatch("68 BE 0C 00 00 50 E8");
        return site;
    }

    uintptr_t NewSavesLoadSite()
    {
        static const uintptr_t site =
            FirstMatch("56 E8 ? ? ? ? E8 ? ? ? ? A1 ? ? ? ? 8D 0C 85 00 00 00 00");
        return site;
    }
}

// The test-only seam over the converter's walk (tests/save_walk.cpp). It answers what
// sub_10051B30 decides from the file - the eighteen chunk headers in file order, the
// three size flags and the three patch values those flags choose - and does nothing
// else: no site is patched, no hook is installed and the converter's published state
// is not touched, so calling it cannot change the game image or the module.
// `NewSavesConvert` runs the same walk (`NewSavesWalkFile`) and the same choice
// (`NewSavesChoosePatch`), so a test that calls this observes the production decision
// rather than a copy of it. The shipped ASI never calls it: the conversion happens
// behind the savegame-load trampoline, which no offline harness can reach.
//
// The faults are the original's, unchanged: a chunk size below nine underflows the
// `operator new` operand and `std::bad_alloc` leaves through this call, and a file
// shorter than its header leaves the magic test reading the stack buffer's tail.

export namespace Loading
{
    // One 9-byte chunk header: the five-byte tag (which the walk never tests) and the
    // dword at +5, the chunk's total size.
    struct SaveWalkChunk
    {
        char id[5];
        uint32_t size;
    };

    enum class SaveWalkEnd : uint8_t
    {
        noFile,    // fopen_s answered non-zero
        notASave,  // the dword at +0xC is not "SAVE"
        walked,    // all eighteen chunk headers were read
    };

    struct SaveWalkDecision
    {
        SaveWalkEnd end = SaveWalkEnd::noFile;
        uint32_t chunkCount = 0;                      // eighteen when walked
        SaveWalkChunk chunks[kNewSavesChunkCount]{};  // in file order
        uint8_t forceRead = 0;                        // byte_1015AD98: chunk 9 is 4C45h
        uint8_t extraBlock = 0;                       // byte_1015AD99: chunk 13 is 1741h
        uint8_t newChunk16 = 0;                       // byte_1015AD9A: chunk 16 is 0E77h
        uint8_t branchOpcode = 0;                     // site 872185h: EBh, else 74h
        uint32_t readSize = 0;                        // site 7D8C50h: 0E6Eh, else 0CBEh
        uint16_t readBranch = 0;                      // site 7D8C50h: 9090h, else 2675h
    };

    export SaveWalkDecision WalkSaveFile(const char* path);
}

namespace
{
    // The three values the patch step writes, chosen from the walk's flags by the two
    // conditions the original's `cmovne`s test: the branch opcode at 0x872185 from
    // chunk 9 (`mov ecx,0EBh` / `mov eax,74h` / `cmovnz eax,ecx` / `mov [esi],al`,
    // 0x10051D35-0x10051D42), and the pushed read size (`mov ecx,0E6Eh` /
    // `mov eax,0CBEh` / `cmovnz`, 0x10051E3B-0x10051E4A) with the branch in front of
    // it (`mov ecx,9090h` / `mov eax,2675h` / `cmovnz`, 0x10051E6E-0x10051E7B) from
    // chunk 16. Both callers of this - the patch step and the test seam - go through
    // it, so the value a test reads cannot drift from the value that is written.
    struct NewSavesPatchValues
    {
        std::byte branchOpcode;
        uint32_t readSize;
        uint16_t readBranch;
    };

    NewSavesPatchValues NewSavesChoosePatch(uint8_t forceRead, uint8_t newChunk16)
    {
        return {
            forceRead != 0 ? Bytes::ShortJmp : kNewSavesJumpIfZero,
            newChunk16 != 0 ? kNewSavesReadSizeNew : kNewSavesReadSizeOld,
            newChunk16 != 0 ? kNewSavesBranchNop : kNewSavesBranchJne,
        };
    }

    // The three site patches, in the original's order and with its exact writes.
    // A site whose pattern is absent is skipped and the others still run.
    void NewSavesPatchSites()
    {
        const NewSavesPatchValues values =
            NewSavesChoosePatch(zpatch_new_saves_force_read, zpatch_new_saves_new_chunk16);

        // 0x872185's branch: `jz rel8` unless the save has the newer chunk 9, where
        // it becomes `jmp rel8`, forcing the longer path.
        if (const uintptr_t site = NewSavesBranchSite(); site != 0)
        {
            injector::WriteMemory<std::byte>(site, values.branchOpcode, true);
        }

        // 0x7D8C50: `jne +0x26; push 0xCBE; push eax; call LoadData`. The call
        // becomes the plugin's read hook; the pushed size and the branch follow the
        // save's chunk 16 (`jne` and 0xCBE for the older layout, NOPs and 0xE6E for
        // the newer one). The original target is read before the call is replaced.
        if (const uintptr_t site = NewSavesReadSite(); site != 0)
        {
            int32_t displacement{};
            std::memcpy(&displacement, reinterpret_cast<const void*>(site + kNewSavesCallOperand),
                sizeof(displacement));
            const uint32_t originalTarget = static_cast<uint32_t>(site + kNewSavesCallSite + 5) +
                                            static_cast<uint32_t>(displacement);

            injector::MakeCALL(reinterpret_cast<void*>(site + kNewSavesCallSite),
                reinterpret_cast<void*>(&NewSavesReadHook), true);
            if (zpatch_new_saves_loaddata == 0)
                zpatch_new_saves_loaddata = originalTarget;

            injector::WriteMemory<uint32_t>(site + kNewSavesReadSizeOperand, values.readSize, true);
            injector::WriteMemory<uint16_t>(site + kNewSavesBranchSite, values.readBranch, true);
        }

        // 0x8DA090: the second LoadData call becomes the plugin's hook, and the
        // shared read entry is repointed at the function's own later call target
        // (the `E8` at the anchor's +0x1D), which is the entry both hooks then use.
        if (const uintptr_t site = NewSavesLoadSite(); site != 0)
        {
            int32_t displacement{};
            std::memcpy(&displacement, reinterpret_cast<const void*>(site + kNewSavesCallOperand),
                sizeof(displacement));
            const uint32_t originalTarget = static_cast<uint32_t>(site + kNewSavesCallSite + 5) +
                                            static_cast<uint32_t>(displacement);

            injector::MakeCALL(reinterpret_cast<void*>(site + kNewSavesCallSite),
                reinterpret_cast<void*>(&NewSavesSecondLoadHook), true);
            if (zpatch_new_saves_p3_original == 0)
                zpatch_new_saves_p3_original = originalTarget;

            int32_t loadDisplacement{};
            std::memcpy(&loadDisplacement, reinterpret_cast<const void*>(site + kNewSavesLoadOperand),
                sizeof(loadDisplacement));
            zpatch_new_saves_loaddata = static_cast<uint32_t>(site + kNewSavesLoadOperand + 4) +
                                        static_cast<uint32_t>(loadDisplacement);
        }
    }

    // sub_10051B30's first half: open the file, check the header's magic, then walk the
    // eighteen 9-byte chunk headers. The original's call sequence is kept, faults
    // included: the header read is unchecked (a short file leaves the buffer's tail as
    // the stack held it, and the magic test reads it), and `operator new(size - 9)` is
    // unchecked (a size below nine underflows the operand, which throws).
    //
    // What the walk decides lands in `walk`; it touches no module state, so the test
    // seam can call it without publishing anything.
    Loading::SaveWalkEnd NewSavesWalkFile(const char* path, Loading::SaveWalkDecision& walk)
    {
        std::FILE* stream = nullptr;
        if (fopen_s(&stream, path, "rb") != 0 || stream == nullptr)
            return Loading::SaveWalkEnd::noFile;

        uint8_t header[kNewSavesHeaderSize];
        (void)std::fread(header, kNewSavesHeaderSize, 1, stream);

        uint32_t magic = 0;
        std::memcpy(&magic, header + kNewSavesMagicOffset, sizeof(magic));
        if (magic != kNewSavesMagic)
        {
            std::fclose(stream);
            return Loading::SaveWalkEnd::notASave;
        }

        // One stack header for all eighteen reads: a short read leaves the previous
        // iteration's bytes in it, which is what the original's reused slot does.
        uint8_t chunkHeader[kNewSavesChunkHeaderSize];
        for (std::size_t index = 0; index < kNewSavesChunkCount; ++index)
        {
            (void)std::fread(chunkHeader, kNewSavesChunkHeaderSize, 1, stream);
            uint32_t size = 0;
            std::memcpy(&size, chunkHeader + kNewSavesChunkSizeOffset, sizeof(size));

            const uint32_t payload = size - static_cast<uint32_t>(kNewSavesChunkHeaderSize);
            void* const payloadBlock = ::operator new(payload);
            (void)std::fread(payloadBlock, 1, payload, stream);
            std::free(payloadBlock);

            std::memcpy(walk.chunks[index].id, chunkHeader, sizeof(walk.chunks[index].id));
            walk.chunks[index].size = size;
            walk.chunkCount = static_cast<uint32_t>(index) + 1;

            switch (index)
            {
            case 9:
                walk.forceRead = size == kNewSavesChunk9Size ? 1 : 0;
                break;
            case 13:
                walk.extraBlock = size == kNewSavesChunk13Size ? 1 : 0;
                break;
            case 16:
                walk.newChunk16 = size == kNewSavesChunk16Size ? 1 : 0;
                break;
            default:
                break;
            }
        }
        std::fclose(stream);

        return Loading::SaveWalkEnd::walked;
    }

    // sub_10051B30, recovered VA 0x10051B30: the converter. The call sequence is
    // the original's, faults included: an unchecked header read, an unchecked
    // `operator new` per chunk (freed with `free`, exactly as the original mixes
    // the two), an unchecked payload read, and the patches only after the file is
    // closed. A chunk whose size is below nine underflows the payload, which makes
    // the allocation throw, as the original's does.
    void NewSavesConvert(const std::string& path)
    {
        Loading::SaveWalkDecision walk;
        if (NewSavesWalkFile(path.c_str(), walk) != Loading::SaveWalkEnd::walked)
            return;

        // The original stores each flag as it walks its chunk (0x1015AD98/99/9A);
        // here they become module state once the walk is over, before the patch step
        // that reads them. A walk that throws leaves them alone, and nothing can read
        // that difference: the throw leaves the converter.
        zpatch_new_saves_force_read = walk.forceRead;
        zpatch_new_saves_extra_block = walk.extraBlock;
        zpatch_new_saves_new_chunk16 = walk.newChunk16;

        NewSavesPatchSites();
    }

    // sub_10051FD0, recovered VA 0x10051FD0: the prepare step the trampoline calls.
    // The original's gate is the captured save-name buffer: when the handler never
    // found its anchor, nothing runs. The directory comes from the game's own
    // function (found by signature, called with the original's three arguments); a
    // zero answer skips the conversion. The path is the original's std::string: the
    // directory, one backslash, then the name.
    void NewSavesPrepare()
    {
        const auto* const name = reinterpret_cast<const char*>(static_cast<uintptr_t>(zpatch_new_saves_src));
        if (name == nullptr)
            return;

        // The original's lazily resolved directory function: one scan, cached with
        // its null on a miss.
        static const uintptr_t getDirectory = FirstMatch("55 8B EC 83 E4 F8 83 EC 4C 56 33 C0 8D 4C 24 28");
        if (getDirectory == 0)
            return;

        // The original's buffer is the module's .bss object byte_101A8308 (312 bytes
        // up to the next object): it persists across conversions and is zero before
        // the first one.
        static char directory[kNewSavesDirectorySize] = {};
        char suffix = 0;
        using GetSaveDirectoryFn = uint8_t(__cdecl*)(int, char*, char*);
        if (reinterpret_cast<GetSaveDirectoryFn>(getDirectory)(0, directory, &suffix) == 0)
            return;

        std::string path(directory);
        path += '\\';
        path += name;
        NewSavesConvert(path);
    }
}

// The test seam: the walk, plus the three values the patch step would write, which
// are a pure function of the two flags that decide them. Nothing is patched, no hook
// is installed, and the converter's published state is untouched.
Loading::SaveWalkDecision Loading::WalkSaveFile(const char* path)
{
    Loading::SaveWalkDecision decision;
    decision.end = NewSavesWalkFile(path, decision);

    const NewSavesPatchValues values =
        NewSavesChoosePatch(decision.forceRead, decision.newChunk16);
    decision.branchOpcode = static_cast<uint8_t>(values.branchOpcode);
    decision.readSize = values.readSize;
    decision.readBranch = values.readBranch;
    return decision;
}

namespace
{
    void CopyOverrideDatPath()
    {
        const std::string fallback("0");
        const auto& value = Feather::Value(Config::Ini(), "Options", "OverrideGTADatPathTo", fallback);
        if (value == "0")
            return;

        strcpy_s(zpatch_dat_path, sizeof(zpatch_dat_path), value.c_str());
    }
}

namespace
{
    void PublishLocalSettingsPath()
    {
        if (!Loader::Enabled("LocalSettingsPath"))
            return;

        CaptureLocalSettings();

        auto pattern = find_pattern("7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3");
        if (!pattern.empty())
        {
            // The wide path initializer's epilogue: each operand pushes one game
            // path buffer, or loads the episode counter.
            zpatch_local_path_1c = GamePointer<wchar_t>(*pattern.get_first<uint32_t>(-178));
            zpatch_local_path_10 = GamePointer<wchar_t>(*pattern.get_first<uint32_t>(-190));
            zpatch_local_episode_ptr = GamePointer<const int>(*pattern.get_first<uint32_t>(-151));
            zpatch_local_path_18 = GamePointer<char>(*pattern.get_first<uint32_t>(-444));
            zpatch_local_path_08 = GamePointer<char>(*pattern.get_first<uint32_t>(-432));
        }

        pattern = find_pattern("7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3");
        if (!pattern.empty())
        {
            // The alternate epilogue (absent on 1.0.8.0) stores the same two
            // LocalAppdata buffers the wide pattern captures.
            zpatch_local_path_18 = GamePointer<char>(*pattern.get_first<uint32_t>(-160));
            zpatch_local_path_1c = GamePointer<wchar_t>(*pattern.get_first<uint32_t>(-55));
        }
    }


    void PublishNewFilesCompatibility()
    {
        if (!Loader::Enabled("NewFilesCompatibility") || ConflictingFileModulePresent())
            return;

        // The registrar's `call <entry>`: the operand at +6 is the displacement the
        // update bodies register their callback through.
        auto pattern = find_pattern("68 64 28 C8 67 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 6, sizeof(displacement));
            zpatch_newfiles_register_slot = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match) + 10 + displacement);
        }
    }

    // NewSavesCompatibility's first anchor: the `push <save-name buffer>` site.
    // Its operand (the anchor's +1) is the game's buffer address, which is what the
    // prepare step appends to the save directory. The second anchor publishes the
    // load entry's continuation, +0xD.
    void PublishNewSavesCompatibility()
    {
        if (!Loader::Enabled("NewSavesCompatibility"))
            return;

        auto primary = find_pattern("68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08");
        if (primary.empty())
            return;
        zpatch_new_saves_src = *primary.get_first<uint32_t>(1);

        auto pattern = find_pattern("89 84 24 E0 01 00 00 8B 45 08 53 33 DB");
        if (!pattern.empty())
            zpatch_new_saves_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(13)));
    }

}

namespace
{

    // RemoveGFWLUpdatePopup: the anchor's first twelve bytes are the subtraction
    // and its two stores; the stub replays them and resumes at match + 12.
    void InstallRemoveGFWLUpdatePopup()
    {
        if (!Loader::Enabled("RemoveGFWLUpdatePopup"))
            return;

        // dword_10159C10: the xlive.dll module the original's loader captures.
        zpatch_xlive_base =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GetModuleHandleA("xlive.dll")));

        auto pattern = find_pattern("2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_gfwl_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match) + 12);
            injector::MakeJMP(match, reinterpret_cast<void*>(&GfwlStubBody), true);
        }
    }

    // RemoveRGSCLoginRequirement: the login-required branch of the RGSC launch
    // path. On 1.0.8.0 the original writes the same five-byte jump twice (the
    // duplicate is kept); the second anchor never matches on that build but does
    // on the ones the key was gated for.
    void InstallRemoveRGSCLoginRequirement()
    {
        if (!Loader::Enabled("RemoveRGSCLoginRequirement"))
            return;

        auto pattern = find_pattern("8B 44 24 ? 85 C0 74 ? F3 0F 7E 05 ? ? ? ? 66 0F D6 00");
        if (Loader::Current() == Build::v1080 && !pattern.empty())
        {
            auto* site = pattern.get_first<std::byte>(0);  // 0x498460
            injector::MakeJMP(site, reinterpret_cast<void*>(&RgscLoginRemovedBody), true);
            injector::MakeJMP(site, reinterpret_cast<void*>(&RgscLoginRemovedBody), true);
        }

        pattern = find_pattern("83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&RgscLoginRemovedBody), true);
    }


    void InstallVSyncFix()
    {
        if (!Loader::Enabled("VSyncFix"))
            return;

        auto pattern = find_pattern("8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(30),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_vsync_flag)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(30)));
        }

        pattern = find_pattern("8B 10 50 8B 82 A8 00 00 00 FF D0 83");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(13),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_vsync_flag)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(13)));
        }
    }

    // BenchmarkFix: two match sites eight bytes into the matched call sequence.
    // The resume is the anchor's +13, read before the sites are overwritten; the
    // key's build mask excludes 1.0.8.0, so this arm is inert on that image.
    void InstallBenchmarkFix()
    {
        if (!Loader::Enabled("BenchmarkFix"))
            return;

        auto pattern = find_pattern<2>("E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20");
        if (!pattern.empty())
        {
            // The two resumes are each match's own +13, not one shared value:
            // `mov eax,[edx]; add eax,0Dh; mov dword_1015AD50,eax` publishes the
            // first, `mov eax,[ecx+4]; add eax,0Dh; mov dword_1015AD54,eax` the
            // second, and the bodies read their own word
            // (`jmp [dword_1015AD50]` / `jmp [dword_1015AD54]`, 0x10005667 /
            // 0x10005687). The matches sit in different functions, so one shared
            // resume sends body B to body A's continuation.
            zpatch_benchmark_resume_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get(0).get<std::byte>(0)) + 13);
            if (pattern.size() > 1)
                zpatch_benchmark_resume_b = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(pattern.get(1).get<std::byte>(0)) + 13);

            injector::MakeJMP(pattern.get(0).get<std::byte>(8), reinterpret_cast<void*>(&BenchmarkBodyA), true);
            if (pattern.size() > 1)
                injector::MakeJMP(pattern.get(1).get<std::byte>(8), reinterpret_cast<void*>(&BenchmarkBodyB), true);
        }
    }

    // OverrideGTADatPath: the anchor is the gta.dat loader's `push <char*>`, and
    // the original rewrites just that immediate (the anchor's +1).
    void InstallOverrideGTADatPath()
    {
        if (!Loader::Enabled("OverrideGTADatPath"))
            return;

        auto pattern = find_pattern("68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(1),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(zpatch_dat_path)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(1)));
        }
    }

    // SkipIntro: the anchor is the intro sequence's store; the arm jumps from the
    // anchor's -6, and the body resumes at the anchor itself.
    void InstallSkipIntro()
    {
        if (!Loader::Enabled("SkipIntro") || FusionFixGateClosed())
            return;

        auto pattern = find_pattern("89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_skip_intro_match = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match));
            injector::MakeJMP(match - 6, reinterpret_cast<void*>(&SkipIntroBody), true);
        }
    }

    // SkipMenu: each anchor's call is retargeted to the body, and the resume is
    // the call's original target (the call's displacement is read before the
    // site is overwritten).
    void InstallSkipMenu()
    {
        if (!Loader::Enabled("SkipMenu") || FusionFixGateClosed())
            return;

        auto pattern = find_pattern("83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 24, sizeof(displacement));
            zpatch_skip_menu_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match) + 28 + displacement);
            injector::MakeCALL(match + 23, reinterpret_cast<void*>(&SkipMenuBody), true);
        }

        pattern = find_pattern("6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 3, sizeof(displacement));
            zpatch_skip_menu_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match) + 7 + displacement);
            injector::MakeCALL(match + 2, reinterpret_cast<void*>(&SkipMenuBody), true);
        }

        pattern = find_pattern("6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 3, sizeof(displacement));
            zpatch_skip_menu_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match) + 7 + displacement);
            injector::MakeCALL(match + 2, reinterpret_cast<void*>(&SkipMenuBody), true);
        }
    }

    // SavegameFix: two anchors, eleven call retargets and one two-byte NOP, in
    // the original handler's write order. The call displacements are excluded
    // from the parity comparison; the NOP is not.
    void InstallSavegameFix()
    {
        if (!Loader::Enabled("SavegameFix"))
            return;

        auto pattern = find_pattern("75 ? 83 C1 01 83 C0 01 83 F9 04");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeCALL(match - 73, reinterpret_cast<void*>(&SavegameCloseBody), true);
            injector::MakeCALL(match - 143, reinterpret_cast<void*>(&SavegameCopyBody), true);
            injector::MakeCALL(match - 176, reinterpret_cast<void*>(&SavegameCopyBody), true);
            injector::MakeCALL(match - 280, reinterpret_cast<void*>(&SavegameMutexBody), true);
            injector::MakeCALL(match - 240, reinterpret_cast<void*>(&SavegameNullBody), true);
            injector::MakeNOP(match, 2, true);
        }

        pattern = find_pattern("8D 4C 24 18 51 6A 00 6A 0C 57");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeCALL(match - 86, reinterpret_cast<void*>(&SavegameMutexBody), true);
            injector::MakeCALL(match - 47, reinterpret_cast<void*>(&SavegameNullBody), true);
            injector::MakeCALL(match + 10, reinterpret_cast<void*>(&SavegameCopyBody), true);
            injector::MakeCALL(match + 90, reinterpret_cast<void*>(&SavegameCopyBody), true);
            injector::MakeCALL(match + 128, reinterpret_cast<void*>(&SavegameCloseBody), true);
        }
    }

    void InstallLocalSettingsPath()
    {
        if (!Loader::Enabled("LocalSettingsPath"))
            return;

        auto pattern = find_pattern("53 8B 5C 24 08 56 6A 00 6A 00");
        if (!pattern.empty())
        {
            // +16 is the call's memory operand, +32 the pushed path-name operand.
            // Both addresses point into this module; the write-set dump expresses
            // them in the baseline's coordinates.
            auto* match = pattern.get_first<std::byte>(0);
            injector::WriteMemory<uint32_t>(match + 16,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_local_saves_slot)), true);
            injector::WriteMemory<uint32_t>(match + 32,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(zpatch_local_iv_suffix)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(match + 16));
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(match + 32));
        }

        pattern = find_pattern("7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(14),
                reinterpret_cast<void*>(&LocalSettingsDetourBody), true);

        pattern = find_pattern("7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(-13),
                reinterpret_cast<void*>(&LocalSettingsFallbackBody), true);

        // The detour body's inline `call` is invisible to C++; naming the helper
        // here keeps its out-of-line definition in the image.
        const auto step = reinterpret_cast<uintptr_t>(&LocalSettingsStep);
        (void)step;
    }


    void InstallNewFilesCompatibility()
    {
        if (!Loader::Enabled("NewFilesCompatibility") || ConflictingFileModulePresent())
            return;

        auto pattern = find_pattern("E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 7B FF FF FF");
        if (!pattern.empty())
            injector::MakeCALL(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&NewfilesUpdateABody), true);

        pattern = find_pattern("E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 6C FF FF FF");
        if (!pattern.empty())
            injector::MakeCALL(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&NewfilesUpdateBBody), true);

        pattern = find_pattern("68 64 28 C8 67 E8");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(28),
                reinterpret_cast<void*>(&NewfilesRegisterBody), true);
    }

    // The same load entry is the write. The original returns before this scan when
    // the first pattern missed, so the write is gated on the first pattern as well:
    // 1.0.0.4.0 carries the entry but not the name site, and the original writes
    // nothing there.
    void InstallNewSavesCompatibility()
    {
        if (!Loader::Enabled("NewSavesCompatibility"))
            return;

        auto primary = find_pattern("68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08");
        if (primary.empty())
            return;

        auto pattern = find_pattern("89 84 24 E0 01 00 00 8B 45 08 53 33 DB");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&NewSavesTrampoline), true);
    }

}

// Registered like every other arm: the install tier owns the gate.
export void RegisterLoading()
{
    Loader::AddPublish(Loader::Entry::Loading, 0, PublishLocalSettingsPath);
    Loader::AddPublish(Loader::Entry::Loading, 1, CopyOverrideDatPath);
    Loader::AddPublish(Loader::Entry::Loading, 3, PublishNewFilesCompatibility);
    Loader::AddPublish(Loader::Entry::Loading, 4, PublishNewSavesCompatibility);

    Loader::AddInstall(Loader::Entry::Loading, 2, InstallLocalSettingsPath);
    Loader::AddInstall(Loader::Entry::Loading, 3, InstallRemoveGFWLUpdatePopup);
    Loader::AddInstall(Loader::Entry::Loading, 4, InstallRemoveRGSCLoginRequirement);
    Loader::AddInstall(Loader::Entry::Loading, 6, InstallVSyncFix);
    Loader::AddInstall(Loader::Entry::Loading, 7, InstallBenchmarkFix);
    Loader::AddInstall(Loader::Entry::Loading, 8, InstallOverrideGTADatPath);
    Loader::AddInstall(Loader::Entry::Loading, 9, InstallSkipIntro);
    Loader::AddInstall(Loader::Entry::Loading, 10, InstallSkipMenu);
    Loader::AddInstall(Loader::Entry::Loading, 11, InstallSavegameFix);
    Loader::AddInstall(Loader::Entry::Loading, 13, InstallNewFilesCompatibility);
    Loader::AddInstall(Loader::Entry::Loading, 14, InstallNewSavesCompatibility);

    // The hook bodies carry their recovered VAs so the dump can express every
    // branch in the original's coordinates.
    Bodies::Register(reinterpret_cast<uintptr_t>(&LocalSettingsDetourBody), 0x100512F0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&LocalSettingsFallbackBody), 0x10051300u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GfwlStubBody), 0x100054E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&RgscLoginRemovedBody), 0x10038550u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BenchmarkBodyA), 0x10005650u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BenchmarkBodyB), 0x10005670u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SkipIntroBody), 0x10045C30u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SkipMenuBody), 0x10046610u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameNullBody), 0x10038570u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameCloseBody), 0x10045580u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameCopyBody), 0x100455A0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameMutexBody), 0x100455E0u);


    // NewFilesCompatibility's registration tail (transcribed) and its two update
    // bodies (rebuilt, so they carry no proof row).
    Bodies::Register(reinterpret_cast<uintptr_t>(&NewfilesRegisterBody), 0x10051600u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NewfilesUpdateABody), 0x10051890u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NewfilesUpdateBBody), 0x100518D0u);

    // NewSavesCompatibility's trampoline (transcribed) and the four steps behind it
    // (the original's compiled C, rebuilt, so they carry no proof row): the prepare
    // step, the converter, and the two hooks it installs at conversion time.
    Bodies::Register(reinterpret_cast<uintptr_t>(&NewSavesTrampoline), 0x100521E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NewSavesPrepare), 0x10051FD0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NewSavesConvert), 0x10051B30u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NewSavesReadHook), 0x10051A90u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NewSavesSecondLoadHook), 0x10051AD0u);

    // The registrar's inline-asm `call` and the callbacks it names are invisible
    // to C++; naming them here keeps their out-of-line definitions in the image.
    const auto newfilesRegister = reinterpret_cast<uintptr_t>(&NewfilesRegister);
    const auto newfilesCallback = reinterpret_cast<uintptr_t>(&NewfilesCallback);
    (void)newfilesRegister;
    (void)newfilesCallback;

    // The two imports the save bodies call: the original calls them through its
    // own IAT slots, so the bodies call through these published addresses.
    zpatch_savegame_close_handle = reinterpret_cast<uintptr_t>(&CloseHandle);
    zpatch_savegame_create_mutex = reinterpret_cast<uintptr_t>(&CreateMutexA);

    // The copy body's inline `call` is invisible to C++; naming the wrapper here
    // keeps its out-of-line definition in the image.
    const auto copyBlock = reinterpret_cast<uintptr_t>(&SavegameCopyBlock);
    (void)copyBlock;
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// sub_100054E0, recovered VA 0x100054E0 (8 instructions): the shared
// Patch4RemoveModChecks / RemoveGFWLUpdatePopup stub. It runs the flag-gated
// helper between pusha/popa, replays the 64-bit ECX:ESI subtraction from EDX:EAX
// the site held (and the two stores of its result into the caller's argument
// slots), then tail-jumps to the replayed site's continuation. `pusha`/`popa` are
// emitted as bytes: the inline assembler renders both with a 0x66 operand-size
// prefix, which the original does not have.
static __declspec(naked) void GfwlStubBody()
{
    __asm
    {
        __emit 0x60  // pusha
        call GfwlPatch
        __emit 0x61  // popa
        sub eax, ecx
        sbb edx, esi
        mov DWORD PTR [esp + 0Ch], eax
        mov DWORD PTR [esp + 10h], edx
        jmp DWORD PTR [zpatch_gfwl_resume]
    }
}

// sub_10038550, recovered VA 0x10038550 (2 instructions): the RGSC login check's
// replacement. It sets only AL, so the caller sees "already signed in" and the
// upper 24 bits of EAX stay whatever the caller had, exactly as the original.
static __declspec(naked) void RgscLoginRemovedBody()
{
    __asm
    {
        mov al, 1
        ret
    }
}

// sub_10005650, recovered VA 0x10005650: BenchmarkFix's first match-site shim. It
// checks the benchmark object, runs the interposed callback (ESI with EDI), then
// resumes, or unwinds through the enclosing function's tail when the object is
// null.
static __declspec(naked) void BenchmarkBodyA()
{
    __asm
    {
        test eax, eax
        jz benchmark_a_zero
        push edi
        mov ebx, eax
        call esi
        jmp DWORD PTR [zpatch_benchmark_resume_a]
    benchmark_a_zero:
        xor al, al
        pop esi
        pop ebp
        pop ebx
        add esp, 10h
        ret
    }
}

// sub_10005670, recovered VA 0x10005670: BenchmarkFix's second match-site shim,
// the same shape as the first with its own resume.
static __declspec(naked) void BenchmarkBodyB()
{
    __asm
    {
        test eax, eax
        jz benchmark_b_zero
        push edi
        mov ebx, eax
        call esi
        jmp DWORD PTR [zpatch_benchmark_resume_b]
    benchmark_b_zero:
        xor al, al
        pop esi
        pop ebp
        pop ebx
        add esp, 10h
        ret
    }
}

// sub_10045C30, recovered VA 0x10045C30 (6 instructions): the intro-screen
// detour. Entered through a jump from the anchor's -6 with the site's ECX and
// EDX; it scales ECX by 400 and zeroes EDX below the 8000 threshold, which is
// what skips the intro outright, then resumes at the anchor.
static __declspec(naked) void SkipIntroBody()
{
    __asm
    {
        imul ecx, ecx, 190h
        cmp edx, 1F40h
        jge skip_intro_ok
        xor edx, edx
    skip_intro_ok:
        jmp DWORD PTR [zpatch_skip_intro_match]
    }
}

// sub_10046610, recovered VA 0x10046610 (12 instructions): the menu detour. The
// site's call is retargeted to this body, whose first call with a zero argument
// switches the menu id to 6 (one-shot through the flag), then tail-jumps to the
// original call's target; the call's own return address stays on the stack, so
// the target returns to the game's next instruction.
static __declspec(naked) void SkipMenuBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        cmp BYTE PTR [zpatch_skip_menu_flag], 0
        mov eax, DWORD PTR [ebp + 8]
        jne skip_menu_go
        test eax, eax
        jne skip_menu_go
        mov eax, 6
        mov BYTE PTR [zpatch_skip_menu_flag], 1
    skip_menu_go:
        mov DWORD PTR [ebp + 8], eax
        pop ebp
        jmp DWORD PTR [zpatch_skip_menu_resume]
    }
}

// sub_10038570, recovered VA 0x10038570 (2 instructions): the "no savegame"
// path returns 0.
static __declspec(naked) void SavegameNullBody()
{
    __asm
    {
        xor eax, eax
        ret 8
    }
}

// sub_10045580, recovered VA 0x10045580 (7 instructions): close the save
// handle and report success.
static __declspec(naked) void SavegameCloseBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push DWORD PTR [ebp + 8]
        call DWORD PTR [zpatch_savegame_close_handle]
        xor eax, eax
        pop ebp
        ret 4
    }
}

// sub_100455E0, recovered VA 0x100455E0 (19 instructions): create the save
// mutex into the caller's handle slot; a null slot reports 1.
static __declspec(naked) void SavegameMutexBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push esi
        mov esi, DWORD PTR [ebp + 0Ch]
        test esi, esi
        jz savegame_mutex_null
        push 0
        push 0
        push 0
        call DWORD PTR [zpatch_savegame_create_mutex]
        mov DWORD PTR [esi], eax
        xor eax, eax
        pop esi
        pop ebp
        ret 8
    savegame_mutex_null:
        mov eax, 1
        pop esi
        pop ebp
        ret 8
    }
}

// sub_100455A0, recovered VA 0x100455A0 (28 instructions): the read/copy path.
// It refuses when the destination buffer is smaller than the requested size
// (0x8007007A), otherwise copies through the CRT and records the size.
static __declspec(naked) void SavegameCopyBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push esi
        mov esi, DWORD PTR [ebp + 0Ch]
        push edi
        mov edi, DWORD PTR [ebp + 14h]
        cmp DWORD PTR [edi], esi
        jnb savegame_copy_room
        mov DWORD PTR [edi], esi
        mov eax, 8007007Ah
        pop edi
        pop esi
        pop ebp
        ret 14h
    savegame_copy_room:
        mov eax, DWORD PTR [ebp + 10h]
        test eax, eax
        jz savegame_copy_skip
        push esi
        push DWORD PTR [ebp + 8]
        push eax
        call SavegameCopyBlock
        add esp, 0Ch
    savegame_copy_skip:
        mov DWORD PTR [edi], esi
        xor eax, eax
        pop edi
        pop esi
        pop ebp
        ret 14h
    }
}

// sub_100512F0, recovered VA 0x100512F0 (7 instructions): the wide path
// initializer's epilogue detour (game 0x856EE6). It runs the step helper between
// pusha/popa and replays the three pops and the return the five-byte jump
// swallowed. `pusha`/`popa` are emitted as bytes: the inline assembler renders
// both mnemonics with a 0x66 operand-size prefix, which the original does not
// have.
static __declspec(naked) void LocalSettingsDetourBody()
{
    __asm
    {
        __emit 0x60  // pusha
        call LocalSettingsStep
        __emit 0x61  // popa
        pop edi
        pop esi
        pop ebx
        ret
    }
}

// sub_10051300, recovered VA 0x10051300 (11 instructions): the fallback epilogue
// detour (a site that does not exist on 1.0.8.0). It runs the same helper, then
// re-issues the four-argument call the site held through ESI and replays the two
// pops and the return. Register ESI is the call target on entry, so the body
// saves it through pusha/popa and uses the live value.
static __declspec(naked) void LocalSettingsFallbackBody()
{
    __asm
    {
        __emit 0x60  // pusha
        call LocalSettingsStep
        __emit 0x61  // popa
        push 0
        push 0
        push 1Ch
        push 0
        call esi
        pop esi
        pop edi
        ret
    }
}


// sub_10051600, recovered VA 0x10051600 (4 instructions): the registrar's
// replacement tail. The original runs the registration between pushad/popad so
// the caller's registers survive, then returns to the registrar's caller.
static __declspec(naked) void NewfilesRegisterBody()
{
    __asm
    {
        __emit 0x60  // pushad
        call NewfilesRegister
        __emit 0x61  // popad
        ret
    }
}

// sub_10051A90, recovered VA 0x10051A90 (20 instructions): the read at game
// 0x7D8C50. It reads into its own 0xE78-byte local - the caller's buffer,
// `[ebp+8]`, is deliberately left untouched - and answers the read's value; an
// unpublished read entry skips the call and leaves EAX at the zero the test just
// saw, which is the value this returns. The /GS frame and the returned register are
// the original's, so this replaces the C++ rebuild the tree carried (docs/rebuilt.md).
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void NewSavesReadHook()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 0E7Ch
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        mov eax, DWORD PTR [zpatch_new_saves_loaddata]
        test eax, eax
        je newsaves_read_skip
        push DWORD PTR [ebp + 0Ch]
        lea ecx, [ebp - 0E7Ch]
        push ecx
        call eax
        add esp, 8
    newsaves_read_skip:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10051AD0, recovered VA 0x10051AD0 (33 instructions): the second LoadData call
// at game 0x8DA090. When the save has the newer chunk 13 it consumes a twelve-byte
// block before and after the original call - both reads run through the same slot
// and the second one's value is what the body leaves in EAX - and otherwise the
// original call's value passes through. The original call itself is unconditional
// (a call through an unpublished slot faults), and it is the site's own target the
// installer republished. Transcribed, so the C++ rebuild is gone (docs/rebuilt.md).
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void NewSavesSecondLoadHook()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 10h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        cmp BYTE PTR [zpatch_new_saves_extra_block], 0
        je newsaves_second_before
        mov eax, DWORD PTR [zpatch_new_saves_loaddata]
        test eax, eax
        je newsaves_second_before
        lea ecx, [ebp - 10h]
        push 0Ch
        push ecx
        call eax
        add esp, 8
    newsaves_second_before:
        call DWORD PTR [zpatch_new_saves_p3_original]
        cmp BYTE PTR [zpatch_new_saves_extra_block], 0
        je newsaves_second_done
        mov eax, DWORD PTR [zpatch_new_saves_loaddata]
        test eax, eax
        je newsaves_second_done
        lea ecx, [ebp - 10h]
        push 0Ch
        push ecx
        call eax
        add esp, 8
    newsaves_second_done:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_100521E0, recovered VA 0x100521E0 (8 instructions): the savegame-load
// trampoline. pusha/popa keep the caller's registers across the prepare step; the
// four instructions after them are the one the five-byte jump overwrote
// (`mov [esp+0x1E0],eax`) and the three that follow it, and the tail jumps to the
// entry's +0xD. `pusha`/`popa` are emitted as bytes: the assembler renders both
// with a 0x66 operand-size prefix, which the original does not have.
static __declspec(naked) void NewSavesTrampoline()
{
    __asm
    {
        __emit 0x60  // pusha
        call NewSavesPrepare
        __emit 0x61  // popa
        mov dword ptr [esp + 1E0h], eax
        mov eax, dword ptr [ebp + 8]
        push ebx
        xor ebx, ebx
        jmp dword ptr [zpatch_new_saves_resume]
    }
}




