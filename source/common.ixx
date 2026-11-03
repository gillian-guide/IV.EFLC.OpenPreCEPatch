// SPDX-License-Identifier: GPL-3.0-or-later
//
// opp.common — the shared types every module speaks.
//
// Owns the small value types the event tiers and the feature modules pass around: the
// build enum, the mapped game image, the per-arm context, the scan match, the patch
// result and the named event list. `common.hxx` is third-party/platform headers only;
// the INI document is `opp.config`, imported here so PatchContext's inline Option can
// call Config::Option.

module;

#include "common.hxx"

#include <algorithm>
#include <functional>

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

    // Optional per-arm instrumentation for the offline dump tool. The ASI never
    // installs an observer, so the release path stays allocation- and call-free.
    struct PatchObserver
    {
        void* user{};
        void (*begin)(void* user, const char* name){};
        void (*end)(void* user, const char* name){};
    };

    // A FusionFix-style event: a named std::function handler list. Feature modules
    // subscribe their arms at static-init time and the install driver in opp.game
    // runs the publish/install/post tiers in order.
    //
    // Inside a tier the handlers run in *name* order, not subscription order:
    // subscription order is the executable's static-init (link) order, which differs
    // between the ASI, the dump and the tests, and two arms that patch the same site
    // must not depend on which binary runs them. Name order is deterministic
    // everywhere; the 96-cell matrix proves it is write-equivalent, and
    // tests/order_golden.txt freezes the per-tier arm set. An unnamed handler runs
    // last (the native tests use that for a throwaway subscription).
    template <typename... Args>
    class Event
    {
    public:
        using Handler = std::function<void(Args...)>;

        // Appends `handler`; a null handler is ignored.
        void Add(Handler handler, const char* name = nullptr)
        {
            if (handler)
                handlers_.push_back({std::move(handler), name});
        }
        void operator+=(Handler handler) { Add(std::move(handler)); }

        size_t size() const noexcept { return handlers_.size(); }
        const char* name(size_t index) const noexcept
        {
            return index < handlers_.size() ? handlers_[index].name : nullptr;
        }

        // Optional per-arm instrumentation: the hooks bracket every handler, and
        // `unnamed` is the label they see for a handler added without a name.
        void SetObserver(const PatchObserver& observer, const char* unnamed = nullptr) noexcept
        {
            observer_ = observer;
            unnamed_ = unnamed;
        }

        // Runs every handler in name order, bracketing each with the observer's
        // begin/end hooks when one is installed.
        void Execute(Args... args) const
        {
            for (const size_t index : ExecutionOrder())
            {
                const Handler& handler = handlers_[index].handler;
                const char* label =
                    handlers_[index].name != nullptr ? handlers_[index].name : unnamed_;
                if (observer_.begin != nullptr)
                    observer_.begin(observer_.user, label != nullptr ? label : "?");
                handler(args...);
                if (observer_.end != nullptr)
                    observer_.end(observer_.user, label != nullptr ? label : "?");
            }
        }

    private:
        struct Entry
        {
            Handler handler;
            const char* name{};
        };

        std::vector<size_t> ExecutionOrder() const
        {
            std::vector<size_t> order(handlers_.size());
            for (size_t i = 0; i < order.size(); ++i)
                order[i] = i;
            std::sort(order.begin(), order.end(), [&](size_t left, size_t right) {
                const char* a = handlers_[left].name;
                const char* b = handlers_[right].name;
                if (a == nullptr || b == nullptr)
                    return a != nullptr;  // unnamed handlers last
                return std::strcmp(a, b) < 0;
            });
            return order;
        }

        std::vector<Entry> handlers_;
        PatchObserver observer_{};
        const char* unnamed_{};
    };

    // The three tiers, run in this order by PatchGame. The publish tier is
    // load-bearing: a site's original bytes must be read (and its resume
    // published) before any install overwrites them.
    Event<PatchContext&>& PublishEvent() noexcept;
    Event<PatchContext&>& InstallEvent() noexcept;
    Event<PatchContext&>& PostEvent() noexcept;

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

    // Fundamental float/double constants used at ABI stores. The bit patterns
    // are pinned here once; modules store std::bit_cast<uint32_t>(constant) at
    // the boundary instead of spelling the hex bits in logic.
    inline constexpr float kDrivebyFovDegrees = 45.0f;
    inline constexpr float kHalfPi = 1.5707964f;          // pi/2
    inline constexpr float kDefaultConeOffset = 2.0f;
    inline constexpr double kCutsceneZero = 0.0;
    inline constexpr double kCutsceneLimit = 0.3333;      // recovered bits, not 1/3
    inline constexpr float kCutsceneEpsilon = 0.05f;
    inline constexpr float kCutsceneHalf = 0.5f;
    inline constexpr float kBikePhysicsFloor = 0.0066666667f;
    static_assert(std::bit_cast<uint32_t>(kDrivebyFovDegrees) == 0x42340000u);
    static_assert(std::bit_cast<uint32_t>(kHalfPi) == 0x3FC90FDBu);
    static_assert(std::bit_cast<uint32_t>(kDefaultConeOffset) == 0x40000000u);
    static_assert(std::bit_cast<uint64_t>(kCutsceneZero) == 0x0ull);
    static_assert(std::bit_cast<uint64_t>(kCutsceneLimit) == 0x3FD554C985F06F69ull);
    static_assert(std::bit_cast<uint32_t>(kCutsceneEpsilon) == 0x3D4CCCCDu);
    static_assert(std::bit_cast<uint32_t>(kCutsceneHalf) == 0x3F000000u);
    static_assert(std::bit_cast<uint32_t>(kBikePhysicsFloor) == 0x3BDA740Eu);

    // Deleters for C-owned resources kept RAII: the allocation itself stays the
    // original's (malloc/free), only the ownership is expressed in the type.
    struct FreeDeleter final
    {
        void operator()(void* pointer) const noexcept { std::free(pointer); }
    };

    struct FileCloser final
    {
        void operator()(std::FILE* file) const noexcept
        {
            if (file != nullptr)
                std::fclose(file);
        }
    };
} // namespace opp

namespace opp
{
    Event<PatchContext&>& PublishEvent() noexcept
    {
        static Event<PatchContext&> event;
        return event;
    }

    Event<PatchContext&>& InstallEvent() noexcept
    {
        static Event<PatchContext&> event;
        return event;
    }

    Event<PatchContext&>& PostEvent() noexcept
    {
        static Event<PatchContext&> event;
        return event;
    }
} // namespace opp

#if defined(OPCEP_DUMP_TOOL)
// The dump records every module-relative address an installer emits, so the
// write-set baseline can canonicalise those operands. C linkage so the modules
// (this declaration) and the dump tool's plain TU (the definition) agree.
extern "C" void opp_record_dump_pointer(uint32_t value);
#endif
