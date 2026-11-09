# Features

The catalogue is 99 `[Options]` keys. Each belongs to one feature module, and the tables below keep that grouping: a key's module is where its patterns, arm and bodies live. The *shipped* column is the value `ZolikaPatch.ini` carries; the *absent* column is the default the arm passes when the key is missing. They differ for a handful of keys.

`all 12` means every build the catalogue supports: 1.0.0.0, 1.0.0.4, 1.0.4.0, 1.0.6.0, 1.0.6.1, 1.0.7.0, 1.0.8.0, 1.1.0.0, 1.1.1.0, 1.1.2.0, 1.1.3.0 and 1.2.0.43. Nine of those have images and are scored by the dump matrix. A key's scope is the mask its arm gates on; inside that scope a missing signature is still a clean skip, as it is in the original.

## stability (18)

Crash fixes. Each key has its own arm in the module's registration, and the module also carries the 38,455-entry model-name table `BadPlayerModelCrashFix` uses.

| key | shipped | absent | builds |
| --- | ---: | ---: | --- |
| `AimProjectileCrashFix` | 1 | 1 | all 12 |
| `BadPlayerModelCrashFix` | 1 | 1 | all 12 |
| `BlackscreenFix` | 1 | 1 | all 12 |
| `CollisionDeadlockFix` | 1 | 1 | all 12 |
| `CrashMsgFix` | 1 | 1 | all 12 |
| `EpisodeSwitchFreezeFix` | 1 | 1 | all 12 |
| `FreezeGunFix` | 1 | 1 | all 12 |
| `GroupHackFix` | 1 | 1 | all 12 |
| `HighFPSSpeedupFix` | 1 | 1 | all 12 |
| `MPNikoCrashFix` | 1 | 1 | all 12 |
| `PathNodeCrashFix` | 1 | 1 | all 12 |
| `PlaneCrashFix` | 1 | 1 | all 12 |
| `PlayerAsPedComponentsFix` | 1 | 1 | all 12 |
| `RemoveAllWeaponsProtection` | 1 | 1 | all 12 |
| `SCOSignatureFix` | 1 | 1 | all 12 |
| `SMPA60Fix` | 1 | 1 | all 12 |
| `TryToSkipAllErrors` | 1 | 1 | all 12 |
| `WantedLevelCrashFix` | 1 | 1 | all 12 |

## vehicles (21)

Vehicle, steer and shadow keys. The module carries the transcribed steer consumer that five of these keys depend on, and the registered tables the body proof resolves by content.

| key | shipped | absent | builds |
| --- | ---: | ---: | --- |
| `BikeFeetFix` | 1 | 1 | all 12 |
| `BikePhoneAnimsFix` | 1 | 1 | all 12 |
| `CarDecalsFix` | 1 | 1 | all 12 |
| `CarDynamicShadowFix` | 1 | 0 | all 12 |
| `CarPartsShadowFix` | 1 | 1 | all 12 |
| `DualVehicleHeadlights` | 0 | 1 | all 12 |
| `EpisodicVehicleSupport` | 1 | 1 | all except 1.0.0.0, 1.0.4.0 |
| `ForceCarHeadlightShadows` | 0 | 1 | all 12 |
| `FreezeCarFix` | 1 | 1 | all 12 |
| `GarageModelFix` | 1 | 1 | all 12 |
| `HighFPSBikePhysicsFix` | 1 | 1 | all 12 |
| `IncreaseVehicleModelLimit` | 1 | 1 | all 12 |
| `IncreaseVehicleStructLimit` | 1 | 1 | all 12 |
| `InfiniteCarBottomDistance` | 1 | 1 | all 12 |
| `MovingPlaneParts` | 1 | 1 | all 12 |
| `MovingSteeringWheelSupport` | 1 | 1 | all 12 |
| `ShakingEngineBlock` | 1 | 1 | all 12 |
| `NoLiveryLimit` | 1 | 1 | all 12 |
| `PipeBombDropIconSupport` | 1 | 1 | all except 1.0.0.0, 1.0.4.0 |
| `ReversingLightFix` | 1 | 1 | all 12 |
| `TeleportProtection` | 1 | 1 | all 12 |

## graphics (16)

Rendering, light and shadow keys, including the 146-instruction cutscene chain and the 157-instruction reflection body.

| key | shipped | absent | builds |
| --- | ---: | ---: | --- |
| `BorderlessWindowed` | 1 | 1 | all 12 |
| `BuildingAlphaFix` | 1 | 1 | all 12 |
| `BuildingDynamicShadows` | 0 | 1 | all 12 |
| `CutsceneFixes` | 1 | 1 | all 12 |
| `EmissiveLerpFix` | 1 | 1 | all 12 |
| `ForceDynamicShadowsEverywhere` | 0 | 0 | all 12 |
| `ForceShadowsOnObjects` | 1 | 1 | all 12 |
| `HighQualityReflections` | 0 | 0 | all 12 |
| `ImprovedShaderStreaming` | 1 | 1 | all 12 |
| `NoImpostorHeightCutoff` | 1 | 1 | all 12 |
| `NoLODLightHeightCutoff` | 0 | 0 | all 12 |
| `RemoveBoundingBoxCulling` | 0 | 0 | all 12 |
| `RemoveStaticCarShadows` | 0 | 0 | all 12 |
| `RemoveStaticCutsceneCarShadows` | 0 | 0 | all 12 |
| `SuperLODFix` | 1 | 1 | 1.0.7.0, 1.0.8.0 |
| `VRAMFix` | 1 | 1 | all 12 |

