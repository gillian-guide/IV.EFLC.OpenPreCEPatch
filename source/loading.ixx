module;

#include <common.hxx>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <shlwapi.h>
#include <unordered_set>

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

namespace
{
    // StartupTimeFix: the game's three startup-timer sites each `push` a dword
    // pointer; the original retargets every one of them (its handler collects all
    // matches) to its own timing word at the anchor's +6. The word itself is zero.
    uint32_t zpatch_startup_time = 0;
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

// NewMemorySystem, as the original handler applies it: the allocator-initialisation
// call at game 0x107A is retargeted to this module's vtable installer, and a jump
// at 0x107F skips the five-slot clear the installer replaces, resuming at 0x108C.
//
// The table is the original's CRageAllocator vtable (original 0x100B5038, 24
// slots). Ten of its methods are transcribed below as bodies; the three arena
// methods (allocate 0x10038360, free 0x10038430, "is this mine" 0x100385A0) are
// compiled code in the original and are rebuilt in C++; five slots point at
// bodies another arm already installs, because the original's table does exactly
// that (0x10038550 RGSC login, 0x10038570 savegame null path, 0x100057F0 MP sync).

namespace
{
    constexpr uint32_t kNmsVtableSlots = 24;
    constexpr uint8_t kNmsHeapPoison = 0xCD;  // the fill the original memsets with
}

extern "C"
{
    // dword_10159CD8: the address the arm reads from the allocator init's
    // `mov esi, <singleton>` operand.
    uint32_t zpatch_nms_object = 0;

    // 0x100B5038: the vtable the installer writes into that singleton.
    uint32_t zpatch_nms_vtable[kNmsVtableSlots] = {};
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
static void GfwlStubBody();
static void RgscLoginRemovedBody();
static void BenchmarkBodyA();
static void BenchmarkBodyB();
static void SavegameNullBody();
static void SavegameCloseBody();
static void SavegameMutexBody();
static void SavegameCopyBody();
static void NmsNoopRet0Body();
static void* NmsCtorBody();
static void NmsRejectPtrBody();
static void* NmsSelfBody();
static void* NmsZeroBody();
static void NmsNoopPairBody();
static void NmsRejectBody();
static void NmsSyncBody();
static void NmsNoopArgBody();
static void NmsThunk18Body();
static void NmsApplyBody();
static void NmsApplyHardenedBody();
static void NewfilesRegisterBody();
static void NewSavesReadHook();
static void NewSavesSecondLoadHook();
static void NewSavesTrampoline();
void* __cdecl DlcDiscoveryCpp(void* object);
static void DlcDummyBody();
static void DlcStructFixupBody();


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


namespace
{


    // The allocation registry. The original keeps the pointers it handed out in a
    // hash map (FNV-1a over the pointer's four bytes) behind the recursive
    // critical section at 0x101A0808; only membership is observable. The
    // `nms` group replaces the node-per-insert set with a flat open-addressing
    // table over VirtualAlloc: no allocation per game allocation, no throw in a
    // noexcept caller, and the same membership. The 1:1 path's observable
    // behaviour is unchanged either way (the set is not part of the write set).
    CRITICAL_SECTION nms_registry_lock;

    class NmsRegistry final
    {
    public:
        NmsRegistry() = default;
        NmsRegistry(const NmsRegistry&) = delete;
        NmsRegistry& operator=(const NmsRegistry&) = delete;
        ~NmsRegistry() { Release(); }

        bool Insert(uint32_t key) noexcept
        {
            if (key <= kErased)
                return false;
            if (slots_ == nullptr || (used_ + 1) * 4 >= capacity_ * 3)
            {
                if (!Grow())
                    return false;
            }
            return InsertNoGrow(key);
        }

        bool Erase(uint32_t key) noexcept
        {
            if (slots_ == nullptr)
                return false;
            const size_t mask = capacity_ - 1;
            for (size_t slot = Hash(key) & mask;; slot = (slot + 1) & mask)
            {
                const uint32_t value = slots_[slot];
                if (value == kEmpty)
                    return false;
                if (value == key)
                {
                    slots_[slot] = kErased;
                    --size_;
                    return true;
                }
            }
        }

        bool Contains(uint32_t key) const noexcept
        {
            if (slots_ == nullptr)
                return false;
            const size_t mask = capacity_ - 1;
            for (size_t slot = Hash(key) & mask;; slot = (slot + 1) & mask)
            {
                const uint32_t value = slots_[slot];
                if (value == kEmpty)
                    return false;
                if (value == key)
                    return true;
            }
        }

        size_t Size() const noexcept { return size_; }
        size_t Capacity() const noexcept { return capacity_; }

    private:
        static constexpr uint32_t kEmpty = 0u;
        static constexpr uint32_t kErased = 1u;
        static constexpr size_t kInitialSlots = 1024u;

        static size_t Hash(uint32_t key) noexcept
        {
            return static_cast<size_t>(key * 2654435761u);
        }

        bool InsertNoGrow(uint32_t key) noexcept
        {
            const size_t mask = capacity_ - 1;
            size_t erased = SIZE_MAX;
            for (size_t slot = Hash(key) & mask;; slot = (slot + 1) & mask)
            {
                const uint32_t value = slots_[slot];
                if (value == key)
                    return true;
                if (value == kErased)
                {
                    if (erased == SIZE_MAX)
                        erased = slot;
                    continue;
                }
                if (value == kEmpty)
                {
                    if (erased != SIZE_MAX)
                    {
                        slots_[erased] = key;
                    }
                    else
                    {
                        slots_[slot] = key;
                        ++used_;
                    }
                    ++size_;
                    return true;
                }
            }
        }

        bool Grow() noexcept
        {
            const size_t freshCapacity = capacity_ == 0 ? kInitialSlots : capacity_ * 2;
            auto* fresh = static_cast<uint32_t*>(VirtualAlloc(nullptr,
                freshCapacity * sizeof(uint32_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (fresh == nullptr)
                return false;

            auto* previous = slots_;
            const size_t previousCapacity = capacity_;
            slots_ = fresh;
            capacity_ = freshCapacity;
            size_ = 0;
            used_ = 0;
            for (size_t slot = 0; slot < previousCapacity; ++slot)
            {
                const uint32_t value = previous[slot];
                if (value > kErased)
                    (void)InsertNoGrow(value);
            }
            if (previous != nullptr)
                VirtualFree(previous, 0, MEM_RELEASE);
            return true;
        }

        void Release() noexcept
        {
            if (slots_ != nullptr)
                VirtualFree(slots_, 0, MEM_RELEASE);
            slots_ = nullptr;
            capacity_ = 0;
            size_ = 0;
            used_ = 0;
        }

        uint32_t* slots_ = nullptr;
        size_t capacity_ = 0;
        size_t size_ = 0;
        size_t used_ = 0;  // occupied + tombstones
    };

    NmsRegistry nms_allocations;

    // The original simple allocator answers "is this pointer mine?" by *range*:
    // `sub_454FB0` is `ptr >= block + 16 && ptr < block + size`. NMS answered by
    // exact allocation start, so a pointer that was an interior pointer - or a
    // pointer already freed - made `sysMemMultiAllocator::free` (0x455FF0) find
    // no owner and call `lmsgboxExit("MMA10")` (0x456021). That is a fatal
    // mis-owned free, not an out-of-memory code: the 2026-11-04 minidump has the
    // freed pointer and the MMA10 string on the same stack.
    //
    // The group now keeps the original's *range* answer at page granularity: one
    // bit per 4 KiB page of the 32-bit address space (128 KiB, allocated once).
    // A page is marked when an NMS block covers it and stays marked, like the
    // original's reserved arena, whose free space is still "mine". `free` still
    // uses the exact registry: an exact, live pointer is freed; a pointer that
    // is only inside a marked page is a no-op (the original would have routed it
    // to the same allocator, but calling HeapFree on an interior/double-freed
    // address would corrupt the heap).
    std::atomic<uint8_t*> nms_page_map{ nullptr };
    std::once_flag nms_page_map_once;

    constexpr size_t kNmsPageShift = 12;                 // 4 KiB pages
    constexpr size_t kNmsPageCount = 1u << (32 - kNmsPageShift);
    constexpr size_t kNmsPageMapBytes = kNmsPageCount / 8;  // 128 KiB

    uint8_t* NmsPageMap() noexcept
    {
        std::call_once(nms_page_map_once, []
        {
            nms_page_map.store(static_cast<uint8_t*>(VirtualAlloc(nullptr,
                kNmsPageMapBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)));
        });
        return nms_page_map.load(std::memory_order_acquire);
    }

    void MarkNmsPages(uintptr_t begin, size_t bytes) noexcept
    {
        auto* map = NmsPageMap();
        if (map == nullptr || bytes == 0)
            return;
        const size_t first = begin >> kNmsPageShift;
        const size_t last = (begin + bytes - 1) >> kNmsPageShift;
        for (size_t page = first; page <= last && page < kNmsPageCount; ++page)
            map[page >> 3] = static_cast<uint8_t>(map[page >> 3] | (1u << (page & 7)));
    }

    bool NmsPageIsMine(uintptr_t address) noexcept
    {
        const auto* map = nms_page_map.load(std::memory_order_acquire);
        if (map == nullptr)
            return false;
        const size_t page = address >> kNmsPageShift;
        if (page >= kNmsPageCount)
            return false;
        return (map[page >> 3] & (1u << (page & 7))) != 0;
    }

    // The private heap: NMS allocations stay off the CRT heap FusionFix and
    // LibertyCityPlates use. A failed HeapCreate falls back to the process heap.
    HANDLE nms_heap = nullptr;
    std::once_flag nms_heap_once;

    HANDLE NmsHeap() noexcept
    {
        std::call_once(nms_heap_once, [] { nms_heap = HeapCreate(0, 0, 0); });
        return nms_heap != nullptr ? nms_heap : GetProcessHeap();
    }

    // The allocator object's pre-swap vtable, captured by the `nms` group's apply
    // body before it writes ours. A pointer allocated before the swap is not in
    // the registry and must be handed back to the allocator that produced it.
    void** nms_original_object = nullptr;
    uintptr_t nms_original_vtable[kNmsVtableSlots] = {};

    void NmsCaptureOriginal() noexcept
    {
        auto* object = reinterpret_cast<void**>(static_cast<uintptr_t>(zpatch_nms_object));
        if (object == nullptr || nms_original_object != nullptr)
            return;
        nms_original_object = object;
        if (object[0] != nullptr)
            std::memcpy(nms_original_vtable, object[0], sizeof(nms_original_vtable));
    }

    // The one-time setup of the registry lock, shared by the two paths that reach
    // it: PublishNewMemorySystem (the game's allocator-init, once per process) and
    // the test-only seam below (tests/nms_arena.cpp). A second call is a no-op, so
    // neither path can initialise the section twice.
    void PrepareArenaRegistry()
    {
        static std::once_flag prepared;
        std::call_once(prepared, [] { InitializeCriticalSection(&nms_registry_lock); });
    }

    // sub_10038360, recovered VA 0x10038360: the vtable's allocate slot
    // (__stdcall, three arguments). Assembly in the original: unit =
    // max(0x10, align), want = max(size, unit), HeapAlloc(want + 2 * unit),
    // align up to ((base - 1) / unit + 2) * unit, store the distance from the
    // base at [pointer - 4], fill `want` bytes with 0xCD, register the pointer.
    // ECX is the object, which the original's body ignores too.
    void* __stdcall NmsAllocateBody(uint32_t size, uint32_t align, uint32_t flags)
    {
        (void)flags;
        // The arena's own alignment is 16 bytes; a caller-supplied alignment
        // above 1 MiB is not a request this allocator is meant to serve, and
        // any arithmetic on it must not wrap before the bound check.
        if (align > 0x100000u)
            return nullptr;
        const uint64_t unit = align < 0x10u ? 0x10u : align;
        const uint64_t want = size < unit ? unit : size;
        const uint64_t total = want + unit * 2u;
        if (total > UINT32_MAX)
            return nullptr;
        void* base = HeapAlloc(NmsHeap(), 0, static_cast<SIZE_T>(total));
        if (base == nullptr)
            return nullptr;

        const auto raw = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base));
        const uint64_t aligned64 = ((static_cast<uint64_t>(raw) - 1u) / unit + 2u) * unit;
        if (aligned64 > UINT32_MAX || aligned64 + want > UINT32_MAX + 1ull)
        {
            HeapFree(NmsHeap(), 0, base);
            return nullptr;
        }
        const auto aligned = static_cast<uint32_t>(aligned64);
        *reinterpret_cast<uint32_t*>(aligned - 4u) = aligned - raw;
        std::memset(reinterpret_cast<void*>(aligned), kNmsHeapPoison, static_cast<size_t>(want));

        EnterCriticalSection(&nms_registry_lock);
        (void)nms_allocations.Insert(aligned);
        MarkNmsPages(raw, static_cast<size_t>(total));
        LeaveCriticalSection(&nms_registry_lock);
        return reinterpret_cast<void*>(aligned);
    }

    // sub_10038430, recovered VA 0x10038430: the free slot (__stdcall, one
    // argument). The base is recovered from the distance stored at [ptr - 4].
    void __stdcall NmsFreeBody(void* pointer)
    {
        if (pointer == nullptr)
            return;

        const auto raw = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));

        // The page bitmap is read under the same lock that marks it.
        bool mine = false;
        bool pageOwned = false;
        EnterCriticalSection(&nms_registry_lock);
        mine = nms_allocations.Contains(raw);
        if (mine)
            (void)nms_allocations.Erase(raw);
        pageOwned = NmsPageIsMine(raw);
        LeaveCriticalSection(&nms_registry_lock);

        if (!mine)
        {
            // A pointer that was already freed, or an interior pointer into
            // a live NMS block: the original's range answer was "mine", so
            // `sysMemMultiAllocator::free` routed it here. Answering the
            // range truth and then not calling HeapFree keeps the game
            // alive; the block is freed by its real owner (or leaked if it
            // was a double free), never freed twice.
            if (pageOwned)
            {
                Log::Print("nms: free of a non-exact pointer {:#010x} inside an "
                           "NMS block; ignored (MMA10 avoided)", raw);
                return;
            }
            if (nms_original_object != nullptr && nms_original_vtable[3] != 0)
            {
                using OriginalFree = void(__thiscall*)(void*, void*);
                reinterpret_cast<OriginalFree>(nms_original_vtable[3])(
                    nms_original_object, pointer);
            }
            return;
        }

        const auto base = raw - *reinterpret_cast<const uint32_t*>(raw - 4u);
        HeapFree(NmsHeap(), 0, reinterpret_cast<void*>(base));
        return;
    }

    // sub_100385A0, recovered VA 0x100385A0: the "is this pointer mine" query
    // (__stdcall, one argument, returns AL).
    uint8_t __stdcall NmsContainsBody(uint32_t key)
    {
        EnterCriticalSection(&nms_registry_lock);
        const bool found = nms_allocations.Contains(key) || NmsPageIsMine(key);
        LeaveCriticalSection(&nms_registry_lock);
        return found ? uint8_t{ 1 } : uint8_t{ 0 };
    }

    // 0x100B5038's entry order. Slots 4/5, 6/7, 8/9, 11/14/17, 15/16/23 and
    // 20/21 repeat an entry, exactly as the original's table does.
    void PublishNmsVtable()
    {
        const uint32_t entries[kNmsVtableSlots] = {
            reinterpret_cast<uint32_t>(&NmsCtorBody),           // [0]  0x10038640
            reinterpret_cast<uint32_t>(&NmsRejectPtrBody),      // [1]  0x10038350
            reinterpret_cast<uint32_t>(&NmsAllocateBody),       // [2]  0x10038360
            reinterpret_cast<uint32_t>(&NmsFreeBody),           // [3]  0x10038430
            reinterpret_cast<uint32_t>(&NmsSelfBody),           // [4]  0x10038540
            reinterpret_cast<uint32_t>(&NmsSelfBody),           // [5]  0x10038540
            reinterpret_cast<uint32_t>(&NmsZeroBody),           // [6]  0x10001950
            reinterpret_cast<uint32_t>(&NmsZeroBody),           // [7]  0x10001950
            reinterpret_cast<uint32_t>(&NmsSyncBody),           // [8]  0x100057F0
            reinterpret_cast<uint32_t>(&NmsSyncBody),           // [9]  0x100057F0
            reinterpret_cast<uint32_t>(&RgscLoginRemovedBody),  // [10] 0x10038550
            reinterpret_cast<uint32_t>(&NmsNoopRet0Body),       // [11] 0x10038560
            reinterpret_cast<uint32_t>(&SavegameNullBody),      // [12] 0x10038570
            reinterpret_cast<uint32_t>(&NmsNoopPairBody),       // [13] 0x10038580
            reinterpret_cast<uint32_t>(&NmsNoopRet0Body),       // [14] 0x10038560
            reinterpret_cast<uint32_t>(&NmsRejectBody),         // [15] 0x10038590
            reinterpret_cast<uint32_t>(&NmsRejectBody),         // [16] 0x10038590
            reinterpret_cast<uint32_t>(&NmsNoopRet0Body),       // [17] 0x10038560
            reinterpret_cast<uint32_t>(&NmsContainsBody),       // [18] 0x100385A0
            reinterpret_cast<uint32_t>(&NmsThunk18Body),        // [19] 0x10038620
            reinterpret_cast<uint32_t>(&NmsSyncBody),           // [20] 0x100057F0
            reinterpret_cast<uint32_t>(&NmsSyncBody),           // [21] 0x100057F0
            reinterpret_cast<uint32_t>(&NmsNoopArgBody),        // [22] 0x10038630
            reinterpret_cast<uint32_t>(&NmsRejectBody),         // [23] 0x10038590
        };
        std::memcpy(zpatch_nms_vtable, entries, sizeof(entries));
    }
}

// The test-only seam over the three arena methods of the NewMemorySystem vtable
// (0x100B5038: allocate 0x10038360, free 0x10038430, membership 0x100385A0). They
// are compiled into this unit's anonymous namespace and reached in the game only
// through the table PublishNmsVtable fills; a rebuild's evidence is behaviour, so
// tests/nms_arena.cpp (ctest `zp_nms_arena`) drives them directly with no game
// image. Get() names the same three functions the table publishes and runs the
// registry lock's one-time setup - nothing else: no site is written, no arm is
// installed, and the published table is untouched. The only module state it can
// change is that lock's initialisation, which the game's own allocator-init performs
// anyway; the shipped ASI never calls it.

export namespace MemoryArena
{
    // The ABIs are the originals', which is part of what the caller sees:
    //   allocate 0x10038360  __stdcall, `ret 0Ch`, three arguments, ECX ignored
    //                        (the object the vtable passes in ECX is unused);
    //   release  0x10038430  __stdcall, `ret 4`, one argument, null is a no-op
    //                        (`test edx,edx / je` before anything else);
    //   contains 0x100385A0  __stdcall, `ret 4`, answers in AL only: the body ends
    //                        `cmp edi,esi / setne al`, so EAX's upper 24 bits are
    //                        whatever LeaveCriticalSection left there and only the
    //                        byte is contractual.
    struct Methods
    {
        void* (__stdcall* allocate)(uint32_t size, uint32_t align, uint32_t flags);
        void (__stdcall* release)(void* pointer);
        uint8_t (__stdcall* contains)(uint32_t key);
        HANDLE(__cdecl* heap)();
    };

