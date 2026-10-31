#pragma once
// Write-tracing stand-in for the vendored injector, compiled only into the
// debug ASI (ZP_TRACE_WRITES). It performs exactly the same stores and appends
// "site length bytes" to ZolikaPatch.writes.log, so a live run can be compared
// with the reference implementation's log and a boot fault can be located at
// its last write.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace injector
{
// The arm currently running, set by the generated PatchGame trace.
extern const char* g_traceArm;

inline void TraceWrite(void* address, const void* value, std::size_t size)
{
	static std::FILE* log = std::fopen("ZolikaPatch.writes.log", "a");
	if (log == nullptr)
		return;
	std::fprintf(log, "%s %08lX %zu ", g_traceArm ? g_traceArm : "?",
	             static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(address)), size);
	const auto* bytes = static_cast<const unsigned char*>(value);
	for (std::size_t i = 0; i < size; ++i)
		std::fprintf(log, "%02X", bytes[i]);
	std::fprintf(log, "\n");
	std::fflush(log);
}

inline void WriteMemoryRaw(void* address, const void* value, std::size_t size, bool unprotect)
{
	TraceWrite(address, value, size);
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

inline void* MakeCALL(void* at, const void* destination, bool unprotect)
{
	const auto relative = static_cast<std::int32_t>(
	    reinterpret_cast<std::uintptr_t>(destination) - (reinterpret_cast<std::uintptr_t>(at) + 5));
	std::uint8_t bytes[5] = {0xE8, 0, 0, 0, 0};
	std::memcpy(bytes + 1, &relative, sizeof(relative));
	WriteMemoryRaw(at, bytes, sizeof(bytes), unprotect);
	return at;
}

inline void* MakeJMP(void* at, const void* destination, bool unprotect)
{
	const auto relative = static_cast<std::int32_t>(
	    reinterpret_cast<std::uintptr_t>(destination) - (reinterpret_cast<std::uintptr_t>(at) + 5));
	std::uint8_t bytes[5] = {0xE9, 0, 0, 0, 0};
	std::memcpy(bytes + 1, &relative, sizeof(relative));
	WriteMemoryRaw(at, bytes, sizeof(bytes), unprotect);
	return at;
}
} // namespace injector
