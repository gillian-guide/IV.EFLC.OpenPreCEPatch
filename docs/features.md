# Features

One subsection per INI key. A key is enabled when its value parses to
exactly `1`; the default is used when the key is absent.

## STABILITY

crash, freeze and game-logic fixes

### AimProjectileCrashFix

crash fix, usually happens in GTAC [all]

- **Default:** `1`

### BadPlayerModelCrashFix

fix crash if another player switches to an invalid playermodel [all]

- **Default:** `1`

### BenchmarkFix

fix crash after benchmark ends [all-1080,1130]

- **Default:** `1`

### BlackscreenFix

fix a crash method that Modna uses [all]

- **Default:** `1`

### CollisionDeadlockFix

freeze fix, usually happens in GTAC [all]

- **Default:** `1`

### CombinedRadioFreezeFix

fix freeze after Concrete Jungle with the 3 EFLC exclusive radio stations installed [all]

- **Default:** `1`

### CrashMsgFix

fix the other crash method that Modna uses [all]

- **Default:** `1`

### EpisodeSwitchFreezeFix

fix crash when switching episode mid game [all]

- **Default:** `1`

### GarageModelFix

fixes cars being randomly changed in parking spots if addons models are added [all]

- **Default:** `1`

### GroupHackFix

MP crash method fix [all]

- **Default:** `1`

### MPNikoCrashFix

fix crash if another player changes their model to Niko then changes it back to something else [all]

- **Default:** `1`

### OutOfCommissionFix

fixes the climbing quick-time event in Out of Commission at high FPS (1060+ only) [all]

- **Default:** `1`

### PathNodeCrashFix

crash fix, usually happens in GTAC [all]

- **Default:** `1`

### PedModelsInCutscenesFix

fixes crash when starting cutscenes as a ped [all]

- **Default:** `1`

### PlaneCrashFix

fix random crashing while using actual planes [all]

- **Default:** `1`

### PlayerAsPedComponentsFix

fix Niko peds' clothes never changing properly in the trainer [all]

- **Default:** `1`

### SMPA60Fix

fix SMPA60 both when abusing the poor game with car mods and if another player is doing that in MP [all]

- **Default:** `1`

### SpeedupBugFix

fixes the game being permanently sped up slightly in some cases [all; FF]

- **Default:** `1`

### SuperLODFix

fix crash if superlod is spawned, disable this for GTAC as it's already fixed there [1070,1080]

- **Default:** `1`

### TryToSkipAllErrors

attempt to completely ignore any and all error messages that would ever pop up this will likely still eventually crash your game but at least it'll be an actual crash instead of an in most cases useless error [all]

- **Default:** `0`

### WantedLevelCrashFix

fix rare crash when getting a wanted level [all]

- **Default:** `1`

## PERFORMANCE

loading, streaming and memory

### FastLoading

cut loading times in half or more (can be combined with ColAccel to get even faster loading) [all; FF]

- **Default:** `1`

### HighFPSSpeedupFix

fix game speeding up above 300FPS [all; FF]

- **Default:** `1`

### ImprovedShaderStreaming

improved a really badly coded function, reduces stutter somewhat [all; FF]

- **Default:** `1`

### IncreasePtrNodes

increase the PtrNode limit, fixes random crashing in big Cunning Stunts maps [all; FF]

- **Default:** `1`

### IncreaseVehicleModelLimit

increase the amount of possible vehicles.ide entries to 1024 [all; FF]

- **Default:** `1`

### IncreaseVehicleStructLimit

increase the amount of vehicles with different models that can be loaded at one time [all; FF]

- **Default:** `1`

### NewMemorySystem

reimplements memory allocation, possibly increasing performance, reducing loading times and memory usage and completely eliminating SMPA* errors [all]

- **Default:** `0`

### NoLoadingSleep

makes the game get to the main menu faster [all; FF]

- **Default:** `1`

### NoPhysicsSleep

reduces the threshold velocity below which physics objects can "go to sleep" at, this means vehicles stop in a more realistic way but it might be a performance hit [all]

- **Default:** `1`

### RemoveUselessChecks

might be a small performance boost but highly incompatible with other mods [1040,1070,1080]

- **Default:** `0`

### StartupTimeFix

fixes the 15+ second startup times before the game window even pops up [all]

- **Default:** `1`

## GRAPHICS

shadows, lights, LODs and reflections

### BuildingAlphaFix

fix missing fadein for LODs in patch 6+, disable this on patch 5 and below [all]

- **Default:** `1`

### BuildingDynamicShadows

enable night shadows cast from buildings [all; FF]

- **Default:** `0`

### CarDecalsFix

