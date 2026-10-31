// Stub hook bodies for the write-set dump tool: the dump only needs their
// addresses (they appear as jump targets), never their code - the real
// bodies are 32-bit assembly. stub_bodies.h is the declaration list; including
// it here makes the compiler check every definition against it.
#include "pch.h"

#include <cstdint>

#include "common.h"
#include "xlive/xliveless.h"
#include "stub_bodies.h"

void abi_aim_projectile_crash_fix() {}
void abi_bad_player_model() {}
extern "C" void abi_benchmark_a() {}
extern "C" void abi_benchmark_b() {}
int __cdecl abi_better_mp_sync() { return {}; }
void abi_bike_physics_detour() {}
void abi_blackscreen_shim() {}
extern "C" LRESULT __stdcall abi_borderless_a(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam) { return {}; }
extern "C" LRESULT __stdcall abi_borderless_w(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam) { return {}; }
extern "C" void abi_building_dynamic_shadows() {}
void abi_car_decals_fix() {}
void abi_carparts_shadow() {}
void abi_carparts_shadow_2() {}
void abi_carparts_shadow_2b() {}
void abi_collision_deadlock_walk() {}
int __cdecl abi_combined_radio_freeze_fix() { return {}; }
extern "C" void abi_copsirens_a() {}
extern "C" void abi_copsirens_b() {}
extern "C" void abi_copsirens_ide() {}
extern "C" void abi_crash_msg_shim() {}
void abi_cutscene_chain_b() {}
extern "C" std::uint8_t abi_death_music_enabled() noexcept { return {}; }
extern "C" char abi_death_music_install() noexcept { return {}; }
void abi_driveby_fov_shim() {}
extern "C" void abi_dual_headlights_new() {}
extern "C" void abi_dual_headlights_old() {}
void abi_emissive_lerp_shim() {}
void abi_episode_switch_shim_b() {}
void abi_episode_switch_shim_c() {}
void abi_episode_switch_store_shim() {}
void abi_episode_switch_sum() {}
void abi_episode_switch_vcall() {}
void abi_fchs_shim() {}
void abi_freeze_gun_shim() {}
unsigned int __cdecl abi_game_hash(const char* text) { return {}; }
void abi_garage_body_a() {}
void abi_garage_body_b() {}
void abi_gfwl_stub() {}
void abi_group_hack_shim() {}
void abi_hqr_body() {}
void* __fastcall abi_improved_shader_streaming_find_hpe(void* self, int, const void* query) { return {}; }
void* __fastcall abi_improved_shader_streaming_find_v(void* self, int, const void* query) { return {}; }
int __stdcall abi_load_dlcs_dummy(int, int) { return {}; }
void abi_load_dlcs_struct_fixup() {}
void abi_local_settings_detour() {}
void abi_local_settings_fallback_detour() {}
void abi_mf_angle_step() {}
void abi_mouse_advance() {}
void abi_mouse_angle() {}
void abi_mouse_axis_flag() {}
void abi_mouse_axis_float() {}
void abi_mouse_axis_int() {}
void abi_mouse_axis_neg() {}
void abi_new_saves_compat() {}
extern "C" std::int32_t abi_new_saves_p3_hook() noexcept { return {}; }
extern "C" std::uint8_t abi_new_saves_read_hook(void*, std::uint32_t size) noexcept { return {}; }
extern "C" void* abi_newfiles_cb(void** a1) { return {}; }
extern "C" void abi_newfiles_register_body() {}
extern "C" char __fastcall abi_newfiles_update_a(void* self, int) { return {}; }
extern "C" char __fastcall abi_newfiles_update_b(void* self, int) { return {}; }
extern "C" void __cdecl abi_nms_apply() {}
extern "C" void* abi_nms_ctor() { return {}; }
extern "C" void __stdcall abi_nms_noop_arg(void*) {}
extern "C" void __stdcall abi_nms_noop_pair(void*, void*) {}
extern "C" void __cdecl abi_nms_noop_ret0() {}
extern "C" bool __cdecl abi_nms_reject() { return {}; }
extern "C" bool __stdcall abi_nms_reject_ptr(void*) { return {}; }
extern "C" void* __fastcall abi_nms_self(void* self, void* /*edx*/, void*) { return {}; }
extern "C" void abi_nms_thunk18() {}
extern "C" void* __fastcall abi_nms_zero_ptr(void* /*self*/, void* /*edx*/, void*) { return {}; }
extern "C" void abi_nolivery_a() {}
extern "C" void abi_nolivery_b() {}
extern "C" void abi_nolivery_c() {}
extern "C" void abi_nolivery_d() {}
extern "C" void abi_nolivery_e() {}
void abi_ooc_branch() {}
extern "C" std::uint32_t abi_ooc_wrap(std::uint32_t arg0, std::uint32_t arg1) noexcept { return {}; }
void abi_path_node_shim() {}
bool __fastcall abi_ped_models_in_cutscenes_check(const void* self, void*) { return {}; }
void abi_plane_crash_shim() {}
void abi_player_ped_components_shim() {}
void abi_popup_headlights() {}
void abi_popup_headlights_edx() {}
int __cdecl abi_rgsc_login_removed() { return {}; }
int __stdcall abi_savegame_fix_close(void* handle) { return {}; }
int __stdcall abi_savegame_fix_copy(const void* src, std::uint32_t size, void* dst,
                                    std::uint32_t* out_size, int) { return {}; }
