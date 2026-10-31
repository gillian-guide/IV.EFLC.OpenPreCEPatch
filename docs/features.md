# Features

One section per `[Options]` key. The behaviour described is the original's, including the preserved faults called out at the end of a section. Builds listed as *all* use pattern-gated installs: a build where the signature is absent simply skips the feature.

## AddOptionsInMenu

adds some extra menu options if IVMenuAPI is present

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** flag-only.

**Preserved behaviour.** The focus-loss callback writes its saved site unconditionally; a failed prime scan leaves it null.

## AimProjectileCrashFix

crash fix, usually happens in GTAC

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## AllowHashesForIDE

allow entering raw hashes into .ide files if you add "hash:" before the hash (in unsigned decimal) to get around the character limit in .ide entries

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## BadPlayerModelCrashFix

fix crash if another player switches to an invalid playermodel

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## BenchmarkFix

fix crash after benchmark ends

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** 1.0.0.0, 1.0.4.0, 1.0.0.4, 1.0.6.0, 1.0.7.0, 1.1.0.0, 1.1.1.0, 1.1.2.0, 1.0.6.1, 1.2.0.43.
- **Install class:** function-call-only.

## BetterMPSync

increase sync interval when far away, works on GFWL and GTAC

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## BikeFeetFix

previously TLAD-exclusive foot position fix for chopper bikes (patch 5+)

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## BikePhoneAnimsFix

fixed standing still animation for bikes during a phone call (patch 5+)

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## BlackscreenFix

fix a crash method that Modna uses

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## BorderlessWindowed

borderless windowed, needs -windowed commandline

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## BuildingAlphaFix

fix missing fadein for LODs in patch 6+, disable this on patch 5 and below

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## BuildingDynamicShadows

enable night shadows cast from buildings

- **Default:** shipped `0`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## CarDecalsFix

fix bulletholes not appearing on some modded cars outside TBoGT

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## CarDynamicShadowFix

tunnel, lamppost and heli spotlight shadows for cars (disables headlight shadows)

- **Default:** shipped `1`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** function-call-only.

**Preserved behaviour.** With ForceCarHeadlightShadows=1 the EA00 write destroys the E870 pattern first, so the original's P2 scan fails; the arm order is preserved.

## CarPartsShadowFix

tunnel, lamppost and heli spotlight shadows for detached car parts

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## CollisionDeadlockFix

freeze fix, usually happens in GTAC

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## CombinedRadioFreezeFix

fix freeze after Concrete Jungle with the 3 EFLC exclusive radio stations installed

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## CopSirensFlag

add +siren vehicles.ide flag to cars to make them use sirens

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## CrashMsgFix

fix the other crash method that Modna uses

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## CutsceneFixes

fix zoomed in cutscenes and jittery cutscene camera motion at high framerates

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

**Preserved behaviour.** The resolver result and the scene object are dereferenced without checks, and the body self-modifies the game's camera function. Preserved.

## DebugPrintSpawns

log all entities as they sync in into GTAC console (GTAC only, requires FreezeCarFix to be on) this doesn't actually log which player spawned the entity but who's currently syncing it which 99% of the time is the person who spawned it ONLY if they spawned it while they were in sync range

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## DoNotPauseOnMinimize

don't pause when unfocused in window/borderless mode

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## DrivebyFOVFix

fix for driveby shots not landing in the center of the crosshair if you're too high above ground level

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

**Preserved behaviour.** The resolver result and the cached entry are dereferenced without a null check. An exception in the resolver yields 0 and the following [eax+0Eh] read faults, exactly like the original.

## DualVehicleHeadlights

cast 2 separate lights for car headlights with their own shadows

- **Default:** shipped `0`; absent-key `1`.
- **Builds:** 1.0.0.0, 1.0.4.0, 1.0.0.4.
- **Install class:** signature-patch.

**Preserved behaviour.** The old/new destination is selected by a build range check; both install rows carry their own masks.

## EmissiveLerpFix

fix emissive lights like lampposts slowly fading in and out depending on time of day

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## EpisodeSwitchFreezeFix

fix crash when switching episode mid game

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## EpisodicVehicleSupport