    // The three entries, with the registry lock prepared; see Methods.
    export Methods Get();
}

MemoryArena::Methods MemoryArena::Get()
{
    PrepareArenaRegistry();
    return { &NmsAllocateBody, &NmsFreeBody, &NmsContainsBody, &NmsHeap };
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
    // bytes including the terminator.
    inline bool DlcPathFits(std::string_view path)
    {
        return !path.empty() && path.size() < 0x104;
    }
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
        {
            // A short header is not a save this build can read.
            if (std::fread(header, kNewSavesHeaderSize, 1, stream) != 1)
            {
                std::fclose(stream);
                return Loading::SaveWalkEnd::rejected;
            }
        }

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
            if (std::fread(chunkHeader, kNewSavesChunkHeaderSize, 1, stream) != 1)
            {
                std::fclose(stream);
                return Loading::SaveWalkEnd::rejected;
            }

            uint32_t size = 0;
            std::memcpy(&size, chunkHeader + kNewSavesChunkSizeOffset, sizeof(size));

            if (size < static_cast<uint32_t>(kNewSavesChunkHeaderSize))
            {
                std::fclose(stream);
                return Loading::SaveWalkEnd::rejected;
            }

            const uint32_t payload = size - static_cast<uint32_t>(kNewSavesChunkHeaderSize);
            // The scan only needs the headers; seek over the payload. A failed
            // seek means the file is shorter than its own chunk table.
            if (payload > static_cast<uint32_t>(std::numeric_limits<long>::max()) ||
                std::fseek(stream, static_cast<long>(payload), SEEK_CUR) != 0)
            {
                std::fclose(stream);
                return Loading::SaveWalkEnd::rejected;
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

    void PublishNewMemorySystem()
    {
        if (!Loader::Enabled("NewMemorySystem"))
            return;

        // The allocator init's `mov esi, <singleton>`: the operand is the address
        // the installer writes the vtable at.
        auto pattern = find_pattern("6A 01 B9 00 00 40 06");
        if (!pattern.empty())
            zpatch_nms_object = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                GamePointer<std::byte>(*pattern.get_first<uint32_t>(8))));

        // The registry lock and the table are this module's own state: the game
        // initialises them once, and the offline tools call the pipeline once per
        // dump cell, so the one-time flag keeps a second initialisation off the
        // section (nothing about the game's behaviour depends on it).
        PrepareArenaRegistry();
        static bool prepared = false;
        if (!prepared)
        {
            prepared = true;
            PublishNmsVtable();
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
}

namespace
{
    // FastLoading: the ten-millisecond sleep the streaming code takes between
    // batches. The arm zeroes the pushed `sleep(10)` argument; the original handler
    // writes the one byte at the anchor's +1.
    void InstallFastLoading()
    {
        if (!Loader::Enabled("FastLoading"))
            return;

        auto pattern = find_pattern("6A 0A E8 ? ? ? ? 83 C7 0A 83 C4 04 81 FF E8 03 00 00 75 02 33 FF");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first<std::byte>(1), Bytes::Zero, true);
    }

    // NoLoadingSleep: the loading screen's five-second `Sleep(5000)` becomes
    // `Sleep(0)`. The arm zeroes the pushed immediate at the anchor's +1.
    void InstallNoLoadingSleep()
    {
        if (!Loader::Enabled("NoLoadingSleep"))
            return;

        auto pattern = find_pattern("68 88 13 00 00 FF 15");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first<uint32_t>(1), 0u, true);
    }

