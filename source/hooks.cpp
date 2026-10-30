// SPDX-License-Identifier: GPL-3.0-or-later
//
// The machine-level bodies the game jumps or calls into, plus the reference-VA
// registry that binds them. Every body keeps the original site's calling
// convention and register contract; the comment above each one names the
// original destination VA. Resumes are tail-jumped, never called.

#include "common.hxx"
#include "game.hxx"
#include "hooks.h"
#include "catalogue.hxx"
#include "log.hxx"
#include "pattern.hxx"
#include "rebased.h"
#include "driveby_seh.h"
#include "data/ooc_payload.h"

#include <shlwapi.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <intrin.h>
#include <new>
#include <string>
#include <xmmintrin.h>

// ---------------------------------------------------------------------------
// Hook bodies: the game jumps or calls straight into these.
// ---------------------------------------------------------------------------

#if defined(_WIN32)

using namespace opp;

// ---------------------------------------------------------------------------
// Runtime-pointer failsafes
//
// These bodies are transcribed from the original, which calls its preamble
// pointers and game-published lookups without checking them: on a build where a
// pointer is missing, the original faults. With the failsafe group on (the
// default; OPCEP_IMPROVEMENTS=except:failsafe restores the original fault) a
// missing pointer makes the body continue with the value the game would have
// had without the fix, and logs one line, so a partially-matching build
// degrades instead of dying. The guard is always at the top of the body, before
// any register or stack state the resume expects is touched.
// ---------------------------------------------------------------------------
static bool zpatch_failsafe_skip(const char* what, bool missing) noexcept
{
    if (zpatch_opt_failsafe == 0u || !missing)
        return false;
    opp::Log("failsafe: %s is missing, continuing without the fix", what);
    return true;
}

// Game structure offsets used by the helpers below; the inline asm keeps the
// numeric operands because MASM needs compile-time constants.
constexpr size_t kModelIndexOffset = 0x2E;
constexpr size_t kVehicleSteeringOffset = 0x10D8;
constexpr size_t kVehicleBoneOffset = 0xE18;
constexpr size_t kVehicleLockOwnerOffset = 0x84;
constexpr size_t kVehicleRpmOffset = 0x10C8;
constexpr size_t kVehicleFlagsOffset = 0xF64;
constexpr size_t kVehicleEngineStateOffset = 0x1354;
constexpr size_t kVehicleSuspensionTravelOffset = 0x1F3C;
constexpr size_t kVehiclePitchOffset = 0x1F08;
constexpr size_t kVehicleRollOffset = 0x1F04;
constexpr size_t kVehicleHeaveOffset = 0x1F00;
constexpr uint32_t kModelTablePointerRva = 0x150FDD0u;
constexpr uint32_t kModelTableCountRva = 0x150FDD4u;

extern "C"
{

// PlayerAsPedComponentsFix helper (original sub_1003D930): compare the object's
// model (+0x2E) against the "player" model index. The shim stores the object
// (EAX) first; C linkage so the shim's asm can call it by name.
static void zpatch_cookie_tail() noexcept;  // defined with the cookie shim

int32_t abi_player_ped_helper() noexcept
{
    int32_t index = -1;
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    // 0x1003D960 `call dword_10159C0C` is unguarded, so a null lookup faults.
    zpatch_player_ped_computed = 0;  // original writes 0 before the lookup
    if (!zpatch_failsafe_skip("PlayerAsPedComponentsFix model lookup", lookup == nullptr))
        lookup(GameHash("player"), &index);
    const auto* object = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(zpatch_player_ped_object));
    // The object is the argument the shim stashed; a clean skip keeps the
    // original's "no match" answer (model -1) instead of dereferencing null.
    const int16_t model =
        zpatch_failsafe_skip("PlayerAsPedComponentsFix object", object == nullptr)
            ? static_cast<int16_t>(-1)
            : *reinterpret_cast<const int16_t*>(object + kModelIndexOffset);
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
    int32_t index = -1;
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_super_lod_lookup));
    // A missing lookup or object answers "not the SuperLOD model", which is the
    // answer the site gives when the fix does not apply.
    if (zpatch_failsafe_skip("SuperLODFix lookup", lookup == nullptr) ||
        zpatch_failsafe_skip("SuperLODFix object", object == nullptr))
        return false;
    lookup(0xAE4B15D6u, &index);  // sub_100469A0's call is unguarded
    const auto* o = static_cast<const uint8_t*>(object);
    return *reinterpret_cast<const int16_t*>(o + kModelIndexOffset) == index;
}

// HighFPSBikePhysicsFix helper (original sub_1001EF30): clamp zpatch_bike_value
// below 0.006666666665 to 0x3BDA740E. SSE in the original.
void abi_bike_physics_helper() noexcept
{
    float value = 0.0f;
    std::memcpy(&value, &zpatch_bike_value, sizeof(value));
    if (0.006666666665 > static_cast<double>(value))
        zpatch_bike_value = 0x3BDA740Eu;
}

// PopupHeadlightsFlag checker (original sub_10044310), called between
// pushad/popad: look up 0xEB9F21D3 ("popuplights"), walk the +popuplights list,
// then copy the last matching [PopupLightAngles] angle (starts at pi/2). A
// failed lookup leaves -1, which matches a model of -1, as in the original.
void abi_popup_check() noexcept
{
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    zpatch_popup_angle = 0x3FC90FDBu;  // dword_101580CC: pi/2 float bits
    zpatch_popup_flag = 0;             // dword_1015BE24
    if ((zpatch_opt_model_lookup_guards != 0u || zpatch_opt_failsafe != 0u) &&
        lookup == nullptr)
        return;  // opt-in / failsafe: a preamble miss is a clean skip
    // PopupLookupCache: the hash -> model-index mapping is static, so remember
    // the last resolution per hash. Direct-mapped, collision just re-resolves.
    static uint32_t cache_hash[512] = {};
    static int32_t cache_index[512] = {};
    static uint8_t cache_valid[512] = {};
    const auto resolve = [&](uint32_t hash) -> int32_t
    {
        const size_t slot = (hash * 2654435761u) >> 23;
        if (zpatch_opt_popup_cache != 0u && cache_valid[slot] != 0u &&
            cache_hash[slot] == hash)
            return cache_index[slot];
        int32_t resolved = -1;
        lookup(hash, &resolved);  // sub_10044310's call is unguarded by default
        if (zpatch_opt_popup_cache != 0u)
        {
            cache_hash[slot] = hash;
            cache_index[slot] = resolved;
            cache_valid[slot] = 1;
        }
        return resolved;
    };
    int32_t out = resolve(0xEB9F21D3u);
    zpatch_popup_index = static_cast<uint32_t>(out);  // dword_1015BE1C
    if (out == static_cast<int32_t>(zpatch_popup_model))
    {
        zpatch_popup_flag = 1;
        return;
    }
    for (uint32_t i = 0; i < 512; ++i)
    {
        const uint32_t hash = zpatch_popuplights_models[i];
        if (hash == 0) return;
        out = resolve(hash);
        if (out != static_cast<int32_t>(zpatch_popup_model)) continue;
        zpatch_popup_flag = 1;
        for (uint32_t k = 0; k < 512; ++k)
        {
            if (zpatch_popup_hashes[k] != 0 &&
                zpatch_popup_hashes[k] == hash)
            {
                uint32_t bits = 0;
                std::memcpy(&bits, &zpatch_popup_angles[k], sizeof(bits));
                zpatch_popup_angle = bits;
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
    const uint32_t* const hashes = zpatch_fchs_hashes;
    uint32_t hash = hashes[0];
    zpatch_fchs_offset = 0x40000000u;  // 2.0f, as the original
    if (hash == 0u) return;
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    // 0x1001AD5C `call dword_10159C0C` and 0x1001AD62 `movsx eax,word [v2+2Eh]`
    // are unguarded in the original.
    const auto* base = reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(zpatch_fchs_param));
    if (zpatch_opt_model_lookup_guards != 0u || zpatch_opt_failsafe != 0u)
    {
        if (lookup == nullptr || base == nullptr)
            return;  // a preamble/parameter miss is a clean skip
    }
    static uint32_t cache_hash[512] = {};
    static int32_t cache_index[512] = {};
    static uint8_t cache_valid[512] = {};
    const auto resolve = [&](uint32_t value) -> int32_t
    {
        const size_t slot = (value * 2654435761u) >> 23;
        if (zpatch_opt_popup_cache != 0u && cache_valid[slot] != 0u &&
            cache_hash[slot] == value)
            return cache_index[slot];
        int32_t resolved = -1;
        lookup(value, &resolved);
        if (zpatch_opt_popup_cache != 0u)
        {
            cache_hash[slot] = value;
            cache_index[slot] = resolved;
            cache_valid[slot] = 1;
        }
        return resolved;
    };
    for (uint32_t i = 0;;)
    {
        const int32_t index = resolve(hash);
        if (static_cast<int32_t>(
                *reinterpret_cast<const int16_t*>(base + kModelIndexOffset)) == index)
        {
            float value = 0.0f;
            std::memcpy(&value, &zpatch_fchs_offsets[i], sizeof(value));
            std::memcpy(&zpatch_fchs_offset, &value, sizeof(value));
            return;
        }
        ++i;
        // The original walks into adjacent .data instead; layout-specific and
        // unreproducible.
        if (i == 512u) return;
        hash = hashes[i];
        if (hash == 0u) return;
    }
}

// Native-script "call by hash" layer (sub_100038E0 and the sub_1005Bxxx
// wrappers): the resolver (dword_1015BDE0, game 0x626AF0 on 1.0.8.0) takes the
// hash in ECX/ESI and the 0xA4-byte call block in EDX; the handler is cdecl with
// one argument. The original always returns (char)1 and skips a null handler.
extern "C" uint32_t zpatch_script_resolve(uint32_t hash);
extern "C" uint32_t zpatch_script_resolve_with_block(uint32_t hash,
                                                           void* block);
extern "C" uint32_t __cdecl zpatch_script_resolve_seh(uint32_t hash,
                                                            uint32_t edx_arg);
// Unmangled so the inline asm can call it (the declaration is in namespace
// zpatch).
__declspec(naked) uint32_t zpatch_script_resolve(uint32_t)
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
__declspec(naked) uint32_t zpatch_script_resolve_with_block(uint32_t,
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

static uint32_t __cdecl zpatch_resolver_call(uint32_t hash,
                                                  uint32_t edx_arg)
{
    return zpatch_script_resolve_with_block(
        hash, reinterpret_cast<void*>(static_cast<uintptr_t>(edx_arg)));
}

extern "C" __declspec(noinline) uint32_t __cdecl
zpatch_script_resolve_seh(uint32_t hash, uint32_t edx_arg)
{
    return detail::driveby_resolve_seh(&zpatch_resolver_call, hash, edx_arg);
}

extern "C" __declspec(noinline) uint32_t __cdecl
zpatch_driveby_resolve_seh(uint32_t hash, uint32_t edx_arg)
{
    return zpatch_script_resolve_seh(hash, edx_arg);
}

struct zpatch_native_block final
{
    uint32_t m_p_return;   // +0x00 = &args
    uint32_t m_arg_count;  // +0x04
    uint32_t m_p_args;     // +0x08 = &args
    uint32_t m_out_count;  // +0x0C
    uint32_t m_out_ptrs[4];  // +0x10
    struct record final { uint32_t v[3]; uint32_t padding; };
    record m_records[4];        // +0x20 (sub_100038C0 zeroes v[0..2] of each)
    uint32_t args[17];     // +0x60 (arg 0 is the return slot)
};

extern "C" char zpatch_script_call_by_hash(uint32_t hash, void* block) noexcept
{
    const auto handler = reinterpret_cast<void(__cdecl*)(void*)>(
        static_cast<uintptr_t>(zpatch_script_resolve_with_block(hash, block)));
    if (handler) handler(block);
    return 1;
}

// One builder covers every wrapper: the sub_1005DE40/DE80 cleanup loops are
// identical (copy out each pending record, then return args[0]; DE80 returns
// only its low byte).
extern "C" uint32_t zpatch_script_call(uint32_t hash, uint32_t nargs,
                                            const uint32_t* args) noexcept
{
    zpatch_native_block block;
    std::memset(&block, 0, 0xA0);
    for (auto& record : block.m_records)
    {
        record.v[0] = 0;
        record.v[1] = 0;
        record.v[2] = 0;
    }
    const auto base = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(block.args));
    block.m_p_return = base;
    block.m_p_args = base;
    block.m_arg_count = nargs;
    for (uint32_t i = 0; i < nargs && i < 17u; ++i) block.args[i] = args[i];
    zpatch_script_call_by_hash(hash, &block);
    while (block.m_out_count != 0u)
    {
        --block.m_out_count;
        auto* destination = reinterpret_cast<uint32_t*>(
            static_cast<uintptr_t>(block.m_out_ptrs[block.m_out_count]));
        const auto& source = block.m_records[block.m_out_count];
        destination[0] = source.v[0];
        destination[1] = source.v[1];
        destination[2] = source.v[2];
    }
    return block.args[0];
}

extern "C" uint8_t zpatch_script_call_bool(uint32_t hash, uint32_t nargs,
                                                const uint32_t* args) noexcept
{
    return static_cast<uint8_t>(zpatch_script_call(hash, nargs, args));
}

// The MP-only diagnostic name lookup of sub_10003F00 walks the original ASI's
// own 38455-entry model-name table (0x100DE770); single-player never reaches
// this path (the caller is gated on GTAC.dll).
extern "C" const char* zpatch_bpm_model_name(uint32_t hash) noexcept
{
    // Return the first table name whose GameHash matches; "**UNKNOWN**" on a miss.
    return ModelNameFromHash(hash).data();
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
namespace
{
const char* zpatch_spawns_who_c(uint32_t id) noexcept
{
    // The original reads the native hash from [0x101580C8] (zpatch_salt_c8); the
    // loader's probe-miss fallback rewrites that slot on 1000/1010/1040.
    return reinterpret_cast<const char*>(static_cast<uintptr_t>(
        zpatch_script_call(zpatch_salt_c8, 1, &id)));
}
}  // namespace
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
namespace
{
void zpatch_spawns_print_c(const char* text) noexcept
{
    const uint32_t arg = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(text));
    zpatch_script_call(0x79023CFCu, 1, &arg);
}
}  // namespace
__declspec(naked) void zpatch_spawns_print_edx()
{
    __asm {
        push edx
        call zpatch_spawns_print_c
        add  esp, 4
        ret
    }
}

// sub_10001280: the original's bounded formatter (buf, size, fmt, ...).
extern "C" int zpatch_spawns_sprintf(char* buffer, size_t size,
                                     const char* format, ...)
{
    va_list args;
    va_start(args, format);
    const int result = _vsnprintf_s(buffer, size, _TRUNCATE, format, args);
    va_end(args);
    return result;
}

// sub_1005FE7B: the /GS cookie check (cookie in ECX). It is the last call on
// the probe's return path, so it must preserve EAX (the model index); the
// original is `cmp ecx,[0x1015800C]; jne fail; ret`.
extern "C" void zpatch_spawns_cookie_check_ecx();
extern "C" uintptr_t __security_cookie;
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
        static_cast<uintptr_t>(zpatch_pre_ped_array));
    if (zpatch_failsafe_skip("BadPlayerModelCrashFix model table", models == nullptr))
        return;  // nothing to remap: the site keeps the model it was given
    void* object = models[zpatch_bpm_model_index];
    if (object == nullptr) return;
    void** vtable = *reinterpret_cast<void***>(object);
    // sub_10037AC0 leaves ECX = the models-table base (dword_1015AD78) before
    // `call eax`; preserve that contract (odd as it looks).
    const char class_id = reinterpret_cast<char(__thiscall*)(void*)>(vtable[3])(
        const_cast<void*>(reinterpret_cast<const void*>(models)));
    if (class_id == 6) return;
    if (GetModuleHandleW(L"GTAC.dll") != nullptr)
    {
        const uint32_t modelHash =
            *reinterpret_cast<const uint32_t*>(
                static_cast<const uint8_t*>(object) + 0x3C);
        const char* model_name = zpatch_bpm_model_name(modelHash);
        uint32_t entity = zpatch_bpm_obj;
        const char* who = reinterpret_cast<const char*>(static_cast<uintptr_t>(
            zpatch_script_call(zpatch_salt_c8, 1, &entity)));
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s used a non-ped model (%s)", who,
                      model_name);
        const uint32_t text = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(buffer));
        zpatch_script_call(0x79023CFCu, 1, &text);
    }
    const uint32_t hash = GameHash("m_y_multiplayer");
    if (zpatch_script_call_bool(zpatch_salt_b8, 1, &hash) == 0)
    {
        const auto detour = reinterpret_cast<void(__cdecl*)(uint32_t, uint32_t)>(
            static_cast<uintptr_t>(zpatch_pre_fn_59c74));
        if (!zpatch_failsafe_skip("BadPlayerModelCrashFix script detour",
                                  detour == nullptr))
            detour(hash, 0);
        zpatch_script_call(zpatch_salt_40, 0, nullptr);
    }
    int32_t new_index = -1;
    const auto lookup = reinterpret_cast<void(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    if (!zpatch_failsafe_skip("BadPlayerModelCrashFix model lookup",
                              lookup == nullptr))
    {
        lookup(hash, &new_index);
        zpatch_bpm_model_index = static_cast<uint32_t>(new_index);
    }
}

