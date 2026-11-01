// SPDX-License-Identifier: GPL-3.0-or-later
//
// Third-party and platform headers only.
//
// Every module interface unit includes this in its global module fragment; the shared
// project types are exported by `opp.common` and the INI document by `opp.config`. A
// module purview must not see a project header, so nothing here declares project
// entities. Layout follows GTAIV.EFLC.FusionFix: flat `source/`, `external/` for the
// vendored submodules, `data/plugins/` for the shipped INI.

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <windows.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <Hooking.Patterns.h>
#include <injector/injector.hpp>
#if !defined(OPCEP_DUMP_TOOL)
#include <injector/calling.hpp>
#include <injector/hooking.hpp>
#include <injector/utility.hpp>
#endif
