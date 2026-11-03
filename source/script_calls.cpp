// zp.hook_bodies - the native-script "call by hash" layer: the resolver
// entry points, the 0xA4-byte call block and the spawn/print/name helpers.
// Ordinary C++, so it lives outside the assembly unit (P3); the naked
// resolvers and wrappers stay in source/hook_bodies.cpp and call these by name.

#include "common.hxx"
#include "common.h"
#include "rebased.h"
#include "driveby_seh.h"
#include "body_helpers.h"

#if defined(_WIN32)

extern "C"
{
static std::uint32_t __cdecl zpatch_resolver_call(std::uint32_t hash,
                                                  std::uint32_t edx_arg)
{
	return zpatch_script_resolve_with_block(
	    hash, reinterpret_cast<void*>(static_cast<std::uintptr_t>(edx_arg)));
}

extern "C" __declspec(noinline) std::uint32_t __cdecl
zpatch_script_resolve_seh(std::uint32_t hash, std::uint32_t edx_arg)
{
	return detail::driveby_resolve_seh(&zpatch_resolver_call, hash, edx_arg);
}

extern "C" __declspec(noinline) std::uint32_t __cdecl
zpatch_driveby_resolve_seh(std::uint32_t hash, std::uint32_t edx_arg)
{
	return zpatch_script_resolve_seh(hash, edx_arg);
}

struct zpatch_native_block final
{
	std::uint32_t m_p_return;   // +0x00 = &args
	std::uint32_t m_arg_count;  // +0x04
	std::uint32_t m_p_args;     // +0x08 = &args
	std::uint32_t m_out_count;  // +0x0C
	std::uint32_t m_out_ptrs[4];  // +0x10
	struct record final { std::uint32_t v[3]; std::uint32_t padding; };
	record m_records[4];        // +0x20 (sub_100038C0 zeroes v[0..2] of each)
	std::uint32_t args[17];     // +0x60 (arg 0 is the return slot)
};

extern "C" char zpatch_script_call_by_hash(std::uint32_t hash, void* block) noexcept
{
	const auto handler = reinterpret_cast<void(__cdecl*)(void*)>(
	    static_cast<std::uintptr_t>(zpatch_script_resolve_with_block(hash, block)));
	if (handler) handler(block);
	return 1;
}

// One builder covers every wrapper: the sub_1005DE40/DE80 cleanup loops are
// identical (copy out each pending record, then return args[0]; DE80 returns
// only its low byte).
extern "C" std::uint32_t zpatch_script_call(std::uint32_t hash, std::uint32_t nargs,
                                            const std::uint32_t* args) noexcept
{
	zpatch_native_block block;
	// The original clears 0xA0 bytes (args[16] stays dirty; preserved).
	std::memset(&block, 0, offsetof(zpatch_native_block, args[16]));
	for (auto& record : block.m_records)
	{
		record.v[0] = 0;
		record.v[1] = 0;
		record.v[2] = 0;
	}
	const auto base = static_cast<std::uint32_t>(
	    reinterpret_cast<std::uintptr_t>(block.args));
	block.m_p_return = base;
	block.m_p_args = base;
	block.m_arg_count = nargs;
	for (std::uint32_t i = 0; i < nargs && i < 17u; ++i) block.args[i] = args[i];
	zpatch_script_call_by_hash(hash, &block);
	while (block.m_out_count != 0u)
	{
		--block.m_out_count;
		auto* destination = reinterpret_cast<std::uint32_t*>(
		    static_cast<std::uintptr_t>(block.m_out_ptrs[block.m_out_count]));
		const auto& source = block.m_records[block.m_out_count];
		destination[0] = source.v[0];
		destination[1] = source.v[1];
		destination[2] = source.v[2];
	}
	return block.args[0];
}

extern "C" std::uint8_t zpatch_script_call_bool(std::uint32_t hash, std::uint32_t nargs,
                                                const std::uint32_t* args) noexcept
{
	return static_cast<std::uint8_t>(zpatch_script_call(hash, nargs, args));
}

const char* zpatch_spawns_who_c(std::uint32_t id) noexcept
{
	// The original reads the native hash from [0x101580C8] (zpatch_salt_c8); the
	// loader's probe-miss fallback rewrites that slot on 1000/1010/1040.
	return reinterpret_cast<const char*>(static_cast<std::uintptr_t>(
	    zpatch_script_call(zpatch_salt_c8, 1, &id)));
}

void zpatch_spawns_print_c(const char* text) noexcept
{
	const std::uint32_t arg = static_cast<std::uint32_t>(
	    reinterpret_cast<std::uintptr_t>(text));
	zpatch_script_call(kScriptCallHash, 1, &arg);
}

// sub_10001280: the original's bounded formatter (buf, size, fmt, ...).
extern "C" int zpatch_spawns_sprintf(char* buffer, std::size_t size,
                                     const char* format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = _vsnprintf_s(buffer, size, _TRUNCATE, format, args);
	va_end(args);
	return result;
}
}  // extern "C"

#endif  // _WIN32
