// SPDX-License-Identifier: GPL-3.0-or-later
//
// performance
//
// One function per INI arm, in the original loader order. The bodies are the
// readable reconstruction's verified arms; only the wrapper and the arm trace
// are ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#include "opcodes.hxx"

export module opp.performance;

import opp.pipeline;
import opp.patch;
import opp.game;
import opp.common;
import opp.settings;
import opp.log;

// ---------------------------------------------------------------------------
// Hook bodies: the machine-level entry points performance installs.
//
// Each body keeps the original site's calling convention and register
// contract; the comment above it names the original destination VA. They are
// 32-bit only (the x64 parity dump resolves names to the original VAs).
// ---------------------------------------------------------------------------
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

extern "C"
{

// 0x10045a30 / 0x10045a80 ImprovedShaderStreaming: replacements for the two
// "find shader entry" functions. Originals are __thiscall(this, query) -> entry
// or 0 with one callee-popped argument, so __fastcall(self, edx, query)
// reproduces the ECX=this / stack-arg / ret 4 contract: walk the vector at
// this+8 (count at this+0xC) and return the first non-null entry whose
// vtable[+0x10]() name passes the filter (sub_100459E0: name[4]=='h' or
// name[4]=='p' && name[5]=='e'; sub_10045A10: name[4]=='v').
using ShaderEntryNameFn = const void*(__fastcall*)(void* entry, int);
using ShaderStreamFilterFn = int(__cdecl*)(const void* query, const void* name);
int __cdecl abi_shader_stream_filter_hpe(const void*, const void* name)
{
    const auto* p = static_cast<const unsigned char*>(name);
    if (!p) return 2;
    if (p[4] == 'h') return 0;
    if (p[4] == 'p' && p[5] == 'e') return 0;
    return 2;
}
int __cdecl abi_shader_stream_filter_v(const void*, const void* name)
{
    const auto* p = static_cast<const unsigned char*>(name);
    return (p && p[4] == 'v') ? 0 : 2;
}
namespace
{
void* shader_streaming_find(void* self, const void* query, ShaderStreamFilterFn filter)
{
    auto* base = static_cast<unsigned char*>(self);
    const unsigned count = *reinterpret_cast<const unsigned short*>(base + 0xc);
    auto** array = *reinterpret_cast<void***>(base + 8);
    for (unsigned i = 0; i < count; ++i)
    {
        void* entry = array[i];
        if (!entry) continue;
        auto* vt = *reinterpret_cast<void* const* const*>(entry);
        const auto* name = reinterpret_cast<ShaderEntryNameFn>(vt[0x10 / sizeof(void*)])(entry, 0);
        if (filter(query, name) == 0) return entry;
    }
    return nullptr;
}
}  // namespace
void* __fastcall abi_improved_shader_streaming_find_hpe(void* self, int, const void* query)
{
    return shader_streaming_find(self, query, &abi_shader_stream_filter_hpe);
}
void* __fastcall abi_improved_shader_streaming_find_v(void* self, int, const void* query)
{
    return shader_streaming_find(self, query, &abi_shader_stream_filter_v);
}

// NewMemorySystem: the reimplemented CRageAllocator whose vtable (original
// 0x100B5038) the E8 body writes into the game's allocator singleton. The
// original swaps the arena allocator for a HeapAlloc-backed one: allocate() uses
// unit = max(0x10, align), want = max(size, unit), HeapAllocs want + 2*unit,
// rounds up to ((base-1)/unit + 2)*unit, stores the base offset at [p-4], fills
// want bytes with 0xCD and registers the pointer under the critical section at
// 0x101A0808; free() recovers the base from [p-4], HeapFrees and unregisters.
// Only vtable[18] observes the registry, so a plain set suffices.
// The allocator vtable's small methods. The original's calling conventions
// are preserved; only the constructor and thunk still need their exact
// prologue/tail-jump.
extern "C" void __cdecl abi_nms_noop_ret0() {}

// 0x10038640: constructor (thiscall; one stack argument, callee pops it).
static __declspec(naked) void* abi_nms_ctor()
{
    __asm {
        push ebp
        mov ebp, esp
        mov eax, dword ptr [ebp + 8]
        push esi
        mov esi, ecx
        and eax, 5
        mov dword ptr [esi], offset zpatch_nms_vtable
        cmp al, 5
        jne nms_ctor_done
        push 4
        push esi
        call abi_nms_noop_ret0
        add esp, 8
    nms_ctor_done:
        mov eax, esi
        pop esi
        pop ebp
        ret 4
        __emit 0CCh
    }
}

// 0x10038350
extern "C" bool __stdcall abi_nms_reject_ptr(void*) { return false; }

// 0x10038540
extern "C" void* __fastcall abi_nms_self(void* self, void* /*edx*/, void*)
{
    return self;
}

// 0x10001950
extern "C" void* __fastcall abi_nms_zero_ptr(void* /*self*/, void* /*edx*/, void*)
{
    return nullptr;
}

// 0x10038580
extern "C" void __stdcall abi_nms_noop_pair(void*, void*) {}

// 0x10038590
extern "C" bool __cdecl abi_nms_reject() { return false; }

// 0x10038620: forward through vtable[6].
// Tail-jumps, so the caller's return address and EBP stay untouched.
static __declspec(naked) void abi_nms_thunk18()
{
    __asm {
        push ebp
        mov ebp, esp
        mov eax, dword ptr [ecx]
        pop ebp
        jmp dword ptr [eax + 18h]
        __emit 0CCh
    }
}

// 0x10038630
extern "C" void __stdcall abi_nms_noop_arg(void*) {}

// 0x10038670: install the vtable in the game's allocator singleton.
extern "C" void __cdecl abi_nms_apply()
{
    auto* object = reinterpret_cast<void**>(zpatch_nms_object);
    if (object) *object = zpatch_nms_vtable;
}

namespace
{
// NewMemorySystem's allocation registry: the original keeps the pointers it
// handed out in its own hash map (FNV-1a over the pointer's four bytes) under
// the recursive critical section at 0x101A0808. Only membership is observable
// (vtable[18] asks "is this pointer mine?"), so the keys go in a flat
// open-addressing set behind an equivalent lock.
//
// It is a flat table rather than std::unordered_set because the set is touched
// on *every* game allocation and free: an unordered_set node is one heap
// allocation per insert (and a throw inside a noexcept caller on failure). The
// table allocates only when it grows, never throws (VirtualAlloc returns null
// and the insert is dropped), and keeps the same membership semantics.
CRITICAL_SECTION g_nms_lock;

// One registry entry. The state is explicit so a key can never be confused with
// a marker: Empty is a never-used slot, Occupied a live key, Erased a tombstone
// that still terminates a probe chain. A Slot is 8 bytes (key + state byte +
// padding); at the 3/4 load factor that is ~11 bytes per live pointer, still far
// below the original map's per-node cost, and the type is what makes the probe
// code readable.
struct Slot final
{
    enum class State : uint8_t
    {
        empty,
        occupied,
        erased,
    };

    uint32_t key{};
    State state{State::empty};

    bool IsEmpty() const noexcept { return state == State::empty; }
    bool IsOccupied() const noexcept { return state == State::occupied; }
    bool IsErased() const noexcept { return state == State::erased; }

    void Occupy(uint32_t value) noexcept
    {
        key = value;
        state = State::occupied;
    }
    void Erase() noexcept { state = State::erased; }
};
static_assert(sizeof(Slot) == 8u, "a typed slot is a key plus its state byte");

// The owning storage for the registry's slots: one VirtualAlloc block, its
// capacity in slots and the two counters the probe policy needs. It is allocated
// once and only replaced by NmsRegistry::Grow; the current block lives for the
// process lifetime (the original's map does the same) and is released by
// ~SlotTable at module detach.
class SlotTable final
{
public:
    SlotTable() noexcept = default;
    SlotTable(const SlotTable&) = delete;
    SlotTable& operator=(const SlotTable&) = delete;
    SlotTable(SlotTable&& other) noexcept { Swap(other); }
    ~SlotTable() noexcept { Release(); }

    // The replacement block for Grow. The flags are the original allocator's
    // (MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE); an empty table means
    // VirtualAlloc failed and the caller must keep what it has.
    static SlotTable Allocate(size_t capacity) noexcept
    {
        auto* slots = static_cast<Slot*>(VirtualAlloc(
            nullptr, capacity * sizeof(Slot), MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE));
        if (slots == nullptr)
            return SlotTable{};
        SlotTable table;
        table.slots_ = slots;
        table.capacity_ = capacity;
        return table;
    }

    bool empty() const noexcept { return slots_ == nullptr; }
    size_t capacity() const noexcept { return capacity_; }
    size_t live() const noexcept { return live_; }
    size_t occupied() const noexcept { return occupied_; }

    Slot& At(size_t index) noexcept { return slots_[index]; }
    const Slot& At(size_t index) const noexcept { return slots_[index]; }

    // Counter bookkeeping for one insert: `reused_erased` says the key took a
    // tombstone instead of an Empty slot, so the occupied count does not move.
    void NoteInsert(bool reused_erased) noexcept
    {
        ++live_;
        if (!reused_erased)
            ++occupied_;
    }
    void NoteErase() noexcept { --live_; }
    void ResetCounts() noexcept
    {
        live_ = 0u;
        occupied_ = 0u;
    }

    void Swap(SlotTable& other) noexcept
    {
        Slot* const slots = slots_;
        slots_ = other.slots_;
        other.slots_ = slots;
        const size_t capacity = capacity_;
        capacity_ = other.capacity_;
        other.capacity_ = capacity;
        const size_t live = live_;
        live_ = other.live_;
        other.live_ = live;
        const size_t occupied = occupied_;
        occupied_ = other.occupied_;
        other.occupied_ = occupied;
    }

    void Release() noexcept
    {
        if (slots_ != nullptr)
            VirtualFree(slots_, 0, MEM_RELEASE);
        slots_ = nullptr;
        capacity_ = 0u;
        live_ = 0u;
        occupied_ = 0u;
    }

private:
    Slot* slots_{};
    size_t capacity_{};
    size_t live_{};
    size_t occupied_{};
};

class NmsRegistry final
{
public:
    // The two values a four-byte-aligned pointer can never be. They stay
    // rejected so an unknown/zero key behaves exactly as before the typed slots.
    static constexpr uint32_t kReservedZeroKey = 0u;
    static constexpr uint32_t kReservedErasedKey = 0xFFFFFFFFu;

    // The smallest table (in slots) and the probe/growth policy, named so the
    // comments in ReserveForInsert and ProbeIndex can refer to them.
    static constexpr size_t kMinCapacity = 1024u;
    // Grow when the next insert would reach 3/4 of the slots, tombstones
    // included because they still terminate a probe.
    static constexpr size_t kGrowAtPercent = 75u;
    // If at most half of the occupied slots are live, the table is mostly
    // tombstones: rebuild it at the same capacity instead of doubling.
    static constexpr size_t kRebuildWhenLiveAtMostPercent = 50u;
    // Knuth's 32-bit multiplicative hash constant: the keys are 4-byte-aligned
    // heap addresses whose low bits are all zero, so the multiply spreads them
    // before the >> 8.
    static constexpr uint32_t kHashMultiplier = 2654435761u;

    // Insert `key` unless it is already a member. Returns false for a reserved
    // key and for an allocation failure (see Grow); a duplicate returns true
    // without changing the set, like the original's map insert.
    bool insert(uint32_t key) noexcept
    {
        if (IsReserved(key))
            return false;
        if (!ReserveForInsert())
            return false;
        return InsertNoGrow(key);
    }

    void erase(uint32_t key) noexcept
    {
        if (table_.empty() || IsReserved(key))
            return;
        Slot* const slot = Find(key);
        if (slot == nullptr)
            return;
        slot->Erase();
        table_.NoteErase();
    }

    bool contains(uint32_t key) const noexcept
    {
        if (table_.empty() || IsReserved(key))
            return false;
        return Find(key) != nullptr;
    }

    size_t size() const noexcept { return table_.live(); }
    size_t capacity() const noexcept { return table_.capacity(); }

private:
    static constexpr size_t kNoSlot = static_cast<size_t>(-1);

    static bool IsReserved(uint32_t key) noexcept
    {
        return key == kReservedZeroKey || key == kReservedErasedKey;
    }

    // Multiplicative hash then linear probing: hash >> 8 keeps the multiply's
    // high bits and `probe` walks the table linearly. The capacity is always a
    // power of two, so the mask is the modulo.
    static size_t ProbeIndex(uint32_t key, size_t probe, size_t mask) noexcept
    {
        return (static_cast<size_t>((key * kHashMultiplier) >> 8) + probe) & mask;
    }

    // The live slot holding `key`, or nullptr when the probe reaches an Empty
    // slot (which proves the key is absent).
    Slot* Find(uint32_t key) noexcept
    {
        const size_t mask = table_.capacity() - 1u;
        for (size_t probe = 0; probe <= mask; ++probe)
        {
            Slot& slot = table_.At(ProbeIndex(key, probe, mask));
            if (slot.IsEmpty())
                return nullptr;
            if (slot.IsOccupied() && slot.key == key)
                return &slot;
        }
        return nullptr;
    }

    const Slot* Find(uint32_t key) const noexcept
    {
        const size_t mask = table_.capacity() - 1u;
        for (size_t probe = 0; probe <= mask; ++probe)
        {
            const Slot& slot = table_.At(ProbeIndex(key, probe, mask));
            if (slot.IsEmpty())
                return nullptr;
            if (slot.IsOccupied() && slot.key == key)
                return &slot;
        }
        return nullptr;
    }

    // Make room for one more insert, following the policy above. Never touches
    // the table unless it grows.
    bool ReserveForInsert() noexcept
    {
        if (table_.empty())
            return Grow(kMinCapacity);
        if ((table_.occupied() + 1u) * 100u < table_.capacity() * kGrowAtPercent)
            return true;
        if (table_.live() * 100u <= table_.occupied() * kRebuildWhenLiveAtMostPercent)
            return Grow(table_.capacity());
        return Grow(table_.capacity() * 2u);
    }

    // Insert into the current table; never allocates. A duplicate is a no-op; a
    // tombstone is reused before an Empty slot, and the first tombstone seen is
    // used if the probe runs off the end.
    bool InsertNoGrow(uint32_t key) noexcept
    {
        const size_t mask = table_.capacity() - 1u;
        size_t erased = kNoSlot;
        for (size_t probe = 0; probe <= mask; ++probe)
        {
            const size_t index = ProbeIndex(key, probe, mask);
            Slot& slot = table_.At(index);
            if (slot.IsOccupied() && slot.key == key)
                return true;
            if (slot.IsErased() && erased == kNoSlot)
                erased = index;
            if (slot.IsEmpty())
            {
                if (erased != kNoSlot)
                {
                    table_.At(erased).Occupy(key);
                    table_.NoteInsert(true);
                }
                else
                {
                    slot.Occupy(key);
                    table_.NoteInsert(false);
                }
                return true;
            }
        }
        if (erased != kNoSlot)
        {
            table_.At(erased).Occupy(key);
            table_.NoteInsert(true);
            return true;
        }
        return false;
    }

    // The replacement block for one growth step.
    static SlotTable AllocateReplacement(size_t wanted) noexcept
    {
        return SlotTable::Allocate(wanted);
    }

    // Make the replacement live and reset the new table's counters. The previous
    // table is handed back through `replacement`, so the caller can rehash from
    // it and then release it.
    void Publish(SlotTable& replacement) noexcept
    {
        table_.Swap(replacement);
        table_.ResetCounts();
    }

    // Re-insert every live key of `old` into the current (new) table. `old` is
    // still owned by the caller until it releases it.
    void RehashInto(const SlotTable& old) noexcept
    {
        for (size_t index = 0; index < old.capacity(); ++index)
        {
            const Slot& slot = old.At(index);
            if (slot.IsOccupied())
                InsertNoGrow(slot.key);
        }
    }

    // Allocate a replacement at `wanted` slots, rehash into it and release the
    // old block. On allocation failure nothing changes and the caller's insert
    // returns false: the game still gets its block, but the pointer is missing
    // from "is this pointer mine", so that query answers false for it (a false
    // negative). The original's map insert failure has the same observable
    // result.
    bool Grow(size_t wanted) noexcept
    {
        SlotTable replacement = AllocateReplacement(wanted);
        if (replacement.empty())
            return false;
        Publish(replacement);     // table_ = new block, replacement = old block
        RehashInto(replacement);  // fill table_ from the old block
        replacement.Release();    // free the old block
        return true;
    }

    SlotTable table_;
};

NmsRegistry g_nms_allocations;

struct NmsRegistryInit final
{
    NmsRegistryInit() noexcept { InitializeCriticalSection(&g_nms_lock); }
};
NmsRegistryInit g_nms_registry_init;
}  // namespace

// vtable[2] (0x10038360): HeapAlloc-backed allocate with the original's
// alignment scheme. ECX is the object (unused by the original's body too).
extern "C" void* __stdcall zpatch_nms_alloc(uint32_t size, uint32_t align,
                                            uint32_t flags) noexcept
{
    // The allocator's third argument is ignored by the original as well.
    (void)flags;
    // The original's alignment scheme: a unit is at least 16 bytes, the payload
    // is at least one unit, the HeapAlloc block reserves two spare units (the
    // alignment slack and the offset dword the game reads back), and the
    // returned pointer is unit-aligned past the first unit.
    constexpr uint32_t kMinUnit = 0x10u;
    constexpr uint32_t kSpareUnits = 2u;
    constexpr size_t kBaseOffsetSize = sizeof(uint32_t);
    const uint32_t unit = align < kMinUnit ? kMinUnit : align;
    const uint32_t want = size < unit ? unit : size;
    void* base = HeapAlloc(GetProcessHeap(), 0, want + unit * kSpareUnits);
    if (!base) return nullptr;
    const auto raw = reinterpret_cast<uintptr_t>(base);
    const uintptr_t aligned = ((raw - 1u) / unit + kSpareUnits) * unit;
    *reinterpret_cast<uint32_t*>(aligned - kBaseOffsetSize) =
        static_cast<uint32_t>(aligned - raw);
    auto* result = reinterpret_cast<void*>(aligned);
    // 0xCD is the MSVC debug-heap free-fill byte; the original's allocator
    // leaves the block with it, so the fill is kept 1:1.
    constexpr int kDebugHeapFill = 0xCD;
    std::memset(result, kDebugHeapFill, want);
    EnterCriticalSection(&g_nms_lock);
    g_nms_allocations.insert(static_cast<uint32_t>(aligned));
    LeaveCriticalSection(&g_nms_lock);
    return result;
}

// vtable[3] (0x10038430): recover the HeapAlloc base from [ptr-4], free it and
// drop the pointer from the registry.
extern "C" void __stdcall zpatch_nms_free(void* ptr) noexcept
{
    if (!ptr) return;
    // The allocate path stored (aligned - HeapAlloc base) in the dword just
    // before the returned pointer.
    constexpr size_t kBaseOffsetSize = sizeof(uint32_t);
    const auto raw = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t base =
        raw - *reinterpret_cast<const uint32_t*>(raw - kBaseOffsetSize);
    HeapFree(GetProcessHeap(), 0, reinterpret_cast<void*>(base));
    EnterCriticalSection(&g_nms_lock);
    g_nms_allocations.erase(static_cast<uint32_t>(raw));
    LeaveCriticalSection(&g_nms_lock);
}

// vtable[18] (0x100385A0): the game's "is this pointer mine" query.
extern "C" uint8_t __stdcall zpatch_nms_contains(uint32_t key) noexcept
{
    EnterCriticalSection(&g_nms_lock);
    const bool found = g_nms_allocations.contains(key);
    LeaveCriticalSection(&g_nms_lock);
    return found ? uint8_t{1} : uint8_t{0};
}

// Offline check for the registry above (native tests only): a local instance,
// so the live registry is untouched. Exercises insert/erase/contains, growth
// past the first block, tombstone reuse and the reserved keys.
extern "C" bool zpatch_nms_registry_selftest() noexcept
{
    NmsRegistry registry;
    constexpr uint32_t kKeys = 20000u;
    std::array<uint32_t, kKeys> keys{};
    // The key space is 28 bits of pointer shifted into 4-byte steps; the seed is
    // the recovered self-test seed and the generator is the recovered LCG.
    constexpr uint32_t kNmsKeyMask = (1u << 28) - 1u;
    static_assert(kNmsKeyMask == 0x0FFFFFFFu);
    constexpr uint32_t kSelfTestSeed = 0x1234'5678u;
    constexpr uint32_t kSelfTestAlignment = 16u;
    constexpr uint32_t kLcgMultiplier = 1664525u;
    constexpr uint32_t kLcgIncrement = 1013904223u;
    uint32_t state = kSelfTestSeed;
    for (uint32_t i = 0; i < kKeys; ++i)
    {
        // Aligned, 4-byte-spaced pseudo-random pointers, like the real keys.
        state = state * kLcgMultiplier + kLcgIncrement;
        keys[i] = (state & kNmsKeyMask) * kSelfTestAlignment;
    }

    for (uint32_t i = 0; i < kKeys; ++i)
        if (!registry.insert(keys[i])) return false;
    if (registry.size() != kKeys) return false;
    // It must have grown past the first block at least once.
    if (registry.capacity() <= NmsRegistry::kMinCapacity) return false;

    // Duplicates are idempotent, not new entries.
    if (!registry.insert(keys[0])) return false;
    if (registry.size() != kKeys) return false;

    // The reserved keys are never members.
    if (registry.insert(NmsRegistry::kReservedZeroKey)) return false;
    if (registry.insert(NmsRegistry::kReservedErasedKey)) return false;
    if (registry.contains(NmsRegistry::kReservedZeroKey) ||
        registry.contains(NmsRegistry::kReservedErasedKey))
        return false;

    // Erase every other key; the rest must survive, including across the
    // tombstone-heavy table.
    for (uint32_t i = 0; i < kKeys; i += 2)
        registry.erase(keys[i]);
    if (registry.size() != kKeys / 2) return false;
    for (uint32_t i = 0; i < kKeys; ++i)
        if (registry.contains(keys[i]) != ((i & 1u) != 0u)) return false;

    // Re-inserting into tombstones must not grow the size past live entries.
    for (uint32_t i = 0; i < kKeys; i += 2)
        if (!registry.insert(keys[i])) return false;
    if (registry.size() != kKeys) return false;
    for (uint32_t i = 0; i < kKeys; ++i)
        if (!registry.contains(keys[i])) return false;

    // Erasing a non-member changes nothing.
    constexpr uint32_t kNotAMember = 0xDEAD'BEE0u;  // never inserted anywhere
    registry.erase(kNotAMember);
    if (registry.size() != kKeys) return false;

    // Churn: repeated erase-all/re-insert cycles must stay bounded instead of
    // doubling on tombstones alone.
    constexpr uint32_t kChurnCycles = 8u;
    for (uint32_t cycle = 0; cycle < kChurnCycles; ++cycle)
    {
        for (uint32_t i = 0; i < kKeys; ++i) registry.erase(keys[i]);
        if (registry.size() != 0u) return false;
        for (uint32_t i = 0; i < kKeys; ++i)
            if (!registry.insert(keys[i])) return false;
    }
    if (!registry.contains(keys[kKeys - 1])) return false;
    constexpr size_t kMaxChurnCapacityFactor = 4u;
    return registry.capacity() <= kMaxChurnCapacityFactor * kKeys;
}

// The CRageAllocator vtable, entry order and per-entry conventions as the
// original's 0x100B5038 (see the individual methods above). The uintptr_t
// entries are the ABI itself: a vtable is an array of code addresses in the
// original's data layout, which is why the addresses are stored, not typed.
extern "C"
{
uintptr_t zpatch_nms_vtable[kNmsVtableEntries] =
{
        reinterpret_cast<uintptr_t>(&abi_nms_ctor),         // [0] 0x10038640
        reinterpret_cast<uintptr_t>(&abi_nms_reject_ptr),   // [1] 0x10038350
        reinterpret_cast<uintptr_t>(&zpatch_nms_alloc),     // [2] 0x10038360
        reinterpret_cast<uintptr_t>(&zpatch_nms_free),      // [3] 0x10038430
        reinterpret_cast<uintptr_t>(&abi_nms_self),         // [4] 0x10038540
        reinterpret_cast<uintptr_t>(&abi_nms_self),         // [5] 0x10038540
        reinterpret_cast<uintptr_t>(&abi_nms_zero_ptr),     // [6] 0x10001950
        reinterpret_cast<uintptr_t>(&abi_nms_zero_ptr),     // [7] 0x10001950
        reinterpret_cast<uintptr_t>(&abi_better_mp_sync),   // [8] 0x100057F0
        reinterpret_cast<uintptr_t>(&abi_better_mp_sync),   // [9] 0x100057F0
        reinterpret_cast<uintptr_t>(&abi_rgsc_login_removed),  // [10] 0x10038550
        reinterpret_cast<uintptr_t>(&abi_nms_noop_ret0),    // [11] 0x10038560
        reinterpret_cast<uintptr_t>(&abi_savegame_fix_null),   // [12] 0x10038570
        reinterpret_cast<uintptr_t>(&abi_nms_noop_pair),    // [13] 0x10038580
        reinterpret_cast<uintptr_t>(&abi_nms_noop_ret0),    // [14] 0x10038560
        reinterpret_cast<uintptr_t>(&abi_nms_reject),       // [15] 0x10038590
        reinterpret_cast<uintptr_t>(&abi_nms_reject),       // [16] 0x10038590
        reinterpret_cast<uintptr_t>(&abi_nms_noop_ret0),    // [17] 0x10038560
        reinterpret_cast<uintptr_t>(&zpatch_nms_contains),  // [18] 0x100385A0
        reinterpret_cast<uintptr_t>(&abi_nms_thunk18),      // [19] 0x10038620
        reinterpret_cast<uintptr_t>(&abi_better_mp_sync),   // [20] 0x100057F0
        reinterpret_cast<uintptr_t>(&abi_better_mp_sync),   // [21] 0x100057F0
        reinterpret_cast<uintptr_t>(&abi_nms_noop_arg),     // [22] 0x10038630
        reinterpret_cast<uintptr_t>(&abi_nms_reject),       // [23] 0x10038590
};
}  // extern "C"
}  // extern "C"

