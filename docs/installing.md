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

- The real `XLivelessAddon.asi` is only needed if another plugin depends on the
  patches it applies. The `XLivelessAddon.asi` built by this repository is an
  inert marker shim and should **not** be used together with FusionFix: the real
  addon boots, the inert shim does not. If you already have the real addon, keep
  it; otherwise do not install the shim.

## Upgrading from ZolikaPatch

Replace `ZolikaPatch.asi` with `IV.EFLC.OpenPreCEPatch.asi`. Leave
`ZolikaPatch.ini` in place. On first launch the ASI sees the old file, ports
every option into `IV.EFLC.OpenPreCEPatch.ini`, splits the old `MiscFixes`
bundle into the five per-fix keys, carries the per-model light tables over, and
then reads the new file. The old file is not modified.

If the new INI already exists, it is used as-is; the old file is not merged.

## INI tags

Each option line ends with a compact tag:

```ini
SkipIntro = 1          // skip intro screens [all; FF]
OutOfCommissionFix = 1 // ... [1060+]
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

| OpenPreCEPatch options | FusionFix module |
| --- | --- |
| SkipIntro, SkipMenu | `skipintro.ixx` |
| BorderlessWindowed, DoNotPauseOnMinimize | `windowed.ixx` |
| EpisodicVehicleSupport, EpisodicWeaponSupport, PoliceEpisodicWeaponSupport, TBoGTHeliHeightLimit, SCOSignatureFix, PipeBombDropIconSupport | `episodiccontent.ixx` |
| RestoreDeathMusic | `deathmusic.ixx` |
| BikeFeetFix, BikePhoneAnimsFix, ReversingLightFix | `fixes.ixx` |
| CutsceneFixes | `cutscenecam.ixx`, `frameratevigilante.ixx` |
| MouseFix | `rawinput.ixx`, `sniper.ixx` |
| IncreasePtrNodes, IncreaseVehicleModelLimit, IncreaseVehicleStructLimit, NoLiveryLimit | `limits.ixx` |
| FastLoading, NoLoadingSleep | `loadingdelays.ixx`, `framelimit.ixx` |
| HighFPSBikePhysicsFix, HighFPSSpeedupFix, SpeedupBugFix | `frameratevigilante.ixx` |
| Car/building shadow options, EmissiveLerpFix | `shadows.ixx`, `nightshadows.ixx`, `fixes.ixx` |
| HighQualityReflections | `experimental.ixx`, `reflectionmsaa.ixx` |
| ImprovedShaderStreaming | `shaders.ixx` |
| VRAMFix | `memory.ixx` |
| VSyncFix | `framelimit.ixx`, `windowed.ixx` |
| AddOptionsInMenu | `settings.ixx` |
| LocalSettingsPath | `userdata.ixx` |

The INI itself marks each of these with `FF`.

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
