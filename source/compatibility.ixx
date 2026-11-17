module;

#include <common.hxx>

// Declares the /GS cookie the transcribed savegame bodies' prologues read and the
// checks their epilogues call, and the CRT calls the converter and the Steam
// bridge make.
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>

export module compatibility;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import options;
import patch;
import shared;

// COMPATIBILITY: NewFilesCompatibility, NewSavesCompatibility,
// GFWLProtectionDisabler and SteamAchievements. The old `steam` module is the
// achievement bridge of the SteamAchievements arm and lives here now.
export namespace Compatibility
{
    void Prepare();

    void PublishNewFilesCompatibility();
    void PublishNewSavesCompatibility();

    void InstallNewFilesCompatibility();
    void InstallNewSavesCompatibility();
    void InstallGFWLProtectionDisabler();
    void InstallSteamAchievements();
}

// The Steam achievement mirror, implemented at the GFWL API boundary.
//
// GTA IV imports xlive.dll!XUserWriteAchievements by ordinal 5278 (a
// three-argument __stdcall). Replacing that import slot and calling the original
// first keeps the GFWL/xliveless achievement store working, while each id is
// mirrored to Steam's ISteamUserStats. Because the hook is at the import, no
// game-code patch (TryToSkipAllErrors, VSyncFix) can move or overwrite it.
//
// The hook is installed synchronously in the install pass. SteamAPI_Init loads
// libraries and starts threads, so it runs lazily on the first achievement write
// (a game thread), never in DllMain; ids observed before it succeeds stay queued
// and are replayed when it does.
//
// The achievement ids/names and the ISteamUserStats v012 method indices are
// recovered data, used with a provenance comment; every executable line here is
// written for this tree.
export namespace Steam
{
    // Installs the import hook. Answers false when the module is not the game
    // executable, steam_api.dll is absent, xlive is the packed system build, or
    // the ordinal is not imported; the game then keeps its GFWL-only behaviour.
    bool Install() noexcept;

    // The id -> Steam name mapping, exposed for the native test.
    const char* AchievementNameForTest(uint32_t id) noexcept;
}

// The published state of the two compatibility arms, in C linkage because the
// transcribed code names the slots directly.
extern "C"
{
    // dword_10159C08: the game's file-registration entry, published from the
    // `call` operand the registrar site carries.
    uint32_t zpatch_newfiles_register_slot = 0;
}

// The NewSaves published state, by the names the original keeps in its own .bss.
extern "C"
{
    uint32_t zpatch_new_saves_src = 0;          // dword_1015AD9C: the game's save-name buffer
    uint32_t zpatch_new_saves_resume = 0;       // dword_1015AD88: the load entry's +0xD
    uint32_t zpatch_new_saves_loaddata = 0;     // dword_1015AD94: the game's LoadData entry
    uint32_t zpatch_new_saves_p3_original = 0;  // dword_1015AD8C: the second LoadData call
    uint8_t zpatch_new_saves_force_read = 0;    // byte_1015AD98: chunk 9 has the newer size
    uint8_t zpatch_new_saves_new_chunk16 = 0;   // byte_1015AD9A: chunk 16 has the newer size
    uint8_t zpatch_new_saves_extra_block = 0;   // byte_1015AD99: chunk 13 has the newer size
}

// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488), so the readable code above them names
// each body through a plain prototype; only the definition in the tail keeps the
// attribute.
static void NewfilesRegisterBody();
static void NewSavesReadHook();
static void NewSavesSecondLoadHook();
static void NewSavesTrampoline();
static char __fastcall NewfilesUpdateABody(void* self, int);
static char __fastcall NewfilesUpdateBBody(void* self, int);
void NewSavesPrepare();
void NewSavesConvert(const std::string& path);

namespace
{
    // OpenPreCEPatch-only keys have no recovered catalogue row (the 99-key table
    // is the original's INI), so they are read directly. The read keeps the
    // original's gate: the stored value must be exactly 1.
    bool OpenOptionEnabled(std::string_view key, std::string_view fallback)
    {
        // The any-section read accepts OpenPreCEPatch's grouped INI as well as the
        // original flat one; `[Options]` is searched first inside it.
        return Config::Integer(Config::Ini(), key, fallback) == 1;
    }