int __stdcall abi_savegame_fix_mutex(void*, void** out_handle) { return {}; }
int __stdcall abi_savegame_fix_null(int, int) { return {}; }
int __cdecl abi_shader_stream_filter_hpe(const void*, const void* name) { return {}; }
int __cdecl abi_shader_stream_filter_v(const void*, const void* name) { return {}; }
void abi_skip_intro_body() {}
void abi_skip_menu_body() {}
void abi_smpa60_shim() {}
void abi_smpa60_tail_shim() {}
void abi_spawns_case_0x24() {}
void abi_spawns_case_0x3D() {}
void abi_spawns_case_0x56() {}
void abi_spawns_case_0x6F() {}
void abi_spawns_case_0x88() {}
void abi_spawns_case_0x9E() {}
void abi_spawns_case_0xB4() {}
void abi_spawns_case_0xCA() {}
void abi_spawns_case_0xE0() {}
void abi_spawns_case_0xF6() {}
void abi_spawns_guard() {}
void abi_spawns_probe() {}
extern "C" void* __fastcall abi_speedup_body(void* self, int) { return {}; }
void abi_steer_detour_eax() {}
void abi_steer_detour_edx() {}
void abi_super_lod_fix_shim() {}
void abi_vram_fix_shim() {}
void abi_wanted_level_shim() {}

// Launcher stages and menu callbacks are not part of the installer pass.
PatchResult ApplySkipLauncher(GameImage, SkipHookResolver, void*, SkipLauncherReport&,
                               SkipLauncherStage) noexcept
{
	return {PatchResultCode::unsupported_platform, "stub"};
}

std::uintptr_t ResolveSkipHook(SkipHook, void*) noexcept { return 0; }
void SetCreateFileSlot(std::uint32_t slot_va) noexcept { (void)slot_va; }

// hooks.h declares these with C linkage; the dump never calls them.
extern "C" std::uint8_t abi_menu_death_get() noexcept { return 0; }
extern "C" void abi_menu_death_set(std::uint32_t, std::uint32_t) noexcept {}
extern "C" std::uint8_t abi_menu_focus_get() noexcept { return 0; }
extern "C" void abi_menu_focus_set(std::uint32_t, std::uint32_t) noexcept {}
extern "C" void zpatch_menu_prime_focus_loss() noexcept {}
extern "C" void zpatch_local_settings_capture_cwd() noexcept {}
