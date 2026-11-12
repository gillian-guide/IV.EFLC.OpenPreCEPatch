module;

#include <common.hxx>
#include <cstring>

export module performance;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import options;
import patch;
import shared;

// PERFORMANCE: StartupTimeFix, FastLoading, NoLoadingSleep, ImprovedShaderStreaming
// and NewMemorySystem. The load-time performance arms came from `loading` (the
// startup timers, the two sleeps, NewMemorySystem) and ImprovedShaderStreaming from
// `graphics`, whose handler carve-out it was.
export namespace Performance
{
    void Prepare();

    void PublishNewMemorySystem();

    void InstallFastLoading();
    void InstallNoLoadingSleep();
    void InstallStartupTimeFix();
    void InstallImprovedShaderStreaming();
    void InstallNewMemorySystem();
}

namespace
{
    // StartupTimeFix: the game's three startup-timer sites each `push` a dword
    // pointer; the original retargets every one of them (its handler collects all
    // matches) to its own timing word at the anchor's +6. The word itself is zero.
    uint32_t zpatch_startup_time = 0;
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

// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488), so the readable code above them names
// each body through a plain prototype; only the definition in the tail keeps the
// attribute.
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
static void ShaderStreamingFilterHpe();
static void ShaderStreamingFilterV();
static void ShaderStreamingFindHpe();
static void ShaderStreamingFindV();

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
    //   allocate 0x10038360  __stdcall, `ret 0Ch`, three arguments, ECX ignored;
    //   release  0x10038430  __stdcall, `ret 4`, one argument, null is a no-op;
    //   contains 0x100385A0  __stdcall, `ret 4`, answers in AL only.
    struct Methods
    {
        void* (__stdcall* allocate)(uint32_t size, uint32_t align, uint32_t flags);
        void (__stdcall* release)(void* pointer);
        uint8_t (__stdcall* contains)(uint32_t key);
        HANDLE(__cdecl* heap)();
    };

    // The three entries, with the registry lock prepared; see Methods.
    Methods Get();
}

namespace
{
    // A captured pattern operand is a 32-bit VA of the image *as loaded*: the
    // loader has already applied the image's relocations, so the dword is a live
    // address and the cast is the whole conversion.
    template <typename T>
    T* GamePointer(uint32_t value)
    {
        return reinterpret_cast<T*>(static_cast<uintptr_t>(value));
    }
}

// The allocation registry, the private heap and the three arena methods.

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
            static_cast<uint32_t>(Bodies::AddressFor(0x10038550u)),  // [10] 0x10038550
            reinterpret_cast<uint32_t>(&NmsNoopRet0Body),       // [11] 0x10038560
            static_cast<uint32_t>(Bodies::AddressFor(0x10038570u)),      // [12] 0x10038570
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

MemoryArena::Methods MemoryArena::Get()
{
    PrepareArenaRegistry();
    return { &NmsAllocateBody, &NmsFreeBody, &NmsContainsBody, &NmsHeap };
}

namespace Performance
{
    void Prepare()
    {
        // The ten transcribed methods and the three rebuilt arena methods of the
        // allocator vtable (0x100B5038). The table's other five slots are the RGSC
        // login, savegame-null and MP-sync bodies the original's table also points
        // at; those are registered by their own sections, and PublishNmsVtable seeks
        // them through the body registry.
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
    }
}

namespace Performance
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

    // ImprovedShaderStreaming: the name-by-name LookupShader walks become one call
    // each into a shader-array walk with a kind filter, and the rest of the old
    // chain is jumped over. The original's handler sub_10045AD0 writes the four
    // sites at match + 0x55 (E8 body A), + 0x62 (E9, displacement 0x8E), + 0xFD
    // (E8 body B) and + 0x10A (E9, displacement 0x377); the displacement it writes
    // is the constant but it encodes exactly the two targets below, so the jumps
    // are computed rather than hardcoded. The finders are the bodies the calls
    // name; the filters are only called by the finders, so they appear in the body
    // manifest rather than as branch targets.
    void InstallImprovedShaderStreaming()
    {
        if (!Loader::Enabled("ImprovedShaderStreaming"))
            return;

        auto pattern = find_pattern(
            "33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        Bodies::Register(reinterpret_cast<uintptr_t>(&ShaderStreamingFindHpe), 0x10045A30u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&ShaderStreamingFindV), 0x10045A80u);

        // Only the two LookupShader calls are replaced. The two jumps over the
        // remaining name chain are a pure speed-up, and the bytes they overwrite
        // are inside the window LibertyCityPlates scans for
        // addCustomShaderEffect; dropping them keeps that mod's address scan
        // intact.
        //
        // A build can match the surrounding bytes without the two call opcodes;
        // writing a branch over an arbitrary instruction is worse than not
        // applying the speed-up.
        if (match[85] != std::byte{ 0xE8 } || match[253] != std::byte{ 0xE8 })
            return;

        injector::MakeCALL(match + 85, reinterpret_cast<void*>(&ShaderStreamingFindHpe), true);
        injector::MakeCALL(match + 253, reinterpret_cast<void*>(&ShaderStreamingFindV), true);
    }

}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

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
// the original's table shares with BetterMPSync. multiplayer.ixx owns that body but
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

