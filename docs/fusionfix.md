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
| AddOptionsInMenu | `settings.ixx` menu hooks |
| BikeFeetFix | `fixes.ixx` feet on bikes |
| BikePhoneAnimsFix | `fixes.ixx` phone/bike animation |
| BorderlessWindowed | `windowed.ixx` (`PREF_BORDERLESS`) |
| BuildingDynamicShadows | `shadows.ixx` |
| CarDynamicShadowFix | `shadows.ixx`, `nightshadows.ixx` |
| CarPartsShadowFix | `shadows.ixx` |
| CutsceneFixes | `cutscenecam.ixx`, `frameratevigilante.ixx` |
| DoNotPauseOnMinimize | `windowed.ixx` (`PREF_BLOCKONLOSTFOCUS`) |
| EmissiveLerpFix | `fixes.ixx`, `shaders.ixx` emissive state |
| EpisodicVehicleSupport | `episodiccontent.ixx` (`EpisodicVehicles`) |
| EpisodicWeaponSupport | `episodiccontent.ixx` (`EpisodicWeapons`) |
| FastLoading | `loadingdelays.ixx`, `framelimit.ixx` |
| ForceCarHeadlightShadows | `nightshadows.ixx` |
| ForceDynamicShadowsEverywhere | `shadows.ixx` |
| ForceShadowsOnObjects | `shadows.ixx` |
| HighFPSBikePhysicsFix | 
rameratevigilante.ixx vehicle physics |
| HighFPSSpeedupFix | 
rameratevigilante.ixx time scale |
| HighQualityReflections | `experimental.ixx`, `reflectionmsaa.ixx` |
| ImprovedShaderStreaming | `shaders.ixx` |
| IncreasePtrNodes | `limits.ixx` (`PtrNode Double` pool) |
| IncreaseVehicleModelLimit | `limits.ixx` |
| IncreaseVehicleStructLimit | `limits.ixx` (`VehicleStruct` pool) |
| LocalSettingsPath | `userdata.ixx` (`CustomUserProfilePath`) |
| MouseFix | `rawinput.ixx`, `sniper.ixx` |
| NoImpostorHeightCutoff | `lodlights.ixx`, `fixes.ixx` |
| NoLODLightHeightCutoff | `lodlights.ixx` |
| NoLiveryLimit | `limits.ixx` (`LiveriesLimit`) |
| NoLoadingSleep | `loadingdelays.ixx` Sleep hook |
| PipeBombDropIconSupport | `episodiccontent.ixx` |
| PoliceEpisodicWeaponSupport | `episodiccontent.ixx` (`TBoGTPoliceWeapons`) |
ixes.ixx / global code patches |
| ReversingLightFix | `fixes.ixx` reversing-light bone |
| RestoreDeathMusic | `deathmusic.ixx` (`DeathMusic`) |
| SCOSignatureFix | `episodiccontent.ixx` (`RemoveSCOSignatureCheck`) |
| SpeedupBugFix | 
rameratevigilante.ixx time scale |
| SkipIntro | `skipintro.ixx` (`PREF_SKIP_INTRO`) |
| SkipMenu | `skipintro.ixx` (`PREF_SKIP_MENU`) |
| TBoGTHeliHeightLimit | `episodiccontent.ixx` |
| VRAMFix | memory.ixx VRAM detection |
| VSyncFix | `framelimit.ixx`, `windowed.ixx` |

Options not in this table patch sites FusionFix does not touch (for example the
path-node, plane, projectile, MP-sync and file-compatibility fixes), so they stay
active alongside FusionFix.

Detection checks the module directory for any `*.asi` whose name contains
`FusionFix`, and also checks whether `GTAIV.EFLC.FusionFix.asi` /
`GTAIV.FusionFix.asi` is already loaded. It runs before the INI is read, so the
gate applies to every option call, including the loader's own menu and local
settings paths.
