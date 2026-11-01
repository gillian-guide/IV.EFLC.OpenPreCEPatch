#pragma once
// A minimal stand-in for the vendored injector, used only by the 64-bit write-set
// dump: the real header assumes a flat 32-bit space, which is right for the ASI but
// breaks a 64-bit harness. It performs the same stores (MakeJMP/MakeCALL write the
// opcode and the rel32 the caller's target implies).

#include <cstddef>
#include <cstdint>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace injector
{
inline void WriteMemoryRaw(void* address, const void* value, std::size_t size, bool unprotect)
{
	DWORD previous = 0;
	if (unprotect)
		VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &previous);
	std::memcpy(address, value, size);
	if (unprotect)
		VirtualProtect(address, size, previous, &previous);
}

template <typename T>
inline void WriteMemory(void* address, const T& value)
{
	WriteMemoryRaw(address, &value, sizeof(T), true);
}

template <typename T>
inline void WriteMemory(void* address, T value, bool unprotect)
{
	WriteMemoryRaw(address, &value, sizeof(T), unprotect);
}

inline void MakeNOP(void* at, std::size_t count, bool unprotect)
{
	std::uint8_t nops[256];
	std::memset(nops, 0x90, sizeof(nops));
	for (std::size_t done = 0; done < count;)
	{
		const std::size_t size = count - done < sizeof(nops) ? count - done : sizeof(nops);
		WriteMemoryRaw(static_cast<std::uint8_t*>(at) + done, nops, size, unprotect);
		done += size;
	}
}

template <typename T>
inline T ReadMemory(void* address)
{
	T value{};
	std::memcpy(&value, address, sizeof(T));
	return value;
}

inline void WriteBranch(void* at, const void* destination, bool unprotect, std::uint8_t opcode)
{
	const auto relative = static_cast<std::int32_t>(
	    reinterpret_cast<std::uintptr_t>(destination) - (reinterpret_cast<std::uintptr_t>(at) + 5));
	std::uint8_t bytes[5] = {opcode, 0, 0, 0, 0};
	std::memcpy(bytes + 1, &relative, sizeof(relative));
	WriteMemoryRaw(at, bytes, sizeof(bytes), unprotect);
}

inline void* MakeCALL(void* at, const void* destination, bool unprotect)
{
	WriteBranch(at, destination, unprotect, 0xE8);
	return at;
}

inline void* MakeJMP(void* at, const void* destination, bool unprotect)
{
	WriteBranch(at, destination, unprotect, 0xE9);
	return at;
}

// The translation-free pointer wrapper the real header exposes (a no-op here:
// the dump never translates addresses).
inline void* raw_ptr(void* p) { return p; }
inline const void* raw_ptr(const void* p) { return p; }
template <typename T>
inline T* raw_ptr(T* p) { return p; }

// The subset of memory_pointer_raw the call sites use.
struct MemoryPointerRaw
{
	void* p;

	template <typename T>
	T* get() const { return static_cast<T*>(p); }
	std::uintptr_t as_int() const { return reinterpret_cast<std::uintptr_t>(p); }
	operator void*() const { return p; }
};

// The destination of the rel32 at `at` (E8/E9 operand form): at + size + rel.
inline MemoryPointerRaw ReadRelativeOffset(void* at, std::size_t size, bool unprotect)
{
	std::int32_t relative = 0;
	std::memcpy(&relative, at, size);
	return {static_cast<std::uint8_t*>(at) + size + relative};
}

// C3, or C2 <pop> and one trailing zero byte. This models the REAL header
// exactly: it calls WriteMemory(int 0xC2), a FOUR-byte store, then writes the
// pop word at +1, so the opcode write also zeroes the byte after the immediate.
// A site that must leave that byte alone (TeleportProtection) cannot use MakeRET.
inline void MakeRET(void* at, std::uint16_t pop, bool unprotect)
{
	const std::uint32_t opcode = pop == 0 ? 0xC3u : 0xC2u;
	WriteMemoryRaw(at, &opcode, sizeof(opcode), unprotect);
	if (pop == 0)
		return;
	WriteMemoryRaw(static_cast<std::uint8_t*>(at) + 1, &pop, sizeof(pop), unprotect);
}
} // namespace injector