// RestoreDeathMusic (original sub_10050A50): the death-music gate, gated on
// byte_10159B86 and answered through the shipped hash-salt script calls.
extern "C" uint8_t abi_death_music_enabled() noexcept
{
    if (!zpatch_flag_death_music) return 0;
    uint32_t queried = 0;
    const uint32_t query_args[2] = {
        zpatch_script_call(zpatch_salt_5c, 0, nullptr),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&queried))};
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
        const auto* module = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
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
                    const ModuleView view = ExecutableView({module, nt->OptionalHeader.SizeOfImage});
                    if (FindPattern(view, "6A 06 68 ? ? ? ? 8D 8C", match))
                        zpatch_death_music_site = static_cast<uint32_t>(
                            reinterpret_cast<uintptr_t>(match.address));
                }
            }
        }
    }
    if (zpatch_death_music_site != 0)
    {
        auto* site = reinterpret_cast<uint8_t*>(
            static_cast<uintptr_t>(zpatch_death_music_site) + 1);
        injector::WriteMemory<uint8_t>(  // push imm8
            site, static_cast<uint8_t>(std::rand() % 5 + 1), true);
    }
    return 1;
}

// The NewSavesCompatibility converter (original sub_10051B30): parse the save's
// 0x110-byte header (magic "SAVE" at +0xC), walk 18 chunks of 9-byte header +
// payload, record the new-format chunk sizes, then patch three game sites.
namespace
{
// Byte/word/dword stores through VirtualProtect. As in every original handler,
// the result is ignored and the write happens anyway (an unmapped target faults).
void wp_byte(void* address, uint8_t value) noexcept
{
    injector::WriteMemory<uint8_t>(address, value, true);  // byte store
}
void wp_dword(void* address, uint32_t value) noexcept
{
    injector::WriteMemory<uint32_t>(address, value, true);  // dword store
}
void wp_word(void* address, uint16_t value) noexcept
{
    injector::WriteMemory<uint16_t>(address, value, true);  // word store
}
const uint8_t* scan_game(std::string_view signature) noexcept
{
    const auto* module = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
    if (!module) return nullptr;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    Match match;
    const ModuleView view = ExecutableView({module, nt->OptionalHeader.SizeOfImage});
    if (!FindPattern(view, signature, match)) return nullptr;
    return match.address;
}
}  // namespace

// The original's path/name helpers call the secure CRT (`strcpy_s`/`wcscpy_s`)
// through its IAT; the volatile pointer keeps a distinct secure-copy call.
namespace
{
using ZpStrcpyS = int(__cdecl*)(char*, size_t, const char*);
volatile ZpStrcpyS zpatch_secure_strcpy_s =
    static_cast<ZpStrcpyS>(&strcpy_s);
}  // namespace

// The 0x104-byte path buffer the original's LocalSettingsPath helpers use.
constexpr size_t kLocalPathCapacity = 0x104u;

// LocalSettingsPath: the CWD captured at load, the "\GTA IV" folder suffix the
// game's path helper is retargeted to push, the four-byte call slot the game
// calls through (original off_10158064 = sub_10051320), and the five path
// pointers the detour rewrites.
extern "C" int __stdcall zpatch_local_copy_saves(uint32_t, uint32_t,
                                                 uint32_t, uint32_t,
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
    GetCurrentDirectoryW(kLocalPathCapacity, zpatch_local_wide_cwd);
    zpatch_local_saves_slot = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(&zpatch_local_copy_saves));
}

// The detours' helper (original sub_10050D10): rewrites the game's four
// just-computed settings/save path buffers to "<...>\saves\<episode>\" (the
// first ANSI destination is hardcoded "GTA IV"). Called between pushad/popad.
namespace
{
void zpatch_build_ansi_path(char* out, const char* name) noexcept
{
    const char prefix[] = "\\saves\\";
    size_t o = 0;
    for (const char* s = prefix; *s; ++s) out[o++] = *s;
    for (const char* s = name; *s; ++s) out[o++] = *s;
    out[o++] = '\\';
    out[o] = '\0';
}
void zpatch_build_wide_path(wchar_t* out, const wchar_t* name) noexcept
{
    const wchar_t prefix[] = L"\\saves\\";
    size_t o = 0;
    for (const wchar_t* s = prefix; *s; ++s) out[o++] = *s;
    for (const wchar_t* s = name; *s; ++s) out[o++] = *s;
    out[o++] = L'\\';
    out[o] = 0;
}
// The original copies with strcpy_s/wcscpy_s (cap 0x200); these write the same
// bytes without the static CRT's secure-copy path.
void zpatch_copy_path(char* dst, size_t cap, const char* src) noexcept
{
    size_t n = 0;
    while (n + 1 < cap && src[n] != '\0')
    {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = '\0';
}
void zpatch_copy_wpath(wchar_t* dst, size_t cap, const wchar_t* src) noexcept
{
    size_t n = 0;
    while (n + 1 < cap && src[n] != 0)
    {
        dst[n] = src[n];
        ++n;
    }
    dst[n] = 0;
}
}  // namespace

// The cert cap the original's strcpy_s/wcscpy_s path copies use.
constexpr size_t kSettingsPathCapacity = 0x200u;

extern "C" void zpatch_local_settings_step() noexcept
{
    const int episode = zpatch_local_episode_ptr
                            ? *reinterpret_cast<const int*>(
                                  static_cast<uintptr_t>(
                                      zpatch_local_episode_ptr))
                            : 0;
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
    char ansi_iv[16];
    char ansi_episode[32];
    wchar_t wide_episode[32];
    // sub_10050D10 copies the literal "\saves\GTA IV\" into ansi_iv, independent
    // of the episode.
    zpatch_build_ansi_path(ansi_iv, "GTA IV");
    zpatch_build_ansi_path(ansi_episode, ansi_name);
    zpatch_build_wide_path(wide_episode, wide_name);
    if (zpatch_local_path_18)
        zpatch_copy_path(reinterpret_cast<char*>(static_cast<uintptr_t>(
                             zpatch_local_path_18)),
                         kSettingsPathCapacity, ansi_iv);
    if (zpatch_local_path_08)
        zpatch_copy_path(reinterpret_cast<char*>(static_cast<uintptr_t>(
                             zpatch_local_path_08)),
                         kSettingsPathCapacity, ansi_episode);
    if (zpatch_local_path_1c)
        zpatch_copy_wpath(reinterpret_cast<wchar_t*>(static_cast<uintptr_t>(
                              zpatch_local_path_1c)),
                          kSettingsPathCapacity, wide_episode);
    if (zpatch_local_path_10)
        zpatch_copy_wpath(reinterpret_cast<wchar_t*>(static_cast<uintptr_t>(
                              zpatch_local_path_10)),
                          kSettingsPathCapacity, wide_episode);
    zpatch_cookie_tail();
}

// GarageModelFix helpers (originals sub_10046B90 / sub_10046BD0): stash the
// vehicle's model hash across four scattered bytes of the garage object
// (skipped in MP), then rebuild it and repair out-of-range garage models.
// The originals take ECX=object, EDX=vehicle; naked wrappers forward to these.
namespace
{
void zpatch_garage_store_c(uint8_t* object, const uint8_t* vehicle) noexcept
{
    if (zpatch_citizen_present) return;
    const int16_t model =
        *reinterpret_cast<const int16_t*>(vehicle + 46);
    const auto base = static_cast<uintptr_t>(zpatch_pre_ped_array);
    if (zpatch_failsafe_skip("GarageModelFix model table", base == 0u))
        return;  // nothing stashed: the lookup side reads hash 0 and keeps the model
    const uint32_t entry = *reinterpret_cast<const uint32_t*>(
        base + 4u * static_cast<uint32_t>(model));
    const uint32_t hash = *reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(entry)) + 60);
    object[64] = static_cast<uint8_t>(hash >> 8);
    object[19] = static_cast<uint8_t>(hash);
    object[65] = static_cast<uint8_t>(hash >> 16);
    object[70] = static_cast<uint8_t>(hash >> 24);
}
int32_t zpatch_garage_lookup_c(uint8_t* object) noexcept
{
    uint32_t hash = object[19];
    hash |= (static_cast<uint32_t>(object[64]) |
             ((static_cast<uint32_t>(object[65]) |
               (static_cast<uint32_t>(object[70]) << 8))
              << 8))
            << 8;
    const int32_t original = static_cast<int32_t>(object[19]);
    if (hash == 0u) return original;
    // 0x10046C10 / 0x10046C4A call dword_10159C0C unguarded.
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    if (zpatch_failsafe_skip("GarageModelFix model lookup", lookup == nullptr))
        return original;  // the garage keeps the model it stored
    int32_t out = -1;
    lookup(hash, &out);
    if (out <= 0 || out >= 32000)
    {
        out = -1;
        lookup(0xC703DB5Fu, &out);
        *reinterpret_cast<int16_t*>(object + 16) = static_cast<int16_t>(out);
        *reinterpret_cast<uint16_t*>(object + 48) &= 0xFFFEu;
        return 65534;
    }
    *reinterpret_cast<int16_t*>(object + 16) = static_cast<int16_t>(out);
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
        cmp  DWORD PTR [zpatch_pre_ped_array], 0
        jne  garage_table_ok
        cmp  BYTE PTR [zpatch_opt_failsafe], 0
        jne  garage_done
    garage_table_ok:
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
extern "C" int32_t zpatch_garage_lookup_model();
__declspec(naked) int32_t zpatch_garage_lookup_model()
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

// MovingSteeringWheelSupport / ShakingEngineBlock / MovingPlaneParts.
// abi_vehicle_component is the original sub_10003F40: a name lookup in the
// model registry (entries at [registry], stride 0xE0, count +0x14, name +0,
// bone id +0x14). abi_steer_body is the shared consumer (original
// sub_10004C70), called between pushad/popad by the two detours.
extern "C" const void* abi_vehicle_component(const void* registry,
                                             const char* name) noexcept
{
    // sub_10003F40 dereferences the registry and each candidate name unguarded.
    const bool guarded = zpatch_opt_component_guards != 0u;
    if (guarded && registry == nullptr) return nullptr;
    const auto* entries = *reinterpret_cast<const uint8_t* const*>(registry);
    if (guarded && entries == nullptr) return nullptr;
    const auto count = *reinterpret_cast<const uint16_t*>(
        static_cast<const uint8_t*>(registry) + 0x14);
    for (uint16_t i = 0; i < count; ++i)
    {
        const auto* entry = entries + static_cast<size_t>(i) * 0xE0u;
        const char* candidate = *reinterpret_cast<const char* const*>(entry);
        if (guarded && candidate == nullptr) continue;
        if (_stricmp(candidate, name) == 0) return entry;
    }
    return nullptr;
}

// sub_10003980's script-float layout: ctx+0x0E holds the slot offset, the slot
// is at ctx+offset+0x18 and the float at slot+8.
constexpr size_t kScriptOffsetField = 0x0E;
constexpr size_t kScriptSlotBase = 0x18;
constexpr size_t kScriptFloatOffset = 8;

extern "C" uint32_t abi_steer_body() noexcept
{
    auto* const vehicle = reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(zpatch_steer_vehicle));
    const bool guarded = zpatch_opt_component_guards != 0u;
    if (guarded && vehicle == nullptr) return 0;

    using BoneFn = uint32_t*(__thiscall*)(void*, uint32_t);
    using SetFn = void(__thiscall*)(uint32_t*, float);
    using PartFn = void(__thiscall*)(void*, int, int, float, int, int, int);
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    const auto get_bone = reinterpret_cast<BoneFn>(
        static_cast<uintptr_t>(zpatch_pre_fn_59b88));
    const auto set_a = reinterpret_cast<SetFn>(
        static_cast<uintptr_t>(zpatch_pre_fn_5d63c));
    const auto set_b = reinterpret_cast<SetFn>(
        static_cast<uintptr_t>(zpatch_pre_fn_59cdc));
    const auto part_call = reinterpret_cast<PartFn>(
        static_cast<uintptr_t>(zpatch_steer_vtable_fn));

    // The moving-parts body calls four game functions and the model lookup; a
    // missing preamble pointer or a vehicle whose vtable slot was not published
    // would fault in the original. The clean skip returns 0, which the resume
    // reads as "no component moved this frame".
    if (zpatch_failsafe_skip("moving-parts model lookup", lookup == nullptr) ||
        zpatch_failsafe_skip("moving-parts bone lookup", get_bone == nullptr) ||
        zpatch_failsafe_skip("moving-parts setter A", set_a == nullptr) ||
        zpatch_failsafe_skip("moving-parts setter B", set_b == nullptr) ||
        zpatch_failsafe_skip("moving-parts component call", part_call == nullptr))
        return 0;

    // sub_10003980: read the current script float (resolver hash salt_3c).
    const auto script_float = [&]() -> float
    {
        const uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
        if (guarded && ctx == 0) return 0.0f;
        const uint32_t offset =
            *reinterpret_cast<const uint32_t*>(
                static_cast<uintptr_t>(ctx) + kScriptOffsetField);
        const uint32_t value = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(ctx) + offset + kScriptSlotBase);
        // The script slot can be empty before the script has run; the original
        // dereferences it (fault preserved when the folded guard is off).
        if (guarded && value == 0) return 0.0f;
        return *reinterpret_cast<const float*>(
            static_cast<uintptr_t>(value) + kScriptFloatOffset);
    };
    const auto model_of = [vehicle]()
    {
        return *reinterpret_cast<const int16_t*>(vehicle + kModelIndexOffset);
    };
    const auto identity = [](uint32_t* m)
    {
        m[0] = 0x3F800000u;
        m[1] = 0;
        m[2] = 0;
        m[4] = 0;
        m[5] = 0x3F800000u;
        m[6] = 0;
        m[8] = 0;
        m[9] = 0;
        m[10] = 0x3F800000u;
    };
    const auto bone_matrix = [&](int16_t model, const char* name) -> uint32_t*
    {
        const auto base = static_cast<uintptr_t>(zpatch_pre_ped_array);
        if (guarded && (base == 0u || model < 0)) return nullptr;
        const uint32_t ped = *reinterpret_cast<const uint32_t*>(
            base + 4u * static_cast<uint32_t>(model));
        if (guarded && ped == 0u) return nullptr;
        const uint32_t p1 = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(ped) + 8);
        if (guarded && p1 == 0u) return nullptr;
        const uint32_t p2 = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(p1) + 0xB4);
        if (guarded && p2 == 0u) return nullptr;
        const uint32_t registry = *reinterpret_cast<const uint32_t*>(
            static_cast<uintptr_t>(p2) + 0xC);
        if (guarded && registry == 0u) return nullptr;
        const auto* entry = static_cast<const uint8_t*>(abi_vehicle_component(
            reinterpret_cast<const void*>(static_cast<uintptr_t>(registry)), name));
        if (entry == nullptr) return nullptr;  // the original's own `if (v6)` check
        // sub_10004C70 0x10004D4A calls get_bone unguarded (it is null only when
        // the loader's preamble missed).
        const uint32_t bone =
            *reinterpret_cast<const uint16_t*>(entry + 0x14);
        return get_bone(vehicle, bone);
    };

    // The steering block dereferences the vehicle's lock owner. The folded guard
    // skips it while there is no owner (the original faults); the original path
    // keeps the plain `zpatch_flag_steering` condition.
    const int16_t steer_model = model_of();
    const uint32_t steer_lock_owner = *reinterpret_cast<const uint32_t*>(
        vehicle + kVehicleBoneOffset);
    if (zpatch_flag_steering && (!guarded || steer_lock_owner != 0u))
    {
        const int16_t model = steer_model;
        const float steering = *reinterpret_cast<const float*>(vehicle + kVehicleSteeringOffset);
        const uint32_t lock_owner = steer_lock_owner;
        const float lock = *reinterpret_cast<const float*>(
            static_cast<uintptr_t>(lock_owner) + kVehicleLockOwnerOffset);
        const float turn = static_cast<float>(static_cast<double>(steering / lock) *
                                              -3.141592653589793);
        float tilt = 0.0f;
        // SteerLookupCache: the wheelangle table is static after startup, so a
        // model's tilt is stable. Direct-mapped by model; a collision re-walks.
        struct SteerTiltCache final
        {
            int16_t model;
            float tilt;
            uint8_t valid;
        };
        static SteerTiltCache tilt_cache[64] = {};
        const size_t tilt_slot = static_cast<uint16_t>(model) & 63u;
        if (zpatch_opt_steer_cache != 0u && tilt_cache[tilt_slot].valid != 0u &&
            tilt_cache[tilt_slot].model == model)
        {
            tilt = tilt_cache[tilt_slot].tilt;
        }
        else
        {
            for (int i = 0; i < 512; ++i)
            {
                const uint32_t hash = zpatch_wheelangle_models[i];
                if (hash == 0u) break;
                int32_t resolved = -1;
                if (guarded && lookup == nullptr) break;
                lookup(hash, &resolved);  // sub_10004C70 0x10004CE7: unconditional
                if (resolved == model)
                {
                    tilt = static_cast<float>(
                        static_cast<double>(zpatch_wheelangle_angles[i]) *
                        3.141592653589793 / 180.0);
                    break;
                }
            }
            if (zpatch_opt_steer_cache != 0u)
            {
                tilt_cache[tilt_slot].model = model;
                tilt_cache[tilt_slot].tilt = tilt;
                tilt_cache[tilt_slot].valid = 1;
            }
        }
        if (model != -1)
        {
            uint32_t* matrix = bone_matrix(model, "steeringwheel");
            if (matrix != nullptr)
            {
                identity(matrix);
                if (set_a) set_a(matrix, tilt);
                if (set_b) set_b(matrix, turn);
            }
        }
    }
    if (zpatch_flag_engine)
    {
        if (zpatch_engine_last_timestep != script_float())
        {
            const double step = zpatch_engine_descending ? -0.5 : 0.5;
            const float phase = static_cast<float>(
                static_cast<double>(script_float()) * step +
                static_cast<double>(zpatch_engine_phase));
            zpatch_engine_phase = phase;
            uint8_t direction = zpatch_engine_descending;
            if (static_cast<double>(phase) > 0.01) direction = 1;
            if (static_cast<double>(phase) < -0.01) direction = 0;
            zpatch_engine_descending = direction;
        }
        const int16_t model = model_of();
        if (model != -1)
        {
            uint32_t* matrix = bone_matrix(model, "engineblock");
            if (matrix != nullptr)
            {
                identity(matrix);
                const uint32_t flags =
                    *reinterpret_cast<const uint32_t*>(vehicle + kVehicleEngineStateOffset);
                float scale = 0.0f;
                if ((flags & 8u) != 0u)
                {
                    const float rpm = *reinterpret_cast<const float*>(vehicle + kVehicleRpmOffset);
                    scale = static_cast<float>(static_cast<double>(rpm) * 0.75 + 0.25);
                }
                if ((flags & 0x10u) != 0u) scale = 1.0f;
                if (set_b) set_b(matrix, scale * zpatch_engine_phase);
            }
        }
    }
    if (zpatch_flag_plane &&
        *reinterpret_cast<const uint32_t*>(vehicle + kVehicleFlagsOffset) == 5u &&
        part_call != nullptr)
    {
        const float travel = *reinterpret_cast<const float*>(vehicle + kVehicleSuspensionTravelOffset);
        part_call(vehicle, 89, 4, script_float() * travel * 100.0f, 0, 0, 0);
        part_call(vehicle, 91, 4, script_float() * travel * 100.0f, 0, 0, 0);
        part_call(vehicle, 90, 4, script_float() * travel * 100.0f, 0, 0, 0);
        part_call(vehicle, 92, 4, script_float() * travel * 100.0f, 0, 0, 0);
        const float pitch = *reinterpret_cast<const float*>(vehicle + kVehiclePitchOffset);
        const float roll = *reinterpret_cast<const float*>(vehicle + kVehicleRollOffset);
        const float heave = *reinterpret_cast<const float*>(vehicle + kVehicleHeaveOffset);
        part_call(vehicle, 96, 3,
                  static_cast<float>(static_cast<double>(pitch) * 0.5), 1, 0, 0);
        part_call(vehicle, 97, 3,
                  static_cast<float>(static_cast<double>(pitch) * -0.5), 1, 0, 0);
        part_call(vehicle, 94, 3,
                  static_cast<float>(static_cast<double>(roll) * -0.5), 1, 0, 0);
        part_call(vehicle, 95, 3,
                  static_cast<float>(static_cast<double>(roll) * -0.5), 1, 0, 0);
        part_call(vehicle, 93, 5,
                  static_cast<float>(static_cast<double>(heave) * 0.5), 1, 0, 0);
    }
    // Unconditional tail: snapshot the last script timestep and return its slot.
    const uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
    if (guarded && ctx == 0u)
    {
        zpatch_engine_last_timestep = 0.0f;
        return 0;
    }
    const uint32_t offset = *reinterpret_cast<const uint32_t*>(
        static_cast<uintptr_t>(ctx) + kScriptOffsetField);
    const uint32_t value_ptr = *reinterpret_cast<const uint32_t*>(
        static_cast<uintptr_t>(ctx) + offset + kScriptSlotBase);
    if (guarded && value_ptr == 0u)
    {
        zpatch_engine_last_timestep = 0.0f;
        return 0;
    }
    const float last = *reinterpret_cast<const float*>(
        static_cast<uintptr_t>(value_ptr) + kScriptFloatOffset);
    zpatch_engine_last_timestep = last;
    return value_ptr;
}

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
bool zpatch_tls_static_header(uint32_t& guard) noexcept
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

