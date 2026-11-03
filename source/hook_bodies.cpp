// zp.hook_bodies - the assembly unit: the rebased globals the bodies address by
// name, the bodies themselves and the resolver's table.
//
// An implementation unit, not part of the interface: the bodies are `__declspec(naked)`
// 32-bit assembly with inline `__asm`, which MSVC refuses to write into a module
// interface (C2488). `@exact` marks a byte-exact transcription; tools/verify_bodies.py
// proves those instruction-for-instruction against the shipped ASI.
//
// Everything that is ordinary C++ lives outside this unit: the OOC payload
// (source/ooc.cpp), the NMS allocator (source/nms_allocator.cpp), the NewSaves
// converter (source/new_saves.cpp), the native-script call layer
// (source/script_calls.cpp) and the steer/engine/plane consumer
// (source/vehicle_steer.cpp). Only the bodies, their published data and the
// helpers the inline assembly calls remain here.
//
// The x64 write-set dump never runs the bodies - it needs only their addresses, which
// tests/stub_bodies.cpp provides - so the assembly region is compiled out under
// ZP_DUMP_TOOL and the table names the stubs.

module;

#include "common.hxx"
#include "common.h"
#include "rebased.h"
#include "hooks.h"
#include "driveby_seh.h"
#include "body_helpers.h"

#include "hook_bodies.h"

module zp.hook_bodies;

import zp.common;
import zp.game;
import zp.patch;
import zp.catalogue;

// Every body's declaration in this unit's purview: it gives the definitions
// below external linkage (the install arms and the resolver's table bind to them
// by C name).
extern "C"
{
#include "hook_bodies_decls.inc"
}

// ---------------------------------------------------------------------------
// Recovered values the bodies and their published globals were built from.
// ---------------------------------------------------------------------------

// Recovered float values. The value is the source of truth; the static_asserts
// pin the recovered bit patterns, so a changed constant is a compile error.
constexpr float kFloatQuarter = 0.25f;
constexpr float kDrivebyFov = 45.0f;
constexpr float kPiOver2 = 1.5707964f;  // the original's rounded pi/2
constexpr float kFchsOffset = 2.0f;
constexpr float kBikeSpeedThreshold = 0.0066666668f;
constexpr float kCutsceneEps = 0.05f;
constexpr float kCutsceneSpeedLimit = 0.5f;
constexpr double kCutsceneLimit = 0.3333;  // the original's stored double
constexpr double kCutsceneZero = 0.0;
static_assert(std::bit_cast<std::uint32_t>(kFloatQuarter) == 0x3E800000u);
static_assert(std::bit_cast<std::uint32_t>(kDrivebyFov) == 0x42340000u);
static_assert(std::bit_cast<std::uint32_t>(kPiOver2) == 0x3FC90FDBu);
static_assert(std::bit_cast<std::uint32_t>(kFchsOffset) == 0x40000000u);
static_assert(std::bit_cast<std::uint32_t>(kBikeSpeedThreshold) == 0x3BDA740Eu);
static_assert(std::bit_cast<std::uint32_t>(kCutsceneEps) == 0x3D4CCCCDu);
static_assert(std::bit_cast<std::uint32_t>(kCutsceneSpeedLimit) == 0x3F000000u);
static_assert(std::bit_cast<std::uint64_t>(kCutsceneLimit) == 0x3FD554C985F06F69ull);
static_assert(std::bit_cast<std::uint64_t>(kCutsceneZero) == 0x0ull);
constexpr std::uint32_t kFloatAbsMask = 0x7FFFFFFFu;  // clears a float's sign bit

// Recovered layout constants the compiled helpers still use: the garage's
// cleared flag word and the NewFiles/NoLivery/Episode name scratch.
constexpr std::uint16_t kClearedLowBit = 0xFFFEu;          // clears flag bit 0
constexpr std::uint32_t kNewFilesNameA = 0x10u;             // name, update A
constexpr std::uint32_t kNewFilesNameB = 0x10Fu;            // name, update B
constexpr std::size_t kNoLiveryNameCapacity = 0x10u;        // strcpy_s extent
constexpr std::size_t kEpisodeSuffixCapacity = 0x40u;       // "\\Episode%d\\"
constexpr std::uint8_t kMovssStorePrefix = 0xF3u;           // movss dword ptr
constexpr std::uint8_t kMovssTwoByteOpcode = 0x0Fu;
constexpr std::uint8_t kMovssStoreOpcode = 0x11u;
constexpr std::uint8_t kModrmXmm0Esi = 0x86u;   // /r 0x86: xmm0, [esi+disp32]
constexpr std::uint8_t kModrmXmm1Esi = 0x8Eu;   // /r 0x8E: xmm1, [esi+disp32]
constexpr std::uint8_t kFstpSt0Bytes[2] = {0xDD, 0xD8};  // fstp st(0)
constexpr std::uint8_t kMovEbxHead[2] = {0x8B, 0x5C};    // mov ebx,[esp+..] head
// The borderless arm's GWL_STYLE mask: the recovered 0xFF30FFFF clears the
// framed-window bits (caption, system menu, thick frame, minimise/maximise).
constexpr std::uint32_t kBorderlessStyleMask =
    ~static_cast<std::uint32_t>(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                                WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
static_assert(kBorderlessStyleMask == 0xFF30FFFFu);
// opaque: model hashes the original handlers look up by hash (the game's
// name->hash result); provenance: ZolikaPatch_original.asi sub_100469A0 /
// sub_10044XXX, IDA 2026-10-31; why: the input strings are not in this repo.
constexpr std::uint32_t kSuperLodLookupHash = 0xAE4B15D6u;
constexpr std::uint32_t kSuperLodFallbackHash = 0xC703DB5Fu;
constexpr std::uint32_t kPopupLightsHash = 0xEB9F21D3u;  // popuplights script
constexpr std::uint32_t kNewFilesRegistrarKey = 0x2C89A1F9u;

// The DLC discovery scratch is the original's `::operator new` block.
struct OperatorDeleteDeleter final
{
	void operator()(void* pointer) const noexcept { ::operator delete(pointer); }
};
// ---------------------------------------------------------------------------
// Rebased globals: the live globals the hook bodies address by name.
// ---------------------------------------------------------------------------

// C linkage so the naked LoadDLCs body can address it from inline asm.
extern "C"
{
	std::uint32_t zpatch_dlc_load_callback = 0;              // dword_1015BDDC
	std::uint32_t zpatch_skip_intro_match = 0;               // dword_1015AD74
	std::uint32_t zpatch_skip_menu_resume = 0;               // dword_10159348
	std::uint8_t zpatch_skip_menu_flag = 0;                  // byte_101A8450
	std::uint32_t zpatch_blackscreen_resume = 0;             // dword_1015AD58
	std::uint32_t zpatch_blackscreen_value_ptr = 0;          // dword_1015AD5C
	std::uint32_t zpatch_path_node_resume_ok = 0;            // dword_1015BDFC
	std::uint32_t zpatch_path_node_resume_fail = 0;          // dword_1015BE00
	std::uint32_t zpatch_plane_crash_resume_a = 0;           // dword_1015BE18
	std::uint32_t zpatch_plane_crash_resume_b = 0;           // dword_1015BE14
	std::uint32_t zpatch_wanted_level_resume = 0;            // dword_1015AD6C
	std::uint32_t zpatch_group_hack_resume = 0;              // dword_1015AD60
	std::uint32_t zpatch_smpa60_resume_a = 0;                // dword_10159C70
	std::uint32_t zpatch_smpa60_resume_b = 0;                // dword_10159C68
	std::uint32_t zpatch_smpa60_resume_c = 0;                // dword_10159C6C (tail shim)
	std::uint32_t zpatch_freeze_gun_resume = 0;              // dword_1015AD64
	std::uint32_t zpatch_player_ped_object = 0;              // dword_10159D28
	std::uint32_t zpatch_player_ped_computed = 0;            // dword_10159D20
	std::uint32_t zpatch_player_ped_resume = 0;              // dword_10159D2C
	std::uint32_t zpatch_model_lookup = 0;                   // dword_10159C0C (shared)
	std::uint32_t zpatch_emissive_ecx = 0;                   // dword_10159CC0
	std::uint32_t zpatch_emissive_edx = 0;                   // dword_10159CCC
	std::uint32_t zpatch_emissive_const = 0;                 // dword_10159CC4
	std::uint32_t zpatch_emissive_resume = 0;                // dword_10159CC8
	std::uint32_t zpatch_carparts_resume_if = 0;             // dword_1015BDC8 (match+2)
	std::uint32_t zpatch_carparts_resume_else = 0;           // dword_1015BDC0 (match+0x45)
	std::uint32_t zpatch_super_lod_fail = 0;                 // dword_101580AC
	std::uint32_t zpatch_super_lod_ok = 0;                   // dword_101580B0
	std::uint32_t zpatch_super_lod_lookup = 0;               // dword_101580B4
	std::uint32_t zpatch_episode_switch_resume_a = 0;        // dword_10159BBC
	std::uint32_t zpatch_episode_switch_resume_b = 0;        // dword_10159BB4
	std::uint32_t zpatch_episode_switch_resume_c = 0;        // dword_10159BB8
	std::uint32_t zpatch_episode_flag = 2;                   // unk_10158128 (int 2)
	float zpatch_bike_value = 0.0f;                          // dword_1015AD7C
	std::uint32_t zpatch_bike_saved = 0;                     // dword_1015AD80
	std::uint32_t zpatch_bike_resume = 0;                    // dword_1015AD84
	std::uint32_t zpatch_collision_deadlock_should_walk = 0; // dword_1015BDD4
	std::uint32_t zpatch_collision_deadlock_iterations = 0;  // dword_1015BDD8
	std::uint32_t zpatch_vram_arg_a = 0;                     // dword_1015D630
	std::uint32_t zpatch_vram_arg_b = 0;                     // dword_1015D634
	std::uint8_t zpatch_vram_flag = 0;                       // byte_1015D638
	std::uint32_t zpatch_driveby_resolver = 0;               // dword_1015BDE0
	std::uint32_t zpatch_driveby_resume = 0;                 // dword_1015BDF4
	// dword_101580C4 (45.0f, original on-disk)
	float zpatch_driveby_fov = kDrivebyFov;
	std::uint32_t zpatch_driveby_cached = 0;           // dword_1015BDF8
	std::uint8_t zpatch_driveby_done = 0;              // byte_1015AD4A
	std::uint32_t zpatch_nolivery_name = 0;            // dword_10159BA8 (char*)
	std::uint32_t zpatch_nolivery_index = 0;           // dword_10159BB0
	std::uint32_t zpatch_nolivery_hash = 0;            // dword_10159B98
	std::uint32_t zpatch_nolivery_resume = 0;          // dword_10159B9C
	std::uint32_t zpatch_nolivery_fn = 0;              // dword_10159BA4
	std::uint32_t zpatch_nolivery_resume_d = 0;        // dword_10159BAC
	std::uint32_t zpatch_nolivery_arg = 0;             // dword_10159B94
	std::uint32_t zpatch_nolivery_count = 0;           // dword_10159B90
	std::uint32_t zpatch_nolivery_bufptr = 0;          // dword_10159BA0 (char*)
	std::uint32_t zpatch_nolivery_dest = 0;            // dword_10159B8C (char*)
	std::uint32_t zpatch_crash_msg_getter = 0;         // dword_1015AD68
	std::uint32_t zpatch_newfiles_register_slot = 0;   // dword_10159C08
	std::uint32_t zpatch_benchmark_resume_a = 0;       // dword_1015AD50
	std::uint32_t zpatch_benchmark_resume_b = 0;       // dword_1015AD54
	std::uint32_t zpatch_bds_hash_a = 0;               // dword_10159C1C
	std::uint32_t zpatch_bds_hash_b = 0;               // dword_10159C24
	std::uint32_t zpatch_bds_resume_a = 0;             // dword_10159C20
	std::uint32_t zpatch_bds_resume_b = 0;             // dword_10159C28
	std::uint32_t zpatch_copsirens_resume = 0;         // dword_1015937C
	std::uint32_t zpatch_copsirens_resume_b = 0;       // dword_10159374
	std::uint32_t zpatch_copsirens_index = 0;          // dword_10159370
	std::uint32_t zpatch_copsirens_index_b = 0;        // dword_1015936C
	std::uint32_t zpatch_copsirens_flag = 0;           // dword_10159B80
	std::uint32_t zpatch_copsirens_flag_b = 0;         // dword_10159378
	std::uint32_t zpatch_ide_string = 0;               // dword_1015BE08
	std::uint32_t zpatch_ide_model = 0;                // dword_1015BE0C
	std::uint32_t zpatch_ide_hook_slot = 0;            // dword_1015BE10
	std::uint32_t zpatch_copsirens_models[512] = {};   // dword_10159380
	std::uint32_t zpatch_popuplights_models[512] = {}; // dword_1015CE28
	std::uint32_t zpatch_wheelangle_models[512] = {};  // dword_1015A538
	std::uint32_t zpatch_wheelangle_angles[512] = {};  // dword_10159D38
	std::uint8_t zpatch_borderless_w_done = 0;         // byte_101A8445
	std::uint8_t zpatch_borderless_a_done = 0;         // byte_101A8444
	float* zpatch_speedup_t4c = nullptr;              // dword_1015934C
	float* zpatch_speedup_t50 = nullptr;              // dword_10159350
	float* zpatch_speedup_t54 = nullptr;              // dword_10159354
	float* zpatch_speedup_t58 = nullptr;              // dword_10159358
	float* zpatch_speedup_t5c = nullptr;              // dword_1015935C
	float* zpatch_speedup_t60 = nullptr;              // dword_10159360
	float* zpatch_speedup_t64 = nullptr;              // dword_10159364
	float* zpatch_speedup_t68 = nullptr;              // dword_10159368
	// OverrideGTADatPath: the original pushed its own "common:/data/gta.dat" global;
	// we push this buffer instead (filled from OverrideGTADatPathTo at load).
	char zpatch_override_gta_dat_path[260] = "common:/data/gta.dat";
	// PopupHeadlightsFlag (see rebased.h). dword_101580CC is pre-set to pi/2 on
	// disk and rewritten by the shim's checker on every call.
	std::uint32_t zpatch_popup_model = 0;
	std::uint32_t zpatch_popup_flag = 0;
	std::uint32_t zpatch_popup_index = 0;
	float zpatch_popup_angle = kPiOver2;
	std::uint32_t zpatch_popup_resume_a = 0;
	std::uint32_t zpatch_popup_resume_b = 0;
	std::uint32_t zpatch_popup_hashes[512] = {};
	float zpatch_popup_angles[512] = {};
	// ForceCarHeadlightShadows: dword_101580C0 is pre-set to 2.0f on disk.
	std::uint32_t zpatch_fchs_param = 0;
	std::uint32_t zpatch_fchs_resume = 0;
	float zpatch_fchs_offset = kFchsOffset;
	std::uint32_t zpatch_fchs_hashes[512] = {};
	float zpatch_fchs_offsets[512] = {};
	// BadPlayerModelCrashFix scratch.
	std::uint32_t zpatch_bpm_obj = 0;
	std::uint32_t zpatch_bpm_model_index = 0;
	std::uint32_t zpatch_bpm_resume = 0;
	// NewSavesCompatibility state.
	std::uint32_t zpatch_new_saves_src = 0;
	std::uint32_t zpatch_new_saves_resume = 0;
	std::uint32_t zpatch_new_saves_chunk_len = 0;
	std::uint8_t zpatch_new_saves_force_new_read = 0;
	std::uint8_t zpatch_new_saves_extra_block = 0;
	std::uint8_t zpatch_new_saves_new_chunk16 = 0;
	// Loader-preamble resolutions (unconditional in the original).
	std::uint32_t zpatch_pre_ped_array = 0;
	std::uint32_t zpatch_pre_fn_59c74 = 0;
	std::uint32_t zpatch_pre_fn_59b88 = 0;
	std::uint32_t zpatch_pre_fn_5d63c = 0;
	std::uint32_t zpatch_pre_fn_59cdc = 0;
	// Hash salts: opaque 32-bit fingerprints read by the scripting layer, not
	// float bit patterns. The loader rewrites them only when its build probe
	// misses (the fallback table in dllmain.cpp).
	std::uint32_t zpatch_salt_28 = 0x6A6B4F18u;
	std::uint32_t zpatch_salt_3c = 0x022B2DA9u;
	std::uint32_t zpatch_salt_40 = 0x4BF36A32u;
	std::uint32_t zpatch_salt_50 = 0x3B0C6738u;
	std::uint32_t zpatch_salt_54 = 0x511454A9u;
	std::uint32_t zpatch_salt_5c = 0x62E319C6u;
	std::uint32_t zpatch_salt_a8 = 0x79A95BF9u;
	std::uint32_t zpatch_salt_b8 = 0x4E61480Au;
	std::uint32_t zpatch_salt_c8 = 0x570F5725u;
	std::uint32_t zpatch_salt_d4 = 0x75E005F1u;
	// Module-presence flags (set by the loader, exactly like the original).
	std::uint8_t zpatch_xliveless_present = 0;
	std::uint8_t zpatch_citizen_present = 0;
	std::uint32_t zpatch_xlive_base = 0;
	// Original one-time scan caches and the shared ASI TlsIndex.
	std::uint32_t zpatch_gfwl_scan_guard = 0;
	std::uint32_t zpatch_gfwl_site = 0;
	std::uint32_t zpatch_speedup_scan_guard = 0;
	std::uint32_t zpatch_speedup_clock = 0;
	std::uint32_t zpatch_tls_index = 0;
	// Live option-flag globals. Every one starts at 0: the loader arms only raise
	// a flag to 1, so an option that is off leaves it at 0.
	std::uint8_t zpatch_flag_death_music = 0u;      // byte_10159B86
	std::uint8_t zpatch_flag_patch4_modchecks = 0u; // byte_10159C14
	std::uint8_t zpatch_flag_gfwl_popup = 0u;       // byte_10159B87
	std::uint8_t zpatch_flag_debug_spawns = 0u;     // byte_10159C15
	std::uint8_t zpatch_flag_donotpause = 0u;       // byte_1015AD43
	// RestoreDeathMusic's lazily resolved site.
	std::uint32_t zpatch_death_music_site = 0;
	// NewSavesCompatibility's element counter.
	std::uint32_t zpatch_new_saves_element_count = 0;
	std::uint32_t zpatch_new_saves_loaddata = 0;
	std::uint32_t zpatch_new_saves_p3_original = 0;
	std::uint32_t zpatch_gfwl_resume = 0;
	std::uint32_t zpatch_carparts_shadow_fn = 0; // dword_1015B5B8
	std::uint32_t zpatch_carparts_resume_2 = 0;  // dword_1015BDCC
	// DualVehicleHeadlights.
	std::uint32_t zpatch_dual_bcc = 0;      // dword_10159BCC
	std::uint32_t zpatch_dual_bd0 = 0;      // dword_10159BD0
	std::uint32_t zpatch_dual_bd4 = 0;      // dword_10159BD4
	std::uint32_t zpatch_dual_be0 = 0;      // dword_10159BE0
	std::uint32_t zpatch_dual_be4 = 0;      // dword_10159BE4
	std::uint32_t zpatch_dual_light_a = 1u; // dword_1015802C (original on-disk 1)
	std::uint32_t zpatch_dual_light_b = 2u; // dword_10158030 (original on-disk 2)
	std::uint32_t zpatch_dual_r_eax = 0;    // dword_10159BC4
	std::uint32_t zpatch_dual_r_ebx = 0;    // dword_10159BEC
	std::uint32_t zpatch_dual_r_ecx = 0;    // dword_10159BD8
	std::uint32_t zpatch_dual_r_edx = 0;    // dword_10159BF0
	std::uint32_t zpatch_dual_r_esi = 0;    // dword_10159BC8
	std::uint32_t zpatch_dual_r_edi = 0;    // dword_10159BE8
	std::uint32_t zpatch_dual_r_ebp = 0;    // dword_10159BDC
	std::uint32_t zpatch_dual_r_esp = 0;    // dword_10159BF4
	// LocalSettingsPath.
	char zpatch_local_ansi_cwd[260] = {};
	char zpatch_local_iv_suffix[8] = "\\GTA IV";
	int(__stdcall* zpatch_local_saves_slot)(std::uint32_t, std::uint32_t,
	                                         std::uint32_t, std::uint32_t,
	                                         char*) = nullptr;
	char* zpatch_local_path_08 = nullptr;
	wchar_t* zpatch_local_path_10 = nullptr;
	const int* zpatch_local_episode_ptr = nullptr;
	char* zpatch_local_path_18 = nullptr;
	wchar_t* zpatch_local_path_1c = nullptr;
	// FreezeCarFix / DebugPrintSpawns.
	std::uint32_t zpatch_spawns_kind = std::numeric_limits<std::uint32_t>::max();
	std::uint32_t zpatch_spawns_class = 0;
	std::uint32_t zpatch_spawns_skip = 0;
	std::uint32_t zpatch_spawns_orig_a = 0;
	std::uint32_t zpatch_spawns_orig_b = 0;
	std::uint32_t zpatch_spawns_case0_resume = 0;
	std::uint32_t zpatch_spawns_case1_resume = 0;
	std::uint32_t zpatch_spawns_case2_resume = 0;
	std::uint32_t zpatch_spawns_case3_resume = 0;
	std::uint32_t zpatch_spawns_case4_resume = 0;
	std::uint32_t zpatch_spawns_case5_resume = 0;
	std::uint32_t zpatch_spawns_case6_resume = 0;
	std::uint32_t zpatch_spawns_case7_resume = 0;
	std::uint32_t zpatch_spawns_case8_resume = 0;
	std::uint32_t zpatch_spawns_case9_resume = 0;
	char zpatch_fmt_spawned_car[] = "%s spawned a car (%s)";
	char zpatch_fmt_nonspawned_car[] = "%s spawned a non-car car (%s)";
	char zpatch_fmt_spawned_heli[] = "%s spawned a heli (%s)";
	char zpatch_fmt_nonspawned_heli[] = "%s spawned a non-heli heli (%s)";
	char zpatch_fmt_spawned_boat[] = "%s spawned a boat (%s)";
	char zpatch_fmt_nonspawned_boat[] = "%s spawned a non-boat boat (%s)";
	char zpatch_fmt_spawned_bike[] = "%s spawned a bike (%s)";
	char zpatch_fmt_nonspawned_bike[] = "%s spawned a non-bike bike (%s)";
	char zpatch_fmt_spawned_train[] = "%s spawned a train (%s)";
	char zpatch_fmt_nonspawned_train[] = "%s spawned a non-train train (%s)";
	char zpatch_fmt_spawned_plane[] = "%s spawned a plane (%s)";
	char zpatch_fmt_nonspawned_plane[] = "%s spawned a non-plane plane (%s)";
	char zpatch_fmt_spawned_ped[] = "%s spawned a ped (%s)";
	char zpatch_fmt_nonspawned_ped[] = "%s spawned a non-ped ped (%s)";
	char zpatch_fmt_spawned_vehicle_object[] = "%s spawned a vehicle object (%s)";
	char zpatch_fmt_spawned_object[] = "%s spawned an object (%s)";
	char zpatch_fmt_spawned_generic[] = "%s spawned (%s)";
	char zpatch_fmt_used_nonped[] = "%s used a non-ped model (%s)";
	wchar_t zpatch_str_gtac[] = L"GTAC.dll";
	// The spawn probe's GetModuleHandleW call goes through this slot (MSVC's inline
	// assembler cannot name the @-decorated import pointer); the hook bodies fill it at load.
	std::uint32_t zpatch_spawns_getmodulehandlew = 0;
	std::uint32_t zpatch_garage_resume_a = 0;
	std::uint32_t zpatch_garage_resume_b = 0;
	std::uint32_t zpatch_steer_vehicle = 0;
	std::uint32_t zpatch_steer_resume = 0;
	std::uint32_t zpatch_steer_vtable_fn = 0;
	float zpatch_engine_last_timestep = 0.0f;
	float zpatch_engine_phase = 0.0f;
	std::uint8_t zpatch_engine_descending = 0;
	std::uint8_t zpatch_flag_steering = 0u; // byte_1015AD40 (on-disk 0)
	std::uint8_t zpatch_flag_engine = 0u;   // byte_1015AD41 (on-disk 0)
	std::uint8_t zpatch_flag_plane = 0u;    // byte_10159C17 (on-disk 0)
	// MouseFix (see the hook bodies). The constants are not const so the
	// inline assembler can address them as plain external symbols; nothing writes
	// them. Values are the original .rdata bits (hex floats keep them exact).
	int* zpatch_mf_x = nullptr;
	int* zpatch_mf_y = nullptr;
	std::uint8_t* zpatch_mf_flag = nullptr;
	int (*zpatch_mf_time)() = nullptr;
	std::uint8_t* zpatch_mf_resume_a = nullptr;
	std::uint8_t* zpatch_mf_resume_b = nullptr;
	float zpatch_mf_xmm_save[8] = {};
	double zpatch_mf_scale = 0.0078125;
	double zpatch_mf_scale_255 = 255.0;
	float zpatch_mf_sign = 127.5f;
	float zpatch_mf_mul_neg1 = -1.0f;
	float zpatch_mf_step_scale = 0.1f;
	double zpatch_mf_mul_neg1_d = -1.0;
	double zpatch_mf_time_mul = 0x1.333334015bc21p-4;
	double zpatch_mf_time_add = 0.025;
	double zpatch_mf_pi = 0x1.921fb5a7ed197p+1;
	double zpatch_mf_two_pi = 0x1.921fb6134ce3ep+2;
	double zpatch_mf_neg_pi = -0x1.921fb5a7ed197p+1;
	// `xorps xmm, xmmword ptr [mask]` needs a 16-byte aligned address: the
	// original's .rdata constants are naturally aligned, so the masks are
	// alignas(16) (a misaligned mask faults with #GP/AV).
	alignas(16) unsigned char zpatch_mf_neg_mask[16] = {0, 0, 0, 0x80, 0, 0, 0, 0x80,
	                                                    0, 0, 0, 0x80, 0, 0, 0, 0x80};
	alignas(16) unsigned char zpatch_mf_double_sign[16] = {0, 0, 0, 0, 0, 0, 0, 0x80,
	                                                       0, 0, 0, 0, 0, 0, 0, 0x80};
	// OutOfCommissionFix (see the hook bodies).
	std::uint8_t* zpatch_ooc_resume = nullptr;
	void (*zpatch_ooc_logger)(char*, int, int) = nullptr;
	std::uint32_t (*zpatch_ooc_orig)(std::uint32_t, std::uint32_t) = nullptr;
	std::uint32_t zpatch_ooc_state = 0;
	std::uint32_t zpatch_ooc_output_begin = 0;
	std::uint32_t zpatch_ooc_output_cursor = 0;
	std::uint32_t zpatch_ooc_output_end = 0;
	std::uint32_t zpatch_ooc_archive_offset = 0;
	std::uint32_t zpatch_ooc_vector_begin = 0;
	std::uint32_t zpatch_ooc_vector_end = 0;
	std::uint32_t zpatch_ooc_vector_capacity = 0;
	char zpatch_ooc_fmt_a[] = "mor";
	char zpatch_ooc_fmt_b[] = "me%sy%c%c%p,%d,%c%c%s";
	// HighQualityReflections (see the hook bodies).
	std::uint32_t zpatch_hqr_resume = 0;
	std::uint32_t zpatch_hqr_builder = 0;
	std::uint32_t zpatch_hqr_dump[8] = {};
	std::uint32_t zpatch_hqr_flags = 0x11Du; // the original's dword_10158060
	std::uint32_t zpatch_hqr_mode = 0;       // the original's BSS slot stays 0
	// CutsceneFixes chain B (see the hook bodies): the worker's two publishes, the body's
	// frame-time accumulator (the original's .data BSS, past the raw data, so 0) and
	// the read-only constants it compares against (original .rdata/.data bits).
	double (*zpatch_cutscene_target)(void*, int) = nullptr;  // dword_1015BDE4
	std::uint8_t* zpatch_cutscene_poke = nullptr;            // dword_1015BDF0
	double zpatch_cutscene_accum = 0.0;                      // qword_1015BDE8 (BSS: starts 0)
	double zpatch_cutscene_zero = kCutsceneZero;
	double zpatch_cutscene_limit = kCutsceneLimit;
	std::uint32_t zpatch_cutscene_abs_mask = kFloatAbsMask;  // 0x101523D0
	float zpatch_cutscene_eps_a = kCutsceneEps;
	float zpatch_cutscene_eps_b = kCutsceneSpeedLimit;
	// NewMemorySystem: the allocator singleton the handler's second scan publishes
	// (the immediate of `mov esi, 0x19cf9f0`); the E8 body writes our vtable there.
	std::uint32_t zpatch_nms_object = 0; // dword_10159CD8
}
namespace
{
// Rebased originals the handlers address directly: StartupTimeFix pushes the
// address of g_startup_time (the original pushed dword_101580d8).
std::uint32_t g_startup_time = 0;
std::uint32_t g_dlc_register_rva = 0;
std::uint32_t g_dlc_apply_rva = 0;
// VSyncFix: the game code reads/writes this dword through the operand the
// handler patches (the original pushed &unk_10159BC0).
std::uint32_t g_vsync_flag = 0;
} // namespace

// Copies `path` into the buffer the retargeted push reads, clamped and terminated.
void SetOverrideGtaDatPath(std::string_view path) noexcept
{
	if (path.empty())
		path = "common:/data/gta.dat";
	const std::size_t n = path.size() < sizeof(zpatch_override_gta_dat_path) - 1
	    ? path.size()
	    : sizeof(zpatch_override_gta_dat_path) - 1;
	std::memcpy(zpatch_override_gta_dat_path, path.data(), n);
	zpatch_override_gta_dat_path[n] = '\0';
}

// Accessors for the rebased globals whose address a handler publishes.
std::uint32_t& StartupTimeGlobal() noexcept
{
	return g_startup_time;
}
std::uint32_t& DlcRegisterRva() noexcept
{
	return g_dlc_register_rva;
}
std::uint32_t& DlcApplyRva() noexcept
{
	return g_dlc_apply_rva;
}

// Rebases a value that addresses the original module, keyed by INI option.
std::uintptr_t FindGlobalValue(std::string_view option) noexcept
{
	if (option == "StartupTimeFix")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&g_startup_time));
	if (option == "VSyncFix")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&g_vsync_flag));
	if (option == "OverrideGTADatPath")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(zpatch_override_gta_dat_path));
	// PopupHeadlightsFlag: the `fld` operand at +82 now loads our pi/2 float.
	if (option == "PopupHeadlightsFlag")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&zpatch_popup_angle));
	// ForceCarHeadlightShadows: the operand at +211 now loads our 2.0f dword.
	if (option == "ForceCarHeadlightShadows")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&zpatch_fchs_offset));
	// LocalSettingsPath: the path-helper call goes through our 4-byte slot.
	if (option == "LocalSettingsPath")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&zpatch_local_saves_slot));
	return 0;
}

