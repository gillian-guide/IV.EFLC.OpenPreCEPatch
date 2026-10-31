#pragma once

#if !defined(_MSC_VER) || !defined(_M_IX86)
#error driveby_seh.h is the MSVC x86 SEH path
#endif

#include <windows.h>
#include <cstdint>

namespace detail
{
using driveby_resolver_call = std::uint32_t(__cdecl*)(std::uint32_t hash,
                                                      std::uint32_t edx_arg);

// Mirrors sub_10003980's catch-all SEH around the resolver call. Its original
// filter always accepts, and the handler returns zero to the caller.
__declspec(noinline) inline std::uint32_t driveby_resolve_seh(driveby_resolver_call resolver,
                                                              std::uint32_t hash,
                                                              std::uint32_t edx_arg) noexcept
{
	__try
	{
		return resolver(hash, edx_arg);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
}
} // namespace detail
