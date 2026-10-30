# Features

One subsection per INI key. A key is enabled when its value parses to
exactly `1`; the default is used when the key is absent.

## STABILITY

### BenchmarkFix

fix crash after benchmark ends [all-1080,1130]

- **Default:** `1`

### CombinedRadioFreezeFix

fix freeze after Concrete Jungle with the 3 EFLC exclusive radio stations installed

- **Default:** `1`

### EpisodeSwitchFreezeFix

fix crash when switching episode mid game

- **Default:** `1`

### FreezeGunFix

fix crash when firing the ROCKET weapon

- **Default:** `1`

### WantedLevelCrashFix

fix rare crash when getting a wanted level

- **Default:** `1`

### SMPA60Fix

fix SMPA60 when abusing the game with car mods

- **Default:** `1`

### TryToSkipAllErrors

attempt to completely ignore any and all error messages, possibly useful for debugging

- **Default:** `0`

## FIXES

### CutsceneFixes

fix zoomed in cutscenes and jittery cutscene camera motion at high framerates [FF]

- **Default:** `1`

### OutOfCommissionFix

fixes the climbing quick-time event in Out of Commission at high FPS [1060-1130, FF]

- **Default:** `1`

### HighFPSBikePhysicsFix

fix bikes sliding out easily above 60FPS [FF]

- **Default:** `1`

### BikeFeetFix

previously TLAD-exclusive foot position fix for chopper bikes [FF]

- **Default:** `1`

### BikePhoneAnimsFix

fixed standing still animation for bikes during a phone call [FF]

- **Default:** `1`

### HighFPSSpeedupFix

fix game speeding up above 300FPS [FF]

- **Default:** `1`

### SpeedupBugFix

fixes the game being permanently sped up slightly in some cases [FF]

- **Default:** `1`

### VSyncFix

fix VSync locking below 60FPS [1004,1060-1061,1070,1100-1120,12043; FF]

- **Default:** `1`

### MouseFix

fixes the mouse deadzone and removes mouse acceleration from snipers [FF]

- **Default:** `1`

### DrivebyFOVFix

fix for driveby shots not landing in the center of the crosshair if you're too high above ground level

- **Default:** `1`

## PERFORMANCE

### StartupTimeFix

fixes the 15+ second startup times before the game window even pops up

- **Default:** `1`

### FastLoading

cut loading times in half or more

- **Default:** `1`

### NoLoadingSleep

makes the game get to the main menu faster [FF]

- **Default:** `1`

### ImprovedShaderStreaming

allegedly improves stutter, potentially unstable

- **Default:** `0`

### NewMemorySystem

reimplements memory allocation, allegedly reducing loading times and memory usage and eliminating SMPA* errors, potentially unstable

- **Default:** `0`

## GRAPHICS

### EmissiveLerpFix

fix emissive lights like lampposts slowly fading in and out depending on time of day [FF]

- **Default:** `1`

### ReversingLightFix

fix reverse lights being cast in the middle of the car's hood [FF]

- **Default:** `1`

### BuildingAlphaFix

fix missing fadein for LODs [1060+; FF]

- **Default:** `1`

### BuildingDynamicShadows

enable night shadows cast from buildings [FF]

- **Default:** `0`

### CarDynamicShadowFix

tunnel, lamppost and heli spotlight shadows for cars (disables headlight shadows) [FF]

- **Default:** `1`

### CarPartsShadowFix

tunnel, lamppost and heli spotlight shadows for detached car parts [FF]

- **Default:** `1`

### ForceShadowsOnObjects

make all objects cast shadows (e.g. bollards, Hove Beach train tracks) [FF]

- **Default:** `1`

### DualVehicleHeadlights

cast 2 separate lights for car headlights with their own shadows

- **Default:** `0`

### ForceCarHeadlightShadows

use this to re-enable headlight shadows if CarDynamicShadowFix is enabled, buggy and should never be used with DualVehicleHeadlights [FF]

- **Default:** `0`

### NoImpostorHeightCutoff

removes arbitrary height cutoff from vehicle imposters, makes them behave more like V [FF]

- **Default:** `1`

### NoLODLightHeightCutoff

removes arbitrary height cutoff from distant lights, might look inaccurate up close [FF]

- **Default:** `0`

### RemoveStaticCarShadows

remove static shadow textures under cars

- **Default:** `0`

### RemoveStaticCutsceneCarShadows

remove static shadow textures under cars in cutscenes

- **Default:** `0`

### InfiniteCarBottomDistance

remove the draw distance from static vehicle shadows

- **Default:** `0`

## EPISODIC

### LoadDLCs

