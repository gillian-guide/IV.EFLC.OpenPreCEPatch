// The one header every translation unit includes (FusionFix-style layout).
//
// Include it in the `module;` global fragment, so Win32, the standard library and
// the two vendored libraries are declared identically everywhere. A type or
// constant that several modules share belongs in `common.ixx`, not here.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shlwapi.h>
#include <stdint.h>
#include <tlhelp32.h>
#include <winver.h>
#include <intrin.h>
#include <xmmintrin.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

// The write layer differs per target: the shipped ASI uses the vendored 32-bit
// injector, the 64-bit write-set dump and the trace build use their own shims.
#if defined(ZP_DUMP_TOOL)
#include "../tests/dump_injector/injector/injector.hpp"
#elif defined(ZP_TRACE_WRITES)
#include "../tests/trace_injector/injector/injector.hpp"
#else
#include "injector/injector.hpp"
#endif
#include "Hooking.Patterns.h"
