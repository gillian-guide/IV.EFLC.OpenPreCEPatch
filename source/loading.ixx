// SPDX-License-Identifier: GPL-3.0-or-later
//
// loading
//
// One function per INI arm, in the original loader order; the bodies are the
// readable reconstruction's verified arms, the wrapper and the arm trace ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#include "opcodes.hxx"

#include <new>
#include <memory>
#include <optional>
#include <span>
#include <shlwapi.h>
export module opp.loading;

import opp.pipeline;
import opp.patch;
import opp.game;
import opp.common;
import opp.settings;
import opp.log;

// Hook bodies: the machine-level entry points loading installs. Each keeps the original
// site's calling convention and register contract (its comment names the destination VA);
// 32-bit only - the x64 parity dump resolves names to the original VAs.
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

// ---------------------------------------------------------------------------
// NewSavesCompatibility's two LoadData hooks
//
// Both are compiled C reconstructions of functions the original ASI keeps at
// 0x10051A90 (read hook) and 0x10051AD0 (p3 hook); neither carries an @exact
// contract. Their contract is the LoadData ABI they wrap: cdecl, a buffer and a
// byte count, returning LoadData's own result.
// ---------------------------------------------------------------------------
using LoadFn = int32_t(__cdecl*)(void*, uint32_t);
using OrigFn = int32_t(__cdecl*)();

namespace
{
// The game's LoadData entry the P2/P3 sites call (original dword_1015AD94) and
// the handler the p3 hook wraps (original dword_1015AD8C). Both are captured by
// the install arm, so they are 32-bit VAs behind rebased globals, not typed
// pointers; GameFunction is the one conversion back to a callable.
LoadFn LoadData() noexcept
{
    return GameFunction<LoadFn>(static_cast<uintptr_t>(zpatch_new_saves_loaddata));
}
OrigFn OriginalP3Handler() noexcept
{
    return GameFunction<OrigFn>(static_cast<uintptr_t>(zpatch_new_saves_orig_handler));
}
}  // namespace

// The fixed stack scratch of the original read hook (sub_10051A90): its frame is
// `sub esp, 0xE7C` with a 3704-byte (0xE78) buffer and the /GS cookie. 3704 is
// the original's literal, and the largest request the P2 site passes is the
// new-format read (kNewSaveReadBytes = 3694), which fits. `size` is passed
// through to LoadData; the buffer is never sized from it.
constexpr size_t kSaveScratchSize = 3704u;
// The original p3 hook (sub_10051AD0) keeps a fixed 12-byte local and reads the
// new-format block into it before and after the wrapped handler; the block is a
// sink (its contents are never observed).
constexpr size_t kSaveBufferSize = 12u;
// The two sizes the P2 install writes into its `push imm32`: an old-format save
// reads 3262 bytes, a new-format one 3694.
constexpr uint32_t kOldSaveReadBytes = 3262u;
constexpr uint32_t kNewSaveReadBytes = 3694u;

extern "C"
{
// The P2 LoadData replacement (original sub_10051A90): load into the fixed
// scratch instead of the game's destination and return LoadData's result. The
// original returns the full EAX; this rebuild declares uint8_t, so callers see
// AL exactly as they did before.
extern "C" uint8_t abi_new_saves_read_hook(void*, uint32_t size) noexcept
{
    std::array<uint8_t, kSaveScratchSize> scratch;
    if (zpatch_new_saves_loaddata == 0) return 0;
    return static_cast<uint8_t>(LoadData()(scratch.data(), size));
}

// The P3 LoadData replacement (original sub_10051AD0): for a new-format save
// (chunk 13 has the new size, original byte_1015AD99) it consumes the 12-byte
// block, runs the original handler, then consumes a second 12-byte block and
// returns that read's result. The pre/post reads are the original's own
// behaviour, not a duplicate introduced by this rebuild.
extern "C" int32_t abi_new_saves_p3_hook() noexcept
{
    std::array<uint8_t, kSaveBufferSize> buffer;
    const LoadFn load = LoadData();
    if (zpatch_new_saves_chunk13_new != 0 && load)
        static_cast<void>(load(buffer.data(), kSaveBufferSize));
    const int32_t result = OriginalP3Handler()();
    if (zpatch_new_saves_chunk13_new != 0 && load)
        return load(buffer.data(), kSaveBufferSize);
    return result;
}

}  // extern "C"

// ---------------------------------------------------------------------------
// The save-format scanner: the converter reads the game's save through a cursor, so the
// byte-level steps have names (the 0x110-byte header, the 9-byte chunk headers, the
// per-chunk payload step, the 18-chunk walk). The game hands the scan a std::FILE*, so the
// cursor wraps it rather than replacing it with an ifstream - the fread/fseek conventions
// and the unchecked-malloc fault are the original's.
//
// The three new-format probes are its only output: chunk 13 gates the p3 hook, chunk 16
// selects the 3694-byte P2 read, chunk 9 gates P1.
namespace
{
// True when a scan validates every read/size and seeks over payloads (the
// converter's shipped path). All-false is the original loop: every fread result
// is ignored, the size underflow passes and the payload malloc is unchecked. The
// pair is a named policy so a caller cannot swap the two booleans' meaning.
constexpr opp::SaveScanPolicy kConverterScan{true, true};
constexpr opp::SaveScanPolicy kOriginalScan{false, false};

// The save header is 0x110 bytes; the format magic "SAVE" sits at +0xC and each
// of the 18 chunks starts with a 9-byte header whose dword at +5 is the chunk's
// total size.
constexpr size_t kSaveHeaderSize = 0x110;
constexpr size_t kSaveChunkHeaderSize = 9;
constexpr size_t kSaveMagicOffset = 0x0C;
constexpr size_t kSaveChunkSizeOffset = 5;
constexpr uint32_t kSaveMagic = 0x45564153u;  // "SAVE"
constexpr size_t kSaveChunkCount = 18;
// The three chunks whose size tells the formats apart (the original's recovered
// indices); each size constant is the new format's chunk total.
constexpr size_t kChunk9Index = 9;
constexpr size_t kChunk13Index = 13;
constexpr size_t kChunk16Index = 16;
constexpr uint32_t kNewFormatChunk9Size = 19525u;
constexpr uint32_t kNewFormatChunk13Size = 5953u;
constexpr uint32_t kNewFormatChunk16Size = 3703u;
// fseek takes a long; the element count is only seekable when it fits one.
constexpr uint32_t kSeekCountLimit = 0x7FFFFFFFu;

// The game-owned std::FILE*: one named operation per original libc call.
class FileCursor final
{
public:
    explicit FileCursor(std::FILE* file) noexcept : file_(file) {}

