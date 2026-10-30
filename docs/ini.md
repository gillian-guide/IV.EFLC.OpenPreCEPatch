# INI reference

Generated from `data/plugins/IV.EFLC.OpenPreCEPatch.ini`.

Tags: `[all]` / `[1060+]` / `[all-1080,1130]` list the game builds an option applies to.
`FF` means the option is automatically disabled when a FusionFix ASI is present.

The tag is the applicability the shipped arm implements. For the recovered
per-build evidence behind it (handler, gate instruction, RVA row) and for
the builds the original is merely pattern-gated on, see
[version-matrix.md](version-matrix.md).

## STABILITY

| key | default | notes |
| --- | --- | --- |
| `BenchmarkFix` | `1` | fix crash after benchmark ends [all-1080,1130] |
| `CombinedRadioFreezeFix` | `1` | fix freeze after Concrete Jungle with the 3 EFLC exclusive radio stations installed |
| `EpisodeSwitchFreezeFix` | `1` | fix crash when switching episode mid game |
| `FreezeGunFix` | `1` | fix crash when firing the ROCKET weapon |
| `WantedLevelCrashFix` | `1` | fix rare crash when getting a wanted level |
| `TryToSkipAllErrors` | `0` | attempt to completely ignore any and all error messages, possibly useful for debugging |

## FIXES

| key | default | notes |
| --- | --- | --- |
| `CutsceneFixes` | `1` | fix zoomed in cutscenes and jittery cutscene camera motion at high framerates [FF] |
| `OutOfCommissionFix` | `1` | fixes the climbing quick-time event in Out of Commission at high FPS [1060-1130, FF] |
| `HighFPSBikePhysicsFix` | `1` | fix bikes sliding out easily above 60FPS [FF] |
| `BikeFeetFix` | `1` | previously TLAD-exclusive foot position fix for chopper bikes [FF] |
| `BikePhoneAnimsFix` | `1` | fixed standing still animation for bikes during a phone call [FF] |
| `HighFPSSpeedupFix` | `1` | fix game speeding up above 300FPS [FF] |
| `SpeedupBugFix` | `1` | fixes the game being permanently sped up slightly in some cases [FF] |
| `VSyncFix` | `1` | fix VSync locking below 60FPS [1004,1060-1061,1070,1100-1120,12043; FF] |
| `MouseFix` | `1` | fixes the mouse deadzone and removes mouse acceleration from snipers [FF] |
| `DrivebyFOVFix` | `1` | fix for driveby shots not landing in the center of the crosshair if you're too high above ground level |

## PERFORMANCE

| key | default | notes |
| --- | --- | --- |
| `StartupTimeFix` | `1` | fixes the 15+ second startup times before the game window even pops up |
| `FastLoading` | `1` | cut loading times in half or more [FF] |
| `NoLoadingSleep` | `1` | makes the game get to the main menu faster [FF] |
| `ImprovedShaderStreaming` | `0` | allegedly improves stutter, potentially unstable |
| `NewMemorySystem` | `0` | reimplements memory allocation, allegedly reducing loading times and memory usage and eliminating SMPA* errors, potentially unstable |

## GRAPHICS

| key | default | notes |
| --- | --- | --- |
| `EmissiveLerpFix` | `1` | fix emissive lights like lampposts slowly fading in and out depending on time of day [FF] |
| `ReversingLightFix` | `1` | fix reverse lights being cast in the middle of the car's hood [FF] |
| `BuildingAlphaFix` | `1` | fix missing fadein for LODs [1060+; FF] |
| `BuildingDynamicShadows` | `0` | enable night shadows cast from buildings [FF] |
| `CarDynamicShadowFix` | `1` | tunnel, lamppost and heli spotlight shadows for cars (disables headlight shadows) [FF] |
| `CarPartsShadowFix` | `1` | tunnel, lamppost and heli spotlight shadows for detached car parts [FF] |
| `ForceShadowsOnObjects` | `1` | make all objects cast shadows (e.g. bollards, Hove Beach train tracks) [FF] |
| `DualVehicleHeadlights` | `0` | cast 2 separate lights for car headlights with their own shadows |
| `ForceCarHeadlightShadows` | `0` | use this to re-enable headlight shadows if CarDynamicShadowFix is enabled, buggy and should never be used with DualVehicleHeadlights [FF] |
| `NoImpostorHeightCutoff` | `1` | removes arbitrary height cutoff from vehicle imposters, makes them behave more like V [FF] |
| `NoLODLightHeightCutoff` | `0` | removes arbitrary height cutoff from distant lights, might look inaccurate up close [FF] |
| `RemoveStaticCarShadows` | `0` | remove static shadow textures under cars |
| `RemoveStaticCutsceneCarShadows` | `0` | remove static shadow textures under cars in cutscenes |
| `InfiniteCarBottomDistance` | `0` | remove the draw distance from static vehicle shadows |