// sub_100459E0, recovered VA 0x100459E0: the HPE filter the shader finder calls
// with the shader-name object. The finder takes the entry whose filter result is 0,
// so 0 means "this is the name kind we want": the kind byte 'h', or 'p' followed by
// 'e'. Every other name returns 2, which the finder reads as "keep looking".
static __declspec(naked) void ShaderStreamingFilterHpe()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        mov cl, BYTE PTR [eax + 4]
        cmp cl, 68h
        jz hpe_match
        cmp cl, 70h
        jnz hpe_skip
        cmp BYTE PTR [eax + 5], 65h
        jz hpe_match
    hpe_skip:
        mov eax, 2
        pop ebp
        ret
    hpe_match:
        xor eax, eax
        pop ebp
        ret
    }
}

// sub_10045A10, recovered VA 0x10045A10: the V filter, the same 0-for-match /
// 2-for-skip contract for the single kind byte 'v'.
static __declspec(naked) void ShaderStreamingFilterV()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov ecx, DWORD PTR [ebp + 0Ch]
        xor edx, edx
        mov eax, 2
        cmp BYTE PTR [ecx + 4], 76h
        cmovz eax, edx
        pop ebp
        ret
    }
}

// sub_10045A30, recovered VA 0x10045A30: the replacement for the
// `LookupShader("gta_ped")` call (the arm's second write then jumps over the
// following ped-name lookups). It walks the shader group's array (count at +0Ch,
// array at +8), asks each shader's virtual +0x10 for its name object and takes
// the first one the HPE filter reports as the wanted kind (a 0 result; 2 means
// keep looking), or returns null. `ret 4` because the caller pushed two arguments.
static __declspec(naked) void ShaderStreamingFindHpe()
{
    __asm
    {
        push ebx
        push esi
        push edi
        mov edi, ecx
        xor esi, esi
        cmp WORD PTR [edi + 0Ch], si
        jbe hpe_none
        mov ebx, DWORD PTR [esp + 10h]
    hpe_loop:
        mov eax, DWORD PTR [edi + 8]
        mov ecx, DWORD PTR [eax + esi * 4]
        test ecx, ecx
        jz hpe_advance
        mov edx, DWORD PTR [ecx]
        mov eax, DWORD PTR [edx + 10h]
        call eax
        push eax
        push ebx
        call ShaderStreamingFilterHpe
        add esp, 8
        test eax, eax
        jz hpe_found
    hpe_advance:
        movzx ecx, WORD PTR [edi + 0Ch]
        add esi, 1
        cmp esi, ecx
        jl hpe_loop
    hpe_none:
        pop edi
        pop esi
        xor eax, eax
        pop ebx
        ret 4
    hpe_found:
        mov edx, DWORD PTR [edi + 8]
        mov eax, DWORD PTR [edx + esi * 4]
        pop edi
        pop esi
        pop ebx
        ret 4
    }
}

// sub_10045A80, recovered VA 0x10045A80: the same walk for the V filter, the
// replacement for the `LookupShader("gta_vehicle_default")` call (the arm's fourth
// write then jumps to the end of the shader chain).
static __declspec(naked) void ShaderStreamingFindV()
{
    __asm
    {
        push ebx
        push esi
        push edi
        mov edi, ecx
        xor esi, esi
        cmp WORD PTR [edi + 0Ch], si
        jbe v_none
        mov ebx, DWORD PTR [esp + 10h]
    v_loop:
        mov eax, DWORD PTR [edi + 8]
        mov ecx, DWORD PTR [eax + esi * 4]
        test ecx, ecx
        jz v_advance
        mov edx, DWORD PTR [ecx]
        mov eax, DWORD PTR [edx + 10h]
        call eax
        push eax
        push ebx
        call ShaderStreamingFilterV
        add esp, 8
        test eax, eax
        jz v_found
    v_advance:
        movzx ecx, WORD PTR [edi + 0Ch]
        add esi, 1
        cmp esi, ecx
        jl v_loop
    v_none:
        pop edi
        pop esi
        xor eax, eax
        pop ebx
        ret 4
    v_found:
        mov edx, DWORD PTR [edi + 8]
        mov eax, DWORD PTR [edx + esi * 4]
        pop edi
        pop esi
        pop ebx
        ret 4
    }
}