    [[nodiscard]] bool Valid() const noexcept { return file_ != nullptr; }

    // fread(item, 1): the header/chunk read, which asks for one whole item. Only
    // the unchecked policy ignores the answer.
    [[nodiscard]] bool ReadItem(std::span<std::byte> item) noexcept
    {
        return item.empty() || std::fread(item.data(), item.size(), 1, file_) == 1;
    }

    // fread(ptr, 1, n): the payload read, whose partial result the unchecked
    // policy ignores.
    [[nodiscard]] size_t ReadBytes(std::span<std::byte> bytes) noexcept
    {
        return std::fread(bytes.data(), 1, bytes.size(), file_);
    }

    [[nodiscard]] bool Seek(long offset) noexcept
    {
        return std::fseek(file_, offset, SEEK_CUR) == 0;
    }

    // The raw handle, used only for the original's null-payload fault path.
    [[nodiscard]] std::FILE* Handle() const noexcept { return file_; }

private:
    std::FILE* file_;
};

// Reads a little-endian dword out of a byte buffer (the save file's own order).
[[nodiscard]] uint32_t LoadDword(const std::byte* bytes) noexcept
{
    uint32_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}

// Reads the 0x110-byte header and checks the "SAVE" magic. The unchecked policy
// ignores a short read and still inspects the buffer, exactly like the original.
[[nodiscard]] bool ReadSaveHeader(FileCursor& file, opp::SaveScanPolicy policy) noexcept
{
    std::array<std::byte, kSaveHeaderSize> header{};
    const bool read = file.ReadItem(header);
    if (policy.validate && !read)
        return false;
    return LoadDword(header.data() + kSaveMagicOffset) == kSaveMagic;
}

// Reads one 9-byte chunk header and returns its total size. A size below the
// header is the original's allocation-count underflow; only the validating
// policy rejects it.
[[nodiscard]] std::optional<uint32_t> ReadChunkHeader(FileCursor& file,
                                                      opp::SaveScanPolicy policy) noexcept
{
    std::array<std::byte, kSaveChunkHeaderSize> header{};
    const bool read = file.ReadItem(header);
    if (policy.validate && !read)
        return std::nullopt;
    const uint32_t size = LoadDword(header.data() + kSaveChunkSizeOffset);
    if (policy.validate && size < kSaveChunkHeaderSize)
        return std::nullopt;
    return size;
}

// The per-chunk payload step: seek over the payload when asked (the 9-byte
// headers are all the scan needs), skip a header-only chunk, or read it into the
// original's unchecked malloc buffer. The buffer frees through std::free; on
// allocation failure the validating policy stops and the original policy passes
// the null pointer to fread, which is the fault the 1:1 path keeps.
[[nodiscard]] bool SkipPayload(FileCursor& file, uint32_t chunk_size,
                               opp::SaveScanPolicy policy) noexcept
{
    const uint32_t element_count = chunk_size - kSaveChunkHeaderSize;
    if (policy.skip_payload && chunk_size >= kSaveChunkHeaderSize &&
        element_count <= kSeekCountLimit)
    {
        if (file.Seek(static_cast<long>(element_count)))
            return true;
    }
    if (element_count == 0u && (policy.validate || policy.skip_payload))
        return true;  // a header-only chunk: malloc(0) == nullptr is not a failure

    void* const raw = std::malloc(element_count);
    const std::unique_ptr<void, opp::FreeDeleter> payload(raw);
    if (policy.validate && raw == nullptr)
        return false;
    if (raw == nullptr)
        return std::fread(raw, 1, element_count, file.Handle()) == element_count;
    const auto bytes = std::span<std::byte>(static_cast<std::byte*>(raw), element_count);
    const size_t read = file.ReadBytes(bytes);
    return !policy.validate || read == element_count;
}

// Walks the 18 chunks in file order, publishing the chunk-length/element-count
// globals on each iteration (the original's converter state) and recording the
// three format probes.
[[nodiscard]] std::optional<opp::SaveChunkProbes> ScanChunks(
    FileCursor& file, opp::SaveScanPolicy policy) noexcept
{
    opp::SaveChunkProbes probes{};
    for (size_t index = 0; index < kSaveChunkCount; ++index)
    {
        const std::optional<uint32_t> size = ReadChunkHeader(file, policy);
        if (!size)
            return std::nullopt;
        zpatch_new_saves_chunk_len = *size;                             // dword_1015AD90
        zpatch_new_saves_element_count = *size - kSaveChunkHeaderSize;  // dword_1015ADA0
        if (!SkipPayload(file, *size, policy))
            return std::nullopt;
        if (index == kChunk9Index)
            probes.chunk9_new = *size == kNewFormatChunk9Size;
        if (index == kChunk16Index)
            probes.chunk16_new = *size == kNewFormatChunk16Size;
        if (index == kChunk13Index)
            probes.chunk13_new = *size == kNewFormatChunk13Size;
    }
    return probes;
}

// The whole scan: header, then the 18 chunks.
[[nodiscard]] std::optional<opp::SaveChunkProbes> ScanSaveFile(
    FileCursor& file, opp::SaveScanPolicy policy) noexcept
{
    if (!file.Valid())
        return std::nullopt;
    if (!ReadSaveHeader(file, policy))
        return std::nullopt;
    return ScanChunks(file, policy);
}
}  // namespace