APC and buzzard support for all episodes

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** 1.0.0.4, 1.0.6.0, 1.0.7.0, 1.0.8.0, 1.1.0.0, 1.1.1.0, 1.1.2.0, 1.1.3.0, 1.0.6.1, 1.2.0.43.
- **Install class:** signature-patch.

## EpisodicWeaponSupport

explosive shotgun, stickybomb and DSR1 sniper support for all episodes

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** 1.0.0.4, 1.0.6.0, 1.0.7.0, 1.0.8.0, 1.1.0.0, 1.1.1.0, 1.1.2.0, 1.1.3.0, 1.0.6.1, 1.2.0.43.
- **Install class:** signature-patch.

## FastLoading

cut loading times in half or more (can be combined with ColAccel to get even faster loading)

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## FixPCIDKickbug

fix keygen users kicking each other on join in GFWL

- **Default:** shipped `0`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

**Preserved behaviour.** The HKCU GFWL PCID value is deleted on every launch when the key is on (the arm's absent-key default is 1).

## ForceCarHeadlightShadows

use this to re-enable headlight shadows if CarDynamicShadowFix is enabled, buggy and should never be used with DualVehicleHeadlights

- **Default:** shipped `0`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

**Preserved behaviour.** The shared model lookup and the object are dereferenced unconditionally. A missing preamble pointer or a null object faults. Preserved.

## ForceDynamicShadowsEverywhere

make all light sources cast dynamic shadows

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## ForceShadowsOnObjects

make all objects cast shadows (e.g. bollards, Hove Beach train tracks)

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## FreezeCarFix

fix crash if another player spawns a car that has the wrong vehicle type

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## FreezeGunFix

fix crash when using ROCKET weapon

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## GarageModelFix

fixes cars being randomly changed in parking spots if addons models are added

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

**Preserved behaviour.** The lookup helper and the table-entry chain are dereferenced unconditionally. Preserved.

## GroupHackFix

MP crash method fix

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## HighFPSBikePhysicsFix

fix bikes sliding out easily above 60FPS

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## HighFPSSpeedupFix

fix game speeding up above 300FPS

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## HighQualityReflections

make everything reflect the entire world except vehicles and peds, major performance hit

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## ImprovedShaderStreaming

improved a really badly coded function, reduces stutter somewhat

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## IncreasePtrNodes

increase the PtrNode limit, fixes random crashing in big Cunning Stunts maps

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## IncreaseVehicleModelLimit

increase the amount of possible vehicles.ide entries to 1024

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## IncreaseVehicleStructLimit

increase the amount of vehicles with different models that can be loaded at one time

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## InfiniteCarBottomDistance

remove the draw distance from static vehicle shadows

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## LaunchWithPlayGTAIV

ability to use a (non-SecuROM) launcher renamed to PlayGTAIV so you can launch patch 7 or 8 through Steam

- **Default:** shipped `1`; absent-key `0`.
- **Builds:** IV 1.0.0.0, 1.0.4.0, 1.0.6.0, 1.0.7.0, 1.0.8.0 (not EFLC).
- **Install class:** rva-patch.

## LoadDLCs

load TBoGT and TLAD in the same folder structure EFLC does it (root folder -> episode's folder) and any more addon episodes in the same format (extra DLC folders should be named Episode3, Episode4, etc. and their ids in setup2.xml be the episode id multiplied by 2)

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** 1.0.6.0, 1.0.7.0, 1.0.8.0.
- **Install class:** rva-patch.

## LocalSettingsPath

moves your savegames and settings to a saves folder inside the game folder

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## MiscFixes

some features from XLivelessAddon, disable if not using GFWL and you have XLivelessAddon installed already

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## MouseFix

fixes the mouse deadzone and removes mouse acceleration from snipers

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## MovingPlaneParts

makes propellers, ailerons, elevators and rudders move on planes that support them

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** flag-only.

## MovingSteeringWheelSupport

make the "steeringwheel" car bone rotate properly if it exists (Example of a car with proper steering wheel setup: https://mega.nz/file/vcAAib6Z#bsbJaseAbdxleRwr1XGL-d_it_P9DyjsPoXP-pnMoq8 ) use the new +wheelangle: vehicles.ide flag to specify an angle the steering wheel should be tilted forward or backwards by (in degrees) while porting a car from V, write the angle down into this flag and set the actual wheel's angle to 0, 0, 0 to make it work properly

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** flag-only.

## MPNikoCrashFix

fix crash if another player changes their model to Niko then changes it back to something else

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## NewFilesCompatibility

adds some extra code that allows script.img and radiohud from newer versions to work (script.img is still 1060+ only, radiohud works on all versions)

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## NewMemorySystem

reimplements memory allocation, possibly increasing performance, reducing loading times and memory usage and completely eliminating SMPA* errors

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

**Preserved behaviour.** The game's own allocator init never runs; allocations are filled with 0xCD. Preserved.

## NewSavesCompatibility

makes newer savefiles load properly without needing them to be downgraded first (1060+ only)

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

**Preserved behaviour.** fread results are ignored, a size below 9 underflows the allocation count, and the allocation is not checked. Preserved.

## NoImpostorHeightCutoff

removes arbitrary height cutoff from vehicle imposters, makes them behave more like V

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

**Preserved behaviour.** On 1.0.0.0 the first block matches once, so the original writes through an uninitialised vector entry. The row set is reproduced.

## NoLiveryLimit

removes the 4 livery limit, allowing up to 255 unique livery textures for any car

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## NoLoadingSleep

makes the game get to the main menu faster

- **Default:** shipped `1`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## NoLODLightHeightCutoff

removes arbitrary height cutoff from distant lights, might look inaccurate up close

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## NoPhysicsSleep

reduces the threshold velocity below which physics objects can "go to sleep" at, this means vehicles stop in a more realistic way but it might be a performance hit

- **Default:** shipped `1`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## OutOfCommissionFix

fixes the climbing quick-time event in Out of Commission at high FPS (1060+ only)

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

**Preserved behaviour.** The decoder runs on every call (its guard has no writer), neither allocation is checked, and the archive is leaked each time. Preserved.

## OverrideGTADatPath

load gta.dat from another path

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## OverrideGTADatPathTo

the path to load gta.dat from if the above is enabled

- **Default:** shipped `common:/data/gta.dat`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## Patch4RemoveModChecks

remove modded file checks from patch 4 to make MP work

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## Patch6and7RemoveModChecks

remove modded file checks from 1060 and 1070 for compatibility with 1080 players

- **Default:** shipped `1`; absent-key `0`.
- **Builds:** 1.0.6.0, 1.0.7.0.
- **Install class:** rva-patch.

## Patch7RemoveModChecks

Undocumented alias. Setting it to 1 also enables the Patch6and7RemoveModChecks install set; the loader adds the alias to the config object before the option passes run.

- **Default:** shipped `-`; absent-key `0`.
- **Builds:** 1.0.6.0, 1.0.7.0.
- **Install class:** undocumented-loader-key.

## PathNodeCrashFix

crash fix, usually happens in GTAC

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## PedModelsInCutscenesFix

fixes crash when starting cutscenes as a ped

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## PipeBombDropIconSupport

use droppipebomb_icon for pipe bomb in base IV

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** 1.0.0.4, 1.0.6.0, 1.0.7.0, 1.0.8.0, 1.1.0.0, 1.1.1.0, 1.1.2.0, 1.1.3.0, 1.0.6.1, 1.2.0.43.
- **Install class:** signature-patch.

## PlaneCrashFix

fix random crashing while using actual planes

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## PlayerAsPedComponentsFix

fix Niko peds' clothes never changing properly in the trainer

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## PoliceEpisodicWeaponSupport

makes cops use P90s and assault shotguns - only enable this if those weapons exist, or NOOSE and FIB will spawn unarmed

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** 1.0.0.4, 1.0.6.0, 1.0.7.0, 1.0.8.0, 1.1.0.0, 1.1.1.0, 1.1.2.0, 1.1.3.0, 1.0.6.1, 1.2.0.43.
- **Install class:** signature-patch.

## PopupHeadlightsFlag

vehicles.ide flag for Hakumai's popup headlights on any car that supports it (+popuplights)

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## RemoveAllWeaponsProtection

make other players no longer able to use malicious trainers to remove all your weapons

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## RemoveBoundingBoxCulling

only ever use this if HighQualityReflections causes too much pop-in in the reflections, unstable

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

**Preserved behaviour.** The branch is inverted relative to the option name; enabling it can make the world vanish / crash LAN MP. The shipped INI keeps it at 0, the original's own default. The faulting code is present.

## RemoveGFWLUpdatePopup

remove "update or sign out of LIVE" popup for GFWL when running patch 5 and below, making Multiplayer possible on all pre-patch 6 versions

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** function-call-only.

**Preserved behaviour.** With xlive.dll absent the base is 0 and the four stores hit the low 0x66F770 page (access violation). Preserved.

## RemoveRGSCLoginRequirement

remove Social Club login requirement so you can play MP even if you skip login on patch 4 and below, or use Play Offline on patch 7

- **Default:** shipped `1`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## RemoveStaticCarShadows

remove static shadow textures under cars

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## RemoveStaticCutsceneCarShadows

remove static shadow textures under cars in cutscenes

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## RemoveUselessChecks

might be a small performance boost but highly incompatible with other mods

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** 1.0.4.0, 1.0.7.0, 1.0.8.0.
- **Install class:** rva-patch.

## RestoreDeathMusic

restores the unused music on death

- **Default:** shipped `1`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## ReversingLightFix

fix reverse lights being cast in the middle of the car's hood

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## SavegameFix

remove GFWL savegame CRC check

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## SCOSignatureFix

allow loading of any .sco file regardless of which episode it came from (or which platform it came from, in case of patch 4 and below) this is required for trainer IV parachute support to work

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## ShakingEngineBlock

make the engineblock part of cars shake like V if they have support for it

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** flag-only.

## SkipIntro

skip intro screens

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

**Preserved behaviour.** The original compresses every post-menu load screen when the site runs again, not only the first intro. Preserved.

## SkipLauncher

from XLivelessAddon but with some additional improvements, ability to launch game directly from .exe and/or skip the patch 4 launcher by using any other launch/playgtaiv exe

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## SkipMenu

skip main menu, launching straight into the last savegame regardless of what episode that's from (IV if no savegame is found)

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** all builds.
- **Install class:** signature-patch.

**Preserved behaviour.** The first zero menu id becomes 6 unconditionally; with no savegame the original does not fall back to a new game. Preserved.

## SMPA60Fix

fix SMPA60 both when abusing the poor game with car mods and if another player is doing that in MP

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## SpeedupBugFix

fixes the game being permanently sped up slightly in some cases

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## StartupTimeFix

fixes the 15+ second startup times before the game window even pops up

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** function-call-only.

## SuperLODFix

fix crash if superlod is spawned, disable this for GTAC as it's already fixed there

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** 1.0.7.0, 1.0.8.0.
- **Install class:** rva-patch.

## TBoGTHeliHeightLimit

unlocks the height limit for aircraft to the TBoGT level for all episodes

- **Default:** shipped `0`; absent-key `0`.
- **Builds:** 1.0.0.4, 1.0.6.0, 1.0.7.0, 1.0.8.0, 1.1.0.0, 1.1.1.0, 1.1.2.0, 1.1.3.0, 1.0.6.1, 1.2.0.43.
- **Install class:** signature-patch.

## TeleportProtection

make other players no longer able to use malicious trainers to teleport you

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## TryToSkipAllErrors

attempt to completely ignore any and all error messages that would ever pop up this will likely still eventually crash your game but at least it'll be an actual crash instead of an in most cases useless error

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## VRAMFix

workaround for VRAM above 2GB

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.

## VSyncFix

fix VSync locking below 60FPS in patch 7

- **Default:** shipped `1`; absent-key `0`.
- **Builds:** 1.0.0.4, 1.0.6.0, 1.0.7.0, 1.1.0.0, 1.1.1.0, 1.1.2.0, 1.0.6.1, 1.2.0.43.
- **Install class:** signature-patch.

## WantedLevelCrashFix

fix rare crash when getting a wanted level

- **Default:** shipped `1`; absent-key `1`.
- **Builds:** all builds.
- **Install class:** signature-patch.