    // RemoveGFWLUpdatePopup: the anchor's first twelve bytes are the subtraction
    // and its two stores; the stub replays them and resumes at match + 12.
    void InstallRemoveGFWLUpdatePopup()
    {
        if (!Loader::Enabled("RemoveGFWLUpdatePopup"))
            return;

        // dword_10159C10: the xlive.dll module the original's loader captures.
        zpatch_xlive_base =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GetModuleHandleA("xlive.dll")));

        // The stub replays into xlive.dll through the captured base. Without the
        // module the original writes through zero; keep the game's popup instead.
        if (Failsafe::Missing("RemoveGFWLUpdatePopup xlive.dll", zpatch_xlive_base == 0))
            return;

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

    void InstallStartupTimeFix()
    {
        if (!Loader::Enabled("StartupTimeFix"))
            return;

        // Three sites on 1.0.8.0: the scan hint has to ask for all of them (the
        // scanner stops at the hint).
        auto pattern = find_pattern<3>("68 00 C2 EC 80 68");
        if (!pattern.empty())
        {
            for (size_t index = 0; index < pattern.size(); ++index)
            {
                injector::WriteMemory<uint32_t>(pattern.get(index).get<std::byte>(6),
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_startup_time)), true);
                Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get(index).get<std::byte>(6)));
            }
        }
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
        if (!pattern.empty() && ExpectMatchCount(pattern, 2, "BenchmarkFix entries"))
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


    void InstallNewMemorySystem()
    {
        if (!Loader::Enabled("NewMemorySystem"))
            return;

        auto pattern = find_pattern("3D 80 99 99 59 73 05");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            // -34 is the call the original retargets, -29 the five-slot clear's
            // first store; the jump resumes four instructions later at -16.
            // The group uses a body that captures the pre-swap vtable first; the
            // call's observable write is the same vtable dword.
            void* body = reinterpret_cast<void*>(&NmsApplyHardenedBody);
            injector::MakeCALL(match - 34, body, true);
            injector::MakeJMP(match - 29, reinterpret_cast<void*>(match - 16), true);
        }
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
            if (length == 0 || !Loading::DlcPathFits(std::string_view(modulePath, length)))
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