// Native-test/inspection seam: one typed result instead of three out-pointers.
// The game-facing hook bodies above keep their ABI; only this test entry (and
// hook.h's declaration) changed.
extern "C" opp::SaveScanResult zpatch_new_saves_scan(
    void* handle, opp::SaveScanPolicy policy) noexcept
{
    FileCursor file(static_cast<std::FILE*>(handle));
    if (const std::optional<opp::SaveChunkProbes> probes = ScanSaveFile(file, policy))
        return {true, *probes};
    return {};
}

extern "C"
{

// The NewSavesCompatibility converter (original sub_10051B30): parse the save's
// 0x110-byte header (magic "SAVE" at +0xC), walk 18 chunks of 9-byte header +
// payload, record the new-format chunk sizes, then patch three game sites.
extern "C" void abi_new_saves_convert(const char* path) noexcept
{
    const std::unique_ptr<std::FILE, opp::FileCloser> file(std::fopen(path, "rb"));
    if (!file)
        return;
    FileCursor cursor(file.get());
    const std::optional<SaveChunkProbes> probes = ScanSaveFile(cursor, kConverterScan);
    if (!probes)
        return;  // including the original wrong-magic early return
    zpatch_new_saves_chunk9_new = probes->chunk9_new ? 1u : 0u;
    zpatch_new_saves_chunk16_new = probes->chunk16_new ? 1u : 0u;
    zpatch_new_saves_chunk13_new = probes->chunk13_new ? 1u : 0u;

    // P1 (game 0x872185): force the 0x3C-byte read path for new saves.
    static const uint8_t* p1 = nullptr;
    if (p1 == nullptr) p1 = scan_game("74 37 80 3D ? ? ? ? 00 75 2E 8D 84 ? ? ? ? 00 6A 1E");
    if (p1 != nullptr)
        wp_byte(const_cast<uint8_t*>(p1),
                zpatch_new_saves_chunk9_new != 0 ? opp::ops::kJmpRel8 : opp::ops::kJzRel8);

    // P2 (game 0x7D8C50): route the oversized read through the plugin's scratch
    // buffer, resize the read, and force/NOP the branch per the save format.
    static const uint8_t* p2 = nullptr;
    if (p2 == nullptr) p2 = scan_game("68 BE 0C 00 00 50 E8");
    if (p2 != nullptr)
    {
        // The E8 call sits at site+6 with its rel32 operand at site+7.
        constexpr ptrdiff_t kLoadDataCallOffset = 6;
        constexpr ptrdiff_t kBranchOperandOffset = 7;
        constexpr ptrdiff_t kLoadSizeOperandOffset = 1;
        constexpr ptrdiff_t kSkipLoadBranchOffset = -5;
        auto* site = const_cast<uint8_t*>(p2);
        const uint32_t original_target = BranchDestination(site, kBranchOperandOffset);
        // The call already is E8+rel32; MakeCALL writes the same five bytes and
        // keeps the original's read of the old destination first.
        injector::MakeCALL(injector::raw_ptr(site + kLoadDataCallOffset),
                           injector::raw_ptr(&abi_new_saves_read_hook), true);
        if (zpatch_new_saves_loaddata == 0)
            zpatch_new_saves_loaddata = original_target;

        // The `push imm32` at P2+1 carries the load size: the old format reads
        // 3262 bytes, the new one 3694.
        wp_dword(site + kLoadSizeOperandOffset,
                 zpatch_new_saves_chunk16_new != 0 ? kNewSaveReadBytes
                                                   : kOldSaveReadBytes);

        // The `jnz rel8` five bytes before the call skips the load; for a new
        // save it is a two-byte NOP instead (75 26 = jnz +0x26).
        constexpr opp::ops::InstructionWindow<2> kSkipLoadBranch{{0x75, 0x26}};
        static_assert(opp::ops::PackedLowWord(kSkipLoadBranch) == 0x2675u);
        if (zpatch_new_saves_chunk16_new != 0)
            injector::MakeNOP(injector::raw_ptr(site + kSkipLoadBranchOffset),
                              kSkipLoadBranch.Size(), true);
        else
            wp_word(site + kSkipLoadBranchOffset, opp::ops::PackedLowWord(kSkipLoadBranch));
    }

    // P3 (game 0x8DA090): route the second LoadData call through the plugin body
    // that consumes the extra 12-byte block for new saves, and repoint the
    // shared LoadData slot at the function's own later call target.
    static const uint8_t* p3 = nullptr;
    if (p3 == nullptr) p3 = scan_game("56 E8 ? ? ? ? E8 ? ? ? ? A1 ? ? ? ? 8D 0C 85 00 00 00 00");
    if (p3 != nullptr)
    {
        // The replaced call is the second E8 (site+6, rel32 operand at site+7);
        // the third call's operand at site+30 names the shared LoadData slot
        // that P2's captured value is repointed to.
        constexpr ptrdiff_t kLoadDataCallOffset = 6;
        constexpr ptrdiff_t kBranchOperandOffset = 7;
        constexpr ptrdiff_t kLaterCallOperandOffset = 30;
        auto* site = const_cast<uint8_t*>(p3);
        const uint32_t original_target = BranchDestination(site, kBranchOperandOffset);
        injector::MakeCALL(injector::raw_ptr(site + kLoadDataCallOffset),
                           injector::raw_ptr(&abi_new_saves_p3_hook), true);
        if (zpatch_new_saves_orig_handler == 0)
            zpatch_new_saves_orig_handler = original_target;
        zpatch_new_saves_loaddata = BranchDestination(site, kLaterCallOperandOffset);
    }
}

// The prepare step of the tramp (original sub_10051FD0): if the handler
// captured the game's save-name buffer, resolve the game's save-directory getter
// (scan) and convert "<dir>\\<save name>".
extern "C" void abi_new_saves_prepare() noexcept
{
    // The getter fills `directory` and a trailing byte; 312 and 560 are the
    // original's recovered stack capacities for the directory and the joined
    // directory + save-name path.
    constexpr size_t kSaveDirectoryCapacity = 312u;
    constexpr size_t kSavePathCapacity = 560u;
    using GetSaveDirectoryFn = uint8_t(__cdecl*)(int, char*, char*);
    const auto* name = reinterpret_cast<const char*>(
        static_cast<uintptr_t>(zpatch_new_saves_src));
    if (name == nullptr) return;
    static const uint8_t* get_dir = nullptr;
    if (get_dir == nullptr)
        get_dir = scan_game("55 8B EC 83 E4 F8 83 EC 4C 56 33 C0 8D 4C 24 28");
    if (get_dir == nullptr) return;
    const auto get_directory = GameFunction<GetSaveDirectoryFn>(
        reinterpret_cast<uintptr_t>(get_dir));
    char directory[kSaveDirectoryCapacity]{};
    char suffix = 0;
    if (get_directory(0, directory, &suffix) == 0)
        return;
    char path[kSavePathCapacity];
    std::snprintf(path, sizeof(path), "%s\\%s", directory, name);
    abi_new_saves_convert(path);
}

// The two game functions the DLC discovery routine calls, published as RVAs by
// the LoadDLCs handler (dword_1015804C register, dword_101580D0 apply).
using DlcRegisterFn = void(__stdcall*)(int id, char* request, int flag);
using DlcApplyFn = void(__thiscall*)(void* object, char* request);

// @exact 0x10023480 LoadDLCs: __stdcall, two arguments, mov al,1 / ret 8. The
// original sets only AL, so the upper 24 bits of EAX are preserved and a plain
// `return 1` (mov eax,1) is observable.
static __declspec(naked) int __stdcall abi_load_dlcs_dummy(int, int)
{
    __asm {
        mov al, 1
        ret 8
    }
}

// @exact 0x100521E0 NewSavesCompatibility tramp: the handler E9s the savegame-
// load entry at "89 84 24 E0 01 00 00 8B 45 08 53 33 DB" (game 0x85B093) so this
// runs the converter first, then replicates the overwritten instruction (the
// stack-cookie store) and the next two, and tail-jumps to match+13. Entry is by
// JMP, so the [esp+0x1E0] store targets the same game frame slot as the original.
static __declspec(naked) void abi_new_saves_compat()
{
    __asm {
        pushad
        call abi_new_saves_prepare
        popad
        mov  dword ptr [esp + 1E0h], eax
        mov  eax, dword ptr [ebp + 8]
        push ebx
        xor  ebx, ebx
        jmp  dword ptr [zpatch_new_saves_resume]
    }
}

// NewFilesCompatibility: body 0x10051600 registers a callback with the game's
// file-registration function; bodies 0x10051890 / 0x100518D0 are the per-file
// update hooks (E8 sites) that resolve the asset object, hash the name and
// register it. Helpers mirror sub_100515D0 / sub_100515E0 / sub_10051730 /
// sub_10051610 / sub_100517E0.
extern "C" void* abi_newfiles_cb(void** a1)  // 0x100515D0
{
    void* const result = *a1;
    *reinterpret_cast<int*>(*a1) = 1;
    return result;
}
// 0x100515E0 is five instructions: push callback, push hash, call the
// published register function, add esp,8, ret. The flags at the resume come
// from that add esp,8, so abi_newfiles_register_body keeps the naked stack
// shape around this C helper.
extern "C" void __cdecl abi_newfiles_register()
{
    // The slot is a cdecl two-argument registrar; the original cleans both
    // arguments after the call.
    using RegisterFileFn = void(__cdecl*)(uint32_t, void*);
    const auto register_file =
        GameFunction<RegisterFileFn>(static_cast<uintptr_t>(zpatch_newfiles_register_slot));
    // The script-handler hash the original registers the callback under
    // (sub_10051600's `register_file` argument).
    constexpr uint32_t kNewFilesHandlerHash = 0x2C89A1F9u;
    register_file(kNewFilesHandlerHash, reinterpret_cast<void*>(&abi_newfiles_cb));
}
extern "C" void* abi_newfiles_find_obj()  // 0x10051730
{
    // The original re-dereferences the container global on EVERY call
    // (`mov esi, [match+2]`), so it is picked up again whenever the game
    // rebuilds its registry; caching the container itself left a stale one that
    // faulted in the game's container search (0x43a490).
    constexpr ptrdiff_t kContainerGlobalOperand = 2;
    static uintptr_t match = 0;
    static bool resolved = false;
    if (!resolved)
    {
        resolved = true;
        match = newfiles_find(
            "8B 35 ? ? ? ? 6A 00 50 E8 ? ? ? ? 83 C4 08 50 8B CE E8 ? ? ? ? 85 C0 89 07 5E 74 05");
    }
    if (!match) return nullptr;
    const uint32_t global =
        Field(reinterpret_cast<const uint8_t*>(match), kContainerGlobalOperand);
    return global ? reinterpret_cast<void*>(
                        *reinterpret_cast<const uint32_t*>(reinterpret_cast<const void*>(global)))
                  : nullptr;
}
extern "C" int abi_newfiles_call_obj(void* obj, uint32_t hash)  // 0x10051610
{
    using Fn = int(__fastcall*)(void* self, int, uint32_t);
    static uintptr_t cached = 0;
    static bool resolved = false;
    if (!resolved)
    {
        resolved = true;
        cached = newfiles_find("85 C9 53 55 56 57 74 40 8B 6C 24 14 8D 64 24 00");
        if (!cached) cached = newfiles_find("53 55 8B 6C 24 0C 56 57 EB 06");
    }
    if (!cached) return 0;
    return GameFunction<Fn>(cached)(obj, 0, hash);
}
extern "C" int abi_newfiles_call_add(void* self, void* str)  // 0x100517E0
{
    using Fn = int(__fastcall*)(void* self, int, void*);
    // The pattern matches 20 bytes into the original function; the write body
    // keeps pointing at its entry, which is that many bytes before the match.
    constexpr uintptr_t kFunctionHeadroom = 20u;
    static uintptr_t cached = 0;
    static bool resolved = false;
    if (!resolved)
    {
        resolved = true;
        const uintptr_t m = newfiles_find("8B 0F 85 C9 74 ? 0F B7 41 0A 66 85 C0");
        cached = m ? m - kFunctionHeadroom : 0;
    }
    if (!cached) return 0;
    return GameFunction<Fn>(cached)(self, 0, str);
}
// @exact 0x10051600
static __declspec(naked) void abi_newfiles_register_body()  // 0x10051600
{
    __asm {
        pushad
        call abi_newfiles_register
        popad
        ret
    }
}
// The update object holds the name string at +0x10 (version A) / +0x10F
// (version B) and the callback's first/second arguments at +8 / +4; 0x10051890
// and 0x100518D0 differ only in those offsets.
extern "C" char __fastcall abi_newfiles_update_a(void* self, int)  // 0x10051890
{
    constexpr size_t kUpdateNameOffsetA = 0x10;
    constexpr ptrdiff_t kUpdateArgOffsetA = 8;
    auto* base = static_cast<unsigned char*>(self);
    if (base[kUpdateNameOffsetA] == 0) return 0;
    if (abi_newfiles_call_obj(abi_newfiles_find_obj(),
                              GameHash(reinterpret_cast<const char*>(base + kUpdateNameOffsetA))))
        abi_newfiles_call_add(base + kUpdateArgOffsetA, base + kUpdateNameOffsetA);
    return 1;
}
extern "C" char __fastcall abi_newfiles_update_b(void* self, int)  // 0x100518D0
{
    constexpr size_t kUpdateNameOffsetB = 0x10F;
    constexpr ptrdiff_t kUpdateArgOffsetB = 4;
    auto* base = static_cast<unsigned char*>(self);
    if (base[kUpdateNameOffsetB] == 0) return 0;
    if (abi_newfiles_call_obj(abi_newfiles_find_obj(),
                              GameHash(reinterpret_cast<const char*>(base + kUpdateNameOffsetB))))
        abi_newfiles_call_add(base + kUpdateArgOffsetB, base + kUpdateNameOffsetB);
    return 1;
}
// DLC discovery post step (sub_10023150): enumerate \dlc\tlad\content\ / \TLAD\,
// \dlc\TBoGT\content\ / \TBoGT\ and \EpisodeN\ next to the executable and
// register each through the two published game RVAs. The original writes the
// module base + RVA into the struct and hands it to the register/apply functions.
//
// Naked only to capture EDX: the apply function returns its second value in EDX
// and the caller's resume reads it. Contract: __cdecl, four dword arguments
// (object, request, apply function, out-EDX); no return value; the caller cleans
// the stack.
static __declspec(naked) void __cdecl zpatch_dlc_apply_capture_edx(
    void*, char*, DlcApplyFn, uint32_t*)
{
    __asm {
        push ebp
        mov  ebp, esp
        mov  ecx, dword ptr [ebp + 8]
        push dword ptr [ebp + 0Ch]
        call dword ptr [ebp + 10h]
        mov  eax, dword ptr [ebp + 14h]
        mov  dword ptr [eax], edx
        mov  esp, ebp
        pop  ebp
        ret
    }
}

// The 0x104-byte path buffer the discovery routine builds for each DLC folder.
constexpr size_t kDlcPathCapacity = 0x104u;
// sub_10023150's per-folder request struct and the two fields it sets before
// handing the request to the game's register function (recovered stack layout).
constexpr size_t kDlcRequestSize = 360u;
constexpr size_t kDlcRequestModeOffset = 358u;  // byte, set to 1
constexpr size_t kDlcRequestFlagOffset = 344u;  // dword, set to 1
constexpr uint8_t kDlcRequestRegistered = 1u;
// The DLC entry's post-discovery state byte (the fixup body sets +0xA4 after it).
constexpr size_t kDlcEntryPostStateOffset = 358u;
constexpr uint8_t kDlcEntryPostState = 2u;
// Episode 254 is the one the original treats as the last apply producer.
constexpr int kEpisode254Id = 508;
// The episode loop's recovered bounds and id formula: "\Episode3" .. "\Episode254".
constexpr int kFirstEpisodeNumber = 3;
constexpr int kEpisodeNumberLimit = 255;  // exclusive
constexpr int kEpisodeIdMultiplier = 2;

void* dlc_discovery_post(void* object)
{
    // The original allocates with a throwing operator new and ignores both its
    // result and GetModuleFileNameA's: a failed allocation or a failed module
    // path then builds every DLC path from uninitialized bytes. The failsafe
    // leaves the episode list untouched instead, which is the LoadDLCs=0 state.
    auto* const module_path =
        static_cast<char*>(::operator new(kDlcPathCapacity, std::nothrow));
    if (zpatch_failsafe_skip("LoadDLCs path buffer", module_path == nullptr))
        return object;
    const DWORD module_length = GetModuleFileNameA(nullptr, module_path, kDlcPathCapacity);
    if (zpatch_failsafe_skip("LoadDLCs module path",
                             module_length == 0 || module_length >= kDlcPathCapacity))
        return object;
    PathRemoveFileSpecA(module_path);
    uint32_t last_apply_edx = 0;
    bool episode254_registered = false;

    // The original picks ONE path per group: the "content" layout if it exists,
    // otherwise the legacy fallback; it never registers both.
    const auto try_paths = [&](const char* primary, const char* fallback, int id)
    {
        char path[kDlcPathCapacity];
        strcpy_s(path, kDlcPathCapacity, module_path);
        strcat_s(path, kDlcPathCapacity, primary);
        if (!PathFileExistsA(path))
        {
            if (std::strcmp(primary, fallback) == 0) return;
            strcpy_s(path, kDlcPathCapacity, module_path);
            strcat_s(path, kDlcPathCapacity, fallback);
            if (!PathFileExistsA(path)) return;
        }
        char request[kDlcRequestSize]{};
        strcpy_s(request, kDlcPathCapacity, path);
        request[kDlcRequestModeOffset] = kDlcRequestRegistered;
        *reinterpret_cast<uint32_t*>(request + kDlcRequestFlagOffset) =
            kDlcRequestRegistered;
        const auto game_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        const auto register_fn =
            GameFunction<DlcRegisterFn>(game_base + DlcRegisterRva());
        const auto apply_fn = GameFunction<DlcApplyFn>(game_base + DlcApplyRva());
        register_fn(id, request, kDlcRequestRegistered);
        zpatch_dlc_apply_capture_edx(object, request, apply_fn, &last_apply_edx);
        if (id == kEpisode254Id) episode254_registered = true;
    };

    try_paths("\\dlc\\tlad\\content\\", "\\TLAD\\", 2);
    try_paths("\\dlc\\TBoGT\\content\\", "\\TBoGT\\", 4);
    for (int i = kFirstEpisodeNumber; i < kEpisodeNumberLimit; ++i)
    {
        // The original formats "\EpisodeN\" into a 64-byte stack buffer.
        constexpr size_t kEpisodeSuffixCapacity = 0x40;
        char suffix[kEpisodeSuffixCapacity];
        zpatch_spawns_sprintf(suffix, sizeof(suffix), "\\Episode%d\\", i);
        try_paths(suffix, suffix, kEpisodeIdMultiplier * i);
    }
    *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(object) +
                                kDlcEntryPostStateOffset) = kDlcEntryPostState;
    // sub_10023150's final sub_10001280 call leaves its 0x40 buffer size in EDX;
    // PathFileExistsA preserves EDX and the caller's resume observes it. If
    // Episode254 exists, its final apply call is the last EDX producer instead
    // (captured by the asm helper above).
    if (episode254_registered)
    {
        __asm { mov edx, last_apply_edx }
    }
    else
    {
        __asm { mov edx, 40h }
    }
    return object;
}

