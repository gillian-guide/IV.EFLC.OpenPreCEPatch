// zp.hook_bodies - NewMemorySystem's CRageAllocator replacement: the
// HeapAlloc-backed arena, its registry and the vtable the E8 body writes into
// the game's allocator singleton. Ordinary C++, so it lives outside the
// assembly unit (P3); the constructor and the vtable thunk stay naked in
// source/hook_bodies.cpp.

#include "common.hxx"
#include "common.h"
#include "rebased.h"
#include "hooks.h"
#include "hook_bodies.h"

#if defined(_WIN32)

// NewMemorySystem: the reimplemented CRageAllocator whose vtable (original
// 0x100B5038) the E8 body writes into the game's allocator singleton. The
// original swaps the arena allocator for a HeapAlloc-backed one: allocate() uses
// unit = max(0x10, align), want = max(size, unit), HeapAllocs want + 2*unit,
// rounds up to ((base-1)/unit + 2)*unit, stores the base offset at [p-4], fills
// want bytes with 0xCD and registers the pointer under the critical section at
// 0x101A0808; free() recovers the base from [p-4], HeapFrees and unregisters.
// Only vtable[18] observes the registry, so a plain set suffices.

namespace
{
constexpr std::uint8_t kUninitialisedHeapPoison = 0xCDu;    // MSVC debug CRT
}  // namespace

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
// the recursive critical section at 0x101A0808. Only membership is observable,
// so the keys go in a plain set behind an equivalent lock.
CRITICAL_SECTION g_nms_lock;
std::unordered_set<std::uint32_t> g_nms_allocations;
struct NmsRegistryInit final
{
	NmsRegistryInit() noexcept { InitializeCriticalSection(&g_nms_lock); }
};
NmsRegistryInit g_nms_registry_init;
}  // namespace

// vtable[2] (0x10038360): HeapAlloc-backed allocate with the original's
// alignment scheme. ECX is the object (unused by the original's body too).
extern "C" void* __stdcall zpatch_nms_alloc(std::uint32_t size, std::uint32_t align,
                                            std::uint32_t flags) noexcept
{
	(void)flags;
	const std::uint32_t unit = align < 0x10u ? 0x10u : align;
	const std::uint32_t want = size < unit ? unit : size;
	void* base = HeapAlloc(GetProcessHeap(), 0, want + unit * 2u);
	if (!base) return nullptr;
	const auto raw = reinterpret_cast<std::uintptr_t>(base);
	const std::uintptr_t aligned = ((raw - 1u) / unit + 2u) * unit;
	*reinterpret_cast<std::uint32_t*>(aligned - 4u) =
	    static_cast<std::uint32_t>(aligned - raw);
	auto* result = reinterpret_cast<void*>(aligned);
	std::memset(result, kUninitialisedHeapPoison, want);
	EnterCriticalSection(&g_nms_lock);
	g_nms_allocations.insert(static_cast<std::uint32_t>(aligned));
	LeaveCriticalSection(&g_nms_lock);
	return result;
}

// vtable[3] (0x10038430): recover the HeapAlloc base from [ptr-4], free it and
// drop the pointer from the registry.
extern "C" void __stdcall zpatch_nms_free(void* ptr) noexcept
{
	if (!ptr) return;
	const auto raw = reinterpret_cast<std::uintptr_t>(ptr);
	const std::uintptr_t base = raw - *reinterpret_cast<const std::uint32_t*>(raw - 4u);
	HeapFree(GetProcessHeap(), 0, reinterpret_cast<void*>(base));
	EnterCriticalSection(&g_nms_lock);
	g_nms_allocations.erase(static_cast<std::uint32_t>(raw));
	LeaveCriticalSection(&g_nms_lock);
}

// vtable[18] (0x100385A0): the game's "is this pointer mine" query.
extern "C" std::uint8_t __stdcall zpatch_nms_contains(std::uint32_t key) noexcept
{
	EnterCriticalSection(&g_nms_lock);
	const bool found = g_nms_allocations.find(key) != g_nms_allocations.end();
	LeaveCriticalSection(&g_nms_lock);
	return found ? std::uint8_t{1} : std::uint8_t{0};
}

// The CRageAllocator vtable, entry order and per-entry conventions as the
// original's 0x100B5038 (see the individual methods above).
extern "C"
{
std::uintptr_t zpatch_nms_vtable[24] =
{
		reinterpret_cast<std::uintptr_t>(&abi_nms_ctor),         // [0] 0x10038640
		reinterpret_cast<std::uintptr_t>(&abi_nms_reject_ptr),   // [1] 0x10038350
		reinterpret_cast<std::uintptr_t>(&zpatch_nms_alloc),     // [2] 0x10038360
		reinterpret_cast<std::uintptr_t>(&zpatch_nms_free),      // [3] 0x10038430
		reinterpret_cast<std::uintptr_t>(&abi_nms_self),         // [4] 0x10038540
		reinterpret_cast<std::uintptr_t>(&abi_nms_self),         // [5] 0x10038540
		reinterpret_cast<std::uintptr_t>(&abi_nms_zero_ptr),     // [6] 0x10001950
		reinterpret_cast<std::uintptr_t>(&abi_nms_zero_ptr),     // [7] 0x10001950
		reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [8] 0x100057F0
		reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [9] 0x100057F0
		reinterpret_cast<std::uintptr_t>(&abi_rgsc_login_removed),  // [10] 0x10038550
		reinterpret_cast<std::uintptr_t>(&abi_nms_noop_ret0),    // [11] 0x10038560
		reinterpret_cast<std::uintptr_t>(&abi_savegame_fix_null),   // [12] 0x10038570
		reinterpret_cast<std::uintptr_t>(&abi_nms_noop_pair),    // [13] 0x10038580
		reinterpret_cast<std::uintptr_t>(&abi_nms_noop_ret0),    // [14] 0x10038560
		reinterpret_cast<std::uintptr_t>(&abi_nms_reject),       // [15] 0x10038590
		reinterpret_cast<std::uintptr_t>(&abi_nms_reject),       // [16] 0x10038590
		reinterpret_cast<std::uintptr_t>(&abi_nms_noop_ret0),    // [17] 0x10038560
		reinterpret_cast<std::uintptr_t>(&zpatch_nms_contains),  // [18] 0x100385A0
		reinterpret_cast<std::uintptr_t>(&abi_nms_thunk18),      // [19] 0x10038620
		reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [20] 0x100057F0
		reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [21] 0x100057F0
		reinterpret_cast<std::uintptr_t>(&abi_nms_noop_arg),     // [22] 0x10038630
		reinterpret_cast<std::uintptr_t>(&abi_nms_reject),       // [23] 0x10038590
};
}  // extern "C"

#endif  // _WIN32