    // The standalone GFWLProtectionDisabler2019.asi flips xlive.dll+0xF1605 from
    // its `jz`/`jmp` to a `jmp`. No xlive image is available to scan, so the RVA
    // is the only anchor and the pre-image byte is the guard: a different xlive
    // build is left untouched.
    // An offset in a module is only readable when it is inside the mapped image
    // and the containing page is committed and accessible. The OpenPreCEPatch
    // xlive arm has no signature to check, so this is its guard.
    bool ModuleImageContains(HMODULE module, size_t offset, size_t bytes) noexcept
    {
        if (module == nullptr || bytes == 0)
            return false;

        MODULEINFO info{};
        if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info)))
            return false;
        if (offset > info.SizeOfImage || bytes > info.SizeOfImage - offset)
            return false;

        const auto* address = reinterpret_cast<const uint8_t*>(module) + offset;
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(address, &region, sizeof(region)) != sizeof(region))
            return false;
        if (region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
            return false;
        return bytes <= region.RegionSize;
    }
}

// The GFWL xlive arm's module-image guard: an offset is readable only when it
// lies inside the mapped image and its page is committed and accessible.
export bool ModuleImageContainsForTest(uintptr_t module, size_t offset, size_t bytes) noexcept
{
    return ModuleImageContains(reinterpret_cast<HMODULE>(module), offset, bytes);
}

namespace Compatibility
{
    void Prepare()
    {
        // NewFilesCompatibility's registration tail (transcribed) and its two
        // update bodies (rebuilt, so they carry no proof row).
        Bodies::Register(reinterpret_cast<uintptr_t>(&NewfilesRegisterBody), 0x10051600u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&NewfilesUpdateABody), 0x10051890u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&NewfilesUpdateBBody), 0x100518D0u);

        // NewSavesCompatibility's trampoline (transcribed), the two steps behind it
        // and the two read hooks.
        Bodies::Register(reinterpret_cast<uintptr_t>(&NewSavesTrampoline), 0x100521E0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&NewSavesPrepare), 0x10051FD0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&NewSavesConvert), 0x10051B30u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&NewSavesReadHook), 0x10051A90u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&NewSavesSecondLoadHook), 0x10051AD0u);
    }
}

// The savegame-load converter's walk, its helpers and its test-only seam.

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
    // this half cannot be driven from a synthetic image either.
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