// @exact 0x10023490 LoadDLCs struct fixup: the object arrives in ESI and the
// epilogue pops esi/ebx and cleans 0x14 stack bytes; the original instruction
// sequence is reproduced (with our own callback slot and post step).
// Field map of the DLC entry in ESI: +0x00/+0x40/+0x80/+0xC0/+0x100 are the five
// per-episode flag bytes the fixup clears, +0x167 is "already loaded",
// +0x168 the forced flag, +0xA4 the post-step marker; 0x14 is the epilogue's
// stack cleanup.
static __declspec(naked) void abi_load_dlcs_struct_fixup()
{
    __asm {
        mov BYTE PTR [esi], 0
        mov BYTE PTR [esi + 0x40], 0
        mov BYTE PTR [esi + 0x80], 0
        mov BYTE PTR [esi + 0xc0], 0
        mov BYTE PTR [esi + 0x100], 0
        cmp BYTE PTR [esi + 0x167], 0
        je load
        mov BYTE PTR [esi + 0x168], 1
    exit_label:
        pop esi
        pop ebx
        add esp, 0x14
        ret
    load:
        mov ecx, esi
        call DWORD PTR [zpatch_dlc_load_callback]
        push esi
        call dlc_discovery_post
        pop esi
        mov BYTE PTR [esi + 0xa4], 1
        jmp exit_label
    }
}
}  // extern "C"

