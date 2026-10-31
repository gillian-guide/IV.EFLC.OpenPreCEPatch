#pragma once
// A minimal stand-in for the vendored injector, used only by the 64-bit
// write-set dump. The real header is 32-bit oriented (its pointer arithmetic
// assumes a module and game in one flat 32-bit space), which is exactly right
// for the ASI but breaks a 64-bit harness. This shim performs the same stores:
// MakeJMP/MakeCALL write the opcode and the rel32 the caller's target implies.

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
} // namespace injector