export namespace Compatibility
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
        rejected,  // the hardening group: a short read or a size below nine
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

    // The `dlc_path` group's bound: the discovery body's path buffer is 0x104

}

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
            const uint32_t originalTarget = static_cast<uint32_t>(
                injector::ReadRelativeOffset(site + kNewSavesCallOperand).as_int());

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
            const uint32_t originalTarget = static_cast<uint32_t>(
                injector::ReadRelativeOffset(site + kNewSavesCallOperand).as_int());

            injector::MakeCALL(reinterpret_cast<void*>(site + kNewSavesCallSite),
                reinterpret_cast<void*>(&NewSavesSecondLoadHook), true);
            if (zpatch_new_saves_p3_original == 0)
                zpatch_new_saves_p3_original = originalTarget;

            zpatch_new_saves_loaddata = static_cast<uint32_t>(
                injector::ReadRelativeOffset(site + kNewSavesLoadOperand).as_int());
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
    Compatibility::SaveWalkEnd NewSavesWalkFile(const char* path, Compatibility::SaveWalkDecision& walk)
    {
        std::FILE* stream = nullptr;
        if (fopen_s(&stream, path, "rb") != 0 || stream == nullptr)
            return Compatibility::SaveWalkEnd::noFile;

        uint8_t header[kNewSavesHeaderSize];
        {
            // A short header is not a save this build can read.
            if (std::fread(header, kNewSavesHeaderSize, 1, stream) != 1)
            {
                std::fclose(stream);
                return Compatibility::SaveWalkEnd::rejected;
            }
        }

        uint32_t magic = 0;
        std::memcpy(&magic, header + kNewSavesMagicOffset, sizeof(magic));
        if (magic != kNewSavesMagic)
        {
            std::fclose(stream);
            return Compatibility::SaveWalkEnd::notASave;
        }

        // One stack header for all eighteen reads: a short read leaves the previous
        // iteration's bytes in it, which is what the original's reused slot does.
        uint8_t chunkHeader[kNewSavesChunkHeaderSize];
        for (std::size_t index = 0; index < kNewSavesChunkCount; ++index)
        {
            if (std::fread(chunkHeader, kNewSavesChunkHeaderSize, 1, stream) != 1)
            {
                std::fclose(stream);
                return Compatibility::SaveWalkEnd::rejected;
            }

            uint32_t size = 0;
            std::memcpy(&size, chunkHeader + kNewSavesChunkSizeOffset, sizeof(size));

            if (size < static_cast<uint32_t>(kNewSavesChunkHeaderSize))
            {
                std::fclose(stream);
                return Compatibility::SaveWalkEnd::rejected;
            }

            const uint32_t payload = size - static_cast<uint32_t>(kNewSavesChunkHeaderSize);
            // The scan only needs the headers; seek over the payload. A failed
            // seek means the file is shorter than its own chunk table.
            if (payload > static_cast<uint32_t>(std::numeric_limits<long>::max()) ||
                std::fseek(stream, static_cast<long>(payload), SEEK_CUR) != 0)
            {
                std::fclose(stream);
                return Compatibility::SaveWalkEnd::rejected;
            }

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

        return Compatibility::SaveWalkEnd::walked;
    }

    // sub_10051B30, recovered VA 0x10051B30: the converter. The call sequence is
    // the original's, faults included: an unchecked header read, an unchecked
    // `operator new` per chunk (freed with `free`, exactly as the original mixes
    // the two), an unchecked payload read, and the patches only after the file is
    // closed. A chunk whose size is below nine underflows the payload, which makes
    // the allocation throw, as the original's does.
    void NewSavesConvert(const std::string& path)
    {
        Compatibility::SaveWalkDecision walk;
        if (NewSavesWalkFile(path.c_str(), walk) != Compatibility::SaveWalkEnd::walked)
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

// The test seam: the walk, plus the three values the patch step would write, which
// are a pure function of the two flags that decide them. Nothing is patched, no hook
// is installed, and the converter's published state is untouched.
Compatibility::SaveWalkDecision Compatibility::WalkSaveFile(const char* path)
{
    Compatibility::SaveWalkDecision decision;
    decision.end = NewSavesWalkFile(path, decision);

    const NewSavesPatchValues values =
        NewSavesChoosePatch(decision.forceRead, decision.newChunk16);
    decision.branchOpcode = static_cast<uint8_t>(values.branchOpcode);
    decision.readSize = values.readSize;
    decision.readBranch = values.readBranch;
    return decision;
}

namespace Compatibility
{

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
            zpatch_newfiles_register_slot = static_cast<uint32_t>(
                injector::ReadRelativeOffset(match + 6).as_int());
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

    void InstallGFWLProtectionDisabler()
    {
        if (!OpenOptionEnabled("GFWLProtectionDisabler", "1"))
            return;

        const HMODULE xlive = GetModuleHandleA("xlive.dll");
        if (xlive == nullptr)
            return;

        // xlive.dll is also the common filename for Ultimate ASI Loader's proxy.
        // Only a real Games for Windows LIVE image has this export; patching the
        // UAL proxy's image at the GFWL RVA would corrupt the loader itself.
        if (GetProcAddress(xlive, "XLiveInitialize") == nullptr)
        {
            Log::Print("gfwl: xlive.dll is not Games for Windows LIVE; skipped");
            return;
        }

        constexpr size_t kSiteOffset = 988677;  // 0xF1605
        if (!ModuleImageContains(xlive, kSiteOffset, 1))
        {
            Log::Print("gfwl: xlive.dll+0x{:X} is not readable; skipped", kSiteOffset);
            return;
        }

        auto* const site = reinterpret_cast<std::byte*>(xlive) + kSiteOffset;
        const std::byte opcode = *site;
        if (opcode == std::byte{ 0x74 } || opcode == std::byte{ 0xEB })
            injector::WriteMemory<std::byte>(site, std::byte{ 0xEB }, true);
    }

    // The Steam mirror lives at the GFWL import boundary; the option read and
    // the group gate stay here with the other compatibility keys.
    void InstallSteamAchievements()
    {
        if (OpenOptionEnabled("SteamAchievements", "1"))
            (void)Steam::Install();
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

namespace
{
    struct Achievement
    {
        uint32_t id;
        const char* name;
    };

    // GTA IV / EFLC achievement ids and their Steam API names: ids 1..40 are the
    // base game's single-player achievements, 41..50 the multiplayer ones,
    // 51..55 TLAD and 56..65 TBoGT (the script ACHIEVEMENT_IDS enum and the
    // standalone SteamAchievements.asi agree).
    constexpr Achievement kAchievements[] = {
        { 1, "THETOUR" }, { 2, "SCOREDARTGAME" }, { 3, "POOLWIN" }, { 4, "QUBBEAT" },
        { 5, "MELEECOUNTER" }, { 6, "STREETRACE" }, { 7, "BIKEWHEELIE" }, { 8, "TURKEY" },
        { 9, "SATAXI" }, { 10, "CARROLLS" }, { 11, "LOSERATING" }, { 12, "DRUGDONE" },
        { 13, "SAGUNCAR" }, { 14, "CHAINREACTION" }, { 15, "WANTEDLEVEL6STARSURVIVAL" },
        { 16, "KEYMISSION1" }, { 17, "IMPORTDONE" }, { 18, "WANTEDDONE" },
        { 19, "VIGILDONE" }, { 20, "KEYMISSION0" }, { 21, "TAXIPASSANGER" },
        { 22, "ALLHELITOURTAKEN" }, { 23, "DATING" }, { 24, "SAHELI" },
        { 25, "HALFMILLION" }, { 26, "KEYMISSION2" }, { 27, "OPENEDALDERNEY" },
        { 28, "SMSTHEFTDONE" }, { 29, "USJDONE" }, { 30, "ASSASSIN" },
        { 31, "HIDDENPACKAGES" }, { 32, "UNDERALLBRIDGES" }, { 33, "SABOMBCAR" },
        { 34, "KEYMISSION3" }, { 35, "FRIENDSLIKEYOU" }, { 36, "METRANDOM" },
        { 37, "KEYMISSION4" }, { 38, "STORYDONE" }, { 39, "STREETLIGHTSDOWN" },
        { 40, "GAMECOMPLETION100" }, { 41, "MPTEAMWIN1" }, { 42, "MPCOMPWIN1" },
        { 43, "MPRACEWIN1" }, { 44, "MPCOOPWIN1" }, { 45, "MPTEAMWINALL" },
        { 46, "MPCOMPWINALL" }, { 47, "MPRACEWINALL" }, { 48, "MPSTARRATING" },
        { 49, "MPCOOPWINALL" }, { 50, "VIRALKILLME" }, { 51, "E1INTRO" },
        { 52, "E1BECOMELEADER" }, { 53, "E1FINISHSTORY" }, { 54, "E1KNOCKOFF" },
        { 55, "E1BUDDYSTAT" }, { 56, "E2BASEJUMP" }, { 57, "E2TRINITY" },
        { 58, "E2GOLF" }, { 59, "E2FIGHTC" }, { 60, "E2DANCE" }, { 61, "E2DRUGS" },
        { 62, "E2FREEFALL" }, { 63, "E2STORY" }, { 64, "E2FAIRFLAIR" },
        { 65, "E2ALLFLAIR" },
    };

    constexpr uint32_t kMaxAchievementId = 65;
    constexpr uint32_t kMaxMirrorBatch = 16;
    constexpr WORD kWriteAchievementsOrdinal = 5278;

    struct XUserAchievement
    {
        uint32_t userIndex;
        uint32_t achievementId;
    };

    using WriteAchievementsFn = uint32_t(__stdcall*)(uint32_t, const XUserAchievement*, void*);
    using SetAchievementFn = bool(__thiscall*)(void*, const char*);
    using StoreStatsFn = bool(__thiscall*)(void*);
    using RequestStatsFn = bool(__thiscall*)(void*);
    using RunCallbacksFn = void(__cdecl*)();

    WriteAchievementsFn g_original = nullptr;
    SetAchievementFn g_set_achievement = nullptr;
    StoreStatsFn g_store_stats = nullptr;
    RequestStatsFn g_request_stats = nullptr;
    RunCallbacksFn g_run_callbacks = nullptr;
    void* g_user_stats = nullptr;

    std::mutex g_mutex;
    std::mutex g_init_mutex;
    bool g_hook_installed = false;
    std::atomic<bool> g_disabled{ false };
    std::atomic<bool> g_ready{ false };
    bool g_pending[kMaxAchievementId + 1] = {};
    bool g_sent[kMaxAchievementId + 1] = {};

    const Achievement* AchievementFor(uint32_t id) noexcept
    {
        for (const auto& achievement : kAchievements)
        {
            if (achievement.id == id)
                return &achievement;
        }
        return nullptr;
    }

    bool EpisodesExecutable() noexcept
    {
        char module[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, module, MAX_PATH) == 0)
            return false;
        const char* name = std::strrchr(module, '\\');
        name = name != nullptr ? name + 1 : module;
        return _strnicmp(name, "EFLC", 4) == 0;
    }

    bool SteamApiPresent() noexcept
    {
        char module[MAX_PATH] = {};
        if (GetModuleFileNameA(nullptr, module, MAX_PATH) == 0)
            return false;
        char* slash = std::strrchr(module, '\\');
        if (slash == nullptr)
            return false;
        slash[1] = '\0';
        char path[MAX_PATH] = {};
        std::snprintf(path, sizeof(path), "%ssteam_api.dll", module);
        return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    }

    // The packed system xlive's ordinal-5278 export uses the caller's return
    // address in its anti-tamper check; an IAT hook changes it. Only the plain
    // local xliveless build is a supported target.
    bool ProtectedSystemXlive() noexcept
    {
        const HMODULE xlive = GetModuleHandleA("xlive.dll");
        if (xlive == nullptr)
            return false;
        char path[MAX_PATH] = {};
        if (GetModuleFileNameA(xlive, path, MAX_PATH) == 0)
            return false;
        char windows[MAX_PATH] = {};
        const UINT length = GetWindowsDirectoryA(windows, MAX_PATH);
        if (length == 0 || length >= MAX_PATH)
            return false;
        if (_strnicmp(path, windows, length) != 0)
            return false;
        return path[length] == '\\' || path[length] == '\0';
    }

    void PrepareAppId() noexcept
    {
        char inherited[32] = {};
        const DWORD length = GetEnvironmentVariableA("SteamAppId", inherited, sizeof(inherited));
        if (length != 0)
            return;
        const char* app = EpisodesExecutable() ? "12220" : "12210";
        SetEnvironmentVariableA("SteamAppId", app);
        SetEnvironmentVariableA("SteamGameId", app);
    }

    bool ReplaceImportOrdinal(HMODULE module, const char* library, WORD ordinal,
        void* replacement, void** original) noexcept
    {
        const auto* base = reinterpret_cast<const uint8_t*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0)
            return false;

        auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
            const_cast<uint8_t*>(base) + directory.VirtualAddress);
        for (; descriptor->Name != 0; ++descriptor)
        {
            if (_stricmp(reinterpret_cast<const char*>(base + descriptor->Name), library) != 0)
                continue;

            auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
                const_cast<uint8_t*>(base) + descriptor->OriginalFirstThunk);
            auto* slot = reinterpret_cast<IMAGE_THUNK_DATA*>(
                const_cast<uint8_t*>(base) + descriptor->FirstThunk);
            if (descriptor->OriginalFirstThunk == 0)
                thunk = slot;
            for (; thunk->u1.AddressOfData != 0; ++thunk, ++slot)
            {
                if (!IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal) ||
                    IMAGE_ORDINAL(thunk->u1.Ordinal) != ordinal)
                    continue;

                DWORD previous = 0;
                if (!VirtualProtect(&slot->u1.Function, sizeof(void*), PAGE_READWRITE, &previous))
                    return false;
                if (original != nullptr)
                    *original = reinterpret_cast<void*>(slot->u1.Function);
                slot->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
                VirtualProtect(&slot->u1.Function, sizeof(void*), previous, &previous);
                return true;
            }
        }
        return false;
    }

    bool TryInitialise() noexcept
    {
        std::lock_guard initLock(g_init_mutex);
        if (g_ready.load(std::memory_order_acquire))
            return true;
        if (g_disabled.load(std::memory_order_relaxed))
            return false;

        PrepareAppId();

        const HMODULE api = LoadLibraryA("steam_api.dll");
        if (api == nullptr)
        {
            g_disabled = true;
            return false;
        }

        using InitFn = bool(__cdecl*)();
        using GetUserFn = int(__cdecl*)();
        using FindInterfaceFn = void*(__cdecl*)(int, const char*);
        using IsRunningFn = bool(__cdecl*)();

        const auto init = reinterpret_cast<InitFn>(GetProcAddress(api, "SteamAPI_Init"));
        const auto get_user = reinterpret_cast<GetUserFn>(GetProcAddress(api, "SteamAPI_GetHSteamUser"));
        const auto find_interface = reinterpret_cast<FindInterfaceFn>(
            GetProcAddress(api, "SteamInternal_FindOrCreateUserInterface"));
        const auto is_running = reinterpret_cast<IsRunningFn>(GetProcAddress(api, "SteamAPI_IsSteamRunning"));
        g_run_callbacks = reinterpret_cast<RunCallbacksFn>(GetProcAddress(api, "SteamAPI_RunCallbacks"));
        if (init == nullptr || get_user == nullptr || find_interface == nullptr)
        {
            g_disabled = true;
            return false;
        }
        if (is_running != nullptr && !is_running())
            return false;
        if (!init())
            return false;

        g_user_stats = find_interface(get_user(), "STEAMUSERSTATS_INTERFACE_VERSION012");
        if (g_user_stats == nullptr)
        {
            g_disabled = true;
            return false;
        }
        const void* const* vtable = *reinterpret_cast<const void* const* const*>(g_user_stats);
        if (vtable == nullptr)
        {
            g_disabled = true;
            return false;
        }
        g_request_stats = reinterpret_cast<RequestStatsFn>(vtable[0]);
        g_set_achievement = reinterpret_cast<SetAchievementFn>(vtable[7]);
        g_store_stats = reinterpret_cast<StoreStatsFn>(vtable[10]);
        if (g_set_achievement == nullptr || g_store_stats == nullptr)
        {
            g_disabled = true;
            return false;
        }

        if (g_request_stats != nullptr)
            (void)g_request_stats(g_user_stats);
        for (int i = 0; i < 10 && g_run_callbacks != nullptr; ++i)
        {
            g_run_callbacks();
            Sleep(50);
        }
        g_ready.store(true, std::memory_order_release);
        return true;
    }

    void Apply(uint32_t id, const char* name) noexcept
    {
        if (g_set_achievement == nullptr)
            return;
        const bool set = g_set_achievement(g_user_stats, name);
        if (set && g_store_stats != nullptr)
            g_store_stats(g_user_stats);
        if (g_run_callbacks != nullptr)
            g_run_callbacks();
        Log::Print("steam: achievement {} ({}) {}", id, name,
            set ? "unlocked" : "rejected by Steam");
    }

    void Observe(uint32_t id) noexcept
    {
        const Achievement* achievement = AchievementFor(id);
        if (achievement == nullptr)
        {
            Log::Print("steam: achievement id {} has no Steam name, ignored", id);
            return;
        }

        bool need_init = false;
        {
            std::lock_guard lock(g_mutex);
            if (g_sent[id])
                return;
            g_pending[id] = true;
            need_init = !g_ready.load(std::memory_order_acquire);
        }

        if (need_init)
        {
            // Initialisation is deliberately outside the state mutex: it loads
            // steam_api.dll and pumps callbacks, and a callback can reach this
            // hook on the same thread.
            (void)TryInitialise();
            std::lock_guard lock(g_mutex);
            if (!g_ready.load(std::memory_order_acquire))
            {
                Log::Print("steam: achievement {} ({}) queued until Steam is ready", id,
                    achievement->name);
                return;
            }
        }

        uint32_t send[kMaxAchievementId + 1] = {};
        size_t send_count = 0;
        {
            std::lock_guard lock(g_mutex);
            if (!g_ready.load(std::memory_order_acquire))
                return;
            for (uint32_t queued = 1; queued <= kMaxAchievementId; ++queued)
            {
                if (g_pending[queued] && !g_sent[queued])
                {
                    g_sent[queued] = true;
                    g_pending[queued] = false;
                    send[send_count++] = queued;
                }
            }
        }

        // The Steam calls run without the state mutex; the function pointers are
        // fixed once g_ready was published.
        for (size_t index = 0; index < send_count; ++index)
        {
            const Achievement* queued = AchievementFor(send[index]);
            if (queued != nullptr)
                Apply(send[index], queued->name);
        }
    }

    extern "C" uint32_t __stdcall WriteAchievementsHook(
        uint32_t count, const XUserAchievement* achievements, void* overlapped) noexcept
    {
        const uint32_t result =
            g_original != nullptr ? g_original(count, achievements, overlapped) : 0;

        if (achievements != nullptr && count != 0)
        {
            const uint32_t mirror = count > kMaxMirrorBatch ? kMaxMirrorBatch : count;
            for (uint32_t index = 0; index < mirror; ++index)
                Observe(achievements[index].achievementId);
        }
        return result;
    }
}

