#pragma once
// Declarations of the hook bodies: the game jumps or calls straight into them, so each
// keeps the original site's calling convention and register contract. The naked ones are
// defined in `source/hook_bodies.cpp`, the compiled helpers in the plain units beside it
// (`source/ooc.cpp`, `source/new_saves.cpp`, ...); their `@exact` proof is
// `tools/verify_bodies.py`.
//
// The installer arms name some bodies directly (`&abi_...`) and the write-set dump names
// all of them; both need this list. C linkage is what lets the inline assembly address the
// published globals by name.

#include <cstdint>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "common.h"
#include "hooks.h"
#include "xlive/xliveless.h"

extern "C"
{
#include "hook_bodies_decls.inc"
}  // extern "C"