fix bulletholes not appearing on some modded cars outside TBoGT [all]

- **Default:** `1`

### CarDynamicShadowFix

tunnel, lamppost and heli spotlight shadows for cars (disables headlight shadows) [all; FF]

- **Default:** `1`

### CarPartsShadowFix

tunnel, lamppost and heli spotlight shadows for detached car parts [all; FF]

- **Default:** `1`

### CutsceneFixes

fix zoomed in cutscenes and jittery cutscene camera motion at high framerates [all; FF]

- **Default:** `1`

### DualVehicleHeadlights

cast 2 separate lights for car headlights with their own shadows [all]

- **Default:** `0`

### EmissiveLerpFix

fix emissive lights like lampposts slowly fading in and out depending on time of day [all; FF]

- **Default:** `1`

### ForceCarHeadlightShadows

use this to re-enable headlight shadows if CarDynamicShadowFix is enabled, buggy and should never be used with DualVehicleHeadlights [all; FF]

- **Default:** `0`

### ForceDynamicShadowsEverywhere

make all light sources cast dynamic shadows [all; FF]

- **Default:** `0`

### ForceShadowsOnObjects

make all objects cast shadows (e.g. bollards, Hove Beach train tracks) [all; FF]

- **Default:** `1`

### HighQualityReflections

make everything reflect the entire world except vehicles and peds, major performance hit [all; FF]

- **Default:** `0`

### InfiniteCarBottomDistance

remove the draw distance from static vehicle shadows [all]

- **Default:** `1`

### NoImpostorHeightCutoff

removes arbitrary height cutoff from vehicle imposters, makes them behave more like V [all; FF]

- **Default:** `1`

### NoLODLightHeightCutoff

removes arbitrary height cutoff from distant lights, might look inaccurate up close [all; FF]

- **Default:** `0`

### RemoveBoundingBoxCulling

only ever use this if HighQualityReflections causes too much pop-in in the reflections, unstable [all]

- **Default:** `0`

### RemoveStaticCarShadows

remove static shadow textures under cars [all]

- **Default:** `0`

### RemoveStaticCutsceneCarShadows

remove static shadow textures under cars in cutscenes [all]

- **Default:** `0`

### ReversingLightFix

fix reverse lights being cast in the middle of the car's hood [all; FF]

- **Default:** `1`

## VEHICLES

vehicle behaviour and model flags

### BikeFeetFix

previously TLAD-exclusive foot position fix for chopper bikes (patch 5+) [all; FF]

- **Default:** `1`

### BikePhoneAnimsFix

fixed standing still animation for bikes during a phone call (patch 5+) [all; FF]

- **Default:** `1`

### CopSirensFlag

add +siren vehicles.ide flag to cars to make them use sirens [all]

- **Default:** `1`

### EpisodicVehicleSupport

APC and buzzard support for all episodes [all-1000,1040; FF]

- **Default:** `1`

### EpisodicWeaponSupport

explosive shotgun, stickybomb and DSR1 sniper support for all episodes [all-1000,1040; FF]

- **Default:** `1`

### HighFPSBikePhysicsFix

fix bikes sliding out easily above 60FPS [all; FF]

- **Default:** `1`

### MovingPlaneParts

makes propellers, ailerons, elevators and rudders move on planes that support them [all]

- **Default:** `1`

### MovingSteeringWheelSupport