void zpatch_tls_static_footer(uint32_t& guard) noexcept
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
            const uint8_t* found =
                scan_game("33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57");
            zpatch_gfwl_site = found == nullptr
                                           ? 0u
                                           : static_cast<uint32_t>(
                                                 reinterpret_cast<uintptr_t>(found));
            zpatch_tls_static_footer(zpatch_gfwl_scan_guard);
        }
        if (zpatch_gfwl_site != 0)
        {
            uint32_t target = 0;
            std::memcpy(&target,
                        reinterpret_cast<const void*>(
                            static_cast<uintptr_t>(zpatch_gfwl_site) + 30u),
                        4);
            // 0x10005427 writes *[site+30] without a null check.
            wp_dword(reinterpret_cast<void*>(target), 0);
        }
    }
    if (zpatch_flag_gfwl_popup)
    {
        // 0x10005421 writes xlive base + 0x66F770.. with no base check; with
        // xlive.dll absent the base is 0 and the original faults on that page.
        const auto base = static_cast<uintptr_t>(zpatch_xlive_base);
        // Without xlive.dll there is no GFWL update popup to remove: the four
        // writes have no meaning at base 0, so the failsafe just skips them.
        if (zpatch_failsafe_skip("RemoveGFWLUpdatePopup xlive.dll", base == 0u))
            return;
        wp_dword(reinterpret_cast<void*>(base + 6748016u), 0x10000E83u);
        wp_dword(reinterpret_cast<void*>(base + 6748020u), 0x10000E83u);
        wp_dword(reinterpret_cast<void*>(base + 7034320u), 0x10000E83u);
        wp_dword(reinterpret_cast<void*>(base + 7034352u), 0x10000E83u);
    }
}

extern "C" uint8_t abi_new_saves_read_hook(void*, uint32_t size) noexcept
{
    uint8_t scratch[3704];
    if (zpatch_new_saves_loaddata == 0) return 0;
    using LoadFn = uint8_t(__cdecl*)(void*, uint32_t);
    return reinterpret_cast<LoadFn>(
        static_cast<uintptr_t>(zpatch_new_saves_loaddata))(scratch, size);
}

extern "C" int32_t abi_new_saves_p3_hook() noexcept
{
    uint8_t buffer[12];
    using LoadFn = int32_t(__cdecl*)(void*, uint32_t);
    const auto load = reinterpret_cast<LoadFn>(
        static_cast<uintptr_t>(zpatch_new_saves_loaddata));
    if (zpatch_new_saves_flag13 != 0 && load)
        static_cast<void>(load(buffer, 12));
    using OrigFn = int32_t(__cdecl*)();
    const int32_t result = reinterpret_cast<OrigFn>(
        static_cast<uintptr_t>(zpatch_new_saves_p3_orig))();
    if (zpatch_new_saves_flag13 != 0 && load) return load(buffer, 12);
    return result;
}

// The converter's chunk scan, split out so the native tests can drive it
// without a game image. `guarded` enables the folded NewSavesCompatibility
// checks; `seek` enables the fseek-over-payload path. With both 0 this is the
// original loop: every fread result is ignored and the underflow/malloc are
// unchecked.
extern "C" bool zpatch_new_saves_scan(void* handle, int guarded, int seek,
                                      uint32_t* flag9, uint32_t* flag16,
                                      uint32_t* flag13) noexcept
{
    auto* file = static_cast<std::FILE*>(handle);
    if (file == nullptr) return false;
    uint8_t header[0x110];
    if (guarded != 0)
    {
        // Opt-in guard: a short header cannot carry a magic/chunk table.
        if (std::fread(header, 0x110, 1, file) != 1)
            return false;
    }
    else
    {
        std::fread(header, 0x110, 1, file);  // return ignored, like the original
    }
    uint32_t magic = 0;
    std::memcpy(&magic, header + 0x0C, 4);
    if (magic != 0x45564153u)  // "SAVE"
        return false;

    uint32_t local9 = 0;
    uint32_t local16 = 0;
    uint32_t local13 = 0;
    for (int i = 0; i < 18; ++i)
    {
        uint8_t chunk[9];
        const size_t chunk_read = std::fread(chunk, 9, 1, file);
        if (guarded != 0 && chunk_read != 1)
            return false;
        uint32_t size = 0;
        std::memcpy(&size, chunk + 5, 4);
        if (guarded != 0 && size < 9u)
        {
            // The original underflows the allocation count here.
            return false;
        }
        zpatch_new_saves_chunk_len = size;                  // dword_1015AD90
        zpatch_new_saves_element_count = size - 9u;         // dword_1015ADA0
        bool skipped = false;
        if (seek != 0 && size >= 9u && zpatch_new_saves_element_count <= 0x7FFFFFFFu)
        {
            // Only the 9-byte chunk headers are needed; seek over the payload.
            skipped = std::fseek(file,
                                 static_cast<long>(zpatch_new_saves_element_count),
                                 SEEK_CUR) == 0;
        }
        if (!skipped)
        {
            if (zpatch_new_saves_element_count == 0u &&
                (guarded != 0 || seek != 0))
            {
                // A header-only chunk is valid; do not treat malloc(0) == null
                // as an allocation failure. With both keys off this branch is
                // skipped so the original malloc/fread/free still runs.
            }
            else
            {
                void* payload = std::malloc(zpatch_new_saves_element_count);
                if (guarded != 0 && payload == nullptr)
                    return false;
                const size_t payload_read =
                    std::fread(payload, 1, zpatch_new_saves_element_count, file);
                std::free(payload);
                if (guarded != 0 && payload_read != zpatch_new_saves_element_count)
                    return false;
            }
        }
        if (i == 9) local9 = size == 19525u ? 1u : 0u;
        if (i == 16) local16 = size == 3703u ? 1u : 0u;
        if (i == 13) local13 = size == 5953u ? 1u : 0u;
    }
    if (flag9 != nullptr) *flag9 = local9;
    if (flag16 != nullptr) *flag16 = local16;
    if (flag13 != nullptr) *flag13 = local13;
    return true;
}

extern "C" void abi_new_saves_convert(const char* path) noexcept
{
    std::FILE* file = std::fopen(path, "rb");
    if (!file) return;
    uint32_t flag9 = 0;
    uint32_t flag16 = 0;
    uint32_t flag13 = 0;
    const bool parsed = zpatch_new_saves_scan(
        file, zpatch_opt_new_saves_guards, zpatch_opt_new_saves_seek,
        &flag9, &flag16, &flag13);
    std::fclose(file);
    if (!parsed) return;  // including the original wrong-magic early return
    zpatch_new_saves_flag9 = flag9;
    zpatch_new_saves_flag16 = flag16;
    zpatch_new_saves_flag13 = flag13;

    // P1 (game 0x872185): force the 0x3C-byte read path for new saves.
    static const uint8_t* p1 = nullptr;
    if (p1 == nullptr) p1 = scan_game("74 37 80 3D ? ? ? ? 00 75 2E 8D 84 ? ? ? ? 00 6A 1E");
    if (p1 != nullptr)
        wp_byte(const_cast<uint8_t*>(p1),
                zpatch_new_saves_flag9 != 0 ? 0xEB : 0x74);

    // P2 (game 0x7D8C50): route the oversized read through the plugin's scratch
    // buffer, resize the read, and force/NOP the branch per the save format.
    static const uint8_t* p2 = nullptr;
    if (p2 == nullptr) p2 = scan_game("68 BE 0C 00 00 50 E8");
    if (p2 != nullptr)
    {
        auto* site = const_cast<uint8_t*>(p2);
        int32_t original_rel = 0;
        std::memcpy(&original_rel, site + 7, 4);
        const auto original_target = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(site) + 11u +
            static_cast<uint32_t>(original_rel));
        wp_byte(site + 6, 0xE8);
        const auto hook = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&abi_new_saves_read_hook));
        wp_dword(site + 7, hook - static_cast<uint32_t>(
                                      reinterpret_cast<uintptr_t>(site + 6)) - 5u);
        if (zpatch_new_saves_loaddata == 0)
            zpatch_new_saves_loaddata = original_target;
        wp_dword(site + 1, zpatch_new_saves_flag16 != 0 ? 3694u : 3262u);
        wp_word(site - 5, zpatch_new_saves_flag16 != 0 ? 0x9090u : 0x2675u);
    }

    // P3 (game 0x8DA090): route the second LoadData call through the plugin body
    // that consumes the extra 12-byte block for new saves, and repoint the
    // shared LoadData slot at the function's own later call target.
    static const uint8_t* p3 = nullptr;
    if (p3 == nullptr) p3 = scan_game("56 E8 ? ? ? ? E8 ? ? ? ? A1 ? ? ? ? 8D 0C 85 00 00 00 00");
    if (p3 != nullptr)
    {
        auto* site = const_cast<uint8_t*>(p3);
        int32_t original_rel = 0;
        std::memcpy(&original_rel, site + 7, 4);
        const auto original_target = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(site) + 11u +
            static_cast<uint32_t>(original_rel));
        wp_byte(site + 6, 0xE8);
        const auto hook = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&abi_new_saves_p3_hook));
        wp_dword(site + 7, hook - static_cast<uint32_t>(
                                      reinterpret_cast<uintptr_t>(site + 6)) - 5u);
        if (zpatch_new_saves_p3_orig == 0)
            zpatch_new_saves_p3_orig = original_target;
        int32_t third_rel = 0;
        std::memcpy(&third_rel, site + 30, 4);
        zpatch_new_saves_loaddata = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(site) + 30u +
            static_cast<uint32_t>(third_rel) + 4u);
    }
}

// The prepare step of the tramp (original sub_10051FD0): if the handler
// captured the game's save-name buffer, make sure the savegame directory
// exists (game function found by signature) and convert "<dir>\\<save name>".
extern "C" void abi_new_saves_prepare() noexcept
{
    const auto* name = reinterpret_cast<const char*>(
        static_cast<uintptr_t>(zpatch_new_saves_src));
    if (name == nullptr) return;
    static const uint8_t* get_dir = nullptr;
    if (get_dir == nullptr)
        get_dir = scan_game("55 8B EC 83 E4 F8 83 EC 4C 56 33 C0 8D 4C 24 28");
    if (get_dir == nullptr) return;
    char directory[312]{};
    char suffix = 0;
    if (reinterpret_cast<uint8_t(__cdecl*)(int, char*, char*)>(
            const_cast<uint8_t*>(get_dir))(0, directory, &suffix) == 0)
        return;
    char path[560];
    std::snprintf(path, sizeof(path), "%s\\%s", directory, name);
    abi_new_saves_convert(path);
}
}  // extern "C"

// The spawn probe's GetModuleHandleW call goes through our own slot (MSVC's
// inline assembler cannot name the @-decorated import pointer).
struct SpawnsGetModuleHandleInit final
{
    SpawnsGetModuleHandleInit() noexcept
    {
        zpatch_spawns_getmodulehandlew = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&GetModuleHandleW));
    }
};
SpawnsGetModuleHandleInit g_spawns_getmodulehandle_init;

// CutsceneFixes chain B's VirtualProtect slot: the body loads this slot instead
// of the @-decorated import pointer, which the inline assembler cannot name
// (same value).
uint32_t zpatch_cutscene_virtualprotect = 0;
struct CutsceneVirtualProtectInit final
{
    CutsceneVirtualProtectInit() noexcept
    {
        zpatch_cutscene_virtualprotect = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&VirtualProtect));
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

// 0x10003e50 AllowHashesForIDE: the game's model-name hash function, replaced so
// that "hash:<number>" literals are accepted (see GameHash). The original is
// __cdecl(char*) -> unsigned int; this wrapper keeps that ABI.
unsigned int __cdecl abi_game_hash(const char* text)
{
    return GameHash(text ? std::string_view{text} : std::string_view{});
}

