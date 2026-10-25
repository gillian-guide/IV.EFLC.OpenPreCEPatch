# FAQ

## Is this a fork of ZolikaPatch?

Yes. ZolikaPatch is abandoned and closed; this is its community successor. The
original's options, defaults, signatures and behaviour are preserved, and the
code is now readable and maintainable.

## Why does the INI have sections now?

The original put every key in `[Options]`. A hundred keys in one block is hard
to navigate, so this release groups them by subsystem. On first launch the ASI
ports an old `ZolikaPatch.ini` into the grouped `IV.EFLC.OpenPreCEPatch.ini`;
no external converter is needed. A flat INI is also still read if you prefer to
keep one.

## What happened to `MiscFixes`?

It is split into five keys under `[XLIVELESS]`. An old INI with `MiscFixes`
keeps the original bundle behaviour during the import; an INI without it uses
the five keys.

## Why is there an `XLivelessAddon.asi` next to the mod?

FusionFix and some other plugins look for that module name before deciding what
to install. The shim only provides the name; it does not load the mod or patch
anything. The real module is `IV.EFLC.OpenPreCEPatch.asi`.

## Does it work with FusionFix?

Yes. When a FusionFix ASI is present, OpenPreCEPatch automatically disables the
options that patch the same subsystems; the INI marks them with `FF` and the log
records how many were gated. Remove FusionFix and they become active again.

## Does it work on 1.2.0.59?

No. The classifier returns `unknown` and installs nothing. Use a supported build.

## Is multiplayer supported?

The mod preserves the original's multiplayer fixes, including the GFWL and
CitizenIV paths. Use only with a legitimate game copy and the services you are
allowed to use.
