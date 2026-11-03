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

#include <new>
#include <memory>
#include <optional>
#include <span>
#include <shlwapi.h>
export module opp.loading;

import opp.patch;
import opp.game;
import opp.common;
import opp.config;
import opp.log;

// Hook bodies: the machine-level entry points loading installs. Each keeps the original
// site's calling convention and register contract (its comment names the destination VA);
// 32-bit only - the x64 parity dump resolves names to the original VAs.
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

// NewSavesCompatibility's two LoadData hooks: both are compiled C reconstructions
// of functions the original ASI keeps at 0x10051A90 (read hook) and 0x10051AD0
// (p3 hook); neither carries an @exact contract. Their contract is the LoadData
// ABI they wrap: cdecl, a buffer and a byte count, returning LoadData's own
// result.
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

// The original p3 hook (sub_10051AD0) keeps a fixed 12-byte local and reads the
// new-format block into it before and after the wrapped handler; the block is a
// sink (its contents are never observed).
constexpr size_t kSaveBufferSize = 12u;

extern "C"
{
// The P2 LoadData replacement (original sub_10051A90): load into the fixed
// scratch instead of the game's destination and return LoadData's result. The
// original returns the full EAX; this rebuild declares uint8_t, so callers see
// AL exactly as they did before.
//
// The scratch is the original's frame: `sub esp, 0xE7C` with a 3704-byte (0xE78)
// buffer under the /GS cookie. 3704 is the original's literal, and the largest
// request the P2 site passes is the new-format read (3694), which fits. `size` is
// passed through to LoadData; the buffer is never sized from it.
extern "C" uint8_t abi_new_saves_read_hook(void*, uint32_t size) noexcept
{
    std::array<uint8_t, 3704u> scratch;
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

// The save-format scanner: the converter reads the game's save through a cursor,
// so the byte-level steps have names (the 0x110-byte header, the 9-byte chunk
// headers, the per-chunk payload step, the 18-chunk walk). The game hands the
// scan a std::FILE*, so the cursor wraps it rather than replacing it with an
// ifstream - the fread/fseek conventions and the unchecked-malloc fault are the
// original's.
//
// The three new-format probes are its only output: chunk 13 gates the p3 hook,
// chunk 16 selects the 3694-byte P2 read, chunk 9 gates P1.
namespace
{
// The header each of the 18 save chunks starts with.
constexpr size_t kSaveChunkHeaderSize = 9;

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
    std::array<std::byte, 272> header{};
    const bool read = file.ReadItem(header);
    if (policy.validate && !read)
        return false;
    return LoadDword(header.data() + 12) == 0x45564153u;  // "SAVE" at +0xC
}

// Reads one 9-byte chunk header and returns its total size (the dword at +5). A
// size below the header is the original's allocation-count underflow; only the
// validating policy rejects it.
[[nodiscard]] std::optional<uint32_t> ReadChunkHeader(FileCursor& file,
                                                      opp::SaveScanPolicy policy) noexcept
{
    std::array<std::byte, kSaveChunkHeaderSize> header{};
    const bool read = file.ReadItem(header);
    if (policy.validate && !read)
        return std::nullopt;
    const uint32_t size = LoadDword(header.data() + 5);
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
    // fseek takes a long; the element count is only seekable when it fits one.
    const uint32_t element_count = chunk_size - kSaveChunkHeaderSize;
    if (policy.skip_payload && chunk_size >= kSaveChunkHeaderSize &&
        element_count <= 0x7FFFFFFFu)
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
    for (size_t index = 0; index < 18; ++index)
    {
        const std::optional<uint32_t> size = ReadChunkHeader(file, policy);
        if (!size)
            return std::nullopt;
        zpatch_new_saves_chunk_len = *size;                             // dword_1015AD90
        zpatch_new_saves_element_count = *size - kSaveChunkHeaderSize;  // dword_1015ADA0
        if (!SkipPayload(file, *size, policy))
            return std::nullopt;
        // The three chunk sizes that tell the formats apart (the original's
        // recovered indices); each is the new format's chunk total.
        if (index == 9u)
            probes.chunk9_new = *size == 19525u;
        if (index == 16u)
            probes.chunk16_new = *size == 3703u;
        if (index == 13u)
            probes.chunk13_new = *size == 5953u;
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

// The three patchers of the converter's game sites; abi_new_saves_convert scans
// each window on first use and keeps the hit.
namespace
{
// P1: force the 0x3C-byte read path for new saves by flipping its `jz` to `jmp`.
void PatchP1(const uint8_t* site) noexcept
{
    wp_byte(const_cast<uint8_t*>(site),
            zpatch_new_saves_chunk9_new != 0 ? opp::ops::kJmpRel8 : opp::ops::kJzRel8);
}

// P2: route the oversized read through the plugin's scratch buffer, resize the
// read and force/NOP the skip branch per the save format.
void PatchP2(const uint8_t* site) noexcept
{
    // The E8 call sits at site+6 (rel32 operand at site+7), the `push imm32`
    // carrying the load size at site+1, and the `jnz rel8` five bytes before it.
    constexpr ptrdiff_t kLoadDataCallOffset = 6;
    constexpr ptrdiff_t kBranchOperandOffset = 7;
    constexpr ptrdiff_t kSkipLoadBranchOffset = -5;

    auto* const target = const_cast<uint8_t*>(site);
    const uint32_t original_target = BranchDestination(target, kBranchOperandOffset);
    // The call already is E8+rel32; MakeCALL writes the same five bytes and keeps
    // the original's read of the old destination first.
    injector::MakeCALL(injector::raw_ptr(target + kLoadDataCallOffset),
                       injector::raw_ptr(&abi_new_saves_read_hook), true);
    if (zpatch_new_saves_loaddata == 0)
        zpatch_new_saves_loaddata = original_target;

    // The two sizes the P2 install writes into its `push imm32`: an old-format
    // save reads 3262 bytes, a new-format one 3694.
    wp_dword(target + 1, zpatch_new_saves_chunk16_new != 0 ? 3694u : 3262u);

    // The short branch is `75 26` (jnz +0x26); the new save replaces it with a
    // two-byte NOP and the old save restores the same two bytes.
    if (zpatch_new_saves_chunk16_new != 0)
        injector::MakeNOP(injector::raw_ptr(target + kSkipLoadBranchOffset), 2, true);
    else
        wp_word(target + kSkipLoadBranchOffset, 0x2675u);
}

// P3: route the second LoadData call through the plugin body that consumes the
// extra 12-byte block for new saves, and repoint the shared LoadData slot at the
// function's own later call target.
void PatchP3(const uint8_t* site) noexcept
{
    // The replaced call is the second E8 (site+6, rel32 operand at site+7); the
    // third call's operand at site+30 names the shared LoadData slot.
    constexpr ptrdiff_t kLoadDataCallOffset = 6;
    constexpr ptrdiff_t kBranchOperandOffset = 7;

    auto* const target = const_cast<uint8_t*>(site);
    const uint32_t original_target = BranchDestination(target, kBranchOperandOffset);
    injector::MakeCALL(injector::raw_ptr(target + kLoadDataCallOffset),
                       injector::raw_ptr(&abi_new_saves_p3_hook), true);
    if (zpatch_new_saves_orig_handler == 0)
        zpatch_new_saves_orig_handler = original_target;
    zpatch_new_saves_loaddata = BranchDestination(target, 30);
}
}  // namespace

extern "C"
{

// The NewSavesCompatibility converter (original sub_10051B30): parse the save's
// 0x110-byte header (magic "SAVE" at +0xC), walk 18 chunks of 9-byte header +
// payload, record the new-format chunk sizes, then patch three game sites.
extern "C" void abi_new_saves_convert(const char* path) noexcept
{
    // The converter's shipped scan policy: validate every read/size and seek over
    // payloads. All-false would be the original loop: every fread result ignored,
    // the size underflow passing and the payload malloc unchecked. The pair is a
    // named policy so a caller cannot swap the two booleans' meaning.
    const std::unique_ptr<std::FILE, opp::FileCloser> file(std::fopen(path, "rb"));
    if (!file)
        return;
    FileCursor cursor(file.get());
    const std::optional<SaveChunkProbes> probes = ScanSaveFile(cursor, opp::SaveScanPolicy{true, true});
    if (!probes)
        return;  // including the original wrong-magic early return
    zpatch_new_saves_chunk9_new = probes->chunk9_new ? 1u : 0u;
    zpatch_new_saves_chunk16_new = probes->chunk16_new ? 1u : 0u;
    zpatch_new_saves_chunk13_new = probes->chunk13_new ? 1u : 0u;

    // Each site is scanned on first use and the hit kept; the three blocks run in
    // the original's order. The 1.0.8.0 VA each window was recovered at trails it
    // as the review cross-check; the VAs differ on 1.0.6.0/1.0.6.1/1.0.7.0, so the
    // scan is the anchor.
    static const uint8_t* p1 = nullptr;
    if (p1 == nullptr)
        p1 = scan_game("74 37 80 3D ? ? ? ? 00 75 2E 8D 84 ? ? ? ? 00 6A 1E");  // 0x872185
    if (p1 != nullptr)
        PatchP1(p1);

    static const uint8_t* p2 = nullptr;
    if (p2 == nullptr) p2 = scan_game("68 BE 0C 00 00 50 E8");  // 0x7D8C50
    if (p2 != nullptr)
        PatchP2(p2);

    static const uint8_t* p3 = nullptr;
    if (p3 == nullptr)
        p3 = scan_game("56 E8 ? ? ? ? E8 ? ? ? ? A1 ? ? ? ? 8D 0C 85 00 00 00 00");  // 0x8DA090
    if (p3 != nullptr)
        PatchP3(p3);
}

// The prepare step of the tramp (original sub_10051FD0): if the handler
// captured the game's save-name buffer, resolve the game's save-directory getter
// (scan) and convert "<dir>\\<save name>".
extern "C" void abi_new_saves_prepare() noexcept
{
    // The getter (sub_61E7D0, VA 0x61E7D0 on 1.0.8.0) fills `directory` and
    // writes a trailing byte through its third argument; 312 and 560 are the
    // original's recovered stack capacities for the directory and the joined
    // directory + save-name path.
    using GetSaveDirectoryFn = uint8_t(__cdecl*)(int episode, char* directory,
                                                 char* trailing_byte);
    const auto* name = Rebased<const char*>(zpatch_new_saves_src);
    if (name == nullptr) return;
    static const uint8_t* get_dir = nullptr;
    if (get_dir == nullptr)
        get_dir = scan_game("55 8B EC 83 E4 F8 83 EC 4C 56 33 C0 8D 4C 24 28");
    if (get_dir == nullptr) return;
    const auto get_directory = GameFunction<GetSaveDirectoryFn>(
        reinterpret_cast<uintptr_t>(get_dir));
    char directory[312]{};
    char suffix = 0;
    if (get_directory(0, directory, &suffix) == 0)
        return;
    char path[560];
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
    register_file(0x2C89A1F9u, reinterpret_cast<void*>(&abi_newfiles_cb));
}

namespace
{
// The runtime windows the newfiles bodies scan are generic hash->dictionary
// idioms, so a look-alike in another function satisfies one: the first hit is
// what the body uses. newfiles_find returns a bare address, so these scans cannot
// carry an expected-count guard. On 1.0.8.0 the lookup window hits twice
// (0x43A490, the grcTexture instantiation, and 0xA31B30) and the add window twice
// (0x85DF54, inside CSprite2d::SetTexture, and 0xA98269).

// A runtime window scanned on first use: the game never reloads its code, so the
// scan runs once and a hit is kept for the process (the original's one-time
// guard, in named fields).
struct ScannedWindow final
{
    bool      resolved = false;
    uintptr_t address = 0;
};

uintptr_t ScanOnce(ScannedWindow& window, std::initializer_list<std::string_view> candidates)
{
    if (!window.resolved)
    {
        window.resolved = true;
        for (const std::string_view candidate : candidates)
        {
            window.address = newfiles_find(candidate);
            if (window.address != 0)
                break;
        }
    }
    return window.address;
}
}  // namespace

extern "C" void* abi_newfiles_find_obj()  // 0x10051730
{
    // The original re-dereferences the container global on EVERY call
    // (`mov esi, [match+2]`), so it is picked up again whenever the game
    // rebuilds its registry; caching the container itself left a stale one that
    // faulted in the game's container search (0x43a490).
    static ScannedWindow container;
    const uintptr_t match = ScanOnce(
        container,
        {"8B 35 ? ? ? ? 6A 00 50 E8 ? ? ? ? 83 C4 08 50 8B CE E8 ? ? ? ? 85 C0 89 07 5E 74 05"});
    if (match == 0) return nullptr;
    // The container global the window loads (`mov esi, dword_19CE510`): its operand
    // is at +2 and the game re-creates the dictionary, so it is re-read.
    const uint32_t global = Field(reinterpret_cast<const uint8_t*>(match), 2);
    return global ? reinterpret_cast<void*>(
                        *reinterpret_cast<const uint32_t*>(reinterpret_cast<const void*>(global)))
                  : nullptr;
}
extern "C" int abi_newfiles_call_obj(void* obj, uint32_t hash)  // 0x10051610
{
    using Fn = int(__fastcall*)(void* self, int, uint32_t);
    static ScannedWindow lookup;
    const uintptr_t target = ScanOnce(
        lookup,
        {"85 C9 53 55 56 57 74 40 8B 6C 24 14 8D 64 24 00", "53 55 8B 6C 24 0C 56 57 EB 06"});
    if (target == 0) return 0;
    return GameFunction<Fn>(target)(obj, 0, hash);
}
extern "C" int abi_newfiles_call_add(void* self, void* str)  // 0x100517E0
{
    using Fn = int(__fastcall*)(void* self, int, void*);
    static ScannedWindow add;
    const uintptr_t match =
        ScanOnce(add, {"8B 0F 85 C9 74 ? 0F B7 41 0A 66 85 C0"});
    if (match == 0) return 0;
    // The add window matches 20 bytes into the intended function, so the write
    // body keeps pointing at its entry. On 1.0.8.0 the first hit is the SetTexture
    // occurrence, which is where the 20 comes from.
    return GameFunction<Fn>(match - 20)(self, 0, str);
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

namespace
{
// The update object both E8 hooks share: the two original bodies differ only in
// the asset-name offset and the callback's second argument (see the two hooks).
char __fastcall NewFilesUpdate(void* self, size_t name_offset, ptrdiff_t arg_offset)
{
    auto* const base = static_cast<unsigned char*>(self);
    if (base[name_offset] == 0)
        return 0;
    if (abi_newfiles_call_obj(abi_newfiles_find_obj(),
                              GameHash(reinterpret_cast<const char*>(base + name_offset))))
        abi_newfiles_call_add(base + arg_offset, base + name_offset);
    return 1;
}
}  // namespace

// The asset name sits at +0x10 (version A) / +0x10F (version B) and the
// callback's second argument at +8 / +4.
extern "C" char __fastcall abi_newfiles_update_a(void* self, int)  // 0x10051890
{
    constexpr size_t kNewFilesNameOffset = 0x10;
    constexpr ptrdiff_t kNewFilesArgOffset = 8;
    return NewFilesUpdate(self, kNewFilesNameOffset, kNewFilesArgOffset);
}
extern "C" char __fastcall abi_newfiles_update_b(void* self, int)  // 0x100518D0
{
    constexpr size_t kNewFilesNameOffset = 0x10F;
    constexpr ptrdiff_t kNewFilesArgOffset = 4;
    return NewFilesUpdate(self, kNewFilesNameOffset, kNewFilesArgOffset);
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

// The 0x104-byte buffers the discovery builds: the module path and each DLC
// folder path.
constexpr size_t kDlcPathCapacity = 0x104u;

// The per-folder request struct sub_10023150 hands to the game's register
// function: 360 bytes, with the module path first and the two fields the
// discovery sets in its tail.
constexpr size_t kDlcRequestFlagOffset = 0x158;
constexpr size_t kDlcRequestModeOffset = 0x166;
constexpr size_t kDlcRequestSize = 360;
constexpr uint8_t kDlcRequestRegistered = 1u;

struct DlcRegisterRequest final
{
    char     path[kDlcPathCapacity];                                   // +0x000
    uint8_t  reserved[kDlcRequestFlagOffset - kDlcPathCapacity];       // +0x104
    uint32_t flag;                                                     // +0x158
    uint8_t  reserved2[kDlcRequestModeOffset -
                       (kDlcRequestFlagOffset + sizeof(uint32_t))];    // +0x15C
    uint8_t  mode;                                                     // +0x166
    uint8_t  tail[kDlcRequestSize -
                  (kDlcRequestModeOffset + sizeof(uint8_t))];          // +0x167
};
static_assert(sizeof(DlcRegisterRequest) == kDlcRequestSize);

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
    // The discovery calls the two published helper RVAs. If the publish pattern
    // missed (a relocated absolute or a different build), a zero RVA would add
    // the image base and call the PE header; the failsafe leaves the episode
    // list untouched instead of faulting.
    if (zpatch_failsafe_skip("LoadDLCs helper RVAs",
                             DlcRegisterRva() == 0u || DlcApplyRva() == 0u))
        return object;
    PathRemoveFileSpecA(module_path);
    uint32_t last_apply_edx = 0;
    bool episode254_registered = false;

    // The episode loop's recovered bounds and id formula ("\Episode3" ..
    // "\Episode254"); episode 254 is the last apply producer the original tracks.
    // The original formats "\EpisodeN\" into a 64-byte stack buffer.

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
        DlcRegisterRequest request{};
        // The request's own capacity, not the path buffer's: the two happen to be
        // the same size today, and only `path` bounds this copy.
        strcpy_s(request.path, sizeof(request.path), path);
        request.mode = kDlcRequestRegistered;
        request.flag = kDlcRequestRegistered;
        const auto game_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        const auto register_fn =
            GameFunction<DlcRegisterFn>(game_base + DlcRegisterRva());
        const auto apply_fn = GameFunction<DlcApplyFn>(game_base + DlcApplyRva());
        register_fn(id, request.path, kDlcRequestRegistered);
        zpatch_dlc_apply_capture_edx(object, request.path, apply_fn, &last_apply_edx);
        if (id == 2 * 254) episode254_registered = true;
    };

    try_paths("\\dlc\\tlad\\content\\", "\\TLAD\\", 2);
    try_paths("\\dlc\\TBoGT\\content\\", "\\TBoGT\\", 4);
    for (int i = 3; i < 255; ++i)
    {
        char suffix[64];
        zpatch_spawns_sprintf(suffix, sizeof(suffix), "\\Episode%d\\", i);
        try_paths(suffix, suffix, 2 * i);
    }
    // The DLC entry's post-discovery state byte (+0x166, the original's literal
    // 358), written on the same entry the struct-fixup body receives in ESI. That
    // body's +0xA4 marker is a different field, so the two offsets are unrelated
    // even though both recoveries name 358 for the request's mode byte and this one.
    *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(object) + 358) = 2;
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

namespace
{
// The episode entry the fixup patches (ESI on entry). Opaque bytes: these are the
// only fields the body touches, at the offsets the original's disassembly shows.
// Five per-episode flag bytes sit at +0x00..+0x100 (stride 0x40); the post-step
// marker at +0xA4 falls inside that block, which is why the entry stays a set of
// named offsets rather than a struct with typed fields. The values are decimal so
// the asm block reads as offsets, not as a hex dump; the recovered hex is on each.
//
// Enumerators, not constexpr objects: the inline assembler folds an enumerator to
// its value but assembles a constexpr variable as its own address.
enum : size_t
{
    kDlcEpisodeFlag0Offset = 0,
    kDlcEpisodeFlagStride = 64,  // 0x40
    kDlcEpisodeFlag1Offset = kDlcEpisodeFlag0Offset + kDlcEpisodeFlagStride,
    kDlcEpisodeFlag2Offset = kDlcEpisodeFlag0Offset + 2 * kDlcEpisodeFlagStride,
    kDlcEpisodeFlag3Offset = kDlcEpisodeFlag0Offset + 3 * kDlcEpisodeFlagStride,
    kDlcEpisodeFlag4Offset = kDlcEpisodeFlag0Offset + 4 * kDlcEpisodeFlagStride,
    kDlcAlreadyLoadedOffset = 359,  // +0x167, byte, tested
    kDlcForcedOffset = 360,         // +0x168, byte, set to 1
    kDlcPostStepOffset = 164,       // +0xA4, byte, set after the discovery
    kDlcEpilogueStackBytes = 20,    // 0x14, add esp,0x14
};
static_assert(kDlcEpisodeFlag4Offset == 0x100);
static_assert(kDlcEpisodeFlagStride == 0x40);
}  // namespace

// @exact 0x10023490 LoadDLCs struct fixup: the object arrives in ESI and the
// epilogue pops esi/ebx and cleans 0x14 stack bytes; the original instruction
// sequence is reproduced (with our own callback slot and post step).
static __declspec(naked) void abi_load_dlcs_struct_fixup()
{
    __asm {
        mov BYTE PTR [esi + kDlcEpisodeFlag0Offset], 0
        mov BYTE PTR [esi + kDlcEpisodeFlag1Offset], 0
        mov BYTE PTR [esi + kDlcEpisodeFlag2Offset], 0
        mov BYTE PTR [esi + kDlcEpisodeFlag3Offset], 0
        mov BYTE PTR [esi + kDlcEpisodeFlag4Offset], 0
        cmp BYTE PTR [esi + kDlcAlreadyLoadedOffset], 0
        je load
        mov BYTE PTR [esi + kDlcForcedOffset], 1
    exit_label:
        pop esi
        pop ebx
        add esp, kDlcEpilogueStackBytes
        ret
    load:
        mov ecx, esi
        call DWORD PTR [zpatch_dlc_load_callback]
        push esi
        call dlc_discovery_post
        pop esi
        mov BYTE PTR [esi + kDlcPostStepOffset], 1
        jmp exit_label
    }
}
}  // extern "C"

#endif  // !defined(_WIN64)

namespace opp::features
{

    // publish LoadDLCs

    namespace
    {
        // The per-gated-build windows: the struct-fixup site
        // (CExtraContentManager::Init) and the dummy loader entry's call site.
        // The helper call sits `helper_before` bytes before the site it is found
        // from (the same distance on 1.0.6.0 and 1.0.7.0, longer on 1.0.8.0).
        constexpr uintptr_t kHelperCallBefore = 0x70A;
        struct DlcWindows final
        {
            GameBuild        build;
            std::string_view fixup;
            std::string_view dummy_call;
            uintptr_t        helper_before;
        };
        constexpr DlcWindows kDlcWindows[] = {
            {GameBuild::v1060,
             "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? ?? E8",
             "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 8C 24 ?? ?? ?? ?? ?? ?? ?? ?? ?? 52",
             kHelperCallBefore},
            {GameBuild::v1070,
             "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? 83 E8 ?? 75 ?? 5E 5B",
             "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 8C 24 ?? ?? ?? ?? ?? ?? ?? ?? ?? 52",
             kHelperCallBefore},
            {GameBuild::v1080,
             "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6",
             "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 4C 24 ?? 51 8D 94 24 ?? ?? ?? ?? 52 E8",
             2186},  // 0x88A, further from the helper call than the older builds
        };

        const DlcWindows* FindDlcWindows(GameBuild build) noexcept
        {
            for (const DlcWindows& windows : kDlcWindows)
            {
                if (windows.build == build)
                    return &windows;
            }
            return nullptr;
        }

        // A site inside the game image, as the RVA the body rebases.
        uint32_t AsRva(const void* address) noexcept
        {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address) -
                                         Game::Get().gameBase);
        }

        // One scan of a recovered window; `nullptr` leaves the site alone (an
        // image that is not the one the window was proved on).
        uint8_t* FindSite(std::string_view window, const char* what)
        {
            if (window.empty())
                return nullptr;
            auto pattern = find_pattern(window);
            if (pattern.size() == 0 || !ExpectMatchCount(pattern, 1, what))
                return nullptr;
            return pattern.get_first<uint8_t>();
        }
    } // namespace


    // LoadDLCs

    class LoadDLCs
    {
    public:
        LoadDLCs()
        {
            PublishEvent() += []
            {
                TraceArm("PublishLoadDLCs");

                // LoadDLCs. The two helpers the discovery loop calls
                // (DlcRegisterRva/DlcApplyRva) are separate functions; the per-build RVAs
                // come from each function's own unique window, proved on the on-disk
                // builds by tools/opcep_sigcheck.py (tests/parity/address_sites.tsv).
                if (Game::Option("LoadDLCs", "1"))
                {
                    const DlcWindows* const windows = FindDlcWindows(Game::Get().build);
                    if (windows == nullptr)
                        return;
                    if (auto pattern = find_pattern(windows->fixup);
                        pattern.size() > 0 && ExpectMatchCount(pattern, 1, "LoadDLCs struct fixup"))
                    {
                        // The loader callback entry sits 0x8C past the struct-fixup site on
                        // every gated build (1.0.6.0 0x4144D4->0x414560, 1.0.7.0
                        // 0x414274->0x414300, 1.0.8.0 0x4903D4->0x490460); the publish scans
                        // the same window the install does instead of carrying three RVAs.
                        zpatch_dlc_load_callback = static_cast<uint32_t>(
                            MatchAddress(pattern) + 140);  // 0x8C
                    }
                    // The register helper (sub_88F440) is the stack-cookie prologue whose A1
                    // operand is an absolute VA: the game is relocated at load, so it stays a
                    // wildcard or the scan misses on every non-preferred base. The window ends
                    // before the fourth push, the only byte 1.0.8.0 changes (57 / 8B), so one
                    // window fits all gated builds.
                    if (auto pattern = find_pattern("81 EC 0C 02 00 00 A1 ?? ?? ?? ?? 33 C4 89 84 24 08 02 00 00 53 55 56");
                        pattern.size() > 0 && ExpectMatchCount(pattern, 1, "LoadDLCs register helper"))
                    {
                        DlcRegisterRva() = AsRva(pattern.get_first<uint8_t>());
                    }
                    // The apply helper (sub_88FE00) is identical on every gated build.
                    if (auto pattern = find_pattern("51 53 8B D9 0F B7 8B 50 01 00 00 55 56 33 ED 85");
                        pattern.size() > 0 && ExpectMatchCount(pattern, 1, "LoadDLCs apply helper"))
                    {
                        DlcApplyRva() = AsRva(pattern.get_first<uint8_t>());
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallLoadDLCs");

                // LoadDLCs: 6 sites per build, each the pre-write instruction window at the
                // recovered RVA.
                if (Game::Option("LoadDLCs", "1"))
                {
                    const DlcWindows* const windows = FindDlcWindows(Game::Get().build);
                    if (windows == nullptr)
                        return;
                    // The struct-fixup body calls the callback slot before the discovery
                    // step. If the callback publish missed, installing the body would
                    // call a null slot; skip the arm instead.
        #if !defined(_WIN64)
                    if (zpatch_failsafe_skip("LoadDLCs callback", zpatch_dlc_load_callback == 0u))
                        return;
        #endif
                    // The scans happen before the writes: the call site's window contains
                    // the twelve bytes the original NOPs after it. The fixup and dummy-call
                    // windows are per build; the other four are shared by every gated build.
                    uint8_t* const dummyCall = FindSite(windows->dummy_call, "LoadDLCs dummy call");
                    uint8_t* const earlyJump = FindSite(
                        "E8 ?? ?? ?? ?? 6A ?? 8D 44 24 ?? 50 53 56 8D 4C 24",
                        "LoadDLCs early jump");
                    uint8_t* const structFixup = FindSite(windows->fixup, "LoadDLCs struct fixup");
                    uint8_t* const skipFixupJump = FindSite(
                        "80 BE ?? ?? ?? ?? ?? 74 ?? 8B 44 24 ?? 50 E8 ?? ?? ?? ?? 83 C4 ?? ?? ?? ?? ?? 5E",
                        "LoadDLCs skip-fixup jump");
                    uint8_t* const helperCall = FindSite(
                        "E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? 5E",
                        "LoadDLCs helper call");
                    uint8_t* const tail = FindSite(
                        "8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? B0 ?? 5E C2 ?? ?? CC CC CC CC",
                        "LoadDLCs tail");

                    // Destinations as deltas from the site: +0x5C jumps into the same block,
                    // +0x1E skips the fixup (identical on 1.0.6.0/1.0.7.0).
                    if (dummyCall != nullptr)
                        injector::MakeCALL(dummyCall, OPP_BODY(abi_load_dlcs_dummy), true);
                    if (earlyJump != nullptr)
                        injector::MakeJMP(earlyJump, earlyJump + 92, true);  // 0x5C
                    if (structFixup != nullptr)
                        injector::MakeJMP(structFixup, OPP_BODY(abi_load_dlcs_struct_fixup), true);
                    if (skipFixupJump != nullptr)
                        injector::MakeJMP(skipFixupJump, skipFixupJump + 30, true);  // 0x1E
                    if (helperCall != nullptr)
                        injector::MakeCALL(helperCall, helperCall - windows->helper_before, true);
                    // The call following the tail site has twelve bytes the original NOPs.
                    if (tail != nullptr)
                        injector::MakeNOP(tail, 12, true);
                }
            };
        }
    } loadDLCs;


    // publish NewFilesCompatibility

    namespace
    {
        // NewFilesCompatibility: the registrar window (published as the slot the
        // register body calls). The two per-file update E8 hooks are 12-byte
        // prefixes that differ only in the last jNZ displacement, so each must
        // resolve once.
        constexpr std::string_view kNewFilesRegistrarWindow = "68 64 28 C8 67 E8";
    } // namespace


    // NewFilesCompatibility

    class NewFilesCompatibility
    {
    public:
        NewFilesCompatibility()
        {
            PublishEvent() += []
            {
                TraceArm("PublishNewFilesCompatibility");

                // NewFilesCompatibility: 1 slot
                if (Game::Option("NewFilesCompatibility", "1") && !NewFilesConflictModulesPresent())
                {
                    if (auto pattern = find_pattern(kNewFilesRegistrarWindow); pattern.size() > 0)
                    {
                        // The E8 to the registrar: its rel32 operand is at +6.
                        zpatch_newfiles_register_slot = BranchDestination(
                            pattern.get_first<uint8_t>(), 6);
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallNewFilesCompatibility");

                // NewFilesCompatibility: 2 siginstalls, 1 sigjump
                if (Game::Option("NewFilesCompatibility", "1") && !NewFilesConflictModulesPresent())
                {
                    if (auto pattern = find_pattern("E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 7B FF FF FF");
                        pattern.size() > 0 && ExpectMatchCount(pattern, 1, "NewFiles update A"))
                    {
                        injector::MakeCALL(pattern.get_first<uint8_t>(), OPP_BODY(abi_newfiles_update_a), true);
                    }

                    if (auto pattern = find_pattern("E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 6C FF FF FF");
                        pattern.size() > 0 && ExpectMatchCount(pattern, 1, "NewFiles update B"))
                    {
                        injector::MakeCALL(pattern.get_first<uint8_t>(), OPP_BODY(abi_newfiles_update_b), true);
                    }

                    if (auto pattern = find_pattern(kNewFilesRegistrarWindow); pattern.size() > 0)
                    {
                        // The registrar call itself is at +28 in this window.
                        injector::MakeJMP(pattern.get_first<uint8_t>() + 28, OPP_BODY(abi_newfiles_register_body), true);
                    }
                }
            };
        }
    } newFilesCompatibility;


    // publish NewSavesCompatibility

    namespace
    {
        // The tramp site (the savegame-load entry's prologue) and the save-name
        // `push imm32` in the boot window; the resume is the instruction after
        // the 13-byte prologue the tramp overwrites.
        constexpr std::string_view kNewSavesBootWindow =
            "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08";
        constexpr std::string_view kNewSavesEntryWindow =
            "89 84 24 E0 01 00 00 8B 45 08 53 33 DB";
    } // namespace


    // NewSavesCompatibility

    class NewSavesCompatibility
    {
    public:
        NewSavesCompatibility()
        {
            PublishEvent() += []
            {
                TraceArm("PublishNewSavesCompatibility");

                // NewSavesCompatibility: 3 slots. The newer save format is 1060-1080
                // only; gate the arm instead of relying on the pattern coincidentally
                // missing on 1000/1004/1040 and EFLC.
                if (Game::Option("NewSavesCompatibility", "1") &&
                    BuildIs(Game::Get().build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                        GameBuild::v1080}))
                {
                    if (auto boot = find_pattern(kNewSavesBootWindow); boot.size() > 0)
                    {
                        // The push imm32 operand is the save-name pointer.
                        zpatch_new_saves_src = *boot.get_first<uint32_t>(1);
                        if (auto pattern = find_pattern(kNewSavesEntryWindow); pattern.size() > 0)
                        {
                            zpatch_new_saves_resume = MatchAddress(pattern, 13);
                        }
                    }
                }
            };
            InstallEvent() += []
            {
                TraceArm("InstallNewSavesCompatibility");

                // NewSavesCompatibility: 1 sigjump. The savegame-load entry is only
                // redirected when both the tramp site and the entry window exist.
                if (Game::Option("NewSavesCompatibility", "1") &&
                    BuildIs(Game::Get().build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                        GameBuild::v1080}))
                {
                    if (find_pattern(kNewSavesBootWindow).size() > 0)
                    {
                        if (auto pattern = find_pattern(kNewSavesEntryWindow); pattern.size() > 0)
                        {
                            injector::MakeJMP(pattern.get_first<uint8_t>(), OPP_BODY(abi_new_saves_compat), true);
                        }
                    }
                }
            };
        }
    } newSavesCompatibility;


    // OverrideGTADatPath

    class OverrideGTADatPath
    {
    public:
        OverrideGTADatPath()
        {
            InstallEvent() += []
            {
                TraceArm("InstallOverrideGTADatPath");

                // OverrideGTADatPath: 1 literalstore
                if (Game::Option("OverrideGTADatPath", "0"))
                {
                    if (auto pattern = find_pattern("68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8");
                        pattern.size() > 0)
                    {
                        // The `push imm32` operand names the gta.dat path pointer the
                        // original pushes; the arm points it at the configured path.
                        injector::WriteMemory<uint32_t>(pattern.get_first<uint8_t>(1), static_cast<uint32_t>(
                                                 FindGlobalValue("OverrideGTADatPath")), true);
                    }
                }
            };
        }
    } overrideGTADatPath;


    // SCOSignatureFix

    class SCOSignatureFix
    {
    public:
        SCOSignatureFix()
        {
            InstallEvent() += []
            {
                TraceArm("InstallSCOSignatureFix");

                // SCOSignatureFix: 1 literal. The recovered op row anchors the write at
                // offset 0 of its own signature. The old arm scanned a signature 32 bytes
                // downstream and wrote at match-32, which is only correct while that
                // distance holds; a build where it does not would flip an unrelated `jz`.
                // This signature ends on the `jz` itself (`mov ds:X,edi; test ebp,ebp; jz`),
                // unique on 1.0.8.0.
                if (Game::Option("SCOSignatureFix", "1"))
                {
                    if (auto pattern = find_pattern("89 3D ? ? ? ? 85 ED 74 1E");
                        pattern.size() > 0)
                    {
                        // `mov ds:X,edi` (6 bytes) + `test ebp,ebp` (2) put the `jz`
                        // opcode at +8; the original flips it to a `jmp`.
                        injector::WriteMemory<uint8_t>(pattern.get_first<uint8_t>(8), opp::ops::kJmpRel8, true);
                    }
                }
            };
        }
    } sCOSignatureFix;


} // namespace opp::features