// 0x10050870 PedModelsInCutscenesFix: replaces the game's "does this object use
// the player model" check (the original calls a game helper and compares +0x2E).
// The original is __thiscall(this) -> bool in al; __fastcall puts `this` in ECX
// and returns bool in AL the same way.
bool __fastcall abi_ped_models_in_cutscenes_check(const void* self, void*)
{
    int32_t index = -1;
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    // `call dword_10159C0C` is unguarded in the original. A missing lookup or a
    // null self answers "not the player model", the un-patched answer.
    if (zpatch_failsafe_skip("PedModelsInCutscenesFix model lookup", lookup == nullptr) ||
        zpatch_failsafe_skip("PedModelsInCutscenesFix object", self == nullptr))
        return false;
    lookup(GameHash("player"), &index);
    const auto* o = static_cast<const uint8_t*>(self);
    return *reinterpret_cast<const int16_t*>(o + kModelIndexOffset) == index;
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

// 0x1001af60 EmissiveLerpFix: register-exact transcription of the original
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
// Opt-in prologue form of body A (garage_entry, installed at game 0xC68B90):
// the function entry is the mid-body site minus 0x20 and starts with an 8-byte
// `movss xmm0, ds:X`. The hook skips that instruction, runs the same store
// helper with the prologue's own arguments (ECX=this/object, [esp+4]=vehicle),
// restores every GPR, reloads xmm0 and resumes after the instruction (+8). The
// prologue's ECX/arg0 are the object/vehicle the mid-body site reads from
// ESI/EDI, so the store sees the same pair; the store writes object+0x13/+0x40/
// +0x41/+0x46, which the copier that follows never touches.
__declspec(naked) void abi_garage_entry_a()
{
    __asm {
        pushad
        mov  ecx, dword ptr [esp + 18h]   // ECX at entry = this (object)
        mov  edx, dword ptr [esp + 24h]   // [esp+4] at entry = vehicle
        call zpatch_garage_store_model
        popad
        push eax
        mov  eax, OFFSET zpatch_garage_scale
        __emit 0F3h
        __emit 00Fh
        __emit 010h
        __emit 000h                       // movss xmm0, DWORD PTR [eax]
        pop  eax
        jmp  dword ptr [zpatch_garage_entry_resume_a]
        __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable)
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

// @exact 0x10037330
// Spawn probes: the game's 10 class cases all do the same thing. The macro
// keeps the shim body in one place; only the kind and resume differ.
#define SPAWNS_CASE(name, kind, resume)        \
    __declspec(naked) void name() {               \
        __asm push ebp                              \
        __asm mov ebp, esp                          \
        __asm mov ecx, dword ptr [ebp + 0Ch]        \
        __asm movsx eax, cl                         \
        __asm mov dword ptr [zpatch_spawns_kind], kind \
        __asm mov dword ptr [zpatch_spawns_class], eax \
        __asm mov dword ptr [ebp + 0Ch], ecx        \
        __asm pop ebp                               \
        __asm jmp dword ptr [resume]                \
    }

SPAWNS_CASE(abi_spawns_case_0x24, 5, zpatch_spawns_case0_resume)
SPAWNS_CASE(abi_spawns_case_0x3D, 3, zpatch_spawns_case1_resume)
SPAWNS_CASE(abi_spawns_case_0x56, 3, zpatch_spawns_case2_resume)
SPAWNS_CASE(abi_spawns_case_0x6F, 0, zpatch_spawns_case3_resume)
SPAWNS_CASE(abi_spawns_case_0x88, 4, zpatch_spawns_case4_resume)
SPAWNS_CASE(abi_spawns_case_0x9E, 6, zpatch_spawns_case5_resume)
SPAWNS_CASE(abi_spawns_case_0xB4, 7, zpatch_spawns_case6_resume)
SPAWNS_CASE(abi_spawns_case_0xCA, 1, zpatch_spawns_case7_resume)
SPAWNS_CASE(abi_spawns_case_0xE0, 2, zpatch_spawns_case8_resume)
SPAWNS_CASE(abi_spawns_case_0xF6, 8, zpatch_spawns_case9_resume)

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
    const auto argB = static_cast<int32_t>(zpatch_vram_arg_b);
    zpatch_vram_flag = 0;
    if (argB < 0 || zpatch_vram_arg_a != 0 || argB < 0x3E800000)
        zpatch_vram_flag = 1;
}
// 0x1004aab0 VRAMFix: stashes two of the game function's arguments, calls the
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

// One-shot diagnostic for DrivebyFOVFix: scans the resolver's registry table
// (base+0x150FDD0/count 0x150FDD4, build-9 RVAs) for the key 0x9399528. Naked so
// the shim can call it without disturbing the live xmm1..xmm7 (xmm0 is scratch).
// The scan result is not consumed.
extern "C" void abi_driveby_fov_note_impl()
{
    static bool logged = false;
    if (logged) return;
    logged = true;
    const uintptr_t game_base =
        static_cast<uintptr_t>(zpatch_driveby_resume) - 5u - 0x85031Fu;  // build-9 sig RVA
    uint32_t count = 0;
    uint32_t table = 0;
    if (game_base > 0x10000u && game_base < 0x80000000u)
    {
        count = *reinterpret_cast<const uint32_t*>(game_base + kModelTableCountRva);
        table = *reinterpret_cast<const uint32_t*>(game_base + kModelTablePointerRva);
        if (table > 0x10000u && count > 0u && count < 0x100000u)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t key = *reinterpret_cast<const uint32_t*>(
                    static_cast<uintptr_t>(table) + i * 8u);
                if (key == 0x9399528u) break;
            }
        }
    }
}
extern "C" __declspec(naked) void abi_driveby_fov_note()
{
    __asm {
        sub esp, 70h
        __emit 0Fh
        __emit 11h
        __emit 4Ch
        __emit 24h
        __emit 00h                    // movups [esp], xmm1
        __emit 0Fh
        __emit 11h
        __emit 54h
        __emit 24h
        __emit 10h                    // movups [esp+0x10], xmm2
        __emit 0Fh
        __emit 11h
        __emit 5Ch
        __emit 24h
        __emit 20h                    // movups [esp+0x20], xmm3
        __emit 0Fh
        __emit 11h
        __emit 64h
        __emit 24h
        __emit 30h                    // movups [esp+0x30], xmm4
        __emit 0Fh
        __emit 11h
        __emit 6Ch
        __emit 24h
        __emit 40h                    // movups [esp+0x40], xmm5
        __emit 0Fh
        __emit 11h
        __emit 74h
        __emit 24h
        __emit 50h                    // movups [esp+0x50], xmm6
        __emit 0Fh
        __emit 11h
        __emit 7Ch
        __emit 24h
        __emit 60h                    // movups [esp+0x60], xmm7
        call abi_driveby_fov_note_impl
        __emit 0Fh
        __emit 10h
        __emit 4Ch
        __emit 24h
        __emit 00h                    // movups xmm1, [esp]
        __emit 0Fh
        __emit 10h
        __emit 54h
        __emit 24h
        __emit 10h                    // movups xmm2, [esp+0x10]
        __emit 0Fh
        __emit 10h
        __emit 5Ch
        __emit 24h
        __emit 20h                    // movups xmm3, [esp+0x20]
        __emit 0Fh
        __emit 10h
        __emit 64h
        __emit 24h
        __emit 30h                    // movups xmm4, [esp+0x30]
        __emit 0Fh
        __emit 10h
        __emit 6Ch
        __emit 24h
        __emit 40h                    // movups xmm5, [esp+0x40]
        __emit 0Fh
        __emit 10h
        __emit 74h
        __emit 24h
        __emit 50h                    // movups xmm6, [esp+0x50]
        __emit 0Fh
        __emit 10h
        __emit 7Ch
        __emit 24h
        __emit 60h                    // movups xmm7, [esp+0x60]
        add esp, 70h
        ret
    }
}

// 0x100064b0 DrivebyFOVFix: computes the drive-by FOV once and feeds it to the
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
        // The resolver is SEH-wrapped (a null preamble pointer or a miss returns
        // 0); the original then dereferences that 0. With the failsafe on the
        // default 45.0f is kept and the resume runs un-patched.
        cmp  BYTE PTR [zpatch_opt_failsafe], 0
        je   drv_resolved
        test eax, eax
        je   drv_done
    drv_resolved:
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
        cmp  BYTE PTR [zpatch_opt_failsafe], 0
        je   drv_cached_ok
        test eax, eax
        je   drv_done
    drv_cached_ok:
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
    char buffer[64];
    // Original: sprintf(buffer, 0x40, "%s_sign_%d", name, index + 1).
    zpatch_spawns_sprintf(
        buffer, sizeof(buffer), "%s_sign_%d",
        reinterpret_cast<const char*>(static_cast<uintptr_t>(zpatch_nolivery_name)),
        static_cast<int>(zpatch_nolivery_index) + 1);
    zpatch_nolivery_hash = GameHash(buffer);
    zpatch_cookie_tail();
}
extern "C" void abi_nolivery_copy_name()  // sub_1003D2F0
{
    // push [bufptr]; push 0x10; push [dest]; call [strcpy_s]; add esp,0xc; ret.
    // Reads both globals unguarded and has no /GS cookie tail (leaf).
    char* const dst = reinterpret_cast<char*>(static_cast<uintptr_t>(zpatch_nolivery_dest));
    const char* const src =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(zpatch_nolivery_bufptr));
    zpatch_secure_strcpy_s(dst, 0x10u, src);
}
extern "C" void abi_nolivery_count_loop()  // sub_1003D270
{
    using LookupFn = int(__fastcall*)(void* self, int, uint32_t hash);
    const auto fn = reinterpret_cast<LookupFn>(static_cast<uintptr_t>(zpatch_nolivery_fn));
    void* const self = reinterpret_cast<void*>(static_cast<uintptr_t>(zpatch_nolivery_arg));
    const char* const name =
        reinterpret_cast<const char*>(static_cast<uintptr_t>(zpatch_nolivery_bufptr));
    zpatch_nolivery_count = 0;
    for (uint32_t i = 1;; ++i)
    {
        char buffer[64];
        zpatch_spawns_sprintf(buffer, sizeof(buffer), "%s_sign_%d", name,
                              static_cast<int>(i));
        const int result = fn ? fn(self, 0, GameHash(buffer)) : 0;
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
// normally. The original's two nops are omitted (not byte-exact).
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
uintptr_t newfiles_find(std::string_view signature)
{
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
    const auto* dos = base ? reinterpret_cast<const IMAGE_DOS_HEADER*>(base) : nullptr;
    const auto* nt = dos && dos->e_magic == IMAGE_DOS_SIGNATURE
                         ? reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew)
                         : nullptr;
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    Match match;
    if (!FindPattern(ExecutableView({base, nt->OptionalHeader.SizeOfImage}), signature, match)) return 0;
    return reinterpret_cast<uintptr_t>(match.address);
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
    const auto register_file = reinterpret_cast<void(__cdecl*)(uint32_t, void*)>(
        static_cast<uintptr_t>(zpatch_newfiles_register_slot));
    register_file(0x2C89A1F9u, reinterpret_cast<void*>(&abi_newfiles_cb));
}
extern "C" void* abi_newfiles_find_obj()  // 0x10051730
{
    // The original re-dereferences the container global on EVERY call
    // (**(_DWORD**)(match+2)), so it is picked up again whenever the game rebuilds
    // its registry; caching the container itself left a stale one that faulted in
    // the game's container search (0x43a490).
    static uintptr_t match = 0;
    static bool resolved = false;
    if (!resolved)
    {
        resolved = true;
        match = newfiles_find(
            "8B 35 ? ? ? ? 6A 00 50 E8 ? ? ? ? 83 C4 08 50 8B CE E8 ? ? ? ? 85 C0 89 07 5E 74 05");
    }
    if (!match) return nullptr;
    const uint32_t global =
        *reinterpret_cast<const uint32_t*>(reinterpret_cast<const void*>(match + 2u));
    return global ? reinterpret_cast<void*>(*reinterpret_cast<const uint32_t*>(
                        reinterpret_cast<const void*>(global)))
                  : nullptr;
}
extern "C" int abi_newfiles_call_obj(void* obj, uint32_t hash)  // 0x10051610
{
    using Fn = int(__fastcall*)(void* self, int, uint32_t);
    static uintptr_t cached = 0;
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
    static uintptr_t cached = 0;
    static bool resolved = false;
    if (!resolved)
    {
        resolved = true;
        const uintptr_t m = newfiles_find("8B 0F 85 C9 74 ? 0F B7 41 0A 66 85 C0");
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
    if (base[0x10] == 0) return 0;
    if (abi_newfiles_call_obj(abi_newfiles_find_obj(),
                              GameHash(reinterpret_cast<const char*>(base + 0x10))))
        abi_newfiles_call_add(base + 8, base + 0x10);
    return 1;
}
extern "C" char __fastcall abi_newfiles_update_b(void* self, int)  // 0x100518D0
{
    auto* base = static_cast<unsigned char*>(self);
    if (base[0x10F] == 0) return 0;
    if (abi_newfiles_call_obj(abi_newfiles_find_obj(),
                              GameHash(reinterpret_cast<const char*>(base + 0x10F))))
        abi_newfiles_call_add(base + 4, base + 0x10F);
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
    static const char* const kPolice[] =
    {
            "police",     "police2",    "police3",  "police4",   "noose",      "pstockade",
            "nstockade",  "polpatriot", "fbi",      "n_police",  "n_police2",  "n_polpatriot",
            "n_pstockade", nullptr};
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    const int32_t index = static_cast<int32_t>(zpatch_copsirens_index);
    zpatch_copsirens_flag = 0;
    // sub_10005F70's `call dword_10159C0C` is unguarded. A missing lookup
    // answers "not a police car", which leaves the siren decision un-patched.
    if (zpatch_failsafe_skip("CopSirensFlag model lookup", lookup == nullptr))
        return 0;
    for (int i = 0; kPolice[i]; ++i)
    {
        int32_t out = -1;
        lookup(GameHash(kPolice[i]), &out);
        if (out == index)
        {
            zpatch_copsirens_flag = 1;
            return 1;
        }
    }
    // Original sub_10005F70 re-stores the flag before the models loop
    // (mov eax,[0x10159B80]; ...; mov [0x10159B80],eax); keep the store.
    {
        volatile uint32_t* flag = &zpatch_copsirens_flag;
        *flag = *flag;
    }
    for (int i = 0; i < 512 && zpatch_copsirens_models[i]; ++i)
    {
        int32_t out = -1;
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
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        static_cast<uintptr_t>(zpatch_model_lookup));
    const int32_t index = static_cast<int32_t>(zpatch_copsirens_index_b);
    zpatch_copsirens_flag_b = 0;
    int32_t out = -1;
    // sub_10006260's `call dword_10159C0C` is unguarded.
    if (zpatch_failsafe_skip("CopSirensFlag model lookup (B)", lookup == nullptr))
        return 0;
    lookup(GameHash("police2"), &out);
    if (out != index)
    {
        if (!zpatch_copsirens_models[0]) return 0;
        for (int i = 0; i < 512 && zpatch_copsirens_models[i]; ++i)
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
bool prefix_ci(const char* text, const char* prefix, size_t length) noexcept
{
    for (size_t i = 0; i < length; ++i)
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
        reinterpret_cast<const char*>(static_cast<uintptr_t>(zpatch_ide_string));
    const uint32_t model = *reinterpret_cast<const uint32_t*>(
        reinterpret_cast<const void*>(static_cast<uintptr_t>(zpatch_ide_model) + 60u));
    const auto append = [](uint32_t* array, uint32_t value)
    {
        for (int i = 0; i < 512; ++i)
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
        uint32_t bits = 0;
        std::memcpy(&bits, &angle, sizeof(bits));
        for (int i = 0; i < 512; ++i)
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
        SetWindowPos(hwnd, reinterpret_cast<HWND>(static_cast<INT_PTR>(-2)), 0, 0, 0, 0, 3);
        SetWindowLongA(hwnd, GWL_STYLE,
                       GetWindowLongA(hwnd, GWL_STYLE) & static_cast<LONG>(0xff30ffffu));
        SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) & ~static_cast<LONG>(8));
        zpatch_borderless_w_done = 1;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
extern "C" LRESULT __stdcall abi_borderless_a(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam)
{
    if (!zpatch_borderless_a_done)
    {
        SetWindowPos(hwnd, reinterpret_cast<HWND>(static_cast<INT_PTR>(-2)), 0, 0, 0, 0, 3);
        SetWindowLongA(hwnd, GWL_STYLE,
                       GetWindowLongA(hwnd, GWL_STYLE) & static_cast<LONG>(0xff30ffffu));
        SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) & ~static_cast<LONG>(8));
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
        zpatch_speedup_clock = static_cast<uint32_t>(newfiles_find(
            "1B D9 89 5C 24 24 0F 88"));
        // Diagnostic only: the original has no check here; a missed match makes the
        // faithful `clock - 51` call fault immediately (0xFFFFFFCD).
        zpatch_tls_static_footer(zpatch_speedup_scan_guard);
    }
    // The original dereferences the timing pointers unconditionally (its one-time
    // test is on the float, not on the pointers).
    float* const t50 = reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t50));
    if (*t50 == 0.0f)
    {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        const double scale = 1000.0 / static_cast<double>(frequency.QuadPart);
        const float v5 = static_cast<float>(scale);
        const float v6 = static_cast<float>(scale * 1000.0);
        const float v7 = static_cast<float>(scale * 0.001000000047497451);
        const float v8 = static_cast<float>(0.001000000047497451 / scale);
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t64)) = v5;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t60)) = v6;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t50)) = v7;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t5c)) = v8;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t68)) = v5;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t4c)) = v6;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t54)) = v7;
        *reinterpret_cast<float*>(static_cast<uintptr_t>(zpatch_speedup_t58)) = v8;
    }
    using ClockFn = __int64(__cdecl*)(void);
    // The original calls its resolved clock function unconditionally; a failed
    // scan faults at match-51.
    const __int64 stamp = reinterpret_cast<ClockFn>(
        static_cast<uintptr_t>(zpatch_speedup_clock) - 51u)();
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