## EPISODIC

| key | default | notes |
| --- | --- | --- |
| `LoadDLCs` | `1` | load TBoGT and TLAD from EFLC or CE folders, plus any more addon episodes in the same format (extra DLC folders should be named Episode3, Episode4, etc. and their ids in setup2.xml be the episode id multiplied by 2) [1060,1070,1080] |
| `SCOSignatureFix` | `1` | allow loading of any .sco file regardless of which episode it came from (or which platform it came from, in case of patch 4 and below) this is required for trainer IV parachute support to work [all; FF] |
| `EpisodicVehicleSupport` | `0` | APC and buzzard support for all episodes [1004,1060-1130,1061,12043; FF] |
| `EpisodicWeaponSupport` | `0` | explosive shotgun, stickybomb and DSR1 sniper support for all episodes [1004,1060-1130,1061,12043; FF] |
| `PoliceEpisodicWeaponSupport` | `0` | makes cops use P90s and assault shotguns - only enable this if those weapons exist, or NOOSE and FIB will spawn unarmed [1004,1060-1130,1061,12043; FF] |
| `TBoGTHeliHeightLimit` | `0` | unlocks the height limit for aircraft to the TBoGT level for all episodes [1004,1060-1130,1061,12043; FF] |
| `PipeBombDropIconSupport` | `0` | use droppipebomb_icon for pipe bomb in base IV [1004,1060-1130,1061,12043; FF] |

## COMPATIBILITY

| key | default | notes |
| --- | --- | --- |
| `LaunchWithPlayGTAIV` | `1` | ability to use a (non-SecuROM) launcher renamed to PlayGTAIV [1000,1040,1060,1070,1080] |
| `NewFilesCompatibility` | `1` | allows script.img and radiohud from newer versions to work (script.img is still 1060+ only) |
| `NewSavesCompatibility` | `1` | allows newer savefiles to load properly without needing them to be downgraded first [1060-1080] |
| `SavegameFix` | `1` | remove GFWL savegame CRC check |
| `GFWLProtectionDisabler` | `1` | disable the GFWL xlive.dll protection check |
| `SteamAchievements` | `1` | mirror GFWL achievements to Steam |

## MULTIPLAYER

| key | default | notes |
| --- | --- | --- |
| `BetterMPSync` | `1` | increase sync interval when far away |
| `SMPA60Fix` | `1` | fix SMPA60 when abusing the game with car mods |
| `MPNikoCrashFix` | `1` | fix crash if another player changes their model to Niko then changes it back to something else |
| `IncreasePtrNodes` | `1` | increase the PtrNode limit, fixes random crashing in big Cunning Stunts maps [FF] |
| `BadPlayerModelCrashFix` | `1` | fix crash if another player switches to an invalid playermodel |
| `FreezeCarFix` | `1` | fix crash if another player spawns a car that has the wrong vehicle type |
| `BlackscreenFix` | `1` | fix a modmenu crash method |
| `CrashMsgFix` | `1` | fix a modmenu crash method |
| `GroupHackFix` | `1` | fix a modmenu crash method |
| `RemoveAllWeaponsProtection` | `1` | make other players no longer able to remove all your weapons |
| `TeleportProtection` | `1` | make other players no longer able to to teleport you |
| `SuperLODFix` | `1` | fix crash if SuperLOD is spawned [1070,1080] |
| `AimProjectileCrashFix` | `0` | crash fix, usually happens in GTAC |
| `PathNodeCrashFix` | `0` | crash fix, usually happens in GTAC |
| `CollisionDeadlockFix` | `0` | freeze fix, usually happens in GTAC |
| `DebugPrintSpawns` | `0` | log all entities as they sync in into GTAC console (GTAC only, requires FreezeCarFix to be on) this doesn't actually log which player spawned the entity but who's currently syncing it which 99% of the time is the person who spawned it ONLY if they spawned it while they were in sync range |
| `RemoveModChecks` | `1` | remove modded-file checks (patch 4, 1060 and 1070) [1004,1060,1070] |
| `RemoveRGSCLoginRequirement` | `1` | remove Social Club login requirement for MP (all builds; 1.0.8.0 uses an RVA fallback) [all] |
| `RemoveGFWLUpdatePopup` | `1` | remove "update or sign out of LIVE" popup for GFWL (patch 5 and below) [1000-1040] |

## VEHICLES

