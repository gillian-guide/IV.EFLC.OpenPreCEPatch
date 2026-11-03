#pragma once
// Helpers the hook bodies call: the unchecked write wrappers, the shared
// game-image scan and the compiled bodies that live outside the assembly unit
// (P3).
//
// The helpers are declared with C linkage so the inline assembly can name them;
// the write wrappers stay inline because they are the same three VirtualProtect
// stores the originals inline.

#include "common.hxx"
#include "common.h"

#include <cstdint>
#include <string_view>

// The model-catalogue hash the native "print" call is made through; the
// BadPlayerModel fallback and the print helper share it.
inline constexpr std::uint32_t kScriptCallHash = 0x79023CFCu;

// As in every original handler, the VirtualProtect result is ignored and the
// write happens anyway (an unmapped target faults).
inline void wp_byte(void* address, std::uint8_t value) noexcept
{
	injector::WriteMemory<std::uint8_t>(address, value, true);  // byte store
}

inline void wp_word(void* address, std::uint16_t value) noexcept
{
	injector::WriteMemory<std::uint16_t>(address, value, true);  // word store
}

inline void wp_dword(void* address, std::uint32_t value) noexcept
{
	injector::WriteMemory<std::uint32_t>(address, value, true);  // dword store
}

extern "C"
{
// The running image's first match; null on a module-header or scan miss
// (defined with the bodies, source/hook_bodies.cpp).
const std::uint8_t* scan_game(std::string_view signature) noexcept;

// The native-script call layer (source/script_calls.cpp): the resolver's SEH
// wrapper the drive-by helper calls, the wrapper that keeps a call block, and the
// spawn/model-name helpers the probe and the print bodies call.
std::uint32_t zpatch_script_resolve(std::uint32_t hash);
std::uint32_t zpatch_script_resolve_with_block(std::uint32_t hash, void* block);
std::uint32_t zpatch_script_resolve_seh(std::uint32_t hash, std::uint32_t edx_arg);
std::uint32_t zpatch_driveby_resolve_seh(std::uint32_t hash, std::uint32_t edx_arg);
std::uint32_t zpatch_script_call(std::uint32_t hash, std::uint32_t nargs,
                                 const std::uint32_t* args) noexcept;
std::uint8_t zpatch_script_call_bool(std::uint32_t hash, std::uint32_t nargs,
                                     const std::uint32_t* args) noexcept;
const char* zpatch_bpm_model_name(std::uint32_t hash) noexcept;
const char* zpatch_spawns_who_c(std::uint32_t id) noexcept;
void zpatch_spawns_print_c(const char* text) noexcept;
int zpatch_spawns_sprintf(char* buffer, std::size_t size, const char* format, ...);

// The compiled bodies whose definition lives outside the assembly unit: the
// NewSaves tramp's prepare step (source/new_saves.cpp) and the
// steer/engine/plane consumer (source/vehicle_steer.cpp).
void abi_new_saves_prepare() noexcept;
std::uint32_t abi_steer_body() noexcept;
}