// 0x1001ef60 HighFPSBikePhysicsFix: saves the incoming value, clamps it,
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
int __stdcall abi_savegame_fix_copy(const void* src, uint32_t size, void* dst,
                                    uint32_t* out_size, int)
{
    if (*out_size >= size)
    {
        if (dst) std::memcpy(dst, src, size);
        *out_size = size;
        return 0;
    }
    *out_size = size;
    return static_cast<int>(0x8007007Au);
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
        imul ecx, ecx, 0x190
        cmp edx, 0x1f40
        jge ok
        xor edx, edx
    ok:
        jmp DWORD PTR [zpatch_skip_intro_match]
    }
}

// SkipIntro's 300 ms body (borrowed from FusionFix's skipintro.ixx): the same
// site and resume, but below the 0x1F40 threshold the screens play for 300 ms
// instead of being skipped outright. The original body above stays available
// through the test-only OPCEP_IMPROVEMENTS switch; it is not a separate INI key.
__declspec(naked) void abi_skip_intro_screens()
{
    __asm {
        imul ecx, ecx, 0x190
        cmp edx, 0x1f40
        jge ok
        mov edx, 0x12c
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
    void*, char*, DlcApplyFn, uint32_t*)
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

// The 0x104-byte path buffers sub_10023150 builds for each DLC folder.
constexpr size_t kDlcPathCapacity = 0x104u;

void* dlc_discovery_post(void* object)
{
    // The original allocates with a throwing operator new and ignores both its
    // result and GetModuleFileNameA's: a failed allocation or a failed module
    // path then builds every DLC path from uninitialized bytes. The failsafe
    // leaves the episode list untouched instead, which is the LoadDLCs=0 state.
    auto* const module_path =
        static_cast<char*>(::operator new(kDlcPathCapacity, std::nothrow));
    if (zpatch_failsafe_skip("LoadDLCs path buffer", module_path == nullptr))
        return object;
    const DWORD module_length = GetModuleFileNameA(nullptr, module_path, kDlcPathCapacity);
    if (zpatch_failsafe_skip("LoadDLCs module path",
                             module_length == 0 || module_length >= kDlcPathCapacity))
        return object;
    PathRemoveFileSpecA(module_path);
    uint32_t last_apply_edx = 0;
    bool episode254_registered = false;

    // The original picks ONE path per group: the "content" layout if it exists,
    // otherwise the legacy fallback; it never registers both.
    const auto try_paths = [&](const char* primary, const char* fallback, int id)
    {
        char path[kDlcPathCapacity];
        strcpy_s(path, kDlcPathCapacity, module_path);
        strcat_s(path, kDlcPathCapacity, primary);
        if (!PathFileExistsA(path))
        {
            if (std::strcmp(primary, fallback) == 0) return;
            strcpy_s(path, kDlcPathCapacity, module_path);
            strcat_s(path, kDlcPathCapacity, fallback);
            if (!PathFileExistsA(path)) return;
        }
        char request[360]{};
        strcpy_s(request, kDlcPathCapacity, path);
        request[358] = 1;
        *reinterpret_cast<uint32_t*>(request + 344) = 1;
        const auto game_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        const auto register_fn = reinterpret_cast<DlcRegisterFn>(
            game_base + DlcRegisterRva());
        const auto apply_fn = reinterpret_cast<DlcApplyFn>(
            game_base + DlcApplyRva());
        register_fn(id, request, 1);
        zpatch_dlc_apply_capture_edx(object, request, apply_fn, &last_apply_edx);
        if (id == 508) episode254_registered = true;
    };

    try_paths("\\dlc\\tlad\\content\\", "\\TLAD\\", 2);
    try_paths("\\dlc\\TBoGT\\content\\", "\\TBoGT\\", 4);
    for (int i = 3; i < 255; ++i)
    {
        char suffix[0x40];
        zpatch_spawns_sprintf(suffix, sizeof(suffix), "\\Episode%d\\", i);
        try_paths(suffix, suffix, 2 * i);
    }
    *reinterpret_cast<uint8_t*>(static_cast<uint8_t*>(object) + 358) = 2;
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
void zpatch_mf_advance_c() noexcept
{
    // sub_100241E0: when both mouse axes are zero, patch three 8-byte windows of
    // the resumed game function (dword_10159C40 + 52/94/107) with the "pull"
    // constants; otherwise NOP-ish them out. VirtualProtect failures are ignored
    // like the original (a null resume would fault; unreachable once installed).
    const auto base = static_cast<uintptr_t>(zpatch_mf_resume_b);
    // sub_100241E0 dereferences the resume slot unconditionally.
    const auto* x = reinterpret_cast<const int32_t*>(
        static_cast<uintptr_t>(zpatch_mf_x));
    const auto* y = reinterpret_cast<const int32_t*>(
        static_cast<uintptr_t>(zpatch_mf_y));
    // sub_100241E0 evaluates Y first, then X, and dereferences both unguarded.
    const bool zero = (static_cast<double>(*y) * 0.0078125 == 0.0) &&
                      (static_cast<double>(*x) * 0.0078125 == 0.0);
    struct Pair final
    {
        uintptr_t offset;
        uint32_t first;
        uint32_t second;
    };
    const Pair pairs[3] =
    {
            {52u, zero ? 0x86110FF3u : 0x90909090u, zero ? 0x144u : 0x90909090u},
            {94u, zero ? 0x8E110FF3u : 0x90909090u, zero ? 0x148u : 0x90909090u},
            {107u, zero ? 0x01489ED9u : 0x9090D8DDu, zero ? 0x5C8B0000u : 0x5C8B9090u},
    };
    for (const Pair& pair : pairs)
    {
        auto* target = reinterpret_cast<uint32_t*>(base + pair.offset);
        // Each site is an 8-byte window: the instruction (or nops) plus its operand.
        injector::WriteMemory<uint32_t>(target, pair.first, true);       // bytes 0..3
        injector::WriteMemory<uint32_t>(target + 1, pair.second, true);  // bytes 4..7
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

// OutOfCommissionFix (callback 0x1004C3F0). Scan 1 (A9 39 44 24 18 at game
// 0xA22E90) E9s match+7 to the branch-widening body and publishes match+0x11 as
// its resume. Scan 2 (E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56 at game 0xB70733)
// E8s the match to the wrapper and publishes dword_10159BF8 (the E8 at
// match-0x33's target) / dword_10159C04 (the match E8's original target).
// ---------------------------------------------------------------------------
namespace
{
// OutOfCommissionFix's real payload: the original embeds a compressed
// "finale2.sco" (163796 bytes at 0x100B6798); its init (sub_1004B750 ->
// sub_1004B570/sub_1004B150) decodes it into a 179536-byte table, registers it,
// then sub_1004ACE0 builds an in-memory RPF archive around it. The wrapper mounts
// that archive with the game's own mount function ("memory:$<ptr>,<size>,0:") and
// the E9 body widens the game's script-id check to accept its resource magic.
// The decode below mirrors sub_1004B150's opcode loop.
constexpr size_t kOocTableSize = 0x2BD50u;  // 179536
constexpr const unsigned char* kOocPakBlob = zpatch_ooc_blob;

inline void zpatch_ooc_store(uint32_t* destination,
                             uint32_t value) noexcept
{
    *reinterpret_cast<volatile uint32_t*>(destination) = value;
}

struct OocPakEntry final
{
    char name_buffer[16];
    uint32_t name_size;
    uint32_t name_capacity;
    uint32_t file_size;
    uint32_t stored_offset;
    uint32_t stored_size;
    uint32_t padding;
    uint32_t data;
};
static_assert(sizeof(OocPakEntry) == 44);

void zpatch_ooc_register_entry(unsigned char* table)
{
    auto* begin = reinterpret_cast<OocPakEntry*>(
        static_cast<uintptr_t>(zpatch_ooc_vector_begin));
    auto* end = reinterpret_cast<OocPakEntry*>(
        static_cast<uintptr_t>(zpatch_ooc_vector_end));
    auto* capacity = reinterpret_cast<OocPakEntry*>(
        static_cast<uintptr_t>(zpatch_ooc_vector_capacity));
    const size_t size = begin == nullptr ? 0u
                                               : static_cast<size_t>(end - begin);
    const size_t cap = begin == nullptr ? 0u
                                              : static_cast<size_t>(capacity - begin);
    bool reallocated = false;
    if (size == cap)
    {
        const size_t new_capacity =
            (cap + cap / 2u > size) ? cap + cap / 2u : size + 1u;
        auto* replacement = static_cast<OocPakEntry*>(
            ::operator new(new_capacity * sizeof(OocPakEntry)));
        if (size != 0u) std::memcpy(replacement, begin, size * sizeof(OocPakEntry));
        if (begin != nullptr) ::operator delete(begin);
        begin = replacement;
        end = begin + size;
        capacity = begin + new_capacity;
        reallocated = true;
    }

    OocPakEntry entry{};
    std::memcpy(entry.name_buffer, "finale2.sco", sizeof("finale2.sco"));
    entry.name_size = 11u;
    entry.name_capacity = 15u;
    entry.file_size = static_cast<uint32_t>(kOocTableSize);
    entry.data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table));
    std::memcpy(end, &entry, sizeof(entry));
    ++end;
    if (reallocated)
    {
        zpatch_ooc_store(&zpatch_ooc_vector_begin,
                         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(begin)));
        zpatch_ooc_store(&zpatch_ooc_vector_end,
                         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end)));
        zpatch_ooc_store(&zpatch_ooc_vector_capacity,
                         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(capacity)));
    }
    else
    {
        zpatch_ooc_store(&zpatch_ooc_vector_end,
                         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end)));
    }
}

// The finale2 stream's opcode format. The names mirror the decoder branches;
// the code values are part of the resource format, not arbitrary numbers.
constexpr uint8_t kOocOpLiteral = 0x20;
constexpr uint8_t kOocOpLongBackref = 0x18;
constexpr uint8_t kOocOpWideBackref = 0x10;
constexpr uint8_t kOocOpMediumLiteral = 0x08;
constexpr uint8_t kOocOpLongLiteral = 0x07;
constexpr uint8_t kOocOpShortBackref = 0x06;
constexpr uint8_t kOocOpWideLiteralBackref = 0x04;
constexpr uint8_t kOocOpShortDistance = 0x40;
constexpr uint8_t kOocOpShortestDistance = 0x80;
constexpr uint8_t kOocOpEnd = 0x05;
constexpr uint32_t kOocByteBase = 256u;
constexpr uint32_t kOocWordBase = 65536u;
constexpr uint32_t kOocLongDistanceBias = 1572863u;
constexpr uint32_t kOocWideDistanceBias = 0xFFFFFu;
constexpr uint32_t kOocShortDistanceBias = 0x3FFFu;
constexpr uint8_t kOocLiteralBias = 31u;
constexpr uint8_t kOocLiteralAdvance = 30u;
constexpr uint8_t kOocShortestBias = 127u;
constexpr uint32_t kOocMediumLiteralBias = 2047u;
constexpr uint32_t kOocAdlerModulus = 0xFFF1u;
constexpr size_t kOocAdlerBlock = 1872u;

struct OocStream final
{
    size_t cursor = 0;  // the original's dword_101A8430
    unsigned char* dest = nullptr;

    void literal(size_t count, size_t from) noexcept
    {
        if (cursor + count <= kOocTableSize)
        {
            // The original's source bound (a1 + n >= dword_101887D4 = blob base - 16)
            // is constant-true: the stream starts past the base pointer.
            std::memcpy(dest + cursor, kOocPakBlob + from, count);
            cursor += count;
        }
        else
        {
            cursor = kOocTableSize + 1;
        }
    }
    void backref(size_t distance, size_t count) noexcept
    {
        if (cursor + count > kOocTableSize || distance > cursor)
        {
            cursor = kOocTableSize + 1;
            return;
        }
        unsigned char* out = dest + cursor;
        const unsigned char* in = out - distance;
        for (size_t i = 0; i < count; ++i) out[i] = in[i];  // overlap propagates
        cursor += count;
    }
    size_t step(size_t pc) noexcept
    {
        const unsigned char op = kOocPakBlob[pc];
        if (op < kOocOpLiteral)
        {
            if (op >= kOocOpLongBackref)
            {
                backref(kOocPakBlob[pc + 2] + kOocByteBase * kOocPakBlob[pc + 1] +
                            kOocWordBase * op - kOocLongDistanceBias,
                        kOocPakBlob[pc + 3] + 1u);
                return pc + 4;
            }
            if (op >= kOocOpWideBackref)
            {
                backref(kOocPakBlob[pc + 2] + kOocByteBase * kOocPakBlob[pc + 1] +
                            kOocWordBase * op - kOocWideDistanceBias,
                        (kOocPakBlob[pc + 3] << 8) + kOocPakBlob[pc + 4] + 1u);
                return pc + 5;
            }
            if (op >= kOocOpMediumLiteral)
            {
                const size_t count = kOocByteBase * op + kOocPakBlob[pc + 1] - kOocMediumLiteralBias;
                literal(count, pc + 2);
                return pc + 2 + count;
            }
            if (op == kOocOpLongLiteral)
            {
                const size_t count = kOocByteBase * kOocPakBlob[pc + 1] + kOocPakBlob[pc + 2] + 1u;
                literal(count, pc + 3);
                return pc + 3 + count;
            }
            if (op == kOocOpShortBackref)
            {
                backref(kOocPakBlob[pc + 3] + kOocByteBase * kOocPakBlob[pc + 2] +
                            kOocWordBase * kOocPakBlob[pc + 1] + 1u,
                        kOocPakBlob[pc + 4] + 1u);
                return pc + 5;
            }
            if (op == kOocOpWideLiteralBackref)
            {
                backref(kOocPakBlob[pc + 3] + kOocByteBase * kOocPakBlob[pc + 2] +
                            kOocWordBase * kOocPakBlob[pc + 1] + 1u,
                        (kOocPakBlob[pc + 4] << 8) + kOocPakBlob[pc + 5] + 1u);
                return pc + 6;
            }
            return pc;  // no-progress opcode terminates the stream
        }
        if (op < kOocOpShortestDistance)
        {
            if (op >= kOocOpShortDistance)
            {
                backref(kOocPakBlob[pc + 1] + kOocByteBase * op -
                            kOocShortDistanceBias,
                        kOocPakBlob[pc + 2] + 1u);
                return pc + 3;
            }
            literal(op - kOocLiteralBias, pc + 1);
            return pc + (op - kOocLiteralAdvance);
        }
        backref(kOocPakBlob[pc + 1] + 1u, op - kOocShortestBias);
        return pc + 2;
    }
};

// The original's Adler-32 variant (a = 1, b = 0, 1872-byte partition, 32-bit
// wrapping); reproduced because the blob's trailer was built with it.
uint32_t zpatch_ooc_adler32(const unsigned char* data, size_t size) noexcept
{
    uint32_t a = 1u;
    uint32_t b = 0u;
    for (size_t block = 0; block < size; block += kOocAdlerBlock)
    {
        const size_t end = (block + kOocAdlerBlock < size) ? block + kOocAdlerBlock : size;
        for (size_t i = block; i < end; ++i)
        {
            a += data[i];
            b += a;
        }
        a %= kOocAdlerModulus;
        b %= kOocAdlerModulus;
    }
    return (b << 16) | a;
}

bool zpatch_ooc_decode(unsigned char* table) noexcept
{
    // sub_1004B570: decode until the no-progress opcode, then require the
    // 05 FA terminator, the exact destination size and the Adler trailer.
    const auto table_begin = reinterpret_cast<uintptr_t>(table);
    zpatch_ooc_store(&zpatch_ooc_input_bound,
                     static_cast<uint32_t>(
                         reinterpret_cast<uintptr_t>(kOocPakBlob) - 16u));
    zpatch_ooc_store(&zpatch_ooc_output_begin,
                     static_cast<uint32_t>(table_begin));
    zpatch_ooc_store(&zpatch_ooc_output_cursor,
                     static_cast<uint32_t>(table_begin));
    zpatch_ooc_store(&zpatch_ooc_output_end,
                     static_cast<uint32_t>(table_begin + kOocTableSize));
    OocStream stream{0, table};
    size_t prior_cursor = stream.cursor;
    size_t pc = stream.step(0);
    if (stream.cursor != prior_cursor)
    {
        zpatch_ooc_store(&zpatch_ooc_output_cursor,
                         static_cast<uint32_t>(table_begin + stream.cursor));
    }
    bool terminated = pc == 0;
    while (!terminated && stream.cursor <= kOocTableSize)
    {
        const size_t previous = pc;
        prior_cursor = stream.cursor;
        pc = stream.step(pc);
        if (stream.cursor != prior_cursor)
        {
            zpatch_ooc_store(&zpatch_ooc_output_cursor,
                             static_cast<uint32_t>(table_begin + stream.cursor));
        }
        if (pc == previous) terminated = true;
    }
    if (!terminated || stream.cursor != kOocTableSize) return false;
    if (kOocPakBlob[pc] != 5u || kOocPakBlob[pc + 1] != 0xFAu) return false;
    const uint32_t trailer = (static_cast<uint32_t>(kOocPakBlob[pc + 2]) << 24) |
                                  (static_cast<uint32_t>(kOocPakBlob[pc + 3]) << 16) |
                                  (static_cast<uint32_t>(kOocPakBlob[pc + 4]) << 8) |
                                  static_cast<uint32_t>(kOocPakBlob[pc + 5]);
    return zpatch_ooc_adler32(table, kOocTableSize) == trailer;
}