// Registered like every other arm: the install tier owns the gate.
export void* DlcDiscoveryForTest(void* object) noexcept
{
    return DlcDiscoveryCpp(object);
}

export void SetDlcRvasForTest(uint32_t registerRva, uint32_t applyRva) noexcept
{
    zpatch_dlc_register_rva = registerRva;
    zpatch_dlc_apply_rva = applyRva;
}

export void RegisterLoading()
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

    Loader::PublishEvent() += []
    {
        CopyOverrideDatPath();
        PublishNewMemorySystem();
        PublishNewFilesCompatibility();
        PublishNewSavesCompatibility();
        PublishLoadDLCs();
    };
    Loader::InstallEvent() += []
    {
        InstallFastLoading();
        InstallNoLoadingSleep();
        InstallRemoveGFWLUpdatePopup();
        InstallRemoveRGSCLoginRequirement();
        InstallStartupTimeFix();
        InstallVSyncFix();
        InstallBenchmarkFix();
        InstallOverrideGTADatPath();
        InstallSavegameFix();
        InstallNewMemorySystem();
        InstallNewFilesCompatibility();
        InstallNewSavesCompatibility();
        InstallLoadDLCs();
    };

    // The hook bodies carry their recovered VAs so the dump can express every
    // branch in the original's coordinates.
    Bodies::Register(reinterpret_cast<uintptr_t>(&GfwlStubBody), 0x100054E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&RgscLoginRemovedBody), 0x10038550u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BenchmarkBodyA), 0x10005650u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&BenchmarkBodyB), 0x10005670u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameNullBody), 0x10038570u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameCloseBody), 0x10045580u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameCopyBody), 0x100455A0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SavegameMutexBody), 0x100455E0u);

    // NewMemorySystem's ten transcribed methods and the three rebuilt arena
    // methods of its vtable (0x100B5038).
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsCtorBody), 0x10038640u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsRejectPtrBody), 0x10038350u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsSelfBody), 0x10038540u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsZeroBody), 0x10001950u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsNoopRet0Body), 0x10038560u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsNoopPairBody), 0x10038580u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsRejectBody), 0x10038590u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsSyncBody), 0x100057F0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsThunk18Body), 0x10038620u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsNoopArgBody), 0x10038630u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsApplyHardenedBody), 0x10038670u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsAllocateBody), 0x10038360u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsFreeBody), 0x10038430u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NmsContainsBody), 0x100385A0u);

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

    // LoadDLCs' two bodies, and the discovery pass the fixup body calls (a rebuild,
    // so it carries no proof row).
    Bodies::Register(reinterpret_cast<uintptr_t>(&DlcDummyBody), 0x10023480u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&DlcStructFixupBody), 0x10023490u);
    // The C++ discovery takes the transcribed body's role; the reference keeps
    // the fixup body's call target provable against the original.
    Bodies::Register(reinterpret_cast<uintptr_t>(&DlcDiscoveryCpp), 0x10023150u);

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