| key | default | notes |
| --- | --- | --- |
| `GarageModelFix` | `1` | fixes cars being randomly changed in parking spots if addons models are added |
| `IncreaseVehicleStructLimit` | `1` | increase the amount of vehicles with different models that can be loaded at one time [FF] |
| `IncreaseVehicleModelLimit` | `1` | increase the amount of possible vehicles.ide entries to 1024 [FF] |
| `CarDecalsFix` | `0` | fix bulletholes not appearing on some modded cars outside TBoGT |
| `NoLiveryLimit` | `1` | removes the 4 livery limit, allowing up to 255 unique livery textures for any car [FF] |
| `PopupHeadlightsFlag` | `0` | vehicles.ide flag for Hakumai's popup headlights on any car that supports it (+popuplights) |
| `CopSirensFlag` | `0` | add +siren vehicles.ide flag to cars to make them use sirens |
| `PlaneCrashFix` | `0` | fix random crashing while using actual planes |
| `MovingPlaneParts` | `0` | makes propellers, ailerons, elevators and rudders move on planes that support them |
| `ShakingEngineBlock` | `0` | make the engineblock part of cars shake like V if they have support for it |
| `MovingSteeringWheelSupport` | `0` | make the "steeringwheel" car bone rotate properly if it exists (Example of a car with proper steering wheel setup: https://mega.nz/file/vcAAib6Z#bsbJaseAbdxleRwr1XGL-d_it_P9DyjsPoXP-pnMoq8 ) use the new +wheelangle: vehicles.ide flag to specify an angle the steering wheel should be tilted forward or backwards by (in degrees) while porting a car from V, write the angle down into this flag and set the actual wheel's angle to 0, 0, 0 to make it work properly |

## MISC

| key | default | notes |
| --- | --- | --- |
| `RestoreDeathMusic` | `0` | restores the unused music on death [FF] |
| `OverrideGTADatPath` | `0` | load gta.dat from another path |
| `OverrideGTADatPathTo` | `common:/data/gta.dat` | the path to load gta.dat from if the above is enabled |
| `AllowHashesForIDE` | `1` | allow entering raw hashes into .ide files if you add "hash:" before the hash (in unsigned decimal) to get around the character limit in .ide entries |
| `PlayerAsPedComponentsFix` | `1` | fix Niko peds' clothes never changing properly in the trainer |
| `PedModelsInCutscenesFix` | `1` | fixes crash when starting cutscenes as a ped |

## HeadlightConeOffsets

per-model headlight cone offset

| key | default | notes |
| --- | --- | --- |
| `Car1Name` | `admiral` |  |
| `Car1Offset` | `-5` |  |
| `Car2Name` | `airtug` |  |
| `Car2Offset` | `5` |  |
| `Car3Name` | `benson` |  |
| `Car3Offset` | `-4` |  |
| `Car4Name` | `biff` |  |
| `Car4Offset` | `-6` |  |
| `Car5Name` | `boxville` |  |
| `Car5Offset` | `-8` |  |
| `Car6Name` | `buccaneer` |  |
| `Car6Offset` | `-5` |  |
| `Car7Name` | `burrito` |  |
| `Car7Offset` | `4` |  |
| `Car8Name` | `burrito2` |  |
| `Car8Offset` | `4` |  |
| `Car9Name` | `bus` |  |
| `Car9Offset` | `-4` |  |
| `Car10Name` | `cabby` |  |
| `Car10Offset` | `-6` |  |
| `Car11Name` | `cavalcade` |  |
| `Car11Offset` | `4` |  |
| `Car12Name` | `chavos` |  |
| `Car12Offset` | `3` |  |
| `Car13Name` | `cognoscenti` |  |
| `Car13Offset` | `7` |  |
| `Car14Name` | `comet` |  |
| `Car14Offset` | `6` |  |
| `Car15Name` | `df8` |  |
| `Car15Offset` | `3` |  |
| `Car16Name` | `dilettante` |  |
| `Car16Offset` | `4` |  |
| `Car17Name` | `dukes` |  |
| `Car17Offset` | `-5` |  |
| `Car18Name` | `emperor` |  |
| `Car18Offset` | `5` |  |
| `Car19Name` | `emperor2` |  |
| `Car19Offset` | `5` |  |
| `Car20Name` | `esperanto` |  |
| `Car20Offset` | `5` |  |
| `Car21Name` | `fbi` |  |
| `Car21Offset` | `5` |  |
| `Car22Name` | `feltzer` |  |
| `Car22Offset` | `7` |  |
| `Car23Name` | `firetruk` |  |
| `Car23Offset` | `7` |  |
| `Car24Name` | `fortune` |  |
| `Car24Offset` | `17` |  |
| `Car25Name` | `futo` |  |
| `Car25Offset` | `5` |  |
| `Car26Name` | `fxt` |  |
| `Car26Offset` | `5` |  |
| `Car27Name` | `habanero` |  |
| `Car27Offset` | `9` |  |
| `Car28Name` | `hakumai` |  |
| `Car28Offset` | `4` |  |
| `Car29Name` | `ingot` |  |
| `Car29Offset` | `-15` |  |
| `Car30Name` | `intruder` |  |
| `Car30Offset` | `5` |  |
| `Car31Name` | `manana` |  |
| `Car31Offset` | `5` |  |
| `Car32Name` | `marbella` |  |
| `Car32Offset` | `5` |  |
| `Car33Name` | `merit` |  |
| `Car33Offset` | `-7` |  |
| `Car34Name` | `minivan` |  |
| `Car34Offset` | `-6` |  |
| `Car35Name` | `mule` |  |
| `Car35Offset` | `-2` |  |
| `Car36Name` | `noose` |  |
| `Car36Offset` | `7` |  |
| `Car37Name` | `nstockade` |  |
| `Car37Offset` | `4` |  |
| `Car38Name` | `oracle` |  |
| `Car38Offset` | `-5` |  |
| `Car39Name` | `packer` |  |
| `Car39Offset` | `-10` |  |
| `Car40Name` | `perennial` |  |
| `Car40Offset` | `5` |  |
| `Car41Name` | `perennial2` |  |
| `Car41Offset` | `5` |  |
| `Car42Name` | `phantom` |  |
| `Car42Offset` | `5` |  |
| `Car43Name` | `pinnacle` |  |
| `Car43Offset` | `4` |  |
| `Car44Name` | `pmp600` |  |
| `Car44Offset` | `5` |  |
| `Car45Name` | `police` |  |
| `Car45Offset` | `7` |  |
| `Car46Name` | `police2` |  |
| `Car46Offset` | `-5` |  |
| `Car47Name` | `pony` |  |
| `Car47Offset` | `5` |  |
| `Car48Name` | `premier` |  |
| `Car48Offset` | `5` |  |
| `Car49Name` | `pres` |  |
| `Car49Offset` | `5` |  |
| `Car50Name` | `primo` |  |
| `Car50Offset` | `5` |  |
| `Car51Name` | `rebla` |  |
| `Car51Offset` | `5` |  |
| `Car52Name` | `ripley` |  |
| `Car52Offset` | `-3` |  |
| `Car53Name` | `romero` |  |
| `Car53Offset` | `3` |  |
| `Car54Name` | `rom` |  |
| `Car54Offset` | `5` |  |
| `Car55Name` | `ruiner` |  |
| `Car55Offset` | `-3` |  |
| `Car56Name` | `sabre` |  |
| `Car56Offset` | `4` |  |
| `Car57Name` | `sabre2` |  |
| `Car57Offset` | `4` |  |
| `Car58Name` | `sabregt` |  |
| `Car58Offset` | `5` |  |
| `Car59Name` | `schafter` |  |
| `Car59Offset` | `3.5` |  |
| `Car60Name` | `sentinel` |  |
| `Car60Offset` | `-15` |  |
| `Car61Name` | `speedo` |  |
| `Car61Offset` | `5` |  |
| `Car62Name` | `stalion` |  |
| `Car62Offset` | `5` |  |
| `Car63Name` | `stretch` |  |
| `Car63Offset` | `5` |  |
| `Car64Name` | `sultan` |  |
| `Car64Offset` | `4` |  |
| `Car65Name` | `sultanrs` |  |
| `Car65Offset` | `4` |  |
| `Car66Name` | `supergt` |  |
| `Car66Offset` | `5` |  |
| `Car67Name` | `taxi` |  |
| `Car67Offset` | `7` |  |
| `Car68Name` | `taxi2` |  |
| `Car68Offset` | `-5` |  |
| `Car69Name` | `trash` |  |
| `Car69Offset` | `4` |  |
| `Car70Name` | `turismo` |  |
| `Car70Offset` | `5` |  |
| `Car71Name` | `uranus` |  |
| `Car71Offset` | `5` |  |
| `Car72Name` | `vigero` |  |
| `Car72Offset` | `-5` |  |
| `Car73Name` | `vigero2` |  |
| `Car73Offset` | `-5` |  |
| `Car74Name` | `vincent` |  |
| `Car74Offset` | `7.5` |  |
| `Car75Name` | `voodoo` |  |
| `Car75Offset` | `5` |  |
| `Car76Name` | `washington` |  |
| `Car76Offset` | `5` |  |
| `Car77Name` | `yankee` |  |
| `Car77Offset` | `5` |  |
| `Car78Name` | `comet5` |  |
| `Car78Offset` | `5` |  |
