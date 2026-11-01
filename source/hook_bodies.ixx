// zp.hook_bodies - the interface of the bodies the game jumps or calls straight
// into: the resolver that maps an original VA to a body, the rebased-global
// accessors, and the dump's canonicalisation entry points.
//
// Declarations only: the bodies are `__declspec(naked)` 32-bit assembly and MSVC
// cannot write a naked definition into a module interface (C2488), so they are
// defined in `source/hook_bodies.cpp`. `src/hook_bodies.h` declares the C-linkage
// bodies for the plain translation units (some install arms name them directly).

module;

#include "common.hxx"
#include "common.h"

export module zp.hook_bodies;

import zp.common;

export
{
std::uintptr_t ResolveHookBody(std::uint32_t reference_va, void* user) noexcept;

std::size_t HookBodyCount() noexcept;

// src/rebased.h's accessors (declared there, implemented here).
void SetOverrideGtaDatPath(std::string_view path) noexcept;

std::uint32_t& StartupTimeGlobal() noexcept;

std::uintptr_t FindGlobalValue(std::string_view option) noexcept;

std::uintptr_t FindGlobalSymbol(std::string_view symbol) noexcept;

std::uintptr_t FindGlobalSlot(std::string_view slot_name) noexcept;

#if defined(ZP_DUMP_TOOL)
std::size_t DumpHookBodyCount() noexcept;

std::uint32_t DumpHookBodyReference(std::size_t index) noexcept;

std::uintptr_t DumpHookBodyAddress(std::size_t index) noexcept;
#endif
}  // export