make the "steeringwheel" car bone rotate properly if it exists (Example of a car with proper steering wheel setup: https://mega.nz/file/vcAAib6Z#bsbJaseAbdxleRwr1XGL-d_it_P9DyjsPoXP-pnMoq8 ) use the new +wheelangle: vehicles.ide flag to specify an angle the steering wheel should be tilted forward or backwards by (in degrees) while porting a car from V, write the angle down into this flag and set the actual wheel's angle to 0, 0, 0 to make it work properly [all]

- **Default:** `1`

### NoLiveryLimit

removes the 4 livery limit, allowing up to 255 unique livery textures for any car [all; FF]

- **Default:** `1`

### PoliceEpisodicWeaponSupport

makes cops use P90s and assault shotguns - only enable this if those weapons exist, or NOOSE and FIB will spawn unarmed [all-1000,1040; FF]

- **Default:** `0`

### PopupHeadlightsFlag

vehicles.ide flag for Hakumai's popup headlights on any car that supports it (+popuplights) [all]

- **Default:** `1`

### ShakingEngineBlock

make the engineblock part of cars shake like V if they have support for it [all]

- **Default:** `1`

### TBoGTHeliHeightLimit

unlocks the height limit for aircraft to the TBoGT level for all episodes [all-1000,1040; FF]

- **Default:** `0`

## INPUT

camera, mouse, window and frame pacing

### DrivebyFOVFix

fix for driveby shots not landing in the center of the crosshair if you're too high above ground level [all]

- **Default:** `1`

### MouseFix

fixes the mouse deadzone and removes mouse acceleration from snipers [all; FF]

- **Default:** `1`

### VSyncFix

fix VSync locking below 60FPS in patch 7 [1004,1060,1061,1070,1100,1110,1120,12043; FF]

- **Default:** `1`

## MULTIPLAYER

GFWL/CitizenIV multiplayer fixes

### BetterMPSync

increase sync interval when far away, works on GFWL and GTAC [all]

- **Default:** `1`

### DebugPrintSpawns

log all entities as they sync in into GTAC console (GTAC only, requires FreezeCarFix to be on) this doesn't actually log which player spawned the entity but who's currently syncing it which 99% of the time is the person who spawned it ONLY if they spawned it while they were in sync range [all]

- **Default:** `0`

### FreezeCarFix

fix crash if another player spawns a car that has the wrong vehicle type [all]

- **Default:** `1`

### FreezeGunFix

fix crash when using ROCKET weapon [all]

- **Default:** `1`

### RemoveAllWeaponsProtection

make other players no longer able to use malicious trainers to remove all your weapons [all]

- **Default:** `1`

### TeleportProtection

make other players no longer able to use malicious trainers to teleport you [all]

- **Default:** `1`

## LOADING

files, saves, DLC and launcher hooks

### LoadDLCs

load TBoGT and TLAD in the same folder structure EFLC does it (root folder -> episode's folder) and any more addon episodes in the same format (extra DLC folders should be named Episode3, Episode4, etc. and their ids in setup2.xml be the episode id multiplied by 2) [1060,1070,1080]

- **Default:** `1`

### LocalSettingsPath

moves your savegames and settings to a saves folder inside the game folder [all; FF]

- **Default:** `0`

### NewFilesCompatibility

adds some extra code that allows script.img and radiohud from newer versions to work (script.img is still 1060+ only, radiohud works on all versions) [all]

- **Default:** `1`

### NewSavesCompatibility

makes newer savefiles load properly without needing them to be downgraded first (1060+ only) [all]

- **Default:** `1`

### OverrideGTADatPath

load gta.dat from another path [all]

- **Default:** `0`

### SCOSignatureFix

allow loading of any .sco file regardless of which episode it came from (or which platform it came from, in case of patch 4 and below) this is required for trainer IV parachute support to work [all; FF]

- **Default:** `1`

### OverrideGTADatPathTo

the path to load gta.dat from if the above is enabled [all]

- **Default:** `common:/data/gta.dat`

## COMPATIBILITY

other mods, game builds and packaging

### AllowHashesForIDE

allow entering raw hashes into .ide files if you add "hash:" before the hash (in unsigned decimal) to get around the character limit in .ide entries [all]

- **Default:** `1`

### LaunchWithPlayGTAIV

ability to use a (non-SecuROM) launcher renamed to PlayGTAIV so you can launch patch 7 or 8 through Steam [1000,1040,1060,1070,1080]

- **Default:** `1`

### Patch4RemoveModChecks

remove modded file checks from patch 4 to make MP work [all]

- **Default:** `1`

### Patch6and7RemoveModChecks

remove modded file checks from 1060 and 1070 for compatibility with 1080 players [1060,1070]

- **Default:** `1`

### RemoveGFWLUpdatePopup

remove "update or sign out of LIVE" popup for GFWL when running patch 5 and below, making Multiplayer possible on all pre-patch 6 versions [all]

- **Default:** `1`

### GFWLProtectionDisabler

disable the GFWL xlive.dll protection check (same one-byte patch as GFWLProtectionDisabler2019) [all]

- **Default:** `0`

### SteamAchievements

mirror GFWL achievement writes to Steam; the original GFWL call still runs, so GFWL achievements stay active [all]

- **Default:** `1`

### RemoveRGSCLoginRequirement

remove Social Club login requirement so you can play MP even if you skip login on patch 4 and below, or use Play Offline on patch 7 [all]

- **Default:** `1`

### SavegameFix

remove GFWL savegame CRC check [all]

- **Default:** `1`

## INTERFACE

menu, HUD and audio toggles

### AddOptionsInMenu

adds some extra menu options if IVMenuAPI is present [all; FF]

- **Default:** `1`

### PipeBombDropIconSupport

use droppipebomb_icon for pipe bomb in base IV [all-1000,1040; FF]

- **Default:** `1`

### RestoreDeathMusic

restores the unused music on death [all; FF]

- **Default:** `1`
