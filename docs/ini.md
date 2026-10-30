# INI reference

Generated from `data/plugins/IV.EFLC.OpenPreCEPatch.ini`.

Tags: `[all]` / `[1060+]` / `[all-1080,1130]` list the game builds an option applies to.
`FF` means the option is automatically disabled when a FusionFix ASI is present.

## STABILITY

crash, freeze and game-logic fixes

| key | default | notes |
| --- | --- | --- |
| `AimProjectileCrashFix` | `1` | crash fix, usually happens in GTAC [all] |
| `BadPlayerModelCrashFix` | `1` | fix crash if another player switches to an invalid playermodel [all] |
| `BenchmarkFix` | `1` | fix crash after benchmark ends [all-1080,1130] |
| `BlackscreenFix` | `1` | fix a crash method that Modna uses [all] |
| `CollisionDeadlockFix` | `1` | freeze fix, usually happens in GTAC [all] |
| `CombinedRadioFreezeFix` | `1` | fix freeze after Concrete Jungle with the 3 EFLC exclusive radio stations installed [all] |
| `CrashMsgFix` | `1` | fix the other crash method that Modna uses [all] |
| `EpisodeSwitchFreezeFix` | `1` | fix crash when switching episode mid game [all] |
| `GarageModelFix` | `1` | fixes cars being randomly changed in parking spots if addons models are added [all] |
| `GroupHackFix` | `1` | MP crash method fix [all] |
| `MPNikoCrashFix` | `1` | fix crash if another player changes their model to Niko then changes it back to something else [all] |
| `OutOfCommissionFix` | `1` | fixes the climbing quick-time event in Out of Commission at high FPS (1060+ only) [all] |
| `PathNodeCrashFix` | `1` | crash fix, usually happens in GTAC [all] |
| `PedModelsInCutscenesFix` | `1` | fixes crash when starting cutscenes as a ped [all] |
| `PlaneCrashFix` | `1` | fix random crashing while using actual planes [all] |
| `PlayerAsPedComponentsFix` | `1` | fix Niko peds' clothes never changing properly in the trainer [all] |
| `SMPA60Fix` | `1` | fix SMPA60 both when abusing the poor game with car mods and if another player is doing that in MP [all] |
| `SpeedupBugFix` | `1` | fixes the game being permanently sped up slightly in some cases [all; FF] |
| `SuperLODFix` | `1` | fix crash if superlod is spawned, disable this for GTAC as it's already fixed there [1070,1080] |
| `TryToSkipAllErrors` | `0` | attempt to completely ignore any and all error messages that would ever pop up this will likely still eventually crash your game but at least it'll be an actual crash instead of an in most cases useless error [all] |
| `WantedLevelCrashFix` | `1` | fix rare crash when getting a wanted level [all] |

## PERFORMANCE

loading, streaming and memory

| key | default | notes |
| --- | --- | --- |
| `FastLoading` | `1` | cut loading times in half or more (can be combined with ColAccel to get even faster loading) [all; FF] |
| `HighFPSSpeedupFix` | `1` | fix game speeding up above 300FPS [all; FF] |
| `ImprovedShaderStreaming` | `1` | improved a really badly coded function, reduces stutter somewhat [all; FF] |
| `IncreasePtrNodes` | `1` | increase the PtrNode limit, fixes random crashing in big Cunning Stunts maps [all; FF] |
| `IncreaseVehicleModelLimit` | `1` | increase the amount of possible vehicles.ide entries to 1024 [all; FF] |
| `IncreaseVehicleStructLimit` | `1` | increase the amount of vehicles with different models that can be loaded at one time [all; FF] |
| `NewMemorySystem` | `0` | reimplements memory allocation, possibly increasing performance, reducing loading times and memory usage and completely eliminating SMPA* errors [all] |
| `NoLoadingSleep` | `1` | makes the game get to the main menu faster [all; FF] |
| `NoPhysicsSleep` | `1` | reduces the threshold velocity below which physics objects can "go to sleep" at, this means vehicles stop in a more realistic way but it might be a performance hit [all] |
| `RemoveUselessChecks` | `0` | might be a small performance boost but highly incompatible with other mods [1040,1070,1080] |
| `StartupTimeFix` | `1` | fixes the 15+ second startup times before the game window even pops up [all] |

## GRAPHICS

shadows, lights, LODs and reflections

