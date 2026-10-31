// SPDX-License-Identifier: GPL-3.0-or-later
//
// The settings module: the INI document, the typed value readers, the FusionFix
// compatibility gate and the first-run/legacy import. `opp.settings` is the
// façade the feature modules and dllmain import for every INI question; it
// re-exports opp.config so one import covers the whole contract.

module;

export module opp.settings;

export import opp.config;