// sub_1004ACE0 for the single registered entry ("finale2.sco", table, 179536).
// 20-byte header, one 16-byte record, the name, then the payload at offset 2048
// followed by the zero padding to the next 2048 boundary. The table is freed like
// the original; `final_offset` is the original's dword_10159C78.
// Archive page size (the 0x800 granularity of the payload/records).
constexpr uint32_t kOocPageSize = 2048u;

unsigned char* zpatch_ooc_build_archive(unsigned char* table,
                                        uint32_t& final_offset) noexcept
{
    const uint32_t size = static_cast<uint32_t>(kOocTableSize);
    const uint32_t remainder = size & (kOocPageSize - 1u);
    const uint32_t pages = size / kOocPageSize + (remainder != 0u ? 1u : 0u);
    const uint32_t total = pages * kOocPageSize + kOocPageSize;
    // No null check by default, like the original's archive build: the memset
    // below faults on a failed allocation. OutOfCommissionSafeInit returns 0
    // instead and the caller skips the mount.
    auto* archive = static_cast<unsigned char*>(std::malloc(total));
    if (archive == nullptr && zpatch_opt_ooc_safe != 0u)
    {
        std::free(table);
        return nullptr;
    }
    std::memset(archive, 0, total);
    uint32_t offset = 0;
    const auto append = [&](const void* source, uint32_t bytes) noexcept
    {
        std::memcpy(archive + offset, source, bytes);
        offset += bytes;
        zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
    };
    zpatch_ooc_store(&zpatch_ooc_archive_offset, 0);
    const char name[] = "finale2.sco";
    const uint32_t name_bytes = sizeof(name);  // includes the NUL
    struct OocHeader final
    {
        uint32_t fields[4];
        uint16_t words[2];
    };
    static_assert(sizeof(OocHeader) == 20);
    const OocHeader header{{0x86723DFFu, 3u, 1u, name_bytes + 16u},
                           {0x10u, 0xE9u}};
    append(&header, sizeof(header));
    unsigned char record[16] = {};
    std::memcpy(record, &size, 4);                 // physical size (entry[0x20])
    const uint32_t index = 1u;                // v9 + 1
    std::memcpy(record + 8, &index, 4);
    const uint16_t record_pages = static_cast<uint16_t>(pages);
    std::memcpy(record + 12, &record_pages, 2);    // sub_1004ABB0
    if (remainder != 0u)
    {
        const uint16_t padding =
            static_cast<uint16_t>(kOocPageSize - remainder);
        const uint16_t packed =
            static_cast<uint16_t>((static_cast<uint16_t>(0u) & 0xF800u) | padding);
        std::memcpy(record + 14, &packed, 2);        // sub_1004AB90
    }
    append(record, 16);
    append(name, name_bytes);
    offset = kOocPageSize;                         // the original resets dword_10159C78
    zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
    append(table, size);
    std::free(table);                              // j_j_free inside the payload loop
    if (remainder != 0u)
        offset += kOocPageSize - remainder;        // zero padding (already memset)
    if (remainder != 0u) zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
    final_offset = offset;
    return archive;
}

// sub_1004B750: the one-shot init the wrapper runs before mounting. Its guard
// dword_10159C00 has no writer in the original, so it re-runs (and leaks the
// archive) on every call - kept 1:1. Both mallocs are unchecked like the
// original.
uint32_t zpatch_ooc_lookup() noexcept
{
    // OutOfCommissionCache: the original's guard dword_10159C00 has no writer,
    // so it re-decodes and leaks on every call. Run the init once and reuse the
    // mounted archive; the original path stays selected when the key is off.
    static uint32_t cached_archive = 0;
    static uint32_t cached_state = 0;
    if (zpatch_opt_ooc_cache != 0u && cached_archive != 0u)
    {
        zpatch_ooc_store(&zpatch_ooc_state, cached_state);
        return cached_archive;
    }

    auto* table = static_cast<unsigned char*>(std::malloc(kOocTableSize));
    if (table == nullptr && zpatch_opt_ooc_safe != 0u) return 0;
    const bool decoded = zpatch_ooc_decode(table);
    if (!decoded && zpatch_opt_ooc_safe != 0u)
    {
        std::free(table);
        return 0;
    }
    zpatch_ooc_store(&zpatch_ooc_table_size,
                     static_cast<uint32_t>(kOocTableSize));
    zpatch_ooc_register_entry(table);
    uint32_t final_offset = 0;
    unsigned char* archive = zpatch_ooc_build_archive(table, final_offset);
    if (archive == nullptr && zpatch_opt_ooc_safe != 0u) return 0;
    zpatch_ooc_store(&zpatch_ooc_vector_end,
                     zpatch_ooc_vector_begin);
    zpatch_ooc_store(&zpatch_ooc_state, final_offset);
    if (zpatch_opt_ooc_cache != 0u)
    {
        cached_archive = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(archive));
        cached_state = final_offset;
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(archive));
}

uint32_t zpatch_ooc_fnv1a(const unsigned char* data, size_t size) noexcept
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}
}  // namespace

// Offline self-test for OutOfCommissionFix: the decode must reproduce the blob's
// own Adler trailer and the archive must match the golden layout/hash.
extern "C" bool zpatch_ooc_selftest() noexcept
{
    auto* table = static_cast<unsigned char*>(std::malloc(kOocTableSize));
    if (table == nullptr) return false;
    const bool decoded = zpatch_ooc_decode(table);
    const uint32_t table_adler = zpatch_ooc_adler32(table, kOocTableSize);
    uint32_t final_offset = 0;
    unsigned char* archive = zpatch_ooc_build_archive(table, final_offset);
    if (archive == nullptr)
    {
        std::free(table);
        return false;
    }
    const uint32_t header[4] = {0x86723DFFu, 3u, 1u, 28u};
    bool header_ok = true;
    for (int i = 0; i < 4; ++i)
        header_ok = header_ok && std::memcmp(archive + 4 * i, &header[i], 4) == 0;
    header_ok = header_ok && archive[16] == 0x10u && archive[17] == 0u &&
                archive[18] == 0xE9u && archive[19] == 0u;
    const uint32_t record_size = 0x2BD50u;
    const uint32_t record_index = 1u;
    bool record_ok = std::memcmp(archive + 20, &record_size, 4) == 0;
    const uint32_t zero = 0u;
    record_ok = record_ok && std::memcmp(archive + 24, &zero, 4) == 0 &&
                std::memcmp(archive + 28, &record_index, 4) == 0;
    record_ok = record_ok && archive[32] == 88u && archive[33] == 0u &&
                archive[34] == 0xB0u && archive[35] == 0x02u;
    const bool name_ok = std::memcmp(archive + 36, "finale2.sco", 12) == 0;
    const bool ok = decoded && final_offset == 182272u && table_adler == 0x41E794ABu &&
                    header_ok && record_ok && name_ok &&
                    zpatch_ooc_fnv1a(archive, final_offset) == 0xB5AF7B10u;
    std::free(archive);
    return ok;
}

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

// 0x1004C350 OutOfCommissionFix wrapper (E8 target of game 0xB70733): run the
// one-shot fake-pak registration, print the fixed model name through the
// published logger, then call the game function the E8 replaced with the
// original arguments. Its register use is cdecl-clean, so it is ordinary C++.
extern "C" uint32_t abi_ooc_wrap(uint32_t arg0, uint32_t arg1) noexcept
{
    const uint32_t lookup = zpatch_ooc_lookup();
    if (lookup != 0u)
    {
        char buffer[0x44];
        zpatch_spawns_sprintf(buffer, 0x40, zpatch_ooc_fmt_b, zpatch_ooc_fmt_a, 0x3A, 0x24,
                              static_cast<int>(lookup), static_cast<int>(zpatch_ooc_state),
                              0x30, 0x3A, 0);
        const auto logger = reinterpret_cast<void(__cdecl*)(char*, int, int)>(
            static_cast<uintptr_t>(zpatch_ooc_logger));
        // sub_1004C350 calls the logger and the original target unguarded.
        logger(buffer, 1, -1);
    }
    const auto original = reinterpret_cast<uint32_t(__cdecl*)(uint32_t, uint32_t)>(
        static_cast<uintptr_t>(zpatch_ooc_orig));
    return original(arg0, arg1);
}

// The worker's sub_10003980: the resolver call with the salt in ECX and ESI.
extern "C" __declspec(naked) uint32_t zpatch_cutscene_scene()
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

// NewMemorySystem: the reimplemented CRageAllocator whose vtable (original
// 0x100B5038) the E8 body writes into the game's allocator singleton. The
// original swaps the arena allocator for a HeapAlloc-backed one: allocate() uses
// unit = max(0x10, align), want = max(size, unit), HeapAllocs want + 2*unit,
// rounds up to ((base-1)/unit + 2)*unit, stores the base offset at [p-4], fills
// want bytes with 0xCD and registers the pointer under the critical section at
// 0x101A0808; free() recovers the base from [p-4], HeapFrees and unregisters.
// Only vtable[18] observes the registry, so a plain set suffices.
// The allocator vtable's small methods. The original's calling conventions
// are preserved; only the constructor and thunk still need their exact
// prologue/tail-jump.
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

// 0x10038670: install the vtable in the game's allocator singleton.
extern "C" void __cdecl abi_nms_apply()
{
    auto* object = reinterpret_cast<void**>(zpatch_nms_object);
    if (object) *object = zpatch_nms_vtable;
}

// Opt-in (nms_thunk): the 1.0.8.0 static-init thunk 0xD56060 is
// `call initialize_memoryAllocators` (0x401060) followed by a per-thread call,
// an atexit registration and the epilogue. The hook replaces the five-byte call:
// the body calls the allocator init through the published entry (so the game's
// own construction and its atexit registration run), stores the reimplemented
// vtable in the allocator object, then tail-jumps to the thunk's resume so the
// rest of the thunk stays the original's. The thunk reloads EAX/ECX/EDX in its
// own next instructions, so the body only needs to keep the stack balanced.
extern "C" __declspec(naked) void abi_nms_init_thunk()
{
    __asm {
        call dword ptr [zpatch_nms_init_fn]
        mov  eax, dword ptr [zpatch_nms_object]
        test eax, eax
        je   nms_thunk_done
        mov  ecx, OFFSET zpatch_nms_vtable
        mov  dword ptr [eax], ecx
    nms_thunk_done:
        jmp  dword ptr [zpatch_nms_thunk_resume]
        __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable)
    }
}

namespace
{
// NewMemorySystem's allocation registry: the original keeps the pointers it
// handed out in its own hash map (FNV-1a over the pointer's four bytes) under
// the recursive critical section at 0x101A0808. Only membership is observable,
// so the keys go in a flat open-addressing set behind an equivalent lock.
//
// It is a flat table rather than std::unordered_set because the set is touched
// on *every* game allocation and free: an unordered_set node is one heap
// allocation per insert (and a throw inside a noexcept caller on failure). The
// table allocates only when it grows, never throws (VirtualAlloc returns null
// and the insert is dropped), and keeps the same membership semantics.
CRITICAL_SECTION g_nms_lock;

class NmsRegistry final
{
public:
    bool insert(uint32_t key) noexcept
    {
        // 0 is the empty marker and 0xFFFFFFFF the erased marker; a four-byte
        // aligned pointer can be neither.
        if (key == kNmsEmpty || key == kNmsErased)
            return false;
        if (!EnsureCapacity())
            return false;
        return InsertNoGrow(key);
    }

    void erase(uint32_t key) noexcept
    {
        if (capacity_ == 0u || key == kNmsEmpty || key == kNmsErased)
            return;
        const size_t mask = capacity_ - 1u;
        for (size_t probe = 0; probe <= mask; ++probe)
        {
            uint32_t& slot = slots_[Probe(key, probe, mask)];
            if (slot == kNmsEmpty)
                return;
            if (slot == key)
            {
                slot = kNmsErased;
                --count_;
                return;
            }
        }
    }

    bool contains(uint32_t key) const noexcept
    {
        if (capacity_ == 0u || key == kNmsEmpty || key == kNmsErased)
            return false;
        const size_t mask = capacity_ - 1u;
        for (size_t probe = 0; probe <= mask; ++probe)
        {
            const uint32_t slot = slots_[Probe(key, probe, mask)];
            if (slot == kNmsEmpty)
                return false;
            if (slot == key)
                return true;
        }
        return false;
    }

    size_t size() const noexcept { return count_; }
    size_t capacity() const noexcept { return capacity_; }

private:
    static constexpr uint32_t kNmsEmpty = 0u;
    static constexpr uint32_t kNmsErased = 0xFFFFFFFFu;
    static constexpr size_t kMinCapacity = 1024u;

    static size_t Probe(uint32_t key, size_t probe, size_t mask) noexcept
    {
        // Multiplicative hash, then linear probing: the keys are aligned heap
        // addresses, whose low bits are all zero, so the multiply is what
        // spreads them.
        const uint32_t mixed = key * 2654435761u;
        return (static_cast<size_t>(mixed >> 8) + probe) & mask;
    }

    bool EnsureCapacity() noexcept
    {
        if (capacity_ == 0u)
            return Grow(kMinCapacity);
        // Keep the probe chains short: grow at 3/4 occupancy, counting erased
        // slots because they still terminate a probe. When the table is mostly
        // tombstones (churn), rebuild it at the same size instead of doubling,
        // so memory stays proportional to the live count.
        if ((used_ + 1u) * 4u >= capacity_ * 3u)
        {
            if (count_ * 2u <= used_)
                return Grow(capacity_);
            return Grow(capacity_ * 2u);
        }
        return true;
    }

    bool InsertNoGrow(uint32_t key) noexcept
    {
        const size_t mask = capacity_ - 1u;
        size_t erased = static_cast<size_t>(-1);
        for (size_t probe = 0; probe <= mask; ++probe)
        {
            uint32_t& slot = slots_[Probe(key, probe, mask)];
            if (slot == key)
                return true;
            if (slot == kNmsErased && erased == static_cast<size_t>(-1))
                erased = Probe(key, probe, mask);
            if (slot == kNmsEmpty)
            {
                uint32_t& target = erased != static_cast<size_t>(-1) ? slots_[erased] : slot;
                target = key;
                ++count_;
                if (erased == static_cast<size_t>(-1))
                    ++used_;
                return true;
            }
        }
        if (erased != static_cast<size_t>(-1))
        {
            slots_[erased] = key;
            ++count_;
            return true;
        }
        return false;
    }

    bool Grow(size_t wanted) noexcept
    {
        auto* replacement = static_cast<uint32_t*>(VirtualAlloc(
            nullptr, wanted * sizeof(uint32_t), MEM_COMMIT | MEM_RESERVE,
            PAGE_READWRITE));
        if (replacement == nullptr)
        {
            // Out of address space: keep the old table and drop this insert.
            // Membership is only a query, so a missing entry is a false
            // negative like the original's own allocation failure path.
            return false;
        }
        uint32_t* const old = slots_;
        const size_t old_capacity = capacity_;
        slots_ = replacement;
        capacity_ = wanted;
        used_ = 0u;
        size_t live = 0u;
        for (size_t index = 0; index < old_capacity; ++index)
        {
            const uint32_t key = old[index];
            if (key != kNmsEmpty && key != kNmsErased)
            {
                InsertNoGrow(key);
                ++live;
            }
        }
        count_ = live;
        if (old != nullptr)
            VirtualFree(old, 0, MEM_RELEASE);
        return true;
    }

    uint32_t* slots_{};
    size_t capacity_{};
    size_t count_{};
    size_t used_{};
};

NmsRegistry g_nms_allocations;

struct NmsRegistryInit final
{
    NmsRegistryInit() noexcept { InitializeCriticalSection(&g_nms_lock); }
};
NmsRegistryInit g_nms_registry_init;
}  // namespace

