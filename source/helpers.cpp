// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers.hxx"

#include "game.hxx"
#include "log.hxx"

#include <cstdio>

extern "C" uint8_t zpatch_opt_trace_arms;

namespace opp
{
    namespace
    {
        // The arm whose write is being guarded. The install pass is one
        // synchronous thread, so a plain global is enough and allocates
        // nothing inside DllMain.
        const char* g_currentArm = "startup";
        size_t g_writesApplied = 0;
        size_t g_writesSkipped = 0;

        void LogSkippedWrite(const void* site, const char* reason) noexcept
        {
            ++g_writesSkipped;
            Log("arm %s: skip write at %p: %s", g_currentArm, site, reason);
        }
    } // namespace

    void LogPatternMiss(std::string_view signature) noexcept
    {
        // One line per miss: arm, then the signature it asked for. The string
        // view points at a literal in the arm, so nothing is built at runtime.
        Log("arm %s: no match for signature [%.*s]", g_currentArm,
            static_cast<int>(signature.size()), signature.data());
    }

    const char* CurrentArm() noexcept
    {
        return g_currentArm;
    }

    size_t ArmWritesApplied() noexcept
    {
        return g_writesApplied;
    }

    size_t ArmWritesSkipped() noexcept
    {
        return g_writesSkipped;
    }

    bool SiteWritable(const PatchContext& ctx, const void* site, size_t length) noexcept
    {
        const auto base = reinterpret_cast<uintptr_t>(ctx.image.base);
        const auto address = reinterpret_cast<uintptr_t>(site);
        if (base == 0 || ctx.image.size == 0 || length == 0)
        {
            LogSkippedWrite(site, "no writable game image");
            return false;
        }
        if (address < base || address - base >= ctx.image.size)
        {
            LogSkippedWrite(site, "site outside the game image");
            return false;
        }
        if (length > ctx.image.size - (address - base))
        {
            LogSkippedWrite(site, "write crosses the end of the game image");
            return false;
        }
        ++g_writesApplied;
        return true;
    }

    void* AbiTarget(const char* name) noexcept
    {
        void* const target = AbiByName(name);
        if (target == nullptr)
        {
            // A withheld or unverified body: the original handler's install is
            // skipped when its destination has no live body.
            Log("arm %s: skip install: no body for %s", g_currentArm,
                name != nullptr ? name : "(null)");
            ++g_writesSkipped;
        }
        return target;
    }

    void SkipArmWrite(const char* reason) noexcept
    {
        LogSkippedWrite(nullptr, reason);
    }

    bool FusionFixGateClosed()
    {
        const HMODULE module = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi");
        if (module == nullptr)
            return false;
        const auto* image = reinterpret_cast<const uint8_t*>(module);
        return ModuleLacksSignature(image, kFusionFixProbeRva);
    }

    bool NewFilesConflictModulesPresent()
    {
        return GetModuleHandleA("GTAC.dll") != nullptr ||
               GetModuleHandleA("JacksIVFixes.asi") != nullptr;
    }

    void TraceArm(const char* name)
    {
        g_currentArm = name != nullptr ? name : "?";
        // Runtime opt-in (TraceInstallArms): the log names the arm before it
        // runs, so a crash during the install pass leaves the last arm visible.
        if (zpatch_opt_trace_arms != 0u)
            Log("arm %s", name);
#if defined(OPCEP_TRACE_INSTALLS)
        static std::FILE* log = std::fopen("IV.EFLC.OpenPreCEPatch.trace.log", "a");
        if (log)
        {
            std::fprintf(log, "arm %s\n", name);
            std::fflush(log);
        }
#else
        (void)name;
#endif
    }
} // namespace opp

// Copy `size` bytes and re-point the listed `call rel32` instructions at their
// original absolute targets. The offsets are the E8 opcodes; a non-E8 byte, an
// out-of-range offset or a displacement that does not fit fails the whole copy,
// so a shape mismatch on another build is a clean skip, never a half-fixed copy.
extern "C" bool zpatch_relocate_calls(void* destination, const void* source,
                                      size_t size, const uint32_t* callOffsets,
                                      size_t callCount) noexcept
{
    if (destination == nullptr || source == nullptr || callOffsets == nullptr ||
        size < 5u)
        return false;
    std::memcpy(destination, source, size);
    auto* dst = static_cast<uint8_t*>(destination);
    const auto* src = static_cast<const uint8_t*>(source);
    for (size_t i = 0; i < callCount; ++i)
    {
        const size_t offset = callOffsets[i];
        if (offset + 5u > size || dst[offset] != 0xE8u)
            return false;
        int32_t old_rel = 0;
        std::memcpy(&old_rel, src + offset + 1u, sizeof(old_rel));
        const uintptr_t target = reinterpret_cast<uintptr_t>(src + offset + 5u) +
                                 static_cast<uintptr_t>(static_cast<intptr_t>(old_rel));
        const intptr_t new_rel = static_cast<intptr_t>(target) -
                                 static_cast<intptr_t>(reinterpret_cast<uintptr_t>(
                                     dst + offset + 5u));
        if (new_rel > INT32_MAX || new_rel < INT32_MIN)
            return false;
        const auto narrowed = static_cast<int32_t>(new_rel);
        std::memcpy(dst + offset + 1u, &narrowed, sizeof(narrowed));
    }
    return true;
}