namespace Steam
{

    bool Install() noexcept
    {
        if (g_hook_installed)
            return true;
        if (g_disabled)
            return false;

        // The offline dump maps a copy of the image; only the real game process
        // owns the import table this hook rewrites.
        if (reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) != Game::Get().ImageBase())
            return false;
        if (!SteamApiPresent())
        {
            g_disabled = true;
            Log::Print("steam: no steam_api.dll, skipped");
            return false;
        }
        if (ProtectedSystemXlive())
        {
            g_disabled = true;
            Log::Print("steam: packed system xlive.dll, IAT hook skipped");
            return false;
        }

        void* original = nullptr;
        if (!ReplaceImportOrdinal(GetModuleHandleW(nullptr), "xlive.dll",
                kWriteAchievementsOrdinal, reinterpret_cast<void*>(&WriteAchievementsHook),
                &original))
        {
            g_disabled = true;
            Log::Print("steam: XUserWriteAchievements ordinal {} not imported, skipped",
                kWriteAchievementsOrdinal);
            return false;
        }
        g_original = reinterpret_cast<WriteAchievementsFn>(original);
        g_hook_installed = true;
        Log::Print("steam: hooked xlive.dll ordinal {}", kWriteAchievementsOrdinal);
        return true;
    }

    const char* AchievementNameForTest(uint32_t id) noexcept
    {
        const Achievement* achievement = AchievementFor(id);
        return achievement != nullptr ? achievement->name : nullptr;
    }

}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

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
// the original's, so this replaces the C++ rebuild the tree carried.
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
// installer republished. Transcribed, so the C++ rebuild is gone.
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