// Named plugin-data symbols the sig-install `plugin_data_ptr` kind addresses
// (a 4-byte value written at the site: the ADDRESS of the named global).
std::uintptr_t FindGlobalSymbol(std::string_view symbol) noexcept
{
	if (symbol == "zpatch_local_iv_suffix")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(zpatch_local_iv_suffix));
	if (symbol == "zpatch_episode_flag")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&zpatch_episode_flag));
	return 0;
}

// The original globals whose addresses a handler publishes, keyed by the
// original name: the value is the rebuilt module global or accessor that
// replaces it. A name without a row has no counterpart, so a publish through
// it is skipped. Kept as data because the table is provenance:
// tools/verify_bodies.py reads the same rows to cross-check the operand
// bindings it learns from the disassembly.
struct GlobalSlot final
{
	std::string_view name;  // the original global (dword_/byte_/Src)
	void* address;          // its counterpart in this module
};

constexpr GlobalSlot kGlobalSlots[] = {
	// LoadDLCs: the callback slot receives the game's loader address; the
	// two RVAs are the register/apply values the discovery routine adds to the
	// module base at use time.
	{"dword_1015BDDC", &zpatch_dlc_load_callback},
	{"dword_101580D0", &g_dlc_apply_rva},
	{"dword_1015804C", &g_dlc_register_rva},
	{"dword_1015AD74", &zpatch_skip_intro_match},
	{"dword_10159348", &zpatch_skip_menu_resume},
	{"byte_101A8450", &zpatch_skip_menu_flag},
	{"dword_1015AD58", &zpatch_blackscreen_resume},
	{"dword_1015AD5C", &zpatch_blackscreen_value_ptr},
	{"dword_1015BDFC", &zpatch_path_node_resume_ok},
	{"dword_1015BE00", &zpatch_path_node_resume_fail},
	{"dword_1015BE18", &zpatch_plane_crash_resume_a},
	{"dword_1015BE14", &zpatch_plane_crash_resume_b},
	{"dword_1015AD6C", &zpatch_wanted_level_resume},
	{"dword_1015AD60", &zpatch_group_hack_resume},
	{"dword_10159C70", &zpatch_smpa60_resume_a},
	{"dword_10159C68", &zpatch_smpa60_resume_b},
	{"dword_10159C6C", &zpatch_smpa60_resume_c},
	{"dword_1015AD64", &zpatch_freeze_gun_resume},
	{"dword_10159D28", &zpatch_player_ped_object},
	{"dword_10159D20", &zpatch_player_ped_computed},
	{"dword_10159D2C", &zpatch_player_ped_resume},
	{"dword_10159C0C", &zpatch_model_lookup},
	{"dword_10159CC0", &zpatch_emissive_ecx},
	{"dword_10159CCC", &zpatch_emissive_edx},
	{"dword_10159CC4", &zpatch_emissive_const},
	{"dword_10159CC8", &zpatch_emissive_resume},
	{"dword_1015BDC8", &zpatch_carparts_resume_if},
	{"dword_1015BDC0", &zpatch_carparts_resume_else},
	{"dword_101580AC", &zpatch_super_lod_fail},
	{"dword_101580B0", &zpatch_super_lod_ok},
	{"dword_101580B4", &zpatch_super_lod_lookup},
	{"dword_10159BBC", &zpatch_episode_switch_resume_a},
	{"dword_10159BB4", &zpatch_episode_switch_resume_b},
	{"dword_10159BB8", &zpatch_episode_switch_resume_c},
	{"dword_1015AD7C", &zpatch_bike_value},
	{"dword_1015AD80", &zpatch_bike_saved},
	{"dword_1015AD84", &zpatch_bike_resume},
	{"dword_1015BDD4", &zpatch_collision_deadlock_should_walk},
	{"dword_1015BDE0", &zpatch_driveby_resolver},
	{"dword_1015BDF4", &zpatch_driveby_resume},
	{"dword_1015BDE4", &zpatch_cutscene_target},
	{"dword_1015BDF0", &zpatch_cutscene_poke},
	{"dword_10159CD8", &zpatch_nms_object},
	{"dword_10159B9C", &zpatch_nolivery_resume},
	{"dword_10159BA4", &zpatch_nolivery_fn},
	{"dword_10159BAC", &zpatch_nolivery_resume_d},
	{"dword_1015AD68", &zpatch_crash_msg_getter},
	{"dword_10159C08", &zpatch_newfiles_register_slot},
	{"dword_1015AD50", &zpatch_benchmark_resume_a},
	{"dword_1015AD54", &zpatch_benchmark_resume_b},
	{"dword_10159C1C", &zpatch_bds_hash_a},
	{"dword_10159C24", &zpatch_bds_hash_b},
	{"dword_10159C20", &zpatch_bds_resume_a},
	{"dword_10159C28", &zpatch_bds_resume_b},
	{"dword_1015937C", &zpatch_copsirens_resume},
	{"dword_10159374", &zpatch_copsirens_resume_b},
	{"dword_1015BE10", &zpatch_ide_hook_slot},
	{"dword_1015934C", &zpatch_speedup_t4c},
	{"dword_10159350", &zpatch_speedup_t50},
	{"dword_10159354", &zpatch_speedup_t54},
	{"dword_10159358", &zpatch_speedup_t58},
	{"dword_1015935C", &zpatch_speedup_t5c},
	{"dword_10159360", &zpatch_speedup_t60},
	{"dword_10159364", &zpatch_speedup_t64},
	{"dword_10159368", &zpatch_speedup_t68},
	// PopupHeadlightsFlag resumes (match+12 / match+10).
	{"dword_1015D628", &zpatch_popup_resume_a},
	{"dword_1015D62C", &zpatch_popup_resume_b},
	// ForceCarHeadlightShadows resume (match+12 of its second pattern).
	{"dword_1015B5BC", &zpatch_fchs_resume},
	// BadPlayerModelCrashFix resume (match+10).
	{"dword_10159CB8", &zpatch_bpm_resume},
	// NewSavesCompatibility: Src is captured from the game push immediate;
	// dword_1015AD88 is the tramp resume.
	{"Src", &zpatch_new_saves_src},
	{"dword_1015AD88", &zpatch_new_saves_resume},
	{"dword_10159C18", &zpatch_gfwl_resume},
	{"dword_1015B5B8", &zpatch_carparts_shadow_fn},
	{"dword_1015BDCC", &zpatch_carparts_resume_2},
	// DualVehicleHeadlights's five published game pointers.
	{"dword_10159BCC", &zpatch_dual_bcc},
	{"dword_10159BD0", &zpatch_dual_bd0},
	{"dword_10159BD4", &zpatch_dual_bd4},
	{"dword_10159BE0", &zpatch_dual_be0},
	{"dword_10159BE4", &zpatch_dual_be4},
	// LocalSettingsPath's five game path pointers.
	{"dword_10159D08", &zpatch_local_path_08},
	{"dword_10159D10", &zpatch_local_path_10},
	{"dword_10159D14", &zpatch_local_episode_ptr},
	{"dword_10159D18", &zpatch_local_path_18},
	{"dword_10159D1C", &zpatch_local_path_1c},
	// FreezeCarFix / DebugPrintSpawns resume slots.
	{"dword_10159CB0", &zpatch_spawns_orig_a},
	{"dword_10159C98", &zpatch_spawns_orig_b},
	{"dword_10159CBC", &zpatch_spawns_case0_resume},
	{"dword_10159C9C", &zpatch_spawns_case1_resume},
	{"dword_10159CA0", &zpatch_spawns_case2_resume},
	{"dword_10159CA8", &zpatch_spawns_case3_resume},
	{"dword_10159C7C", &zpatch_spawns_case4_resume},
	{"dword_10159C88", &zpatch_spawns_case5_resume},
	{"dword_10159CAC", &zpatch_spawns_case6_resume},
	{"dword_10159C90", &zpatch_spawns_case7_resume},
	{"dword_10159C94", &zpatch_spawns_case8_resume},
	{"dword_10159C84", &zpatch_spawns_case9_resume},
	// GarageModelFix resumes.
	{"dword_10159CD4", &zpatch_garage_resume_a},
	{"dword_10159CD0", &zpatch_garage_resume_b},
	// Trio resumes / call target.
	{"dword_1015AD44", &zpatch_steer_resume},
	{"dword_1015AD4C", &zpatch_steer_vtable_fn},
	// MouseFix publishes (the callback's slot writes).
	{"dword_10159C50", &zpatch_mf_x},
	{"dword_10159C3C", &zpatch_mf_y},
	{"dword_10159C5C", &zpatch_mf_flag},
	{"dword_10159C34", &zpatch_mf_time},
	{"dword_10159C60", &zpatch_mf_resume_a},
	{"dword_10159C40", &zpatch_mf_resume_b},
	// OutOfCommissionFix publishes.
	{"dword_10158034", &zpatch_ooc_resume},
	{"dword_10159BF8", &zpatch_ooc_logger},
	{"dword_10159C04", &zpatch_ooc_orig},
	// HighQualityReflections publishes.
	{"dword_10159CFC", &zpatch_hqr_resume},
	{"dword_10159D04", &zpatch_hqr_builder},
};

std::uintptr_t FindGlobalSlot(std::string_view slot_name) noexcept
{
	for (const GlobalSlot& slot : kGlobalSlots)
	{
		if (slot_name == slot.name)
			return reinterpret_cast<std::uintptr_t>(slot.address);
	}
	return 0;  // no counterpart: a publish through this slot is skipped
}

// ---------------------------------------------------------------------------
// Hook bodies: the game jumps or calls straight into these.
// ---------------------------------------------------------------------------

#if defined(_WIN32)

// Game structure offsets used by the helpers below; the inline asm keeps the
// numeric operands because MASM needs compile-time constants.
constexpr std::size_t kModelIndexOffset = 0x2E;
// CPed's class id: the original compares the vtable result against 6.
constexpr char kPedModelClassId = 6;
// CPed's model hash (BadPlayerModelCrashFix's fallback name lookup).
constexpr std::uint32_t kPedModelHashOffset = 0x3Cu;
// The bodies below are 32-bit assembly. The x64 write-set dump never runs
// them - it only needs their addresses, which tests/stub_bodies.cpp provides -
// so the whole assembly region is compiled out for it.
#if !defined(ZP_DUMP_TOOL)


extern "C"
{

// PlayerAsPedComponentsFix helper (original sub_1003D930): compare the object's
// model (+0x2E) against the "player" model index. The shim stores the object
// (EAX) first; C linkage so the shim's asm can call it by name.
static void zpatch_cookie_tail() noexcept;  // defined with the cookie shim

std::int32_t abi_player_ped_helper() noexcept
{
	std::int32_t index = -1;
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	// 0x1003D960 `call dword_10159C0C` is unguarded, so a null lookup faults.
	zpatch_player_ped_computed = 0;  // original writes 0 before the lookup
	lookup(GameHash("player"), &index);
	const auto* object = reinterpret_cast<const std::uint8_t*>(
	    static_cast<std::uintptr_t>(zpatch_player_ped_object));
	const std::int16_t model = *reinterpret_cast<const std::int16_t*>(object + kModelIndexOffset);
	zpatch_player_ped_computed = (index == model) ? 1u : 0u;
	// Tail contract: EDX=1 (the cmove source), ECX=cookie via the /GS check,
	// EAX=the object's model (the movsx eax,[eax+2Eh] the resume sees).
	__asm { mov edx, 1 }
	zpatch_cookie_tail();
	return model;
}

// EmissiveLerpFix has no compiled helpers: the original tramp (sub_1001AF20)
// is integer math plus a cvtsi2ss/mulss tail that must touch only xmm0 (the
// game keeps xmm1..xmm7 live across the site).

// SuperLODFix helper (original sub_100469A0): resolve 0xAE4B15D6 through the
// handler-published lookup (dword_101580B4) and compare the object's model
// (+0x2E). The object is the stack argument the shim pushes.
bool abi_super_lod_helper(const void* object) noexcept
{
	std::int32_t index = -1;
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_super_lod_lookup));
	lookup(kSuperLodLookupHash, &index);  // sub_100469A0's call is unguarded
	const auto* o = static_cast<const std::uint8_t*>(object);
	return *reinterpret_cast<const std::int16_t*>(o + kModelIndexOffset) == index;
}

// HighFPSBikePhysicsFix helper (original sub_1001EF30): clamp zpatch_bike_value
// below 0.006666666665 to 0x3BDA740E. SSE in the original.
void abi_bike_physics_helper() noexcept
{
	const float value = zpatch_bike_value;
	if (0.006666666665 > static_cast<double>(value))
		zpatch_bike_value = kBikeSpeedThreshold;
}

// PopupHeadlightsFlag checker (original sub_10044310), called between
// pushad/popad: look up 0xEB9F21D3 ("popuplights"), walk the +popuplights list,
// then copy the last matching [PopupLightAngles] angle (starts at pi/2). A
// failed lookup leaves -1, which matches a model of -1, as in the original.
void abi_popup_check() noexcept
{
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	zpatch_popup_angle = kPiOver2;  // dword_101580CC: pi/2
	zpatch_popup_flag = 0;             // dword_1015BE24
	std::int32_t out = -1;
	lookup(kPopupLightsHash, &out);  // sub_10044310's call is unguarded
	zpatch_popup_index = static_cast<std::uint32_t>(out);  // dword_1015BE1C
	if (out == static_cast<std::int32_t>(zpatch_popup_model))
	{
		zpatch_popup_flag = 1;
		return;
	}
	for (std::uint32_t i = 0; i < kVehicleLightSlotCount; ++i)
	{
		const std::uint32_t hash = zpatch_popuplights_models[i];
		if (hash == 0) return;
		out = -1;
		lookup(hash, &out);
		if (out != static_cast<std::int32_t>(zpatch_popup_model)) continue;
		zpatch_popup_flag = 1;
		for (std::uint32_t k = 0; k < kVehicleLightSlotCount; ++k)
		{
			if (zpatch_popup_hashes[k] != 0 &&
			    zpatch_popup_hashes[k] == hash)
			{
				zpatch_popup_angle = zpatch_popup_angles[k];
			}
		}
		return;
	}
}

// ForceCarHeadlightShadows checker (original sub_1001AD20), called between
// pushad/popad: walk the [HeadlightConeOffsets] table (dword_1015ADB8) and
// publish the first matching offset (dword_1015B5C0) into dword_101580C0;
// default 2.0f. The lookup and the object's model (+0x2E) are unguarded.
void abi_fchs_check() noexcept
{
	const std::uint32_t* const hashes = zpatch_fchs_hashes;
	std::uint32_t hash = hashes[0];
	zpatch_fchs_offset = kFchsOffset;  // 2.0f
	if (hash == 0u) return;
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	// 0x1001AD5C `call dword_10159C0C` and 0x1001AD62 `movsx eax,word [v2+2Eh]`
	// are unguarded in the original.
	const auto* base = reinterpret_cast<const std::uint8_t*>(
	    static_cast<std::uintptr_t>(zpatch_fchs_param));
	for (std::uint32_t i = 0;;)
	{
		std::int32_t index = -1;
		lookup(hash, &index);
		if (static_cast<std::int32_t>(
		        *reinterpret_cast<const std::int16_t*>(base + kModelIndexOffset)) == index)
		{
			zpatch_fchs_offset = zpatch_fchs_offsets[i];
			return;
		}
		++i;
		// The original walks into adjacent .data instead; layout-specific and
		// unreproducible.
		if (i == kVehicleLightSlotCount) return;
		hash = hashes[i];
		if (hash == 0u) return;
	}
}

// The native-script call layer's resolver entry points: unmangled so the
// inline asm and the call layer can name them (src/body_helpers.h declares
// them). The layer itself is source/script_calls.cpp.
__declspec(naked) std::uint32_t zpatch_script_resolve(std::uint32_t)
{
	// Naked: EDX carries the caller's block, so push it explicitly and keep the
	// original's cleanup.
	__asm {
		push edx
		push dword ptr [esp + 8]
		call zpatch_script_resolve_seh
		add  esp, 8
		ret
	}
}
__declspec(naked) std::uint32_t zpatch_script_resolve_with_block(std::uint32_t,
                                                                  void*)
{
	// Naked: hash goes in ECX, block in EDX; ESI must survive the call.
	__asm {
		push esi
		mov  esi, dword ptr [esp + 8]
		mov  ecx, esi
		mov  edx, dword ptr [esp + 0Ch]
		mov  eax, DWORD PTR [zpatch_driveby_resolver]
		call eax
		pop  esi
		ret
	}
}


// The MP-only diagnostic name lookup of sub_10003F00 walks the original ASI's
// own 38455-entry model-name table (0x100DE770); single-player never reaches
// this path (the caller is gated on GTAC.dll).
extern "C" const char* zpatch_bpm_model_name(std::uint32_t hash) noexcept
{
	// Return the first table name whose GameHash matches; "**UNKNOWN**" on a miss.
	return zp::ModelNameFromHash(hash).data();
}

// FreezeCarFix / DebugPrintSpawns helper shims: the original's helpers take
// their single argument in ECX or EDX. The probe body calls these by name.
extern "C" const char* zpatch_spawns_name_ecx();  // sub_10003F00 twin, arg in ECX
__declspec(naked) const char* zpatch_spawns_name_ecx()
{
	__asm {
		push ecx
		call zpatch_bpm_model_name
		add  esp, 4
		ret
	}
}

extern "C" const char* zpatch_spawns_who_edx();  // sub_1005B290 twin, arg in EDX
__declspec(naked) const char* zpatch_spawns_who_edx()
{
	__asm {
		push edx
		call zpatch_spawns_who_c
		add  esp, 4
		ret
	}
}

extern "C" void zpatch_spawns_print_edx();  // sub_1005B530 twin, arg in EDX
__declspec(naked) void zpatch_spawns_print_edx()
{
	__asm {
		push edx
		call zpatch_spawns_print_c
		add  esp, 4
		ret
	}
}


// sub_1005FE7B: the /GS cookie check (cookie in ECX). It is the last call on
// the probe's return path, so it must preserve EAX (the model index); the
// original is `cmp ecx,[0x1015800C]; jne fail; ret`.
extern "C" void zpatch_spawns_cookie_check_ecx();
extern "C" std::uintptr_t __security_cookie;
__declspec(naked) void zpatch_spawns_cookie_check_ecx()
{
	__asm {
		cmp ecx, dword ptr [__security_cookie]
		jne spawns_cookie_fail
		ret
	spawns_cookie_fail:
		mov ecx, 2
		int 29h
	}
}

// The original helpers compiled with /GS end with
//   mov ecx,[ebp-N]; xor ecx,ebp; call <cookie check>; ...
// and the shim resumes *without* restoring EFLAGS (pushal/popal do not), so the
// resume sees the flags from `cmp ecx,[__security_cookie]`; keep that tail.
static void zpatch_cookie_tail() noexcept
{
	__asm {
		mov ecx, dword ptr [__security_cookie]
		call zpatch_spawns_cookie_check_ecx
	}
}

// BadPlayerModelCrashFix's check (original sub_10037AC0): if the model at
// zpatch_bpm_model_index is not a ped (class id != 6), fall back to
// "m_y_multiplayer". Called between pushad/popad, so GPR clobbering is fine.
extern "C" void abi_bpm_check() noexcept
{
	const auto* models = reinterpret_cast<void* const*>(
	    static_cast<std::uintptr_t>(zpatch_pre_ped_array));
	void* object = models[zpatch_bpm_model_index];
	if (object == nullptr) return;
	void** vtable = *reinterpret_cast<void***>(object);
	// sub_10037AC0 leaves ECX = the models-table base (dword_1015AD78) before
	// `call eax`; preserve that contract (odd as it looks).
	const char class_id = reinterpret_cast<char(__thiscall*)(void*)>(vtable[3])(
	    const_cast<void*>(reinterpret_cast<const void*>(models)));
	if (class_id == kPedModelClassId) return;
	if (GetModuleHandleW(L"GTAC.dll") != nullptr)
	{
		const std::uint32_t modelHash =
		    *reinterpret_cast<const std::uint32_t*>(
		        static_cast<const std::uint8_t*>(object) + kPedModelHashOffset);
		const char* model_name = zpatch_bpm_model_name(modelHash);
		std::uint32_t entity = zpatch_bpm_obj;
		const char* who = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(
		    zpatch_script_call(zpatch_salt_c8, 1, &entity)));
		std::array<char, kSpawnsNameCapacity> buffer;
		std::snprintf(buffer.data(), buffer.size(), "%s used a non-ped model (%s)", who,
		              model_name);
		const std::uint32_t text = static_cast<std::uint32_t>(
		    reinterpret_cast<std::uintptr_t>(buffer.data()));
		zpatch_script_call(kScriptCallHash, 1, &text);
	}
	const std::uint32_t hash = GameHash("m_y_multiplayer");
	if (zpatch_script_call_bool(zpatch_salt_b8, 1, &hash) == 0)
	{
		reinterpret_cast<void(__cdecl*)(std::uint32_t, std::uint32_t)>(
		    static_cast<std::uintptr_t>(zpatch_pre_fn_59c74))(hash, 0);
		zpatch_script_call(zpatch_salt_40, 0, nullptr);
	}
	std::int32_t new_index = -1;
	reinterpret_cast<void(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup))(hash, &new_index);
	zpatch_bpm_model_index = static_cast<std::uint32_t>(new_index);
}

// RestoreDeathMusic (original sub_10050A50): the death-music gate, gated on
// byte_10159B86 and answered through the shipped hash-salt script calls.
extern "C" std::uint8_t abi_death_music_enabled() noexcept
{
	if (!zpatch_flag_death_music) return 0;
	std::uint32_t queried = 0;
	const std::uint32_t query_args[2] = {
	    zpatch_script_call(zpatch_salt_5c, 0, nullptr),
	    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&queried))};
	zpatch_script_call(zpatch_salt_54, 2, query_args);
	if (zpatch_script_call_bool(zpatch_salt_28, 1, &queried) != 0) return 1;
	if (zpatch_script_call_bool(zpatch_salt_a8, 0, nullptr) != 0 &&
	    zpatch_script_call_bool(zpatch_salt_50, 0, nullptr) != 0)
		return 1;
	return 0;
}

