// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rebased globals and the original-global name tables. The hook bodies
// address these symbols directly; the DEBUG-only address tables map an original
// VA to the rebuilt module's equivalent, so a publish either lands in the right
// place or is skipped instead of writing a dangling original address.

#include "common.hxx"
#include "rebased.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Rebased globals: the live globals the hook bodies address by name.
// ---------------------------------------------------------------------------

// C linkage so the naked LoadDLCs body can address it from inline asm.
extern "C"
{
    uint32_t zpatch_dlc_load_callback = 0;              // dword_1015BDDC
    uint32_t zpatch_skip_intro_match = 0;               // dword_1015AD74
    uint32_t zpatch_skip_menu_resume = 0;               // dword_10159348
    uint8_t zpatch_skip_menu_flag = 0;                  // byte_101A8450
    uint32_t zpatch_blackscreen_resume = 0;             // dword_1015AD58
    uint32_t zpatch_blackscreen_value_ptr = 0;          // dword_1015AD5C
    uint32_t zpatch_path_node_resume_ok = 0;            // dword_1015BDFC
    uint32_t zpatch_path_node_resume_fail = 0;          // dword_1015BE00
    uint32_t zpatch_plane_crash_resume_a = 0;           // dword_1015BE18
    uint32_t zpatch_plane_crash_resume_b = 0;           // dword_1015BE14
    uint32_t zpatch_wanted_level_resume = 0;            // dword_1015AD6C
    uint32_t zpatch_group_hack_resume = 0;              // dword_1015AD60
    uint32_t zpatch_smpa60_resume_a = 0;                // dword_10159C70
    uint32_t zpatch_smpa60_resume_b = 0;                // dword_10159C68
    uint32_t zpatch_smpa60_resume_c = 0;                // dword_10159C6C (tail shim)
    uint32_t zpatch_freeze_gun_resume = 0;              // dword_1015AD64
    uint32_t zpatch_player_ped_object = 0;              // dword_10159D28
    uint32_t zpatch_player_ped_computed = 0;            // dword_10159D20
    uint32_t zpatch_player_ped_resume = 0;              // dword_10159D2C
    uint32_t zpatch_model_lookup = 0;                   // dword_10159C0C (shared)
    uint32_t zpatch_emissive_ecx = 0;                   // dword_10159CC0
    uint32_t zpatch_emissive_edx = 0;                   // dword_10159CCC
    uint32_t zpatch_emissive_const = 0;                 // dword_10159CC4
    uint32_t zpatch_emissive_resume = 0;                // dword_10159CC8
    uint32_t zpatch_carparts_resume_if = 0;             // dword_1015BDC8 (match+2)
    uint32_t zpatch_carparts_resume_else = 0;           // dword_1015BDC0 (match+0x45)
    uint32_t zpatch_super_lod_fail = 0;                 // dword_101580AC
    uint32_t zpatch_super_lod_ok = 0;                   // dword_101580B0
    uint32_t zpatch_super_lod_lookup = 0;               // dword_101580B4
    uint32_t zpatch_episode_switch_resume_a = 0;        // dword_10159BBC
    uint32_t zpatch_episode_switch_resume_b = 0;        // dword_10159BB4
    uint32_t zpatch_episode_switch_resume_c = 0;        // dword_10159BB8
    uint32_t zpatch_episode_flag = 2;                   // unk_10158128 (int 2)
    uint32_t zpatch_bike_value = 0;                     // dword_1015AD7C
    uint32_t zpatch_bike_saved = 0;                     // dword_1015AD80
    uint32_t zpatch_bike_resume = 0;                    // dword_1015AD84
    uint32_t zpatch_collision_deadlock_should_walk = 0; // dword_1015BDD4
    uint32_t zpatch_collision_deadlock_iterations = 0;  // dword_1015BDD8
    uint32_t zpatch_vram_arg_a = 0;                     // dword_1015D630
    uint32_t zpatch_vram_arg_b = 0;                     // dword_1015D634
    uint8_t zpatch_vram_flag = 0;                       // byte_1015D638
    uint32_t zpatch_driveby_resolver = 0;               // dword_1015BDE0
    uint32_t zpatch_driveby_resume = 0;                 // dword_1015BDF4
    uint32_t zpatch_driveby_fov = 0x42340000u;    // dword_101580C4 (45.0f, original on-disk)
    uint32_t zpatch_driveby_cached = 0;           // dword_1015BDF8
    uint8_t zpatch_driveby_done = 0;              // byte_1015AD4A
    uint32_t zpatch_driveby_last_result = 0;      // resolver result (diagnostic)
    uint32_t zpatch_nolivery_name = 0;            // dword_10159BA8 (char*)
    uint32_t zpatch_nolivery_index = 0;           // dword_10159BB0
    uint32_t zpatch_nolivery_hash = 0;            // dword_10159B98
    uint32_t zpatch_nolivery_resume = 0;          // dword_10159B9C
    uint32_t zpatch_nolivery_fn = 0;              // dword_10159BA4
    uint32_t zpatch_nolivery_resume_d = 0;        // dword_10159BAC
    uint32_t zpatch_nolivery_arg = 0;             // dword_10159B94
    uint32_t zpatch_nolivery_count = 0;           // dword_10159B90
    uint32_t zpatch_nolivery_bufptr = 0;          // dword_10159BA0 (char*)
    uint32_t zpatch_nolivery_dest = 0;            // dword_10159B8C (char*)
    uint32_t zpatch_crash_msg_getter = 0;         // dword_1015AD68
    uint32_t zpatch_newfiles_register_slot = 0;   // dword_10159C08
    uint32_t zpatch_benchmark_resume_a = 0;       // dword_1015AD50
    uint32_t zpatch_benchmark_resume_b = 0;       // dword_1015AD54
    uint32_t zpatch_bds_hash_a = 0;               // dword_10159C1C
    uint32_t zpatch_bds_hash_b = 0;               // dword_10159C24
    uint32_t zpatch_bds_resume_a = 0;             // dword_10159C20
    uint32_t zpatch_bds_resume_b = 0;             // dword_10159C28
    uint32_t zpatch_copsirens_resume = 0;         // dword_1015937C
    uint32_t zpatch_copsirens_resume_b = 0;       // dword_10159374
    uint32_t zpatch_copsirens_index = 0;          // dword_10159370
    uint32_t zpatch_copsirens_index_b = 0;        // dword_1015936C
    uint32_t zpatch_copsirens_flag = 0;           // dword_10159B80
    uint32_t zpatch_copsirens_flag_b = 0;         // dword_10159378
    uint32_t zpatch_ide_string = 0;               // dword_1015BE08
    uint32_t zpatch_ide_model = 0;                // dword_1015BE0C
    uint32_t zpatch_ide_hook_slot = 0;            // dword_1015BE10
    uint32_t zpatch_copsirens_models[512] = {};   // dword_10159380
    uint32_t zpatch_popuplights_models[512] = {}; // dword_1015CE28
    uint32_t zpatch_wheelangle_models[512] = {};  // dword_1015A538
    uint32_t zpatch_wheelangle_angles[512] = {};  // dword_10159D38
    uint8_t zpatch_borderless_w_done = 0;         // byte_101A8445
    uint8_t zpatch_borderless_a_done = 0;         // byte_101A8444
    uint32_t zpatch_speedup_t4c = 0;              // dword_1015934C
    uint32_t zpatch_speedup_t50 = 0;              // dword_10159350
    uint32_t zpatch_speedup_t54 = 0;              // dword_10159354
    uint32_t zpatch_speedup_t58 = 0;              // dword_10159358
    uint32_t zpatch_speedup_t5c = 0;              // dword_1015935C
    uint32_t zpatch_speedup_t60 = 0;              // dword_10159360
    uint32_t zpatch_speedup_t64 = 0;              // dword_10159364
    uint32_t zpatch_speedup_t68 = 0;              // dword_10159368
    // OverrideGTADatPath: the original pushed its own "common:/data/gta.dat" global;
    // we push this buffer instead (filled from OverrideGTADatPathTo at load).
    char zpatch_override_gta_dat_path[260] = "common:/data/gta.dat";
    // PopupHeadlightsFlag (see rebased.h). dword_101580CC is pre-set to pi/2 on
    // disk and rewritten by the shim's checker on every call.
    uint32_t zpatch_popup_model = 0;
    uint32_t zpatch_popup_flag = 0;
    uint32_t zpatch_popup_index = 0;
    uint32_t zpatch_popup_angle = 0x3FC90FDBu;
    uint32_t zpatch_popup_resume_a = 0;
    uint32_t zpatch_popup_resume_b = 0;
    uint32_t zpatch_popup_hashes[512] = {};
    float zpatch_popup_angles[512] = {};
    // ForceCarHeadlightShadows: dword_101580C0 is pre-set to 2.0f on disk.
    uint32_t zpatch_fchs_param = 0;
    uint32_t zpatch_fchs_resume = 0;
    uint32_t zpatch_fchs_offset = 0x40000000u;
    uint32_t zpatch_fchs_hashes[512] = {};
    float zpatch_fchs_offsets[512] = {};
    // BadPlayerModelCrashFix scratch.
    uint32_t zpatch_bpm_obj = 0;
    uint32_t zpatch_bpm_model_index = 0;
    uint32_t zpatch_bpm_resume = 0;
    // NewSavesCompatibility state.
    uint32_t zpatch_new_saves_src = 0;
    uint32_t zpatch_new_saves_resume = 0;
    uint32_t zpatch_new_saves_chunk_len = 0;
    uint8_t zpatch_new_saves_flag9 = 0;
    uint8_t zpatch_new_saves_flag13 = 0;
    uint8_t zpatch_new_saves_flag16 = 0;
    // Loader-preamble resolutions (unconditional in the original).
    uint32_t zpatch_pre_ped_array = 0;
    uint32_t zpatch_pre_fn_59c74 = 0;
    uint32_t zpatch_pre_fn_59b88 = 0;
    uint32_t zpatch_pre_fn_5d63c = 0;
    uint32_t zpatch_pre_fn_59cdc = 0;
    // Hash salts: the on-disk (probe-hit) values of the original's ten dwords.
    uint32_t zpatch_salt_28 = 0x6A6B4F18u;
    uint32_t zpatch_salt_3c = 0x022B2DA9u;
    uint32_t zpatch_salt_40 = 0x4BF36A32u;
    uint32_t zpatch_salt_50 = 0x3B0C6738u;
    uint32_t zpatch_salt_54 = 0x511454A9u;
    uint32_t zpatch_salt_5c = 0x62E319C6u;
    uint32_t zpatch_salt_a8 = 0x79A95BF9u;
    uint32_t zpatch_salt_b8 = 0x4E61480Au;
    uint32_t zpatch_salt_c8 = 0x570F5725u;
    uint32_t zpatch_salt_d4 = 0x75E005F1u;
    // Module-presence flags (set by the loader, exactly like the original).
    uint8_t zpatch_xliveless_present = 0;
    uint8_t zpatch_citizen_present = 0;
    uint32_t zpatch_xlive_base = 0;
    // Original one-time scan caches and the shared ASI TlsIndex.
    uint32_t zpatch_gfwl_scan_guard = 0;
    uint32_t zpatch_gfwl_site = 0;
    uint32_t zpatch_speedup_scan_guard = 0;
    uint32_t zpatch_speedup_clock = 0;
    uint32_t zpatch_tls_index = 0;
    // Live option-flag globals. Every one starts at 0: the loader arms only raise
    // a flag to 1, so an option that is off leaves it at 0.
    uint8_t zpatch_flag_death_music = 0u;      // byte_10159B86
    uint8_t zpatch_flag_patch4_modchecks = 0u; // byte_10159C14
    uint8_t zpatch_flag_gfwl_popup = 0u;       // byte_10159B87
    uint8_t zpatch_flag_debug_spawns = 0u;     // byte_10159C15
    uint8_t zpatch_flag_donotpause = 0u;       // byte_1015AD43
    // OpenPreCEPatch opt-in switches. 0 selects the original branch everywhere.
    uint8_t zpatch_opt_new_saves_guards = 0u;
    uint8_t zpatch_opt_new_saves_seek = 0u;
    uint8_t zpatch_opt_ooc_safe = 0u;
    uint8_t zpatch_opt_ooc_cache = 0u;
    uint8_t zpatch_opt_component_guards = 0u;
    uint8_t zpatch_opt_model_lookup_guards = 0u;
    uint8_t zpatch_opt_steer_cache = 0u;
    uint8_t zpatch_opt_popup_cache = 0u;
    uint8_t zpatch_opt_model_index = 0u;
    uint8_t zpatch_opt_skip_intro_screens = 0u;
    uint8_t zpatch_opt_trace_arms = 0u;
    uint8_t zpatch_opt_windowed = 0u;
    // RestoreDeathMusic's lazily resolved site.
    uint32_t zpatch_death_music_site = 0;
    // NewSavesCompatibility's element counter.
    uint32_t zpatch_new_saves_element_count = 0;
    uint32_t zpatch_new_saves_loaddata = 0;
    uint32_t zpatch_new_saves_p3_orig = 0;
    uint32_t zpatch_gfwl_resume = 0;
    uint32_t zpatch_carparts_shadow_fn = 0; // dword_1015B5B8
    uint32_t zpatch_carparts_resume_2 = 0;  // dword_1015BDCC
    uint32_t zpatch_car_dynamic_d = 0;      // dword_1015BDC4
    // DualVehicleHeadlights.
    uint32_t zpatch_dual_bcc = 0;      // dword_10159BCC
    uint32_t zpatch_dual_bd0 = 0;      // dword_10159BD0
    uint32_t zpatch_dual_bd4 = 0;      // dword_10159BD4
    uint32_t zpatch_dual_be0 = 0;      // dword_10159BE0
    uint32_t zpatch_dual_be4 = 0;      // dword_10159BE4
    uint32_t zpatch_dual_light_a = 1u; // dword_1015802C (original on-disk 1)
    uint32_t zpatch_dual_light_b = 2u; // dword_10158030 (original on-disk 2)
    uint32_t zpatch_dual_r_eax = 0;    // dword_10159BC4
    uint32_t zpatch_dual_r_ebx = 0;    // dword_10159BEC
    uint32_t zpatch_dual_r_ecx = 0;    // dword_10159BD8
    uint32_t zpatch_dual_r_edx = 0;    // dword_10159BF0
    uint32_t zpatch_dual_r_esi = 0;    // dword_10159BC8
    uint32_t zpatch_dual_r_edi = 0;    // dword_10159BE8
    uint32_t zpatch_dual_r_ebp = 0;    // dword_10159BDC
    uint32_t zpatch_dual_r_esp = 0;    // dword_10159BF4
    // LocalSettingsPath.
    char zpatch_local_ansi_cwd[260] = {};
    wchar_t zpatch_local_wide_cwd[260] = {};
    char zpatch_local_iv_suffix[8] = "\\GTA IV";
    uint32_t zpatch_local_saves_slot = 0;
    uint32_t zpatch_local_path_08 = 0;
    uint32_t zpatch_local_path_10 = 0;
    uint32_t zpatch_local_episode_ptr = 0;
    uint32_t zpatch_local_path_18 = 0;
    uint32_t zpatch_local_path_1c = 0;
    // FreezeCarFix / DebugPrintSpawns.
    uint32_t zpatch_spawns_kind = 0xFFFFFFFFu;
    uint32_t zpatch_spawns_class = 0;
    uint32_t zpatch_spawns_skip = 0;
    uint32_t zpatch_spawns_orig_a = 0;
    uint32_t zpatch_spawns_orig_b = 0;
    uint32_t zpatch_spawns_case0_resume = 0;
    uint32_t zpatch_spawns_case1_resume = 0;
    uint32_t zpatch_spawns_case2_resume = 0;
    uint32_t zpatch_spawns_case3_resume = 0;
    uint32_t zpatch_spawns_case4_resume = 0;
    uint32_t zpatch_spawns_case5_resume = 0;
    uint32_t zpatch_spawns_case6_resume = 0;
    uint32_t zpatch_spawns_case7_resume = 0;
    uint32_t zpatch_spawns_case8_resume = 0;
    uint32_t zpatch_spawns_case9_resume = 0;
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
    uint32_t zpatch_spawns_getmodulehandlew = 0;
    uint32_t zpatch_garage_resume_a = 0;
    uint32_t zpatch_garage_resume_b = 0;
    uint32_t zpatch_steer_vehicle = 0;
    uint32_t zpatch_steer_resume = 0;
    uint32_t zpatch_steer_vtable_fn = 0;
    float zpatch_engine_last_timestep = 0.0f;
    float zpatch_engine_phase = 0.0f;
    uint8_t zpatch_engine_descending = 0;
    uint8_t zpatch_flag_steering = 0u; // byte_1015AD40 (on-disk 0)
    uint8_t zpatch_flag_engine = 0u;   // byte_1015AD41 (on-disk 0)
    uint8_t zpatch_flag_plane = 0u;    // byte_10159C17 (on-disk 0)
    // MouseFix (see the hook bodies). The constants are not const so the
    // inline assembler can address them as plain external symbols; nothing writes
    // them. Values are the original .rdata bits (hex floats keep them exact).
    uint32_t zpatch_mf_x = 0;
    uint32_t zpatch_mf_y = 0;
    uint32_t zpatch_mf_flag = 0;
    uint32_t zpatch_mf_time = 0;
    uint32_t zpatch_mf_resume_a = 0;
    uint32_t zpatch_mf_resume_b = 0;
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
    uint32_t zpatch_ooc_resume = 0;
    uint32_t zpatch_ooc_logger = 0;
    uint32_t zpatch_ooc_orig = 0;
    uint32_t zpatch_ooc_state = 0;
    uint32_t zpatch_ooc_input_bound = 0;
    uint32_t zpatch_ooc_output_begin = 0;
    uint32_t zpatch_ooc_output_cursor = 0;
    uint32_t zpatch_ooc_output_end = 0;
    uint32_t zpatch_ooc_table_size = 0;
    uint32_t zpatch_ooc_archive_offset = 0;
    uint32_t zpatch_ooc_vector_begin = 0;
    uint32_t zpatch_ooc_vector_end = 0;
    uint32_t zpatch_ooc_vector_capacity = 0;
    char zpatch_ooc_fmt_a[] = "mor";
    char zpatch_ooc_fmt_b[] = "me%sy%c%c%p,%d,%c%c%s";
    // HighQualityReflections (see the hook bodies).
    uint32_t zpatch_hqr_resume = 0;
    uint32_t zpatch_hqr_builder = 0;
    uint32_t zpatch_hqr_dump[8] = {};
    uint32_t zpatch_hqr_flags = 0x11Du; // the original's dword_10158060
    uint32_t zpatch_hqr_mode = 0;       // the original's BSS slot stays 0
    // CutsceneFixes chain B (see the hook bodies): the worker's two publishes, the body's
    // frame-time accumulator (the original's .data BSS, past the raw data, so 0) and
    // the read-only constants it compares against (original .rdata/.data bits).
    uint32_t zpatch_cutscene_target = 0;                    // dword_1015BDE4
    uint32_t zpatch_cutscene_poke = 0;                      // dword_1015BDF0
    uint64_t zpatch_cutscene_accum = 0;                     // qword_1015BDE8 (BSS: starts 0)
    uint64_t zpatch_cutscene_zero = 0x0ull;                 // 0.0
    uint64_t zpatch_cutscene_limit = 0x3FD554C985F06F69ull; // 0.333333...
    uint32_t zpatch_cutscene_abs_mask = 0x7FFFFFFFu;        // 0x101523D0
    uint32_t zpatch_cutscene_eps_a = 0x3D4CCCCDu;           // 0.05f
    uint32_t zpatch_cutscene_eps_b = 0x3F000000u;           // 0.5f
    // NewMemorySystem: the allocator singleton the handler's second scan publishes
    // (the immediate of `mov esi, 0x19cf9f0`); the E8 body writes our vtable there.
    uint32_t zpatch_nms_object = 0; // dword_10159CD8
}
namespace
{
// Rebased originals the handlers address directly: StartupTimeFix pushes the
// address of g_startup_time (the original pushed dword_101580d8).
uint32_t g_startup_time = 0;
uint32_t g_dlc_register_rva = 0;
uint32_t g_dlc_apply_rva = 0;
// VSyncFix: the game code reads/writes this dword through the operand the
// handler patches (the original pushed &unk_10159BC0).
uint32_t g_vsync_flag = 0;
} // namespace

