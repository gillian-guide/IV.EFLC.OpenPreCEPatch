// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The extern "C" rebased globals: the hook bodies address these symbols by
// name, and the arms publish resumes into them. The C++ accessors that map an
// original global name to its rebuilt counterpart (FindGlobalValue,
// FindGlobalSymbol, FindGlobalSlot, SetOverrideGtaDatPath, StartupTimeGlobal,
// DlcRegisterRva, DlcApplyRva) are exported by opp.game, which owns the
// definitions (rebased.cpp was folded into it).

#include <cstdint>
#include <string_view>

// OverrideGTADatPath: retargeted `push <char*>` operand (the original pushed its
// own "common:/data/gta.dat"); filled from OverrideGTADatPathTo, defaults to it.
extern "C" char zpatch_override_gta_dat_path[260];

// LoadDLCs: the published loader callback and the two RVAs the handler stores
// (dword_101580D0 / dword_1015804C, the register/apply functions).
extern "C" uint32_t zpatch_dlc_load_callback;
// BlackscreenFix: the published call target (dword_1015AD58) and the pointer to
// the game value the shim inspects (dword_1015AD5C).
extern "C" uint32_t zpatch_blackscreen_resume;
extern "C" uint32_t zpatch_blackscreen_value_ptr;
// Crash-fix shims: handler-published resumes (match-relative), called like the originals.
extern "C" uint32_t zpatch_path_node_resume_ok;    // dword_1015BDFC
extern "C" uint32_t zpatch_path_node_resume_fail;  // dword_1015BE00
extern "C" uint32_t zpatch_plane_crash_resume_a;   // dword_1015BE18
extern "C" uint32_t zpatch_plane_crash_resume_b;   // dword_1015BE14
extern "C" uint32_t zpatch_wanted_level_resume;    // dword_1015AD6C
extern "C" uint32_t zpatch_group_hack_resume;      // dword_1015AD60
extern "C" uint32_t zpatch_smpa60_resume_a;  // dword_10159C70
extern "C" uint32_t zpatch_smpa60_resume_b;  // dword_10159C68
extern "C" uint32_t zpatch_smpa60_resume_c;  // dword_10159C6C (tail shim)
// FreezeGunFix shim resume (dword_1015AD64, match-12 of its site).
extern "C" uint32_t zpatch_freeze_gun_resume;
// PlayerAsPedComponentsFix: the shim saves the object (eax) and the helper's
// result, then resumes; the helper also needs the shared model-lookup function.
extern "C" uint32_t zpatch_player_ped_object;    // dword_10159D28
extern "C" uint32_t zpatch_player_ped_computed;  // dword_10159D20
extern "C" uint32_t zpatch_player_ped_resume;    // dword_10159D2C
extern "C" uint32_t zpatch_model_lookup;         // dword_10159C0C (shared)
// EmissiveLerpFix: the shim's save slots and the handler-published const/resume.
extern "C" uint32_t zpatch_emissive_ecx;     // dword_10159CC0
extern "C" uint32_t zpatch_emissive_edx;     // dword_10159CCC
extern "C" uint32_t zpatch_emissive_const;   // dword_10159CC4 (match-16)
extern "C" uint32_t zpatch_emissive_resume;  // dword_10159CC8 (match-12)
// CarPartsShadowFix: the two game-side resume addresses the handler publishes
// (match+2 / match+0x45); body 0x10006880 tail-jumps through them.
extern "C" uint32_t zpatch_carparts_resume_if;    // dword_1015BDC8 (match+2)
extern "C" uint32_t zpatch_carparts_resume_else;  // dword_1015BDC0 (match+0x45)
// SuperLODFix: handler-published base+rva values (build-gated).
extern "C" uint32_t zpatch_super_lod_fail;    // dword_101580AC
extern "C" uint32_t zpatch_super_lod_ok;      // dword_101580B0
extern "C" uint32_t zpatch_super_lod_lookup;  // dword_101580B4
// EpisodeSwitchFreezeFix resumes.
extern "C" uint32_t zpatch_episode_switch_resume_a;  // dword_10159BBC
extern "C" uint32_t zpatch_episode_switch_resume_b;  // dword_10159BB4
extern "C" uint32_t zpatch_episode_switch_resume_c;  // dword_10159BB8
// EpisodicVehicleSupport: the int 2 flag (unk_10158128) whose ADDRESS the
// handler stores over two `cmp [episode_global],2` operand slots on 1070/1080.
extern "C" uint32_t zpatch_episode_flag;  // unk_10158128
// HighFPSBikePhysicsFix: the detour's value/save slots and resume.
extern "C" uint32_t zpatch_bike_value;   // dword_1015AD7C
extern "C" uint32_t zpatch_bike_saved;   // dword_1015AD80
extern "C" uint32_t zpatch_bike_resume;  // dword_1015AD84
// CollisionDeadlockFix: the handler publishes the walk predicate function;
// the iteration counter is scratch state of the replacement.
extern "C" uint32_t zpatch_collision_deadlock_should_walk;  // dword_1015BDD4
extern "C" uint32_t zpatch_collision_deadlock_iterations;   // dword_1015BDD8
// VRAMFix: saved arguments and the helper's verdict (dword_1015D630/34,
// byte_1015D638); the shim then decides whether to return INT_MAX.
// DrivebyFOVFix: the loader-published resolver (key in ECX, from the d4 salt),
// the handler resume, and the helper's float/cache/done.
extern "C" uint32_t zpatch_driveby_resolver;  // dword_1015BDE0
extern "C" uint32_t zpatch_driveby_resume;    // dword_1015BDF4
extern "C" uint32_t zpatch_driveby_fov;       // dword_101580C4
extern "C" uint32_t zpatch_driveby_cached;    // dword_1015BDF8
extern "C" uint8_t zpatch_driveby_done;       // byte_1015AD4A
extern "C" uint32_t zpatch_driveby_last_result;  // resolver result (diagnostic)
// NoLiveryLimit: the published jumps/function pointer plus the helper's state.
extern "C" uint32_t zpatch_nolivery_name;      // dword_10159BA8 (char*)
extern "C" uint32_t zpatch_nolivery_index;     // dword_10159BB0
extern "C" uint32_t zpatch_nolivery_hash;      // dword_10159B98
extern "C" uint32_t zpatch_nolivery_resume;    // dword_10159B9C
extern "C" uint32_t zpatch_nolivery_fn;        // dword_10159BA4
extern "C" uint32_t zpatch_nolivery_resume_d;  // dword_10159BAC
extern "C" uint32_t zpatch_nolivery_arg;       // dword_10159B94
extern "C" uint32_t zpatch_nolivery_count;     // dword_10159B90
extern "C" uint32_t zpatch_nolivery_bufptr;    // dword_10159BA0 (char*)
extern "C" uint32_t zpatch_nolivery_dest;      // dword_10159B8C (char*)
// CrashMsgFix: the handler publishes the original call target at *(match-4).
extern "C" uint32_t zpatch_crash_msg_getter;   // dword_1015AD68
// NewFilesCompatibility: the handler publishes the game's file-registration fn.
extern "C" uint32_t zpatch_newfiles_register_slot;  // dword_10159C08
// BenchmarkFix: the two handler-published resumes (match[0] / match[1] + 0xd).
extern "C" uint32_t zpatch_benchmark_resume_a;  // dword_1015AD50
extern "C" uint32_t zpatch_benchmark_resume_b;  // dword_1015AD54
// BuildingDynamicShadows: the two model hashes and two resumes the handler publishes.
extern "C" uint32_t zpatch_bds_hash_a;     // dword_10159C1C
extern "C" uint32_t zpatch_bds_hash_b;     // dword_10159C24
extern "C" uint32_t zpatch_bds_resume_a;   // dword_10159C20
extern "C" uint32_t zpatch_bds_resume_b;   // dword_10159C28
// CopSirensFlag: shim scratch/resumes, the IDE-hook slots, and the siren
// model-hash array filled by the reimplemented IDE parser.
extern "C" uint32_t zpatch_copsirens_resume;      // dword_1015937C
extern "C" uint32_t zpatch_copsirens_resume_b;    // dword_10159374
extern "C" uint32_t zpatch_copsirens_index;       // dword_10159370
extern "C" uint32_t zpatch_copsirens_index_b;     // dword_1015936C
extern "C" uint32_t zpatch_copsirens_flag;        // dword_10159B80
extern "C" uint32_t zpatch_copsirens_flag_b;      // dword_10159378
extern "C" uint32_t zpatch_ide_string;            // dword_1015BE08
extern "C" uint32_t zpatch_ide_model;             // dword_1015BE0C
extern "C" uint32_t zpatch_ide_hook_slot;         // dword_1015BE10
extern "C" uint32_t zpatch_copsirens_models[512]; // dword_10159380
extern "C" uint32_t zpatch_popuplights_models[512];  // dword_1015CE28
extern "C" uint32_t zpatch_wheelangle_models[512];   // dword_1015A538
extern "C" uint32_t zpatch_wheelangle_angles[512];   // dword_10159D38
// SpeedupBugFix: eight pointers to the game's timing float constants.
extern "C" uint32_t zpatch_speedup_t4c;  // dword_1015934C
extern "C" uint32_t zpatch_speedup_t50;  // dword_10159350
extern "C" uint32_t zpatch_speedup_t54;  // dword_10159354
extern "C" uint32_t zpatch_speedup_t58;  // dword_10159358
extern "C" uint32_t zpatch_speedup_t5c;  // dword_1015935C
extern "C" uint32_t zpatch_speedup_t60;  // dword_10159360
extern "C" uint32_t zpatch_speedup_t64;  // dword_10159364
extern "C" uint32_t zpatch_speedup_t68;  // dword_10159368
// PopupHeadlightsFlag: the shim's model/flag/index scratch, the per-model angle
// the game's fld operand is retargeted to (dword_101580CC), the two resumes and
// the two INI-filled 512-slot tables (dword_1015C628 / dword_1015BE28).
extern "C" uint32_t zpatch_popup_model;   // dword_1015BE20
extern "C" uint32_t zpatch_popup_flag;    // dword_1015BE24
extern "C" uint32_t zpatch_popup_index;   // dword_1015BE1C
extern "C" uint32_t zpatch_popup_angle;   // dword_101580CC (float bits)
extern "C" uint32_t zpatch_popup_resume_a;  // dword_1015D628 (match+12)
extern "C" uint32_t zpatch_popup_resume_b;  // dword_1015D62C (match+10)
extern "C" uint32_t zpatch_popup_hashes[512];  // dword_1015C628
extern "C" float zpatch_popup_angles[512];          // dword_1015BE28 (radians)
// ForceCarHeadlightShadows: the shim's object/scratch, the retargeted offset
// (dword_101580C0), the resume and the two INI-filled 512-slot tables.
extern "C" uint32_t zpatch_fchs_param;    // dword_1015BDD0
extern "C" uint32_t zpatch_fchs_resume;   // dword_1015B5BC (match+12)
extern "C" uint32_t zpatch_fchs_offset;   // dword_101580C0 (float bits)
extern "C" uint32_t zpatch_fchs_hashes[512];  // dword_1015ADB8
extern "C" float zpatch_fchs_offsets[512];         // dword_1015B5C0
// BadPlayerModelCrashFix: the tramp's saved object/model index and resume (match+10).
extern "C" uint32_t zpatch_bpm_obj;          // dword_10159C8C
extern "C" uint32_t zpatch_bpm_model_index;  // dword_10159CA4
extern "C" uint32_t zpatch_bpm_resume;       // dword_10159CB8
// NewSavesCompatibility: the file-name operand captured from `push <imm32>`, the
// converter's scratch and the tramp resume (dword_1015AD88).
extern "C" uint32_t zpatch_new_saves_src;       // Src
extern "C" uint32_t zpatch_new_saves_resume;    // dword_1015AD88
extern "C" uint32_t zpatch_new_saves_chunk_len; // dword_1015AD90
// The chunk-size probes: 1 when the save's chunk at that index has the new
// format's size (byte_1015AD98/99/9A in the original). Chunk 13 gates the p3
// hook's 12-byte reads and chunk 16 selects the 3694-byte read; chunk 9 gates
// the P1 branch.
extern "C" uint8_t zpatch_new_saves_chunk9_new;  // byte_1015AD98
extern "C" uint8_t zpatch_new_saves_chunk13_new; // byte_1015AD99
extern "C" uint8_t zpatch_new_saves_chunk16_new; // byte_1015AD9A
// The preamble's unconditionally resolved pointers (the original resolves them
// on every run): the shared model lookup (dword_10159C0C), the script resolver
// (dword_1015BDE0), the ped array (dword_1015AD78) and four crash-fix functions.
extern "C" uint32_t zpatch_pre_ped_array;   // dword_1015AD78
extern "C" uint32_t zpatch_pre_fn_59c74;    // dword_10159C74
extern "C" uint32_t zpatch_pre_fn_59b88;    // dword_10159B88
extern "C" uint32_t zpatch_pre_fn_5d63c;    // dword_1015D63C
extern "C" uint32_t zpatch_pre_fn_59cdc;    // dword_10159CDC
// The ten hash salts the loader rewrites only when its build probe misses; the
// static values are the probe-hit set. Only the native-scripting layer reads them.
extern "C" uint32_t zpatch_salt_28;  // dword_10158028
extern "C" uint32_t zpatch_salt_3c;  // dword_1015803C
extern "C" uint32_t zpatch_salt_40;  // dword_10158040
extern "C" uint32_t zpatch_salt_50;  // dword_10158050
extern "C" uint32_t zpatch_salt_54;  // dword_10158054
extern "C" uint32_t zpatch_salt_5c;  // dword_1015805C
extern "C" uint32_t zpatch_salt_a8;  // dword_101580A8
extern "C" uint32_t zpatch_salt_b8;  // dword_101580B8
extern "C" uint32_t zpatch_salt_c8;  // dword_101580C8
extern "C" uint32_t zpatch_salt_d4;  // dword_101580D4
// Loader module-presence flags: byte_10159B84 = XLivelessAddon.asi loaded,
// byte_10159B85 = CitizenIV.exe present, dword_10159C10 = the xlive.dll base the
// GFWL patch targets by RVA.
extern "C" uint8_t zpatch_xliveless_present;  // byte_10159B84
extern "C" uint8_t zpatch_citizen_present;    // byte_10159B85
extern "C" uint32_t zpatch_xlive_base;        // dword_10159C10
// The two original MSVC thread-safe-static scan caches. Both guards compare
// against the ASI's TlsIndex (dword_10159328) and per-thread epoch.
extern "C" uint32_t zpatch_gfwl_scan_guard;   // dword_101A8480
extern "C" uint32_t zpatch_gfwl_site;         // dword_101A84C4
extern "C" uint32_t zpatch_speedup_scan_guard; // dword_101A84C8
extern "C" uint32_t zpatch_speedup_clock;     // dword_101A849C
extern "C" uint32_t zpatch_tls_index;         // dword_10159328
// The option flags the live bodies read (filled by the runtime from the option
// state): byte_10159B86 = RestoreDeathMusic, byte_10159C14 = Patch4RemoveModChecks,
// byte_10159B87 = RemoveGFWLUpdatePopup, byte_10159C15 = DebugPrintSpawns.
extern "C" uint8_t zpatch_flag_death_music;      // byte_10159B86
extern "C" uint8_t zpatch_flag_patch4_modchecks; // byte_10159C14
extern "C" uint8_t zpatch_flag_gfwl_popup;       // byte_10159B87
extern "C" uint8_t zpatch_flag_debug_spawns;     // byte_10159C15
// No zpatch_flag_donotpause: the pause-on-focus-loss fix is not installed by
// this tree (the declaration had no definition and no user).
// RestoreDeathMusic: the lazily resolved game site of "6A 06 68 ? ? ? ? 8D 8C"
// (dword_101A84B8) whose push immediate is randomized to a track 1..5.
extern "C" uint32_t zpatch_death_music_site;  // dword_101A84B8
// NewSavesCompatibility state (see the port in the hook bodies).
extern "C" uint32_t zpatch_new_saves_element_count;  // dword_1015ADA0
extern "C" uint32_t zpatch_new_saves_loaddata;       // dword_1015AD94 (game LoadData)
extern "C" uint32_t zpatch_new_saves_orig_handler;   // dword_1015AD8C (P3 original handler)
// Patch4RemoveModChecks / RemoveGFWLUpdatePopup: the shared resume (match+12)
// for the stub's tail jump.
extern "C" uint32_t zpatch_gfwl_resume;              // dword_10159C18
// CarPartsShadowFix's second shim (0x100068A0) and the two slots the shared
// installer / the CarDynamicShadowFix handler publish.
extern "C" uint32_t zpatch_carparts_shadow_fn;       // dword_1015B5B8
extern "C" uint32_t zpatch_carparts_resume_2;        // dword_1015BDCC
extern "C" uint32_t zpatch_car_dynamic_d;          // dword_1015BDC4 (write-only publish)
// DualVehicleHeadlights (handler 0x10006770): the five published game pointers,
// the two light-slot constants the init helper sets (dword_1015802C=1,
// dword_10158030=2), and the eight GPR save slots (dword_10159BC4..BF4).
extern "C" uint32_t zpatch_dual_bcc;               // dword_10159BCC (*(match+207))
extern "C" uint32_t zpatch_dual_bd0;               // dword_10159BD0 (match+*(match+140)+144)
extern "C" uint32_t zpatch_dual_bd4;               // dword_10159BD4 (match+*(match+331)+335)
extern "C" uint32_t zpatch_dual_be0;               // dword_10159BE0 (match+338)
extern "C" uint32_t zpatch_dual_be4;               // dword_10159BE4 (match+*(match+151)+155)
extern "C" uint32_t zpatch_dual_light_a;           // dword_1015802C (=1)
extern "C" uint32_t zpatch_dual_light_b;           // dword_10158030 (=2)
extern "C" uint32_t zpatch_dual_r_eax;             // dword_10159BC4
extern "C" uint32_t zpatch_dual_r_ebx;             // dword_10159BEC
extern "C" uint32_t zpatch_dual_r_ecx;             // dword_10159BD8
extern "C" uint32_t zpatch_dual_r_edx;             // dword_10159BF0
extern "C" uint32_t zpatch_dual_r_esi;             // dword_10159BC8
extern "C" uint32_t zpatch_dual_r_edi;             // dword_10159BE8
extern "C" uint32_t zpatch_dual_r_ebp;             // dword_10159BDC
extern "C" uint32_t zpatch_dual_r_esp;             // dword_10159BF4
// LocalSettingsPath: the captured CWD, the "\GTA IV" suffix the path helper is
// retargeted to push, the call slot the game calls through (off_10158064 =
// sub_10051320) and the five rewritten path pointers (dword_10159D08..1C).
extern "C" char zpatch_local_ansi_cwd[260];
extern "C" wchar_t zpatch_local_wide_cwd[260];
extern "C" char zpatch_local_iv_suffix[8];
extern "C" uint32_t zpatch_local_saves_slot;    // off_10158064
extern "C" uint32_t zpatch_local_path_08;       // dword_10159D08
extern "C" uint32_t zpatch_local_path_10;       // dword_10159D10
extern "C" uint32_t zpatch_local_episode_ptr;   // dword_10159D14
extern "C" uint32_t zpatch_local_path_18;       // dword_10159D18
extern "C" uint32_t zpatch_local_path_1c;       // dword_10159D1C
// FreezeCarFix / DebugPrintSpawns: the spawn-probe selector (on-disk init -1),
// the captured class, the suppress flag and the twelve published resumes.
extern "C" uint32_t zpatch_spawns_kind;   // dword_10158058 (init 0xFFFFFFFF)
extern "C" uint32_t zpatch_spawns_class;  // dword_10159C80
extern "C" uint32_t zpatch_spawns_skip;   // dword_10159CB4
extern "C" uint32_t zpatch_spawns_orig_a;       // dword_10159CB0
extern "C" uint32_t zpatch_spawns_orig_b;       // dword_10159C98
extern "C" uint32_t zpatch_spawns_case0_resume; // dword_10159CBC
extern "C" uint32_t zpatch_spawns_case1_resume; // dword_10159C9C
extern "C" uint32_t zpatch_spawns_case2_resume; // dword_10159CA0
extern "C" uint32_t zpatch_spawns_case3_resume; // dword_10159CA8
extern "C" uint32_t zpatch_spawns_case4_resume; // dword_10159C7C
extern "C" uint32_t zpatch_spawns_case5_resume; // dword_10159C88
extern "C" uint32_t zpatch_spawns_case6_resume; // dword_10159CAC
extern "C" uint32_t zpatch_spawns_case7_resume; // dword_10159C90
extern "C" uint32_t zpatch_spawns_case8_resume; // dword_10159C94
extern "C" uint32_t zpatch_spawns_case9_resume; // dword_10159C84
// The spawn-message format strings and the MP module name (the probe body
// pushes their addresses; exact text matters).
extern "C" char zpatch_fmt_spawned_car[];
extern "C" char zpatch_fmt_nonspawned_car[];
extern "C" char zpatch_fmt_spawned_heli[];
extern "C" char zpatch_fmt_nonspawned_heli[];
extern "C" char zpatch_fmt_spawned_boat[];
extern "C" char zpatch_fmt_nonspawned_boat[];
extern "C" char zpatch_fmt_spawned_bike[];
extern "C" char zpatch_fmt_nonspawned_bike[];
extern "C" char zpatch_fmt_spawned_train[];
extern "C" char zpatch_fmt_nonspawned_train[];
extern "C" char zpatch_fmt_spawned_plane[];
extern "C" char zpatch_fmt_nonspawned_plane[];
extern "C" char zpatch_fmt_spawned_ped[];
extern "C" char zpatch_fmt_nonspawned_ped[];
extern "C" char zpatch_fmt_spawned_vehicle_object[];
extern "C" char zpatch_fmt_spawned_object[];
extern "C" char zpatch_fmt_spawned_generic[];
extern "C" char zpatch_fmt_used_nonped[];
extern "C" wchar_t zpatch_str_gtac[];
extern "C" uint32_t zpatch_spawns_getmodulehandlew;
// GarageModelFix: the two resume slots its bodies tail-jump through.
extern "C" uint32_t zpatch_garage_resume_a;  // dword_10159CD4 (match+6)
extern "C" uint32_t zpatch_garage_resume_b;  // dword_10159CD0 (match+5)
extern "C" uint32_t zpatch_garage_entry_resume_a;
extern "C" float zpatch_garage_scale;
// MovingSteeringWheelSupport / ShakingEngineBlock / MovingPlaneParts: the cached
// vehicle, the published call target/resume, the engine state and three flags.
extern "C" uint32_t zpatch_steer_vehicle;     // dword_1015AD38
extern "C" uint32_t zpatch_steer_resume;      // dword_1015AD44 (match+16)
extern "C" uint32_t zpatch_steer_vtable_fn;   // dword_1015AD4C (game call)
extern "C" float zpatch_engine_last_timestep;      // dword_10159D34
extern "C" float zpatch_engine_phase;              // dword_1015AD3C
extern "C" uint8_t zpatch_engine_descending;  // byte_1015AD42
extern "C" uint8_t zpatch_flag_steering;      // byte_1015AD40
extern "C" uint8_t zpatch_flag_engine;        // byte_1015AD41
extern "C" uint8_t zpatch_flag_plane;         // byte_10159C17
// MouseFix: the four published game pointers/function, the two resumes of its
// naked bodies, body 0x10024560's xmm saves, and the exact constant pool the
// helpers read (original .rdata bits).
extern "C" uint32_t zpatch_mf_x;          // dword_10159C50 (int*)
extern "C" uint32_t zpatch_mf_y;          // dword_10159C3C (int*)
extern "C" uint32_t zpatch_mf_flag;       // dword_10159C5C (char*)
extern "C" uint32_t zpatch_mf_time;       // dword_10159C34 (fn)
extern "C" uint32_t zpatch_mf_resume_a;   // dword_10159C60 (match)
extern "C" uint32_t zpatch_mf_resume_b;   // dword_10159C40 (match+0x756)
extern "C" float zpatch_mf_xmm_save[8];
extern "C" double zpatch_mf_scale;       // 0.0078125
extern "C" double zpatch_mf_scale_255;   // 255.0
extern "C" float zpatch_mf_sign;         // 127.5f
extern "C" float zpatch_mf_mul_neg1;     // -1.0f
extern "C" float zpatch_mf_step_scale;   // 0.1f
extern "C" double zpatch_mf_mul_neg1_d;  // -1.0
extern "C" double zpatch_mf_time_mul;    // 0x1.333334015bc21p-4
extern "C" double zpatch_mf_time_add;    // 0.025
extern "C" double zpatch_mf_pi;          // pi
extern "C" double zpatch_mf_two_pi;      // 2*pi
extern "C" double zpatch_mf_neg_pi;      // -pi
extern "C" unsigned char zpatch_mf_neg_mask[16];     // 4x 0x80000000
extern "C" unsigned char zpatch_mf_double_sign[16];  // 2x 0x8000000000000000
// OutOfCommissionFix: the resume (dword_10158034 = match+0x11), the logger
// (dword_10159BF8), the original call target captured before the E8 is
// overwritten (dword_10159C04), and the wrapper's dword_10159BFC argument.
extern "C" uint32_t zpatch_ooc_resume;   // dword_10158034
extern "C" uint32_t zpatch_ooc_logger;   // dword_10159BF8
extern "C" uint32_t zpatch_ooc_orig;     // dword_10159C04
extern "C" uint32_t zpatch_ooc_state;    // dword_10159BFC
// The original OOC decoder/archive working state (sub_1004B570/sub_1004B150 and
// the fake-pak vector builder): same destinations keep the ordered writes.
extern "C" uint32_t zpatch_ooc_input_bound;      // dword_101887D4
extern "C" uint32_t zpatch_ooc_output_begin;    // dword_101A842C
extern "C" uint32_t zpatch_ooc_output_cursor;   // dword_101A8430
extern "C" uint32_t zpatch_ooc_output_end;      // dword_10196374
extern "C" uint32_t zpatch_ooc_table_size;      // dword_10159C2C
extern "C" uint32_t zpatch_ooc_archive_offset;  // dword_10159C78
extern "C" uint32_t zpatch_ooc_vector_begin;    // Block (dword_1016E43C)
extern "C" uint32_t zpatch_ooc_vector_end;      // dword_1016E440
extern "C" uint32_t zpatch_ooc_vector_capacity; // dword_1016E444
extern "C" const unsigned char zpatch_ooc_blob[];    // byte_100B6798
// The wrapper's two literal arguments are original .rdata 0x1012EB58/0x1012EB0C;
// "mor" starts the "memory:$<ptr>,<size>,0:" path the format builds.
extern "C" char zpatch_ooc_fmt_a[];           // "mor"
extern "C" char zpatch_ooc_fmt_b[];           // "me%sy%c%c%p,%d,%c%c%s"
// HighQualityReflections: the resume (match+0x13) and builder (match + *(match+0xF)
// + 0x13) the callback publishes, the register-dump scratch, and the two argument
// globals (flags = 0x11D; mode = the BSS slot the original never writes, so 0).
// No zpatch_hqr_* globals: HighQualityReflections is not implemented in this
// tree, so its slots had declarations with no definitions or users.
// CutsceneFixes chain B: the worker's two publishes, the body's accumulator and
// the read-only constants it compares against (original .rdata/.data bits).
extern "C" uint32_t zpatch_cutscene_target;   // dword_1015BDE4
extern "C" uint32_t zpatch_cutscene_poke;     // dword_1015BDF0
extern "C" uint64_t zpatch_cutscene_accum;    // dword_1015BDE8 (double)
extern "C" uint64_t zpatch_cutscene_zero;     // 0x101522F0 = 0.0
extern "C" uint64_t zpatch_cutscene_limit;    // 0x10152340 = 0.333333...
extern "C" uint32_t zpatch_cutscene_abs_mask; // 0x101523D0 = 0x7FFFFFFF
extern "C" uint32_t zpatch_cutscene_eps_a;    // 0x101522F8 = 0.05f
extern "C" uint32_t zpatch_cutscene_eps_b;    // 0x101522FC = 0.5f
// NewMemorySystem: the allocator singleton the handler's second scan publishes
// (dword_10159CD8); the E8 body writes our CRageAllocator vtable at it.
extern "C" uint32_t zpatch_nms_object;        // dword_10159CD8