// RestoreDeathMusic (original sub_10050AD0): resolve "6A 06 68 ? ? ? ? 8D 8C"
// once and randomize the push immediate to a track 1..5 (rand() % 5 + 1).
extern "C" char abi_death_music_install() noexcept
{
	if (!abi_death_music_enabled()) return 0;
	if (zpatch_death_music_site == 0)
	{
		const auto* module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleA(nullptr));
		if (module)
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
			if (dos->e_magic == IMAGE_DOS_SIGNATURE)
			{
				const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
				    module + dos->e_lfanew);
				if (nt->Signature == IMAGE_NT_SIGNATURE)
				{
					Match match;
					const ModuleView view{module, nt->OptionalHeader.SizeOfImage};
					if (FindPattern(view, "6A 06 68 ? ? ? ? 8D 8C", match))
						zpatch_death_music_site = static_cast<std::uint32_t>(
						    reinterpret_cast<std::uintptr_t>(match.address));
				}
			}
		}
	}
	if (zpatch_death_music_site != 0)
	{
		auto* site = reinterpret_cast<std::uint8_t*>(
		    static_cast<std::uintptr_t>(zpatch_death_music_site) + 1);
		injector::WriteMemory<std::uint8_t>(  // push imm8
		    site, static_cast<std::uint8_t>(std::rand() % 5 + 1), true);
	}
	return 1;
}

// The running image's first match; null when the module header or the pattern
// is missing. The bodies share this one scanner (src/body_helpers.h declares
// it).
const std::uint8_t* scan_game(std::string_view signature) noexcept
{
	const auto* module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleA(nullptr));
	if (!module) return nullptr;
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
	Match match;
	const ModuleView view{module, nt->OptionalHeader.SizeOfImage};
	if (!FindPattern(view, signature, match)) return nullptr;
	return match.address;
}

// The original's path/name helpers call the secure CRT (`strcpy_s`/`wcscpy_s`)
// through its IAT; the volatile pointer keeps a distinct secure-copy call.
namespace
{
using ZpStrcpyS = int(__cdecl*)(char*, std::size_t, const char*);
volatile ZpStrcpyS zpatch_secure_strcpy_s =
    static_cast<ZpStrcpyS>(&strcpy_s);
}  // namespace

// The AddOptionsInMenu integration's four callbacks (originals sub_10050A30/40
// and sub_1001EDB0/C0). The focus-loss setter re-asserts the game branch
// sub_1001ECB0 patches, using the pre-patch word saved by
// zpatch_menu_prime_focus_loss.
namespace
{
std::uint8_t* g_focus_loss_site = nullptr;
std::uint16_t g_focus_loss_original = 0;
}  // namespace

extern "C" void zpatch_menu_prime_focus_loss() noexcept
{
	if (g_focus_loss_site != nullptr) return;
	const std::uint8_t* found =
	    scan_game("75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15");
	if (found == nullptr) return;
	g_focus_loss_site = const_cast<std::uint8_t*>(
	    reinterpret_cast<const std::uint8_t*>(found));
	std::memcpy(&g_focus_loss_original, g_focus_loss_site, 2);
}

extern "C" std::uint8_t abi_menu_death_get() noexcept
{
	return zpatch_flag_death_music;
}
extern "C" void abi_menu_death_set(std::uint32_t, std::uint32_t value) noexcept
{
	zpatch_flag_death_music = value != 0 ? 1u : 0u;
}
extern "C" std::uint8_t abi_menu_focus_get() noexcept
{
	return !zpatch_flag_donotpause;
}
extern "C" void abi_menu_focus_set(std::uint32_t, std::uint32_t value) noexcept
{
	zpatch_flag_donotpause = value == 0 ? 1u : 0u;
	// No null check: a failed prime scan leaves the saved site null and the
	// original's 2-byte write faults at 0.
	if (zpatch_flag_donotpause != 0)
		injector::MakeNOP(injector::raw_ptr(g_focus_loss_site), 2, true); //nop
	else
		wp_word(g_focus_loss_site, g_focus_loss_original);
}

// The 0x104-byte path buffer the original's LocalSettingsPath helpers use.
constexpr std::size_t kLocalPathCapacity = 0x104u;

// LocalSettingsPath: the CWD captured at load, the "\GTA IV" folder suffix the
// game's path helper is retargeted to push, the four-byte call slot the game
// calls through (original off_10158064 = sub_10051320), and the five path
// pointers the detour rewrites.
extern "C" int __stdcall zpatch_local_copy_saves(std::uint32_t, std::uint32_t,
                                                 std::uint32_t, std::uint32_t,
                                                 char* dest) noexcept
{
	if (dest != nullptr)
		std::snprintf(dest, kLocalPathCapacity, "%s\\saves", zpatch_local_ansi_cwd);
	return 0;
}

// Captures the CWD and fills the call slot the retargeted game call goes
// through (original off_10158064 = sub_10051320).
extern "C" void zpatch_local_settings_capture_cwd() noexcept
{
	GetCurrentDirectoryA(kLocalPathCapacity, zpatch_local_ansi_cwd);
	zpatch_local_saves_slot = &zpatch_local_copy_saves;
}

// The detours' helper (original sub_10050D10): rewrites the game's four
// just-computed settings/save path buffers to "<...>\saves\<episode>\" (the
// first ANSI destination is hardcoded "GTA IV"). Called between pushad/popad.
namespace
{
constexpr std::string_view kSavesPrefix = "\\saves\\";
constexpr std::wstring_view kSavesPrefixWide = L"\\saves\\";
// The two local name buffers: "\saves\GTA IV\" fits the 16-byte one, the
// episode names ("TBoGT") the 32-byte ones.
constexpr std::size_t kIvPathNameCapacity = 16;
constexpr std::size_t kEpisodeNameCapacity = 32;

void zpatch_build_ansi_path(std::span<char> out, std::string_view name) noexcept
{
	std::size_t o = 0;
	for (const char c : kSavesPrefix) out[o++] = c;
	for (const char c : name) out[o++] = c;
	out[o++] = '\\';
	out[o] = '\0';
}
void zpatch_build_wide_path(std::span<wchar_t> out, std::wstring_view name) noexcept
{
	std::size_t o = 0;
	for (const wchar_t c : kSavesPrefixWide) out[o++] = c;
	for (const wchar_t c : name) out[o++] = c;
	out[o++] = L'\\';
	out[o] = 0;
}
// The original copies with strcpy_s/wcscpy_s (cap 0x200); these write the same
// bytes without the static CRT's secure-copy path.
void zpatch_copy_path(std::span<char> dst, std::string_view src) noexcept
{
	std::size_t n = 0;
	while (n + 1 < dst.size() && src[n] != '\0')
	{
		dst[n] = src[n];
		++n;
	}
	dst[n] = '\0';
}
void zpatch_copy_wpath(std::span<wchar_t> dst, std::wstring_view src) noexcept
{
	std::size_t n = 0;
	while (n + 1 < dst.size() && src[n] != 0)
	{
		dst[n] = src[n];
		++n;
	}
	dst[n] = 0;
}
}  // namespace

// The cert cap the original's strcpy_s/wcscpy_s path copies use.
constexpr std::size_t kSettingsPathCapacity = 0x200u;

extern "C" void zpatch_local_settings_step() noexcept
{
	const int episode = zpatch_local_episode_ptr ? *zpatch_local_episode_ptr : 0;
	const char* ansi_name = "GTA IV";
	const wchar_t* wide_name = L"GTA IV";
	if (episode == 1)
	{
		ansi_name = "TLaD";
		wide_name = L"TLaD";
	}
	else if (episode == 2)
	{
		ansi_name = "TBoGT";
		wide_name = L"TBoGT";
	}
	std::array<char, kIvPathNameCapacity> ansi_iv;
	std::array<char, kEpisodeNameCapacity> ansi_episode;
	std::array<wchar_t, kEpisodeNameCapacity> wide_episode;
	// sub_10050D10 copies the literal "\saves\GTA IV\" into ansi_iv, independent
	// of the episode.
	zpatch_build_ansi_path(ansi_iv, "GTA IV");
	zpatch_build_ansi_path(ansi_episode, ansi_name);
	zpatch_build_wide_path(wide_episode, wide_name);
	if (zpatch_local_path_18)
		zpatch_copy_path({zpatch_local_path_18, kSettingsPathCapacity}, ansi_iv.data());
	if (zpatch_local_path_08)
		zpatch_copy_path({zpatch_local_path_08, kSettingsPathCapacity}, ansi_episode.data());
	if (zpatch_local_path_1c)
		zpatch_copy_wpath({zpatch_local_path_1c, kSettingsPathCapacity}, wide_episode.data());
	if (zpatch_local_path_10)
		zpatch_copy_wpath({zpatch_local_path_10, kSettingsPathCapacity}, wide_episode.data());
	zpatch_cookie_tail();
}

// GarageModelFix helpers (originals sub_10046B90 / sub_10046BD0): stash the
// vehicle's model hash across four scattered bytes of the garage object
// (skipped in MP), then rebuild it and repair out-of-range garage models.
// The originals take ECX=object, EDX=vehicle; naked wrappers forward to these.
namespace
{
// The recovered garage layout: the hash bytes are the original's four scattered
// stores, the vehicle's model is at +0x2E, its ped entry's hash at +0x3C, and
// the rebuilt object carries the index at +0x10 and the flag word at +0x30.
constexpr std::size_t kGarageHashByte0 = 64;   // object+0x40
constexpr std::size_t kGarageHashByte1 = 19;   // object+0x13
constexpr std::size_t kGarageHashByte2 = 65;   // object+0x41
constexpr std::size_t kGarageHashByte3 = 70;   // object+0x46
constexpr std::size_t kGarageVehicleModelOffset = 46;  // vehicle+0x2E
constexpr std::size_t kGaragePedHashOffset = 60;       // ped entry+0x3C
constexpr std::size_t kGarageIndexOffset = 16;         // object+0x10
constexpr std::size_t kGarageFlagsOffset = 48;         // object+0x30
constexpr std::int32_t kGarageModelLimit = 32000;
constexpr std::int32_t kGarageInvalidModel = 65534;    // 0xFFFE write-back
void zpatch_garage_store_c(std::uint8_t* object, const std::uint8_t* vehicle) noexcept
{
	if (zpatch_citizen_present) return;
	const std::int16_t model =
	    *reinterpret_cast<const std::int16_t*>(vehicle + kGarageVehicleModelOffset);
	const auto base = static_cast<std::uintptr_t>(zpatch_pre_ped_array);
	const std::uint32_t entry = *reinterpret_cast<const std::uint32_t*>(
	    base + sizeof(std::uint32_t) * static_cast<std::uint32_t>(model));
	const std::uint32_t hash = *reinterpret_cast<const std::uint32_t*>(
	    reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(entry)) +
	    kGaragePedHashOffset);
	object[kGarageHashByte0] = static_cast<std::uint8_t>(hash >> 8);
	object[kGarageHashByte1] = static_cast<std::uint8_t>(hash);
	object[kGarageHashByte2] = static_cast<std::uint8_t>(hash >> 16);
	object[kGarageHashByte3] = static_cast<std::uint8_t>(hash >> 24);
}
std::int32_t zpatch_garage_lookup_c(std::uint8_t* object) noexcept
{
	std::uint32_t hash = object[kGarageHashByte1];
	hash |= (static_cast<std::uint32_t>(object[kGarageHashByte0]) |
	         ((static_cast<std::uint32_t>(object[kGarageHashByte2]) |
	           (static_cast<std::uint32_t>(object[kGarageHashByte3]) << 8))
	          << 8))
	        << 8;
	const std::int32_t original = static_cast<std::int32_t>(object[kGarageHashByte1]);
	if (hash == 0u) return original;
	// 0x10046C10 / 0x10046C4A call dword_10159C0C unguarded.
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	std::int32_t out = -1;
	lookup(hash, &out);
	if (out <= 0 || out >= kGarageModelLimit)
	{
		out = -1;
		lookup(kSuperLodFallbackHash, &out);
		*reinterpret_cast<std::int16_t*>(object + kGarageIndexOffset) =
		    static_cast<std::int16_t>(out);
		*reinterpret_cast<std::uint16_t*>(object + kGarageFlagsOffset) &= kClearedLowBit;
		return kGarageInvalidModel;
	}
	*reinterpret_cast<std::int16_t*>(object + kGarageIndexOffset) = static_cast<std::int16_t>(out);
	return out;
}
}  // namespace
extern "C" void zpatch_garage_store_model();
__declspec(naked) void zpatch_garage_store_model()
{
	__asm {
		cmp  BYTE PTR [zpatch_citizen_present], 0
		push esi
		mov  esi, ecx
		jne  garage_done
		movsx edx, WORD PTR [edx + 0x2e]
		mov  eax, DWORD PTR [zpatch_pre_ped_array]
		mov  eax, DWORD PTR [eax + edx * 4]
		mov  eax, DWORD PTR [eax + 0x3c]
		mov  ecx, eax
		shr  ecx, 8
		mov  BYTE PTR [esi + 0x40], cl
		mov  ecx, eax
		shr  ecx, 0x10
		mov  BYTE PTR [esi + 0x13], al
		shr  eax, 0x18
		mov  BYTE PTR [esi + 0x41], cl
		mov  BYTE PTR [esi + 0x46], al
	garage_done:
		pop  esi
		ret
	}
}
extern "C" std::int32_t zpatch_garage_lookup_model();
__declspec(naked) std::int32_t zpatch_garage_lookup_model()
{
	__asm {
		push ecx
		call zpatch_garage_lookup_c
		add  esp, 4
		push eax
		mov ecx, dword ptr [__security_cookie]
		call zpatch_spawns_cookie_check_ecx
		pop eax
		ret
	}
}

// MovingSteeringWheelSupport / ShakingEngineBlock / MovingPlaneParts' shared
// consumer (original sub_10004C70) and its model-registry lookup live in
// source/vehicle_steer.cpp; the two detours below call it by name.

// The shared flag-gated helper of the Patch4RemoveModChecks /
// RemoveGFWLUpdatePopup handler (original sub_10005340): with Patch4 on, zero
// the dword the matched game code points at (match+30); with the GFWL popup
// removal on, store 0x10000E83 over the four xlive.dll update-window flag slots.
// Called between pushad/popad from abi_gfwl_stub.
namespace
{
// The original guards its one-time pattern scans with the MSVC thread-safe
// static protocol. Its epoch (*(TLS[_tls_index]+4)) is loader state that stays
// non-negative when the ASI is mapped without CRT TLS init, which made the
// scan skip; this keeps the same one-time semantics with a tri-state guard
// (0 = uninitialised, -1 = initialising, 1 = done).
bool zpatch_tls_static_header(std::uint32_t& guard) noexcept
{
	for (;;)
	{
		const LONG observed = InterlockedCompareExchange(
		    reinterpret_cast<volatile LONG*>(&guard), -1, 0);
		if (observed == 0) return true;
		if (observed == -1)
		{
			Sleep(1);  // another thread owns the one-time scan
			continue;
		}
		return false;
	}
}

void zpatch_tls_static_footer(std::uint32_t& guard) noexcept
{
	InterlockedExchange(reinterpret_cast<volatile LONG*>(&guard), 1);
}
}  // namespace

extern "C" void abi_gfwl_patch() noexcept
{
	if (zpatch_flag_patch4_modchecks)
	{
		if (zpatch_tls_static_header(zpatch_gfwl_scan_guard))
		{
			// _Init_thread_header claimed the guard; scan then publish the epoch.
			const std::uint8_t* found =
			    scan_game("33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57");
			zpatch_gfwl_site = found == nullptr
			                               ? 0u
			                               : static_cast<std::uint32_t>(
			                                     reinterpret_cast<std::uintptr_t>(found));
			zpatch_tls_static_footer(zpatch_gfwl_scan_guard);
		}
		if (zpatch_gfwl_site != 0)
		{
			std::uint32_t target = 0;
			std::memcpy(&target,
			            reinterpret_cast<const void*>(
			                static_cast<std::uintptr_t>(zpatch_gfwl_site) + 30u),
			            4);
			// 0x10005427 writes *[site+30] without a null check.
			wp_dword(reinterpret_cast<void*>(target), 0);
		}
	}
	if (zpatch_flag_gfwl_popup)
	{
		// 0x10005421 writes xlive base + 0x66F770.. with no base check; with
		// xlive.dll absent the base is 0 and the original faults on that page.
		// The offsets were decimal in the original's constants (6748016 /
		// 6748020 / 7034320 / 7034352); they are one dword apart at 0x66F770 and
		// two more slots at 0x6B55D0/0x6B55F0.
		constexpr std::uintptr_t kXliveUpdateFlagA = 0x66F770u;
		constexpr std::uintptr_t kXliveUpdateFlagB = kXliveUpdateFlagA + sizeof(std::uint32_t);
		constexpr std::uintptr_t kXliveUpdateFlagC = 0x6B55D0u;
		constexpr std::uintptr_t kXliveUpdateFlagD = 0x6B55F0u;
		constexpr std::uint32_t kXliveUpdateFlagValue = 0x10000E83u;  // documented opaque
		static_assert(kXliveUpdateFlagA == 6748016u);
		static_assert(kXliveUpdateFlagB == 6748020u);
		static_assert(kXliveUpdateFlagC == 7034320u);
		static_assert(kXliveUpdateFlagD == 7034352u);
		const auto base = static_cast<std::uintptr_t>(zpatch_xlive_base);
		wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlagA), kXliveUpdateFlagValue);
		wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlagB), kXliveUpdateFlagValue);
		wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlagC), kXliveUpdateFlagValue);
		wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlagD), kXliveUpdateFlagValue);
	}
}

}  // extern "C"

// The spawn probe's GetModuleHandleW call goes through our own slot (MSVC's
// inline assembler cannot name the @-decorated import pointer).
struct SpawnsGetModuleHandleInit final
{
	SpawnsGetModuleHandleInit() noexcept
	{
		zpatch_spawns_getmodulehandlew = static_cast<std::uint32_t>(
		    reinterpret_cast<std::uintptr_t>(&GetModuleHandleW));
	}
};
SpawnsGetModuleHandleInit g_spawns_getmodulehandle_init;

// CutsceneFixes chain B's VirtualProtect slot: the body loads this slot instead
// of the @-decorated import pointer, which the inline assembler cannot name
// (same value).
std::uint32_t zpatch_cutscene_virtualprotect = 0;
struct CutsceneVirtualProtectInit final
{
	CutsceneVirtualProtectInit() noexcept
	{
		zpatch_cutscene_virtualprotect = static_cast<std::uint32_t>(
		    reinterpret_cast<std::uintptr_t>(&VirtualProtect));
	}
};
CutsceneVirtualProtectInit g_cutscene_virtualprotect_init;

// The two game functions the DLC discovery routine calls, published as RVAs by
// the LoadDLCs handler (dword_1015804C register, dword_101580D0 apply).
using DlcRegisterFn = void(__stdcall*)(int id, char* request, int flag);
using DlcApplyFn = void(__thiscall*)(void* object, char* request);

// @exact 0x10005590 AimProjectileCrashFix: __thiscall (this in ECX), three
// stack arguments, callee pops 12 bytes; MSVC rejects __thiscall on free
// functions, so the original instruction sequence is reproduced directly.
__declspec(naked) void abi_aim_projectile_crash_fix()
{
	__asm {
		cmp ecx, 0
		je skip
		mov eax, [esp + 8]
		mov [ecx + 0x20], eax
		mov eax, [esp + 0xc]
		mov [ecx + 0x24], eax
		neg eax
		sbb eax, eax
		and eax, [esp + 4]
		mov [ecx + 0x1c], eax
	skip:
		ret 12
	}
}

// @exact 0x100057f0 BetterMPSync: xor eax,eax / ret.
int __cdecl abi_better_mp_sync() { return 0; }

// @exact 0x10005ed0 CombinedRadioFreezeFix: mov eax,13h / ret.
int __cdecl abi_combined_radio_freeze_fix() { return 19; }

// @orig 0x10003e50 AllowHashesForIDE: the game's model-name hash function, replaced so
// that "hash:<number>" literals are accepted (see GameHash). The original is
// __cdecl(char*) -> unsigned int; this wrapper keeps that ABI.
unsigned int __cdecl abi_game_hash(const char* text)
{
	return GameHash(text ? std::string_view{text} : std::string_view{});
}

// @orig 0x10050870 PedModelsInCutscenesFix: replaces the game's "does this object use
// the player model" check (the original calls a game helper and compares +0x2E).
// The original is __thiscall(this) -> bool in al; __fastcall puts `this` in ECX
// and returns bool in AL the same way.
bool __fastcall abi_ped_models_in_cutscenes_check(const void* self, void*)
{
	std::int32_t index = -1;
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	// `call dword_10159C0C` is unguarded in the original.
	lookup(GameHash("player"), &index);
	const auto* o = static_cast<const std::uint8_t*>(self);
	return *reinterpret_cast<const std::int16_t*>(o + kModelIndexOffset) == index;
}

// 0x10045a30 / 0x10045a80 ImprovedShaderStreaming: replacements for the two
// "find shader entry" functions. Originals are __thiscall(this, query) -> entry
// or 0 with one callee-popped argument, so __fastcall(self, edx, query)
// reproduces the ECX=this / stack-arg / ret 4 contract: walk the vector at
// this+8 (count at this+0xC) and return the first non-null entry whose
// vtable[+0x10]() name passes the filter (sub_100459E0: name[4]=='h' or
// name[4]=='p' && name[5]=='e'; sub_10045A10: name[4]=='v').
using ShaderEntryNameFn = const void*(__fastcall*)(void* entry, int);
using ShaderStreamFilterFn = int(__cdecl*)(const void* query, const void* name);
int __cdecl abi_shader_stream_filter_hpe(const void*, const void* name)
{
	const auto* p = static_cast<const unsigned char*>(name);
	if (!p) return 2;
	if (p[4] == 'h') return 0;
	if (p[4] == 'p' && p[5] == 'e') return 0;
	return 2;
}
int __cdecl abi_shader_stream_filter_v(const void*, const void* name)
{
	const auto* p = static_cast<const unsigned char*>(name);
	return (p && p[4] == 'v') ? 0 : 2;
}
namespace
{
void* shader_streaming_find(void* self, const void* query, ShaderStreamFilterFn filter)
{
	auto* base = static_cast<unsigned char*>(self);
	const unsigned count = *reinterpret_cast<const unsigned short*>(base + 0xc);
	auto** array = *reinterpret_cast<void***>(base + 8);
	for (unsigned i = 0; i < count; ++i)
	{
		void* entry = array[i];
		if (!entry) continue;
		auto* vt = *reinterpret_cast<void* const* const*>(entry);
		const auto* name = reinterpret_cast<ShaderEntryNameFn>(vt[0x10 / sizeof(void*)])(entry, 0);
		if (filter(query, name) == 0) return entry;
	}
	return nullptr;
}
}  // namespace
void* __fastcall abi_improved_shader_streaming_find_hpe(void* self, int, const void* query)
{
	return shader_streaming_find(self, query, &abi_shader_stream_filter_hpe);
}
void* __fastcall abi_improved_shader_streaming_find_v(void* self, int, const void* query)
{
	return shader_streaming_find(self, query, &abi_shader_stream_filter_v);
}

// @exact 0x10023480 LoadDLCs: __stdcall, two arguments, mov al,1 / ret 8. The
// original sets only AL, so the upper 24 bits of EAX are preserved and a plain
// `return 1` (mov eax,1) is observable.
__declspec(naked) int __stdcall abi_load_dlcs_dummy(int, int)
{
	__asm {
		mov al, 1
		ret 8
	}
}

// @exact 0x10038550 RemoveRGSCLoginRequirement: no arguments, mov al,1 / ret.
__declspec(naked) int __cdecl abi_rgsc_login_removed()
{
	__asm {
		mov al, 1
		ret
	}
}

// @exact 0x10038570 SavegameFix: xor eax,eax / ret 8 (__stdcall, two arguments).
int __stdcall abi_savegame_fix_null(int, int) { return 0; }

// 0x10045580 SavegameFix close: push [ebp+8] / call CloseHandle / xor eax,eax /
// ret 4 (__stdcall, one argument).
int __stdcall abi_savegame_fix_close(void* handle)
{
	if (handle) CloseHandle(handle);
	return 0;
}

// 0x100455e0 SavegameFix mutex: if (!out) return 1;
// *out = CreateMutexA(0,0,0); return 0; __stdcall, two arguments.
int __stdcall abi_savegame_fix_mutex(void*, void** out_handle)
{
	if (!out_handle) return 1;
	*out_handle = CreateMutexA(nullptr, FALSE, nullptr);
	return 0;
}

// Crash-fix shims, transcribed from the original bodies' machine code: the C
// translation hid argument pushes, register setup and flags the resumed code
// consumes. Registers the resumed code depends on (AL, DL, flags) must survive
// intact; each shim ends in a tail jump to the published resume.

// @exact 0x1003d7b0 PathNodeCrashFix: eax = node index; the sentinel is
// 0x1FFFDF. Otherwise dl = [eax+0x1F] and its low bit feed the failure resume.
__declspec(naked) void abi_path_node_shim()
{
	__asm {
		cmp eax, 0x1FFFDF
		je ok
		mov dl, BYTE PTR [eax + 0x1F]
		test dl, 1
		jmp DWORD PTR [zpatch_path_node_resume_fail]
	ok:
		jmp DWORD PTR [zpatch_path_node_resume_ok]
	}
}

// @exact 0x1003d870 PlaneCrashFix: eax = object; null takes the null resume.
// The normal path stores bl at +0x26 and loads eax from [[esi]+0xF4].
__declspec(naked) void abi_plane_crash_shim()
{
	__asm {
		cmp eax, 0
		je null_path
		mov BYTE PTR [eax + 0x26], bl
		mov edx, DWORD PTR [esi]
		mov eax, DWORD PTR [edx + 0xF4]
		jmp DWORD PTR [zpatch_plane_crash_resume_b]
	null_path:
		jmp DWORD PTR [zpatch_plane_crash_resume_a]
	}
}

// @exact 0x10050490 WantedLevelCrashFix: ecx = object; null returns 0. The
// normal path calls vtable+0x128; the resume consumes al and the following
// test's flags.
__declspec(naked) void abi_wanted_level_shim()
{
	__asm {
		cmp ecx, 0
		je null_path
		mov eax, DWORD PTR [ecx]
		mov edx, DWORD PTR [eax + 0x128]
		call edx
		test al, al
		jmp DWORD PTR [zpatch_wanted_level_resume]
	null_path:
		pop ebx
		ret
	}
}

// @exact 0x10036e90 GroupHackFix: eax = 0, ecx += 4, edx = 7, then the resume
// tests the flags of [ecx] against 0.
__declspec(naked) void abi_group_hack_shim()
{
	__asm {
		xor eax, eax
		add ecx, 4
		lea edx, [eax + 7]
		cmp ecx, 0xC
		je null_path
		cmp DWORD PTR [ecx], 0
		jmp DWORD PTR [zpatch_group_hack_resume]
	null_path:
		ret
	}
}

