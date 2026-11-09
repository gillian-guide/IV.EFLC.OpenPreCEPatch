#pragma once

// The shared include header of every module unit and of the two plain
// translation units. Everything a module needs before its purview opens lives
// here, so a module file is `#include <common.hxx>`, `export module x;`, then
// imports -- the layout GTAIV.EFLC.FusionFix (GPL-3.0, ThirteenAG) uses.
//
// A module's global fragment may name declarations from these headers in its
// purview; that is the MSVC-supported shape this tree builds with.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <Psapi.h>
#include <subauth.h>

#include "injector/injector.hpp"
#include "injector/calling.hpp"
#include "injector/hooking.hpp"
#include "injector/utility.hpp"
#include "Hooking.Patterns.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>