#endif  // !defined(_WIN64)

namespace opp::features
{

    // FastLoading

    void InstallFastLoading(PatchContext& ctx)
    {

        TraceArm("FastLoading");

        // FastLoading: 1 sigwrite
        if (ctx.Option("FastLoading", "1"))
        {
            const GameImage& image = ctx.image;

            {
                if (auto pattern = Pattern(image, "6A 0A E8 ? ? ? ? 83 C7 0A 83 C4 04 81 FF E8 03 00 00 75 02 33 FF"); pattern.size() > 0)
                {
                    // `6A 0A` is `push 10`: zeroing its low byte makes the
                    // sleep call wait 0 ms instead of 10 (the original's write).
                    WriteValue<uint8_t>(ctx, pattern.get(0).get<uint8_t>(1), 0x00u);
                }
            }
        }

    }


    // HighFPSSpeedupFix

    void InstallHighFPSSpeedupFix(PatchContext& ctx)
    {

        TraceArm("HighFPSSpeedupFix");

        // HighFPSSpeedupFix: 9 literals
        if (ctx.Option("HighFPSSpeedupFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "0F 2F C1 76 03 0F 28 C8 F3 0F 10 15"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                NopAt(ctx, match - 7, 2);
                NopAt(ctx, match + 3, 2);
                // The branch at +224 becomes a short jmp (the original's
                // unconditional-skip rewrite in the 0x400-byte read function).
                WriteValue<uint8_t>(ctx, match + 224, opp::ops::kJmpRel8);
                NopAt(ctx, match + 231, 2);
                NopAt(ctx, match + 367, 2);
                NopAt(ctx, match + 377, 2);
                NopAt(ctx, match + 607, 2);
                NopAt(ctx, match + 614, 2);
                NopAt(ctx, match + 764, 2);
            }
        }

    }