// @exact 0x10046750 SMPA60Fix: ecx = this; the sentinel 0xDDDDDDDD tails into
// the match+28 resume. The normal path pushes five arguments for the match+14
// resume before tailing into it.
__declspec(naked) void abi_smpa60_shim()
{
	__asm {
		cmp ecx, 0xDDDDDDDD
		je sentinel
		mov ecx, DWORD PTR [eax + 0x70]
		add eax, 0x40
		push eax
		push ecx
		lea edx, [ecx + 0x10]
		push edx
		push ecx
		push ecx
		jmp DWORD PTR [zpatch_smpa60_resume_b]
	sentinel:
		jmp DWORD PTR [zpatch_smpa60_resume_a]
	}
}

// @exact 0x10046780 SMPA60Fix tail shim: if [eax+0x14] is a valid pointer (not
// null, not the 0xCDCDCDCD heap marker) copy [eax+0x10] into [ptr+0x10], then
// tail-jump to the resume the handler publishes (match+136, dword_10159C6C).
__declspec(naked) void abi_smpa60_tail_shim()
{
	__asm {
		mov ecx, DWORD PTR [eax + 0x14]
		test ecx, ecx
		je skip
		cmp ecx, 0xCDCDCDCD
		je skip
		mov edx, DWORD PTR [eax + 0x10]
		mov DWORD PTR [ecx + 0x10], edx
	skip:
		jmp DWORD PTR [zpatch_smpa60_resume_c]
	}
}

// @exact 0x10036eb0 FreezeGunFix: eax = object; if [eax+0x40] is a valid handle
// (not 0xCDCDCDCD, not -1, not 0) push edi and tail-jump into the original
// path, else return 0. The null path pops esi and does ret 8 because the site
// sits just after the enclosing function's push esi.
__declspec(naked) void abi_freeze_gun_shim()
{
	__asm {
		mov ecx, DWORD PTR [eax + 0x38]
		test ecx, ecx
		mov esi, DWORD PTR [eax + 0x40]
		cmp esi, 0xCDCDCDCD
		je freeze_gun_null
		cmp esi, -1
		je freeze_gun_null
		cmp esi, 0
		je freeze_gun_null
		push edi
		jmp DWORD PTR [zpatch_freeze_gun_resume]
	freeze_gun_null:
		xor eax, eax
		pop esi
		ret 8
	}
}

// @exact 0x1003d9a0 PlayerAsPedComponentsFix: save eax, run the helper (stores
// 1/0 in zpatch_player_ped_computed), restore, then push the result and ecx and
// tail-jump to the resume (match+14).
__declspec(naked) void abi_player_ped_components_shim()
{
	__asm {
		pushad
		mov DWORD PTR [zpatch_player_ped_object], eax
		call abi_player_ped_helper
		popad
		push DWORD PTR [zpatch_player_ped_computed]
		push ecx
		jmp DWORD PTR [zpatch_player_ped_resume]
	}
}

// @orig 0x1001af60 EmissiveLerpFix: register-exact transcription of the original
// tramp. It saves ecx/edx, runs sub_1001AF20 (inlined: the integer state
// machine over dword_10159CC0/CCC), then does the SSE tail and tail-jumps to
// the resume (match-12, game 0x7F8BAD).
//
// The tail must stay bit-exact.
//   cvtsi2ss xmm0, edx           merges, keeping xmm0[127:32] from the caller
//   mulss    xmm0, [0x10159cc4]  multiplies by the slot's BYTES (match-16),
//                                not a dereference
//   xmm1..xmm7 untouched         the game keeps them live across the site
// MSVC inline asm cannot spell cvtsi2ss/mulss, so the four-byte encodings are
// emitted. Not byte-exact (the helper is inlined).
__declspec(naked) void abi_emissive_lerp_shim()
{
	__asm {
		push eax
		mov DWORD PTR [zpatch_emissive_ecx], ecx
		mov DWORD PTR [zpatch_emissive_edx], edx
		// --- inlined sub_1001AF20 (0x1001af20) ---
		mov ecx, DWORD PTR [zpatch_emissive_ecx]
		xor edx, edx
		mov eax, DWORD PTR [zpatch_emissive_edx]
		shl ecx, 4
		sub ecx, DWORD PTR [zpatch_emissive_ecx]
		mov DWORD PTR [zpatch_emissive_edx], 0
		lea eax, [eax + ecx*4]
		mov ecx, 0Ch
		add eax, -405
		cmp eax, 2EDh
		__emit 0Fh
		__emit 46h
		__emit 0D1h                  // cmovbe edx, ecx
		mov DWORD PTR [zpatch_emissive_ecx], edx
		// --- reload ecx/edx exactly like the original tramp ---
		mov ecx, DWORD PTR [zpatch_emissive_ecx]
		mov edx, DWORD PTR [zpatch_emissive_edx]
		__emit 0F3h
		__emit 0Fh
		__emit 2Ah
		__emit 0C2h                  // cvtsi2ss xmm0, edx (merge upper bits)
		mov eax, OFFSET zpatch_emissive_const
		__emit 0F3h
		__emit 0Fh
		__emit 59h
		__emit 00h                   // mulss xmm0, DWORD PTR [eax]
		pop eax
		jmp DWORD PTR [zpatch_emissive_resume]
	}
}

// @exact 0x100068A0 CarPartsShadowFix's other slot body: calls the published
// thunk [dword_1015B5B8] twice with (esi, 2|0, ebx, ebx, edi), pops the 20
// argument bytes and tails to [dword_1015BDCC]. The NOPs are original bytes.
__declspec(naked) void abi_carparts_shadow_2()
{
	__asm {
		push esi
		nop
		push 2
		nop
		push ebx
		nop
		push ebx
		nop
		push edi
		nop
		call dword ptr [zpatch_carparts_shadow_fn]
		push esi
		nop
		push 0
		nop
		push ebx
		nop
		push ebx
		nop
		push edi
		nop
		call dword ptr [zpatch_carparts_shadow_fn]
		nop
		add  esp, 14h
		nop
		jmp  dword ptr [zpatch_carparts_resume_2]
		__emit 0CCh   // body delimiter for verify_hook_bodies
	}
}

// @exact 0x100068D0 CarDynamicShadowFix body B (pattern-B variant, unused on
// 1.0.8.0 where pattern A is found): same double call through
// [dword_1015B5B8] with (esi, 2, 0, 0, edi) / (esi, 0, 0, 0, edi), 20 argument
// bytes popped, tail-jump to [dword_1015BDCC].
__declspec(naked) void abi_carparts_shadow_2b()
{
	__asm {
		push esi
		nop
		push 2
		nop
		push 0
		nop
		push 0
		nop
		push edi
		nop
		call dword ptr [zpatch_carparts_shadow_fn]
		nop
		push esi
		nop
		push 0
		nop
		push 0
		nop
		push 0
		nop
		push edi
		nop
		call dword ptr [zpatch_carparts_shadow_fn]
		nop
		add  esp, 14h
		nop
		jmp  dword ptr [zpatch_carparts_resume_2]
		__emit 0CCh   // body delimiter for verify_hook_bodies
	}
}

// @exact 0x10006880 CarPartsShadowFix shim (NOPs are original bytes).
//   cmp edi,0 / nop / je else / nop
//   movzx ebx,word [edi+1Ch] / nop / xor esi,esi / nop
//   test ebx,ebx / nop / jmp [dword_1015BDC8]   ; resume = match+2
// else: nop / jmp [dword_1015BDC0]              ; resume = match+0x45
// The EBX/ESI setup and its flags are consumed by the resumed code; edi and the
// rest of the register state must reach the resume untouched.
__declspec(naked) void abi_carparts_shadow()
{
	__asm {
		cmp  edi, 0
		nop
		je   carparts_else
		nop
		movzx ebx, word ptr [edi + 1Ch]
		nop
		xor  esi, esi
		nop
		test ebx, ebx
		nop
		jmp  dword ptr [zpatch_carparts_resume_if]
	carparts_else:
		nop
		jmp  dword ptr [zpatch_carparts_resume_else]
		__emit 0CCh   // body delimiter for verify_hook_bodies
	}
}

// @exact 0x10044530 PopupHeadlightsFlag (pattern "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1",
// live on 1.0.8.0 at game 0xA53A64).
//   movsx eax,word [esi+2Eh]; pushad; mov [dword_1015BE20],eax
//   call sub_10044310; popad; cmp [dword_1015BE24],1; je resume_a
//   cmp eax,[dword_1015BE1C]; jmp [dword_1015D62C]  ; resume B = site+0xA
// resume_a: jmp [dword_1015D628]                   ; resume A = site+0xC
// pushad/popad is essential: the helper may clobber every GPR, and the resumes
// consume ECX/EDX/ESI exactly as the site left them.
__declspec(naked) void abi_popup_headlights()
{
	__asm {
		movsx eax, word ptr [esi + 2Eh]
		pushad
		mov   dword ptr [zpatch_popup_model], eax
		call  abi_popup_check
		popad
		cmp   dword ptr [zpatch_popup_flag], 1
		je    popup_resume_a
		cmp   eax, dword ptr [zpatch_popup_index]
		jmp   dword ptr [zpatch_popup_resume_b]
	popup_resume_a:
		jmp   dword ptr [zpatch_popup_resume_a]
	}
}
// @exact 0x10044560 PopupHeadlightsFlag: the EDX twin for the second site
// pattern (absent on 1.0.8.0).
__declspec(naked) void abi_popup_headlights_edx()
{
	__asm {
		movsx edx, word ptr [esi + 2Eh]
		pushad
		mov   dword ptr [zpatch_popup_model], edx
		call  abi_popup_check
		popad
		cmp   dword ptr [zpatch_popup_flag], 1
		je    popup_edx_resume_a
		cmp   edx, dword ptr [zpatch_popup_index]
		jmp   dword ptr [zpatch_popup_resume_b]
	popup_edx_resume_a:
		jmp   dword ptr [zpatch_popup_resume_a]
	}
}

// @exact 0x1001ADB0 ForceCarHeadlightShadows thunk (installer E9s the game
// function at pattern "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B",
// game 0x8F7590, resume = match+12).
//   mov [dword_1015BDD0],ecx; pushad; call sub_1001AD20; popad
//   push ebp / mov ebp,esp / and esp,-16 / sub esp,0xD4  ; replayed prologue
//   jmp [dword_1015B5BC]
// The replay is essential: those 12 bytes belong to the function the E9
// replaced, so the resume must run with the frame already established.
__declspec(naked) void abi_fchs_shim()
{
	__asm {
		mov  dword ptr [zpatch_fchs_param], ecx
		pushad
		call abi_fchs_check
		popad
		push ebp
		mov  ebp, esp
		and  esp, 0FFFFFFF0h
		sub  esp, 0D4h
		jmp  dword ptr [zpatch_fchs_resume]
	}
}

// DualVehicleHeadlights (handler 0x10006770, E9 at the game match): bodies
// 0x100064F0 (new builds) / 0x10006630 (builds 1..6) duplicate the headlight-
// cone draw call with the game's custom register convention (args in
// eax/edx/ecx/ebx/ebp/edi/esi plus stack floats), saving every GPR and drawing
// slots 1 and 2 through the published pointers before tailing to match+338.
// abi_dual_headlights_init sets the constants the original's sub_100064D0 sets.
extern "C" void __cdecl abi_dual_headlights_init()
{
	zpatch_dual_light_a = 1;
	zpatch_dual_light_b = 2;
}
// @exact 0x100064F0
extern "C" __declspec(naked) void abi_dual_headlights_new()
{
	__asm {
		pushad
		call abi_dual_headlights_init
		popad
		mov DWORD PTR [zpatch_dual_r_eax], eax
		mov DWORD PTR [zpatch_dual_r_ebx], ebx
		mov DWORD PTR [zpatch_dual_r_ecx], ecx
		mov DWORD PTR [zpatch_dual_r_edx], edx
		mov DWORD PTR [zpatch_dual_r_esi], esi
		mov DWORD PTR [zpatch_dual_r_edi], edi
		mov DWORD PTR [zpatch_dual_r_ebp], ebp
		mov DWORD PTR [zpatch_dual_r_esp], esp
		push 0
		call DWORD PTR [zpatch_dual_bd0]
		add esp, 4
		push eax
		mov ecx, edi
		call DWORD PTR [zpatch_dual_be4]
		push eax
		mov eax, DWORD PTR [esp + 30h]
		fld DWORD PTR [esp + 1Ch]
		push 1
		push DWORD PTR [zpatch_dual_light_a]
		mov ecx, DWORD PTR [ebp + 24h]
		sub esp, 10h
		fstp DWORD PTR [esp + 0Ch]
		add eax, 30h
		fld DWORD PTR [esp + 38h]
		fstp DWORD PTR [esp + 8]
		fld DWORD PTR [esp + 30h]
		fstp DWORD PTR [esp + 4]
		fld DWORD PTR [esp + 2Ch]
		fstp DWORD PTR [esp]
		push eax
		push ecx
		movsx edx, BYTE PTR [edi + 40h]
		mov esi, DWORD PTR [edi + 48h]
		mov edi, DWORD PTR [zpatch_dual_bcc]
		lea ecx, [esp + 54h]
		call DWORD PTR [zpatch_dual_bd4]
		add esp, 24h
		mov eax, DWORD PTR [zpatch_dual_r_eax]
		mov ebx, DWORD PTR [zpatch_dual_r_ebx]
		mov ecx, DWORD PTR [zpatch_dual_r_ecx]
		mov edx, DWORD PTR [zpatch_dual_r_edx]
		mov esi, DWORD PTR [zpatch_dual_r_esi]
		mov edi, DWORD PTR [zpatch_dual_r_edi]
		mov ebp, DWORD PTR [zpatch_dual_r_ebp]
		mov esp, DWORD PTR [zpatch_dual_r_esp]
		push 0
		call DWORD PTR [zpatch_dual_bd0]
		add esp, 4
		push eax
		mov ecx, edi
		call DWORD PTR [zpatch_dual_be4]
		fld DWORD PTR [esp + 18h]
		mov ecx, DWORD PTR [esp + 28h]
		push eax
		push 1
		push DWORD PTR [zpatch_dual_light_b]
		mov eax, DWORD PTR [ebp + 24h]
		sub esp, 10h
		fstp DWORD PTR [esp + 0Ch]
		add ecx, 30h
		fld DWORD PTR [esp + 38h]
		fstp DWORD PTR [esp + 8]
		fld DWORD PTR [esp + 30h]
		fstp DWORD PTR [esp + 4]
		fld DWORD PTR [esp + 2Ch]
		fstp DWORD PTR [esp]
		push ecx
		push eax
		movsx edx, BYTE PTR [edi + 40h]
		mov esi, DWORD PTR [edi + 48h]
		mov edi, DWORD PTR [zpatch_dual_bcc]
		lea ecx, [esp + 54h]
		call DWORD PTR [zpatch_dual_bd4]
		add esp, 24h
		jmp DWORD PTR [zpatch_dual_be0]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}
// @exact 0x10006630
extern "C" __declspec(naked) void abi_dual_headlights_old()
{
	__asm {
		pushad
		call abi_dual_headlights_init
		popad
		mov DWORD PTR [zpatch_dual_r_eax], eax
		mov DWORD PTR [zpatch_dual_r_ebx], ebx
		mov DWORD PTR [zpatch_dual_r_ecx], ecx
		mov DWORD PTR [zpatch_dual_r_edx], edx
		mov DWORD PTR [zpatch_dual_r_esi], esi
		mov DWORD PTR [zpatch_dual_r_edi], edi
		mov DWORD PTR [zpatch_dual_r_ebp], ebp
		mov DWORD PTR [zpatch_dual_r_esp], esp
		push 0
		call DWORD PTR [zpatch_dual_bd0]
		add esp, 4
		push eax
		mov ecx, edi
		call DWORD PTR [zpatch_dual_be4]
		push eax
		mov eax, DWORD PTR [esp + 30h]
		fld DWORD PTR [esp + 20h]
		push 1
		push DWORD PTR [zpatch_dual_light_a]
		mov ecx, DWORD PTR [ebp + 24h]
		sub esp, 10h
		fstp DWORD PTR [esp + 0Ch]
		add eax, 30h
		fld DWORD PTR [esp + 30h]
		fstp DWORD PTR [esp + 8]
		fld DWORD PTR [esp + 2Ch]
		fstp DWORD PTR [esp + 4]
		fld DWORD PTR [esp + 34h]
		fstp DWORD PTR [esp]
		push eax
		push ecx
		movsx edx, BYTE PTR [edi + 40h]
		mov esi, DWORD PTR [edi + 48h]
		mov edi, DWORD PTR [zpatch_dual_bcc]
		lea ecx, [esp + 54h]
		call DWORD PTR [zpatch_dual_bd4]
		add esp, 24h
		mov eax, DWORD PTR [zpatch_dual_r_eax]
		mov ebx, DWORD PTR [zpatch_dual_r_ebx]
		mov ecx, DWORD PTR [zpatch_dual_r_ecx]
		mov edx, DWORD PTR [zpatch_dual_r_edx]
		mov esi, DWORD PTR [zpatch_dual_r_esi]
		mov edi, DWORD PTR [zpatch_dual_r_edi]
		mov ebp, DWORD PTR [zpatch_dual_r_ebp]
		mov esp, DWORD PTR [zpatch_dual_r_esp]
		push 0
		call DWORD PTR [zpatch_dual_bd0]
		add esp, 4
		push eax
		mov ecx, edi
		call DWORD PTR [zpatch_dual_be4]
		fld DWORD PTR [esp + 1Ch]
		mov ecx, DWORD PTR [esp + 28h]
		push eax
		push 1
		push DWORD PTR [zpatch_dual_light_b]
		mov eax, DWORD PTR [ebp + 24h]
		sub esp, 10h
		fstp DWORD PTR [esp + 0Ch]
		add ecx, 30h
		fld DWORD PTR [esp + 30h]
		fstp DWORD PTR [esp + 8]
		fld DWORD PTR [esp + 2Ch]
		fstp DWORD PTR [esp + 4]
		fld DWORD PTR [esp + 34h]
		fstp DWORD PTR [esp]
		push ecx
		push eax
		movsx edx, BYTE PTR [edi + 40h]
		mov esi, DWORD PTR [edi + 48h]
		mov edi, DWORD PTR [zpatch_dual_bcc]
		lea ecx, [esp + 54h]
		call DWORD PTR [zpatch_dual_bd4]
		add esp, 24h
		jmp DWORD PTR [zpatch_dual_be0]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}

// @exact 0x100512F0 LocalSettingsPath detour: entered via E9 at the path-setup
// function's epilogue (game 0x856EE6, swallowing pop edi/esi/ebx; ret plus one
// int3 pad), runs the path-rewrite helper between pushad/popad and replays it.
__declspec(naked) void abi_local_settings_detour()
{
	__asm {
		pushad
		call zpatch_local_settings_step
		popad
		pop edi
		pop esi
		pop ebx
		ret
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after ret)
	}
}
// @exact 0x10051300 LocalSettingsPath fallback detour (pattern absent on
// 1.0.8.0): same helper, then re-issues the original 5-arg call through ESI and
// the two-pop epilogue.
__declspec(naked) void abi_local_settings_fallback_detour()
{
	__asm {
		pushad
		call zpatch_local_settings_step
		popad
		push 0
		push 0
		push 1Ch
		push 0
		call esi
		pop esi
		pop edi
		ret
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after ret)
	}
}

// @exact 0x100051B0 MovingSteeringWheelSupport detour: the installer E9s the
// game site at match+2 (game 0xA538A0); the body caches ESI (the vehicle), runs
// the shared consumer between pushad/popad, calls vfunc 296 (al = its result)
// and tail-jumps to the resume (match+16, the jne after the replayed test).
__declspec(naked) void abi_steer_detour_eax()
{
	__asm {
		mov  dword ptr [zpatch_steer_vehicle], esi
		pushad
		call abi_steer_body
		popad
		mov  edx, dword ptr [esi]
		mov  eax, dword ptr [edx + 128h]
		mov  ecx, esi
		call eax
		test al, al
		jmp  dword ptr [zpatch_steer_resume]
	}
}
// @exact 0x100051E0 MovingSteeringWheelSupport fallback detour (pattern absent
// on 1.0.8.0): same, with the vfunc fetched through EAX/EDX instead.
__declspec(naked) void abi_steer_detour_edx()
{
	__asm {
		mov  dword ptr [zpatch_steer_vehicle], esi
		pushad
		call abi_steer_body
		popad
		mov  eax, dword ptr [esi]
		mov  edx, dword ptr [eax + 128h]
		mov  ecx, esi
		call edx
		test al, al
		jmp  dword ptr [zpatch_steer_resume]
	}
}