| key | default | notes |
| --- | --- | --- |
| `BuildingAlphaFix` | `1` | fix missing fadein for LODs in patch 6+, disable this on patch 5 and below [all] |
| `BuildingDynamicShadows` | `0` | enable night shadows cast from buildings [all; FF] |
| `CarDecalsFix` | `1` | fix bulletholes not appearing on some modded cars outside TBoGT [all] |
| `CarDynamicShadowFix` | `1` | tunnel, lamppost and heli spotlight shadows for cars (disables headlight shadows) [all; FF] |
| `CarPartsShadowFix` | `1` | tunnel, lamppost and heli spotlight shadows for detached car parts [all; FF] |
| `CutsceneFixes` | `1` | fix zoomed in cutscenes and jittery cutscene camera motion at high framerates [all; FF] |
| `DualVehicleHeadlights` | `0` | cast 2 separate lights for car headlights with their own shadows [all] |
| `EmissiveLerpFix` | `1` | fix emissive lights like lampposts slowly fading in and out depending on time of day [all; FF] |
| `ForceCarHeadlightShadows` | `0` | use this to re-enable headlight shadows if CarDynamicShadowFix is enabled, buggy and should never be used with DualVehicleHeadlights [all; FF] |
| `ForceDynamicShadowsEverywhere` | `0` | make all light sources cast dynamic shadows [all; FF] |
| `ForceShadowsOnObjects` | `1` | make all objects cast shadows (e.g. bollards, Hove Beach train tracks) [all; FF] |
| `HighQualityReflections` | `0` | make everything reflect the entire world except vehicles and peds, major performance hit [all; FF] |
| `InfiniteCarBottomDistance` | `1` | remove the draw distance from static vehicle shadows [all] |
| `NoImpostorHeightCutoff` | `1` | removes arbitrary height cutoff from vehicle imposters, makes them behave more like V [all; FF] |
| `NoLODLightHeightCutoff` | `0` | removes arbitrary height cutoff from distant lights, might look inaccurate up close [all; FF] |
| `RemoveBoundingBoxCulling` | `0` | only ever use this if HighQualityReflections causes too much pop-in in the reflections, unstable [all] |
| `RemoveStaticCarShadows` | `0` | remove static shadow textures under cars [all] |
| `RemoveStaticCutsceneCarShadows` | `0` | remove static shadow textures under cars in cutscenes [all] |
| `ReversingLightFix` | `1` | fix reverse lights being cast in the middle of the car's hood [all; FF] |

## VEHICLES

vehicle behaviour and model flags

