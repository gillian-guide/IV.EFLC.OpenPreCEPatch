# Installing

## Requirements

- GTA IV 1.0.0.0, 1.0.0.4, 1.0.4.0, 1.0.6.0, 1.0.6.1, 1.0.7.0, 1.0.8.0,
  EFLC 1.1.0.0/1.1.1.0/1.1.2.0/1.1.3.0, or 1.2.0.43.
- An ASI loader. Ultimate ASI Loader as `dinput8.dll` or `xlive.dll` is
  recommended.

## Files

Copy next to `GTAIV.exe` / `EFLC.exe`:

- `IV.EFLC.OpenPreCEPatch.asi` — the mod.
- `IV.EFLC.OpenPreCEPatch.ini` — the configuration. The ASI creates it on first
  launch if it is missing.

Optional:

- `XLivelessAddon.asi` is a separate successor module built from
  `xlivelessaddon/`. Install it only when you need the XLivelessAddon feature
  set; do not install it as a shim for FusionFix.

## Upgrading from ZolikaPatch

Replace `ZolikaPatch.asi` with `IV.EFLC.OpenPreCEPatch.asi`. Leave
`ZolikaPatch.ini` in place. On first launch the ASI sees the old file, ports
every supported option into `IV.EFLC.OpenPreCEPatch.ini`, carries the per-model
light tables over, and then reads the new file. The old `MiscFixes` bundle is
not ported; use the separate XLivelessAddon module for it. The old file is not modified.

If the new INI already exists, it is used as-is; the old file is not merged.

## INI tags

Each option line ends with a compact tag:

```ini
OutOfCommissionFix = 1 // cached archive, fault-inclusive path still available [1060+]
AimProjectileCrashFix = 1 // ... [all]
```

- `[all]` — applies to every supported build; a missing signature skips it.
- `[1060+]`, `[1070,1080]`, `[all-1080,1130]` — the builds on which the option's
  installs exist.
- `; FF` — the option is automatically disabled when a FusionFix ASI is next to
  the game.

## FusionFix

FusionFix replaces several of the same subsystems. Running both installs on one
site is how a working game turns into a crash, so detection is automatic: if an
ASI whose name contains `FusionFix` is in the game folder (or already loaded),
the overlapping options are forced off and the log records the count. The INI
value is not rewritten; remove FusionFix and the option becomes active again.

The conflicts are listed in `source/fusionfix_compat.cpp`. The groups are:

| OpenPreCEPatch option | FusionFix module / setting |
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

The INI itself marks each of these with `FF`. The `BorderlessWindowed`,
`DoNotPauseOnMinimize` and `VRAMFix` rows are XLivelessAddon options now: the
main module keeps the gate entries so a stale key is still reported, but no
longer installs those features. See `xlivelessaddon/README.md`.

Safety and performance improvements are folded into their original options, so
there are no extra keys to set. The original paths can be selected for testing
with `OPCEP_IMPROVEMENTS=none` or `-DOPCEP_PARITY_BASELINE=ON`; a normal install
does not need either. See `docs/improvements.md`.

## xlive.dll packaging

The original distribution shipped an `xlive.dll` wrapper from XLivelessAddon.
OpenPreCEPatch works with any current ASI loader; if you already use an
`xlive.dll` loader, keep it and do not replace it with a build from another
package.

## Logs

`IV.EFLC.OpenPreCEPatch.log` is written next to the ASI. It records the config
path, build, FusionFix detection and loader stages. Delete it before a fresh
test.