// @exact 0x10046C80 GarageModelFix body A (installed at game 0xC68BB0): pushad;
// ecx=object(esi), edx=vehicle(edi); call the store helper; popad; replay
// mov edx,[edi+0x20]; fld [edx+0x34]; tail-jump to the resume (match+6).
__declspec(naked) void abi_garage_body_a()
{
	__asm {
		pushad
		mov  ecx, esi
		mov  edx, edi
		call zpatch_garage_store_model
		popad
		mov  edx, dword ptr [edi + 20h]
		fld  dword ptr [edx + 34h]
		jmp  dword ptr [zpatch_garage_resume_a]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}
// @exact 0x10046CA0 GarageModelFix body B (installed at game 0x9659C3): pushad;
// call the lookup helper (object in ECX); popad; replay
// movzx ecx,[esi]; push eax; push ecx; tail-jump to the resume (match+5).
__declspec(naked) void abi_garage_body_b()
{
	__asm {
		pushad
		call zpatch_garage_lookup_model
		popad
		movzx ecx, word ptr [esi]
		push eax
		push ecx
		jmp  dword ptr [zpatch_garage_resume_b]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}

// FreezeCarFix / DebugPrintSpawns bodies: the sub_100372F0 guard, the ten
// factory-dispatch thunks and the sub_10037510 spawn probe (installer 0x10037C70;
// the FreezeCarFix loader arm gates the install).
// @exact 0x100372F0
__declspec(naked) void abi_spawns_guard()
{
	// Keeps the original callee's EBP frame and ret 0Ch cleanup, so it stays naked.
	__asm {
		push ebp
		mov ebp, esp
		cmp dword ptr [zpatch_spawns_skip], 0
		jne spawns_guard_taken
		push dword ptr [ebp + 10h]
		mov eax, dword ptr [zpatch_spawns_orig_b]
		push dword ptr [ebp + 0Ch]
		push dword ptr [ebp + 8]
		call eax
		mov dword ptr [zpatch_spawns_skip], 0
		pop ebp
		ret 0Ch
	spawns_guard_taken:
		xor eax, eax
		mov dword ptr [zpatch_spawns_skip], eax
		pop ebp
		ret 0Ch
		__emit 0CCh
	}
}

// The ten factory-dispatch thunks (P3 sites). NOTE: case 1 (+0x3D) and case 2
// (+0x56) both set selector 3 in the original bytes; kept 1:1.

// The selector each spawn thunk writes to zpatch_spawns_kind. The probe
// dispatches on it (0 car, 1 heli, 2 boat, 3 ped, 4 vehicle object, 5 object,
// 6 bike, 7 train, 8 plane); unscoped because the naked macro's inline asm
// takes the enumerator as an immediate.
enum SpawnSelector : std::uint32_t
{
	spawn_car = 0,
	spawn_heli = 1,
	spawn_boat = 2,
	spawn_ped = 3,
	spawn_vehicle_object = 4,
	spawn_object = 5,
	spawn_bike = 6,
	spawn_train = 7,
	spawn_plane = 8,
};

// @exact 0x10037330
// Spawn probes: the game's 10 class cases all do the same thing. The macro
// keeps the shim body in one place; only the selector and resume differ.
#define SPAWNS_CASE(name, selector, resume)            \
	__declspec(naked) void name() {                       \
		__asm push ebp                                       \
		__asm mov ebp, esp                                   \
		__asm mov ecx, dword ptr [ebp + 0Ch]                 \
		__asm movsx eax, cl                                  \
		__asm mov dword ptr [zpatch_spawns_kind], selector   \
		__asm mov dword ptr [zpatch_spawns_class], eax       \
		__asm mov dword ptr [ebp + 0Ch], ecx                 \
		__asm pop ebp                                        \
		__asm jmp dword ptr [resume]                         \
	}

SPAWNS_CASE(abi_spawns_case_0x24, spawn_object, zpatch_spawns_case0_resume)
SPAWNS_CASE(abi_spawns_case_0x3D, spawn_ped, zpatch_spawns_case1_resume)
SPAWNS_CASE(abi_spawns_case_0x56, spawn_ped, zpatch_spawns_case2_resume)
SPAWNS_CASE(abi_spawns_case_0x6F, spawn_car, zpatch_spawns_case3_resume)
SPAWNS_CASE(abi_spawns_case_0x88, spawn_vehicle_object, zpatch_spawns_case4_resume)
SPAWNS_CASE(abi_spawns_case_0x9E, spawn_bike, zpatch_spawns_case5_resume)
SPAWNS_CASE(abi_spawns_case_0xB4, spawn_train, zpatch_spawns_case6_resume)
SPAWNS_CASE(abi_spawns_case_0xCA, spawn_heli, zpatch_spawns_case7_resume)
SPAWNS_CASE(abi_spawns_case_0xE0, spawn_boat, zpatch_spawns_case8_resume)
SPAWNS_CASE(abi_spawns_case_0xF6, spawn_plane, zpatch_spawns_case9_resume)

// sub_10037510, the spawn probe (P1/P4/P5/P6/P7 sites): byte-exact naked body,
// 394 instructions, 0x10037510..0x10037ABC. ECX at the [vtable+0xC] call is the
// selector, exactly like the original - do not "fix" it.
// @exact 0x10037510
__declspec(naked) void abi_spawns_probe()
{
	__asm {

		push ebp
		mov ebp, esp
		sub esp, 4Ch
		mov eax, dword ptr [__security_cookie]
		xor eax, ebp
		mov dword ptr [ebp - 8], eax
		push esi
		mov esi, dword ptr [zpatch_pre_ped_array]
		xor eax, eax
		push edi
		mov edi, dword ptr [ebp + 8]
		__emit 0Fh
		__emit 1Fh
		__emit 00h
L_10037530:
		mov ecx, dword ptr [esi + eax*4]
		test ecx, ecx
		je L_10037540
		cmp dword ptr [ecx + 3Ch], edi
		je L_100375FA
L_10037540:
		inc eax
		cmp eax, 7918h
		jl L_10037530
		or eax, 0FFFFFFFFh
L_1003754B:
		mov ecx, dword ptr [zpatch_spawns_kind]
		cwde
		mov dword ptr [zpatch_spawns_kind], 0FFFFFFFFh
		mov esi, dword ptr [esi + eax*4]
		cmp eax, -1
		je L_10037AA2
		test esi, esi
		je L_10037AA2
		cmp esi, 0Ch
		je L_10037AA2
		cmp esi, 0CDCDCDCDh
		je L_10037AA2
		mov eax, dword ptr [esi]
		test eax, eax
		je L_10037AA2
		cmp eax, 0Ch
		je L_10037AA2
		cmp eax, 0CDCDCDCDh
		je L_10037AA2
		test ecx, ecx
		jne L_10037635
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_10037602
		cmp dword ptr [esi + 6Ch], 0
		jne L_10037602
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_car
		jmp L_10037A8C
L_100375FA:
		movzx eax, ax
		jmp L_1003754B
L_10037602:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_car
		jmp L_10037A27
L_10037635:
		cmp ecx, 1
		jne L_100376C0
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_1003768D
		cmp dword ptr [esi + 6Ch], 4
		jne L_1003768D
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_heli
		jmp L_10037A8C
L_1003768D:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_heli
		jmp L_10037A27
L_100376C0:
		cmp ecx, 2
		jne L_1003774B
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_10037718
		cmp dword ptr [esi + 6Ch], 2
		jne L_10037718
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_boat
		jmp L_10037A8C
L_10037718:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_boat
		jmp L_10037A27
L_1003774B:
		cmp ecx, 6
		jne L_100377D6
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_100377A3
		cmp dword ptr [esi + 6Ch], 1
		jne L_100377A3
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_bike
		jmp L_10037A8C
L_100377A3:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_bike
		jmp L_10037A27
L_100377D6:
		cmp ecx, 7
		jne L_10037861
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_1003782E
		cmp dword ptr [esi + 6Ch], 3
		jne L_1003782E
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_train
		jmp L_10037A8C
L_1003782E:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_train
		jmp L_10037A27
L_10037861:
		cmp ecx, 8
		jne L_100378EC
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_100378B9
		cmp dword ptr [esi + 6Ch], 5
		jne L_100378B9
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_plane
		jmp L_10037A8C
L_100378B9:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_plane
		jmp L_10037A27
L_100378EC:
		cmp ecx, 3
		jne L_1003796D
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 6
		je L_1003792D
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_ped
		jmp L_10037A27
L_1003792D:
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_ped
		jmp L_10037A8C
L_1003796D:
		cmp ecx, 4
		jne L_100379EB
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_100379AB
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_vehicle_object
		jmp L_10037A27
L_100379AB:
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_object
		jmp L_10037A8C
L_100379EB:
		cmp ecx, 5
		jne L_10037AA2
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 6
		je L_10037A59
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_used_nonped
L_10037A27:
		lea eax, [ebp - 4Ch]
		push 40h
		push eax
		call zpatch_spawns_sprintf
		add esp, 14h
		lea edx, [ebp - 4Ch]
		call zpatch_spawns_print_edx
L_10037A3D:
		mov dword ptr [zpatch_spawns_skip], 1
		xor eax, eax
		pop edi
		pop esi
		mov ecx, dword ptr [ebp - 8]
		xor ecx, ebp
		call zpatch_spawns_cookie_check_ecx
		mov esp, ebp
		pop ebp
		ret
L_10037A59:
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_generic
L_10037A8C:
		lea eax, [ebp - 4Ch]
		push 40h
		push eax
		call zpatch_spawns_sprintf
		add esp, 14h
		lea edx, [ebp - 4Ch]
		call zpatch_spawns_print_edx
L_10037AA2:
		mov eax, dword ptr [zpatch_spawns_orig_a]
		push edi
		call eax
		mov ecx, dword ptr [ebp - 8]
		add esp, 4
		xor ecx, ebp
		pop edi
		pop esi
		call zpatch_spawns_cookie_check_ecx
		mov esp, ebp
		pop ebp
		ret
		__emit 0CCh
	}
}

// @exact 0x1001B2E0 CarDecalsFix: the original scans for
// "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00" (unique on
// 1.0.8.0, game 0x6F7F50) and E9s the first match to this replacement of the
// game function's entry.
//   a2 = [esp+8]; if ((a2[0x28] & 0x3C0) == 0x80) { a1 = [esp+4];
//      if (a1[0x38]) return a1[0x34]; } else { a1 = [esp+4]; }
//   return a1[0x30];   (__stdcall, ret 8)
__declspec(naked) void abi_car_decals_fix()
{
	__asm {
		mov eax, dword ptr [esp + 8]
		mov ecx, dword ptr [eax + 28h]
		and ecx, 3C0h
		cmp ecx, 80h
		jne car_decals_other
		mov eax, dword ptr [esp + 4]
		cmp dword ptr [eax + 38h], 0
		je  car_decals_load
		mov eax, dword ptr [eax + 34h]
		ret 8
	car_decals_other:
		mov eax, dword ptr [esp + 4]
	car_decals_load:
		mov eax, dword ptr [eax + 30h]
		ret 8
	}
}

// @exact 0x100054E0 Patch4RemoveModChecks / RemoveGFWLUpdatePopup stub: runs the
// flag-gated helper between pushad/popad, replays the original's first two
// instructions (its 64-bit sub of ECX:ESI from EDX:EAX into the caller's stack
// slots) and tail-jumps to the handler-published continuation (match+12).
__declspec(naked) void abi_gfwl_stub()
{
	__asm {
		pushad
		call abi_gfwl_patch
		popad
		sub  eax, ecx
		sbb  edx, esi
		mov  dword ptr [esp + 0Ch], eax
		mov  dword ptr [esp + 10h], edx
		jmp  dword ptr [zpatch_gfwl_resume]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}

// @exact 0x10037BA0 BadPlayerModelCrashFix trampoline: the handler E9s the game
// function at pattern "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C" (game 0x7CF803,
// 3 bytes in) and publishes match+0xA. The body captures [ebp+0xC] (entity) and
// [ebp+0x10] (model index), runs the check between pushad/popad, writes the
// corrected model index back to [ebp+0x10], then replays the overwritten
// prologue (and esp,0xF0; sub esp,0x54; xorps xmm0,xmm0; push ebx) and tails.
// EBP is the game function's live frame.
__declspec(naked) void abi_bad_player_model()
{
	__asm {
		push edi
		mov  edi, dword ptr [ebp + 0Ch]
		mov  dword ptr [zpatch_bpm_obj], edi
		mov  edi, dword ptr [ebp + 10h]
		mov  dword ptr [zpatch_bpm_model_index], edi
		pushad
		call abi_bpm_check
		popad
		mov  edi, dword ptr [zpatch_bpm_model_index]
		mov  dword ptr [ebp + 10h], edi
		pop  edi
		and  esp, 0FFFFFFF0h
		sub  esp, 54h
		xorps xmm0, xmm0
		push ebx
		jmp  dword ptr [zpatch_bpm_resume]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}

// @exact 0x100521E0 NewSavesCompatibility tramp: the handler E9s the savegame-
// load entry at "89 84 24 E0 01 00 00 8B 45 08 53 33 DB" (game 0x85B093) so this
// runs the converter first, then replicates the overwritten instruction (the
// stack-cookie store) and the next two, and tail-jumps to match+13. Entry is by
// JMP, so the [esp+0x1E0] store targets the same game frame slot as the original.
__declspec(naked) void abi_new_saves_compat()
{
	__asm {
		pushad
		call abi_new_saves_prepare
		popad
		mov  dword ptr [esp + 1E0h], eax
		mov  eax, dword ptr [ebp + 8]
		push ebx
		xor  ebx, ebx
		jmp  dword ptr [zpatch_new_saves_resume]
	}
}

// @exact 0x100469E0 SuperLODFix: not byte-exact (calls a compiled helper). The
// fail path resumes via dword_101580AC, the ok path via dword_101580B0; both are
// handler-published base+rva values, build-gated.
__declspec(naked) void abi_super_lod_fix_shim()
{
	__asm {
		jmp slod_main
	slod_fail:
		jmp DWORD PTR [zpatch_super_lod_fail]
	slod_main:
		push eax
		push ecx
		push esi
		call abi_super_lod_helper
		add esp, 4
		pop ecx
		test al, al
		pop eax
		jne slod_ok
		test byte ptr [eax + 0x72], 0x10
		je slod_fail
	slod_ok:
		jmp DWORD PTR [zpatch_super_lod_ok]
	}
}

// 0x1004AA80: the VRAM verdict helper, called between pushad/popad by the shim
// below. It only reads the two argument globals and sets the flag, so ordinary
// C++ is enough.
extern "C" void abi_vram_fix_helper() noexcept
{
	const auto argB = static_cast<std::int32_t>(zpatch_vram_arg_b);
	zpatch_vram_flag = 0;
	// The original compares the dword's value as a signed int (the float bits).
	const auto quarter_bits = static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(kFloatQuarter));
	if (argB < 0 || zpatch_vram_arg_a != 0 || argB < quarter_bits)
		zpatch_vram_flag = 1;
}
// @orig 0x1004aab0 VRAMFix: stashes two of the game function's arguments, calls the
// verdict helper under pushad/popad, then reproduces the original tail
// (fldcw [esp]; add esp,0x1c; ret), returning INT_MAX when the verdict is set.
// The E9 site replaced that tail, so this must be naked.
// @exact 0x1004AAB0
__declspec(naked) void abi_vram_fix_shim()
{
	__asm {
		mov eax, DWORD PTR [esp + 0x14]
		mov DWORD PTR [zpatch_vram_arg_a], eax
		mov eax, DWORD PTR [esp + 0x10]
		mov DWORD PTR [zpatch_vram_arg_b], eax
		pushad
		call abi_vram_fix_helper
		popad
		cmp BYTE PTR [zpatch_vram_flag], 0
		je vram_skip
		mov eax, 7FFFFFFFh
	vram_skip:
		fldcw WORD PTR [esp]
		add esp, 1Ch
		ret
	}
}

// @orig 0x100064b0 DrivebyFOVFix: computes the drive-by FOV once and feeds it to the
// resume as xmm0. The original is `pushal; call sub_10006450; popal;
// movss xmm0,[0x101580c4]; jmp [0x1015bdf4]`; sub_10006450 resolves the handle
// dword_101580D4 (salt 0x09399528) through dword_1015BDE0, walks two structures,
// caches the entry and reads the float at +0x60.
// The resolver result and cached entry are dereferenced unguarded, like the
// original (0x1000646E / 0x10006490); 45.0f is its own first instruction
// (0x10006457). Kept separate and naked so the shim's call keeps the stack shape.
extern "C" __declspec(naked) void abi_driveby_fov_helper()
{
	__asm {
		cmp BYTE PTR [zpatch_driveby_done], 0
		mov DWORD PTR [zpatch_driveby_fov], 42340000h  // 45.0f default
		jne drv_have
		mov eax, DWORD PTR [zpatch_salt_d4]
		push edx
		push eax
		call zpatch_driveby_resolve_seh
		add esp, 8
		mov ecx, DWORD PTR [eax + 0Eh]    // AVs when the handle did not resolve
		add eax, 0Eh
		add ecx, eax
		mov BYTE PTR [zpatch_driveby_done], 1
		mov eax, DWORD PTR [ecx + 0Fh]    // AVs on a garbage chain
		mov eax, DWORD PTR [eax + ecx + 14h]
		mov DWORD PTR [zpatch_driveby_cached], eax
		jmp drv_read
	drv_have:
		mov eax, DWORD PTR [zpatch_driveby_cached]
	drv_read:
		mov eax, DWORD PTR [eax]          // AVs when the cached entry is null
		test eax, eax
		je drv_done
		mov edx, OFFSET zpatch_driveby_fov
		__emit 0F3h
		__emit 0Fh
		__emit 10h
		__emit 40h
		__emit 60h                   // movss xmm0, DWORD PTR [eax + 0x60]
		__emit 0F3h
		__emit 0Fh
		__emit 11h
		__emit 02h                   // movss DWORD PTR [edx], xmm0
	drv_done:
		ret
	}
}
// 0x100064b0 shim: the original restores every GPR before the SSE load; EAX is
// scratch (pushed/popped), so no register the resume consumes is clobbered.
__declspec(naked) void abi_driveby_fov_shim()
{
	__asm {
		pushad
		call abi_driveby_fov_helper
		popad
		push eax
		mov eax, OFFSET zpatch_driveby_fov
		__emit 0F3h
		__emit 0Fh
		__emit 10h
		__emit 00h                   // movss xmm0, DWORD PTR [eax]
		pop eax
		jmp DWORD PTR [zpatch_driveby_resume]
	}
}

// NoLiveryLimit helpers (sub_1003D220 / sub_1003D2F0 / sub_1003D270). The naked
// shims below stash their register inputs in globals, call a compiled helper,
// then tail-jump to the handler-published resume. The count helper calls the
// published function as `__thiscall(this, hash)`.
extern "C" void abi_nolivery_hash_sign()  // sub_1003D220
{
	std::array<char, kSpawnsNameCapacity> buffer;
	// Original: sprintf(buffer, 0x40, "%s_sign_%d", name, index + 1).
	zpatch_spawns_sprintf(
	    buffer.data(), buffer.size(), "%s_sign_%d",
	    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_nolivery_name)),
	    static_cast<int>(zpatch_nolivery_index) + 1);
	zpatch_nolivery_hash = GameHash(buffer.data());
	zpatch_cookie_tail();
}
extern "C" void abi_nolivery_copy_name()  // sub_1003D2F0
{
	// push [bufptr]; push 0x10; push [dest]; call [strcpy_s]; add esp,0xc; ret.
	// Reads both globals unguarded and has no /GS cookie tail (leaf).
	char* const dst = reinterpret_cast<char*>(static_cast<std::uintptr_t>(zpatch_nolivery_dest));
	const char* const src =
	    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_nolivery_bufptr));
	zpatch_secure_strcpy_s(dst, kNoLiveryNameCapacity, src);
}
extern "C" void abi_nolivery_count_loop()  // sub_1003D270
{
	using LookupFn = int(__fastcall*)(void* self, int, std::uint32_t hash);
	const auto fn = reinterpret_cast<LookupFn>(static_cast<std::uintptr_t>(zpatch_nolivery_fn));
	void* const self = reinterpret_cast<void*>(static_cast<std::uintptr_t>(zpatch_nolivery_arg));
	const char* const name =
	    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_nolivery_bufptr));
	zpatch_nolivery_count = 0;
	for (std::uint32_t i = 1;; ++i)
	{
		std::array<char, kSpawnsNameCapacity> buffer;
		zpatch_spawns_sprintf(buffer.data(), buffer.size(), "%s_sign_%d", name,
		                      static_cast<int>(i));
		const int result = fn ? fn(self, 0, GameHash(buffer.data())) : 0;
		if (result == 0) break;
		zpatch_nolivery_count = i;
	}
	zpatch_cookie_tail();
}
// @exact 0x1003d310: name = &ebx[0x13c], index = eax; resume with (hash, movsx ebx[0x48]).
extern "C" __declspec(naked) void abi_nolivery_a()
{
	__asm {
		pushad
		lea ecx, [ebx + 13Ch]
		mov DWORD PTR [zpatch_nolivery_name], ecx
		mov DWORD PTR [zpatch_nolivery_index], eax
		call abi_nolivery_hash_sign
		popad
		mov ecx, DWORD PTR [zpatch_nolivery_hash]
		movsx edx, WORD PTR [ebx + 48h]
		push ecx
		push edx
		jmp DWORD PTR [zpatch_nolivery_resume]
	}
}
// @exact 0x1003d340: name = &ebx[0x13c], index = ecx; resume with (hash, movsx ebx[0x48]).
extern "C" __declspec(naked) void abi_nolivery_b()
{
	__asm {
		pushad
		lea edx, [ebx + 13Ch]
		mov DWORD PTR [zpatch_nolivery_name], edx
		mov DWORD PTR [zpatch_nolivery_index], ecx
		call abi_nolivery_hash_sign
		popad
		mov edx, DWORD PTR [zpatch_nolivery_hash]
		movsx eax, WORD PTR [ebx + 48h]
		push edx
		push eax
		jmp DWORD PTR [zpatch_nolivery_resume]
	}
}
// @exact 0x1003d370: like a, but the original also drops 8 bytes of args first.
extern "C" __declspec(naked) void abi_nolivery_c()
{
	__asm {
		pushad
		lea edx, [ebx + 13Ch]
		mov DWORD PTR [zpatch_nolivery_name], edx
		mov DWORD PTR [zpatch_nolivery_index], eax
		call abi_nolivery_hash_sign
		popad
		mov edx, DWORD PTR [zpatch_nolivery_hash]
		movsx eax, WORD PTR [ebx + 48h]
		add esp, 8
		push edx
		push eax
		jmp DWORD PTR [zpatch_nolivery_resume]
	}
}
// @exact 0x1003d3a0: copies the frame name into a 16-byte buffer, counts the
// "<name>_sign_N" lookups, writes the count to [ebp+0xc2], then resumes.
extern "C" __declspec(naked) void abi_nolivery_d()
{
	__asm {
		pushad
		mov DWORD PTR [zpatch_nolivery_arg], edi
		lea ecx, [ebp + 13Ch]
		mov DWORD PTR [zpatch_nolivery_dest], ecx
		lea ecx, [esp + 60h]
		mov DWORD PTR [zpatch_nolivery_bufptr], ecx
		call abi_nolivery_copy_name
		call abi_nolivery_count_loop
		popad
		push ebx
		mov ebx, DWORD PTR [zpatch_nolivery_count]
		mov BYTE PTR [ebp + 0C2h], bl
		pop ebx
		jmp DWORD PTR [zpatch_nolivery_resume_d]
	}
}
// @exact 0x1003d3e0: the same sequence as 0x1003d3a0 with ESI as the object
// (name = &edi[0x13c], count written to [edi+0xc2]); same resume slot.
extern "C" __declspec(naked) void abi_nolivery_e()
{
	__asm {
		pushad
		mov DWORD PTR [zpatch_nolivery_arg], esi
		lea ecx, [edi + 13Ch]
		mov DWORD PTR [zpatch_nolivery_dest], ecx
		lea ecx, [esp + 60h]
		mov DWORD PTR [zpatch_nolivery_bufptr], ecx
		call abi_nolivery_copy_name
		call abi_nolivery_count_loop
		popad
		push ebx
		mov ebx, DWORD PTR [zpatch_nolivery_count]
		mov BYTE PTR [edi + 0C2h], bl
		pop ebx
		jmp DWORD PTR [zpatch_nolivery_resume_d]
	}
}

// @exact 0x10036F00 CrashMsgFix: calls the game's message getter through the
// handler-published pointer (the site's original call target, *(match-4)) and
// replaces every '~' with a space. Entered via the handler's E8, so it returns
// normally; `66 90` is the original's two-byte nop before the scan loop.
extern "C" __declspec(naked) void abi_crash_msg_shim()
{
	__asm {
		call DWORD PTR [zpatch_crash_msg_getter]
		mov dl, BYTE PTR [eax]
		test dl, dl
		je cms_done
		mov ecx, eax
		__emit 066h
		__emit 090h
	cms_loop:
		cmp dl, 7Eh
		jne cms_skip
		mov BYTE PTR [ecx], 20h
	cms_skip:
		mov dl, BYTE PTR [ecx + 1]
		inc ecx
		test dl, dl
		jne cms_loop
	cms_done:
		ret
	}
}

// NewFilesCompatibility: body 0x10051600 registers a callback with the game's
// file-registration function; bodies 0x10051890 / 0x100518D0 are the per-file
// update hooks (E8 sites) that resolve the asset object, hash the name and
// register it. Helpers mirror sub_100515D0 / sub_100515E0 / sub_10051730 /
// sub_10051610 / sub_100517E0.
namespace
{
// A scan over the game image; a miss is 0. Same image/PE checks as scan_game.
std::uintptr_t newfiles_find(std::string_view signature)
{
	return reinterpret_cast<std::uintptr_t>(scan_game(signature));
}
}  // namespace
extern "C" void* abi_newfiles_cb(void** a1)  // 0x100515D0
{
	void* const result = *a1;
	*reinterpret_cast<int*>(*a1) = 1;
	return result;
}
// 0x100515E0 is five instructions: push callback, push hash, call the
// published register function, add esp,8, ret. The flags at the resume come
// from that add esp,8, so abi_newfiles_register_body keeps the naked stack
// shape around this C helper.
extern "C" void __cdecl abi_newfiles_register()
{
	// The slot is a cdecl two-argument registrar; the original cleans both
	// arguments after the call.
	const auto register_file = reinterpret_cast<void(__cdecl*)(std::uint32_t, void*)>(
	    static_cast<std::uintptr_t>(zpatch_newfiles_register_slot));
	register_file(kNewFilesRegistrarKey, reinterpret_cast<void*>(&abi_newfiles_cb));
}
extern "C" void* abi_newfiles_find_obj()  // 0x10051730
{
	// The original re-dereferences the container global on EVERY call
	// (**(_DWORD**)(match+2)), so it is picked up again whenever the game rebuilds
	// its registry; caching the container itself left a stale one that faulted in
	// the game's container search (0x43a490).
	static std::uintptr_t match = 0;
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		match = newfiles_find(
		    "8B 35 ? ? ? ? 6A 00 50 E8 ? ? ? ? 83 C4 08 50 8B CE E8 ? ? ? ? 85 C0 89 07 5E 74 05");
	}
	if (!match) return nullptr;
	const std::uint32_t global =
	    *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const void*>(match + 2u));
	return global ? reinterpret_cast<void*>(*reinterpret_cast<const std::uint32_t*>(
	                    reinterpret_cast<const void*>(global)))
	              : nullptr;
}
extern "C" int abi_newfiles_call_obj(void* obj, std::uint32_t hash)  // 0x10051610
{
	using Fn = int(__fastcall*)(void* self, int, std::uint32_t);
	static std::uintptr_t cached = 0;
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		cached = newfiles_find("85 C9 53 55 56 57 74 40 8B 6C 24 14 8D 64 24 00");
		if (!cached) cached = newfiles_find("53 55 8B 6C 24 0C 56 57 EB 06");
	}
	if (!cached) return 0;
	return reinterpret_cast<Fn>(cached)(obj, 0, hash);
}
extern "C" int abi_newfiles_call_add(void* self, void* str)  // 0x100517E0
{
	using Fn = int(__fastcall*)(void* self, int, void*);
	static std::uintptr_t cached = 0;
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		const std::uintptr_t m = newfiles_find("8B 0F 85 C9 74 ? 0F B7 41 0A 66 85 C0");
		cached = m ? m - 20u : 0;
	}
	if (!cached) return 0;
	return reinterpret_cast<Fn>(cached)(self, 0, str);
}
// @exact 0x10051600
extern "C" __declspec(naked) void abi_newfiles_register_body()  // 0x10051600
{
	__asm {
		pushad
		call abi_newfiles_register
		popad
		ret
	}
}
extern "C" char __fastcall abi_newfiles_update_a(void* self, int)  // 0x10051890
{
	auto* base = static_cast<unsigned char*>(self);
	if (base[kNewFilesNameA] == 0) return 0;
	if (abi_newfiles_call_obj(abi_newfiles_find_obj(),
	                          GameHash(reinterpret_cast<const char*>(base + kNewFilesNameA))))
		abi_newfiles_call_add(base + 8, base + kNewFilesNameA);
	return 1;
}
extern "C" char __fastcall abi_newfiles_update_b(void* self, int)  // 0x100518D0
{
	auto* base = static_cast<unsigned char*>(self);
	if (base[kNewFilesNameB] == 0) return 0;
	if (abi_newfiles_call_obj(abi_newfiles_find_obj(),
	                          GameHash(reinterpret_cast<const char*>(base + kNewFilesNameB))))
		abi_newfiles_call_add(base + 4, base + kNewFilesNameB);
	return 1;
}

// BenchmarkFix: two match-site shims (E9 at match[0]/[1]+8). Each checks eax,
// runs the interposed callback (esi with edi), then tails to the published
// resume, or unwinds via the enclosing function's tail when eax is null.
// @exact 0x10005650
extern "C" __declspec(naked) void abi_benchmark_a()  // 0x10005650
{
	__asm {
		test eax, eax
		je bf_a_zero
		push edi
		mov ebx, eax
		call esi
		jmp DWORD PTR [zpatch_benchmark_resume_a]
	bf_a_zero:
		xor al, al
		pop esi
		pop ebp
		pop ebx
		add esp, 10h
		ret
	}
}
// @exact 0x10005670
extern "C" __declspec(naked) void abi_benchmark_b()  // 0x10005670
{
	__asm {
		test eax, eax
		je bf_b_zero
		push edi
		mov ebx, eax
		call esi
		jmp DWORD PTR [zpatch_benchmark_resume_b]
	bf_b_zero:
		xor al, al
		pop esi
		pop ebp
		pop ebx
		add esp, 10h
		ret
	}
}

// BuildingDynamicShadows: shim installed at the sig
// `8B 47 28 C1 E8 06 83 E0 0F 83 C0`. If [edi] is one of the two handler-
// published model hashes it takes the special resume, else it derives the shadow
// level `(([edi+0x28]>>6)&0xf)-1` and takes the normal resume (match+0xf /
// match+0x70).
// @exact 0x10005AF0
extern "C" __declspec(naked) void abi_building_dynamic_shadows()
{
	__asm {
		mov eax, DWORD PTR [edi]
		cmp eax, DWORD PTR [zpatch_bds_hash_a]
		je bds_special
		cmp eax, DWORD PTR [zpatch_bds_hash_b]
		je bds_special
		mov eax, DWORD PTR [edi + 28h]
		shr eax, 6
		and eax, 0Fh
		add eax, -1
		jmp DWORD PTR [zpatch_bds_resume_a]
	bds_special:
		jmp DWORD PTR [zpatch_bds_resume_b]
	}
}