#endif  // !defined(_WIN64)

namespace opp::features
{

    // publish LoadDLCs

    void PublishLoadDLCs(PatchContext& ctx)
    {

        TraceArm("publish LoadDLCs");

        // LoadDLCs. The loader callback the struct-fixup body calls sits 0x8C
        // bytes past the fixup site on every gated build (0x4144D4+0x8C on
        // 1.0.6.0, 0x414274+0x8C on 1.0.7.0, 0x4903D4+0x8C on 1.0.8.0), so the
        // publish scans the same instruction window the install does instead of
        // carrying three RVAs.
        //
        // The two helpers the discovery loop calls (DlcRegisterRva/DlcApplyRva)
        // are separate functions: each per-build RVA below comes from that
        // function's own unique 24/16-byte window, proved with
        // tools/opcep_sigcheck.py (tests/parity/address_sites.tsv).
        if (ctx.Option("LoadDLCs", "1"))
        {
            // Distance from the struct-fixup site to the loader callback entry
            // (1.0.6.0 0x4144D4->0x414560, 1.0.7.0 0x414274->0x414300,
            // 1.0.8.0 0x4903D4->0x490460: the same 0x8C on every gated build).
            constexpr uintptr_t kDlcCallbackDelta = 0x8C;
            // The per-gated-build instruction windows: the struct-fixup site
            // (the callback is 0x8C past it), the register helper and the apply
            // helper. The apply window is identical on all three builds; the
            // register window's stack-cookie dword differs (40 2F F4 / 40 3F F4
            // / 40 2C F6) and 1.0.8.0's ends differently.
            struct DlcWindows final
            {
                GameBuild build;
                const char* fixup;
                const char* register_helper;
                const char* apply_helper;
            };
            constexpr DlcWindows kWindows[] = {
                {GameBuild::v1060,
                 "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? ?? E8",
                 "81 EC 0C 02 00 00 A1 40 2F F4 00 33 C4 89 84 24 08 02 00 00 53 55 56 57",
                 "51 53 8B D9 0F B7 8B 50 01 00 00 55 56 33 ED 85"},
                {GameBuild::v1070,
                 "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? 83 E8 ?? 75 ?? 5E 5B",
                 "81 EC 0C 02 00 00 A1 40 3F F4 00 33 C4 89 84 24 08 02 00 00 53 55 56 57",
                 "51 53 8B D9 0F B7 8B 50 01 00 00 55 56 33 ED 85"},
                {GameBuild::v1080,
                 "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6",
                 "81 EC 0C 02 00 00 A1 40 2C F6 00 33 C4 89 84 24 08 02 00 00 53 55 56 8B",
                 "51 53 8B D9 0F B7 8B 50 01 00 00 55 56 33 ED 85"},
            };

            const GameImage& image = ctx.image;

            // The two helpers the discovery loop calls are entries too, so the
            // publish scans them (unique 24/16-byte windows, proved with
            // tools/opcep_sigcheck.py) and stores the RVA the body rebases.
            const auto AsRva = [&ctx](const void* address) {
                return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address) -
                                             ctx.gameBase);
            };