// sub_10038560, recovered VA 0x10038560 (1 instruction): the table's release
// hook. It cleans no stack argument (`ret 0`), which is why it is written as an
// immediate rather than a plain `ret` (that is `ret`, opcode C3).
static __declspec(naked) void NmsNoopRet0Body()
{
    __asm
    {
        ret 0
    }
}

// sub_10038640, recovered VA 0x10038640 (17 instructions): the CRageAllocator
// constructor, __thiscall with one stack argument the callee pops. It installs
// this module's vtable in the object and, for the `& 5 == 5` selector, runs the
// release hook on a stack scratch slot.
static __declspec(naked) void* NmsCtorBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 8]
        push esi
        mov esi, ecx
        and eax, 5
        mov DWORD PTR [esi], OFFSET zpatch_nms_vtable
        cmp al, 5
        jne nms_ctor_done
        push 4
        push esi
        call NmsNoopRet0Body
        add esp, 8
    nms_ctor_done:
        mov eax, esi
        pop esi
        pop ebp
        ret 4
    }
}

// sub_10038350, recovered VA 0x10038350 (2 instructions): reject a pointer.
static __declspec(naked) void NmsRejectPtrBody()
{
    __asm
    {
        xor al, al
        ret 4
    }
}

// sub_10038540, recovered VA 0x10038540 (2 instructions): the "self" slot.
static __declspec(naked) void* NmsSelfBody()
{
    __asm
    {
        mov eax, ecx
        ret 4
    }
}