## loading (18)

Startup, loading, save and file-compatibility keys, plus the launcher stage and the save converter.

| key | shipped | absent | builds |
| --- | ---: | ---: | --- |
| `BenchmarkFix` | 1 | 1 | all except 1.0.8.0, 1.1.3.0 |
| `FastLoading` | 1 | 1 | all 12 |
| `LoadDLCs` | 1 | 1 | 1.0.6.0, 1.0.7.0, 1.0.8.0 |
| `LocalSettingsPath` | 0 | 0 | all 12 |
| `NewFilesCompatibility` | 1 | 1 | all 12 |
| `NewMemorySystem` | 0 | 0 | all 12 |
| `NewSavesCompatibility` | 1 | 1 | all 12 |
| `NoLoadingSleep` | 1 | 0 | all 12 |
| `OverrideGTADatPath` | 0 | 0 | all 12 |
| `OverrideGTADatPathTo` | - | - | all 12 |
| `RemoveGFWLUpdatePopup` | 0 | 0 | all 12 |
| `RemoveRGSCLoginRequirement` | 1 | 0 | all 12 |
| `SavegameFix` | 1 | 1 | all 12 |
| `SkipIntro` | 1 | 1 | all 12 |
| `SkipLauncher` | 1 | 1 | all 12 |
| `SkipMenu` | 0 | 0 | all 12 |
| `StartupTimeFix` | 1 | 1 | all 12 |
| `VSyncFix` | 1 | 0 | all except 1.0.0.0, 1.0.4.0, 1.0.8.0, 1.1.3.0 |

`OverrideGTADatPathTo` is the catalogue's one string-valued key: it has no numeric default, the shipped INI leaves its line commented out, and the setter runs only when the key is present. `SkipLauncher` is open by construction: the stored baseline records the reference tree's uncalled 7-row arm while this tree implements the original's 25 rows, so the scoreboard reports it as oracle-verified rather than closed.

## audio (13)

The keys the original groups with the radio and the world: audio, input, pointer pools, MP sync and the episodic weapons.

| key | shipped | absent | builds |
| --- | ---: | ---: | --- |
| `CombinedRadioFreezeFix` | 1 | 1 | all 12 |
| `RestoreDeathMusic` | 1 | 0 | all 12 |
| `DoNotPauseOnMinimize` | 0 | 0 | all 12 |
| `MouseFix` | 1 | 1 | all 12 |
| `EpisodicWeaponSupport` | 1 | 1 | all except 1.0.0.0, 1.0.4.0 |
| `OutOfCommissionFix` | 1 | 1 | all 12 |
| `PedModelsInCutscenesFix` | 1 | 1 | all 12 |
| `PoliceEpisodicWeaponSupport` | 0 | 0 | all except 1.0.0.0, 1.0.4.0 |
| `TBoGTHeliHeightLimit` | 0 | 0 | all except 1.0.0.0, 1.0.4.0 |
| `IncreasePtrNodes` | 1 | 1 | all 12 |
| `NoPhysicsSleep` | 1 | 0 | all 12 |
| `AllowHashesForIDE` | 1 | 1 | all 12 |
| `BetterMPSync` | 0 | 0 | all 12 |

## misc (13)

The bundled `MiscFixes` table, the mod-check stubs, the flag keys, the menu integration and the process gate.

| key | shipped | absent | builds |
| --- | ---: | ---: | --- |
| `CopSirensFlag` | 1 | 1 | all 12 |
| `DrivebyFOVFix` | 1 | 1 | all 12 |
| `MiscFixes` | 1 | 1 | all 12 |
| `Patch4RemoveModChecks` | 0 | 0 | all 12 |
| `Patch6and7RemoveModChecks` | 1 | 0 | 1.0.6.0, 1.0.7.0 |
| `Patch7RemoveModChecks` | - | 0 | 1.0.6.0, 1.0.7.0 |
| `PopupHeadlightsFlag` | 1 | 1 | all 12 |
| `RemoveUselessChecks` | 0 | 0 | 1.0.4.0, 1.0.7.0, 1.0.8.0 |
| `SpeedupBugFix` | 1 | 1 | all 12 |
| `AddOptionsInMenu` | 1 | 1 | all 12 |
| `DebugPrintSpawns` | 0 | 0 | all 12 |
| `FixPCIDKickbug` | 0 | 1 | all 12 |
| `LaunchWithPlayGTAIV` | 1 | 0 | 1.0.0.0, 1.0.4.0, 1.0.6.0, 1.0.7.0, 1.0.8.0 |

`Patch7RemoveModChecks` is an undocumented alias with no shipped line: setting it to 1 adds the Patch6and7 install set, which is why its shipped column is empty. `AddOptionsInMenu` is the one key stored as the parse's own answer rather than compared with 1, so `2` enables it; every other key requires exactly `1`.

## Reading a key

The defaults above come from `source/options.inc`, which `tools/gen_options.py` generates from the recovered behaviour inventory and `tools/gen_ini.py` serialises into the shipped INI; both files are drift-checked by `ctest`. The per-key evidence - contract rows, preserved faults, and the transcription decision for every body - is in [parity.md](parity.md), and the per-build scoreboard is [matrix.md](matrix.md).