            const auto publishCallback = [&ctx](hook::pattern& pattern) {
                if (pattern.size() == 0)
                    return;
                zpatch_dlc_load_callback = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(pattern.get(0).get<uint8_t>()) +
                    kDlcCallbackDelta);
            };

            for (const DlcWindows& windows : kWindows)
            {
                if (ctx.build != windows.build)
                    continue;
                if (auto pattern = Pattern(image, windows.fixup); pattern.size() > 0)
                    publishCallback(pattern);
                if (auto pattern = Pattern(image, windows.register_helper); pattern.size() > 0)
                    DlcRegisterRva() = AsRva(pattern.get(0).get<uint8_t>());
                if (auto pattern = Pattern(image, windows.apply_helper); pattern.size() > 0)
                    DlcApplyRva() = AsRva(pattern.get(0).get<uint8_t>());
            }
        }

    }


    // LoadDLCs

    void InstallLoadDLCs(PatchContext& ctx)
    {

        TraceArm("LoadDLCs");

        // LoadDLCs: 6 sites per build, each the pre-write instruction window at
        // the recovered RVA. The three destination deltas are recovered facts and
        // are the same on both 1.0.6.0/1.0.7.0 (jump +0x5C, jump +0x1E, call
        // -0x70A); 1.0.8.0's helper call is at -0x88A. Keeping them relative to
        // the match is what lets the sites be scans.
        //
        // Order: the call site is written before the twelve-byte NOP after it,
        // because the call's window contains the NOP'd bytes. Later blocks keep
        // the original statement order for the same reason.
        if (ctx.Option("LoadDLCs", "1"))
        {
            if (ctx.build != GameBuild::v1060 && ctx.build != GameBuild::v1070 &&
                ctx.build != GameBuild::v1080)
                return;
            const GameImage& image = ctx.image;
            const bool is1080 = ctx.build == GameBuild::v1080;

            // Destinations as deltas from the site: +0x5C jumps into the same
            // block, +0x1E skips the fixup (identical on 1.0.6.0/1.0.7.0), and
            // the helper call is -0x70A on those builds / -0x88A on 1.0.8.0.
            constexpr uintptr_t kJumpIntoSameBlock = 0x5C;
            constexpr uintptr_t kJumpOverFixup = 0x1E;
            constexpr uintptr_t kHelperCallBeforeSite = 0x70A;
            constexpr uintptr_t kHelperCallBeforeSite1080 = 0x88A;
            // The call following the site whose twelve bytes the original NOPs.
            constexpr size_t kTailSiteLength = 12;

            const auto findSite = [&image](const char* signature) -> uint8_t* {
                auto pattern = Pattern(image, signature);
                return pattern.size() > 0 ? pattern.get(0).get<uint8_t>() : nullptr;
            };

            // The dummy-call and struct-fixup windows differ per build;
            // 1.0.6.0/1.0.7.0 share the dummy-call window, 1.0.8.0 has its own.
            const char* const dummyCallSignature =
                is1080
                    ? "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 4C 24 ?? 51 8D 94 24 ?? ?? ?? ?? 52 E8"
                    : "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 8C 24 ?? ?? ?? ?? ?? ?? ?? ?? ?? 52";
            const char* const structFixupSignature =
                ctx.build == GameBuild::v1060
                    ? "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? ?? E8"
                    : (ctx.build == GameBuild::v1070
                           ? "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? 83 E8 ?? 75 ?? 5E 5B"
                           : "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6");

            uint8_t* const dummyCall = findSite(dummyCallSignature);
            uint8_t* const earlyJump = findSite(
                "E8 ?? ?? ?? ?? 6A ?? 8D 44 24 ?? 50 53 56 8D 4C 24");
            uint8_t* const structFixup = findSite(structFixupSignature);
            uint8_t* const skipFixupJump = findSite(
                "80 BE ?? ?? ?? ?? ?? 74 ?? 8B 44 24 ?? 50 E8 ?? ?? ?? ?? 83 C4 ?? ?? ?? ?? ?? 5E");
            uint8_t* const helperCall = findSite(
                "E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? 5E");
            uint8_t* const tail = findSite(
                "8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? B0 ?? 5E C2 ?? ?? CC CC CC CC");

            if (dummyCall != nullptr)
                CallTo(ctx, dummyCall, OPP_BODY(abi_load_dlcs_dummy));
            if (earlyJump != nullptr)
                JumpTo(ctx, earlyJump, earlyJump + kJumpIntoSameBlock);
            if (structFixup != nullptr)
                JumpTo(ctx, structFixup, OPP_BODY(abi_load_dlcs_struct_fixup));
            if (skipFixupJump != nullptr)
                JumpTo(ctx, skipFixupJump, skipFixupJump + kJumpOverFixup);
            if (helperCall != nullptr)
                CallTo(ctx, helperCall,
                       helperCall - (is1080 ? kHelperCallBeforeSite1080
                                            : kHelperCallBeforeSite));
            if (tail != nullptr)
                NopAt(ctx, tail, kTailSiteLength);
        }

    }


    // publish NewFilesCompatibility

    void PublishNewFilesCompatibility(PatchContext& ctx)
    {

        TraceArm("publish NewFilesCompatibility");

        // NewFilesCompatibility: 1 slot
        if (ctx.Option("NewFilesCompatibility", "1") && !NewFilesConflictModulesPresent())
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 64 28 C8 67 E8"); pattern.size() > 0)
            {
                // The E8 at +5 calls the registrar; its rel32 operand is at +6.
                constexpr ptrdiff_t kRegisterCallOperand = 6;
                zpatch_newfiles_register_slot =
                    BranchDestination(pattern.get(0).get<uint8_t>(), kRegisterCallOperand);
            }
        }

    }


    // NewFilesCompatibility

    void InstallNewFilesCompatibility(PatchContext& ctx)
    {

        TraceArm("NewFilesCompatibility");

        // NewFilesCompatibility: 2 siginstalls, 1 sigjump
        if (ctx.Option("NewFilesCompatibility", "1") && !NewFilesConflictModulesPresent())
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 7B FF FF FF"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallTo(ctx, match, OPP_BODY(abi_newfiles_update_a));
            }

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 6C FF FF FF"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallTo(ctx, match, OPP_BODY(abi_newfiles_update_b));
            }

            if (auto pattern = Pattern(image, "68 64 28 C8 67 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The E8 to the registrar sits at +28 in this window.
                constexpr ptrdiff_t kRegisterBodyCall = 28;
                JumpTo(ctx, match + kRegisterBodyCall,
                       OPP_BODY(abi_newfiles_register_body));
            }
        }

    }


    // publish NewSavesCompatibility

    void PublishNewSavesCompatibility(PatchContext& ctx)
    {

        TraceArm("publish NewSavesCompatibility");

        // NewSavesCompatibility: 3 slots. The newer save format is 1060-1080
        // only; gate the arm instead of relying on the pattern coincidentally
        // missing on 1000/1004/1040 and EFLC.
        if (ctx.Option("NewSavesCompatibility", "1") &&
            BuildIs(ctx.build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                GameBuild::v1080}))
        {
            const GameImage& image = ctx.image;
            // The save-name operand is the `push imm32` of the tramp site's
            // first instruction; the resume is the instruction after the
            // 13-byte prologue the tramp overwrites.
            constexpr ptrdiff_t kSaveNameOperand = 1;
            constexpr ptrdiff_t kResumeAfterPrologue = 13;

            auto boot = Pattern(image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08");
            if (boot.size() > 0)
            {
                zpatch_new_saves_src = *boot.get(0).get<uint32_t>(kSaveNameOperand);
                if (auto pattern = Pattern(image, "89 84 24 E0 01 00 00 8B 45 08 53 33 DB"); pattern.size() > 0)
                {
                    zpatch_new_saves_resume =
                        MatchAddress(pattern, kResumeAfterPrologue);
                }
            }
        }

    }


    // NewSavesCompatibility

    void InstallNewSavesCompatibility(PatchContext& ctx)
    {

        TraceArm("NewSavesCompatibility");

        // NewSavesCompatibility: 1 sigjump. The savegame-load entry is only
        // redirected when both the tramp site and the entry window exist.
        if (ctx.Option("NewSavesCompatibility", "1") &&
            BuildIs(ctx.build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                GameBuild::v1080}))
        {
            const GameImage& image = ctx.image;

            if (Pattern(image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08").size() > 0)
            {
                if (auto pattern = Pattern(image, "89 84 24 E0 01 00 00 8B 45 08 53 33 DB"); pattern.size() > 0)
                {
                    JumpTo(ctx, pattern.get(0).get<uint8_t>(),
                           OPP_BODY(abi_new_saves_compat));
                }
            }
        }

    }


    // OverrideGTADatPath

    void InstallOverrideGTADatPath(PatchContext& ctx)
    {

        TraceArm("OverrideGTADatPath");

        // OverrideGTADatPath: 1 literalstore
        if (ctx.Option("OverrideGTADatPath", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The `push imm32` operand names the gta.dat path pointer the
                // original pushes; the arm points it at the configured path.
                constexpr ptrdiff_t kGtaDatPathOperand = 1;
                WriteValue<uint32_t>(ctx, match + kGtaDatPathOperand,
                                     static_cast<uint32_t>(
                                         FindGlobalValue("OverrideGTADatPath")));
            }
        }

    }


    // SCOSignatureFix

    void InstallSCOSignatureFix(PatchContext& ctx)
    {

        TraceArm("SCOSignatureFix");

        // SCOSignatureFix: 1 literal
        if (ctx.Option("SCOSignatureFix", "1"))
        {
            const GameImage& image = ctx.image;

            // The recovered op row anchors the write at offset 0 of its own
            // signature. The old arm scanned a signature 32 bytes downstream
            // and wrote at match-32, which is only correct while that distance
            // holds; a build where it does not would flip an unrelated `jz`.
            // This signature ends on the `jz` itself (`mov ds:X,edi; test
            // ebp,ebp; jz`), unique on 1.0.8.0, so the site is match+8.
            if (auto pattern = Pattern(image, "89 3D ? ? ? ? 85 ED 74 1E"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // `mov ds:X,edi` (6 bytes) + `test ebp,ebp` (2) put the `jz`
                // opcode at +8; the original flips it to a `jmp`.
                constexpr ptrdiff_t kSignatureBranchOffset = 8;
                WriteValue<uint8_t>(ctx, match + kSignatureBranchOffset,
                                    opp::ops::kJmpRel8);
            }
        }

    }

namespace
{
    class LoadingFeatures
    {
    public:
        LoadingFeatures()
        {
            PublishEvent().Add(&PublishLoadDLCs, "PublishLoadDLCs");
            PublishEvent().Add(&PublishNewFilesCompatibility, "PublishNewFilesCompatibility");
            PublishEvent().Add(&PublishNewSavesCompatibility, "PublishNewSavesCompatibility");
            InstallEvent().Add(&InstallLoadDLCs, "InstallLoadDLCs");
            InstallEvent().Add(&InstallNewFilesCompatibility, "InstallNewFilesCompatibility");
            InstallEvent().Add(&InstallNewSavesCompatibility, "InstallNewSavesCompatibility");
            InstallEvent().Add(&InstallSCOSignatureFix, "InstallSCOSignatureFix");
            InstallEvent().Add(&InstallOverrideGTADatPath, "InstallOverrideGTADatPath");
        }
    } loadingFeatures;
} // namespace

} // namespace opp::features
