// SPDX-License-Identifier: GPL-3.0-or-later
//
// XLivelessAddon marker shim.
//
// FusionFix and a few other plugins change what they install when they see a
// loaded XLivelessAddon.asi. This module exists only to satisfy that check. It
// does not load the real mod, patch anything, or keep any state; the
// `OpcepCompatShim` export lets OpenPreCEPatch recognise it as a marker rather
// than as an external XLivelessAddon.

#include <windows.h>

extern "C" __declspec(dllexport) void OpcepCompatShim()
{
}

extern "C" __declspec(dllexport) void InitializeASI()
{
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
    return TRUE;
}
