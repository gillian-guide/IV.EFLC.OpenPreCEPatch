#pragma once
// The recovered scan signatures more than one feature module reads. The original
// handler uses one site for several options, so the signature is declared once
// here: two copies could drift apart silently, and the site's comment (which
// instruction the match is) belongs beside the declaration.
//
// A signature only one module reads stays in that module, beside its arm.

#include <string_view>

// CVehicleModelInfo::setVehicleModelOpts' `noboot` compare (1.0.8.0 0xA464D0):
// the signature starts at the `jnz +0C`'s rel8 (match 0xA46550), so the publish
// takes the branch opcode as MatchAddress(pattern, -1) and the install overwrites
// the `mov ecx,7` five bytes before it. CopSirensFlag, PopupHeadlightsFlag and the
// vehicle steer arms all install the copsirens IDE body here.
inline constexpr std::string_view kIdeHookSlotSignature = "0C 83 88 94 00 00 00 04";

// CModelInfoStore::getModelByKey (1.0.8.0 0x8DD2D0): the model lookup both
// PedModelsInCutscenesFix and PlayerAsPedComponentsFix publish.
inline constexpr std::string_view kPedModelLookupSignature =
    "8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E";