    // ImprovedShaderStreaming

    void InstallImprovedShaderStreaming(PatchContext& ctx)
    {

        TraceArm("ImprovedShaderStreaming");

        // ImprovedShaderStreaming: 1 siginstall
        if (ctx.Option("ImprovedShaderStreaming", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallTo(ctx, match + 85, OPP_BODY(abi_improved_shader_streaming_find_hpe));
                JumpTo(ctx, match + 98, reinterpret_cast<void*>(match + 245));
                CallTo(ctx, match + 253, OPP_BODY(abi_improved_shader_streaming_find_v));
                JumpTo(ctx, match + 266, reinterpret_cast<void*>(match + 1158));
            }
        }

    }


    // IncreasePtrNodes

    void InstallIncreasePtrNodes(PatchContext& ctx)
    {

        TraceArm("IncreasePtrNodes");

        // IncreasePtrNodes: 3 literalstores
        if (ctx.Option("IncreasePtrNodes", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "6A 08 ? ? ? ? ? 68 80 38 01 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The `push imm32` operand at +8 is the pointer-node pool count:
                // 0x13880 (80,000) -> 0x4E200 (320,000).
                WriteValue<uint32_t>(ctx, match + 8, static_cast<uint32_t>(0x4E200u));
            }

            if (auto pattern = Pattern(image, "6A 10 ? ? ? ? ? 68 80 3E 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The same operand on the 16,000-entry pool: -> 0xFA00 (64,000).
                WriteValue<uint32_t>(ctx, match + 8, static_cast<uint32_t>(0xFA00u));
            }

            if (auto pattern = Pattern(image, "6A 14 ? ? ? ? ? 68 80 3E 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The same operand on the 16,000-entry pool: -> 0xFA00 (64,000).
                WriteValue<uint32_t>(ctx, match + 8, static_cast<uint32_t>(0xFA00u));
            }
        }

    }


    // IncreaseVehicleModelLimit

    void InstallIncreaseVehicleModelLimit(PatchContext& ctx)
    {

        TraceArm("IncreaseVehicleModelLimit");

        // IncreaseVehicleModelLimit: 1 indirect
        if (ctx.Option("IncreaseVehicleModelLimit", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 C7 01 81 FF 18 79 00 00 7C D2"); pattern.size() > 0)
            {
                auto* target = MatchPointer(pattern, 77);
                if (target != nullptr)
                    // The model-limit slot the pattern's stored pointer names:
                    // 1024 models (0x400).
                    WriteValue<uint32_t>(ctx, target, static_cast<uint32_t>(0x400u));
            }
        }

    }


    // IncreaseVehicleStructLimit

    void InstallIncreaseVehicleStructLimit(PatchContext& ctx)
    {

        TraceArm("IncreaseVehicleStructLimit");

        // IncreaseVehicleStructLimit: 1 literalstore
        if (ctx.Option("IncreaseVehicleStructLimit", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 ? ? ? ? 6A 32 8B C8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The `push 50` operand at +6 becomes 100: the vehicle-struct
                // count the allocator is asked for.
                WriteValue<uint8_t>(ctx, match + 6, static_cast<uint8_t>(0x64u));
            }
        }

    }


    // publish NewMemorySystem

    void PublishNewMemorySystem(PatchContext& ctx)
    {

        TraceArm("publish NewMemorySystem");

        // NewMemorySystem: 1 slot
        if (ctx.Option("NewMemorySystem", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "6A 01 B9 00 00 40 06"); pattern.size() > 0)
            {
                zpatch_nms_object = *pattern.get(0).get<uint32_t>(8);
            }
        }

    }


    // NewMemorySystem

    void InstallNewMemorySystem(PatchContext& ctx)
    {

        TraceArm("NewMemorySystem");

        // NewMemorySystem: 1 siginstall
        if (ctx.Option("NewMemorySystem", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "3D 80 99 99 59 73 05"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallTo(ctx, match - 34, OPP_BODY(abi_nms_apply));
                JumpTo(ctx, match - 29, reinterpret_cast<void*>(match - 16));
            }
        }

    }

    // NoLoadingSleep

    void InstallNoLoadingSleep(PatchContext& ctx)
    {

        TraceArm("NoLoadingSleep");

        // NoLoadingSleep: 1 literalstore
        if (ctx.Option("NoLoadingSleep", "0"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 88 13 00 00 FF 15"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // `push 0x1388` (5000 ms) becomes `push 0`: the loading sleep
                // the original removes.
                WriteValue<uint32_t>(ctx, match + 1, static_cast<uint32_t>(0x0u));
            }
        }

    }


    // StartupTimeFix

    void InstallStartupTimeFix(PatchContext& ctx)
    {

        TraceArm("StartupTimeFix");

        // StartupTimeFix: 1 matchliteral
        if (ctx.Option("StartupTimeFix", "1"))
        {
            const GameImage& image = ctx.image;

            {
                // The original walks every match of this literal
                // (`for_each_result`) and patches each one, so the hit count is
                // image data rather than a recovered per-build constant: there is
                // no expected count to assert here. P13 guards cover the arms
                // whose original read a fixed site set instead.
                if (auto pattern = PatternAll(image, "68 00 C2 EC 80 68"); pattern.size() > 0)
                {
                    for (size_t i = 0; i < pattern.size(); ++i)
                        WriteValue<uint32_t>(ctx, pattern.get(i).get<uint8_t>() + 6, static_cast<uint32_t>(FindGlobalValue("StartupTimeFix")));
                }
            }
        }

    }

namespace
{
    class PerformanceFeatures
    {
    public:
        PerformanceFeatures()
        {
            PublishEvent().Add(&PublishNewMemorySystem, "PublishNewMemorySystem");
            InstallEvent().Add(&InstallHighFPSSpeedupFix, "InstallHighFPSSpeedupFix");
            InstallEvent().Add(&InstallImprovedShaderStreaming, "InstallImprovedShaderStreaming");
            InstallEvent().Add(&InstallNewMemorySystem, "InstallNewMemorySystem");
            InstallEvent().Add(&InstallFastLoading, "InstallFastLoading");
            InstallEvent().Add(&InstallIncreaseVehicleModelLimit, "InstallIncreaseVehicleModelLimit");
            InstallEvent().Add(&InstallStartupTimeFix, "InstallStartupTimeFix");
            InstallEvent().Add(&InstallIncreasePtrNodes, "InstallIncreasePtrNodes");
            InstallEvent().Add(&InstallIncreaseVehicleStructLimit, "InstallIncreaseVehicleStructLimit");
            InstallEvent().Add(&InstallNoLoadingSleep, "InstallNoLoadingSleep");
        }
    } performanceFeatures;
} // namespace

} // namespace opp::features