// CopSirensFlag helpers (sub_10005F70 / sub_10006260) and the reimplemented IDE
// parser (sub_10003FA0). The shims stash the model index, run the check and tail-
// jump to the resume; the IDE hook fills the model-hash tables the checks read.
extern "C" int abi_copsirens_check_a()  // sub_10005F70
{
	// The twelve police model names the original compares, in its table order.
	constexpr std::array<std::string_view, 13> kPolice = {
	    "police",    "police2",   "police3", "police4",    "noose",      "pstockade",
	    "nstockade", "polpatriot", "fbi",     "n_police",   "n_police2",  "n_polpatriot",
	    "n_pstockade"};
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	const std::int32_t index = static_cast<std::int32_t>(zpatch_copsirens_index);
	zpatch_copsirens_flag = 0;
	// sub_10005F70's `call dword_10159C0C` is unguarded.
	for (const std::string_view name : kPolice)
	{
		std::int32_t out = -1;
		lookup(GameHash(name), &out);
		if (out == index)
		{
			zpatch_copsirens_flag = 1;
			return 1;
		}
	}
	// Original sub_10005F70 re-stores the flag before the models loop
	// (mov eax,[0x10159B80]; ...; mov [0x10159B80],eax); keep the store.
	{
		volatile std::uint32_t* flag = &zpatch_copsirens_flag;
		*flag = *flag;
	}
	for (int i = 0; i < static_cast<int>(kVehicleLightSlotCount) && zpatch_copsirens_models[i]; ++i)
	{
		std::int32_t out = -1;
		lookup(zpatch_copsirens_models[i], &out);
		if (out == index)
		{
			zpatch_copsirens_flag = 1;
			return 1;
		}
	}
	return 0;
}
extern "C" int abi_copsirens_check_b()  // sub_10006260
{
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	const std::int32_t index = static_cast<std::int32_t>(zpatch_copsirens_index_b);
	zpatch_copsirens_flag_b = 0;
	std::int32_t out = -1;
	// sub_10006260's `call dword_10159C0C` is unguarded.
	lookup(GameHash("police2"), &out);
	if (out != index)
	{
		if (!zpatch_copsirens_models[0]) return 0;
		for (int i = 0; i < static_cast<int>(kVehicleLightSlotCount) && zpatch_copsirens_models[i]; ++i)
		{
			out = -1;
			lookup(zpatch_copsirens_models[i], &out);
			if (out == index)
			{
				zpatch_copsirens_flag_b = 1;
				return 1;
			}
		}
		return 0;
	}
	zpatch_copsirens_flag_b = 1;
	return 1;
}
// _strnicmp equivalent: the original's IDE parser takes the case-insensitive
// prefix branch (strnicmp(v0, "wheelangle:", 0xB)); stops at a NUL like the CRT.
bool prefix_ci(const char* text, const char* prefix, std::size_t length) noexcept
{
	for (std::size_t i = 0; i < length; ++i)
	{
		const unsigned char a = static_cast<unsigned char>(text[i]);
		const unsigned char b = static_cast<unsigned char>(prefix[i]);
		if (a == 0 || b == 0) return a == b;
		const unsigned char lower_a =
		    (a >= 'A' && a <= 'Z') ? static_cast<unsigned char>(a + 32) : a;
		const unsigned char lower_b =
		    (b >= 'A' && b <= 'Z') ? static_cast<unsigned char>(b + 32) : b;
		if (lower_a != lower_b) return false;
	}
	return true;
}

extern "C" void abi_ide_parse()  // sub_10003FA0
{
	// The original tests the string unguarded; a null zpatch_ide_string faults.
	const char* const str =
	    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_ide_string));
	const std::uint32_t model = *reinterpret_cast<const std::uint32_t*>(
	    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(zpatch_ide_model) + 60u));
	const auto append = [](std::uint32_t* array, std::uint32_t value)
	{
		for (int i = 0; i < static_cast<int>(kVehicleLightSlotCount); ++i)
		{
			if (array[i] == value) return;
			if (array[i] == 0)
			{
				array[i] = value;
				return;
			}
		}
	};
	// The original's arms are independent `if`s on the same string (the
	// popuplights block falls through to the siren check), not else-if.
	if (std::strcmp(str, "popuplights") == 0)
	{
		append(zpatch_popuplights_models, model);
	}
	if (std::strcmp(str, "siren") == 0)
	{
		append(zpatch_copsirens_models, model);
	}
	if (prefix_ci(str, "wheelangle:", 11))
	{
		// sub_10003FA0's third arm: strnicmp(...,0xB) then strtof(String1+11). An
		// existing model entry is left untouched (the original's scan only breaks
		// out); the angle is stored only on append.
		const float angle = std::strtof(str + 11, nullptr);
		std::uint32_t bits = 0;
		std::memcpy(&bits, &angle, sizeof(bits));
		for (int i = 0; i < static_cast<int>(kVehicleLightSlotCount); ++i)
		{
			if (zpatch_wheelangle_models[i] == model) return;
			if (zpatch_wheelangle_models[i] == 0)
			{
				zpatch_wheelangle_models[i] = model;
				zpatch_wheelangle_angles[i] = bits;
				return;
			}
		}
	}
}
// @exact 0x10006310
extern "C" __declspec(naked) void abi_copsirens_a()  // 0x10006310
{
	__asm {
		mov DWORD PTR [zpatch_copsirens_index], edx
		pushad
		pushfd
		call abi_copsirens_check_a
		popfd
		popad
		mov eax, DWORD PTR [zpatch_copsirens_flag]
		jmp DWORD PTR [zpatch_copsirens_resume]
	}
}
// @exact 0x10006330
extern "C" __declspec(naked) void abi_copsirens_b()  // 0x10006330
{
	__asm {
		mov DWORD PTR [zpatch_copsirens_index_b], eax
		pushad
		pushfd
		call abi_copsirens_check_b
		popfd
		popad
		cmp DWORD PTR [zpatch_copsirens_flag_b], 1
		jmp DWORD PTR [zpatch_copsirens_resume_b]
	}
}
// @exact 0x100040E0
extern "C" __declspec(naked) void abi_copsirens_ide()  // 0x100040E0
{
	__asm {
		pushad
		mov DWORD PTR [zpatch_ide_string], edi
		mov DWORD PTR [zpatch_ide_model], eax
		call abi_ide_parse
		popad
		mov ecx, 7
		xor edx, edx
		__emit 0F3h
		__emit 0A6h               // repe cmpsb
		jmp DWORD PTR [zpatch_ide_hook_slot]
	}
}

// BorderlessWindowed: the two window-proc shims (0x10005890 = Unicode /
// 0x10005900 = ANSI). The installer replaces the game's `call [DefWindowProc]`
// with E8 -> body and NOPs the leftover byte; the body does the one-time style
// fixup then defers to the real DefWindowProc. Its `ret 0x10` consumes the
// DefWindowProc argument slots like the replaced call, so the game's own
// `pop edi/esi/ebp/ebx; add esp,0x10; ret 0x10` still unwinds correctly.
extern "C" LRESULT __stdcall abi_borderless_w(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam)
{
	if (!zpatch_borderless_w_done)
	{
		SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		SetWindowLongA(hwnd, GWL_STYLE,
		               GetWindowLongA(hwnd, GWL_STYLE) & static_cast<LONG>(kBorderlessStyleMask));
		SetWindowLongA(hwnd, GWL_EXSTYLE,
		               GetWindowLongA(hwnd, GWL_EXSTYLE) & ~static_cast<LONG>(WS_EX_TOPMOST));
		zpatch_borderless_w_done = 1;
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}
extern "C" LRESULT __stdcall abi_borderless_a(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam)
{
	if (!zpatch_borderless_a_done)
	{
		SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
		SetWindowLongA(hwnd, GWL_STYLE,
		               GetWindowLongA(hwnd, GWL_STYLE) & static_cast<LONG>(kBorderlessStyleMask));
		SetWindowLongA(hwnd, GWL_EXSTYLE,
		               GetWindowLongA(hwnd, GWL_EXSTYLE) & ~static_cast<LONG>(WS_EX_TOPMOST));
		zpatch_borderless_a_done = 1;
	}
	return DefWindowProcA(hwnd, msg, wparam, lparam);
}

// SpeedupBugFix: replaces the game's timing helper (0x10050540, __thiscall on a
// _QWORD*). It lazily fills the game's eight timing-constant floats (the handler
// publishes pointers to them) from QueryPerformanceFrequency, then stores a
// 64-bit timestamp from the game's own clock fn (match-51) into *this. Compiled.
extern "C" void* __fastcall abi_speedup_body(void* self, int)
{
	if (zpatch_tls_static_header(zpatch_speedup_scan_guard))
	{
		// Mirror sub_10050587's MSVC static-init guard and dword_101A849C cache.
		// The original locates the clock fn with this signature and calls match-51
		// (the handler's `50 64 ...` sig locates the E9 site, not this fn).
		zpatch_speedup_clock = static_cast<std::uint32_t>(newfiles_find(
		    "1B D9 89 5C 24 24 0F 88"));
		// Diagnostic only: the original has no check here; a missed match makes the
		// faithful `clock - 51` call fault immediately (0xFFFFFFCD).
		zpatch_tls_static_footer(zpatch_speedup_scan_guard);
	}
	// The original dereferences the timing pointers unconditionally (its one-time
	// test is on the float, not on the pointers).
	if (*zpatch_speedup_t50 == 0.0f)
	{
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		const double scale = 1000.0 / static_cast<double>(frequency.QuadPart);
		const float v5 = static_cast<float>(scale);
		const float v6 = static_cast<float>(scale * 1000.0);
		const float v7 = static_cast<float>(scale * 0.001000000047497451);
		const float v8 = static_cast<float>(0.001000000047497451 / scale);
		*zpatch_speedup_t64 = v5;
		*zpatch_speedup_t60 = v6;
		*zpatch_speedup_t50 = v7;
		*zpatch_speedup_t5c = v8;
		*zpatch_speedup_t68 = v5;
		*zpatch_speedup_t4c = v6;
		*zpatch_speedup_t54 = v7;
		*zpatch_speedup_t58 = v8;
	}
	using ClockFn = __int64(__cdecl*)(void);
	// The original calls its resolved clock function unconditionally; a failed
	// scan faults at match-51.
	const __int64 stamp = reinterpret_cast<ClockFn>(
	    static_cast<std::uintptr_t>(zpatch_speedup_clock) - 51u)();
	*reinterpret_cast<__int64*>(self) = stamp;
	return self;
}

// @exact 0x1001b040 EpisodeSwitchFreezeFix: stores eax into [ebx+0x20]; null
// returns 0 (pop edi/esi/ebx, ret 0x10), else tail-jumps to match+5.
__declspec(naked) void abi_episode_switch_store_shim()
{
	__asm {
		mov DWORD PTR [ebx + 0x20], eax
		cmp eax, 0
		je ep_store_null
		mov edx, DWORD PTR [eax]
		jmp DWORD PTR [zpatch_episode_switch_resume_a]
	ep_store_null:
		xor al, al
		pop edi
		pop esi
		pop ebx
		mov esp, ebp
		pop ebp
		ret 16
	}
}
// @exact 0x1001b060
__declspec(naked) void abi_episode_switch_shim_b()
{
	__asm {
		cmp ebx, 0
		je ep_b_null
		mov ecx, DWORD PTR [ebx + 0x10]
		cmp ecx, DWORD PTR [ebx + 0x14]
		jmp DWORD PTR [zpatch_episode_switch_resume_b]
	ep_b_null:
		mov BYTE PTR [edi], 0
		pop edi
		pop esi
		mov eax, ebp
		pop ebp
		pop ebx
		pop ecx
		ret
	}
}
// @exact 0x1001b0a0
__declspec(naked) void abi_episode_switch_vcall()
{
	__asm {
		cmp ecx, 0
		je ep_vc_null
		mov eax, ecx
		mov ecx, DWORD PTR [eax]
		mov edx, DWORD PTR [ecx]
		mov eax, DWORD PTR [eax + 4]
		mov edx, DWORD PTR [edx + 0x30]
		push eax
		call edx
		ret
	ep_vc_null:
		mov eax, 0
		ret
	}
}
// @exact 0x1001b0c0
__declspec(naked) void abi_episode_switch_shim_c()
{
	__asm {
		cmp ecx, 0
		je ep_c_null
		push esi
		mov esi, ecx
		cmp DWORD PTR [esi + 0x14], 0
		jmp DWORD PTR [zpatch_episode_switch_resume_c]
	ep_c_null:
		mov eax, 0
		ret
	}
}

// @orig 0x1001ef60 HighFPSBikePhysicsFix: saves the incoming value, clamps it,
// restores the registers, then tail-jumps to the resume (match+9).
// @exact 0x1001EF60
__declspec(naked) void abi_bike_physics_detour()
{
	__asm {
		fld DWORD PTR [ebp + 8]
		fstp DWORD PTR [zpatch_bike_value]
		pushad
		call abi_bike_physics_helper
		popad
		mov DWORD PTR [zpatch_bike_saved], eax
		mov eax, DWORD PTR [zpatch_bike_value]
		mov DWORD PTR [ebp + 8], eax
		mov eax, DWORD PTR [zpatch_bike_saved]
		fld DWORD PTR [ebp + 8]
		push ecx
		mov ecx, esi
		fstp DWORD PTR [esp]
		jmp DWORD PTR [zpatch_bike_resume]
	}
}

// @exact 0x10005d80 CollisionDeadlockFix: full replacement of the node-walk
// function the handler matches (prologue 55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48
// 66, rewritten with E9). The per-node predicate published in dword_1015BDD4 is
// NOT __stdcall: it takes the aligned entry pointer in EAX, a2 in EDX and the
// shift mask on the stack, and pops its argument (ret 4). The original also
// restores esp from ebp, discarding the predicate's cleanup. The iteration
// counter (dword_1015BDD8, zpatch_collision_deadlock_iterations) bounds the walk
// at 10000 steps.
__declspec(naked) void abi_collision_deadlock_walk()
{
	__asm {
		mov DWORD PTR [zpatch_collision_deadlock_iterations], 0
		push ebp
		mov ebp, esp
		and esp, 0xFFFFFFF8
		push ecx
		mov eax, DWORD PTR [ebp + 0x0C]
		movzx ecx, WORD PTR [eax + 0x66]
		push ebx
		mov ebx, DWORD PTR [ebp + 0x08]
		mov edx, DWORD PTR [ebx + 0x70]
		push esi
		push edi
		movzx edi, WORD PTR [edx + ecx*8]
		cmp di, 0xFFFF
		je coll_return_minus1
	coll_loop:
		mov ecx, DWORD PTR [ebx + 0x70]
		movzx eax, di
		movzx edx, WORD PTR [ecx + eax*8]
		lea esi, [ecx + eax*8]
		mov eax, DWORD PTR [esi + 4]
		and eax, 0xFFFFFFF0
		cmp dx, 0xFFFF
		je coll_skip_prefetch
		movzx edx, dx
		mov ecx, DWORD PTR [ecx + edx*8 + 4]
		and ecx, 0xFFFFFFF0
		prefetcht0 BYTE PTR [ecx]
	coll_skip_prefetch:
		mov ecx, DWORD PTR [esi + 4]
		and ecx, 3
		mov edx, 1
		shl edx, cl
		push edx
		mov edx, DWORD PTR [ebp + 0x0C]
		call DWORD PTR [zpatch_collision_deadlock_should_walk]
		test al, al
		jne coll_return_di
		movzx edi, WORD PTR [esi]
		add DWORD PTR [zpatch_collision_deadlock_iterations], 1
		; 0x2710 = 10,000: the original's walk-step bound (inline asm needs a literal)
		cmp DWORD PTR [zpatch_collision_deadlock_iterations], 0x2710
		je coll_return_minus1
		cmp di, 0xFFFF
		jne coll_loop
	coll_return_minus1:
		or ax, 0xFFFF
		pop edi
		pop esi
		pop ebx
		mov esp, ebp
		pop ebp
		ret 8
	coll_return_di:
		mov ax, di
		pop edi
		pop esi
		pop ebx
		mov esp, ebp
		pop ebp
		ret 8
	}
}

// @exact 0x10036ee0 BlackscreenFix shim: the handler publishes the original call
// target (dword_1015AD58) and a pointer to the value the game reads
// (dword_1015AD5C). Values 8..10 pass through unchanged; others run the original.
__declspec(naked) void abi_blackscreen_shim()
{
	__asm {
		mov eax, DWORD PTR [zpatch_blackscreen_value_ptr]
		mov eax, DWORD PTR [eax]
		cmp eax, 7
		jle tail
		cmp eax, 11
		jl keep
	tail:
		jmp DWORD PTR [zpatch_blackscreen_resume]
	keep:
		ret
	}
}

// 0x100455a0 SavegameFix read/copy: if (*out >= size) { if (dst) memcpy(dst, src, size);
// *out = size; return 0; } else { *out = size; return 0x8007007A; }
// __stdcall, five arguments (the fifth is unused by the original).
int __stdcall abi_savegame_fix_copy(const void* src, std::uint32_t size, void* dst,
                                    std::uint32_t* out_size, int)
{
	if (*out_size >= size)
	{
		if (dst) std::memcpy(dst, src, size);
		*out_size = size;
		return 0;
	}
	*out_size = size;
	return static_cast<int>(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
}

// @exact 0x1001b080 EpisodeSwitchFreezeFix sum: __thiscall, no stack arguments,
// returns [ecx+10h] + [ecx+0Ch] (0 when this is null).
__declspec(naked) void abi_episode_switch_sum()
{
	__asm {
		cmp ecx, 0
		je skip
		mov eax, [ecx + 0x10]
		add eax, [ecx + 0xc]
		ret
	skip:
		mov eax, 0
		ret
	}
}

// @exact 0x10045C30 SkipIntro body: entered via E9 from match-6 with the site's
// ecx and edx; resumes by jumping to the handler-published match address
// (dword_1015AD74). The original zeroes edx below the 0x1F40 threshold, i.e. the
// intro screens are skipped outright.
__declspec(naked) void abi_skip_intro_body()
{
	__asm {
		; 0x190 = 400 (the game's per-second tick scale) and 0x1f40 = 8000 (the
		; intro-skip threshold); inline asm takes literals for both.
		imul ecx, ecx, 0x190
		cmp edx, 0x1f40
		jge ok
		xor edx, edx
	ok:
		jmp DWORD PTR [zpatch_skip_intro_match]
	}
}

// @exact 0x10046610 SkipMenu body: entered via E8 from the patched site; the
// first call with a zero argument switches the menu id to 6 unconditionally (the
// one-shot flag is the original's byte_101A8450), then resumes by jumping to the
// published address (dword_10159348 = *(match+24) + match + 28).
__declspec(naked) void abi_skip_menu_body()
{
	__asm {
		push ebp
		mov ebp, esp
		cmp BYTE PTR [zpatch_skip_menu_flag], 0
		mov eax, DWORD PTR [ebp + 8]
		jne skip
		test eax, eax
		jne skip
		mov eax, 6
		mov BYTE PTR [zpatch_skip_menu_flag], 1
	skip:
		mov DWORD PTR [ebp + 8], eax
		pop ebp
		jmp DWORD PTR [zpatch_skip_menu_resume]
	}
}

// DLC discovery post step (sub_10023150): enumerate \dlc\tlad\content\ / \TLAD\,
// \dlc\TBoGT\content\ / \TBoGT\ and \EpisodeN\ next to the executable and
// register each through the two published game RVAs. The original writes the
// module base + RVA into the struct and hands it to the register/apply functions.
extern "C" __declspec(naked) void __cdecl zpatch_dlc_apply_capture_edx(
    void*, char*, DlcApplyFn, std::uint32_t*)
{
	__asm {
		push ebp
		mov  ebp, esp
		mov  ecx, dword ptr [ebp + 8]
		push dword ptr [ebp + 0Ch]
		call dword ptr [ebp + 10h]
		mov  eax, dword ptr [ebp + 14h]
		mov  dword ptr [eax], edx
		mov  esp, ebp
		pop  ebp
		ret
	}
}

// The 0x104-byte path buffers sub_10023150 builds for each DLC folder, and the
// request struct it fills: the path copy, the flag at +358 and the mode dword
// at +344.
constexpr std::size_t kDlcPathCapacity = 0x104u;
constexpr std::size_t kDlcRequestCapacity = 360;
constexpr std::size_t kDlcRequestFlagOffset = 358;
constexpr std::size_t kDlcRequestModeOffset = 344;
constexpr std::size_t kDlcObjectFlagOffset = 358;  // object+358 = 2 after the walk
constexpr int kTladDlcId = 2;
constexpr int kTbogtDlcId = 4;
constexpr int kEpisodeChannelStart = 3;
constexpr int kEpisodeChannelEnd = 255;   // one past the last channel
constexpr int kEpisodeChannelScale = 2;
constexpr int kEpisode254Channel = 508;   // 2 * 254

void* dlc_discovery_post(void* object)
{
	// The original allocates this block unchecked and never frees it; owning it
	// only changes its lifetime (no value or fault differs).
	std::unique_ptr<char, OperatorDeleteDeleter> module_path(
	    static_cast<char*>(::operator new(kDlcPathCapacity)));
	GetModuleFileNameA(nullptr, module_path.get(), kDlcPathCapacity);
	PathRemoveFileSpecA(module_path.get());
	std::uint32_t last_apply_edx = 0;
	bool episode254_registered = false;

	// The original picks ONE path per group: the "content" layout if it exists,
	// otherwise the legacy fallback; it never registers both.
	const auto try_paths = [&](const char* primary, const char* fallback, int id)
	{
		std::array<char, kDlcPathCapacity> path;
		strcpy_s(path.data(), path.size(), module_path.get());
		strcat_s(path.data(), path.size(), primary);
		if (!PathFileExistsA(path.data()))
		{
			if (std::strcmp(primary, fallback) == 0) return;
			strcpy_s(path.data(), path.size(), module_path.get());
			strcat_s(path.data(), path.size(), fallback);
			if (!PathFileExistsA(path.data())) return;
		}
		std::array<char, kDlcRequestCapacity> request{};
		strcpy_s(request.data(), kDlcPathCapacity, path.data());
		request[kDlcRequestFlagOffset] = 1;
		*reinterpret_cast<std::uint32_t*>(request.data() + kDlcRequestModeOffset) = 1;
		const auto game_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
		const auto register_fn = reinterpret_cast<DlcRegisterFn>(
		    game_base + DlcRegisterRva());
		const auto apply_fn = reinterpret_cast<DlcApplyFn>(
		    game_base + DlcApplyRva());
		register_fn(id, request.data(), 1);
		zpatch_dlc_apply_capture_edx(object, request.data(), apply_fn, &last_apply_edx);
		if (id == kEpisode254Channel) episode254_registered = true;
	};

	try_paths("\\dlc\\tlad\\content\\", "\\TLAD\\", kTladDlcId);
	try_paths("\\dlc\\TBoGT\\content\\", "\\TBoGT\\", kTbogtDlcId);
	for (int i = kEpisodeChannelStart; i < kEpisodeChannelEnd; ++i)
	{
		std::array<char, kEpisodeSuffixCapacity> suffix;
		zpatch_spawns_sprintf(suffix.data(), suffix.size(), "\\Episode%d\\", i);
		try_paths(suffix.data(), suffix.data(), kEpisodeChannelScale * i);
	}
	*reinterpret_cast<std::uint8_t*>(static_cast<std::uint8_t*>(object) + kDlcObjectFlagOffset) = 2;
	// sub_10023150's final sub_10001280 call leaves its 0x40 buffer size in EDX;
	// PathFileExistsA preserves EDX and the caller's resume observes it. If
	// Episode254 exists, its final apply call is the last EDX producer instead
	// (captured by the asm helper above).
	if (episode254_registered)
	{
		__asm { mov edx, last_apply_edx }
	}
	else
	{
		__asm { mov edx, 40h }
	}
	return object;
}

// @exact 0x10023490 LoadDLCs struct fixup: the object arrives in ESI and the
// epilogue pops esi/ebx and cleans 0x14 stack bytes; the original instruction
// sequence is reproduced (with our own callback slot and post step).
__declspec(naked) void abi_load_dlcs_struct_fixup()
{
	__asm {
		mov BYTE PTR [esi], 0
		mov BYTE PTR [esi + 0x40], 0
		mov BYTE PTR [esi + 0x80], 0
		mov BYTE PTR [esi + 0xc0], 0
		mov BYTE PTR [esi + 0x100], 0
		cmp BYTE PTR [esi + 0x167], 0
		je load
		mov BYTE PTR [esi + 0x168], 1
	exit_label:
		pop esi
		pop ebx
		add esp, 0x14
		ret
	load:
		mov ecx, esi
		call DWORD PTR [zpatch_dlc_load_callback]
		push esi
		call dlc_discovery_post
		pop esi
		mov BYTE PTR [esi + 0xa4], 1
		jmp exit_label
	}
}

// MouseFix (callback 0x10024600): the four axis helpers the callback E9s the
// game's mouse-read functions to (0x10024060/C0/120/170), the two bodies its
// module scan installs (0x10024560 at match-0xF of game 0xA81B1F, 0x100243A0 at
// match+0x746 of the same scan) and the internal 0x10024440 helper it calls.
// Naked: they return in st(0), keep the xmm save/restore contract and round the
// doubles exactly. The one compiled helper (advance = original 0x100241E0) has
// no register contract.
// ---------------------------------------------------------------------------
extern "C" void abi_mf_angle_step();

namespace
{
// The game stores the mouse axes as 1/128 fixed point (2^-7); multiplying by the
// scale is exact, so `v * scale == 0.0` is `v == 0`.
constexpr double kAxisScale = 0.0078125;

// Vehicle fields the resume windows store to (the original's +0x144 / +0x148).
constexpr std::size_t kResumePitchOffset = 0x144;
constexpr std::size_t kResumeYawOffset = kResumePitchOffset + sizeof(float);
static_assert(kResumeYawOffset == 0x148);

// The three windows are resume-relative offsets (resume = the MouseFix handler's
// published match+0x756, dword_10159C40), not function-relative: they are stable
// only because the shape after the hook point is build-invariant.
constexpr std::size_t kPitchWindow = 52;
constexpr std::size_t kYawWindow = 94;
constexpr std::size_t kYawFpuWindow = 107;

// `movss dword ptr [esi+disp], xmmN` = F3 0F 11 /r <disp32>. /r 0x86 is xmm0,
// 0x8E is xmm1; both use the disp32 form (mod = 10, r/m = 110b = esi).
constexpr std::array<std::uint8_t, 8> MovssToEsi(std::uint8_t modrm, std::size_t field) noexcept
{
	return {kMovssStorePrefix,
	        kMovssTwoByteOpcode,
	        kMovssStoreOpcode,
	        modrm,
	        static_cast<std::uint8_t>(field & kByteMask),
	        static_cast<std::uint8_t>((field >> 8) & kByteMask),
	        static_cast<std::uint8_t>((field >> 16) & kByteMask),
	        static_cast<std::uint8_t>((field >> 24) & kByteMask)};
}

// 8x nop: the window's disabled form.
constexpr std::uint8_t kNop = 0x90;
constexpr std::array<std::uint8_t, 8> kEightNops = {kNop, kNop, kNop, kNop,
                                                    kNop, kNop, kNop, kNop};

// Window 0: store pitch from xmm0 (8 bytes exactly: prefix, opcode, modrm, disp32).
constexpr std::array<std::uint8_t, 8> kPitchStore = MovssToEsi(kModrmXmm0Esi, kResumePitchOffset);
// Window 1: store yaw from xmm1.
constexpr std::array<std::uint8_t, 8> kYawStore = MovssToEsi(kModrmXmm1Esi, kResumeYawOffset);
// Window 2: `fstp dword ptr [esi+148h]` (D9 9E + disp32) then `8B 5C`, the head of
// the next instruction, which never changes.
constexpr std::array<std::uint8_t, 8> kYawFpuStore = {
    0xD9,  // fstp dword ptr [esi+disp32]
    0x9E,
    static_cast<std::uint8_t>(kResumeYawOffset & 0xFF),
    static_cast<std::uint8_t>((kResumeYawOffset >> 8) & 0xFF),
    static_cast<std::uint8_t>((kResumeYawOffset >> 16) & 0xFF),
    static_cast<std::uint8_t>((kResumeYawOffset >> 24) & 0xFF),
    kMovEbxHead[0],
    kMovEbxHead[1]};
// Window 2's disabled form is NOT nop fill: DD D8 is `fstp st(0)`. The enclosing
// routine pushes one x87 value and pops it later, so the pop must stay or the x87
// stack overflows after eight calls. Bytes 6-7 are the preserved next instruction.
constexpr std::array<std::uint8_t, 8> kYawFpuNops = {
    kFstpSt0Bytes[0], kFstpSt0Bytes[1], kNop, kNop, kNop, kNop,
    kMovEbxHead[0], kMovEbxHead[1]};

struct ResumeWindow final
{
	std::size_t offset;
	std::array<std::uint8_t, 8> bothAxesZero;
	std::array<std::uint8_t, 8> otherwise;
};

// The recovered byte sequences (research 07 §3.1.1, original sub_100241E0).
// The body verifier reports this compiled replacement as INFO; these asserts are
// its byte proof for the data it writes, so a change is a compile error.
constexpr bool BytesEqual(const std::array<std::uint8_t, 8>& left,
                          const std::array<std::uint8_t, 8>& right) noexcept
{
	for (std::size_t i = 0; i < left.size(); ++i)
		if (left[i] != right[i]) return false;
	return true;
}
static_assert(BytesEqual(kPitchStore, std::array<std::uint8_t, 8>{
                                          0xF3, 0x0F, 0x11, 0x86, 0x44, 0x01, 0x00, 0x00}));
static_assert(BytesEqual(kYawStore, std::array<std::uint8_t, 8>{
                                        0xF3, 0x0F, 0x11, 0x8E, 0x48, 0x01, 0x00, 0x00}));
static_assert(BytesEqual(kEightNops, std::array<std::uint8_t, 8>{
                                         kNop, kNop, kNop, kNop, kNop, kNop, kNop, kNop}));
static_assert(BytesEqual(kYawFpuStore, std::array<std::uint8_t, 8>{
                                           0xD9, 0x9E, 0x48, 0x01, 0x00, 0x00, 0x8B, 0x5C}));
static_assert(BytesEqual(kYawFpuNops, std::array<std::uint8_t, 8>{
                                          0xDD, 0xD8, kNop, kNop, kNop, kNop, 0x8B, 0x5C}));

constexpr ResumeWindow kResumeWindows[] = {
    {kPitchWindow, kPitchStore, kEightNops},
    {kYawWindow, kYawStore, kEightNops},
    {kYawFpuWindow, kYawFpuStore, kYawFpuNops},
};

void zpatch_mf_advance_c() noexcept
{
	// sub_100241E0: when both mouse axes are zero, patch three 8-byte windows of
	// the resumed game function (dword_10159C40 + 52/94/107) with the "pull"
	// stores; otherwise write their disabled forms. VirtualProtect failures are
	// ignored like the original (a null resume would fault; unreachable once
	// installed).
	// sub_100241E0 dereferences the resume slot unconditionally.
	// sub_100241E0 evaluates Y first, then X, and dereferences both unguarded.
	const bool zero = (static_cast<double>(*zpatch_mf_y) * kAxisScale == 0.0) &&
	                  (static_cast<double>(*zpatch_mf_x) * kAxisScale == 0.0);
	for (const ResumeWindow& window : kResumeWindows)
	{
		const auto& bytes = zero ? window.bothAxesZero : window.otherwise;
		// One 8-byte store keeps the window from being half-written.
		injector::WriteMemoryRaw(
		    injector::raw_ptr(reinterpret_cast<std::uintptr_t>(zpatch_mf_resume_b) + window.offset),
		    const_cast<std::uint8_t*>(bytes.data()), bytes.size(), true); //write
	}
	zpatch_cookie_tail();
}
}  // namespace

// @exact 0x10024060 axis-float helper: X/Y * 0.0078125 returned in st(0).
__declspec(naked) void abi_mouse_axis_float()
{
	__asm {
		push ebp
		mov  ebp, esp
		mov  eax, dword ptr [ebp + 0Ch]
		sub  esp, 8
		test eax, eax
		jnz  mf_axis_float_y
		mov  eax, dword ptr [zpatch_mf_x]
		movd xmm0, dword ptr [eax]
		cvtdq2pd xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale]
		movsd qword ptr [ebp - 8], xmm0
		fld  qword ptr [ebp - 8]
		mov  esp, ebp
		pop  ebp
		ret
	mf_axis_float_y:
		cmp  eax, 1
		jnz  mf_axis_float_zero
		mov  eax, dword ptr [zpatch_mf_y]
		movd xmm0, dword ptr [eax]
		cvtdq2pd xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale]
		movsd qword ptr [ebp - 8], xmm0
		fld  qword ptr [ebp - 8]
		mov  esp, ebp
		pop  ebp
		ret
	mf_axis_float_zero:
		fldz
		mov  esp, ebp
		pop  ebp
		ret
		__emit 0CCh
	}
}
// @exact 0x100240C0 negated axis helper: -127.5 * value.
__declspec(naked) void abi_mouse_axis_neg()
{
	__asm {
		push ebp
		mov  ebp, esp
		mov  eax, dword ptr [ebp + 0Ch]
		test eax, eax
		jnz  mf_axis_neg_y
		mov  eax, dword ptr [zpatch_mf_x]
	mf_axis_neg_xy:
		movd xmm0, dword ptr [eax]
		cvtdq2pd xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale]
		jmp  mf_axis_neg_scale
	mf_axis_neg_y:
		cmp  eax, 1
		jnz  mf_axis_neg_zero
		mov  eax, dword ptr [zpatch_mf_y]
		jmp  mf_axis_neg_xy
	mf_axis_neg_zero:
		xorps xmm0, xmm0
	mf_axis_neg_scale:
		movss xmm1, dword ptr [zpatch_mf_sign]
		xorps xmm1, xmmword ptr [zpatch_mf_neg_mask]
		cvtpd2ps xmm0, xmm0
		mulss xmm1, xmm0
		movss dword ptr [ebp + 0Ch], xmm1
		mov  eax, dword ptr [ebp + 0Ch]
		pop  ebp
		ret
		__emit 0CCh
	}
}
// @exact 0x10024120 int axis helper: (int)(value * 0.0078125 * 255).
__declspec(naked) void abi_mouse_axis_int()
{
	__asm {
		push ebp
		mov  ebp, esp
		mov  eax, dword ptr [ebp + 0Ch]
		test eax, eax
		jnz  mf_axis_int_y
		mov  eax, dword ptr [zpatch_mf_x]
	mf_axis_int_xy:
		movd xmm0, dword ptr [eax]
		cvtdq2pd xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale]
		mulsd xmm0, qword ptr [zpatch_mf_scale_255]
		cvttsd2si eax, xmm0
		pop  ebp
		ret
	mf_axis_int_y:
		cmp  eax, 1
		jnz  mf_axis_int_zero
		mov  eax, dword ptr [zpatch_mf_y]
		jmp  mf_axis_int_xy
	mf_axis_int_zero:
		xorps xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale_255]
		cvttsd2si eax, xmm0
		pop  ebp
		ret
		__emit 0CCh
	}
}
// @exact 0x10024170 flagged float helper: negated variant that flips sign again
// when the published game byte is non-zero.
__declspec(naked) void abi_mouse_axis_flag()
{
	__asm {
		push ebp
		mov  ebp, esp
		mov  eax, dword ptr [ebp + 0Ch]
		test eax, eax
		jnz  mf_axis_flag_y
		mov  eax, dword ptr [zpatch_mf_x]
	mf_axis_flag_xy:
		movd xmm0, dword ptr [eax]
		cvtdq2pd xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale]
		jmp  mf_axis_flag_scale
	mf_axis_flag_y:
		cmp  eax, 1
		jnz  mf_axis_flag_zero
		mov  eax, dword ptr [zpatch_mf_y]
		jmp  mf_axis_flag_xy
	mf_axis_flag_zero:
		xorps xmm0, xmm0
	mf_axis_flag_scale:
		movss xmm1, dword ptr [zpatch_mf_sign]
		xorps xmm1, xmmword ptr [zpatch_mf_neg_mask]
		mov  eax, dword ptr [zpatch_mf_flag]
		cvtpd2ps xmm0, xmm0
		cmp  byte ptr [eax], 0
		mulss xmm1, xmm0
		movss dword ptr [ebp + 0Ch], xmm1
		jz   mf_axis_flag_store
		mulss xmm1, dword ptr [zpatch_mf_mul_neg1]
		movss dword ptr [ebp + 0Ch], xmm1
	mf_axis_flag_store:
		mov  eax, dword ptr [ebp + 0Ch]
		pop  ebp
		ret
		__emit 0CCh
	}
}
// @exact 0x10024440 internal FPU helper called by abi_mouse_angle: advances the
// per-vehicle yaw (+0x148) and pitch (+0x144) by the mouse delta scaled by the
// frame-time factor, wrapping yaw into [-pi, pi]. Reads ECX (`this`).
__declspec(naked) void abi_mf_angle_step()
{
	__asm {
		push ebp
		mov  ebp, esp
		and  esp, 0FFFFFFF8h
		sub  esp, 0Ch
		push esi
		mov  esi, ecx
		call dword ptr [zpatch_mf_time]
		movss xmm2, dword ptr [zpatch_mf_step_scale]
		mov  eax, dword ptr [zpatch_mf_x]
		fstp qword ptr [esp + 8]
		movsd xmm0, qword ptr [esp + 8]
		mulsd xmm0, qword ptr [zpatch_mf_time_mul]
		movd xmm4, dword ptr [eax]
		mov  eax, dword ptr [zpatch_mf_y]
		cvtdq2pd xmm4, xmm4
		addsd xmm0, qword ptr [zpatch_mf_time_add]
		movd xmm3, dword ptr [eax]
		mulsd xmm4, qword ptr [zpatch_mf_scale]
		mov  eax, dword ptr [zpatch_mf_flag]
		cvtdq2pd xmm3, xmm3
		cmp  byte ptr [eax], 0
		xorps xmm4, xmmword ptr [zpatch_mf_double_sign]
		mulsd xmm3, qword ptr [zpatch_mf_scale]
		cvtpd2ps xmm0, xmm0
		xorps xmm3, xmmword ptr [zpatch_mf_double_sign]
		mulss xmm2, xmm0
		mulss xmm2, dword ptr [esi + 60h]
		jz   mf_angle_step_skip
		mulsd xmm3, qword ptr [zpatch_mf_mul_neg1_d]
	mf_angle_step_skip:
		movss xmm0, dword ptr [esi + 144h]
		cvtps2pd xmm2, xmm2
		cvtps2pd xmm0, xmm0
		movaps xmm1, xmm2
		mulsd xmm2, xmm4
		mulsd xmm1, xmm3
		addsd xmm1, xmm0
		cvtpd2ps xmm0, xmm1
		movss dword ptr [esi + 144h], xmm0
		movss xmm0, dword ptr [esi + 148h]
		cvtps2pd xmm0, xmm0
		addsd xmm2, xmm0
		cvtpd2ps xmm0, xmm2
		cvtps2pd xmm1, xmm0
		comisd xmm1, qword ptr [zpatch_mf_pi]
		jbe  mf_angle_step_low
		subsd xmm1, qword ptr [zpatch_mf_two_pi]
		cvtpd2ps xmm0, xmm1
		movss dword ptr [esi + 148h], xmm0
		pop  esi
		mov  esp, ebp
		pop  ebp
		ret
	mf_angle_step_low:
		movsd xmm0, qword ptr [zpatch_mf_neg_pi]
		comisd xmm0, xmm1
		jbe  mf_angle_step_store
		addsd xmm1, qword ptr [zpatch_mf_two_pi]
	mf_angle_step_store:
		cvtpd2ps xmm0, xmm1
		movss dword ptr [esi + 148h], xmm0
		pop  esi
		mov  esp, ebp
		pop  ebp
		ret
		__emit 0CCh
	}
}
// @exact 0x10024560 MouseFix body A (installed at game 0xA81B10): saves xmm0-7
// through the plugin's scratch slots, advances the angles, restores them,
// replays the hooked function's 15-byte prologue and tail-jumps to the resume
// the handler published (match, game 0xA81B1F).
__declspec(naked) void abi_mouse_angle()
{
	__asm {
		pushad
		movss dword ptr [zpatch_mf_xmm_save], xmm0
		movss dword ptr [zpatch_mf_xmm_save + 4], xmm1
		movss dword ptr [zpatch_mf_xmm_save + 8], xmm2
		movss dword ptr [zpatch_mf_xmm_save + 12], xmm3
		movss dword ptr [zpatch_mf_xmm_save + 16], xmm4
		movss dword ptr [zpatch_mf_xmm_save + 20], xmm5
		movss dword ptr [zpatch_mf_xmm_save + 24], xmm6
		movss dword ptr [zpatch_mf_xmm_save + 28], xmm7
		call abi_mf_angle_step
		movss xmm0, dword ptr [zpatch_mf_xmm_save]
		movss xmm1, dword ptr [zpatch_mf_xmm_save + 4]
		movss xmm2, dword ptr [zpatch_mf_xmm_save + 8]
		movss xmm3, dword ptr [zpatch_mf_xmm_save + 12]
		movss xmm4, dword ptr [zpatch_mf_xmm_save + 16]
		movss xmm5, dword ptr [zpatch_mf_xmm_save + 20]
		movss xmm6, dword ptr [zpatch_mf_xmm_save + 24]
		movss xmm7, dword ptr [zpatch_mf_xmm_save + 28]
		popad
		push ebp
		mov  ebp, esp
		and  esp, 0FFFFFFF0h
		xorps xmm0, xmm0
		sub  esp, 94h
		jmp  dword ptr [zpatch_mf_resume_a]
		__emit 0CCh
	}
}
// @exact 0x100243A0 MouseFix body B (installed at game 0xA82265): same xmm
// save/restore around the advance helper (sub_100241E0), then replays the three
// replaced instructions and tail-jumps to the published resume (match+0x756,
// game 0xA82275).
__declspec(naked) void abi_mouse_advance()
{
	__asm {
		pushad
		movss dword ptr [zpatch_mf_xmm_save], xmm0
		movss dword ptr [zpatch_mf_xmm_save + 4], xmm1
		movss dword ptr [zpatch_mf_xmm_save + 8], xmm2
		movss dword ptr [zpatch_mf_xmm_save + 12], xmm3
		movss dword ptr [zpatch_mf_xmm_save + 16], xmm4
		movss dword ptr [zpatch_mf_xmm_save + 20], xmm5
		movss dword ptr [zpatch_mf_xmm_save + 24], xmm6
		movss dword ptr [zpatch_mf_xmm_save + 28], xmm7
		call zpatch_mf_advance_c
		movss xmm0, dword ptr [zpatch_mf_xmm_save]
		movss xmm1, dword ptr [zpatch_mf_xmm_save + 4]
		movss xmm2, dword ptr [zpatch_mf_xmm_save + 8]
		movss xmm3, dword ptr [zpatch_mf_xmm_save + 12]
		movss xmm4, dword ptr [zpatch_mf_xmm_save + 16]
		movss xmm5, dword ptr [zpatch_mf_xmm_save + 20]
		movss xmm6, dword ptr [zpatch_mf_xmm_save + 24]
		movss xmm7, dword ptr [zpatch_mf_xmm_save + 28]
		popad
		mulss xmm0, xmm2
		addss xmm0, xmm3
		movss xmm3, dword ptr [esi + 150h]
		jmp  dword ptr [zpatch_mf_resume_b]
		__emit 0CCh
	}
}

