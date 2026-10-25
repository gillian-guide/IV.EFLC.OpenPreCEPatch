// SPDX-License-Identifier: GPL-3.0-or-later

#include "helpers.hxx"

#include "game.hxx"
#include "data/useless_checks.h"

#include <cstdio>

namespace opp
{
    bool bMenuIntegration = false;

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

    void ApplyUselessChecks(const PatchContext& ctx)
    {
        if (!ctx.Option("RemoveUselessChecks", "0"))
            return;

        for (size_t i = 0; i < kUselessCheckCount; ++i)
        {
            const UselessCheck& check = kUselessChecks[i];
            if (check.build != static_cast<uint8_t>(ctx.build))
                continue;
            if (static_cast<size_t>(check.rva) + check.length > ctx.image.size)
                continue;
            injector::WriteMemoryRaw(ctx.image.base + check.rva,
                                     const_cast<uint8_t*>(check.bytes), check.length, true);
        }
    }

    void TraceArm(const char* name)
    {
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