// Copies `path` into the buffer the retargeted push reads, clamped and terminated.
void SetOverrideGtaDatPath(std::string_view path) noexcept
{
    if (path.empty())
        path = "common:/data/gta.dat";
    const size_t n = path.size() < sizeof(zpatch_override_gta_dat_path) - 1
        ? path.size()
        : sizeof(zpatch_override_gta_dat_path) - 1;
    std::memcpy(zpatch_override_gta_dat_path, path.data(), n);
    zpatch_override_gta_dat_path[n] = '\0';
}

// Accessors for the rebased globals whose address a handler publishes.
uint32_t& StartupTimeGlobal() noexcept
{
    return g_startup_time;
}
uint32_t& DlcRegisterRva() noexcept
{
    return g_dlc_register_rva;
}
uint32_t& DlcApplyRva() noexcept
{
    return g_dlc_apply_rva;
}

// Rebases a value that addresses the original module, keyed by INI option.
#if defined(OPCEP_DUMP_TOOL)
namespace opp
{
    void RecordDumpPointer(uint32_t value);
}
#endif

namespace
{
    uintptr_t DumpTrack(uintptr_t value) noexcept
    {
#if defined(OPCEP_DUMP_TOOL)
        if (value != 0)
            opp::RecordDumpPointer(static_cast<uint32_t>(value));
#endif
        return value;
    }
} // namespace