load TBoGT and TLAD from EFLC or CE folders, plus any more addon episodes in the same format (extra DLC folders should be named Episode3, Episode4, etc. and their ids in setup2.xml be the episode id multiplied by 2) [1060,1070,1080]

- **Default:** `1`

### SCOSignatureFix

allow loading of any .sco file regardless of which episode it came from (or which platform it came from, in case of patch 4 and below) this is required for trainer IV parachute support to work [all; FF]

- **Default:** `1`

### EpisodicVehicleSupport

APC and buzzard support for all episodes [1004,1060-1130,1061,12043; FF]

- **Default:** `0`

### EpisodicWeaponSupport

explosive shotgun, stickybomb and DSR1 sniper support for all episodes [1004,1060-1130,1061,12043; FF]

- **Default:** `0`

### PoliceEpisodicWeaponSupport

makes cops use P90s and assault shotguns - only enable this if those weapons exist, or NOOSE and FIB will spawn unarmed [1004,1060-1130,1061,12043; FF]

- **Default:** `0`

### TBoGTHeliHeightLimit

unlocks the height limit for aircraft to the TBoGT level for all episodes [1004,1060-1130,1061,12043; FF]

- **Default:** `0`

### PipeBombDropIconSupport

use droppipebomb_icon for pipe bomb in base IV [1004,1060-1130,1061,12043; FF]

- **Default:** `0`

## COMPATIBILITY

### LaunchWithPlayGTAIV

ability to use a (non-SecuROM) launcher renamed to PlayGTAIV [1000,1040,1060,1070,1080]

- **Default:** `1`

### NewFilesCompatibility

allows script.img and radiohud from newer versions to work (script.img is still 1060+ only)

- **Default:** `1`

### NewSavesCompatibility

allows newer savefiles to load properly without needing them to be downgraded first [1060-1080]

- **Default:** `1`

### SavegameFix

remove GFWL savegame CRC check

- **Default:** `1`

### GFWLProtectionDisabler

disable the GFWL xlive.dll protection check

- **Default:** `1`

### SteamAchievements

mirror GFWL achievements to Steam

- **Default:** `1`

## MULTIPLAYER

### BetterMPSync

increase sync interval when far away

- **Default:** `1`

### MPNikoCrashFix

fix crash if another player changes their model to Niko then changes it back to something else

- **Default:** `1`

### IncreasePtrNodes

increase the PtrNode limit, fixes random crashing in big Cunning Stunts maps [FF]

- **Default:** `1`

### BadPlayerModelCrashFix

fix crash if another player switches to an invalid playermodel

- **Default:** `1`

### FreezeCarFix

fix crash if another player spawns a car that has the wrong vehicle type

- **Default:** `1`

### BlackscreenFix

fix a modmenu crash method

- **Default:** `1`

### CrashMsgFix

fix a modmenu crash method

- **Default:** `1`

### GroupHackFix

fix a modmenu crash method

- **Default:** `1`

### RemoveAllWeaponsProtection

make other players no longer able to remove all your weapons

- **Default:** `1`

### TeleportProtection

make other players no longer able to to teleport you

- **Default:** `1`

### SuperLODFix

fix crash if SuperLOD is spawned [1070,1080]

- **Default:** `1`

### AimProjectileCrashFix

crash fix, usually happens in GTAC

- **Default:** `0`

### PathNodeCrashFix

crash fix, usually happens in GTAC

- **Default:** `0`

### CollisionDeadlockFix

freeze fix, usually happens in GTAC

- **Default:** `0`

### DebugPrintSpawns

log all entities as they sync in into GTAC console (GTAC only, requires FreezeCarFix to be on) this doesn't actually log which player spawned the entity but who's currently syncing it which 99% of the time is the person who spawned it ONLY if they spawned it while they were in sync range

- **Default:** `0`

### RemoveModChecks

remove modded-file checks (patch 4 = 1.0.4.0, 1060 and 1070) [1040,1060,1070]

- **Default:** `1`

### RemoveRGSCLoginRequirement

remove Social Club login requirement for MP (all builds; 1.0.8.0 uses an RVA fallback) [all]

- **Default:** `1`

### RemoveGFWLUpdatePopup

remove "update or sign out of LIVE" popup for GFWL (patch 5 and below) [1000-1040]

- **Default:** `1`

## VEHICLES

### GarageModelFix

fixes cars being randomly changed in parking spots if addons models are added

- **Default:** `1`

### IncreaseVehicleStructLimit

increase the amount of vehicles with different models that can be loaded at one time [FF]

- **Default:** `1`

### IncreaseVehicleModelLimit

increase the amount of possible vehicles.ide entries to 1024 [FF]