// sub_10001950, recovered VA 0x10001950 (2 instructions): the null slot.
static __declspec(naked) void* NmsZeroBody()
{
    __asm
    {
        xor eax, eax
        ret 4
    }
}

// sub_10038580, recovered VA 0x10038580 (1 instruction): paired no-op, two
// stack arguments.
static __declspec(naked) void NmsNoopPairBody()
{
    __asm
    {
        ret 8
    }
}

// sub_10038590, recovered VA 0x10038590 (2 instructions): the reject slot. It
// sets only AL, so the upper 24 bits of EAX stay whatever the caller had.
static __declspec(naked) void NmsRejectBody()
{
    __asm
    {
        xor al, al
        ret
    }
}

// sub_100057F0, recovered VA 0x100057F0 (2 instructions): the MP-sync stub, which
// the original's table shares with BetterMPSync. audio.ixx owns that body but
// keeps it inside its own unit, so the table gets this copy of the same two
// instructions.
static __declspec(naked) void NmsSyncBody()
{
    __asm
    {
        xor eax, eax
        ret
    }
}

// sub_10038630, recovered VA 0x10038630 (1 instruction): no-op with one stack
// argument.
static __declspec(naked) void NmsNoopArgBody()
{
    __asm
    {
        ret 4
    }
}

