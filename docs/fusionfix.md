# FusionFix conflicts

FusionFix replaces several of the same subsystems OpenPreCEPatch patches. When a
FusionFix ASI is next to the game (or already loaded), OpenPreCEPatch forces the
options below off. The gate is in `source/fusionfix_compat.cpp`; the INI marks
the same keys with `FF`.

The list is deliberately conservative: an option is gated when FusionFix
installs on the same subsystem, even if the exact byte sites differ, because two
mods writing one subsystem is not a combination this project can test.

| option | FusionFix module / setting |
| --- | --- |
| BikeFeetFix | `FusionFix fixes.ixx (feet on bikes)` |
| BikePhoneAnimsFix | `FusionFix fixes.ixx (phone/bike animation)` |
| BuildingAlphaFix | `FusionFix fixes.ixx (LOD fade-in)` |
| BuildingDynamicShadows | `FusionFix shadows.ixx` |
| CarDynamicShadowFix | `FusionFix shadows.ixx / nightshadows.ixx` |
| CarPartsShadowFix | `FusionFix shadows.ixx` |
| CutsceneFixes | `FusionFix cutscenecam.ixx / frameratevigilante.ixx` |
| EmissiveLerpFix | `FusionFix fixes.ixx / shaders.ixx (emissive state)` |
| EpisodicVehicleSupport | `FusionFix episodiccontent.ixx (EpisodicVehicles)` |
| EpisodicWeaponSupport | `FusionFix episodiccontent.ixx (EpisodicWeapons)` |
| FastLoading | `FusionFix loadingdelays.ixx / framelimit.ixx` |
| ForceCarHeadlightShadows | `FusionFix nightshadows.ixx` |
| ForceShadowsOnObjects | `FusionFix shadows.ixx` |
| HighFPSBikePhysicsFix | `FusionFix frameratevigilante.ixx (vehicle physics)` |
| HighFPSSpeedupFix | `FusionFix frameratevigilante.ixx (time scale)` |
| IncreasePtrNodes | `FusionFix limits.ixx (PtrNode Double pool)` |
| IncreaseVehicleModelLimit | `FusionFix limits.ixx` |
| IncreaseVehicleStructLimit | `FusionFix limits.ixx (VehicleStruct pool)` |
| MouseFix | `FusionFix rawinput.ixx / sniper.ixx` |
| NoImpostorHeightCutoff | `FusionFix lodlights.ixx / fixes.ixx` |
| NoLODLightHeightCutoff | `FusionFix lodlights.ixx` |
| NoLiveryLimit | `FusionFix limits.ixx (LiveriesLimit)` |
| NoLoadingSleep | `FusionFix loadingdelays.ixx (Sleep hook)` |
| OutOfCommissionFix | `FusionFix frameratevigilante.ixx / fixes.ixx (high-FPS QTE)` |
| PipeBombDropIconSupport | `FusionFix episodiccontent.ixx` |
| PoliceEpisodicWeaponSupport | `FusionFix episodiccontent.ixx (TBoGTPoliceWeapons)` |
| ReversingLightFix | `FusionFix fixes.ixx (reversing light bone)` |
| RestoreDeathMusic | `FusionFix deathmusic.ixx (DeathMusic)` |
| SCOSignatureFix | `FusionFix episodiccontent.ixx (RemoveSCOSignatureCheck)` |
| SpeedupBugFix | `FusionFix frameratevigilante.ixx (time scale)` |
| TBoGTHeliHeightLimit | `FusionFix episodiccontent.ixx NOPs the same height check` |
| VSyncFix | `FusionFix framelimit.ixx / windowed.ixx` |

Options not in this table patch sites FusionFix does not touch (for example the
path-node, plane, projectile, MP-sync, save and file-compatibility fixes), so
they stay active alongside FusionFix. The folded improvements ride on their
original options and are gated with them.

`SkipIntro`, `SkipMenu`, `BorderlessWindowed`, `DoNotPauseOnMinimize` and
`VRAMFix` are XLivelessAddon options now. Their rows stay in the main table only
so a stale key in an old INI is still reported as gated; the main module does not
install those features. The fork applies its own FusionFix-first gating (it
disables every overlapping feature when a `*FusionFix*.asi` is present).

Detection checks the module directory for any `*.asi` whose name contains
`FusionFix`, and also checks whether `GTAIV.EFLC.FusionFix.asi` /
`GTAIV.FusionFix.asi` is already loaded. It runs before the INI is read, so the
gate applies to every option call, including the loader's own menu and local
settings paths.