// vtable[2] (0x10038360): HeapAlloc-backed allocate with the original's
// alignment scheme. ECX is the object (unused by the original's body too).
extern "C" void* __stdcall zpatch_nms_alloc(uint32_t size, uint32_t align,
                                            uint32_t flags) noexcept
{
    (void)flags;
    const uint32_t unit = align < 0x10u ? 0x10u : align;
    const uint32_t want = size < unit ? unit : size;
    void* base = HeapAlloc(GetProcessHeap(), 0, want + unit * 2u);
    if (!base) return nullptr;
    const auto raw = reinterpret_cast<uintptr_t>(base);
    const uintptr_t aligned = ((raw - 1u) / unit + 2u) * unit;
    *reinterpret_cast<uint32_t*>(aligned - 4u) =
        static_cast<uint32_t>(aligned - raw);
    auto* result = reinterpret_cast<void*>(aligned);
    std::memset(result, 0xCD, want);
    EnterCriticalSection(&g_nms_lock);
    g_nms_allocations.insert(static_cast<uint32_t>(aligned));
    LeaveCriticalSection(&g_nms_lock);
    return result;
}

// vtable[3] (0x10038430): recover the HeapAlloc base from [ptr-4], free it and
// drop the pointer from the registry.
extern "C" void __stdcall zpatch_nms_free(void* ptr) noexcept
{
    if (!ptr) return;
    const auto raw = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t base = raw - *reinterpret_cast<const uint32_t*>(raw - 4u);
    HeapFree(GetProcessHeap(), 0, reinterpret_cast<void*>(base));
    EnterCriticalSection(&g_nms_lock);
    g_nms_allocations.erase(static_cast<uint32_t>(raw));
    LeaveCriticalSection(&g_nms_lock);
}

// vtable[18] (0x100385A0): the game's "is this pointer mine" query.
extern "C" uint8_t __stdcall zpatch_nms_contains(uint32_t key) noexcept
{
    EnterCriticalSection(&g_nms_lock);
    const bool found = g_nms_allocations.contains(key);
    LeaveCriticalSection(&g_nms_lock);
    return found ? uint8_t{1} : uint8_t{0};
}

// Offline check for the registry above (native tests only): a local instance,
// so the live registry is untouched. Exercises insert/erase/contains, growth
// past the first block, tombstone reuse and the reserved keys.
extern "C" bool zpatch_nms_registry_selftest() noexcept
{
    NmsRegistry registry;
    constexpr uint32_t kKeys = 20000u;
    uint32_t keys[kKeys];
    uint32_t state = 0x12345678u;
    for (uint32_t i = 0; i < kKeys; ++i)
    {
        // Aligned, 4-byte-spaced pseudo-random pointers, like the real keys.
        state = state * 1664525u + 1013904223u;
        keys[i] = (state & 0x0FFFFFFFu) * 16u;
    }

    for (uint32_t i = 0; i < kKeys; ++i)
        if (!registry.insert(keys[i])) return false;
    if (registry.size() != kKeys) return false;
    if (registry.capacity() <= 1024u) return false;  // grew at least once

    // Duplicates are idempotent, not new entries.
    if (!registry.insert(keys[0])) return false;
    if (registry.size() != kKeys) return false;

    // The reserved keys are never members.
    if (registry.insert(0u)) return false;
    if (registry.insert(0xFFFFFFFFu)) return false;
    if (registry.contains(0u) || registry.contains(0xFFFFFFFFu)) return false;

    // Erase every other key; the rest must survive, including across the
    // tombstone-heavy table.
    for (uint32_t i = 0; i < kKeys; i += 2)
        registry.erase(keys[i]);
    if (registry.size() != kKeys / 2) return false;
    for (uint32_t i = 0; i < kKeys; ++i)
        if (registry.contains(keys[i]) != ((i & 1u) != 0u)) return false;

    // Re-inserting into tombstones must not grow the size past live entries.
    for (uint32_t i = 0; i < kKeys; i += 2)
        if (!registry.insert(keys[i])) return false;
    if (registry.size() != kKeys) return false;
    for (uint32_t i = 0; i < kKeys; ++i)
        if (!registry.contains(keys[i])) return false;

    // Erasing a non-member changes nothing.
    registry.erase(0xDEADBEE0u);
    if (registry.size() != kKeys) return false;

    // Churn: repeated erase-all/re-insert cycles must stay bounded instead of
    // doubling on tombstones alone.
    for (uint32_t cycle = 0; cycle < 8; ++cycle)
    {
        for (uint32_t i = 0; i < kKeys; ++i) registry.erase(keys[i]);
        if (registry.size() != 0u) return false;
        for (uint32_t i = 0; i < kKeys; ++i)
            if (!registry.insert(keys[i])) return false;
    }
    if (!registry.contains(keys[kKeys - 1])) return false;
    return registry.capacity() <= 4u * kKeys;
}

// The CRageAllocator vtable, entry order and per-entry conventions as the
// original's 0x100B5038 (see the individual methods above).
extern "C"
{
uintptr_t zpatch_nms_vtable[24] =
{
        reinterpret_cast<uintptr_t>(&abi_nms_ctor),         // [0] 0x10038640
        reinterpret_cast<uintptr_t>(&abi_nms_reject_ptr),   // [1] 0x10038350
        reinterpret_cast<uintptr_t>(&zpatch_nms_alloc),     // [2] 0x10038360
        reinterpret_cast<uintptr_t>(&zpatch_nms_free),      // [3] 0x10038430
        reinterpret_cast<uintptr_t>(&abi_nms_self),         // [4] 0x10038540
        reinterpret_cast<uintptr_t>(&abi_nms_self),         // [5] 0x10038540
        reinterpret_cast<uintptr_t>(&abi_nms_zero_ptr),     // [6] 0x10001950
        reinterpret_cast<uintptr_t>(&abi_nms_zero_ptr),     // [7] 0x10001950
        reinterpret_cast<uintptr_t>(&abi_better_mp_sync),   // [8] 0x100057F0
        reinterpret_cast<uintptr_t>(&abi_better_mp_sync),   // [9] 0x100057F0
        reinterpret_cast<uintptr_t>(&abi_rgsc_login_removed),  // [10] 0x10038550
        reinterpret_cast<uintptr_t>(&abi_nms_noop_ret0),    // [11] 0x10038560
        reinterpret_cast<uintptr_t>(&abi_savegame_fix_null),   // [12] 0x10038570
        reinterpret_cast<uintptr_t>(&abi_nms_noop_pair),    // [13] 0x10038580
        reinterpret_cast<uintptr_t>(&abi_nms_noop_ret0),    // [14] 0x10038560
        reinterpret_cast<uintptr_t>(&abi_nms_reject),       // [15] 0x10038590
        reinterpret_cast<uintptr_t>(&abi_nms_reject),       // [16] 0x10038590
        reinterpret_cast<uintptr_t>(&abi_nms_noop_ret0),    // [17] 0x10038560
        reinterpret_cast<uintptr_t>(&zpatch_nms_contains),  // [18] 0x100385A0
        reinterpret_cast<uintptr_t>(&abi_nms_thunk18),      // [19] 0x10038620
        reinterpret_cast<uintptr_t>(&abi_better_mp_sync),   // [20] 0x100057F0
        reinterpret_cast<uintptr_t>(&abi_better_mp_sync),   // [21] 0x100057F0
        reinterpret_cast<uintptr_t>(&abi_nms_noop_arg),     // [22] 0x10038630
        reinterpret_cast<uintptr_t>(&abi_nms_reject),       // [23] 0x10038590
};
}  // extern "C"

struct AbiBody final
{
    uint32_t reference_va;
    void (*address)();
};

// Only destinations whose conventions are verified appear here; everything else
// resolves to 0 and the installer reports it as skipped.
const AbiBody kAbiBodies[] =
{
        {0x10005590u, reinterpret_cast<void(*)()>(&abi_aim_projectile_crash_fix)},
        {0x100057f0u, reinterpret_cast<void(*)()>(&abi_better_mp_sync)},
        {0x10005ed0u, reinterpret_cast<void(*)()>(&abi_combined_radio_freeze_fix)},
        {0x10006880u, reinterpret_cast<void(*)()>(&abi_carparts_shadow)},
        {0x10044530u, reinterpret_cast<void(*)()>(&abi_popup_headlights)},
        {0x10044560u, reinterpret_cast<void(*)()>(&abi_popup_headlights_edx)},
        {0x1001ADB0u, reinterpret_cast<void(*)()>(&abi_fchs_shim)},
        {0x100064F0u, reinterpret_cast<void(*)()>(&abi_dual_headlights_new)},
        {0x10006630u, reinterpret_cast<void(*)()>(&abi_dual_headlights_old)},
        {0x1001B2E0u, reinterpret_cast<void(*)()>(&abi_car_decals_fix)},
        {0x10037BA0u, reinterpret_cast<void(*)()>(&abi_bad_player_model)},
        {0x10050AD0u, reinterpret_cast<void(*)()>(&abi_death_music_install)},
        {0x10050A50u, reinterpret_cast<void(*)()>(&abi_death_music_enabled)},
        {0x100521E0u, reinterpret_cast<void(*)()>(&abi_new_saves_compat)},
        {0x100054E0u, reinterpret_cast<void(*)()>(&abi_gfwl_stub)},
        {0x1003D3E0u, reinterpret_cast<void(*)()>(&abi_nolivery_e)},
        {0x100068A0u, reinterpret_cast<void(*)()>(&abi_carparts_shadow_2)},
        {0x100068D0u, reinterpret_cast<void(*)()>(&abi_carparts_shadow_2b)},
        {0x10046C80u, reinterpret_cast<void(*)()>(&abi_garage_body_a)},
        {0x10046CA0u, reinterpret_cast<void(*)()>(&abi_garage_body_b)},
        {0x100051B0u, reinterpret_cast<void(*)()>(&abi_steer_detour_eax)},
        {0x100051E0u, reinterpret_cast<void(*)()>(&abi_steer_detour_edx)},
        {0x100512F0u, reinterpret_cast<void(*)()>(&abi_local_settings_detour)},
        {0x10051300u, reinterpret_cast<void(*)()>(&abi_local_settings_fallback_detour)},
        {0x10037510u, reinterpret_cast<void(*)()>(&abi_spawns_probe)},
        {0x100372F0u, reinterpret_cast<void(*)()>(&abi_spawns_guard)},
        {0x10037330u, reinterpret_cast<void(*)()>(&abi_spawns_case_0x24)},
        {0x100373F0u, reinterpret_cast<void(*)()>(&abi_spawns_case_0x3D)},
        {0x10037420u, reinterpret_cast<void(*)()>(&abi_spawns_case_0x56)},
        {0x10037390u, reinterpret_cast<void(*)()>(&abi_spawns_case_0x6F)},
        {0x10037450u, reinterpret_cast<void(*)()>(&abi_spawns_case_0x88)},
        {0x10037480u, reinterpret_cast<void(*)()>(&abi_spawns_case_0x9E)},
        {0x100374E0u, reinterpret_cast<void(*)()>(&abi_spawns_case_0xB4)},
        {0x10037360u, reinterpret_cast<void(*)()>(&abi_spawns_case_0xCA)},
        {0x100373C0u, reinterpret_cast<void(*)()>(&abi_spawns_case_0xE0)},
        {0x100374B0u, reinterpret_cast<void(*)()>(&abi_spawns_case_0xF6)},
        {0x10003e50u, reinterpret_cast<void(*)()>(&abi_game_hash)},
        {0x10050870u, reinterpret_cast<void(*)()>(&abi_ped_models_in_cutscenes_check)},
        {0x10045a30u, reinterpret_cast<void(*)()>(&abi_improved_shader_streaming_find_hpe)},
        {0x10045a80u, reinterpret_cast<void(*)()>(&abi_improved_shader_streaming_find_v)},
        {0x1004aab0u, reinterpret_cast<void(*)()>(&abi_vram_fix_shim)},
        // 0x100064b0 (DrivebyFOVFix) is registered above: the arm installs
        // unconditionally when its pattern matches, and the pattern exists on 1.0.8.0.
        {0x1003d310u, reinterpret_cast<void(*)()>(&abi_nolivery_a)},
        {0x1003d340u, reinterpret_cast<void(*)()>(&abi_nolivery_b)},
        {0x1003d370u, reinterpret_cast<void(*)()>(&abi_nolivery_c)},
        {0x1003d3a0u, reinterpret_cast<void(*)()>(&abi_nolivery_d)},
        {0x10036f00u, reinterpret_cast<void(*)()>(&abi_crash_msg_shim)},
        {0x10051600u, reinterpret_cast<void(*)()>(&abi_newfiles_register_body)},
        {0x10051890u, reinterpret_cast<void(*)()>(&abi_newfiles_update_a)},
        {0x100518d0u, reinterpret_cast<void(*)()>(&abi_newfiles_update_b)},
        {0x10005650u, reinterpret_cast<void(*)()>(&abi_benchmark_a)},
        {0x10005670u, reinterpret_cast<void(*)()>(&abi_benchmark_b)},
        {0x10005af0u, reinterpret_cast<void(*)()>(&abi_building_dynamic_shadows)},
        {0x10006310u, reinterpret_cast<void(*)()>(&abi_copsirens_a)},
        {0x10006330u, reinterpret_cast<void(*)()>(&abi_copsirens_b)},
        {0x100040e0u, reinterpret_cast<void(*)()>(&abi_copsirens_ide)},
        {0x10005890u, reinterpret_cast<void(*)()>(&abi_borderless_w)},
        {0x10005900u, reinterpret_cast<void(*)()>(&abi_borderless_a)},
        {0x10050540u, reinterpret_cast<void(*)()>(&abi_speedup_body)},
        {0x1001b080u, reinterpret_cast<void(*)()>(&abi_episode_switch_sum)},
        {0x10023480u, reinterpret_cast<void(*)()>(&abi_load_dlcs_dummy)},
        {0x10023490u, reinterpret_cast<void(*)()>(&abi_load_dlcs_struct_fixup)},
        {0x10045C30u, reinterpret_cast<void(*)()>(&abi_skip_intro_body)},
        {0x10046610u, reinterpret_cast<void(*)()>(&abi_skip_menu_body)},
        {0x10038550u, reinterpret_cast<void(*)()>(&abi_rgsc_login_removed)},
        {0x10038570u, reinterpret_cast<void(*)()>(&abi_savegame_fix_null)},
        // NewMemorySystem's CRageAllocator stubs (the E8 body's vtable); the alloc/
        // free/contains methods are compiled methods registered only through the
        // vtable, not here.
        {0x10038640u, reinterpret_cast<void(*)()>(&abi_nms_ctor)},
        {0x10038350u, reinterpret_cast<void(*)()>(&abi_nms_reject_ptr)},
        {0x10038540u, reinterpret_cast<void(*)()>(&abi_nms_self)},
        {0x10001950u, reinterpret_cast<void(*)()>(&abi_nms_zero_ptr)},
        {0x10038560u, reinterpret_cast<void(*)()>(&abi_nms_noop_ret0)},
        {0x10038580u, reinterpret_cast<void(*)()>(&abi_nms_noop_pair)},
        {0x10038590u, reinterpret_cast<void(*)()>(&abi_nms_reject)},
        {0x10038620u, reinterpret_cast<void(*)()>(&abi_nms_thunk18)},
        {0x10038630u, reinterpret_cast<void(*)()>(&abi_nms_noop_arg)},
        {0x10038670u, reinterpret_cast<void(*)()>(&abi_nms_apply)},
        {0x10045580u, reinterpret_cast<void(*)()>(&abi_savegame_fix_close)},
        {0x100455a0u, reinterpret_cast<void(*)()>(&abi_savegame_fix_copy)},
        {0x100455e0u, reinterpret_cast<void(*)()>(&abi_savegame_fix_mutex)},
        // Crash-fix shims (0x10036ee0..0x10005d80). WantedLevelCrashFix's null
        // path pops ebx (the site sits after `push ebx`); CollisionDeadlock needs
        // eax/edx for its predicate (see the body).
        {0x10036ee0u, reinterpret_cast<void(*)()>(&abi_blackscreen_shim)},
        {0x1003d7b0u, reinterpret_cast<void(*)()>(&abi_path_node_shim)},
        {0x1003d870u, reinterpret_cast<void(*)()>(&abi_plane_crash_shim)},
        {0x10050490u, reinterpret_cast<void(*)()>(&abi_wanted_level_shim)},
        {0x10036e90u, reinterpret_cast<void(*)()>(&abi_group_hack_shim)},
        {0x10046750u, reinterpret_cast<void(*)()>(&abi_smpa60_shim)},
        {0x10046780u, reinterpret_cast<void(*)()>(&abi_smpa60_tail_shim)},
        {0x10036eb0u, reinterpret_cast<void(*)()>(&abi_freeze_gun_shim)},
        {0x1003d9a0u, reinterpret_cast<void(*)()>(&abi_player_ped_components_shim)},
        {0x1001af60u, reinterpret_cast<void(*)()>(&abi_emissive_lerp_shim)},
        {0x100469e0u, reinterpret_cast<void(*)()>(&abi_super_lod_fix_shim)},
        {0x1001b040u, reinterpret_cast<void(*)()>(&abi_episode_switch_store_shim)},
        {0x1001b060u, reinterpret_cast<void(*)()>(&abi_episode_switch_shim_b)},
        {0x1001b0a0u, reinterpret_cast<void(*)()>(&abi_episode_switch_vcall)},
        {0x1001b0c0u, reinterpret_cast<void(*)()>(&abi_episode_switch_shim_c)},
        {0x1001ef60u, reinterpret_cast<void(*)()>(&abi_bike_physics_detour)},
        {0x10005d80u, reinterpret_cast<void(*)()>(&abi_collision_deadlock_walk)},
        {0x10024060u, reinterpret_cast<void(*)()>(&abi_mouse_axis_float)},
        {0x100240C0u, reinterpret_cast<void(*)()>(&abi_mouse_axis_neg)},
        {0x10024120u, reinterpret_cast<void(*)()>(&abi_mouse_axis_int)},
        {0x10024170u, reinterpret_cast<void(*)()>(&abi_mouse_axis_flag)},
        {0x10024440u, reinterpret_cast<void(*)()>(&abi_mf_angle_step)},
        {0x10024560u, reinterpret_cast<void(*)()>(&abi_mouse_angle)},
        {0x100243A0u, reinterpret_cast<void(*)()>(&abi_mouse_advance)},
        {0x1004C3C0u, reinterpret_cast<void(*)()>(&abi_ooc_branch)},
        {0x1004C350u, reinterpret_cast<void(*)()>(&abi_ooc_wrap)},
        {0x100388C0u, reinterpret_cast<void(*)()>(&abi_cutscene_chain_b)},
        {0x100064B0u, reinterpret_cast<void(*)()>(&abi_driveby_fov_shim)},
};