uintptr_t FindGlobalValue(std::string_view option) noexcept
{
    if (option == "StartupTimeFix")
        return DumpTrack(reinterpret_cast<uintptr_t>(&g_startup_time));
    if (option == "VSyncFix")
        return DumpTrack(reinterpret_cast<uintptr_t>(&g_vsync_flag));
    if (option == "OverrideGTADatPath")
        return DumpTrack(reinterpret_cast<uintptr_t>(zpatch_override_gta_dat_path));
    // PopupHeadlightsFlag: the `fld` operand at +82 now loads our pi/2 float.
    if (option == "PopupHeadlightsFlag")
        return DumpTrack(reinterpret_cast<uintptr_t>(&zpatch_popup_angle));
    // ForceCarHeadlightShadows: the operand at +211 now loads our 2.0f dword.
    if (option == "ForceCarHeadlightShadows")
        return DumpTrack(reinterpret_cast<uintptr_t>(&zpatch_fchs_offset));
    // LocalSettingsPath: the path-helper call goes through our 4-byte slot.
    if (option == "LocalSettingsPath")
        return DumpTrack(reinterpret_cast<uintptr_t>(&zpatch_local_saves_slot));
    return 0;
}

// Named plugin-data symbols the sig-install `plugin_data_ptr` kind addresses
// (a 4-byte value written at the site: the ADDRESS of the named global).
uintptr_t FindGlobalSymbol(std::string_view symbol) noexcept
{
    if (symbol == "zpatch_local_iv_suffix")
        return DumpTrack(reinterpret_cast<uintptr_t>(zpatch_local_iv_suffix));
    if (symbol == "zpatch_episode_flag")
        return DumpTrack(reinterpret_cast<uintptr_t>(&zpatch_episode_flag));
    return 0;
}