- **Default:** `1`

### CarDecalsFix

fix bulletholes not appearing on some modded cars outside TBoGT

- **Default:** `0`

### NoLiveryLimit

removes the 4 livery limit, allowing up to 255 unique livery textures for any car [FF]

- **Default:** `1`

### PopupHeadlightsFlag

vehicles.ide flag for Hakumai's popup headlights on any car that supports it (+popuplights)

- **Default:** `0`

### CopSirensFlag

add +siren vehicles.ide flag to cars to make them use sirens

- **Default:** `0`

### PlaneCrashFix

fix random crashing while using actual planes

- **Default:** `0`

### MovingPlaneParts

makes propellers, ailerons, elevators and rudders move on planes that support them

- **Default:** `0`

### ShakingEngineBlock

make the engineblock part of cars shake like V if they have support for it

- **Default:** `0`

### MovingSteeringWheelSupport

make the "steeringwheel" car bone rotate properly if it exists (Example of a car with proper steering wheel setup: https://mega.nz/file/vcAAib6Z#bsbJaseAbdxleRwr1XGL-d_it_P9DyjsPoXP-pnMoq8 ) use the new +wheelangle: vehicles.ide flag to specify an angle the steering wheel should be tilted forward or backwards by (in degrees) while porting a car from V, write the angle down into this flag and set the actual wheel's angle to 0, 0, 0 to make it work properly

- **Default:** `0`

## MISC

### RestoreDeathMusic

restores the unused music on death [FF]

- **Default:** `0`

### OverrideGTADatPath

load gta.dat from another path

- **Default:** `0`

### OverrideGTADatPathTo

the path to load gta.dat from if the above is enabled

- **Default:** `common:/data/gta.dat`

### AllowHashesForIDE

allow entering raw hashes into .ide files if you add "hash:" before the hash (in unsigned decimal) to get around the character limit in .ide entries

- **Default:** `1`

### PlayerAsPedComponentsFix

fix Niko peds' clothes never changing properly in the trainer

- **Default:** `1`

### PedModelsInCutscenesFix

fixes crash when starting cutscenes as a ped

- **Default:** `1`

## HeadlightConeOffsets

per-model headlight cone offset

### Car1Name

No description in the shipped INI.

- **Default:** `admiral`

### Car1Offset

No description in the shipped INI.

- **Default:** `-5`

### Car2Name

No description in the shipped INI.

- **Default:** `airtug`

### Car2Offset

No description in the shipped INI.

- **Default:** `5`

### Car3Name

No description in the shipped INI.

- **Default:** `benson`

### Car3Offset

No description in the shipped INI.

- **Default:** `-4`

### Car4Name

No description in the shipped INI.

- **Default:** `biff`

### Car4Offset

No description in the shipped INI.

- **Default:** `-6`

### Car5Name

No description in the shipped INI.

- **Default:** `boxville`

### Car5Offset

No description in the shipped INI.

- **Default:** `-8`

### Car6Name

No description in the shipped INI.

- **Default:** `buccaneer`

### Car6Offset

No description in the shipped INI.

- **Default:** `-5`

### Car7Name

No description in the shipped INI.

- **Default:** `burrito`

### Car7Offset

No description in the shipped INI.

- **Default:** `4`

### Car8Name

No description in the shipped INI.

- **Default:** `burrito2`

### Car8Offset

No description in the shipped INI.

- **Default:** `4`

### Car9Name

No description in the shipped INI.

- **Default:** `bus`

### Car9Offset

No description in the shipped INI.

- **Default:** `-4`

### Car10Name

No description in the shipped INI.

- **Default:** `cabby`

### Car10Offset

No description in the shipped INI.

- **Default:** `-6`

### Car11Name

No description in the shipped INI.

- **Default:** `cavalcade`

### Car11Offset

No description in the shipped INI.

- **Default:** `4`

### Car12Name

No description in the shipped INI.

- **Default:** `chavos`

### Car12Offset

No description in the shipped INI.

- **Default:** `3`

### Car13Name

No description in the shipped INI.

- **Default:** `cognoscenti`

### Car13Offset

No description in the shipped INI.

- **Default:** `7`

### Car14Name

No description in the shipped INI.

- **Default:** `comet`

### Car14Offset

No description in the shipped INI.

- **Default:** `6`

### Car15Name

No description in the shipped INI.

- **Default:** `df8`

### Car15Offset

No description in the shipped INI.

- **Default:** `3`

### Car16Name

No description in the shipped INI.

- **Default:** `dilettante`

### Car16Offset

No description in the shipped INI.

- **Default:** `4`

### Car17Name

No description in the shipped INI.

- **Default:** `dukes`

### Car17Offset

No description in the shipped INI.

- **Default:** `-5`

### Car18Name

No description in the shipped INI.

- **Default:** `emperor`

### Car18Offset

No description in the shipped INI.

- **Default:** `5`

### Car19Name

No description in the shipped INI.

- **Default:** `emperor2`

### Car19Offset

No description in the shipped INI.

- **Default:** `5`

### Car20Name

No description in the shipped INI.

- **Default:** `esperanto`

### Car20Offset

No description in the shipped INI.

- **Default:** `5`

### Car21Name

No description in the shipped INI.

- **Default:** `fbi`

### Car21Offset

No description in the shipped INI.

- **Default:** `5`

### Car22Name

No description in the shipped INI.

- **Default:** `feltzer`

### Car22Offset

No description in the shipped INI.

- **Default:** `7`

### Car23Name

No description in the shipped INI.

- **Default:** `firetruk`

### Car23Offset

No description in the shipped INI.

- **Default:** `7`

### Car24Name

No description in the shipped INI.

- **Default:** `fortune`

### Car24Offset

No description in the shipped INI.

- **Default:** `17`

### Car25Name

No description in the shipped INI.

- **Default:** `futo`

### Car25Offset

No description in the shipped INI.

- **Default:** `5`

### Car26Name

No description in the shipped INI.

- **Default:** `fxt`

### Car26Offset

No description in the shipped INI.

- **Default:** `5`

### Car27Name

No description in the shipped INI.

- **Default:** `habanero`

### Car27Offset

No description in the shipped INI.

- **Default:** `9`

### Car28Name

No description in the shipped INI.

- **Default:** `hakumai`

### Car28Offset

No description in the shipped INI.

- **Default:** `4`

### Car29Name

No description in the shipped INI.

- **Default:** `ingot`

### Car29Offset

No description in the shipped INI.

- **Default:** `-15`

### Car30Name

No description in the shipped INI.

- **Default:** `intruder`

### Car30Offset

No description in the shipped INI.

- **Default:** `5`

### Car31Name

No description in the shipped INI.

- **Default:** `manana`

### Car31Offset

No description in the shipped INI.

- **Default:** `5`

### Car32Name

No description in the shipped INI.

- **Default:** `marbella`

### Car32Offset

No description in the shipped INI.

- **Default:** `5`

### Car33Name

No description in the shipped INI.

- **Default:** `merit`

### Car33Offset

No description in the shipped INI.

- **Default:** `-7`

### Car34Name

No description in the shipped INI.

- **Default:** `minivan`

### Car34Offset

No description in the shipped INI.

- **Default:** `-6`

### Car35Name

No description in the shipped INI.

- **Default:** `mule`

### Car35Offset

No description in the shipped INI.

- **Default:** `-2`

### Car36Name

No description in the shipped INI.

- **Default:** `noose`

### Car36Offset

No description in the shipped INI.

- **Default:** `7`

### Car37Name

No description in the shipped INI.

- **Default:** `nstockade`

### Car37Offset

No description in the shipped INI.

- **Default:** `4`

### Car38Name

No description in the shipped INI.

- **Default:** `oracle`

### Car38Offset

No description in the shipped INI.

- **Default:** `-5`

### Car39Name

No description in the shipped INI.

- **Default:** `packer`

### Car39Offset

No description in the shipped INI.

- **Default:** `-10`

### Car40Name

No description in the shipped INI.

- **Default:** `perennial`

### Car40Offset

No description in the shipped INI.

- **Default:** `5`

### Car41Name

No description in the shipped INI.

- **Default:** `perennial2`

### Car41Offset

No description in the shipped INI.

- **Default:** `5`

### Car42Name

No description in the shipped INI.

- **Default:** `phantom`

### Car42Offset

No description in the shipped INI.

- **Default:** `5`

### Car43Name

No description in the shipped INI.

- **Default:** `pinnacle`

### Car43Offset

No description in the shipped INI.

- **Default:** `4`

### Car44Name

No description in the shipped INI.

- **Default:** `pmp600`

### Car44Offset

No description in the shipped INI.

- **Default:** `5`

### Car45Name

No description in the shipped INI.

- **Default:** `police`

### Car45Offset

No description in the shipped INI.

- **Default:** `7`

### Car46Name

No description in the shipped INI.

- **Default:** `police2`

### Car46Offset

No description in the shipped INI.

- **Default:** `-5`

### Car47Name

No description in the shipped INI.

- **Default:** `pony`

### Car47Offset

No description in the shipped INI.

- **Default:** `5`

### Car48Name

No description in the shipped INI.

- **Default:** `premier`

### Car48Offset

No description in the shipped INI.

- **Default:** `5`

### Car49Name

No description in the shipped INI.

- **Default:** `pres`

### Car49Offset

No description in the shipped INI.

- **Default:** `5`

### Car50Name

No description in the shipped INI.

- **Default:** `primo`

### Car50Offset

No description in the shipped INI.

- **Default:** `5`

### Car51Name

No description in the shipped INI.

- **Default:** `rebla`

### Car51Offset

No description in the shipped INI.

- **Default:** `5`

### Car52Name

No description in the shipped INI.

- **Default:** `ripley`

### Car52Offset

No description in the shipped INI.

- **Default:** `-3`

### Car53Name

No description in the shipped INI.

- **Default:** `romero`

### Car53Offset

No description in the shipped INI.

- **Default:** `3`

### Car54Name

No description in the shipped INI.

- **Default:** `rom`

### Car54Offset

No description in the shipped INI.

- **Default:** `5`

### Car55Name

No description in the shipped INI.

- **Default:** `ruiner`

### Car55Offset

No description in the shipped INI.

- **Default:** `-3`

### Car56Name

No description in the shipped INI.

- **Default:** `sabre`

### Car56Offset

No description in the shipped INI.

- **Default:** `4`

### Car57Name

No description in the shipped INI.

- **Default:** `sabre2`

### Car57Offset

No description in the shipped INI.

- **Default:** `4`

### Car58Name

No description in the shipped INI.

- **Default:** `sabregt`

### Car58Offset

No description in the shipped INI.

- **Default:** `5`

### Car59Name

No description in the shipped INI.

- **Default:** `schafter`

### Car59Offset

No description in the shipped INI.

- **Default:** `3.5`

### Car60Name

No description in the shipped INI.

- **Default:** `sentinel`

### Car60Offset

No description in the shipped INI.

- **Default:** `-15`

### Car61Name

No description in the shipped INI.

- **Default:** `speedo`

### Car61Offset

No description in the shipped INI.

- **Default:** `5`

### Car62Name

No description in the shipped INI.

- **Default:** `stalion`

### Car62Offset

No description in the shipped INI.

- **Default:** `5`

### Car63Name

No description in the shipped INI.

- **Default:** `stretch`

### Car63Offset

No description in the shipped INI.

- **Default:** `5`

### Car64Name

No description in the shipped INI.

- **Default:** `sultan`

### Car64Offset

No description in the shipped INI.

- **Default:** `4`

### Car65Name

No description in the shipped INI.

- **Default:** `sultanrs`

### Car65Offset

No description in the shipped INI.

- **Default:** `4`

### Car66Name

No description in the shipped INI.

- **Default:** `supergt`

### Car66Offset

No description in the shipped INI.

- **Default:** `5`

### Car67Name

No description in the shipped INI.

- **Default:** `taxi`

### Car67Offset

No description in the shipped INI.

- **Default:** `7`

### Car68Name

No description in the shipped INI.

- **Default:** `taxi2`

### Car68Offset

No description in the shipped INI.

- **Default:** `-5`

### Car69Name

No description in the shipped INI.

- **Default:** `trash`

### Car69Offset

No description in the shipped INI.

- **Default:** `4`

### Car70Name

No description in the shipped INI.

- **Default:** `turismo`

### Car70Offset

No description in the shipped INI.

- **Default:** `5`

### Car71Name

No description in the shipped INI.

- **Default:** `uranus`

### Car71Offset

No description in the shipped INI.

- **Default:** `5`

### Car72Name

No description in the shipped INI.

- **Default:** `vigero`

### Car72Offset

No description in the shipped INI.

- **Default:** `-5`

### Car73Name

No description in the shipped INI.

- **Default:** `vigero2`

### Car73Offset

No description in the shipped INI.

- **Default:** `-5`

### Car74Name

No description in the shipped INI.

- **Default:** `vincent`

### Car74Offset

No description in the shipped INI.

- **Default:** `7.5`

### Car75Name

No description in the shipped INI.

- **Default:** `voodoo`

### Car75Offset

No description in the shipped INI.

- **Default:** `5`

### Car76Name

No description in the shipped INI.

- **Default:** `washington`

### Car76Offset

No description in the shipped INI.

- **Default:** `5`

### Car77Name

No description in the shipped INI.

- **Default:** `yankee`

### Car77Offset

No description in the shipped INI.

- **Default:** `5`

### Car78Name

No description in the shipped INI.

- **Default:** `comet5`

### Car78Offset

No description in the shipped INI.

- **Default:** `5`
