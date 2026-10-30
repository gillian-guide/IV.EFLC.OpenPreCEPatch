// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers.hxx"

#include "game.hxx"
#include "log.hxx"

#include <cstdio>

extern "C" uint8_t zpatch_opt_trace_arms;

namespace opp
{
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