// Slot publishes are keyed by the original global's name; 0 skips the publish.
uintptr_t FindGlobalSlot(std::string_view slot_name) noexcept
{
    // LoadDLCs: the callback slot receives the game's loader address; the two
    // constants are the register/apply RVAs the discovery routine adds to the
    // module base at use time.
    if (slot_name == "dword_1015BDDC")
        return reinterpret_cast<uintptr_t>(&zpatch_dlc_load_callback);
    if (slot_name == "dword_101580D0")
        return reinterpret_cast<uintptr_t>(&g_dlc_apply_rva);
    if (slot_name == "dword_1015804C")
        return reinterpret_cast<uintptr_t>(&g_dlc_register_rva);
    if (slot_name == "dword_1015AD74")
        return reinterpret_cast<uintptr_t>(&zpatch_skip_intro_match);
    if (slot_name == "dword_10159348")
        return reinterpret_cast<uintptr_t>(&zpatch_skip_menu_resume);
    if (slot_name == "byte_101A8450")
        return reinterpret_cast<uintptr_t>(&zpatch_skip_menu_flag);
    if (slot_name == "dword_1015AD58")
        return reinterpret_cast<uintptr_t>(&zpatch_blackscreen_resume);
    if (slot_name == "dword_1015AD5C")
        return reinterpret_cast<uintptr_t>(&zpatch_blackscreen_value_ptr);
    if (slot_name == "dword_1015BDFC")
        return reinterpret_cast<uintptr_t>(&zpatch_path_node_resume_ok);
    if (slot_name == "dword_1015BE00")
        return reinterpret_cast<uintptr_t>(&zpatch_path_node_resume_fail);
    if (slot_name == "dword_1015BE18")
        return reinterpret_cast<uintptr_t>(&zpatch_plane_crash_resume_a);
    if (slot_name == "dword_1015BE14")
        return reinterpret_cast<uintptr_t>(&zpatch_plane_crash_resume_b);
    if (slot_name == "dword_1015AD6C")
        return reinterpret_cast<uintptr_t>(&zpatch_wanted_level_resume);
    if (slot_name == "dword_1015AD60")
        return reinterpret_cast<uintptr_t>(&zpatch_group_hack_resume);
    if (slot_name == "dword_10159C70")
        return reinterpret_cast<uintptr_t>(&zpatch_smpa60_resume_a);
    if (slot_name == "dword_10159C68")
        return reinterpret_cast<uintptr_t>(&zpatch_smpa60_resume_b);
    if (slot_name == "dword_10159C6C")
        return reinterpret_cast<uintptr_t>(&zpatch_smpa60_resume_c);
    if (slot_name == "dword_1015AD64")
        return reinterpret_cast<uintptr_t>(&zpatch_freeze_gun_resume);
    if (slot_name == "dword_10159D28")
        return reinterpret_cast<uintptr_t>(&zpatch_player_ped_object);
    if (slot_name == "dword_10159D20")
        return reinterpret_cast<uintptr_t>(&zpatch_player_ped_computed);
    if (slot_name == "dword_10159D2C")
        return reinterpret_cast<uintptr_t>(&zpatch_player_ped_resume);
    if (slot_name == "dword_10159C0C")
        return reinterpret_cast<uintptr_t>(&zpatch_model_lookup);
    if (slot_name == "dword_10159CC0")
        return reinterpret_cast<uintptr_t>(&zpatch_emissive_ecx);
    if (slot_name == "dword_10159CCC")
        return reinterpret_cast<uintptr_t>(&zpatch_emissive_edx);
    if (slot_name == "dword_10159CC4")
        return reinterpret_cast<uintptr_t>(&zpatch_emissive_const);
    if (slot_name == "dword_10159CC8")
        return reinterpret_cast<uintptr_t>(&zpatch_emissive_resume);
    if (slot_name == "dword_1015BDC8")
        return reinterpret_cast<uintptr_t>(&zpatch_carparts_resume_if);
    if (slot_name == "dword_1015BDC0")
        return reinterpret_cast<uintptr_t>(&zpatch_carparts_resume_else);
    if (slot_name == "dword_101580AC")
        return reinterpret_cast<uintptr_t>(&zpatch_super_lod_fail);
    if (slot_name == "dword_101580B0")
        return reinterpret_cast<uintptr_t>(&zpatch_super_lod_ok);
    if (slot_name == "dword_101580B4")
        return reinterpret_cast<uintptr_t>(&zpatch_super_lod_lookup);
    if (slot_name == "dword_10159BBC")
        return reinterpret_cast<uintptr_t>(&zpatch_episode_switch_resume_a);
    if (slot_name == "dword_10159BB4")
        return reinterpret_cast<uintptr_t>(&zpatch_episode_switch_resume_b);
    if (slot_name == "dword_10159BB8")
        return reinterpret_cast<uintptr_t>(&zpatch_episode_switch_resume_c);
    if (slot_name == "dword_1015AD7C")
        return reinterpret_cast<uintptr_t>(&zpatch_bike_value);
    if (slot_name == "dword_1015AD80")
        return reinterpret_cast<uintptr_t>(&zpatch_bike_saved);
    if (slot_name == "dword_1015AD84")
        return reinterpret_cast<uintptr_t>(&zpatch_bike_resume);
    if (slot_name == "dword_1015BDD4")
        return reinterpret_cast<uintptr_t>(&zpatch_collision_deadlock_should_walk);
    if (slot_name == "dword_1015BDE0")
        return reinterpret_cast<uintptr_t>(&zpatch_driveby_resolver);
    if (slot_name == "dword_1015BDF4")
        return reinterpret_cast<uintptr_t>(&zpatch_driveby_resume);
    if (slot_name == "dword_1015BDE4")
        return reinterpret_cast<uintptr_t>(&zpatch_cutscene_target);
    if (slot_name == "dword_1015BDF0")
        return reinterpret_cast<uintptr_t>(&zpatch_cutscene_poke);
    if (slot_name == "dword_10159CD8")
        return reinterpret_cast<uintptr_t>(&zpatch_nms_object);
    if (slot_name == "dword_10159B9C")
        return reinterpret_cast<uintptr_t>(&zpatch_nolivery_resume);
    if (slot_name == "dword_10159BA4")
        return reinterpret_cast<uintptr_t>(&zpatch_nolivery_fn);
    if (slot_name == "dword_10159BAC")
        return reinterpret_cast<uintptr_t>(&zpatch_nolivery_resume_d);
    if (slot_name == "dword_1015AD68")
        return reinterpret_cast<uintptr_t>(&zpatch_crash_msg_getter);
    if (slot_name == "dword_10159C08")
        return reinterpret_cast<uintptr_t>(&zpatch_newfiles_register_slot);
    if (slot_name == "dword_1015AD50")
        return reinterpret_cast<uintptr_t>(&zpatch_benchmark_resume_a);
    if (slot_name == "dword_1015AD54")
        return reinterpret_cast<uintptr_t>(&zpatch_benchmark_resume_b);
    if (slot_name == "dword_10159C1C")
        return reinterpret_cast<uintptr_t>(&zpatch_bds_hash_a);
    if (slot_name == "dword_10159C24")
        return reinterpret_cast<uintptr_t>(&zpatch_bds_hash_b);
    if (slot_name == "dword_10159C20")
        return reinterpret_cast<uintptr_t>(&zpatch_bds_resume_a);
    if (slot_name == "dword_10159C28")
        return reinterpret_cast<uintptr_t>(&zpatch_bds_resume_b);
    if (slot_name == "dword_1015937C")
        return reinterpret_cast<uintptr_t>(&zpatch_copsirens_resume);
    if (slot_name == "dword_10159374")
        return reinterpret_cast<uintptr_t>(&zpatch_copsirens_resume_b);
    if (slot_name == "dword_1015BE10")
        return reinterpret_cast<uintptr_t>(&zpatch_ide_hook_slot);
    if (slot_name == "dword_1015934C")
        return reinterpret_cast<uintptr_t>(&zpatch_speedup_t4c);
    if (slot_name == "dword_10159350")
        return reinterpret_cast<uintptr_t>(&zpatch_speedup_t50);
    if (slot_name == "dword_10159354")
        return reinterpret_cast<uintptr_t>(&zpatch_speedup_t54);
    if (slot_name == "dword_10159358")
        return reinterpret_cast<uintptr_t>(&zpatch_speedup_t58);
    if (slot_name == "dword_1015935C")
        return reinterpret_cast<uintptr_t>(&zpatch_speedup_t5c);
    if (slot_name == "dword_10159360")
        return reinterpret_cast<uintptr_t>(&zpatch_speedup_t60);
    if (slot_name == "dword_10159364")
        return reinterpret_cast<uintptr_t>(&zpatch_speedup_t64);
    if (slot_name == "dword_10159368")
        return reinterpret_cast<uintptr_t>(&zpatch_speedup_t68);
    // PopupHeadlightsFlag resumes (match+12 / match+10) and the checker's tables.
    if (slot_name == "dword_1015D628")
        return reinterpret_cast<uintptr_t>(&zpatch_popup_resume_a);
    if (slot_name == "dword_1015D62C")
        return reinterpret_cast<uintptr_t>(&zpatch_popup_resume_b);
    // ForceCarHeadlightShadows resume (match+12 of its second pattern).
    if (slot_name == "dword_1015B5BC")
        return reinterpret_cast<uintptr_t>(&zpatch_fchs_resume);
    // BadPlayerModelCrashFix resume (match+10).
    if (slot_name == "dword_10159CB8")
        return reinterpret_cast<uintptr_t>(&zpatch_bpm_resume);
    // NewSavesCompatibility: Src is captured from the game's push immediate;
    // dword_1015AD88 is the tramp resume.
    if (slot_name == "Src")
        return reinterpret_cast<uintptr_t>(&zpatch_new_saves_src);
    if (slot_name == "dword_1015AD88")
        return reinterpret_cast<uintptr_t>(&zpatch_new_saves_resume);
    if (slot_name == "dword_10159C18")
        return reinterpret_cast<uintptr_t>(&zpatch_gfwl_resume);
    if (slot_name == "dword_1015B5B8")
        return reinterpret_cast<uintptr_t>(&zpatch_carparts_shadow_fn);
    if (slot_name == "dword_1015BDC4")
        return reinterpret_cast<uintptr_t>(&zpatch_car_dynamic_d);
    if (slot_name == "dword_1015BDCC")
        return reinterpret_cast<uintptr_t>(&zpatch_carparts_resume_2);
    // DualVehicleHeadlights's five published game pointers.
    if (slot_name == "dword_10159BCC")
        return reinterpret_cast<uintptr_t>(&zpatch_dual_bcc);
    if (slot_name == "dword_10159BD0")
        return reinterpret_cast<uintptr_t>(&zpatch_dual_bd0);
    if (slot_name == "dword_10159BD4")
        return reinterpret_cast<uintptr_t>(&zpatch_dual_bd4);
    if (slot_name == "dword_10159BE0")
        return reinterpret_cast<uintptr_t>(&zpatch_dual_be0);
    if (slot_name == "dword_10159BE4")
        return reinterpret_cast<uintptr_t>(&zpatch_dual_be4);
    // LocalSettingsPath's five game path pointers.
    if (slot_name == "dword_10159D08")
        return reinterpret_cast<uintptr_t>(&zpatch_local_path_08);
    if (slot_name == "dword_10159D10")
        return reinterpret_cast<uintptr_t>(&zpatch_local_path_10);
    if (slot_name == "dword_10159D14")
        return reinterpret_cast<uintptr_t>(&zpatch_local_episode_ptr);
    if (slot_name == "dword_10159D18")
        return reinterpret_cast<uintptr_t>(&zpatch_local_path_18);
    if (slot_name == "dword_10159D1C")
        return reinterpret_cast<uintptr_t>(&zpatch_local_path_1c);
    // FreezeCarFix / DebugPrintSpawns resume slots.
    if (slot_name == "dword_10159CB0")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_orig_a);
    if (slot_name == "dword_10159C98")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_orig_b);
    if (slot_name == "dword_10159CBC")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_case0_resume);
    if (slot_name == "dword_10159C9C")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_case1_resume);
    if (slot_name == "dword_10159CA0")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_case2_resume);
    if (slot_name == "dword_10159CA8")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_case3_resume);
    if (slot_name == "dword_10159C7C")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_case4_resume);
    if (slot_name == "dword_10159C88")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_case5_resume);
    if (slot_name == "dword_10159CAC")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_case6_resume);
    if (slot_name == "dword_10159C90")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_case7_resume);
    if (slot_name == "dword_10159C94")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_case8_resume);
    if (slot_name == "dword_10159C84")
        return reinterpret_cast<uintptr_t>(&zpatch_spawns_case9_resume);
    // GarageModelFix resumes.
    if (slot_name == "dword_10159CD4")
        return reinterpret_cast<uintptr_t>(&zpatch_garage_resume_a);
    if (slot_name == "dword_10159CD0")
        return reinterpret_cast<uintptr_t>(&zpatch_garage_resume_b);
    // Trio resumes / call target.
    if (slot_name == "dword_1015AD44")
        return reinterpret_cast<uintptr_t>(&zpatch_steer_resume);
    if (slot_name == "dword_1015AD4C")
        return reinterpret_cast<uintptr_t>(&zpatch_steer_vtable_fn);
    // MouseFix publishes (the callback's slot writes).
    if (slot_name == "dword_10159C50")
        return reinterpret_cast<uintptr_t>(&zpatch_mf_x);
    if (slot_name == "dword_10159C3C")
        return reinterpret_cast<uintptr_t>(&zpatch_mf_y);
    if (slot_name == "dword_10159C5C")
        return reinterpret_cast<uintptr_t>(&zpatch_mf_flag);
    if (slot_name == "dword_10159C34")
        return reinterpret_cast<uintptr_t>(&zpatch_mf_time);
    if (slot_name == "dword_10159C60")
        return reinterpret_cast<uintptr_t>(&zpatch_mf_resume_a);
    if (slot_name == "dword_10159C40")
        return reinterpret_cast<uintptr_t>(&zpatch_mf_resume_b);
    // OutOfCommissionFix publishes.
    if (slot_name == "dword_10158034")
        return reinterpret_cast<uintptr_t>(&zpatch_ooc_resume);
    if (slot_name == "dword_10159BF8")
        return reinterpret_cast<uintptr_t>(&zpatch_ooc_logger);
    if (slot_name == "dword_10159C04")
        return reinterpret_cast<uintptr_t>(&zpatch_ooc_orig);
    // HighQualityReflections publishes.
    if (slot_name == "dword_10159CFC")
        return reinterpret_cast<uintptr_t>(&zpatch_hqr_resume);
    if (slot_name == "dword_10159D04")
        return reinterpret_cast<uintptr_t>(&zpatch_hqr_builder);
    // SuperLODFix's resume slots are not rebased yet (their ABI body is pending);
    // the publishes are skipped and counted.
    return 0;
}
