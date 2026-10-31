# Troubleshooting

## The game boots without the mod

Read `IV.EFLC.OpenPreCEPatch.log` next to the ASI. It is written at process
attach and names the config path and build. If it is missing, the ASI was not
loaded: check the ASI loader, the 32-bit build and the file name.

## The log says `build: unknown`

The executable is newer than the supported set, or its version resource is
unreadable. The loader installs nothing in that case. Use a supported build.

## Nothing changes after editing the INI

The file is read once at load. Close the game before editing, and make sure the
INI sits next to the ASI. The option keys are case-sensitive in the shipped
parser.

## A crash happens only with one option

Set that option to `0` and confirm. The original's faults are preserved: some
crashes are the documented behaviour, not a regression. Check
`docs/origins.md` and the option's entry in `docs/features.md`.

## An option is ignored and the INI says `FF`

A FusionFix ASI is in the game folder. OpenPreCEPatch disables overlapping
options automatically so two mods do not patch the same subsystem. Remove
FusionFix, or use its equivalent setting, if you want the behaviour. The log
records how many options were gated.

## FusionFix reports a missing/incompatible XLivelessAddon

Install the separate XLivelessAddon companion (`XLivelessAddon.asi` +
`XLivelessAddon.ini`) from its own repository. The main ASI no longer carries
those features or an inert marker shim.

## The old INI is not being used

The old file is imported only when `IV.EFLC.OpenPreCEPatch.ini` is absent. Delete
or rename the new file and relaunch to re-import `ZolikaPatch.ini`. The old file
is never modified.

## Crash on load with a non-original `xlive.dll`

Use an `xlive.dll` that matches the game version, or the one from the original
package. Do not mix loaders.

## A key is ignored

Check the section. A key may appear only once across the grouped sections; the
first match in `[Options]`, then the grouped order, wins. The importer keeps the
undocumented `Patch7RemoveModChecks` alias; other unknown keys are ignored.