uintptr_t ResolveHookBody(uint32_t reference_va, void*) noexcept
{
    for (const AbiBody& body : kAbiBodies)
    {
        if (body.reference_va == reference_va) return reinterpret_cast<uintptr_t>(body.address);
    }
    return 0;
}

size_t HookBodyCount() noexcept
{
    return sizeof(kAbiBodies) / sizeof(kAbiBodies[0]);
}



// Name-based lookup for the installers: they take a body's address and the
// reference-VA registry uses the same definitions, so neither side needs 130
// hand-written declarations.
extern "C" void* AbiByName(const char* name) noexcept
{
    static const struct
    {
        const char* name;
        void (*address)();
    } kAbiNames[] = {
    {"abi_aim_projectile_crash_fix", reinterpret_cast<void(*)()>(&abi_aim_projectile_crash_fix)},
    {"abi_bad_player_model", reinterpret_cast<void(*)()>(&abi_bad_player_model)},
    {"abi_benchmark_a", reinterpret_cast<void(*)()>(&abi_benchmark_a)},
    {"abi_benchmark_b", reinterpret_cast<void(*)()>(&abi_benchmark_b)},
    {"abi_better_mp_sync", reinterpret_cast<void(*)()>(&abi_better_mp_sync)},
    {"abi_bike_physics_detour", reinterpret_cast<void(*)()>(&abi_bike_physics_detour)},
    {"abi_bike_physics_helper", reinterpret_cast<void(*)()>(&abi_bike_physics_helper)},
    {"abi_blackscreen_shim", reinterpret_cast<void(*)()>(&abi_blackscreen_shim)},
    {"abi_borderless_a", reinterpret_cast<void(*)()>(&abi_borderless_a)},
    {"abi_borderless_w", reinterpret_cast<void(*)()>(&abi_borderless_w)},
    {"abi_bpm_check", reinterpret_cast<void(*)()>(&abi_bpm_check)},
    {"abi_building_dynamic_shadows", reinterpret_cast<void(*)()>(&abi_building_dynamic_shadows)},
    {"abi_car_decals_fix", reinterpret_cast<void(*)()>(&abi_car_decals_fix)},
    {"abi_carparts_shadow", reinterpret_cast<void(*)()>(&abi_carparts_shadow)},
    {"abi_carparts_shadow_2", reinterpret_cast<void(*)()>(&abi_carparts_shadow_2)},
    {"abi_carparts_shadow_2b", reinterpret_cast<void(*)()>(&abi_carparts_shadow_2b)},
    {"abi_collision_deadlock_walk", reinterpret_cast<void(*)()>(&abi_collision_deadlock_walk)},
    {"abi_combined_radio_freeze_fix", reinterpret_cast<void(*)()>(&abi_combined_radio_freeze_fix)},
    {"abi_copsirens_a", reinterpret_cast<void(*)()>(&abi_copsirens_a)},
    {"abi_copsirens_b", reinterpret_cast<void(*)()>(&abi_copsirens_b)},
    {"abi_copsirens_check_a", reinterpret_cast<void(*)()>(&abi_copsirens_check_a)},
    {"abi_copsirens_check_b", reinterpret_cast<void(*)()>(&abi_copsirens_check_b)},
    {"abi_copsirens_ide", reinterpret_cast<void(*)()>(&abi_copsirens_ide)},
    {"abi_crash_msg_shim", reinterpret_cast<void(*)()>(&abi_crash_msg_shim)},
    {"abi_cutscene_chain_b", reinterpret_cast<void(*)()>(&abi_cutscene_chain_b)},
    {"abi_death_music_enabled", reinterpret_cast<void(*)()>(&abi_death_music_enabled)},
    {"abi_death_music_install", reinterpret_cast<void(*)()>(&abi_death_music_install)},
    {"abi_driveby_fov_helper", reinterpret_cast<void(*)()>(&abi_driveby_fov_helper)},
    {"abi_driveby_fov_note", reinterpret_cast<void(*)()>(&abi_driveby_fov_note)},
    {"abi_driveby_fov_note_impl", reinterpret_cast<void(*)()>(&abi_driveby_fov_note_impl)},
    {"abi_driveby_fov_shim", reinterpret_cast<void(*)()>(&abi_driveby_fov_shim)},
    {"abi_dual_headlights_init", reinterpret_cast<void(*)()>(&abi_dual_headlights_init)},
    {"abi_dual_headlights_new", reinterpret_cast<void(*)()>(&abi_dual_headlights_new)},
    {"abi_dual_headlights_old", reinterpret_cast<void(*)()>(&abi_dual_headlights_old)},
    {"abi_emissive_lerp_shim", reinterpret_cast<void(*)()>(&abi_emissive_lerp_shim)},
    {"abi_episode_switch_shim_b", reinterpret_cast<void(*)()>(&abi_episode_switch_shim_b)},
    {"abi_episode_switch_shim_c", reinterpret_cast<void(*)()>(&abi_episode_switch_shim_c)},
    {"abi_episode_switch_store_shim", reinterpret_cast<void(*)()>(&abi_episode_switch_store_shim)},
    {"abi_episode_switch_sum", reinterpret_cast<void(*)()>(&abi_episode_switch_sum)},
    {"abi_episode_switch_vcall", reinterpret_cast<void(*)()>(&abi_episode_switch_vcall)},
    {"abi_fchs_check", reinterpret_cast<void(*)()>(&abi_fchs_check)},
    {"abi_fchs_shim", reinterpret_cast<void(*)()>(&abi_fchs_shim)},
    {"abi_freeze_gun_shim", reinterpret_cast<void(*)()>(&abi_freeze_gun_shim)},
    {"abi_game_hash", reinterpret_cast<void(*)()>(&abi_game_hash)},
    {"abi_garage_body_a", reinterpret_cast<void(*)()>(&abi_garage_body_a)},
    {"abi_garage_entry_a", reinterpret_cast<void(*)()>(&abi_garage_entry_a)},
    {"abi_garage_body_b", reinterpret_cast<void(*)()>(&abi_garage_body_b)},
    {"abi_gfwl_patch", reinterpret_cast<void(*)()>(&abi_gfwl_patch)},
    {"abi_gfwl_stub", reinterpret_cast<void(*)()>(&abi_gfwl_stub)},
    {"abi_group_hack_shim", reinterpret_cast<void(*)()>(&abi_group_hack_shim)},
    {"abi_ide_parse", reinterpret_cast<void(*)()>(&abi_ide_parse)},
    {"abi_improved_shader_streaming_find_hpe", reinterpret_cast<void(*)()>(&abi_improved_shader_streaming_find_hpe)},
    {"abi_improved_shader_streaming_find_v", reinterpret_cast<void(*)()>(&abi_improved_shader_streaming_find_v)},
    {"abi_load_dlcs_dummy", reinterpret_cast<void(*)()>(&abi_load_dlcs_dummy)},
    {"abi_load_dlcs_struct_fixup", reinterpret_cast<void(*)()>(&abi_load_dlcs_struct_fixup)},
    {"abi_local_settings_detour", reinterpret_cast<void(*)()>(&abi_local_settings_detour)},
    {"abi_local_settings_fallback_detour", reinterpret_cast<void(*)()>(&abi_local_settings_fallback_detour)},
    {"abi_mf_angle_step", reinterpret_cast<void(*)()>(&abi_mf_angle_step)},
    {"abi_mouse_advance", reinterpret_cast<void(*)()>(&abi_mouse_advance)},
    {"abi_mouse_angle", reinterpret_cast<void(*)()>(&abi_mouse_angle)},
    {"abi_mouse_axis_flag", reinterpret_cast<void(*)()>(&abi_mouse_axis_flag)},
    {"abi_mouse_axis_float", reinterpret_cast<void(*)()>(&abi_mouse_axis_float)},
    {"abi_mouse_axis_int", reinterpret_cast<void(*)()>(&abi_mouse_axis_int)},
    {"abi_mouse_axis_neg", reinterpret_cast<void(*)()>(&abi_mouse_axis_neg)},
    {"abi_new_saves_compat", reinterpret_cast<void(*)()>(&abi_new_saves_compat)},
    {"abi_new_saves_convert", reinterpret_cast<void(*)()>(&abi_new_saves_convert)},
    {"abi_new_saves_p3_hook", reinterpret_cast<void(*)()>(&abi_new_saves_p3_hook)},
    {"abi_new_saves_prepare", reinterpret_cast<void(*)()>(&abi_new_saves_prepare)},
    {"abi_new_saves_read_hook", reinterpret_cast<void(*)()>(&abi_new_saves_read_hook)},
    {"abi_newfiles_call_add", reinterpret_cast<void(*)()>(&abi_newfiles_call_add)},
    {"abi_newfiles_call_obj", reinterpret_cast<void(*)()>(&abi_newfiles_call_obj)},
    {"abi_newfiles_cb", reinterpret_cast<void(*)()>(&abi_newfiles_cb)},
    {"abi_newfiles_find_obj", reinterpret_cast<void(*)()>(&abi_newfiles_find_obj)},
    {"abi_newfiles_register", reinterpret_cast<void(*)()>(&abi_newfiles_register)},
    {"abi_newfiles_register_body", reinterpret_cast<void(*)()>(&abi_newfiles_register_body)},
    {"abi_newfiles_update_a", reinterpret_cast<void(*)()>(&abi_newfiles_update_a)},
    {"abi_newfiles_update_b", reinterpret_cast<void(*)()>(&abi_newfiles_update_b)},
    {"abi_nms_apply", reinterpret_cast<void(*)()>(&abi_nms_apply)},
    {"abi_nms_init_thunk", reinterpret_cast<void(*)()>(&abi_nms_init_thunk)},
    {"abi_nms_ctor", reinterpret_cast<void(*)()>(&abi_nms_ctor)},
    {"abi_nms_noop_arg", reinterpret_cast<void(*)()>(&abi_nms_noop_arg)},
    {"abi_nms_noop_pair", reinterpret_cast<void(*)()>(&abi_nms_noop_pair)},
    {"abi_nms_noop_ret0", reinterpret_cast<void(*)()>(&abi_nms_noop_ret0)},
    {"abi_nms_reject", reinterpret_cast<void(*)()>(&abi_nms_reject)},
    {"abi_nms_reject_ptr", reinterpret_cast<void(*)()>(&abi_nms_reject_ptr)},
    {"abi_nms_self", reinterpret_cast<void(*)()>(&abi_nms_self)},
    {"abi_nms_thunk18", reinterpret_cast<void(*)()>(&abi_nms_thunk18)},
    {"abi_nms_zero_ptr", reinterpret_cast<void(*)()>(&abi_nms_zero_ptr)},
    {"abi_nolivery_a", reinterpret_cast<void(*)()>(&abi_nolivery_a)},
    {"abi_nolivery_b", reinterpret_cast<void(*)()>(&abi_nolivery_b)},
    {"abi_nolivery_c", reinterpret_cast<void(*)()>(&abi_nolivery_c)},
    {"abi_nolivery_copy_name", reinterpret_cast<void(*)()>(&abi_nolivery_copy_name)},
    {"abi_nolivery_count_loop", reinterpret_cast<void(*)()>(&abi_nolivery_count_loop)},
    {"abi_nolivery_d", reinterpret_cast<void(*)()>(&abi_nolivery_d)},
    {"abi_nolivery_e", reinterpret_cast<void(*)()>(&abi_nolivery_e)},
    {"abi_nolivery_hash_sign", reinterpret_cast<void(*)()>(&abi_nolivery_hash_sign)},
    {"abi_ooc_branch", reinterpret_cast<void(*)()>(&abi_ooc_branch)},
    {"abi_ooc_wrap", reinterpret_cast<void(*)()>(&abi_ooc_wrap)},
    {"abi_path_node_shim", reinterpret_cast<void(*)()>(&abi_path_node_shim)},
    {"abi_ped_models_in_cutscenes_check", reinterpret_cast<void(*)()>(&abi_ped_models_in_cutscenes_check)},
    {"abi_plane_crash_shim", reinterpret_cast<void(*)()>(&abi_plane_crash_shim)},
    {"abi_player_ped_components_shim", reinterpret_cast<void(*)()>(&abi_player_ped_components_shim)},
    {"abi_player_ped_helper", reinterpret_cast<void(*)()>(&abi_player_ped_helper)},
    {"abi_popup_check", reinterpret_cast<void(*)()>(&abi_popup_check)},
    {"abi_popup_headlights", reinterpret_cast<void(*)()>(&abi_popup_headlights)},
    {"abi_popup_headlights_edx", reinterpret_cast<void(*)()>(&abi_popup_headlights_edx)},
    {"abi_rgsc_login_removed", reinterpret_cast<void(*)()>(&abi_rgsc_login_removed)},
    {"abi_savegame_fix_close", reinterpret_cast<void(*)()>(&abi_savegame_fix_close)},
    {"abi_savegame_fix_copy", reinterpret_cast<void(*)()>(&abi_savegame_fix_copy)},
    {"abi_savegame_fix_mutex", reinterpret_cast<void(*)()>(&abi_savegame_fix_mutex)},
    {"abi_savegame_fix_null", reinterpret_cast<void(*)()>(&abi_savegame_fix_null)},
    {"abi_shader_stream_filter_hpe", reinterpret_cast<void(*)()>(&abi_shader_stream_filter_hpe)},
    {"abi_shader_stream_filter_v", reinterpret_cast<void(*)()>(&abi_shader_stream_filter_v)},
    {"abi_skip_intro_body", reinterpret_cast<void(*)()>(&abi_skip_intro_body)},
    {"abi_skip_intro_screens", reinterpret_cast<void(*)()>(&abi_skip_intro_screens)},
    {"abi_skip_menu_body", reinterpret_cast<void(*)()>(&abi_skip_menu_body)},
    {"abi_smpa60_shim", reinterpret_cast<void(*)()>(&abi_smpa60_shim)},
    {"abi_smpa60_tail_shim", reinterpret_cast<void(*)()>(&abi_smpa60_tail_shim)},
    {"abi_spawns_guard", reinterpret_cast<void(*)()>(&abi_spawns_guard)},
    {"abi_spawns_probe", reinterpret_cast<void(*)()>(&abi_spawns_probe)},
    {"abi_speedup_body", reinterpret_cast<void(*)()>(&abi_speedup_body)},
    {"abi_steer_body", reinterpret_cast<void(*)()>(&abi_steer_body)},
    {"abi_steer_detour_eax", reinterpret_cast<void(*)()>(&abi_steer_detour_eax)},
    {"abi_steer_detour_edx", reinterpret_cast<void(*)()>(&abi_steer_detour_edx)},
    {"abi_super_lod_fix_shim", reinterpret_cast<void(*)()>(&abi_super_lod_fix_shim)},
    {"abi_super_lod_helper", reinterpret_cast<void(*)()>(&abi_super_lod_helper)},
    {"abi_vehicle_component", reinterpret_cast<void(*)()>(&abi_vehicle_component)},
    {"abi_vram_fix_helper", reinterpret_cast<void(*)()>(&abi_vram_fix_helper)},
    {"abi_vram_fix_shim", reinterpret_cast<void(*)()>(&abi_vram_fix_shim)},
    {"abi_wanted_level_shim", reinterpret_cast<void(*)()>(&abi_wanted_level_shim)},
    };

    if (name == nullptr)
        return nullptr;
    for (const auto& row : kAbiNames)
    {
        if (std::strcmp(row.name, name) == 0)
            return reinterpret_cast<void*>(row.address);
    }
    return nullptr;
}

#endif  // _WIN32