| key | default | notes |
| --- | --- | --- |
| `BikeFeetFix` | `1` | previously TLAD-exclusive foot position fix for chopper bikes (patch 5+) [all; FF] |
| `BikePhoneAnimsFix` | `1` | fixed standing still animation for bikes during a phone call (patch 5+) [all; FF] |
| `CopSirensFlag` | `1` | add +siren vehicles.ide flag to cars to make them use sirens [all] |
| `EpisodicVehicleSupport` | `1` | APC and buzzard support for all episodes [all-1000,1040; FF] |
| `EpisodicWeaponSupport` | `1` | explosive shotgun, stickybomb and DSR1 sniper support for all episodes [all-1000,1040; FF] |
| `HighFPSBikePhysicsFix` | `1` | fix bikes sliding out easily above 60FPS [all; FF] |
| `MovingPlaneParts` | `1` | makes propellers, ailerons, elevators and rudders move on planes that support them [all] |
| `MovingSteeringWheelSupport` | `1` | make the "steeringwheel" car bone rotate properly if it exists (Example of a car with proper steering wheel setup: https://mega.nz/file/vcAAib6Z#bsbJaseAbdxleRwr1XGL-d_it_P9DyjsPoXP-pnMoq8 ) use the new +wheelangle: vehicles.ide flag to specify an angle the steering wheel should be tilted forward or backwards by (in degrees) while porting a car from V, write the angle down into this flag and set the actual wheel's angle to 0, 0, 0 to make it work properly [all] |
| `NoLiveryLimit` | `1` | removes the 4 livery limit, allowing up to 255 unique livery textures for any car [all; FF] |
| `PoliceEpisodicWeaponSupport` | `0` | makes cops use P90s and assault shotguns - only enable this if those weapons exist, or NOOSE and FIB will spawn unarmed [all-1000,1040; FF] |
| `PopupHeadlightsFlag` | `1` | vehicles.ide flag for Hakumai's popup headlights on any car that supports it (+popuplights) [all] |
| `ShakingEngineBlock` | `1` | make the engineblock part of cars shake like V if they have support for it [all] |
| `TBoGTHeliHeightLimit` | `0` | unlocks the height limit for aircraft to the TBoGT level for all episodes [all-1000,1040; FF] |

## INPUT

camera, mouse, window and frame pacing

| key | default | notes |
| --- | --- | --- |
| `DrivebyFOVFix` | `1` | fix for driveby shots not landing in the center of the crosshair if you're too high above ground level [all] |
| `MouseFix` | `1` | fixes the mouse deadzone and removes mouse acceleration from snipers [all; FF] |
| `VSyncFix` | `1` | fix VSync locking below 60FPS in patch 7 [1004,1060,1061,1070,1100,1110,1120,12043; FF] |

## MULTIPLAYER

GFWL/CitizenIV multiplayer fixes

| key | default | notes |
| --- | --- | --- |
| `BetterMPSync` | `1` | increase sync interval when far away, works on GFWL and GTAC [all] |
| `DebugPrintSpawns` | `0` | log all entities as they sync in into GTAC console (GTAC only, requires FreezeCarFix to be on) this doesn't actually log which player spawned the entity but who's currently syncing it which 99% of the time is the person who spawned it ONLY if they spawned it while they were in sync range [all] |
| `FreezeCarFix` | `1` | fix crash if another player spawns a car that has the wrong vehicle type [all] |
| `FreezeGunFix` | `1` | fix crash when using ROCKET weapon [all] |
| `RemoveAllWeaponsProtection` | `1` | make other players no longer able to use malicious trainers to remove all your weapons [all] |
| `TeleportProtection` | `1` | make other players no longer able to use malicious trainers to teleport you [all] |

## LOADING

files, saves, DLC and launcher hooks

| key | default | notes |
| --- | --- | --- |
| `LoadDLCs` | `1` | load TBoGT and TLAD in the same folder structure EFLC does it (root folder -> episode's folder) and any more addon episodes in the same format (extra DLC folders should be named Episode3, Episode4, etc. and their ids in setup2.xml be the episode id multiplied by 2) [1060,1070,1080] |
| `LocalSettingsPath` | `0` | moves your savegames and settings to a saves folder inside the game folder [all; FF] |
| `NewFilesCompatibility` | `1` | adds some extra code that allows script.img and radiohud from newer versions to work (script.img is still 1060+ only, radiohud works on all versions) [all] |
| `NewSavesCompatibility` | `1` | makes newer savefiles load properly without needing them to be downgraded first (1060+ only) [all] |
| `OverrideGTADatPath` | `0` | load gta.dat from another path [all] |
| `SCOSignatureFix` | `1` | allow loading of any .sco file regardless of which episode it came from (or which platform it came from, in case of patch 4 and below) this is required for trainer IV parachute support to work [all; FF] |
| `OverrideGTADatPathTo` | `common:/data/gta.dat` | the path to load gta.dat from if the above is enabled [all] |

## COMPATIBILITY

other mods, game builds and packaging

| key | default | notes |
| --- | --- | --- |
| `AllowHashesForIDE` | `1` | allow entering raw hashes into .ide files if you add "hash:" before the hash (in unsigned decimal) to get around the character limit in .ide entries [all] |
| `LaunchWithPlayGTAIV` | `1` | ability to use a (non-SecuROM) launcher renamed to PlayGTAIV so you can launch patch 7 or 8 through Steam [1000,1040,1060,1070,1080] |
| `Patch4RemoveModChecks` | `1` | remove modded file checks from patch 4 to make MP work [all] |
| `Patch6and7RemoveModChecks` | `1` | remove modded file checks from 1060 and 1070 for compatibility with 1080 players [1060,1070] |
| `RemoveGFWLUpdatePopup` | `1` | remove "update or sign out of LIVE" popup for GFWL when running patch 5 and below, making Multiplayer possible on all pre-patch 6 versions [all] |
| `GFWLProtectionDisabler` | `0` | disable the GFWL xlive.dll protection check (same one-byte patch as GFWLProtectionDisabler2019) [all] |
| `SteamAchievements` | `1` | mirror GFWL achievement writes to Steam; the original GFWL call still runs, so GFWL achievements stay active [all] |
| `RemoveRGSCLoginRequirement` | `1` | remove Social Club login requirement so you can play MP even if you skip login on patch 4 and below, or use Play Offline on patch 7 [all] |
| `SavegameFix` | `1` | remove GFWL savegame CRC check [all] |

## INTERFACE

menu, HUD and audio toggles

| key | default | notes |
| --- | --- | --- |
| `AddOptionsInMenu` | `1` | adds some extra menu options if IVMenuAPI is present [all; FF] |
| `PipeBombDropIconSupport` | `1` | use droppipebomb_icon for pipe bomb in base IV [all-1000,1040; FF] |
| `RestoreDeathMusic` | `1` | restores the unused music on death [all; FF] |
