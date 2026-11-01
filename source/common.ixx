// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.common — the shared types every module speaks.
//
// This module owns the small value types the pipeline and the feature modules
// pass around: the build enum, the mapped game image, the per-arm context, the
// scan match and the patch result. `common.hxx` is third-party/platform headers
// only; the INI document is `opp.config`, imported here so PatchContext's inline
// Option can call Config::Option.

module;

#include "common.hxx"

export module opp.common;

import opp.config;

export namespace opp
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

    // Rebased globals hold 32-bit VAs of game functions, not typed pointers.
    // This is the one conversion back to a callable: the caller may only use it
    // after its scan proved the address and the signature (the arm's pattern is
    // that proof), and the call's ABI is the one the recovered body transcribes.
    template <typename Fn>
    constexpr Fn GameFunction(uintptr_t address) noexcept
    {
        static_assert(std::is_pointer_v<Fn>,
                      "GameFunction returns a function pointer type");
        return reinterpret_cast<Fn>(address);
    }
} // namespace opp

#if defined(OPCEP_DUMP_TOOL)
// The dump records every module-relative address an installer emits, so the
// write-set baseline can canonicalise those operands. C linkage so the modules
// (this declaration) and the dump tool's plain TU (the definition) agree.
extern "C" void opp_record_dump_pointer(uint32_t value);
#endif