// The branch body stays here; the decoder, fake-pak registration and
// archive builder it belongs to are in source/ooc.cpp.

// @exact 0x1004C3C0 OutOfCommissionFix branch body (E9 target of game
// 0xA22E97): replay `cmp [esp+0x30], eax` + `movq xmm0, [esp+0x30]`; when the
// comparison was unequal, re-compare against the original's 0x86723DFF magic so
// the game's following `jz` also accepts the injected value; resume at match+0x11.
__declspec(naked) void abi_ooc_branch()
{
	__asm {
		cmp  dword ptr [esp + 30h], eax
		movq xmm0, qword ptr [esp + 30h]
		jne  ooc_branch_check
		jmp  dword ptr [zpatch_ooc_resume]
	ooc_branch_check:
		mov  eax, 86723DFFh
		cmp  dword ptr [esp + 30h], eax
		jmp  dword ptr [zpatch_ooc_resume]
		__emit 0CCh
	}
}


// HighQualityReflections (callback 0x1001F390): scans for
// `8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8` (unique on every classic build
// and EFLC 1110/1120/1130; absent on 12043), E9s the match to the detour below,
// and publishes dword_10159CFC = match+0x13 (resume) and
// dword_10159D04 = match + *(match+0xF) + 0x13 (the draw-command builder called).
// @exact 0x1001F030 detour body: dumps the registers (the original's crash aid),
// calls the game's draw-command builder 12 times with (index 0..11, flags
// 0x11D, mode 0) and resumes at match+0x13 (tail-jumped).
__declspec(naked) void abi_hqr_body()
{
	__asm {
			mov       dword ptr [zpatch_hqr_dump], eax
			mov       dword ptr [zpatch_hqr_dump+4], ebx
			mov       dword ptr [zpatch_hqr_dump+8], ecx
			mov       dword ptr [zpatch_hqr_dump+12], edx
			mov       dword ptr [zpatch_hqr_dump+16], esi
			mov       dword ptr [zpatch_hqr_dump+20], edi
			mov       dword ptr [zpatch_hqr_dump+24], ebp
			mov       dword ptr [zpatch_hqr_dump+28], esp
			mov       eax, 0
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 1
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 2
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 3
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 4
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 5
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 6
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 7
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 8
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 9
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 0xa
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 0xb
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			jmp       dword ptr [zpatch_hqr_resume]
			__emit 0CCh
	}
}

// The worker's sub_10003980: the resolver call with the salt in ECX and ESI.
extern "C" __declspec(naked) std::uint32_t zpatch_cutscene_scene()
{
	__asm {
		push esi
		mov esi, ecx
		mov eax, dword ptr [zpatch_driveby_resolver]
		call eax
		pop esi
		ret
	}
}
// The worker's sub_1005FE7B cookie check (cookie in ECX), same body as the
// spawns probe's: it must preserve EAX/AL because the cutscene body returns
// its result in AL across this call.
__declspec(naked) void zpatch_cutscene_cookie_check()
{
	__asm {
		jmp zpatch_spawns_cookie_check_ecx
	}
}
// 0x100388C0 CutsceneFixes chain B (original sub_100388C0..0x10038B3F, the dest
// of the E8 installs the worker 0x10038B40 writes at the two camera call sites).
// thiscall char(float). Reads a scene float through the loader-published resolver
// (salt dword_1015803C, ecx/esi), pokes the camera function's `mov byte
// [esp+0x1C],imm` at target+0x4E, calls it and, once the accumulated frame time
// passes 0.3333, repeats with the immediate 0 and restores the pre-call object
// when the position floats differ beyond (0.05,0.05,0.05,0.5,0.5,0.5).
// @exact 0x100388C0
__declspec(naked) void abi_cutscene_chain_b()
{
	__asm {
		push ebp
		mov ebp, esp
		sub esp, 0A18h
		mov eax, dword ptr [__security_cookie]
		xor eax, ebp
		mov dword ptr [ebp - 4], eax
		push esi
		mov esi, ecx
		mov ecx, dword ptr [zpatch_salt_3c]
		push edi
		mov dword ptr [ebp - 0A10h], esi
		call zpatch_cutscene_scene
		lea edi, [ebp - 504h]
		mov ecx, dword ptr [eax + 0Eh]
		mov eax, dword ptr [ecx + eax + 18h]
		mov ecx, 140h
		rep movsd
		movss xmm0, dword ptr [eax + 8]
		lea eax, [ebp - 0A08h]
		mov esi, dword ptr [zpatch_cutscene_poke]
		mov edi, dword ptr [zpatch_cutscene_virtualprotect]
		dec esi
		cvtps2pd xmm0, xmm0
		push eax
		push 40h
		push 1
		push esi
		addsd xmm0, qword ptr [zpatch_cutscene_accum]
		movsd qword ptr [zpatch_cutscene_accum], xmm0
		call edi
		lea eax, [ebp - 0A08h]
		mov byte ptr [esi], 1
		push eax
		push dword ptr [ebp - 0A08h]
		push 1
		push esi
		call edi
		mov eax, dword ptr [zpatch_cutscene_target]
		movss xmm0, dword ptr [ebp + 8]
		push ecx
		mov ecx, dword ptr [ebp - 0A10h]
		mov dword ptr [ebp - 0A14h], eax
		movss dword ptr [esp], xmm0
		call dword ptr [ebp - 0A14h]
		fstp qword ptr [ebp - 0A18h]
		movsd xmm0, qword ptr [ebp - 0A18h]
		ucomisd xmm0, qword ptr [zpatch_cutscene_zero]
		lahf
		test ah, 44h
		jnp ccb_al_zero
		mov al, 1
		jmp ccb_al_done
	ccb_al_zero:
		xor al, al
	ccb_al_done:
		mov edx, dword ptr [ebp - 0A10h]
		lea edi, [ebp - 0A04h]
		movsd xmm0, qword ptr [zpatch_cutscene_limit]
		mov ecx, 140h
		comisd xmm0, qword ptr [zpatch_cutscene_accum]
		mov esi, edx
		mov byte ptr [ebp - 0A09h], al
		rep movsd
		ja ccb_return_al
		mov edi, edx
		lea eax, [ebp - 0A08h]
		push eax
		mov ecx, 140h
		lea esi, [ebp - 504h]
		rep movsd
		mov esi, dword ptr [zpatch_cutscene_poke]
		mov edi, dword ptr [zpatch_cutscene_virtualprotect]
		dec esi
		push 40h
		push 1
		push esi
		call edi
		lea eax, [ebp - 0A08h]
		mov byte ptr [esi], 0
		push eax
		push dword ptr [ebp - 0A08h]
		push 1
		push esi
		call edi
		mov eax, dword ptr [zpatch_cutscene_target]
		movss xmm0, dword ptr [ebp + 8]
		push ecx
		mov ecx, dword ptr [ebp - 0A10h]
		mov dword ptr [ebp - 0A14h], eax
		movss dword ptr [esp], xmm0
		call dword ptr [ebp - 0A14h]
		fstp qword ptr [ebp - 0A18h]
		movsd xmm0, qword ptr [ebp - 0A18h]
		xorps xmm3, xmm3
		ucomisd xmm0, xmm3
		lahf
		test ah, 44h
		jnp ccb_dl_zero
		mov dl, 1
		jmp ccb_dl_done
	ccb_dl_zero:
		xor dl, dl
	ccb_dl_done:
		mov eax, dword ptr [ebp - 0A10h]
		lea edi, [ebp - 504h]
		movss xmm1, dword ptr [zpatch_cutscene_abs_mask]
		mov ecx, 140h
		movss xmm2, dword ptr [zpatch_cutscene_eps_a]
		mov esi, eax
		rep movsd
		movss xmm0, dword ptr [ebp - 4E4h]
		subss xmm0, dword ptr [ebp - 9E4h]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		movss xmm0, dword ptr [ebp - 4E0h]
		subss xmm0, dword ptr [ebp - 9E0h]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		movss xmm0, dword ptr [ebp - 4DCh]
		subss xmm0, dword ptr [ebp - 9DCh]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		movss xmm0, dword ptr [ebp - 4C4h]
		subss xmm0, dword ptr [ebp - 9C4h]
		movss xmm2, dword ptr [zpatch_cutscene_eps_b]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		movss xmm0, dword ptr [ebp - 4C0h]
		subss xmm0, dword ptr [ebp - 9C0h]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		movss xmm0, dword ptr [ebp - 4BCh]
		subss xmm0, dword ptr [ebp - 9BCh]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		pop edi
		mov al, dl
		pop esi
		mov ecx, dword ptr [ebp - 4]
		xor ecx, ebp
		call zpatch_cutscene_cookie_check
		mov esp, ebp
		pop ebp
		ret 4
	ccb_restore:
		mov edi, eax
		movsd qword ptr [zpatch_cutscene_accum], xmm3
		mov al, byte ptr [ebp - 0A09h]
		lea esi, [ebp - 0A04h]
		mov ecx, 140h
		rep movsd
	ccb_return_al:
		mov ecx, dword ptr [ebp - 4]
		pop edi
		xor ecx, ebp
		pop esi
		call zpatch_cutscene_cookie_check
		mov esp, ebp
		pop ebp
		ret 4
		__emit 0CCh
	}
}

// NewMemorySystem's allocator vtable, small methods: the original's
// calling conventions are preserved; only the constructor and the thunk
// need their exact prologue/tail-jump. The allocator, its registry and the
// vtable are in source/nms_allocator.cpp.
extern "C" void __cdecl abi_nms_noop_ret0() {}

// 0x10038640: constructor (thiscall; one stack argument, callee pops it).
extern "C" __declspec(naked) void* abi_nms_ctor()
{
	__asm {
		push ebp
		mov ebp, esp
		mov eax, dword ptr [ebp + 8]
		push esi
		mov esi, ecx
		and eax, 5
		mov dword ptr [esi], offset zpatch_nms_vtable
		cmp al, 5
		jne nms_ctor_done
		push 4
		push esi
		call abi_nms_noop_ret0
		add esp, 8
	nms_ctor_done:
		mov eax, esi
		pop esi
		pop ebp
		ret 4
		__emit 0CCh
	}
}

// 0x10038350
extern "C" bool __stdcall abi_nms_reject_ptr(void*) { return false; }

// 0x10038540
extern "C" void* __fastcall abi_nms_self(void* self, void* /*edx*/, void*)
{
	return self;
}

// 0x10001950
extern "C" void* __fastcall abi_nms_zero_ptr(void* /*self*/, void* /*edx*/, void*)
{
	return nullptr;
}

// 0x10038580
extern "C" void __stdcall abi_nms_noop_pair(void*, void*) {}

// 0x10038590
extern "C" bool __cdecl abi_nms_reject() { return false; }

// 0x10038620: forward through vtable[6].
// Tail-jumps, so the caller's return address and EBP stay untouched.
extern "C" __declspec(naked) void abi_nms_thunk18()
{
	__asm {
		push ebp
		mov ebp, esp
		mov eax, dword ptr [ecx]
		pop ebp
		jmp dword ptr [eax + 18h]
		__emit 0CCh
	}
}

