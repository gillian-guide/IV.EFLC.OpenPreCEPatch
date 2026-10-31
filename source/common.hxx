// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared types and third-party headers for OpenPreCEPatch.
//
// The layout follows GTAIV.EFLC.FusionFix: a flat `source/` root, one file per
// subsystem, `includes/` for the third-party submodules and `data/plugins/` for
// the shipped INI.

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
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Hooking.Patterns.h>
#if defined(OPCEP_DUMP_TOOL)
#include <injector/injector.hpp>
#else
#include <injector/injector.hpp>
#include <injector/calling.hpp>
#include <injector/hooking.hpp>
#include <injector/utility.hpp>
#endif

#include "config.hxx"

namespace opp
{
    // Result code of a patch pass; `detail` is always a fixed literal, never an
    // allocated message (the original reports through the log).
    enum class PatchResultCode
    {
        ok,
        out_of_bounds,
        invalid_input,
        io_error,
        parse_error,
        not_found,
        unsupported_platform,
        mismatch,
    };

    struct PatchResult final
    {
        PatchResultCode code{PatchResultCode::ok};
        std::string_view detail{};
        constexpr explicit operator bool() const noexcept { return code == PatchResultCode::ok; }
    };

    // The original mod's classification enum. The numeric value is the build id
    // every build mask indexes (bit i == ordinal i); it must not change.
    enum class GameBuild : uint8_t
    {
        unknown = 0,
        v1000 = 1,
        v1040 = 5,
        v1004 = 6,
        v1060 = 7,
        v1070 = 8,
        v1080 = 9,
        v1100 = 10,
        v1110 = 11,
        v1120 = 12,
        v1130 = 13,
        v1061 = 15,
        v12043 = 18,
    };

    // The game image as it is mapped in the process. Installers write through it;
    // the standalone tools hand in a mapped copy of the same executable.
    struct GameImage
    {
        uint8_t* base{};
        size_t size{};
    };

    // The state every feature installer receives.
    struct PatchContext
    {
        GameImage image;
        uintptr_t gameBase{};
        GameBuild build{GameBuild::unknown};
        const Config& config;

        // An option is on when its value parses to exactly 1. An absent key uses
        // the default the option's own arm substitutes, which is not always off.
        bool Option(const char* key, const char* defaultValue) const
        {
            return config.Option(key, defaultValue);
        }
    };

    // One bounded signature scan result.
    struct Match
    {
        const uint8_t* address{};
    };

    // A PE32 image that the dump/tests can map at its preferred base.
    struct ModuleView
    {
        const uint8_t* base{};
        size_t size{};
    };

#if defined(OPCEP_DUMP_TOOL)
    // The dump records every module-relative address an installer emits, so the
    // write-set baseline can canonicalise those operands. Declared in this
    // shared header so both opp.game (rebased lookups) and opp.patch (arm
    // writes) can call the one definition in the dump tool.
    void RecordDumpPointer(uint32_t value);
#endif
} // namespace opp
