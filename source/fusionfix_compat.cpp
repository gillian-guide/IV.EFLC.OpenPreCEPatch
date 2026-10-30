// SPDX-License-Identifier: GPL-3.0-or-later

#include "fusionfix_compat.hxx"

#include "log.hxx"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace opp
{
    namespace
    {
        // Each row is a subsystem FusionFix already replaces. The reason names the
        // FusionFix module/setting a maintainer can check when revisiting the row.
        constexpr FusionFixConflict kConflicts[] = {
            {"BikeFeetFix", "FusionFix fixes.ixx (feet on bikes)"},
            {"BikePhoneAnimsFix", "FusionFix fixes.ixx (phone/bike animation)"},
            {"BuildingAlphaFix", "FusionFix fixes.ixx (LOD fade-in)"},
            {"BuildingDynamicShadows", "FusionFix shadows.ixx"},
            {"CarDynamicShadowFix", "FusionFix shadows.ixx / nightshadows.ixx"},
            {"CarPartsShadowFix", "FusionFix shadows.ixx"},
            {"CutsceneFixes", "FusionFix cutscenecam.ixx / frameratevigilante.ixx"},
            {"EmissiveLerpFix", "FusionFix fixes.ixx / shaders.ixx (emissive state)"},
            {"EpisodicVehicleSupport", "FusionFix episodiccontent.ixx (EpisodicVehicles)"},
            {"EpisodicWeaponSupport", "FusionFix episodiccontent.ixx (EpisodicWeapons)"},
            {"ForceCarHeadlightShadows", "FusionFix nightshadows.ixx"},
            {"ForceShadowsOnObjects", "FusionFix shadows.ixx"},
            {"HighFPSBikePhysicsFix", "FusionFix frameratevigilante.ixx (vehicle physics)"},
            {"HighFPSSpeedupFix", "FusionFix frameratevigilante.ixx (time scale)"},
            {"IncreasePtrNodes", "FusionFix limits.ixx (PtrNode Double pool)"},
            {"IncreaseVehicleModelLimit", "FusionFix limits.ixx"},
            {"IncreaseVehicleStructLimit", "FusionFix limits.ixx (VehicleStruct pool)"},
            {"MouseFix", "FusionFix rawinput.ixx / sniper.ixx"},
            {"NoImpostorHeightCutoff", "FusionFix lodlights.ixx / fixes.ixx"},
            {"NoLODLightHeightCutoff", "FusionFix lodlights.ixx"},
            {"NoLiveryLimit", "FusionFix limits.ixx (LiveriesLimit)"},
            {"NoLoadingSleep", "FusionFix loadingdelays.ixx patches the same push 0x1388 immediate to 0"},
            {"OutOfCommissionFix", "FusionFix frameratevigilante.ixx / fixes.ixx (high-FPS QTE)"},
            {"PipeBombDropIconSupport", "FusionFix episodiccontent.ixx"},
            {"PoliceEpisodicWeaponSupport", "FusionFix episodiccontent.ixx (TBoGTPoliceWeapons)"},
            {"ReversingLightFix", "FusionFix fixes.ixx (reversing light bone)"},
            {"RestoreDeathMusic", "FusionFix deathmusic.ixx (DeathMusic)"},
            {"SCOSignatureFix", "FusionFix episodiccontent.ixx (RemoveSCOSignatureCheck)"},
            {"SpeedupBugFix", "FusionFix frameratevigilante.ixx (time scale)"},
            {"TBoGTHeliHeightLimit", "FusionFix episodiccontent.ixx NOPs the same height check"},
            {"VSyncFix", "FusionFix framelimit.ixx / windowed.ixx"},
        };

        bool bFusionFixPresent = false;

        bool EqualsIgnoreCase(std::string_view left, std::string_view right)
        {
            if (left.size() != right.size())
                return false;
            for (size_t i = 0; i < left.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(left[i])) !=
                    std::tolower(static_cast<unsigned char>(right[i])))
                    return false;
            }
            return true;
        }

        bool LooksLikeFusionFix(const std::filesystem::path& path)
        {
            std::string name = path.filename().string();
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return name.find("fusionfix") != std::string::npos &&
                   path.extension() == ".asi";
        }
    } // namespace

    const FusionFixConflict* FusionFixConflicts(size_t& count) noexcept
    {
        count = sizeof(kConflicts) / sizeof(kConflicts[0]);
        return kConflicts;
    }

    bool DetectFusionFix(const std::filesystem::path& moduleDirectory)
    {
        bool present = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi") != nullptr ||
                       GetModuleHandleW(L"GTAIV.FusionFix.asi") != nullptr;

        if (!present && !moduleDirectory.empty())
        {
            std::error_code error;
            for (const auto& entry :
                 std::filesystem::directory_iterator(moduleDirectory, error))
            {
                if (!entry.is_regular_file(error))
                    continue;
                if (LooksLikeFusionFix(entry.path()))
                {
                    present = true;
                    break;
                }
            }
        }

        SetFusionFixPresent(present);
        return present;
    }

    void SetFusionFixPresent(bool present) noexcept
    {
        bFusionFixPresent = present;
    }

    bool FusionFixPresent() noexcept
    {
        return bFusionFixPresent;
    }

    const FusionFixConflict* FindConflict(const char* option) noexcept
    {
        if (option == nullptr)
            return nullptr;
        for (const FusionFixConflict& conflict : kConflicts)
        {
            if (EqualsIgnoreCase(conflict.option, option))
                return &conflict;
        }
        return nullptr;
    }

    bool IsFusionFixGated(const char* option) noexcept
    {
        return bFusionFixPresent && FindConflict(option) != nullptr;
    }

    const char* FusionFixGateReason(const char* option) noexcept
    {
        const FusionFixConflict* conflict = FindConflict(option);
        return conflict ? conflict->reason : nullptr;
    }
} // namespace opp