// sub_10038620, recovered VA 0x10038620 (5 instructions): forward through the
// object's slot 18. The tail jump keeps the caller's return address and EBP.
static __declspec(naked) void NmsThunk18Body()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ecx]
        pop ebp
        jmp DWORD PTR [eax + 18h]
    }
}

// sub_10038670, recovered VA 0x10038670 (5 instructions): the installer. It
// writes this module's vtable into the published singleton and returns without
// cleaning a stack argument (the site's call is replaced wholesale).
static __declspec(naked) void NmsApplyBody()
{
    __asm
    {
        mov eax, DWORD PTR [zpatch_nms_object]
        test eax, eax
        je nms_apply_done
        mov DWORD PTR [eax], OFFSET zpatch_nms_vtable
    nms_apply_done:
        ret
    }
}

// The `nms` group's apply body: capture the pre-swap vtable (for the foreign
// pointer free path), then install ours exactly as the 1:1 body does. pushad/
// popad keeps the caller's registers, which the five-instruction original also
// leaves untouched.
static __declspec(naked) void NmsApplyHardenedBody()
{
    __asm
    {
        __emit 0x60  // pushad
        call NmsCaptureOriginal
        __emit 0x61  // popad
        mov eax, DWORD PTR [zpatch_nms_object]
        test eax, eax
        je nms_apply_hardened_done
        mov DWORD PTR [eax], OFFSET zpatch_nms_vtable
    nms_apply_hardened_done:
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

using DlcRegisterFn = void(__cdecl*)(int, const char*, int);
using DlcApplyFn = void(__cdecl*)(void*, const char*);

// The apply function returns its second value in EDX. Capture it for the one
// caller that observes it (an existing \Episode254\).
static __declspec(naked) void DlcApplyCapture(void* object, const char* path,
    DlcApplyFn apply, uint32_t* out)
{
    __asm
    {
        push ebp
        mov ebp, esp
        push DWORD PTR [ebp + 0Ch]
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
        DlcApplyCapture(object, request.path, applyFn, &lastApplyEdx);
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