// 0x10038630
extern "C" void __stdcall abi_nms_noop_arg(void*) {}


#endif  // !ZP_DUMP_TOOL — assembly bodies are ASI-only; the dump uses stubs


// The original ASI's handler/body addresses, named. The resolver keys on the
// original VA, so each value is an identity label, not a patch address: the name
// is what the table is for, and the comment names the original function (and
// what the body does). `@exact` on a body says the body is a byte-exact
// transcription of that original function; tools/verify_bodies.py proves it.
enum class BodyId : std::uint32_t
{
    aim_projectile_crash_fix = 0x10005590u,  // sub_10005590: AimProjectileCrashFix (__thiscall, ret 12)
    better_mp_sync = 0x100057f0u,  // sub_100057f0: BetterMPSync (xor eax,eax / ret)
    combined_radio_freeze_fix = 0x10005ed0u,  // sub_10005ed0: CombinedRadioFreezeFix (mov eax,13h / ret)
    carparts_shadow = 0x10006880u,  // sub_10006880: CarPartsShadowFix shim (NOPs are original bytes)
    popup_headlights = 0x10044530u,  // sub_10044530: PopupHeadlightsFlag, first site
    popup_headlights_edx = 0x10044560u,  // sub_10044560: PopupHeadlightsFlag, EDX twin
    fchs_shim = 0x1001adb0u,  // sub_1001adb0: ForceCarHeadlightShadows thunk
    dual_headlights_new = 0x100064f0u,  // sub_100064f0: DualVehicleHeadlights, new-game body
    dual_headlights_old = 0x10006630u,  // sub_10006630: DualVehicleHeadlights, old-game body
    car_decals_fix = 0x1001b2e0u,  // sub_1001b2e0: CarDecalsFix (scan-based replacement)
    bad_player_model = 0x10037ba0u,  // sub_10037ba0: BadPlayerModelCrashFix trampoline
    death_music_install = 0x10050ad0u,  // sub_10050ad0: RestoreDeathMusic installer body
    death_music_enabled = 0x10050a50u,  // sub_10050a50: RestoreDeathMusic gate body
    new_saves_compat = 0x100521e0u,  // sub_100521e0: NewSavesCompatibility savegame-load tramp
    gfwl_stub = 0x100054e0u,  // sub_100054e0: Patch4RemoveModChecks / RemoveGFWLUpdatePopup stub
    nolivery_e = 0x1003d3e0u,  // sub_1003d3e0: NoLiveryLimit body E (ESI object, name = &edi[0x13c])
    carparts_shadow_2 = 0x100068a0u,  // sub_100068a0: CarPartsShadowFix other slot
    carparts_shadow_2b = 0x100068d0u,  // sub_100068d0: CarDynamicShadowFix body B (pattern-B)
    garage_body_a = 0x10046c80u,  // sub_10046c80: GarageModelFix body A (installed at game 0xC68BB0)
    garage_body_b = 0x10046ca0u,  // sub_10046ca0: GarageModelFix body B (installed at game 0x9659C3)
    steer_detour_eax = 0x100051b0u,  // sub_100051b0: MovingSteeringWheelSupport detour (EAX entry)
    steer_detour_edx = 0x100051e0u,  // sub_100051e0: MovingSteeringWheelSupport fallback detour (EDX entry)
    local_settings_detour = 0x100512f0u,  // sub_100512f0: LocalSettingsPath detour
    local_settings_fallback_detour = 0x10051300u,  // sub_10051300: LocalSettingsPath fallback detour
    spawns_probe = 0x10037510u,  // sub_10037510: FreezeCarFix/DebugPrintSpawns probe (394 insns)
    spawns_guard = 0x100372f0u,  // sub_100372f0: FreezeCarFix / DebugPrintSpawns guard
    spawns_case_0x24 = 0x10037330u,  // sub_10037330: spawns dispatch case 0x24
    spawns_case_0x3D = 0x100373f0u,  // sub_100373f0: spawns dispatch case 0x3D
    spawns_case_0x56 = 0x10037420u,  // sub_10037420: spawns dispatch case 0x56
    spawns_case_0x6F = 0x10037390u,  // sub_10037390: spawns dispatch case 0x6F
    spawns_case_0x88 = 0x10037450u,  // sub_10037450: spawns dispatch case 0x88
    spawns_case_0x9E = 0x10037480u,  // sub_10037480: spawns dispatch case 0x9E
    spawns_case_0xB4 = 0x100374e0u,  // sub_100374e0: spawns dispatch case 0xB4
    spawns_case_0xCA = 0x10037360u,  // sub_10037360: spawns dispatch case 0xCA
    spawns_case_0xE0 = 0x100373c0u,  // sub_100373c0: spawns dispatch case 0xE0
    spawns_case_0xF6 = 0x100374b0u,  // sub_100374b0: spawns dispatch case 0xF6
    game_hash = 0x10003e50u,  // sub_10003e50: AllowHashesForIDE model-name hash replacement
    ped_models_in_cutscenes_check = 0x10050870u,  // sub_10050870: PedModelsInCutscenesFix player-model check
    improved_shader_streaming_find_hpe = 0x10045a30u,  // sub_10045a30: ImprovedShaderStreaming HPE filter
    improved_shader_streaming_find_v = 0x10045a80u,  // sub_10045a80: ImprovedShaderStreaming V filter
    vram_fix_shim = 0x1004aab0u,  // sub_1004aab0: VRAMFix argument/verdict shim
    nolivery_a = 0x1003d310u,  // sub_1003d310: NoLiveryLimit body A (name = &ebx[0x13c], index = eax)
    nolivery_b = 0x1003d340u,  // sub_1003d340: NoLiveryLimit body B (name = &ebx[0x13c], index = ecx)
    nolivery_c = 0x1003d370u,  // sub_1003d370: NoLiveryLimit body C (drops 8 bytes of args)
    nolivery_d = 0x1003d3a0u,  // sub_1003d3a0: NoLiveryLimit body D (16-byte name copy, counts _sign_N)
    crash_msg_shim = 0x10036f00u,  // sub_10036f00: CrashMsgFix message-getter shim
    newfiles_register_body = 0x10051600u,  // sub_10051600: NewFilesCompatibility register body
    newfiles_update_a = 0x10051890u,  // sub_10051890: NewFilesCompatibility update A
    newfiles_update_b = 0x100518d0u,  // sub_100518d0: NewFilesCompatibility update B
    benchmark_a = 0x10005650u,  // sub_10005650: BenchmarkFix shim A (E9 at match[0])
    benchmark_b = 0x10005670u,  // sub_10005670: BenchmarkFix shim B (E9 at match[1]+8)
    building_dynamic_shadows = 0x10005af0u,  // sub_10005af0: BuildingDynamicShadows shim
    copsirens_a = 0x10006310u,  // sub_10006310: CopSirensFlag shim A
    copsirens_b = 0x10006330u,  // sub_10006330: CopSirensFlag shim B
    copsirens_ide = 0x100040e0u,  // sub_100040e0: CopSirensFlag IDE-hook body
    borderless_w = 0x10005890u,  // sub_10005890: BorderlessWindowed Unicode window-proc shim
    borderless_a = 0x10005900u,  // sub_10005900: BorderlessWindowed ANSI window-proc shim
    speedup_body = 0x10050540u,  // sub_10050540: SpeedupBugFix timing-helper replacement
    episode_switch_sum = 0x1001b080u,  // sub_1001b080: EpisodeSwitchFreezeFix sum body (__thiscall)
    load_dlcs_dummy = 0x10023480u,  // sub_10023480: LoadDLCs dummy (mov al,1 / ret 8)
    load_dlcs_struct_fixup = 0x10023490u,  // sub_10023490: LoadDLCs struct fixup (ESI object, cleans 0x14)
    skip_intro_body = 0x10045c30u,  // sub_10045c30: SkipIntro body (E9 from match-6)
    skip_menu_body = 0x10046610u,  // sub_10046610: SkipMenu body (E8 from the patched site)
    rgsc_login_removed = 0x10038550u,  // sub_10038550: RemoveRGSCLoginRequirement (mov al,1 / ret)
    savegame_fix_null = 0x10038570u,  // sub_10038570: SavegameFix null path (xor eax,eax / ret 8)
    nms_ctor = 0x10038640u,  // sub_10038640: NewMemorySystem CRageAllocator constructor
    nms_reject_ptr = 0x10038350u,  // sub_10038350: NewMemorySystem allocator vtable reject-pointer slot
    nms_self = 0x10038540u,  // sub_10038540: NewMemorySystem allocator vtable self slot
    nms_zero_ptr = 0x10001950u,  // sub_10001950: NewMemorySystem allocator vtable zero-pointer slot
    nms_noop_ret0 = 0x10038560u,  // sub_10038560: NewMemorySystem allocator vtable no-op returning 0
    nms_noop_pair = 0x10038580u,  // sub_10038580: NewMemorySystem allocator vtable paired no-op
    nms_reject = 0x10038590u,  // sub_10038590: NewMemorySystem allocator vtable reject
    nms_thunk18 = 0x10038620u,  // sub_10038620: NewMemorySystem allocator vtable forwarder (slot 18)
    nms_noop_arg = 0x10038630u,  // sub_10038630: NewMemorySystem allocator vtable no-op with argument
    nms_apply = 0x10038670u,  // sub_10038670: NewMemorySystem vtable install
    savegame_fix_close = 0x10045580u,  // sub_10045580: SavegameFix CloseHandle path (ret 4)
    savegame_fix_copy = 0x100455a0u,  // sub_100455a0: SavegameFix read/copy path
    savegame_fix_mutex = 0x100455e0u,  // sub_100455e0: SavegameFix CreateMutex path
    blackscreen_shim = 0x10036ee0u,  // sub_10036ee0: BlackscreenFix shim
    path_node_shim = 0x1003d7b0u,  // sub_1003d7b0: PathNodeCrashFix shim (sentinel 0x1FFFDF)
    plane_crash_shim = 0x1003d870u,  // sub_1003d870: PlaneCrashFix shim
    wanted_level_shim = 0x10050490u,  // sub_10050490: WantedLevelCrashFix shim (null returns 0)
    group_hack_shim = 0x10036e90u,  // sub_10036e90: GroupHackFix shim
    smpa60_shim = 0x10046750u,  // sub_10046750: SMPA60Fix shim (sentinel 0xDDDDDDDD)
    smpa60_tail_shim = 0x10046780u,  // sub_10046780: SMPA60Fix tail shim
    freeze_gun_shim = 0x10036eb0u,  // sub_10036eb0: FreezeGunFix shim
    player_ped_components_shim = 0x1003d9a0u,  // sub_1003d9a0: PlayerAsPedComponentsFix shim
    emissive_lerp_shim = 0x1001af60u,  // sub_1001af60: EmissiveLerpFix shim
    super_lod_fix_shim = 0x100469e0u,  // sub_100469e0: SuperLODFix shim (calls a helper)
    episode_switch_store_shim = 0x1001b040u,  // sub_1001b040: EpisodeSwitchFreezeFix store shim
    episode_switch_shim_b = 0x1001b060u,  // sub_1001b060: EpisodeSwitchFreezeFix shim B
    episode_switch_vcall = 0x1001b0a0u,  // sub_1001b0a0: EpisodeSwitchFreezeFix virtual call shim
    episode_switch_shim_c = 0x1001b0c0u,  // sub_1001b0c0: EpisodeSwitchFreezeFix shim C
    bike_physics_detour = 0x1001ef60u,  // sub_1001ef60: HighFPSBikePhysicsFix detour
    collision_deadlock_walk = 0x10005d80u,  // sub_10005d80: CollisionDeadlockFix replacement
    mouse_axis_float = 0x10024060u,  // sub_10024060: MouseFix axis-float helper
    mouse_axis_neg = 0x100240c0u,  // sub_100240c0: MouseFix negated-axis helper
    mouse_axis_int = 0x10024120u,  // sub_10024120: MouseFix int-axis helper
    mouse_axis_flag = 0x10024170u,  // sub_10024170: MouseFix flagged-axis helper
    mf_angle_step = 0x10024440u,  // sub_10024440: MouseFix angle-step helper
    mouse_angle = 0x10024560u,  // sub_10024560: MouseFix body A (installed at game 0xA81B10)
    mouse_advance = 0x100243a0u,  // sub_100243a0: MouseFix body B (installed at game 0xA82265)
    ooc_branch = 0x1004c3c0u,  // sub_1004c3c0: OutOfCommissionFix branch body
    ooc_wrap = 0x1004c350u,  // sub_1004c350: OutOfCommissionFix wrapper
    hqr_body = 0x1001f030u,  // sub_1001f030: HighQualityReflections body
    cutscene_chain_b = 0x100388c0u,  // sub_100388c0: CutsceneFixes chain B
    driveby_fov_shim = 0x100064b0u,  // sub_100064b0: DrivebyFOVFix shim
};

constexpr std::uint32_t OriginalHandlerVa(BodyId id) noexcept
{
	return static_cast<std::uint32_t>(id);
}

struct AbiBody final
{
	BodyId id;
	void (*address)();
};

// Only destinations whose conventions are verified appear here; everything else
// resolves to 0 and the installer reports it as skipped.
const AbiBody kAbiBodies[] =
{
		{BodyId::aim_projectile_crash_fix, reinterpret_cast<void(*)()>(&abi_aim_projectile_crash_fix)},
		{BodyId::better_mp_sync, reinterpret_cast<void(*)()>(&abi_better_mp_sync)},
		{BodyId::combined_radio_freeze_fix, reinterpret_cast<void(*)()>(&abi_combined_radio_freeze_fix)},
		{BodyId::carparts_shadow, reinterpret_cast<void(*)()>(&abi_carparts_shadow)},
		{BodyId::popup_headlights, reinterpret_cast<void(*)()>(&abi_popup_headlights)},
		{BodyId::popup_headlights_edx, reinterpret_cast<void(*)()>(&abi_popup_headlights_edx)},
		{BodyId::fchs_shim, reinterpret_cast<void(*)()>(&abi_fchs_shim)},
		{BodyId::dual_headlights_new, reinterpret_cast<void(*)()>(&abi_dual_headlights_new)},
		{BodyId::dual_headlights_old, reinterpret_cast<void(*)()>(&abi_dual_headlights_old)},
		{BodyId::car_decals_fix, reinterpret_cast<void(*)()>(&abi_car_decals_fix)},
		{BodyId::bad_player_model, reinterpret_cast<void(*)()>(&abi_bad_player_model)},
		{BodyId::death_music_install, reinterpret_cast<void(*)()>(&abi_death_music_install)},
		{BodyId::death_music_enabled, reinterpret_cast<void(*)()>(&abi_death_music_enabled)},
		{BodyId::new_saves_compat, reinterpret_cast<void(*)()>(&abi_new_saves_compat)},
		{BodyId::gfwl_stub, reinterpret_cast<void(*)()>(&abi_gfwl_stub)},
		{BodyId::nolivery_e, reinterpret_cast<void(*)()>(&abi_nolivery_e)},
		{BodyId::carparts_shadow_2, reinterpret_cast<void(*)()>(&abi_carparts_shadow_2)},
		{BodyId::carparts_shadow_2b, reinterpret_cast<void(*)()>(&abi_carparts_shadow_2b)},
		{BodyId::garage_body_a, reinterpret_cast<void(*)()>(&abi_garage_body_a)},
		{BodyId::garage_body_b, reinterpret_cast<void(*)()>(&abi_garage_body_b)},
		{BodyId::steer_detour_eax, reinterpret_cast<void(*)()>(&abi_steer_detour_eax)},
		{BodyId::steer_detour_edx, reinterpret_cast<void(*)()>(&abi_steer_detour_edx)},
		{BodyId::local_settings_detour, reinterpret_cast<void(*)()>(&abi_local_settings_detour)},
		{BodyId::local_settings_fallback_detour, reinterpret_cast<void(*)()>(&abi_local_settings_fallback_detour)},
		{BodyId::spawns_probe, reinterpret_cast<void(*)()>(&abi_spawns_probe)},
		{BodyId::spawns_guard, reinterpret_cast<void(*)()>(&abi_spawns_guard)},
		{BodyId::spawns_case_0x24, reinterpret_cast<void(*)()>(&abi_spawns_case_0x24)},
		{BodyId::spawns_case_0x3D, reinterpret_cast<void(*)()>(&abi_spawns_case_0x3D)},
		{BodyId::spawns_case_0x56, reinterpret_cast<void(*)()>(&abi_spawns_case_0x56)},
		{BodyId::spawns_case_0x6F, reinterpret_cast<void(*)()>(&abi_spawns_case_0x6F)},
		{BodyId::spawns_case_0x88, reinterpret_cast<void(*)()>(&abi_spawns_case_0x88)},
		{BodyId::spawns_case_0x9E, reinterpret_cast<void(*)()>(&abi_spawns_case_0x9E)},
		{BodyId::spawns_case_0xB4, reinterpret_cast<void(*)()>(&abi_spawns_case_0xB4)},
		{BodyId::spawns_case_0xCA, reinterpret_cast<void(*)()>(&abi_spawns_case_0xCA)},
		{BodyId::spawns_case_0xE0, reinterpret_cast<void(*)()>(&abi_spawns_case_0xE0)},
		{BodyId::spawns_case_0xF6, reinterpret_cast<void(*)()>(&abi_spawns_case_0xF6)},
		{BodyId::game_hash, reinterpret_cast<void(*)()>(&abi_game_hash)},
		{BodyId::ped_models_in_cutscenes_check, reinterpret_cast<void(*)()>(&abi_ped_models_in_cutscenes_check)},
		{BodyId::improved_shader_streaming_find_hpe,
		 reinterpret_cast<void(*)()>(&abi_improved_shader_streaming_find_hpe)},
		{BodyId::improved_shader_streaming_find_v,
		 reinterpret_cast<void(*)()>(&abi_improved_shader_streaming_find_v)},
		{BodyId::vram_fix_shim, reinterpret_cast<void(*)()>(&abi_vram_fix_shim)},
		{BodyId::nolivery_a, reinterpret_cast<void(*)()>(&abi_nolivery_a)},
		{BodyId::nolivery_b, reinterpret_cast<void(*)()>(&abi_nolivery_b)},
		{BodyId::nolivery_c, reinterpret_cast<void(*)()>(&abi_nolivery_c)},
		{BodyId::nolivery_d, reinterpret_cast<void(*)()>(&abi_nolivery_d)},
		{BodyId::crash_msg_shim, reinterpret_cast<void(*)()>(&abi_crash_msg_shim)},
		{BodyId::newfiles_register_body, reinterpret_cast<void(*)()>(&abi_newfiles_register_body)},
		{BodyId::newfiles_update_a, reinterpret_cast<void(*)()>(&abi_newfiles_update_a)},
		{BodyId::newfiles_update_b, reinterpret_cast<void(*)()>(&abi_newfiles_update_b)},
		{BodyId::benchmark_a, reinterpret_cast<void(*)()>(&abi_benchmark_a)},
		{BodyId::benchmark_b, reinterpret_cast<void(*)()>(&abi_benchmark_b)},
		{BodyId::building_dynamic_shadows, reinterpret_cast<void(*)()>(&abi_building_dynamic_shadows)},
		{BodyId::copsirens_a, reinterpret_cast<void(*)()>(&abi_copsirens_a)},
		{BodyId::copsirens_b, reinterpret_cast<void(*)()>(&abi_copsirens_b)},
		{BodyId::copsirens_ide, reinterpret_cast<void(*)()>(&abi_copsirens_ide)},
		{BodyId::borderless_w, reinterpret_cast<void(*)()>(&abi_borderless_w)},
		{BodyId::borderless_a, reinterpret_cast<void(*)()>(&abi_borderless_a)},
		{BodyId::speedup_body, reinterpret_cast<void(*)()>(&abi_speedup_body)},
		{BodyId::episode_switch_sum, reinterpret_cast<void(*)()>(&abi_episode_switch_sum)},
		{BodyId::load_dlcs_dummy, reinterpret_cast<void(*)()>(&abi_load_dlcs_dummy)},
		{BodyId::load_dlcs_struct_fixup, reinterpret_cast<void(*)()>(&abi_load_dlcs_struct_fixup)},
		{BodyId::skip_intro_body, reinterpret_cast<void(*)()>(&abi_skip_intro_body)},
		{BodyId::skip_menu_body, reinterpret_cast<void(*)()>(&abi_skip_menu_body)},
		{BodyId::rgsc_login_removed, reinterpret_cast<void(*)()>(&abi_rgsc_login_removed)},
		{BodyId::savegame_fix_null, reinterpret_cast<void(*)()>(&abi_savegame_fix_null)},
		{BodyId::nms_ctor, reinterpret_cast<void(*)()>(&abi_nms_ctor)},
		{BodyId::nms_reject_ptr, reinterpret_cast<void(*)()>(&abi_nms_reject_ptr)},
		{BodyId::nms_self, reinterpret_cast<void(*)()>(&abi_nms_self)},
		{BodyId::nms_zero_ptr, reinterpret_cast<void(*)()>(&abi_nms_zero_ptr)},
		{BodyId::nms_noop_ret0, reinterpret_cast<void(*)()>(&abi_nms_noop_ret0)},
		{BodyId::nms_noop_pair, reinterpret_cast<void(*)()>(&abi_nms_noop_pair)},
		{BodyId::nms_reject, reinterpret_cast<void(*)()>(&abi_nms_reject)},
		{BodyId::nms_thunk18, reinterpret_cast<void(*)()>(&abi_nms_thunk18)},
		{BodyId::nms_noop_arg, reinterpret_cast<void(*)()>(&abi_nms_noop_arg)},
		{BodyId::nms_apply, reinterpret_cast<void(*)()>(&abi_nms_apply)},
		{BodyId::savegame_fix_close, reinterpret_cast<void(*)()>(&abi_savegame_fix_close)},
		{BodyId::savegame_fix_copy, reinterpret_cast<void(*)()>(&abi_savegame_fix_copy)},
		{BodyId::savegame_fix_mutex, reinterpret_cast<void(*)()>(&abi_savegame_fix_mutex)},
		{BodyId::blackscreen_shim, reinterpret_cast<void(*)()>(&abi_blackscreen_shim)},
		{BodyId::path_node_shim, reinterpret_cast<void(*)()>(&abi_path_node_shim)},
		{BodyId::plane_crash_shim, reinterpret_cast<void(*)()>(&abi_plane_crash_shim)},
		{BodyId::wanted_level_shim, reinterpret_cast<void(*)()>(&abi_wanted_level_shim)},
		{BodyId::group_hack_shim, reinterpret_cast<void(*)()>(&abi_group_hack_shim)},
		{BodyId::smpa60_shim, reinterpret_cast<void(*)()>(&abi_smpa60_shim)},
		{BodyId::smpa60_tail_shim, reinterpret_cast<void(*)()>(&abi_smpa60_tail_shim)},
		{BodyId::freeze_gun_shim, reinterpret_cast<void(*)()>(&abi_freeze_gun_shim)},
		{BodyId::player_ped_components_shim, reinterpret_cast<void(*)()>(&abi_player_ped_components_shim)},
		{BodyId::emissive_lerp_shim, reinterpret_cast<void(*)()>(&abi_emissive_lerp_shim)},
		{BodyId::super_lod_fix_shim, reinterpret_cast<void(*)()>(&abi_super_lod_fix_shim)},
		{BodyId::episode_switch_store_shim, reinterpret_cast<void(*)()>(&abi_episode_switch_store_shim)},
		{BodyId::episode_switch_shim_b, reinterpret_cast<void(*)()>(&abi_episode_switch_shim_b)},
		{BodyId::episode_switch_vcall, reinterpret_cast<void(*)()>(&abi_episode_switch_vcall)},
		{BodyId::episode_switch_shim_c, reinterpret_cast<void(*)()>(&abi_episode_switch_shim_c)},
		{BodyId::bike_physics_detour, reinterpret_cast<void(*)()>(&abi_bike_physics_detour)},
		{BodyId::collision_deadlock_walk, reinterpret_cast<void(*)()>(&abi_collision_deadlock_walk)},
		{BodyId::mouse_axis_float, reinterpret_cast<void(*)()>(&abi_mouse_axis_float)},
		{BodyId::mouse_axis_neg, reinterpret_cast<void(*)()>(&abi_mouse_axis_neg)},
		{BodyId::mouse_axis_int, reinterpret_cast<void(*)()>(&abi_mouse_axis_int)},
		{BodyId::mouse_axis_flag, reinterpret_cast<void(*)()>(&abi_mouse_axis_flag)},
		{BodyId::mf_angle_step, reinterpret_cast<void(*)()>(&abi_mf_angle_step)},
		{BodyId::mouse_angle, reinterpret_cast<void(*)()>(&abi_mouse_angle)},
		{BodyId::mouse_advance, reinterpret_cast<void(*)()>(&abi_mouse_advance)},
		{BodyId::ooc_branch, reinterpret_cast<void(*)()>(&abi_ooc_branch)},
		{BodyId::ooc_wrap, reinterpret_cast<void(*)()>(&abi_ooc_wrap)},
		{BodyId::hqr_body, reinterpret_cast<void(*)()>(&abi_hqr_body)},
		{BodyId::cutscene_chain_b, reinterpret_cast<void(*)()>(&abi_cutscene_chain_b)},
		{BodyId::driveby_fov_shim, reinterpret_cast<void(*)()>(&abi_driveby_fov_shim)},
};



std::uintptr_t ResolveHookBody(std::uint32_t reference_va, void*) noexcept
{
	for (const AbiBody& body : kAbiBodies)
	{
		if (OriginalHandlerVa(body.id) == reference_va)
			return reinterpret_cast<std::uintptr_t>(body.address);
	}
	return 0;
}

std::size_t HookBodyCount() noexcept
{
	return sizeof(kAbiBodies) / sizeof(kAbiBodies[0]);
}

#if defined(ZP_DUMP_TOOL)
// Write-set dump support. A branch the installer writes to a hook body encodes
// the body's address *in the tool*, which is not part of the installer logic:
// the dump canonicalises those displacements to the body's reference VA so its
// output does not depend on where the tool happens to load. (The stub bodies
// still exist because the arms name them directly.)
std::size_t DumpHookBodyCount() noexcept
{
	return HookBodyCount();
}

std::uint32_t DumpHookBodyReference(std::size_t index) noexcept
{
	return OriginalHandlerVa(kAbiBodies[index].id);
}

std::uintptr_t DumpHookBodyAddress(std::size_t index) noexcept
{
	return reinterpret_cast<std::uintptr_t>(kAbiBodies[index].address);
}
#endif  // ZP_DUMP_TOOL

#endif  // _WIN32
