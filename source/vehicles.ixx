module;

#include <common.hxx>
// The /GS cookie the transcribed helpers' prologues read and their epilogues check
// (`__security_cookie`, `__security_check_cookie` in vcruntime.h), and the CRT entries
// the transcribed pieces call (`_stricmp`, `strcpy_s`).
#include <intrin.h>
#include <bit>
#include <cstdio>
#include <cstring>

export module vehicles;

import bodies;
import bytes;
import common;
import config;
import loader;
import native;
import options;
import patch;
import shared;

// The car-parts shadow bodies' published operands: C linkage because the
// transcribed asm names them directly. Each slot is the original's own .bss word;
// the two resume_if/else words hold the site's resume addresses (match+2 and
// match+0x45 of the "85 DB 7E 41 55 EB 0A" anchor).
extern "C"
{
    uint32_t zpatch_carparts_resume_if = 0;    // dword_1015BDC8
    uint32_t zpatch_carparts_resume_else = 0;  // dword_1015BDC0
    uint32_t zpatch_carparts_resume_2 = 0;     // dword_1015BDCC

    // dword_1015B5B8, defined in source/config.ixx: the original keeps it in `.data`
    // immediately after the [HeadlightConeOffsets] hash array, and the cone check's
    // walk reads it as the 513th hash, so this tree defines it as the next object after
    // that array. This arm publishes the same value into it.
    extern uint32_t zpatch_carparts_shadow_fn;

    // The collision body's node-walk test and the deadlock counter it compares.
    uint32_t zpatch_collision_should_walk = 0;  // dword_1015BDD4
    uint32_t zpatch_collision_iterations = 0;   // dword_1015BDD8

    // ---- ForceCarHeadlightShadows --------------------------------------------
    // dword_1015BDD0: the object the hooked function receives in ECX (the shim
    // stores it, the checker reads +0x2E of it). dword_101580C0: the offset float
    // the site's `movss xmm2, [0xD95B68]` is retargeted at; the original seeds its
    // copy with 2.0f on disk, so this one starts there too. dword_1015B5BC: the
    // resume, published from the site's own prologue before the E9 replaces it, and
    // defined in source/config.ixx as the second word of the [HeadlightConeOffsets]
    // block (the original keeps it between the cone's hashes and its offsets, and the
    // cone walk can read it).
    uint32_t zpatch_fchs_param = 0;
    float zpatch_fchs_offset = 2.0f;
    extern uint32_t zpatch_fchs_resume;

    // The bike-physics detour's published state: the incoming value the clamp
    // rewrites, the caller's EAX saved across the clamp call, and the resume (the
    // site's own `call`, match+9).
    float zpatch_bike_value = 0.0f;    // dword_1015AD7C
    uint32_t zpatch_bike_saved = 0;    // dword_1015AD80
    uint32_t zpatch_bike_resume = 0;   // dword_1015AD84

    // DualVehicleHeadlights' published state, one word per operand its two bodies
    // read: the light-state global the site pushes (the `push 0x1252D90` at
    // match+207), the three call targets both bodies share, the resume, the two
    // light indices the init helper sets, and the eight register slots a body saves
    // before its first draw and restores before its second.
    uint32_t zpatch_dual_bcc = 0;      // dword_10159BCC
    uint32_t zpatch_dual_bd0 = 0;      // dword_10159BD0
    uint32_t zpatch_dual_bd4 = 0;      // dword_10159BD4
    uint32_t zpatch_dual_be0 = 0;      // dword_10159BE0
    uint32_t zpatch_dual_be4 = 0;      // dword_10159BE4
    uint32_t zpatch_dual_light_a = 1;  // dword_1015802C (the original's on-disk 1)
    uint32_t zpatch_dual_light_b = 2;  // dword_10158030 (the original's on-disk 2)
    uint32_t zpatch_dual_r_eax = 0;    // dword_10159BC4
    uint32_t zpatch_dual_r_ebx = 0;    // dword_10159BEC
    uint32_t zpatch_dual_r_ecx = 0;    // dword_10159BD8
    uint32_t zpatch_dual_r_edx = 0;    // dword_10159BF0
    uint32_t zpatch_dual_r_esi = 0;    // dword_10159BC8
    uint32_t zpatch_dual_r_edi = 0;    // dword_10159BE8
    uint32_t zpatch_dual_r_ebp = 0;    // dword_10159BDC
    uint32_t zpatch_dual_r_esp = 0;    // dword_10159BF4

    // ---- sub_10004C70, the steer/engine/plane consumer ------------------------
    //
    // dword_1015AD38: the vehicle the detour was called for (the site's ESI). The
    // detour body stores it before the consumer runs; the consumer reads it once.
    uint32_t zpatch_steer_vehicle = 0;
    // dword_1015AD44: the site's resume, which the arm publishes (match+16: the
    // `jnz` the removed vtable call's flags feed).
    uint32_t zpatch_steer_resume = 0;
    // dword_1015AD4C: the game's part-drive function, which the arm reads out of the
    // call operand inside the site's own code (match+0x1A7/+0x1AB, or +0x8C/+0x90 on
    // the other build variant).
    uint32_t zpatch_steer_part_call = 0;
    // dword_10159B88 / dword_1015D63C / dword_10159CDC: the bone-matrix getter and
    // the two matrix setters. The original's loader preamble resolves all three from
    // the game image; ResolveSteerHelpers() does the same here.
    uint32_t zpatch_steer_bone = 0;
    uint32_t zpatch_steer_matrix_a = 0;
    uint32_t zpatch_steer_matrix_b = 0;
    // dword_1015AD78: the model-info array the name lookup starts from (a game
    // address, rebased through the image's preferred base like the BPM arm's copy).
    uint32_t zpatch_steer_model_array = 0;
    // byte_1015AD40 / byte_1015AD41 / byte_10159C17: the three per-key switches the
    // original's handler sets before it calls the shared installer (0x10054A7A,
    // 0x10054B56, 0x10054AE8). They start zero and are never cleared.
    uint8_t zpatch_steer_enabled = 0;
    uint8_t zpatch_engine_enabled = 0;
    uint8_t zpatch_plane_enabled = 0;
    // byte_1015AD42 / dword_1015AD3C / dword_10159D34: the engine shake's live state
    // -- the direction bit, the phase (kept as float bits) and the last script
    // timestep the consumer saw.
    uint8_t zpatch_engine_descending = 0;
    uint32_t zpatch_engine_phase = 0;
    uint32_t zpatch_engine_timestep = 0;

    // ---- sub_10004C70's constants, in its own read order ----------------------
    //
    // The original keeps these in .rdata and reads them with an absolute operand
    // (`mulsd xmm0, qword ptr [0x101523a8]`). Each is declared with the type the
    // instruction reads -- `double` for the `mulsd`/`movsd`/`divsd`/`comisd`
    // operands, `float` for the two `movss`/`mulss` ones -- and initialised with
    // the number it is. Every literal below round-trips to the original's .rdata
    // bytes at the VA in its comment, bit for bit; the proof compares an absolute
    // operand as a slot, so it cannot see the value, and the value is pinned here.
    // No explicit alignment: a scalar load does not need one on x86, and the
    // compiler gives doubles and floats their natural alignment anyway.
    double zpatch_steer_neg_pi = -3.141592653589793;   // 0x101523A8
    double zpatch_steer_pi = 3.141592653589793;        // 0x10152358
    double zpatch_steer_degrees = 180.0;               // 0x10152370
    double zpatch_steer_half = 0.5;                    // 0x10152348
    double zpatch_steer_neg_half = -0.5;               // 0x10152398
    double zpatch_steer_three_quarters = 0.75;         // 0x10152350
    double zpatch_steer_quarter = 0.25;                // 0x10152338
    double zpatch_steer_epsilon = 0.01;                // 0x10152320
    double zpatch_steer_neg_epsilon = -0.01;           // 0x10152390
    float zpatch_steer_one = 1.0f;                     // 0x10152310
    float zpatch_steer_hundred = 100.0f;               // 0x10152388

    // The two bone names the consumer hands sub_10003F40: the original's own .rdata
    // strings at 0x10129518 and 0x10129570, loaded with `mov edx, imm32`.
    const char zpatch_steer_name_wheel[] = "steeringwheel";
    const char zpatch_steer_name_engine[] = "engineblock";

    // The native layer's resolve entry (source/native.ixx) and the salt the consumer
    // passes it. The original's four `mov ecx, dword_1015803C / call sub_10003980`
    // pairs are direct calls, so ours go through the same six-byte forwarder the
    // drive-by helper uses.
    extern uint32_t zpatch_native_salt_1015803C;
    extern uintptr_t zpatch_native_resolve_entry;

    // ---- GarageModelFix ------------------------------------------------------
    // dword_10159CD4 / dword_10159CD0: the two sites' resumes (match+6, match+5).
    uint32_t zpatch_garage_resume_a = 0;
    uint32_t zpatch_garage_resume_b = 0;
    // The garage_entry group's prologue form: the 0xC68B90 entry is 0x20 bytes
    // before the mid-body anchor and starts with `movss xmm0, ds:X` (8 bytes).
    // Resume = prologue + 8; the float the skipped instruction loads is
    // published as a value so the body can reload it after the helper call.
    uint32_t zpatch_garage_entry_resume_a = 0;
    float zpatch_garage_scale = 0.0f;
    // byte_10159B85: the original's DllMain sets it to 1 when CitizenIV.exe is
    // loaded (0x10055DFB..0x10055E17); the garage helper then skips the colour copy.
    uint8_t zpatch_garage_citizen = 0;

    // ---- NoLiveryLimit -------------------------------------------------------
    // dword_10159BA8 (the name), dword_10159BB0 (the sign index), dword_10159B98
    // (the computed hash), dword_10159B9C (the A/B/C resume), dword_10159BA4 (the
    // count loop's lookup), dword_10159BAC (the D/E resume), dword_10159B94 (the
    // lookup's `this`), dword_10159B90 (the counted variants), dword_10159BA0 (the
    // name buffer) and dword_10159B8C (the copy destination).
    uint32_t zpatch_nolivery_name = 0;
    uint32_t zpatch_nolivery_index = 0;
    uint32_t zpatch_nolivery_hash = 0;
    uint32_t zpatch_nolivery_resume = 0;
    uint32_t zpatch_nolivery_fn = 0;
    uint32_t zpatch_nolivery_resume_d = 0;
    uint32_t zpatch_nolivery_arg = 0;
    uint32_t zpatch_nolivery_count = 0;
    uint32_t zpatch_nolivery_bufptr = 0;
    uint32_t zpatch_nolivery_dest = 0;
    // The two CRT entries the transcribed helpers call: `_stricmp` for the model
    // registry lookup and `strcpy_s` for the 0x10-byte name copy. Both stay pointer
    // slots because the original calls them through its own IAT (`FF 15`), and an
    // absolute operand on either side compares as a slot.
    uintptr_t zpatch_steer_stricmp = 0;
    uintptr_t zpatch_nolivery_strcpy_s = 0;

    // The two `%s_sign_%d` sites' format literal: the original's own string at
    // 0x1012D4BC, which its formatter wrapper is called with.
    const char zpatch_nolivery_format_text[] = "%s_sign_%d";
}

// The other modules' pieces these arms share: the game's model-index lookup
// (source/shared.ixx, dword_10159C0C), the wheelangle tables the IDE parser hook
// fills and the hook's own resume (source/misc.ixx, dword_1015A538 / dword_10159D38
// / dword_1015BE10). The three steer keys install that same hook -- the original's
// shared installer calls sub_10004110 -- so the consumer reads the one table set the
// original has.
extern "C"
{
    extern uintptr_t zpatch_model_lookup;
    extern uint32_t zpatch_wheelangle_models[512];
    extern uint32_t zpatch_wheelangle_angles[512];
    extern uint32_t zpatch_ide_hook_slot;
    extern uint32_t zpatch_format_entry;  // sub_10001280 (source/stability.ixx)
}

// ---- FreezeCarFix / DebugPrintSpawns ----------------------------------------
//
// The original's handler 0x10037C70 installs sixteen `E8` detours, at seventeen
// rows (the primary and the short prefix name the same site) across six recovered
// signatures. The guard, the ten factory-dispatch thunks and the 394-instruction
// spawn probe are 0x100372F0 / 0x10037330..0x100374E0 / 0x10037510; the probe's
// diagnostic calls its own name/who/print helpers, which are compiler output and
// are rebuilt in C++ below. The arm is gated exactly like the original's: on the
// primary signature (absent on 1.0.0.0 / 1.0.4.0 / 1.0.0.4, so those builds write
// nothing), and the DebugPrintSpawns flag is read *inside* this gate.
//
// The state words keep the original's on-disk seeds: the selector starts at -1,
// the suppress flag at 0, and the six resume words are published before the sites
// are written.
extern "C"
{
    // 0x10158058: -1 means "no spawn kind chosen yet"; any other value is the
    // kind the body picked. Signed, because the value is a sentinel and a small
    // enumerator, not a bit mask.
    int32_t zpatch_spawns_kind = -1;
    uint32_t zpatch_spawns_class = 0;           // dword_10159C80 (captured arg2 byte)
    uint32_t zpatch_spawns_skip = 0;            // dword_10159CB4
    uint32_t zpatch_spawns_orig_a = 0;          // dword_10159CB0 (probe resume)
    uint32_t zpatch_spawns_orig_b = 0;          // dword_10159C98 (guard resume)
    uint32_t zpatch_spawns_case0_resume = 0;    // dword_10159CBC (site +0x24)
    uint32_t zpatch_spawns_case1_resume = 0;    // dword_10159C9C (site +0x3D)
    uint32_t zpatch_spawns_case2_resume = 0;    // dword_10159CA0 (site +0x56)
    uint32_t zpatch_spawns_case3_resume = 0;    // dword_10159CA8 (site +0x6F)
    uint32_t zpatch_spawns_case4_resume = 0;    // dword_10159C7C (site +0x88)
    uint32_t zpatch_spawns_case5_resume = 0;    // dword_10159C88 (site +0x9E)
    uint32_t zpatch_spawns_case6_resume = 0;    // dword_10159CAC (site +0xB4)
    uint32_t zpatch_spawns_case7_resume = 0;    // dword_10159C90 (site +0xCA)
    uint32_t zpatch_spawns_case8_resume = 0;    // dword_10159C94 (site +0xE0)
    uint32_t zpatch_spawns_case9_resume = 0;    // dword_10159C84 (site +0xF6)

    // The original's IAT slot for GetModuleHandleW (0x10063074), which the probe's
    // GTAC.dll test calls through; filled unconditionally by the publish arm.
    uint32_t zpatch_spawns_getmodulehandlew = 0;
    // byte_10159C15: DebugPrintSpawns, read only inside the FreezeCarFix gate
    // (`movzx ecx,[0x10159C15]; cmp esi,1; cmove ecx,1; mov [0x10159C15],cl`).
    uint8_t zpatch_spawns_debug = 0;
    // sub_10003E50 (source/shared.ixx), the original's own model-name hash.
    uint32_t zpatch_spawns_resolver = 0;
    // dword_1015AD78: the ped-model object array the probe walks. The original's
    // loader preamble resolves it on every run, so this arm resolves it under its
    // own gate too (the BadPlayerModelCrashFix arm carries the same slot for its
    // own bodies).
    uint32_t zpatch_spawns_ped_array = 0;

    // The probe's diagnostic text, at the original's recovered strings: the two
    // names, the module test, and the format each spawn class prints.
    const wchar_t zpatch_spawns_gtac[] = L"GTAC.dll";
    const char zpatch_spawns_fmt_car[] = "%s spawned a car (%s)";
    const char zpatch_spawns_fmt_noncar[] = "%s spawned a non-car car (%s)";
    const char zpatch_spawns_fmt_heli[] = "%s spawned a heli (%s)";
    const char zpatch_spawns_fmt_nonheli[] = "%s spawned a non-heli heli (%s)";
    const char zpatch_spawns_fmt_boat[] = "%s spawned a boat (%s)";
    const char zpatch_spawns_fmt_nonboat[] = "%s spawned a non-boat boat (%s)";
    const char zpatch_spawns_fmt_bike[] = "%s spawned a bike (%s)";
    const char zpatch_spawns_fmt_nonbike[] = "%s spawned a non-bike bike (%s)";
    const char zpatch_spawns_fmt_train[] = "%s spawned a train (%s)";
    const char zpatch_spawns_fmt_nontrain[] = "%s spawned a non-train train (%s)";
    const char zpatch_spawns_fmt_plane[] = "%s spawned a plane (%s)";
    const char zpatch_spawns_fmt_nonplane[] = "%s spawned a non-plane plane (%s)";
    const char zpatch_spawns_fmt_ped[] = "%s spawned a ped (%s)";
    const char zpatch_spawns_fmt_nonped[] = "%s spawned a non-ped ped (%s)";
    const char zpatch_spawns_fmt_vehicleobject[] = "%s spawned a vehicle object (%s)";
    const char zpatch_spawns_fmt_object[] = "%s spawned an object (%s)";
    const char zpatch_spawns_fmt_generic[] = "%s spawned (%s)";
    const char zpatch_spawns_fmt_used_nonped[] = "%s used a non-ped model (%s)";
}

// Forward declarations for the transcribed bodies at the bottom of this
// file: the readable code above installs them (and a body may branch to
// another), so every one is named here. MSVC rejects `__declspec(naked)`
// on a declaration (C2488), so these carry the signature only; each
// definition in the tail is naked.
static void SpawnsNameEntry();
static void SpawnsWhoEntry();
static void SpawnsPrintEntry();
static void SpawnsFormatEntry();
static void SpawnsGuardBody();
static void SpawnsCase0x24Body();
static void SpawnsCase0x3DBody();
static void SpawnsCase0x56Body();
static void SpawnsCase0x6FBody();
static void SpawnsCase0x88Body();
static void SpawnsCase0x9EBody();
static void SpawnsCase0xB4Body();
static void SpawnsCase0xCABody();
static void SpawnsCase0xE0Body();
static void SpawnsCase0xF6Body();
static void SpawnsProbeBody();
static void CollisionDeadlockShim();
static void CarPartsShadowShim();
static void CarDynamicShadowShim();
static void CarDynamicShadowShimB();
static void FchsShimBody();
void FchsCheckCpp() noexcept;
static void CarDecalsSelectorShim();
static void BikePhysicsDetour();
static void DualHeadlightsNewBody();
static void DualHeadlightsOldBody();
static void GarageEntryBodyA();
static void* __fastcall VehicleComponentBody(void*, const char*);
static void SteerResolveEntry();
static void SteerConsumerBody();
static void NoliveryFormat();
static void SteerDetourEaxBody();
static void SteerDetourEdxBody();
static void GarageColourBody();
static void GarageLookupBody();
static void GarageBodyA();
static void GarageBodyB();
static void NoliveryHashBody();
static void NoliveryCountBody();
static void NoliveryCopyBody();
static void NoliveryBodyA();
static void NoliveryBodyB();
static void NoliveryBodyC();
static void NoliveryBodyD();
static void NoliveryBodyE();

// The 38,455-entry name table the probe's name helper walks. This module needs its
// own copy: source/stability.ixx owns the same recovered array for
// BadPlayerModelCrashFix, and that arm registers it with the body registry only
// when its own key is on, so FreezeCarFix cannot reach it. The data is recovered
// (source/model_names.inc) and the lookup below is a C++ rebuild of sub_10003F00,
// whose blocking instruction is the indexed absolute `push [0x100DE770 + esi*4]`
// in a compiler-generated /GS frame.
// The BPM arm in stability.ixx owns the single recovered copy of the name
// table; the spawn diagnostic reads that same array instead of compiling a
// second 865 KiB pointer table.
extern const char* const kBadPlayerModelNames[0x9637];

namespace
{
    // sub_10003F00's payload, rebuilt: first table name whose game hash matches, or
    // "**UNKNOWN**" on a miss (the original answers its own literal at 0x10128D20).
    const char* SpawnsModelName(uint32_t hash)
    {
        using HashFn = uint32_t(__cdecl*)(const char*);
        const auto hash_name = reinterpret_cast<HashFn>(zpatch_spawns_resolver);
        for (uint32_t index = 0; index < 0x9637u; ++index)
        {
            if (hash_name(kBadPlayerModelNames[index]) == hash)
                return kBadPlayerModelNames[index];
        }
        return "**UNKNOWN**";
    }

    // The original's native call block (sub_1005B290 / sub_1005B530 build it), and
    // the one builder both wrappers share: zero the 0xA0 bytes, point the return and
    // argument words at args, invoke the hash through the guarded entry, copy every
    // pending record out and answer args[0]. The blocked instruction is the
    // compiler's own frame; the block layout is the recovered one.
    struct SpawnsNativeBlock final
    {
        uint32_t p_return;    // +0x00
        uint32_t arg_count;   // +0x04
        uint32_t p_args;      // +0x08
        uint32_t out_count;   // +0x0C
        uint32_t out_ptrs[4]; // +0x10
        struct Record final { uint32_t value[3]; uint32_t padding; };
        Record records[4];    // +0x20
        uint32_t args[17];    // +0x60 (arg 0 is the return slot)
    };

    uint32_t SpawnsScriptCall(uint32_t hash, uint32_t nargs, const uint32_t* arguments)
    {
        SpawnsNativeBlock block{};
        const auto base = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&block.args[0]));
        block.p_return = base;
        block.p_args = base;
        block.arg_count = nargs;
        for (uint32_t index = 0; index < nargs && index < 17u; ++index)
            block.args[index] = arguments[index];
        Native::Invoke(hash, &block);
        while (block.out_count != 0u)
        {
            --block.out_count;
            auto* destination = reinterpret_cast<uint32_t*>(
                static_cast<uintptr_t>(block.out_ptrs[block.out_count]));
            const auto& source = block.records[block.out_count];
            destination[0] = source.value[0];
            destination[1] = source.value[1];
            destination[2] = source.value[2];
        }
        return block.args[0];
    }

    // sub_1005B290 and sub_1005B530, rebuilt: the "who spawned it" script call (the
    // original's hash is dword_101580C8, published by the loader's salt rule) and the
    // chat print (fixed hash 0x79023CFC). Both take their one argument in EDX and are
    // entered through the naked thunks below.
    const char* SpawnsWho(uint32_t id)
    {
        return reinterpret_cast<const char*>(
            static_cast<uintptr_t>(SpawnsScriptCall(Native::Salt(0x101580C8u), 1, &id)));
    }

    void SpawnsPrint(const char* text)
    {
        const auto argument = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(text));
        SpawnsScriptCall(0x79023CFCu, 1, &argument);
    }
}

// sub_1001EF30, recovered VA 0x1001EF30: HighFPSBikePhysicsFix's clamp, the helper
// the detour calls between pushad/popad. The original converts the float value to
// double, compares it against the 8-byte constant 0.006666666665 and replaces it
// with 0x3BDA740E when it is below. Written as the same comparison rather than as
// the SSE sequence: the value and the outcome are what the detour's contract is.
static void BikePhysicsClamp() noexcept
{
    // The float the original's 0x3BDA740E immediate denotes, stored when the value
    // is below the threshold; 0.006666667f is the shortest decimal that is that
    // float. The comparison's own 8-byte constant is 0.006666666665 (the qword at
    // 0x10152308).
    constexpr float kBikeSpeedThreshold = 0.006666667f;

    const float value = zpatch_bike_value;
    if (0.006666666665 > static_cast<double>(value))
        zpatch_bike_value = kBikeSpeedThreshold;
}

// sub_100064D0, recovered VA 0x100064D0: the two light indices the dual-headlight
// bodies push. The original's bodies call it inside pushad/popad, so the clobbering
// is harmless there; the words hold the same values from load time on.
static void DualHeadlightConstants() noexcept
{
    zpatch_dual_light_a = 1;
    zpatch_dual_light_b = 2;
}

namespace
{
    // sub_10003E50 (source/shared.ixx), called as the original's direct `call`:
    // this forwarder is what an inline-asm `call` can name (the resolver itself is a
    // naked body in another unit, and a module interface cannot export it).
    uint32_t __cdecl NoliveryResolve(const char* name)
    {
        return reinterpret_cast<uint32_t(__cdecl*)(const char*)>(Shared::ValueResolver())(name);
    }
}

// Vehicle fixes. Every arm anchors to its own recovered signature; most writes are
// in-place bytes, jump rewrites and NOP runs, and the four sites whose fix is a
// rewritten instruction sequence jump to a transcribed body instead.
//
// The signatures are forged from the game database (IDA, 1.0.8.0) at each arm's
// scan anchor and are unique in that image; the offsets are measured from the
// anchor, which is why they did not change when the strings did.
namespace
{
    // One recovered sub-gate: is the current build's id a bit of `mask`? The
    // catalogue's own mask is the arm's gate; a few recovered rows carry a narrower
    // one of their own, which is what this reads.
    bool CurrentBuildInMask(uint32_t mask)
    {
        return (mask & (1u << static_cast<uint32_t>(Loader::Current()))) != 0;
    }

    // CarDynamicShadowFix@noE's own gate: the recovered table gives the row the
    // mask 0x22 (build ids 1000 and 1040), the builds where the installer's pattern
    // E is absent. 1.0.0.4 has pattern E, so it keeps the E path and is not in the
    // mask even though pattern C matches there too.
    constexpr uint32_t kCarDynamicShadowNoEBuilds = 0x22u;

    // DualVehicleHeadlights' two recovered arm gates: the original's own table gives
    // the old body 0x62 (build ids 1, 5 and 6 -- 1.0.0.0, 1.0.4.0, 1.0.0.4) and the
    // new body 0x4BF80 (id 7 and every later build). The original's handler selects
    // between the two by the build id it classified (`cmp [build], 1` .. `cmp
    // [build], 6`, 0x100067F3), which is the same split. The union is every
    // supported build.
    constexpr uint32_t kDualHeadlightsOldBuilds = 0x62u;
    constexpr uint32_t kDualHeadlightsNewBuilds = 0x4BF80u;

    // A captured pattern operand is a 32-bit VA of the image *as loaded* (the same
    // helper as misc.ixx's): the loader has already applied the image's relocations,
    // so the dword is a live address and the cast is the whole conversion. The game
    // runs relocated, so subtracting the preferred base here would add the run's
    // whole relocation distance to the pointer; the offline dump relocates its copy
    // of the image too. Only the arms that write *through* such an operand need it.
    template <typename T>
    T* GamePointer(uint32_t value)
    {
        return reinterpret_cast<T*>(static_cast<uintptr_t>(value));
    }

    // A rel32 target inside the match: the displacement dword at `displacement`,
    // measured from the instruction's last byte at `next`.
    uint32_t Rel32Target(std::byte* match, std::ptrdiff_t displacement, std::ptrdiff_t next)
    {
        const auto displacementValue = *reinterpret_cast<const int32_t*>(match + displacement);
        return static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(match + next) + static_cast<uint32_t>(displacementValue));
    }

    // EpisodicVehicleSupport's own episode word: the game's unk_10158128, an int
    // holding 2, which every `cmp [episode], 2` below tests. The arm hands the game
    // this module's word instead, so the four 1.0.7.0/1.0.8.0 sites read a value
    // this module owns; the dump canonicalises the four published operands.
    uint32_t zpatch_episode_flag = 2;
    // The address of a body another module transcribed, by its recovered VA. The
    // shared IDE parser hook lives in source/misc.ixx, and a module interface cannot
    // export a naked function; the body registry is what makes it reachable (the same
    // route the BPM arm uses for the native layer's pieces).

    // The original loader preamble resolves these five entries unconditionally
    // (sub_100523C0, 0x10052EE6..0x10052F50), before any handler runs: the model-info
    // array (dword_1015AD78), the bone getter (dword_10159B88) and the two matrix
    // setters (dword_1015D63C, dword_10159CDC). Each scan's hit is exactly what the
    // preamble's own pattern takes -- the bone getter's signature has two hits and
    // the preamble takes the first (`cmovnz ecx, eax` with the scan's front element),
    // and a miss keeps the slot as it was, which for a first run is zero, so the
    // consumer faults on the null it reads just as the original's does.
    //
    // The two CRT entries are the ones the livery helpers call through their IAT
    // slots: `_stricmp` (the registry comparator) and `strcpy_s` (the 0x10-byte name
    // copy). byte_10159B85 is the original DllMain's CitizenIV.exe probe
    // (0x10055DFB..0x10055E17); it starts zero and is set once, here.
    void ResolveSteerHelpers()
    {
        auto pattern = find_pattern("85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07");
        if (!pattern.empty())
        {
            const auto value = *pattern.get_first<uint32_t>(-4);
            zpatch_steer_model_array = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(GamePointer<std::byte>(value)));
        }

        pattern = find_pattern("56 8B F1 8B 06 8B 90 A0 00 00 00 57 FF D2 85 C0 74 1C");
        if (!pattern.empty())
            zpatch_steer_bone = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)));

        pattern = find_pattern("0F 10 5E 14 F3 0F 10 66 18 F3 0F 10 4C 24 0C F3");
        if (!pattern.empty())
            zpatch_steer_matrix_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(-0x30)));

        pattern = find_pattern(
            "0F 10 16 F3 0F 10 5E 04 F3 0F 10 4C 24 0C F3 0F 10 66 08 F2 0F 5A C0");
        if (!pattern.empty())
            zpatch_steer_matrix_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(-0x30)));

        zpatch_steer_stricmp = reinterpret_cast<uintptr_t>(&_stricmp);
        // `strcpy_s` is an overload set in the CRT headers (the array form is a
        // template), so the slot takes the three-argument entry explicitly.
        zpatch_nolivery_strcpy_s = reinterpret_cast<uintptr_t>(
            static_cast<errno_t(__cdecl*)(char*, size_t, const char*)>(&strcpy_s));

        zpatch_garage_citizen = GetModuleHandleA("CitizenIV.exe") != nullptr ? 1 : 0;
    }

    // CollisionDeadlockFix's published walk test: the site's own call target (the
    // disp32 at match+91), read before the install overwrites the entry.
    void PublishCollisionDeadlockFix()
    {
        if (!Loader::Enabled("CollisionDeadlockFix"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        const auto displacement = *reinterpret_cast<const int32_t*>(match + 91);
        zpatch_collision_should_walk =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 95) + displacement);
    }

    // HighFPSBikePhysicsFix's resume: the site's own `call` at match+9, read before
    // the install replaces the first five bytes with the jump. The body re-runs those
    // five bytes' instructions and tail-jumps to the call, so the call and the rest of
    // the function run unchanged.
    void PublishHighFPSBikePhysicsFix()
    {
        if (!Loader::Enabled("HighFPSBikePhysicsFix"))
            return;

        auto pattern = find_pattern("D9 45 08 51 8B CE D9 1C 24 E8 ?? ?? ?? ?? 8A 86 90 14 00 00");
        if (pattern.empty())
            return;

        zpatch_bike_resume =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(9)));
    }

    // DualVehicleHeadlights' published operands, all read from the one match: the
    // light-state global the site pushes at match+207, the ped getter's call target
    // (the disp32 at match+140, next byte +144), the driver test's (+151/+155), the
    // draw call's (+331/+335) and the resume (`pop edi`, match+338). The site's own
    // instructions carry those operands, so the publish is a read, not a decode.
    void PublishDualVehicleHeadlights()
    {
        // The catalogue's key mask cannot be the gate here: see the install arm.
        if (Catalogue::Read("DualVehicleHeadlights") != 1)
            return;

        auto pattern = find_pattern("8B 17 8B 42 64 8B CF FF D0 8B 54");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_dual_bcc = *pattern.get_first<uint32_t>(207);
        zpatch_dual_bd0 = Rel32Target(match, 140, 144);
        zpatch_dual_be4 = Rel32Target(match, 151, 155);
        zpatch_dual_bd4 = Rel32Target(match, 331, 335);
        zpatch_dual_be0 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 338));
    }

    // FreezeCarFix / DebugPrintSpawns: the original's handler 0x10037C70 scans the
    // primary signature first and wraps every other scan in its `if (result)`, so a
    // miss on 1.0.0.0 / 1.0.4.0 / 1.0.0.4 installs nothing at all. The arm keeps
    // that shape: the DebugPrintSpawns flag and the probe's operands are read here,
    // before the install tier writes any site, and every resume is the target of the
    // `E8` the install is about to overwrite.
    void PublishFreezeCarFix()
    {
        if (!Loader::Enabled("FreezeCarFix"))
            return;

        // The original reads DebugPrintSpawns only inside this gate and only raises
        // the flag (`movzx ecx,[0x10159C15]; cmp esi,1; cmove ecx,1; mov [...],cl`),
        // so a value other than 1 leaves it at the BSS zero.
        zpatch_spawns_debug = Catalogue::Read("DebugPrintSpawns") == 1 ? 1 : 0;
        zpatch_spawns_getmodulehandlew = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(&GetModuleHandleW));
        zpatch_spawns_resolver = static_cast<uint32_t>(Shared::ValueResolver());

        auto primary = find_pattern("32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0");
        if (primary.empty())
            return;

        auto* primaryMatch = primary.get_first<std::byte>(0);
        zpatch_spawns_orig_a = Rel32Target(primaryMatch, 8, 12);

        // dword_1015AD78, the ped-model object array, is the operand four bytes
        // before the original's own anchor (the same recovered read the
        // BadPlayerModelCrashFix arm makes). It is a live VA: the loader has already
        // relocated the image, and the dump relocates its copy too.
        auto pedArray = find_pattern("85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07");
        if (!pedArray.empty())
            zpatch_spawns_ped_array = *pedArray.get_first<uint32_t>(-4);

        auto guard = find_pattern(
            "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B");
        if (!guard.empty())
            zpatch_spawns_orig_b = Rel32Target(guard.get_first<std::byte>(0), 12, 16);

        // The probe's ten jump-table arms: each site's own `call` target is the
        // resume the thunk tail-jumps to, in case order 0..9.
        auto dispatch = find_pattern("E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00");
        if (!dispatch.empty())
        {
            auto* match = dispatch.get_first<std::byte>(0);
            zpatch_spawns_case0_resume = Rel32Target(match, 37, 41);
            zpatch_spawns_case1_resume = Rel32Target(match, 62, 66);
            zpatch_spawns_case2_resume = Rel32Target(match, 87, 91);
            zpatch_spawns_case3_resume = Rel32Target(match, 112, 116);
            zpatch_spawns_case4_resume = Rel32Target(match, 137, 141);
            zpatch_spawns_case5_resume = Rel32Target(match, 159, 163);
            zpatch_spawns_case6_resume = Rel32Target(match, 181, 185);
            zpatch_spawns_case7_resume = Rel32Target(match, 203, 207);
            zpatch_spawns_case8_resume = Rel32Target(match, 225, 229);
            zpatch_spawns_case9_resume = Rel32Target(match, 247, 251);
        }
    }

    // CarPartsShadowFix's published resumes: the site's own bytes -- the `jle` it
    // would have run (match+2) and the loop exit that `jle` jumps to (match+0x45).
    // The shared detour is installed under either key, so the publish uses the same
    // combined gate.
    void PublishCarPartsShadowFix()
    {
        if (!Loader::Enabled("CarPartsShadowFix") && !Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("85 DB 7E 41 55 EB 0A");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        zpatch_carparts_resume_if = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 2));
        zpatch_carparts_resume_else =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 0x45));
    }

    // CarDynamicShadowFix's published call target and resume, from whichever body
    // variant the build carries: pattern A's `call` sits at match+6, so its resume is
    // match+0xB and the displacement is at match+7; pattern B's `call` sits at
    // match+8, so its resume is match+0xD and the displacement is at match+9.
    void PublishCarDynamicShadowFix()
    {
        if (!Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("56 6A 02 53 53 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 11));
            zpatch_carparts_shadow_fn = resume + *reinterpret_cast<const int32_t*>(match + 7);
            zpatch_carparts_resume_2 = resume;
            return;
        }

        pattern = find_pattern("56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 13));
            zpatch_carparts_shadow_fn = resume + *reinterpret_cast<const int32_t*>(match + 9);
            zpatch_carparts_resume_2 = resume;
        }
    }

    // ForceCarHeadlightShadows' resume: the E9 replaces the shadow function's entry
    // (game 0x8F7590), and the shim replays those twelve prologue bytes itself, so
    // the continuation is the instruction after them (match+12, the `mov eax,
    // [ebp+20h]`). Read before the site is written, as the original's handler does.
    void PublishForceCarHeadlightShadows()
    {
        if (!Loader::Enabled("ForceCarHeadlightShadows"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B");
        if (!pattern.empty())
            zpatch_fchs_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(12)));
    }

    // The three steer keys' shared slots, read before any of their sites is written:
    // the detour's resume (match+16) and the part-drive function the site's own code
    // calls, plus the shared IDE hook's resume (the parser anchor's -1, which the
    // original's sub_10004110 publishes as `result - 1`). Only one of the two site
    // variants exists per build, and each carries its own operand offsets (423/427 on
    // the 1.0.6.0-and-later site, 140/144 on the older one).
    void PublishSteerSites()
    {
        if (auto* match = CachedMatchFor("32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"))
        {
            zpatch_steer_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 16));
            zpatch_steer_part_call = Rel32Target(match, 423, 427);
        }

        if (auto* match = CachedMatchFor("7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"))
        {
            zpatch_steer_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 16));
            zpatch_steer_part_call = Rel32Target(match, 140, 144);
        }

        if (auto* ide = CachedMatchFor("0C 83 88 94 00 00 00 04"))
            zpatch_ide_hook_slot = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(ide - 1));
    }

    // MovingSteeringWheelSupport / ShakingEngineBlock / MovingPlaneParts: the
    // original's handler sets its own switch byte and calls the shared installer
    // (0x10054A7A, 0x10054B56, 0x10054AE8: `mov byte_1015AD4?, 1; call
    // sub_10005210`). The switch bytes are module words, so two of the keys on runs
    // both consumer blocks and one of them off leaves its block dormant -- exactly
    // the original's per-key switches.
    void PublishMovingSteeringWheelSupport()
    {
        if (!Loader::Enabled("MovingSteeringWheelSupport"))
            return;
        PublishSteerSites();
    }

    void PublishShakingEngineBlock()
    {
        if (!Loader::Enabled("ShakingEngineBlock"))
            return;
        PublishSteerSites();
    }

    void PublishMovingPlaneParts()
    {
        if (!Loader::Enabled("MovingPlaneParts"))
            return;
        PublishSteerSites();
    }

    // GarageModelFix's two resumes, read from the sites before they are overwritten:
    // the `fstp [esi+4]` at the first site's +6 and the `call` at the second site's
    // +5.
    void PublishGarageModelFix()
    {
        if (!Loader::Enabled("GarageModelFix"))
            return;

        auto pattern = find_pattern("8B 57 20 D9 42 34 D9 5E 04 8B 47 20");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_garage_resume_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match + 6));

            // The opt-in prologue form: match - 0x20 is the function entry,
            // whose first instruction is `movss xmm0, ds:<float>` (F3 0F 10 05).
            const auto* prologue = match - 0x20;
            if (prologue[0] == std::byte{ 0xF3 } && prologue[1] == std::byte{ 0x0F } &&
                prologue[2] == std::byte{ 0x10 } && prologue[3] == std::byte{ 0x05 })
            {
                const auto target = *reinterpret_cast<const uint32_t*>(prologue + 4);
                if (target != 0 && Game::Get().Contains(target))
                {
                    zpatch_garage_entry_resume_a = static_cast<uint32_t>(
                        reinterpret_cast<uintptr_t>(match - 0x18));
                    zpatch_garage_scale = *reinterpret_cast<const float*>(
                        static_cast<uintptr_t>(target));
                }
            }
        }

        pattern = find_pattern("0F B7 0E 50 51 E8");
        if (!pattern.empty())
            zpatch_garage_resume_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(5)));
    }

    // NoLiveryLimit's published slots, in the original handler's (sub_1003D420)
    // order and with its own either/or: the first site variant publishes the count
    // loop's lookup (the call operand six bytes before the anchor) and the D/E resume
    // (match+90); failing that, the older variant publishes its own pair (match-8 and
    // match+84). The A/B/C resume comes from the livery anchor's +13 either way.
    void PublishNoLiveryLimit()
    {
        if (!Loader::Enabled("NoLiveryLimit"))
            return;

        auto pattern = find_pattern("74 52 8D B5 3C 01 00 00 83 FB 04");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_nolivery_fn = Rel32Target(match, -6, -2);
            zpatch_nolivery_resume_d = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(match + 90));
        }
        else
        {
            pattern = find_pattern("8D AF 3C 01 00 00 8D 54 24 20");
            if (!pattern.empty())
            {
                auto* match = pattern.get_first<std::byte>(0);
                zpatch_nolivery_fn = Rel32Target(match, -8, -4);
                zpatch_nolivery_resume_d = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(match + 84));
            }
        }

        pattern = find_pattern("8B 8C 83 3C 01 00 00 0F BF 53 48");
        if (!pattern.empty())
            zpatch_nolivery_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(13)));
    }

    // CarDecalsFix: the decal selector's entry is replaced by its fixed twin, which
    // drops the episodic-build test between the mask compare and the member read.
    void InstallCarDecalsFix()
    {
        if (!Loader::Enabled("CarDecalsFix"))
            return;

        auto pattern = find_pattern("8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&CarDecalsSelectorShim), 0x1001B2E0u);
        injector::MakeJMP(pattern.get_first(0), reinterpret_cast<void*>(&CarDecalsSelectorShim), true);
    }

    // CollisionDeadlockFix: the node walk is replaced by its capped twin.
    void InstallCollisionDeadlockFix()
    {
        if (!Loader::Enabled("CollisionDeadlockFix"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        Bodies::Register(reinterpret_cast<uintptr_t>(&CollisionDeadlockShim), 0x10005D80u);
        injector::MakeJMP(match, reinterpret_cast<void*>(&CollisionDeadlockShim), true);
    }

    // HighFPSBikePhysicsFix: the bike's per-frame speed value passes through the
    // clamp before the physics step consumes it.
    void InstallHighFPSBikePhysicsFix()
    {
        if (!Loader::Enabled("HighFPSBikePhysicsFix"))
            return;

        auto pattern = find_pattern("D9 45 08 51 8B CE D9 1C 24 E8 ?? ?? ?? ?? 8A 86 90 14 00 00");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&BikePhysicsDetour), 0x1001EF60u);
        injector::MakeJMP(pattern.get_first<std::byte>(0), reinterpret_cast<void*>(&BikePhysicsDetour), true);
    }

    // DualVehicleHeadlights: both recovered arms jump to their own body at the same
    // site, and the original's handler picks the body from the build id it classified
    // (`cmp [build], 1` .. `cmp [build], 6` at 0x100067F3): ids 1..6 get the old body,
    // every later build the new one. That is the pair of recovered masks, so each
    // build runs exactly one arm; the reference tree ran both unconditionally and the
    // later write won, which lands on the same bytes everywhere the masks are
    // disjoint (they are).
    //
    // The gate is the catalogue's INI read alone. This key's catalogue mask (0x20) is
    // the feature document's build list, not the original's: the original's own two
    // arm masks cover every supported build, so `Loader::Enabled` would never install
    // this arm anywhere but 1.0.4.0.
    void InstallDualVehicleHeadlights()
    {
        if (Catalogue::Read("DualVehicleHeadlights") != 1)
            return;

        auto pattern = find_pattern("8B 17 8B 42 64 8B CF FF D0 8B 54");
        if (pattern.empty())
            return;

        if (CurrentBuildInMask(kDualHeadlightsOldBuilds))
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&DualHeadlightsOldBody), 0x10006630u);
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&DualHeadlightsOldBody), true);
        }

        if (CurrentBuildInMask(kDualHeadlightsNewBuilds))
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&DualHeadlightsNewBody), 0x100064F0u);
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&DualHeadlightsNewBody), true);
        }
    }

    void InstallFreezeCarFix()
    {
        if (!Loader::Enabled("FreezeCarFix"))
            return;

        auto primary = find_pattern("32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0");
        if (primary.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsProbeBody), 0x10037510u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsGuardBody), 0x100372F0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x24Body), 0x10037330u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x6FBody), 0x10037390u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xE0Body), 0x100373C0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x3DBody), 0x100373F0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x56Body), 0x10037420u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x88Body), 0x10037450u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0x9EBody), 0x10037480u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xF6Body), 0x100374B0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xB4Body), 0x100374E0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpawnsCase0xCABody), 0x10037360u);

        injector::MakeCALL(primary.get_first<std::byte>(7),
            reinterpret_cast<void*>(&SpawnsProbeBody), true);

        auto guard = find_pattern(
            "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B");
        if (!guard.empty())
        {
            injector::MakeCALL(guard.get_first<std::byte>(11),
                reinterpret_cast<void*>(&SpawnsGuardBody), true);
        }

        struct SpawnCase final
        {
            std::ptrdiff_t dispatchOffset;
            void (*body)();
        };
        constexpr SpawnCase kSpawnCases[] = {
            { 0x24, &SpawnsCase0x24Body },
            { 0x3D, &SpawnsCase0x3DBody },
            { 0x56, &SpawnsCase0x56Body },
            { 0x6F, &SpawnsCase0x6FBody },
            { 0x88, &SpawnsCase0x88Body },
            { 0x9E, &SpawnsCase0x9EBody },
            { 0xB4, &SpawnsCase0xB4Body },
            { 0xCA, &SpawnsCase0xCABody },
            { 0xE0, &SpawnsCase0xE0Body },
            { 0xF6, &SpawnsCase0xF6Body },
        };

        auto dispatch = find_pattern("E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00");
        if (!dispatch.empty())
        {
            auto* match = dispatch.get_first<std::byte>(0);
            for (const SpawnCase& spawn : kSpawnCases)
            {
                injector::MakeCALL(match + spawn.dispatchOffset,
                    reinterpret_cast<void*>(spawn.body), true);
            }
        }

        auto probeA = find_pattern("88 87 40 01 00 00 E8");
        if (!probeA.empty())
        {
            injector::MakeCALL(probeA.get_first<std::byte>(6),
                reinterpret_cast<void*>(&SpawnsProbeBody), true);
        }

        auto probeB = find_pattern("88 5C 24 1F 8B 54 24 28 52 E8");
        if (!probeB.empty())
        {
            injector::MakeCALL(probeB.get_first<std::byte>(9),
                reinterpret_cast<void*>(&SpawnsProbeBody), true);
            injector::MakeCALL(probeB.get_first<std::byte>(254),
                reinterpret_cast<void*>(&SpawnsProbeBody), true);
        }

        auto probeC = find_pattern("38 98 8C 00 00 00 75 2D 57 E8");
        if (!probeC.empty())
        {
            injector::MakeCALL(probeC.get_first<std::byte>(9),
                reinterpret_cast<void*>(&SpawnsProbeBody), true);
        }

        // The second row of the original's own table: a shorter prefix of the
        // primary, installed at the same +7 site, so the write is the duplicate the
        // original makes (17 rows, 16 distinct sites).
        auto shortPrimary = find_pattern("32 DB 8B 54 24 24 52 E8");
        if (!shortPrimary.empty())
        {
            injector::MakeCALL(shortPrimary.get_first<std::byte>(7),
                reinterpret_cast<void*>(&SpawnsProbeBody), true);
        }
    }

    // CarPartsShadowFix shares the detour with CarDynamicShadowFix: the original
    // gates the rewrite on either key and only the standalone forcing writes below
    // are CarPartsShadowFix's own.
    void InstallCarPartsShadowFix()
    {
        if (!Loader::Enabled("CarPartsShadowFix") && !Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("85 DB 7E 41 55 EB 0A");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);

        Bodies::Register(reinterpret_cast<uintptr_t>(&CarPartsShadowShim), 0x10006880u);
        injector::MakeJMP(match - 6, reinterpret_cast<void*>(&CarPartsShadowShim), true);

        if (!Loader::Enabled("CarPartsShadowFix"))
            return;

        // The standalone arm forces the shadow path on its own: the `jnz` that would
        // skip it becomes a short jump. The recovered standalone handler scans two
        // variants: the `83 F8 04` site (1.0.6.0 and later) and the `83 FA 04 75 0C`
        // site (the older builds). Each scan is a clean skip on the builds that do
        // not carry it.
        pattern = find_pattern("83 F8 04 C6 44 24 0E 00 75 0F");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(8), Bytes::ShortJmp, true);

        pattern = find_pattern("83 FA 04 75 0C");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(3), Bytes::ShortJmp, true);
    }

    // CarDynamicShadowFix: the shadow call site is detached so the shadow is drawn
    // twice with the remaining arguments. Two recovered body variants exist: pattern
    // A (1.0.6.0 and later) and pattern B (the older builds); the key's own build
    // mask covers both, and the signatures are the gate. The same installer's C and
    // E branches are the old builds' two extra writes: a range check rewritten as an
    // unconditional jump, and the six-byte NOP fill where pattern E is absent.
    void InstallCarDynamicShadowFix()
    {
        if (!Loader::Enabled("CarDynamicShadowFix"))
            return;

        auto pattern = find_pattern("56 6A 02 53 53 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            Bodies::Register(reinterpret_cast<uintptr_t>(&CarDynamicShadowShim), 0x100068A0u);
            injector::MakeJMP(match, reinterpret_cast<void*>(&CarDynamicShadowShim), true);
        }

        // Pattern B is the old-build variant of the same call site.
        pattern = find_pattern("56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            Bodies::Register(reinterpret_cast<uintptr_t>(&CarDynamicShadowShimB), 0x100068D0u);
            injector::MakeJMP(match, reinterpret_cast<void*>(&CarDynamicShadowShimB), true);
        }

        // Pattern C: the double range check (`>= 0x14` / `>= 6`) is skipped by
        // turning the second test's `jnz` into a jump over it. The row exists only on
        // the older builds, which is also where the @noE fill below belongs.
        pattern = find_pattern("83 F8 14 7D 05 83 F8 06 7D 14");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeJMP(match + 3, match + 0x1E, true);

            // CarDynamicShadowFix@noE: with pattern E absent (the recovered gate is
            // build ids 1000 and 1040) the installer NOP-fills six bytes after the C
            // anchor. 1.0.0.4 has pattern E, so it is not in the mask.
            if (CurrentBuildInMask(kCarDynamicShadowNoEBuilds))
                injector::MakeNOP(match + 62, 6, true);
        }

        // The shadow's record size is forced to 0x100 from 0x104, and the branch that
        // would skip the shadow becomes unconditional.
        pattern = find_pattern("50 68 04 01 00 00 6A 02 6A 00 E8");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(2), Bytes::Zero, true);

        pattern = find_pattern("83 E0 0F 83 F8 03 75 17 F6 86 6C 02 00 00 04");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(6), Bytes::ShortJmp, true);

        // With ForceCarHeadlightShadows off the original also re-encodes the `jz`
        // that skips the shadow as a five-byte `jmp` to the same target, and forces
        // the pushed shadow size from 0x504 to 0x100.
        if (Loader::Enabled("ForceCarHeadlightShadows"))
            return;

        pattern = find_pattern("0F 84 9E 00 00 00 80 7D 28");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto displacement = *reinterpret_cast<const int32_t*>(match + 2);
            injector::MakeJMP(match, match + 6 + displacement, true);
        }

        pattern = find_pattern("51 68 04 05 00 00 6A 02 6A 00 E8");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first(2), 0x100u, true);
    }

    // ForceCarHeadlightShadows: the shadow function hooks to the shim, the three
    // branches that decide whether the headlight cone draws its shadow are forced so
    // the shadow always runs, and the offset float is retargeted at this module's
    // word instead of the game's 0.0f at 0xD95B68. The writes, in the original
    // handler's order:
    //   * `EB` at the anchor's +6 (the `jne +79h` of `cmp al,1` / `test bl,bl`),
    //     `E9` at +95 to match+476 (the second `jne +177h`, one byte shorter than the
    //     six-byte `0F 85` it replaces, which is why the write starts on the opcode
    //     and its sixth byte becomes the unreachable remainder), `EB` at +379 (the
    //     `jne +5Fh` on the same target) and the `movss` operand at +211;
    //   * the E9 at the shadow function's entry;
    //   * the handler tail's restore of the two shadow-buffer pushes (0x504 / 0x104)
    //     and of the `je +9Eh` at 0x8F719F. On a pristine image every one of those is
    //     a no-op; they are the original's `sub_1000E870` writes, which undo exactly
    //     the CarDynamicShadowFix rewrites that arm skips while this key is on, so
    //     the two keys interact the way they do in the original.
    void InstallForceCarHeadlightShadows()
    {
        if (!Loader::Enabled("ForceCarHeadlightShadows"))
            return;

        auto pattern = find_pattern("3C 01 75 50 84 DB 75 79");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::WriteMemory<std::byte>(match + 6, Bytes::ShortJmp, true);
            injector::MakeJMP(match + 95, match + 476, true);
            injector::WriteMemory<std::byte>(match + 379, Bytes::ShortJmp, true);

            // The `movss xmm2, [0xD95B68]` operand becomes this module's float. The
            // original writes its own image's dword_101580C0 here (0x101580C0); the
            // stored baseline records the reference dump's 0xD0000000 placeholder for
            // the word, so the slot is registered for the dump's canonicalisation.
            injector::WriteMemory<uint32_t>(match + 211,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_fchs_offset)), true);
            Bodies::RegisterPublishedImage(reinterpret_cast<uintptr_t>(match + 211));
        }

        pattern = find_pattern("55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&FchsShimBody), true);

        // `push 0x504` (the first shadow buffer) and `push 0x104` (the second) are
        // written back as dwords; both immediates are unchanged on a pristine image.
        pattern = find_pattern("8D 4C 24 50 51 68 04 05 00 00");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(6), 0x00000504u, true);

        pattern = find_pattern("8D 44 24 50 50 68 04 01 00 00");
        if (!pattern.empty())
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(6), 0x00000104u, true);

        // `0F 84 9E 00 00 00` (the `je` that skips the shadow) is rewritten with its
        // own six bytes, the last one left as it was.
        pattern = find_pattern("F3 0F 11 44 24 1C 0F 84 9E 00 00 00");
        if (!pattern.empty())
        {
            auto* site = pattern.get_first<std::byte>(6);
            injector::WriteMemory<std::byte>(site, std::byte{ 0x0F }, true);
            injector::WriteMemory<std::byte>(site + 1, std::byte{ 0x84 }, true);
            injector::WriteMemory<uint16_t>(site + 2, 0x009Eu, true);
            injector::WriteMemory<std::byte>(site + 4, Bytes::Zero, true);
        }
    }

    // InfiniteCarBottomDistance: the `ja` of the bottom-distance clamp becomes an
    // unconditional jump over it (`EB 2A`, written as its two bytes -- there is no
    // short-jump API).
    void InstallInfiniteCarBottomDistance()
    {
        if (!Loader::Enabled("InfiniteCarBottomDistance"))
            return;

        auto pattern = find_pattern("0F 2F D0 76 25 0F 28 C2");
        if (pattern.empty())
            return;

        auto* site = pattern.get_first<std::byte>(3);
        injector::WriteMemory<std::byte>(site, Bytes::ShortJmp, true);
        injector::WriteMemory<std::byte>(site + 1, Bytes::JumpOverClamp, true);
    }

    // ReversingLightFix: the slot displacement of `mov eax,[edx+64h]` becomes 0x60,
    // so the reversing-light path reads the neighbouring member.
    void InstallReversingLightFix()
    {
        if (!Loader::Enabled("ReversingLightFix"))
            return;

        auto pattern = find_pattern("11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(8), Bytes::Slot60, true);
    }

    // BikeFeetFix: the 12-byte rider-foot block is deleted, so the chopper foot
    // position is left to the game's own (correct) path.
    void InstallBikeFeetFix()
    {
        if (!Loader::Enabled("BikeFeetFix"))
            return;

        auto pattern = find_pattern("39 82 54 13 00 00 0F 85 94 04 00 00");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(12), 12, true);
    }

    // BikePhoneAnimsFix: the six-byte phone-animation branch is deleted.
    void InstallBikePhoneAnimsFix()
    {
        if (!Loader::Enabled("BikePhoneAnimsFix"))
            return;

        auto pattern = find_pattern("80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(33), 6, true);
    }

    // PipeBombDropIconSupport: the nine-byte check before the anchor is deleted, so
    // the pipe-bomb drop icon is accepted on the episodic builds.
    void InstallPipeBombDropIconSupport()
    {
        if (!Loader::Enabled("PipeBombDropIconSupport"))
            return;

        auto pattern = find_pattern("83 7D 18 1C 75 14");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(-9), 9, true);
    }

    // EpisodicVehicleSupport: the episode test at each site is deleted, so every
    // vehicle check that is gated on "not episodic" runs on the episodic builds.
    //
    // Two parts, because the original's handler has two:
    //   * 1.0.7.0 and 1.0.8.0 each retarget the absolute operand of two
    //     `cmp dword ptr [episode], 2` sites (the first also a `jnz`, the second a
    //     `jne rel32`) at this module's episode word. The original's recovered table
    //     does that as four *RVA installs* -- a rebased pointer store at a fixed RVA
    //     (build 8: 0x21500 and 0x7D91AE; build 9: 0x95040 and 0x684ABE), not a
    //     pattern hit. The store value proves it: it is this module's own pointer,
    //     which a signature could not name. Spelling the game's absolute operand out
    //     in a signature would also bake in the preferred-base VA, which the loader
    //     rewrites on every run (the executable carries DYNAMIC_BASE), so such an
    //     arm could never match the live image and would never fire in the game;
    //   * every gated build then gets the 23 signature-driven NOP rows, in the
    //     original's row order. The first two rows write every hit of a multi-hit
    //     scan (two each on this image) and two rows carry a second window.
    void InstallEpisodicVehicleSupport()
    {
        if (!Loader::Enabled("EpisodicVehicleSupport"))
            return;

        const auto flag = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_episode_flag));
        struct FlagStore final
        {
            Build build;
            uint32_t rva;
        };
        static constexpr FlagStore kEpisodeFlagStores[] = {
            { Build::v1070, 0x021500u },
            { Build::v1070, 0x7D91AEu },
            { Build::v1080, 0x095040u },
            { Build::v1080, 0x684ABEu },
        };
        for (const auto& store : kEpisodeFlagStores)
        {
            if (Loader::Current() != store.build)
                continue;

            auto* const site =
                reinterpret_cast<std::byte*>(Game::Get().ImageBase() + store.rva);
            if (!Failsafe::Site("EpisodicVehicleSupport episode word",
                    reinterpret_cast<uintptr_t>(site), sizeof(uint32_t)))
                continue;
            injector::WriteMemory<uint32_t>(site, flag, true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(site));
        }

        // Row 1: the `jl` of a loop that only runs on the episodic builds. Two
        // sites, both rewritten (the original writes hits[0] and hits[1]).
        auto pattern = find_pattern<2>("7C 18 0F BF 4E 2E");
        if (ExpectMatchCount(pattern, 2, "EpisodicVehicleSupport component window"))
        {
            for (size_t hit = 0; hit < pattern.size(); ++hit)
                injector::MakeNOP(pattern.get(hit).get<std::byte>(-6), 8, true);
        }

        // Row 2: the same `cmp [episode], 2` shape twice in two functions.
        pattern = find_pattern<2>("83 3D ?? ?? ?? ?? 02 75 17 F3 0F 10 44 24 08");
        if (ExpectMatchCount(pattern, 2, "EpisodicVehicleSupport episode test"))
        {
            for (size_t hit = 0; hit < pattern.size(); ++hit)
                injector::MakeNOP(pattern.get(hit).get<std::byte>(0), 9, true);
        }

        // Rows 3-20: one window each.
        pattern = find_pattern("39 35 ?? ?? ?? ?? 75 0E 80 3D");
        if (!pattern.empty())
            injector::MakeNOP(pattern.get_first(6), 2, true);

        struct EpisodeWindow
        {
            const char* signature;
            int offset;
            uint8_t length;
            int secondOffset;
            uint8_t secondLength;
        };
        static constexpr EpisodeWindow kEpisodeWindows[] = {
            { "74 17 83 3D ?? ?? ?? ?? 02 75 0E F3 0F 10 05", 2, 9, 0, 0 },
            { "7C 22 83 F8 28 75 1D", 0, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 0F 8C 0E 05 00 00", 0, 13, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 08 3B 05", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 17 F3 0F 10 44 24 0C", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 1F 8B 44 24 04", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 21 8B 96 20 08 00 00", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 24 F6 87 6C 02 00 00 04", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 35 0F BF 46 2E", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 75 38 8B 16", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 11 8B 4E", 7, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 1F 8B", 7, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 26 8B 85 40 0B 00 00", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 35 0F BF 43 2E", 0, 9, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 43", 7, 2, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 7C 4A 0F BF 46 2E", 0, 13, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 89 44 24 38", 11, 6, 0, 0 },
            { "83 3D ?? ?? ?? ?? 02 8A 0D", 0, 7, 17, 2 },
            { "83 3D ?? ?? ?? ?? 02 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 44 24 20 75 4C", 21, 2, 0, 0 },
            { "8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18", -15, 7, 10, 2 },
        };
        for (const auto& row : kEpisodeWindows)
        {
            pattern = find_pattern(row.signature);
            if (pattern.empty())
                continue;
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeNOP(match + row.offset, row.length, true);
            if (row.secondLength != 0)
                injector::MakeNOP(match + row.secondOffset, row.secondLength, true);
        }
    }

    // TeleportProtection: the entry becomes `ret 8`. The original writes exactly three
    // bytes -- C2 08 over the opcode and 00 over the byte after the immediate -- and
    // injector::MakeRET is not equivalent: it stores the opcode through a 4-byte int,
    // which would also zero match+3.
    void InstallTeleportProtection()
    {
        if (!Loader::Enabled("TeleportProtection"))
            return;

        auto pattern = find_pattern("55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00");
        if (pattern.empty())
            return;

        auto* match = pattern.get_first<std::byte>(0);
        injector::WriteMemory<std::byte>(match, Bytes::RetImm16, true);
        injector::WriteMemory<std::byte>(match + 1, Bytes::RetImm16High, true);
        injector::WriteMemory<std::byte>(match + 2, Bytes::Zero, true);
    }

    // IncreaseVehicleStructLimit: the struct count the allocator is asked for becomes
    // 100 instead of the original 50: the signature's +6 byte is the immediate of the
    // `push 32` that follows the pushed structure-name address. The pattern is the
    // original's own (`68 ? ? ? ? 6A 32 8B C8`, recovered from the ASI's signature
    // table, handler 0x10023020): the pushed address is a relocated operand, so it
    // must stay a wildcard. Baking the recovered 1.0.8.0 VA in (`68 34 EE DA 00`)
    // makes the arm miss the moment the loader relocates the image -- which it does
    // on every run, because the executable carries DYNAMIC_BASE -- and would also
    // miss in the offline dump once that copy is relocated.
    void InstallIncreaseVehicleStructLimit()
    {
        if (!Loader::Enabled("IncreaseVehicleStructLimit"))
            return;

        auto pattern = find_pattern("68 ? ? ? ? 6A 32 8B C8");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(6), std::byte{ 100 }, true);
    }

    // IncreaseVehicleModelLimit: the vehicle-model cap the loop bound is compared
    // against becomes 1024. The site is the `mov ecx, 0xF3E8E8` at match+77: the
    // arm reads that operand, which is the address of the model-limit object the
    // allocator is constructed with, and writes the new cap through it. The write
    // is a game-data dword, not a published pointer, so there is no slot to
    // register with the dump.
    void InstallIncreaseVehicleModelLimit()
    {
        if (!Loader::Enabled("IncreaseVehicleModelLimit"))
            return;

        auto pattern = find_pattern("83 C7 01 81 FF 18 79 00 00 7C D2");
        if (pattern.empty())
            return;

        constexpr uint32_t kVehicleModelLimit = 0x400u;
        injector::WriteMemory<uint32_t>(
            GamePointer<uint32_t>(*pattern.get_first<uint32_t>(77)), kVehicleModelLimit, true);
    }

    // The steer keys' shared write set, exactly the original's sub_10005210 plus its
    // sub_10004110: the vtable-call site the build carries becomes a jump to that
    // build's detour body, and the shared IDE parser hook is installed so the
    // wheelangle table the consumer reads is filled. The three keys call this from
    // their own arms after setting their switch byte, so enabling two of them writes
    // the same bytes twice, as the original's two handler calls do.
    void InstallSteerSites()
    {
        // The consumer calls the game's model lookup and four preamble-resolved
        // entries. A build whose scans missed one would fault at the first frame
        // that reaches the detour; keep the game's own (absent) motion instead.
        if (Failsafe::Missing("moving-parts model lookup", zpatch_model_lookup == 0) ||
            Failsafe::Missing("moving-parts bone lookup", zpatch_steer_bone == 0) ||
            Failsafe::Missing("moving-parts setter A", zpatch_steer_matrix_a == 0) ||
            Failsafe::Missing("moving-parts setter B", zpatch_steer_matrix_b == 0) ||
            Failsafe::Missing("moving-parts component call", zpatch_steer_part_call == 0) ||
            Failsafe::Missing("moving-parts model table", zpatch_steer_model_array == 0))
            return;

        if (auto* match = CachedMatchFor("32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"))
            injector::MakeJMP(match + 2,
                reinterpret_cast<void*>(&SteerDetourEaxBody), true);

        if (auto* match = CachedMatchFor("7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"))
            injector::MakeJMP(match + 2,
                reinterpret_cast<void*>(&SteerDetourEdxBody), true);

        if (auto* ide = CachedMatchFor("0C 83 88 94 00 00 00 04"))
            injector::MakeJMP(ide - 10,
                reinterpret_cast<void*>(Bodies::AddressFor(0x100040E0u)), true);
    }

    void InstallMovingSteeringWheelSupport()
    {
        if (!Loader::Enabled("MovingSteeringWheelSupport"))
            return;
        zpatch_steer_enabled = 1;
        InstallSteerSites();
    }

    void InstallShakingEngineBlock()
    {
        if (!Loader::Enabled("ShakingEngineBlock"))
            return;
        zpatch_engine_enabled = 1;
        InstallSteerSites();
    }

    void InstallMovingPlaneParts()
    {
        if (!Loader::Enabled("MovingPlaneParts"))
            return;
        zpatch_plane_enabled = 1;
        InstallSteerSites();
    }

    // GarageModelFix: both sites jump to their own body. The first carries the colour
    // copy, the second rewrites the model index the garage object hands on.
    void InstallGarageModelFix()
    {
        if (!Loader::Enabled("GarageModelFix"))
            return;

        // The colour body reads the model-info array and the lookup body calls the
        // game's model lookup; both sites tail-jump through their published resume.
        if (Failsafe::Missing("GarageModelFix model table",
                zpatch_steer_model_array == 0) ||
            Failsafe::Missing("GarageModelFix model lookup",
                zpatch_model_lookup == 0) ||
            Failsafe::Missing("GarageModelFix resume",
                zpatch_garage_entry_resume_a == 0 || zpatch_garage_resume_b == 0))
            return;

        auto pattern = find_pattern("8B 57 20 D9 42 34 D9 5E 04 8B 47 20");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            // The prologue hook: the body replays the store helper with the
            // prologue's own ECX/[esp+4], reloads the skipped xmm0 and resumes
            // after the 8-byte movss.
            injector::MakeJMP(match - 0x20,
                reinterpret_cast<void*>(&GarageEntryBodyA), true);
        }

        pattern = find_pattern("0F B7 0E 50 51 E8");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&GarageBodyB), true);
    }

    // NoLiveryLimit: the collector site (or the older variant of it), then the three
    // livery sites at the anchor, -199 and -237, each jumping to its own body. The
    // order is the original's: the collector first, then the anchor, then -199, then
    // -237.
    void InstallNoLiveryLimit()
    {
        if (!Loader::Enabled("NoLiveryLimit"))
            return;

        // The bodies call the published lookup and jump through both resumes. A
        // view whose anchor matched but whose call operand did not is exactly the
        // shape that faults at the first livery scan.
        if (Failsafe::Missing("NoLiveryLimit lookup", zpatch_nolivery_fn == 0) ||
            Failsafe::Missing("NoLiveryLimit resume",
                zpatch_nolivery_resume == 0 || zpatch_nolivery_resume_d == 0) ||
            Failsafe::Missing("NoLiveryLimit name copy", zpatch_nolivery_strcpy_s == 0))
            return;

        auto pattern = find_pattern("74 52 8D B5 3C 01 00 00 83 FB 04");
        if (!pattern.empty())
        {
            injector::MakeJMP(pattern.get_first<std::byte>(2),
                reinterpret_cast<void*>(&NoliveryBodyD), true);
        }
        else
        {
            pattern = find_pattern("8D AF 3C 01 00 00 8D 54 24 20");
            if (!pattern.empty())
                injector::MakeJMP(pattern.get_first<std::byte>(0),
                    reinterpret_cast<void*>(&NoliveryBodyE), true);
        }

        pattern = find_pattern("8B 8C 83 3C 01 00 00 0F BF 53 48");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeJMP(match, reinterpret_cast<void*>(&NoliveryBodyA), true);
            injector::MakeJMP(match - 199, reinterpret_cast<void*>(&NoliveryBodyC), true);
            injector::MakeJMP(match - 237, reinterpret_cast<void*>(&NoliveryBodyB), true);
        }
    }
}

// Registered like every other arm: the publish tier reads every site the install
// tier is about to overwrite, and the install tier owns the gate and the writes.
export void RegisterVehicles()
{
    // The original's loader preamble fills both per-model light tables before it
    // reads a single option (`sub_100523C0`'s first two blocks). This is the first
    // registration after Shared::Register() has made `sub_10003E50` callable, and
    // nothing before it reads an option or writes an image byte, so the tables are
    // complete -- and a malformed row throws -- exactly where the original's
    // preamble does it. Both sections are read here because the original reads them
    // in one place, [PopupLightAngles] first.
    Config::ReadModelTables(Config::Ini(),
        reinterpret_cast<Config::NameResolver>(Shared::ValueResolver()));

    // The original's loader preamble resolves the steer consumer's five game entries
    // and the two CRT slots before any handler runs, and its DllMain probes for
    // CitizenIV.exe at the same point; none of it writes an image byte.
    ResolveSteerHelpers();

    Loader::PublishEvent() += []
    {
        PublishCollisionDeadlockFix();
        PublishCarPartsShadowFix();
        PublishCarDynamicShadowFix();
        PublishForceCarHeadlightShadows();
        PublishHighFPSBikePhysicsFix();
        PublishDualVehicleHeadlights();
        PublishMovingSteeringWheelSupport();
        PublishShakingEngineBlock();
        PublishMovingPlaneParts();
        PublishGarageModelFix();
        PublishNoLiveryLimit();
        PublishFreezeCarFix();
    };
    Loader::InstallEvent() += []
    {
        InstallCarDecalsFix();
        InstallCollisionDeadlockFix();
        InstallCarPartsShadowFix();
        InstallCarDynamicShadowFix();
        // After the shadow arm: the original's ForceCarHeadlightShadows handler runs
        // after the CarDynamicShadowFix block, and its tail restores what that block
        // rewrites -- so with both keys on, the restore is what the game sees.
        InstallForceCarHeadlightShadows();
        InstallInfiniteCarBottomDistance();
        InstallReversingLightFix();
        InstallBikeFeetFix();
        InstallBikePhoneAnimsFix();
        InstallEpisodicVehicleSupport();
        InstallHighFPSBikePhysicsFix();
        InstallPipeBombDropIconSupport();
        InstallTeleportProtection();
        InstallDualVehicleHeadlights();
        InstallIncreaseVehicleModelLimit();
        InstallIncreaseVehicleStructLimit();
        InstallMovingSteeringWheelSupport();
        InstallShakingEngineBlock();
        InstallMovingPlaneParts();
        InstallGarageModelFix();
        InstallNoLiveryLimit();
        InstallFreezeCarFix();
    };

    // The bodies' recovered VAs, so the dump expresses each arm's branch in the
    // original's coordinates. The three steer keys all jump to the same two detour
    // bodies, so those are registered once here rather than in the shared installer.
    Bodies::Register(reinterpret_cast<uintptr_t>(&VehicleComponentBody), 0x10003F40u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&FchsShimBody), 0x1001ADB0u);
    // The checked C++ check takes the transcribed check's reference, so the
    // shim body's call stays canonical against the original.
    Bodies::Register(reinterpret_cast<uintptr_t>(&FchsCheckCpp), 0x1001AD20u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SteerDetourEaxBody), 0x100051B0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SteerDetourEdxBody), 0x100051E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GarageColourBody), 0x10046B90u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GarageLookupBody), 0x10046BD0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GarageBodyA), 0x10046C80u);
    // The prologue form shares the recovered body A VA: the offline dump
    // canonicalises either path's branch to the same reference.
    Bodies::Register(reinterpret_cast<uintptr_t>(&GarageEntryBodyA), 0x10046C80u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GarageBodyB), 0x10046CA0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NoliveryHashBody), 0x1003D220u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NoliveryCountBody), 0x1003D270u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NoliveryCopyBody), 0x1003D2F0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NoliveryBodyA), 0x1003D310u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NoliveryBodyB), 0x1003D340u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NoliveryBodyC), 0x1003D370u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NoliveryBodyD), 0x1003D3A0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&NoliveryBodyE), 0x1003D3E0u);

    // The steer consumer's two 512-slot wheelangle tables. The transcription reads
    // them through indexed absolute operands (`[esi*4 + zpatch_wheelangle_models]`),
    // which cannot name the original's VA, so each registration pairs this module's
    // copy with the address the original keeps it at and the length there: 512
    // dwords. The registration is mirrored by a `table` row in tests/bodies.tsv, the
    // offline projection the proof resolves. The arrays belong to
    // source/misc.ixx because the transcribed IDE parser hook is what fills them;
    // both are all-zero at load, as the original's are.
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_wheelangle_models),
        0x1015A538u, sizeof(zpatch_wheelangle_models));   // 512 * 4 = 2048
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_wheelangle_angles),
        0x10159D38u, sizeof(zpatch_wheelangle_angles));   // 512 * 4 = 2048

    // ForceCarHeadlightShadows' two [HeadlightConeOffsets] arrays: dword_1015ADB8 (the
    // hashes) and dword_1015B5C0 (the offsets). The hash registration is the original's
    // **extended** 2,052-byte range -- 512 words plus the one word the walk reads past
    // slot 511 (dword_1015B5B8) -- so the proof compares that read too. The word is not
    // a copy: it is CarDynamicShadowFix's published shadow-function slot, defined
    // immediately after the array in source/config.ixx so the layout matches the
    // original's .data. The offsets array is registered at its
    // own base, which the original places 2,056 bytes into the block.
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_headlight_cone_hashes),
        0x1015ADB8u, sizeof(zpatch_headlight_cone_hashes) + sizeof(uint32_t));  // 2052
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_headlight_cone_values),
        0x1015B5C0u, sizeof(zpatch_headlight_cone_values));   // 512 * 4 = 2048

    // Each stub's inline `call` is invisible to C++; naming the forwarders here keeps
    // their out-of-line definitions in the image.
    [[maybe_unused]] const uintptr_t helpers[] = {
        reinterpret_cast<uintptr_t>(&NoliveryFormat),
        reinterpret_cast<uintptr_t>(&NoliveryResolve),
        reinterpret_cast<uintptr_t>(&SteerConsumerBody),
        reinterpret_cast<uintptr_t>(&FchsCheckCpp),
    };
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// The probe's four helper entries. The original calls the compiler-generated
// helpers directly (sub_10003F00 / sub_1005B290 / sub_1005B530 / sub_10001280); a
// module interface cannot export a naked function, so the calls name these
// four-to-six-byte thunks, which reach the rebuilt helper (or the formatter body
// source/stability.ixx already proved) with the stack and registers untouched.
// The call instruction itself stays the original's `E8 rel32`.
static __declspec(naked) void SpawnsNameEntry()
{
    __asm
    {
        push ecx
        call SpawnsModelName
        add esp, 4
        ret
    }
}

static __declspec(naked) void SpawnsWhoEntry()
{
    __asm
    {
        push edx
        call SpawnsWho
        add esp, 4
        ret
    }
}

static __declspec(naked) void SpawnsPrintEntry()
{
    __asm
    {
        push edx
        call SpawnsPrint
        add esp, 4
        ret
    }
}

static __declspec(naked) void SpawnsFormatEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_format_entry]
    }
}

// sub_100372F0, recovered VA 0x100372F0 (16 instructions): the spawn guard. It
// keeps the original callee's EBP frame and `ret 0Ch` cleanup.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void SpawnsGuardBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        cmp DWORD PTR [zpatch_spawns_skip], 0
        jne spawns_guard_taken
        push DWORD PTR [ebp + 10h]
        mov eax, DWORD PTR [zpatch_spawns_orig_b]
        push DWORD PTR [ebp + 0Ch]
        push DWORD PTR [ebp + 8]
        call eax
        mov DWORD PTR [zpatch_spawns_skip], 0
        pop ebp
        ret 0Ch
    spawns_guard_taken:
        xor eax, eax
        mov DWORD PTR [zpatch_spawns_skip], eax
        pop ebp
        ret 0Ch
    }
}

// The ten factory-dispatch thunks, recovered VAs 0x10037330..0x100374E0 (9
// instructions each): the selector the probe dispatches on, the argument's sign
// byte, and the site's own resume. Cases 1 (+0x3D) and 2 (+0x56) both set selector
// 3 in the original bytes; kept 1:1.
#define ZP_SPAWNS_CASE(name, selector, resume)                 \
    static __declspec(naked) void name()                       \
    {                                                          \
        __asm push ebp                                         \
        __asm mov ebp, esp                                     \
        __asm mov ecx, DWORD PTR [ebp + 0Ch]                   \
        __asm movsx eax, cl                                    \
        __asm mov DWORD PTR [zpatch_spawns_kind], selector     \
        __asm mov DWORD PTR [zpatch_spawns_class], eax         \
        __asm mov DWORD PTR [ebp + 0Ch], ecx                   \
        __asm pop ebp                                          \
        __asm jmp DWORD PTR [resume]                           \
    }

ZP_SPAWNS_CASE(SpawnsCase0x24Body, 5, zpatch_spawns_case0_resume)

ZP_SPAWNS_CASE(SpawnsCase0x3DBody, 3, zpatch_spawns_case1_resume)

ZP_SPAWNS_CASE(SpawnsCase0x56Body, 3, zpatch_spawns_case2_resume)

ZP_SPAWNS_CASE(SpawnsCase0x6FBody, 0, zpatch_spawns_case3_resume)

ZP_SPAWNS_CASE(SpawnsCase0x88Body, 4, zpatch_spawns_case4_resume)

ZP_SPAWNS_CASE(SpawnsCase0x9EBody, 6, zpatch_spawns_case5_resume)

ZP_SPAWNS_CASE(SpawnsCase0xB4Body, 7, zpatch_spawns_case6_resume)

ZP_SPAWNS_CASE(SpawnsCase0xCABody, 1, zpatch_spawns_case7_resume)

ZP_SPAWNS_CASE(SpawnsCase0xE0Body, 2, zpatch_spawns_case8_resume)

ZP_SPAWNS_CASE(SpawnsCase0xF6Body, 8, zpatch_spawns_case9_resume)

#undef ZP_SPAWNS_CASE

// sub_10037510, recovered VA 0x10037510 (394 instructions, 0x10037510..0x10037ABC):
// the spawn probe, transcribed instruction for instruction from the original. It
// is reached through nine of the arm's detours; ECX at the vtable +0Ch call is the
// selector, exactly as the original leaves it.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void SpawnsProbeBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 4Ch
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 8], eax
        push esi
        mov esi, DWORD PTR [zpatch_spawns_ped_array]
        xor eax, eax
        push edi
        mov edi, DWORD PTR [ebp + 8]
        __emit 0x0F
        __emit 0x1F
        __emit 0x00
    spawns_walk:
        mov ecx, DWORD PTR [esi + eax * 4]
        test ecx, ecx
        je spawns_walk_next
        cmp DWORD PTR [ecx + 3Ch], edi
        je spawns_found
    spawns_walk_next:
        inc eax
        cmp eax, 7918h
        jl spawns_walk
        or eax, 0FFFFFFFFh
    spawns_dispatch:
        mov ecx, DWORD PTR [zpatch_spawns_kind]
        cwde
        mov DWORD PTR [zpatch_spawns_kind], 0FFFFFFFFh
        mov esi, DWORD PTR [esi + eax * 4]
        cmp eax, -1
        je spawns_original
        test esi, esi
        je spawns_original
        cmp esi, 0Ch
        je spawns_original
        cmp esi, 0CDCDCDCDh
        je spawns_original
        mov eax, DWORD PTR [esi]
        test eax, eax
        je spawns_original
        cmp eax, 0Ch
        je spawns_original
        cmp eax, 0CDCDCDCDh
        je spawns_original
        test ecx, ecx
        jne spawns_case_heli
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_noncar
        cmp DWORD PTR [esi + 6Ch], 0
        jne spawns_noncar
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_car
        jmp spawns_print_spawned
    spawns_found:
        movzx eax, ax
        jmp spawns_dispatch
    spawns_noncar:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_noncar
        jmp spawns_print_plain
    spawns_case_heli:
        cmp ecx, 1
        jne spawns_case_boat
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nonheli
        cmp DWORD PTR [esi + 6Ch], 4
        jne spawns_nonheli
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_heli
        jmp spawns_print_spawned
    spawns_nonheli:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nonheli
        jmp spawns_print_plain
    spawns_case_boat:
        cmp ecx, 2
        jne spawns_case_bike
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nonboat
        cmp DWORD PTR [esi + 6Ch], 2
        jne spawns_nonboat
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_boat
        jmp spawns_print_spawned
    spawns_nonboat:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nonboat
        jmp spawns_print_plain
    spawns_case_bike:
        cmp ecx, 6
        jne spawns_case_train
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nonbike
        cmp DWORD PTR [esi + 6Ch], 1
        jne spawns_nonbike
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_bike
        jmp spawns_print_spawned
    spawns_nonbike:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nonbike
        jmp spawns_print_plain
    spawns_case_train:
        cmp ecx, 7
        jne spawns_case_plane
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nontrain
        cmp DWORD PTR [esi + 6Ch], 3
        jne spawns_nontrain
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_train
        jmp spawns_print_spawned
    spawns_nontrain:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nontrain
        jmp spawns_print_plain
    spawns_case_plane:
        cmp ecx, 8
        jne spawns_case_ped
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nonplane
        cmp DWORD PTR [esi + 6Ch], 5
        jne spawns_nonplane
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_plane
        jmp spawns_print_spawned
    spawns_nonplane:
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nonplane
        jmp spawns_print_plain
    spawns_case_ped:
        cmp ecx, 3
        jne spawns_case_object
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 6
        je spawns_ped_ok
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_nonped
        jmp spawns_print_plain
    spawns_ped_ok:
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_ped
        jmp spawns_print_spawned
    spawns_case_object:
        cmp ecx, 4
        jne spawns_case_vehicleobject
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 5
        jne spawns_nonvehicleobject
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_vehicleobject
        jmp spawns_print_plain
    spawns_nonvehicleobject:
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_object
        jmp spawns_print_spawned
    spawns_case_vehicleobject:
        cmp ecx, 5
        jne spawns_original
        mov eax, DWORD PTR [eax + 0Ch]
        call eax
        cmp al, 6
        je spawns_generic
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_suppress
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_used_nonped
    spawns_print_plain:
        lea eax, [ebp - 4Ch]
        push 40h
        push eax
        call SpawnsFormatEntry
        add esp, 14h
        lea edx, [ebp - 4Ch]
        call SpawnsPrintEntry
    spawns_suppress:
        mov DWORD PTR [zpatch_spawns_skip], 1
        xor eax, eax
        pop edi
        pop esi
        mov ecx, DWORD PTR [ebp - 8]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    spawns_generic:
        cmp BYTE PTR [zpatch_spawns_debug], 0
        je spawns_original
        push OFFSET zpatch_spawns_gtac
        call DWORD PTR [zpatch_spawns_getmodulehandlew]
        test eax, eax
        je spawns_original
        mov ecx, edi
        call SpawnsNameEntry
        mov edx, DWORD PTR [zpatch_spawns_class]
        mov esi, eax
        call SpawnsWhoEntry
        push esi
        push eax
        push OFFSET zpatch_spawns_fmt_generic
    spawns_print_spawned:
        lea eax, [ebp - 4Ch]
        push 40h
        push eax
        call SpawnsFormatEntry
        add esp, 14h
        lea edx, [ebp - 4Ch]
        call SpawnsPrintEntry
    spawns_original:
        mov eax, DWORD PTR [zpatch_spawns_orig_a]
        push edi
        call eax
        mov ecx, DWORD PTR [ebp - 8]
        add esp, 4
        xor ecx, ebp
        pop edi
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// abi_collision_deadlock_walk, recovered VA 0x10005D80: the whole node-walk of
// sub_4E6250 with an iteration cap in it. The walk is the game's own, instruction
// for instruction -- the only additions are the counter word (zeroed at entry,
// incremented per node, compared against 0x2710) and the early exit when it
// trips. The walk test is called through the published pointer (dword_1015BDD4)
// instead of a direct call, because the call's target is the site's own operand.
static __declspec(naked) void CollisionDeadlockShim()
{
    __asm
    {
        mov DWORD PTR [zpatch_collision_iterations], 0
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        push ecx
        mov eax, DWORD PTR [ebp + 0xC]
        movzx ecx, WORD PTR [eax + 0x66]
        push ebx
        mov ebx, DWORD PTR [ebp + 8]
        mov edx, DWORD PTR [ebx + 0x70]
        push esi
        push edi
        movzx edi, WORD PTR [edx + ecx * 8]
        cmp di, 0FFFFh
        jz collision_walk_done
    collision_walk_next:
        mov ecx, DWORD PTR [ebx + 0x70]
        movzx eax, di
        movzx edx, WORD PTR [ecx + eax * 8]
        lea esi, [ecx + eax * 8]
        mov eax, DWORD PTR [esi + 4]
        and eax, 0FFFFFFF0h
        cmp dx, 0FFFFh
        jz collision_walk_no_neighbour
        movzx edx, dx
        mov ecx, DWORD PTR [ecx + edx * 8 + 4]
        and ecx, 0FFFFFFF0h
        prefetcht0 BYTE PTR [ecx]
    collision_walk_no_neighbour:
        mov ecx, DWORD PTR [esi + 4]
        and ecx, 3
        mov edx, 1
        shl edx, cl
        push edx
        mov edx, DWORD PTR [ebp + 0xC]
        call DWORD PTR [zpatch_collision_should_walk]
        test al, al
        jnz collision_walk_found
        movzx edi, WORD PTR [esi]
        add DWORD PTR [zpatch_collision_iterations], 1
        cmp DWORD PTR [zpatch_collision_iterations], 2710h
        jz collision_walk_done
        cmp di, 0FFFFh
        jnz collision_walk_next
    collision_walk_done:
        or ax, 0FFFFh
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 8
    collision_walk_found:
        mov ax, di
        pop edi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret 8
    }
}

// abi_carparts_shadow, recovered VA 0x10006880: the shared car-parts shadow body.
// The site's `movzx ebx,[edi+1Ch]` / `xor esi,esi` are re-run here (the jump
// overwrites them), with a null guard on the parts pointer in front, and the
// re-run `test ebx,ebx` is what the resumed `jle` reads. The nops are the
// original's own padding between those instructions, not filler added here.
static __declspec(naked) void CarPartsShadowShim()
{
    __asm
    {
        cmp edi, 0
        nop
        jz carparts_shadow_skip
        nop
        movzx ebx, WORD PTR [edi + 0x1C]
        nop
        xor esi, esi
        nop
        test ebx, ebx
        nop
        jmp DWORD PTR [zpatch_carparts_resume_if]
    carparts_shadow_skip:
        nop
        jmp DWORD PTR [zpatch_carparts_resume_else]
    }
}

// abi_carparts_shadow_2, recovered VA 0x100068A0: CarDynamicShadowFix's detour. It
// re-runs the shadow call the site overwrote and then repeats it with the
// remaining arguments, calling through the published target rather than a direct
// call, which is what the original's body does.
static __declspec(naked) void CarDynamicShadowShim()
{
    __asm
    {
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
        call DWORD PTR [zpatch_carparts_shadow_fn]
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
        call DWORD PTR [zpatch_carparts_shadow_fn]
        nop
        add esp, 0x14
        nop
        jmp DWORD PTR [zpatch_carparts_resume_2]
    }
}

// abi_carparts_shadow_2b, recovered VA 0x100068D0: CarDynamicShadowFix's second
// shadow-submission body, the old builds' variant of CarDynamicShadowShim. It makes
// the same double call through the published target and pops the same 20 argument
// bytes, but the first submission pushes 2/0/0 instead of 2/ebx/ebx.
static __declspec(naked) void CarDynamicShadowShimB()
{
    __asm
    {
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
        call DWORD PTR [zpatch_carparts_shadow_fn]
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
        call DWORD PTR [zpatch_carparts_shadow_fn]
        nop
        add esp, 0x14
        nop
        jmp DWORD PTR [zpatch_carparts_resume_2]
    }
}

// A checked rebuild of abi_fchs_check: the same table walk, with the model
// lookup and the object pointer guarded. A missing preamble or a null object
// leaves the 2.0f seed instead of faulting.
void FchsCheckCpp() noexcept
{
    zpatch_fchs_offset = 2.0f;
    const auto object = static_cast<uintptr_t>(zpatch_fchs_param);
    if (object == 0)
        return;
    const auto lookup = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
        zpatch_model_lookup);
    if (lookup == nullptr)
        return;

    const int16_t model = *reinterpret_cast<const int16_t*>(object + 0x2E);
    for (size_t index = 0; index < 512; ++index)
    {
        const uint32_t hash = zpatch_headlight_cone_hashes[index];
        if (hash == 0)
            return;
        int32_t resolved = -1;
        lookup(hash, &resolved);
        if (resolved == model)
        {
            zpatch_fchs_offset = zpatch_headlight_cone_values[index];
            return;
        }
    }
}

export void SetFchsStateForTest(uintptr_t object, uintptr_t lookup) noexcept
{
    zpatch_fchs_param = static_cast<uint32_t>(object);
    zpatch_model_lookup = lookup;
}

export void ClearFchsTableForTest() noexcept
{
    for (size_t index = 0; index < 512; ++index)
    {
        zpatch_headlight_cone_hashes[index] = 0;
        zpatch_headlight_cone_values[index] = 0.0f;
    }
}

export void SetFchsSlotForTest(size_t index, uint32_t hash, float offset) noexcept
{
    if (index >= 512)
        return;
    zpatch_headlight_cone_hashes[index] = hash;
    zpatch_headlight_cone_values[index] = offset;
}

export void RunFchsCheckForTest() noexcept
{
    FchsCheckCpp();
}

export float FchsOffsetForTest() noexcept
{
    return zpatch_fchs_offset;
}

// abi_fchs_check, recovered VA 0x1001AD20 (46 instructions, 139 bytes,
// 0x1001AD20..0x1001ADAA): ForceCarHeadlightShadows' per-model offset check,
// transcribed instruction for instruction.
//
// It seeds the offset float (dword_101580C0) with 2.0f, then walks the
// [HeadlightConeOffsets] hash table from slot 0: `mov eax,ds:[0x1015ADB8]` loads
// slot 0, and each iteration asks the game's model store for that hash's model index
// and compares it with the object's signed word at +0x2E (the object is read
// unguarded, as the original's `movsx eax,word [edi+2Eh]` is). On a match it
// publishes that slot's float; otherwise it loads the next slot with
// `mov eax,ds:[esi*4+0x1015ADBC]` -- the table's base **plus four**, because ESI
// counts the slot it just consumed -- and stops at the first zero word.
//
// The one-word-past read. With ESI at 511 that load is `ds:[0x1015B5B8]`, the word
// immediately after the 512-slot table in the original's `.data`. It is *not* dead
// space: it is CarDynamicShadowFix's published shadow-function pointer, written
// before this check can run by the ASI's own car-shadow code (see the block comment
// in source/config.ixx). Our layout reproduces it as the same slot, with the check's
// own resume word (0x1015B5BC) after it, so the walk reads the value the original's
// does rather than a placeholder -- which is what makes this transcription possible
// instead of a bounded walk. The two arrays are registered with
// their original VAs; the hash table's registration is the extended 2,052 bytes
// (512 words + the neighbour).
//
// The /GS frame is the original's (cookie slot, check call slot) and the
// `nop dword ptr [eax]` at 0x1001AD4D is emitted as bytes. No exception frame, no TLS
// static-init guard.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
// abi_fchs_shim, recovered VA 0x1001ADB0 (9 instructions, 0x1001ADB0..0x1001ADCE):
// the hook body the arm installs at the shadow function's entry (game 0x8F7590).
// It stores the object the function was called with, runs the check between
// pushad/popad, then replays the prologue the E9 replaced -- the resume must run
// with the frame those twelve bytes establish -- and tail-jumps to the published
// resume, never calls it.
static __declspec(naked) void FchsShimBody()
{
    __asm
    {
        mov DWORD PTR [zpatch_fchs_param], ecx
        __emit 0x60  // pushad
        call FchsCheckCpp
        __emit 0x61  // popad
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF0h
        sub esp, 0D4h
        jmp DWORD PTR [zpatch_fchs_resume]
    }
}

// abi_car_decals_fix, recovered VA 0x1001B2E0: the fixed twin of the game's decal
// selector (sub_AF7F50), written as a whole function because the fix removes the
// episodic-build test in the middle of it. Everything else is the original's.
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails.
static __declspec(naked) void CarDecalsSelectorShim()
{
    __asm
    {
        mov eax, DWORD PTR [esp + 8]
        mov ecx, DWORD PTR [eax + 0x28]
        and ecx, 0x3C0
        cmp ecx, 0x80
        jnz car_decals_other
        mov eax, DWORD PTR [esp + 4]
        cmp DWORD PTR [eax + 0x38], 0
        jz car_decals_short
        mov eax, DWORD PTR [eax + 0x34]
        ret 8
    car_decals_other:
        mov eax, DWORD PTR [esp + 4]
    car_decals_short:
        mov eax, DWORD PTR [eax + 0x30]
        ret 8
    }
}

// sub_1001EF60, recovered VA 0x1001EF60: the bike-physics detour. It saves the
// incoming bike value, clamps it through the helper, writes the clamped value back
// into the caller's argument slot, leaves EAX as the caller had it, then re-runs the
// site's first four instructions (`fld [ebp+8]; push ecx; mov ecx,esi; fstp [esp]`)
// and tail-jumps to the site's own `call` (match+9), so the call and the rest of the
// function run unchanged.
//
// `pushad`/`popad` are emitted as bytes because the inline assembler renders those
// mnemonics with a 0x66 operand-size prefix, which the original does not have.
static __declspec(naked) void BikePhysicsDetour()
{
    __asm
    {
        fld DWORD PTR [ebp + 8]
        fstp DWORD PTR [zpatch_bike_value]
        __emit 0x60                       // pushad
        call BikePhysicsClamp
        __emit 0x61                       // popad
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

// sub_100064F0, recovered VA 0x100064F0: DualVehicleHeadlights' new-build body. It
// saves every GPR (and ESP) into the module words, draws the two headlights through
// the site's own calls -- the game's light-state global, the ped getter, the driver
// test and the light-draw call the site's instructions name -- restoring the caller's
// registers between the draws, then tail-jumps to the site's `pop edi` (match+338).
// The stack offsets are the recovered ones: this variant is the 1.0.6.0-and-later
// light call's frame, and the old-build body below differs only in them.
static __declspec(naked) void DualHeadlightsNewBody()
{
    __asm
    {
        __emit 0x60                          // pushad
        call DualHeadlightConstants
        __emit 0x61                          // popad
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
    }
}

// sub_10006630, recovered VA 0x10006630: DualVehicleHeadlights' old-build body, the
// 1.0.0.0/1.0.4.0/1.0.0.4 variant. Instruction for instruction the same as the new
// body except for the four float offsets each draw reads: this build's light call
// pushes its arguments one dword (then two) further up the frame.
static __declspec(naked) void DualHeadlightsOldBody()
{
    __asm
    {
        __emit 0x60                          // pushad
        call DualHeadlightConstants
        __emit 0x61                          // popad
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
    }
}

// ---------------------------------------------------------------------------
// The steer/engine/plane consumer, and the garage and livery pieces.
//
// The three "moving parts" keys share one site set: the game's per-frame vehicle
// vtable call is detached to a detour that stores the vehicle, runs the ASI's own
// consumer (sub_10004C70) between pusha/popa and then replays the call it replaced,
// and the shared IDE parser hook is installed so the wheelangle table the consumer
// reads is filled. Only the per-key switch bytes differ.
// ---------------------------------------------------------------------------

// sub_10003F40, recovered VA 0x10003F40 (43 instructions): the model registry
// lookup. The registry is `{name**, count}`, entries stride 0xE0, and the answer is
// the first entry whose name matches case-insensitively -- or null. The original is
// `__fastcall(ECX = registry, EDX = name)` and calls `_stricmp` through its IAT; the
// slot here is filled with the same CRT entry, so the call stays `FF 15`.
static __declspec(naked) void* __fastcall VehicleComponentBody(void*, const char*)
{
    __asm
    {
        push ebp
        mov ebp, esp
        push ecx
        push ebx
        mov ebx, ecx
        mov eax, edx
        push esi
        xor ecx, ecx
        mov DWORD PTR [ebp - 4], eax
        xor esi, esi
        push edi
        cmp cx, WORD PTR [ebx + 14h]
        jnb vehicle_component_miss
        xor edi, edi
        // The original's loop alignment: 66 0F 1F 44 00 00, emitted as bytes so the
        // assembler cannot choose a shorter nop.
        __emit 0x66
        __emit 0x0F
        __emit 0x1F
        __emit 0x44
        __emit 0x00
        __emit 0x00
    vehicle_component_loop:
        push eax
        mov eax, DWORD PTR [ebx]
        push DWORD PTR [eax + edi]
        call DWORD PTR [zpatch_steer_stricmp]
        add esp, 8
        test eax, eax
        jz vehicle_component_found
        movzx eax, WORD PTR [ebx + 14h]
        inc esi
        add edi, 0E0h
        cmp esi, eax
        mov eax, DWORD PTR [ebp - 4]
        jl vehicle_component_loop
    vehicle_component_miss:
        pop edi
        pop esi
        xor eax, eax
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    vehicle_component_found:
        imul eax, esi, 0E0h
        pop edi
        pop esi
        add eax, DWORD PTR [ebx]
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}

// The consumer's call target: the original's `call sub_10003980` (E8), so ours is a
// direct call as well, through the six-byte forwarder the drive-by helper uses
// (0x10006450). ECX (the salt) and the stack reach the native layer untouched.
static __declspec(naked) void SteerResolveEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_native_resolve_entry]
    }
}

// sub_10004C70, recovered VA 0x10004C70 (1,343 bytes, 320 instructions): the
// steer/engine/plane consumer the shared detours call between pusha/popa.
//
// Transcribed instruction for instruction from the original, absolute operands and
// all. It is compiler output for C -- a /GS frame, SSE double conversions around
// every float expression, the `lahf` / `test ah,44h` / `jnp` float-`!=` idiom and two
// `cmova` -- and none of that blocks a transcription: the cookie is an absolute
// slot, the check is a call slot, the .rdata constants are absolute slots, and the
// two 512-slot wheelangle tables are indexed absolutes the table registry covers
// (tests/bodies.tsv table rows `1015A538` / `10159D38`).
//
// What the operands are:
//   * `[0x1015A538 + esi*4]` / `[0x10159D38 + esi*4]` are the wheelangle model and
//     angle tables the transcribed IDE parser hook fills (source/misc.ixx,
//     dword_1015A538 / dword_10159D38). Both are all-zero at load, in the original
//     and here: the walk stops at the first zero model word and the angle is only
//     read for a slot that matched.
//   * `[0x10159C0C]`, `[0x10159B88]`, `[0x1015D63C]`, `[0x10159CDC]`, `[0x1015AD4C]`
//     are the game's model-index lookup, the bone-matrix getter, the two matrix
//     setters and the plane's part-drive function: published slots here, filled by
//     ResolveSteerHelpers() and the site publish, exactly as the original resolves
//     them in its loader preamble.
//   * the four `call sub_10003980` sites resolve the script-timestep slot by the
//     salt dword_1015803C; `[0x1015800C]` is the /GS cookie and the epilogue's
//     `call __security_check_cookie` is the CRT routine.
//
// Every instruction's size is the original's, so every internal branch sits at the
// same offset from the body start -- which is what the proof's branch-target check
// compares. The 10-byte loop padding at 0x10004CC6 (`66 66 0F 1F 84 00 ...`) is
// emitted as bytes because the inline assembler's `nop` is one byte.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void SteerConsumerBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 14h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        cmp BYTE PTR [zpatch_steer_enabled], 0
        push edi
        mov edi, DWORD PTR [zpatch_steer_vehicle]
        je steer_engine
        mov eax, DWORD PTR [edi + 0E18h]
        movss xmm0, DWORD PTR [edi + 10D8h]
        push ebx
        movsx ebx, WORD PTR [edi + 2Eh]
        divss xmm0, DWORD PTR [eax + 84h]
        push esi
        xor esi, esi
        cvtps2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_steer_neg_pi]
        cvtpd2ps xmm0, xmm0
        movss DWORD PTR [ebp - 10h], xmm0
        // The original's loop alignment: 66 66 0F 1F 84 00 00 00 00 00.
        __emit 0x66
        __emit 0x66
        __emit 0x0F
        __emit 0x1F
        __emit 0x84
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00
    steer_wheel_walk:
        mov eax, DWORD PTR [esi*4 + zpatch_wheelangle_models]
        test eax, eax
        je steer_wheel_none
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 8
        cmp DWORD PTR [ebp - 8], ebx
        je steer_wheel_angle
        inc esi
        cmp esi, 200h
        jl steer_wheel_walk
    steer_wheel_none:
        xorps xmm0, xmm0
    steer_wheel_resume:
        movzx eax, WORD PTR [edi + 2Eh]
        movss DWORD PTR [ebp - 0Ch], xmm0
        cmp ax, 0FFFFh
        je steer_steer_done
        movsx ecx, ax
        mov edx, OFFSET zpatch_steer_name_wheel
        mov eax, DWORD PTR [zpatch_steer_model_array]
        mov eax, DWORD PTR [eax + ecx*4]
        mov eax, DWORD PTR [eax + 8]
        mov ecx, DWORD PTR [eax + 0B4h]
        mov ecx, DWORD PTR [ecx + 0Ch]
        call VehicleComponentBody
        test eax, eax
        je steer_steer_done
        movzx eax, WORD PTR [eax + 14h]
        mov ecx, edi
        mov edx, DWORD PTR [zpatch_steer_bone]
        push eax
        call edx
        mov esi, eax
        test esi, esi
        je steer_steer_done
        movss xmm0, DWORD PTR [ebp - 0Ch]
        push ecx
        mov ecx, esi
        mov DWORD PTR [esi], 3F800000h
        mov DWORD PTR [esi + 4], 0
        mov DWORD PTR [esi + 8], 0
        mov DWORD PTR [esi + 10h], 0
        mov DWORD PTR [esi + 14h], 3F800000h
        mov DWORD PTR [esi + 18h], 0
        mov DWORD PTR [esi + 20h], 0
        mov DWORD PTR [esi + 24h], 0
        mov DWORD PTR [esi + 28h], 3F800000h
        movss DWORD PTR [esp], xmm0
        call DWORD PTR [zpatch_steer_matrix_a]
        movss xmm0, DWORD PTR [ebp - 10h]
        push ecx
        mov ecx, esi
        movss DWORD PTR [esp], xmm0
        call DWORD PTR [zpatch_steer_matrix_b]
    steer_steer_done:
        pop esi
        pop ebx
    steer_engine:
        cmp BYTE PTR [zpatch_engine_enabled], 0
        je steer_plane
        mov ecx, DWORD PTR [zpatch_native_salt_1015803C]
        call SteerResolveEntry
        movss xmm0, DWORD PTR [zpatch_engine_timestep]
        mov ecx, DWORD PTR [eax + 0Eh]
        mov eax, DWORD PTR [ecx + eax + 18h]
        ucomiss xmm0, DWORD PTR [eax + 8]
        lahf
        test ah, 44h
        jnp steer_engine_matrix
        cmp BYTE PTR [zpatch_engine_descending], 0
        je steer_step_up
        movsd xmm0, QWORD PTR [zpatch_steer_neg_half]
        jmp steer_step_ready
    // The walk's hit path sits here in the original's layout (0x10004E00), between
    // the step selection and its join: the compiler placed it out of line.
    steer_wheel_angle:
        movss xmm0, DWORD PTR [esi*4 + zpatch_wheelangle_angles]
        cvtps2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_steer_pi]
        divsd xmm0, QWORD PTR [zpatch_steer_degrees]
        cvtpd2ps xmm0, xmm0
        jmp steer_wheel_resume
    steer_step_up:
        movsd xmm0, QWORD PTR [zpatch_steer_half]
    steer_step_ready:
        mov ecx, DWORD PTR [zpatch_native_salt_1015803C]
        movsd QWORD PTR [ebp - 14h], xmm0
        call SteerResolveEntry
        movss xmm0, DWORD PTR [zpatch_engine_phase]
        cvtps2pd xmm0, xmm0
        mov ecx, DWORD PTR [eax + 0Eh]
        mov eax, DWORD PTR [ecx + eax + 18h]
        mov ecx, 1
        movss xmm1, DWORD PTR [eax + 8]
        cvtps2pd xmm1, xmm1
        movzx eax, BYTE PTR [zpatch_engine_descending]
        mulsd xmm1, QWORD PTR [ebp - 14h]
        addsd xmm1, xmm0
        cvtpd2ps xmm0, xmm1
        cvtps2pd xmm1, xmm0
        movss DWORD PTR [zpatch_engine_phase], xmm0
        cvtps2pd xmm0, xmm0
        comisd xmm0, QWORD PTR [zpatch_steer_epsilon]
        movsd xmm0, QWORD PTR [zpatch_steer_neg_epsilon]
        cmova eax, ecx
        xor ecx, ecx
        comisd xmm0, xmm1
        movzx eax, al
        cmova eax, ecx
        mov BYTE PTR [zpatch_engine_descending], al
    steer_engine_matrix:
        movzx eax, WORD PTR [edi + 2Eh]
        cmp ax, 0FFFFh
        je steer_plane
        movsx ecx, ax
        mov edx, OFFSET zpatch_steer_name_engine
        mov eax, DWORD PTR [zpatch_steer_model_array]
        mov eax, DWORD PTR [eax + ecx*4]
        mov eax, DWORD PTR [eax + 8]
        mov ecx, DWORD PTR [eax + 0B4h]
        mov ecx, DWORD PTR [ecx + 0Ch]
        call VehicleComponentBody
        test eax, eax
        je steer_plane
        movzx eax, WORD PTR [eax + 14h]
        mov ecx, edi
        mov edx, DWORD PTR [zpatch_steer_bone]
        push eax
        call edx
        test eax, eax
        je steer_plane
        mov DWORD PTR [eax], 3F800000h
        mov DWORD PTR [eax + 4], 0
        mov DWORD PTR [eax + 8], 0
        mov DWORD PTR [eax + 10h], 0
        mov DWORD PTR [eax + 14h], 3F800000h
        mov DWORD PTR [eax + 18h], 0
        mov DWORD PTR [eax + 20h], 0
        mov DWORD PTR [eax + 24h], 0
        mov DWORD PTR [eax + 28h], 3F800000h
        mov ecx, DWORD PTR [edi + 0F64h]
        test cl, 8
        jne steer_engine_live
        xorps xmm0, xmm0
        jmp steer_engine_scale
    steer_engine_live:
        movss xmm0, DWORD PTR [edi + 10C8h]
        cvtps2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_steer_three_quarters]
        addsd xmm0, QWORD PTR [zpatch_steer_quarter]
        cvtpd2ps xmm0, xmm0
    steer_engine_scale:
        test cl, 10h
        je steer_engine_set
        movss xmm0, DWORD PTR [zpatch_steer_one]
    steer_engine_set:
        mulss xmm0, DWORD PTR [zpatch_engine_phase]
        push ecx
        mov ecx, eax
        movss DWORD PTR [esp], xmm0
        call DWORD PTR [zpatch_steer_matrix_b]
    steer_plane:
        cmp BYTE PTR [zpatch_plane_enabled], 0
        je steer_done
        cmp DWORD PTR [edi + 1354h], 5
        jne steer_done
        mov ecx, DWORD PTR [zpatch_native_salt_1015803C]
        call SteerResolveEntry
        push 0
        push 0
        push 0
        mov ecx, DWORD PTR [eax + 0Eh]
        push ecx
        mov eax, DWORD PTR [ecx + eax + 18h]
        mov ecx, edi
        movss xmm0, DWORD PTR [eax + 8]
        mulss xmm0, DWORD PTR [edi + 1F3Ch]
        mulss xmm0, DWORD PTR [zpatch_steer_hundred]
        movss DWORD PTR [esp], xmm0
        push 4
        push 59h
        call DWORD PTR [zpatch_steer_part_call]
        mov ecx, DWORD PTR [zpatch_native_salt_1015803C]
        call SteerResolveEntry
        push 0
        push 0
        push 0
        mov ecx, DWORD PTR [eax + 0Eh]
        push ecx
        mov eax, DWORD PTR [ecx + eax + 18h]
        mov ecx, edi
        movss xmm0, DWORD PTR [eax + 8]
        mulss xmm0, DWORD PTR [edi + 1F3Ch]
        mulss xmm0, DWORD PTR [zpatch_steer_hundred]
        movss DWORD PTR [esp], xmm0
        push 4
        push 5Bh
        call DWORD PTR [zpatch_steer_part_call]
        mov ecx, DWORD PTR [zpatch_native_salt_1015803C]
        call SteerResolveEntry
        push 0
        push 0
        push 0
        mov ecx, DWORD PTR [eax + 0Eh]
        push ecx
        mov eax, DWORD PTR [eax + ecx + 18h]
        mov ecx, edi
        movss xmm0, DWORD PTR [eax + 8]
        mulss xmm0, DWORD PTR [edi + 1F3Ch]
        mulss xmm0, DWORD PTR [zpatch_steer_hundred]
        movss DWORD PTR [esp], xmm0
        push 4
        push 5Ah
        call DWORD PTR [zpatch_steer_part_call]
        mov ecx, DWORD PTR [zpatch_native_salt_1015803C]
        call SteerResolveEntry
        push 0
        push 0
        push 0
        mov ecx, DWORD PTR [eax + 0Eh]
        push ecx
        mov eax, DWORD PTR [ecx + eax + 18h]
        mov ecx, edi
        movss xmm0, DWORD PTR [eax + 8]
        mulss xmm0, DWORD PTR [edi + 1F3Ch]
        mulss xmm0, DWORD PTR [zpatch_steer_hundred]
        movss DWORD PTR [esp], xmm0
        push 4
        push 5Ch
        call DWORD PTR [zpatch_steer_part_call]
        movss xmm0, DWORD PTR [edi + 1F08h]
        cvtps2pd xmm0, xmm0
        push 0
        push 0
        push 1
        push ecx
        mov ecx, edi
        mulsd xmm0, QWORD PTR [zpatch_steer_half]
        cvtpd2ps xmm0, xmm0
        movss DWORD PTR [esp], xmm0
        push 3
        push 60h
        call DWORD PTR [zpatch_steer_part_call]
        movss xmm0, DWORD PTR [edi + 1F08h]
        push 0
        push 0
        push 1
        cvtps2pd xmm0, xmm0
        push ecx
        mov ecx, edi
        mulsd xmm0, QWORD PTR [zpatch_steer_neg_half]
        cvtpd2ps xmm0, xmm0
        movss DWORD PTR [esp], xmm0
        push 3
        push 61h
        call DWORD PTR [zpatch_steer_part_call]
        movss xmm0, DWORD PTR [edi + 1F04h]
        cvtps2pd xmm0, xmm0
        push 0
        push 0
        push 1
        push ecx
        mov ecx, edi
        mulsd xmm0, QWORD PTR [zpatch_steer_neg_half]
        cvtpd2ps xmm0, xmm0
        movss DWORD PTR [esp], xmm0
        push 3
        push 5Eh
        call DWORD PTR [zpatch_steer_part_call]
        movss xmm0, DWORD PTR [edi + 1F04h]
        cvtps2pd xmm0, xmm0
        push 0
        push 0
        push 1
        push ecx
        mov ecx, edi
        mulsd xmm0, QWORD PTR [zpatch_steer_neg_half]
        cvtpd2ps xmm0, xmm0
        movss DWORD PTR [esp], xmm0
        push 3
        push 5Fh
        call DWORD PTR [zpatch_steer_part_call]
        movss xmm0, DWORD PTR [edi + 1F00h]
        cvtps2pd xmm0, xmm0
        push 0
        push 0
        push 1
        push ecx
        mov ecx, edi
        mulsd xmm0, QWORD PTR [zpatch_steer_half]
        cvtpd2ps xmm0, xmm0
        movss DWORD PTR [esp], xmm0
        push 5
        push 5Dh
        call DWORD PTR [zpatch_steer_part_call]
    steer_done:
        mov ecx, DWORD PTR [zpatch_native_salt_1015803C]
        call SteerResolveEntry
        pop edi
        mov ecx, DWORD PTR [eax + 0Eh]
        mov eax, DWORD PTR [eax + ecx + 18h]
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        movss xmm0, DWORD PTR [eax + 8]
        movss DWORD PTR [zpatch_engine_timestep], xmm0
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10001280, recovered VA 0x10001280: the bounded formatter, transcribed once
// in source/stability.ixx and proved there instruction for instruction
// A module interface cannot export a naked function, so this
// six-byte forwarder is what the livery helpers' `call` names: the original's
// call is an immediate too, and the forwarder enters the body with the stack and
// the registers untouched.
static __declspec(naked) void NoliveryFormat()
{
    __asm
    {
        jmp DWORD PTR [zpatch_format_entry]
    }
}

// sub_100051B0, recovered VA 0x100051B0 (10 instructions): the steer detour. It
// stores the vehicle the site was called for, runs the consumer between pusha/popa,
// then replays the ten bytes the jump replaced (`mov edx,[esi]; mov eax,[edx+128h];
// mov ecx,esi; call eax`) and tail-jumps to the site's `jnz` (match+16), so the
// resumed branch still tests the vtable call's result. pusha/popa are emitted as
// bytes: the inline assembler renders those mnemonics with a 0x66 prefix.
static __declspec(naked) void SteerDetourEaxBody()
{
    __asm
    {
        mov DWORD PTR [zpatch_steer_vehicle], esi
        __emit 0x60  // pusha
        call SteerConsumerBody
        __emit 0x61  // popa
        mov edx, DWORD PTR [esi]
        mov eax, DWORD PTR [edx + 128h]
        mov ecx, esi
        call eax
        test al, al
        jmp DWORD PTR [zpatch_steer_resume]
    }
}

// sub_100051E0, recovered VA 0x100051E0 (10 instructions): the older builds'
// variant of the same detour. The only difference is which register holds the
// vtable pointer across the call (`mov eax,[esi]; mov edx,[eax+128h]`).
static __declspec(naked) void SteerDetourEdxBody()
{
    __asm
    {
        mov DWORD PTR [zpatch_steer_vehicle], esi
        __emit 0x60  // pusha
        call SteerConsumerBody
        __emit 0x61  // popa
        mov eax, DWORD PTR [esi]
        mov edx, DWORD PTR [eax + 128h]
        mov ecx, esi
        call edx
        test al, al
        jmp DWORD PTR [zpatch_steer_resume]
    }
}

// sub_10046B90, recovered VA 0x10046B90 (19 instructions): GarageModelFix's colour
// copy. It takes the garage object in ECX and the vehicle in EDX, reads the
// vehicle's model word into the model-info array, and copies the model's +0x3C
// dword into four byte fields of the garage object (0x40/0x13/0x41/0x46, in the
// original's order). The whole body is skipped when the original's DllMain found
// CitizenIV.exe loaded (byte_10159B85).
static __declspec(naked) void GarageColourBody()
{
    __asm
    {
        cmp BYTE PTR [zpatch_garage_citizen], 0
        push esi
        mov esi, ecx
        jnz garage_colour_done
        movsx edx, WORD PTR [edx + 2Eh]
        mov eax, DWORD PTR [zpatch_steer_model_array]
        mov eax, DWORD PTR [eax + edx * 4]
        mov eax, DWORD PTR [eax + 3Ch]
        mov ecx, eax
        shr ecx, 8
        mov BYTE PTR [esi + 40h], cl
        mov ecx, eax
        shr ecx, 10h
        mov BYTE PTR [esi + 13h], al
        shr eax, 18h
        mov BYTE PTR [esi + 41h], cl
        mov BYTE PTR [esi + 46h], al
    garage_colour_done:
        pop esi
        ret
    }
}

// sub_10046BD0, recovered VA 0x10046BD0 (55 instructions): GarageModelFix's model
// reassignment. It packs the four bytes the colour copy wrote back into a name hash
// and asks the game's model-index lookup for it. A valid index (1..31999) is stored
// at the garage object's +0x10; anything else falls back to the hash 0xC703DB5F,
// whose index is stored the same way, and clears bit 0 of +0x30. Both lookups go
// through the published dword_10159C0C slot, and the answer (the last index or
// 0xFFFE) is what the caller re-pushes as the site's argument.
static __declspec(naked) void GarageLookupBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push esi
        mov esi, ecx
        movzx edx, BYTE PTR [esi + 46h]
        movzx eax, BYTE PTR [esi + 41h]
        shl edx, 8
        or edx, eax
        movzx eax, BYTE PTR [esi + 40h]
        shl edx, 8
        or edx, eax
        movzx eax, BYTE PTR [esi + 13h]
        shl edx, 8
        or edx, eax
        jz garage_lookup_done
        lea eax, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push eax
        push edx
        call DWORD PTR [zpatch_model_lookup]
        mov eax, DWORD PTR [ebp - 8]
        add esp, 8
        test eax, eax
        jle garage_lookup_fallback
        cmp eax, 7D00h
        jge garage_lookup_fallback
        mov WORD PTR [esi + 10h], ax
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    garage_lookup_fallback:
        lea eax, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push eax
        push 0C703DB5Fh
        call DWORD PTR [zpatch_model_lookup]
        mov ax, WORD PTR [ebp - 8]
        add esp, 8
        mov WORD PTR [esi + 10h], ax
        mov eax, 0FFFEh
        and WORD PTR [esi + 30h], ax
    garage_lookup_done:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10046C80, recovered VA 0x10046C80 (8 instructions): GarageModelFix's first
// site body. It runs the colour copy with the site's ESI/EDI, then replays the six
// bytes the jump replaced (`mov edx,[edi+20h]; fld [edx+34h]`) and jumps to the
// site's `fstp [esi+4]` (match+6).
static __declspec(naked) void GarageBodyA()
{
    __asm
    {
        __emit 0x60  // pusha
        mov ecx, esi
        mov edx, edi
        call GarageColourBody
        __emit 0x61  // popa
        mov edx, DWORD PTR [edi + 20h]
        fld DWORD PTR [edx + 34h]
        jmp DWORD PTR [zpatch_garage_resume_a]
    }
}

// The garage_entry group's prologue form of body A, installed at 0xC68B90
// (match - 0x20) instead of 0xC68BB0. At the function entry ECX is the object
// and [esp+4] the vehicle; after pushad they are at [esp+0x18] and [esp+0x24].
// The skipped first instruction is `movss xmm0, ds:<float>` (8 bytes), so the
// body reloads that constant and resumes at entry+8 (match - 0x18).
static __declspec(naked) void GarageEntryBodyA()
{
    __asm
    {
        __emit 0x60  // pusha
        mov ecx, DWORD PTR [esp + 18h]
        mov edx, DWORD PTR [esp + 24h]
        call GarageColourBody
        __emit 0x61  // popa
        push eax
        mov eax, OFFSET zpatch_garage_scale
        __emit 0F3h
        __emit 00Fh
        __emit 010h
        __emit 000h  // movss xmm0, DWORD PTR [eax]
        pop eax
        jmp DWORD PTR [zpatch_garage_entry_resume_a]
    }
}

// sub_10046CA0, recovered VA 0x10046CA0 (7 instructions): GarageModelFix's second
// site body. It runs the model reassignment, replays `movzx ecx,[esi]; push eax;
// push ecx` -- EAX being the index the helper answered -- and jumps to the site's
// own `call` (match+5).
static __declspec(naked) void GarageBodyB()
{
    __asm
    {
        __emit 0x60  // pusha
        call GarageLookupBody
        __emit 0x61  // popa
        movzx ecx, WORD PTR [esi]
        push eax
        push ecx
        jmp DWORD PTR [zpatch_garage_resume_b]
    }
}

// sub_1003D220, recovered VA 0x1003D220 (26 instructions): NoLiveryLimit's
// "<name>_sign_<index+1>" hash. The name comes from dword_10159BA8, the index from
// dword_10159BB0, the formatted text is 64 bytes on the stack, the hash goes
// through the original's own resolver (sub_10003E50) and lands in dword_10159B98.
static __declspec(naked) void NoliveryHashBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 44h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        mov eax, DWORD PTR [zpatch_nolivery_index]
        inc eax
        push eax
        push DWORD PTR [zpatch_nolivery_name]
        lea eax, [ebp - 44h]
        push OFFSET zpatch_nolivery_format_text
        push 40h
        push eax
        call NoliveryFormat
        lea eax, [ebp - 44h]
        push eax
        call NoliveryResolve
        mov ecx, DWORD PTR [ebp - 4]
        add esp, 18h
        xor ecx, ebp
        mov DWORD PTR [zpatch_nolivery_hash], eax
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_1003D270, recovered VA 0x1003D270 (37 instructions): NoLiveryLimit's variant
// count. It formats "<name>_sign_<i>" from i = 1 up, hashes it, and calls the
// published lookup (dword_10159BA4) with the object the site saved (dword_10159B94)
// until the answer is zero; the highest i that answered is stored in dword_10159B90.
static __declspec(naked) void NoliveryCountBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 44h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        mov DWORD PTR [zpatch_nolivery_count], 0
        xor eax, eax
        // The original's loop alignment: 0F 1F 40 00.
        __emit 0x0F
        __emit 0x1F
        __emit 0x40
        __emit 0x00
    nolivery_count_loop:
        inc eax
        push eax
        push DWORD PTR [zpatch_nolivery_bufptr]
        lea eax, [ebp - 44h]
        push OFFSET zpatch_nolivery_format_text
        push 40h
        push eax
        call NoliveryFormat
        lea eax, [ebp - 44h]
        push eax
        call NoliveryResolve
        mov edx, DWORD PTR [zpatch_nolivery_fn]
        add esp, 18h
        mov ecx, DWORD PTR [zpatch_nolivery_arg]
        push eax
        call edx
        test eax, eax
        jz nolivery_count_done
        mov eax, DWORD PTR [zpatch_nolivery_count]
        inc eax
        mov DWORD PTR [zpatch_nolivery_count], eax
        jmp nolivery_count_loop
    nolivery_count_done:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_1003D2F0, recovered VA 0x1003D2F0 (6 instructions): copy the name the site
// passed into the 0x10-byte buffer. The original calls the CRT's strcpy_s through
// its IAT slot; the slot here holds the same import, so the call keeps its shape
// and its unguarded read of both pointers.
static __declspec(naked) void NoliveryCopyBody()
{
    __asm
    {
        push DWORD PTR [zpatch_nolivery_bufptr]
        push 10h
        push DWORD PTR [zpatch_nolivery_dest]
        call DWORD PTR [zpatch_nolivery_strcpy_s]
        add esp, 0Ch
        ret
    }
}

// sub_1003D310, recovered VA 0x1003D310 (11 instructions): the EAX site body. It
// hashes "<name>_sign_<eax+1>" from the name at [ebx+0x13C], then replays the site's
// `movsx edx,[ebx+48h]` and pushes the hash and that word before jumping to the
// site's own call (match+13). pusha/popa ride along so the argument registers of
// the surrounding code survive the helper.
static __declspec(naked) void NoliveryBodyA()
{
    __asm
    {
        __emit 0x60  // pusha
        lea ecx, [ebx + 13Ch]
        mov DWORD PTR [zpatch_nolivery_name], ecx
        mov DWORD PTR [zpatch_nolivery_index], eax
        call NoliveryHashBody
        __emit 0x61  // popa
        mov ecx, DWORD PTR [zpatch_nolivery_hash]
        movsx edx, WORD PTR [ebx + 48h]
        push ecx
        push edx
        jmp DWORD PTR [zpatch_nolivery_resume]
    }
}

// sub_1003D340, recovered VA 0x1003D340 (11 instructions): the ECX site body, the
// same three steps with the index in ECX and the pushed word in EAX.
static __declspec(naked) void NoliveryBodyB()
{
    __asm
    {
        __emit 0x60  // pusha
        lea edx, [ebx + 13Ch]
        mov DWORD PTR [zpatch_nolivery_name], edx
        mov DWORD PTR [zpatch_nolivery_index], ecx
        call NoliveryHashBody
        __emit 0x61  // popa
        mov edx, DWORD PTR [zpatch_nolivery_hash]
        movsx eax, WORD PTR [ebx + 48h]
        push edx
        push eax
        jmp DWORD PTR [zpatch_nolivery_resume]
    }
}

// sub_1003D370, recovered VA 0x1003D370 (12 instructions): the older builds' ECX
// site body. It is the same as the one above plus the `add esp,8` that drops the
// two arguments the site's preceding call had pushed.
static __declspec(naked) void NoliveryBodyC()
{
    __asm
    {
        __emit 0x60  // pusha
        lea edx, [ebx + 13Ch]
        mov DWORD PTR [zpatch_nolivery_name], edx
        mov DWORD PTR [zpatch_nolivery_index], eax
        call NoliveryHashBody
        __emit 0x61  // popa
        mov edx, DWORD PTR [zpatch_nolivery_hash]
        movsx eax, WORD PTR [ebx + 48h]
        add esp, 8
        push edx
        push eax
        jmp DWORD PTR [zpatch_nolivery_resume]
    }
}

// sub_1003D3A0, recovered VA 0x1003D3A0 (14 instructions): the site that collects
// the livery name a caller passes on the stack. It copies that name into the
// site's own [ebp+0x13C] buffer (the `lea esi,[ebp+0x13C]` it replaces), counts the
// "<name>_sign_N" variants, stores the count as a byte at [ebp+0xC2] and jumps to
// the site's continuation (match+90).
static __declspec(naked) void NoliveryBodyD()
{
    __asm
    {
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_nolivery_arg], edi
        lea ecx, [ebp + 13Ch]
        mov DWORD PTR [zpatch_nolivery_dest], ecx
        lea ecx, [esp + 60h]
        mov DWORD PTR [zpatch_nolivery_bufptr], ecx
        call NoliveryCopyBody
        call NoliveryCountBody
        __emit 0x61  // popa
        push ebx
        mov ebx, DWORD PTR [zpatch_nolivery_count]
        mov BYTE PTR [ebp + 0C2h], bl
        pop ebx
        jmp DWORD PTR [zpatch_nolivery_resume_d]
    }
}

// sub_1003D3E0, recovered VA 0x1003D3E0 (14 instructions): the other builds'
// variant of the same collector. The name buffer is at [edi+0x13C] and the count
// byte at [edi+0xC2] instead of on the stack; the object registers are unchanged.
static __declspec(naked) void NoliveryBodyE()
{
    __asm
    {
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_nolivery_arg], esi
        lea ecx, [edi + 13Ch]
        mov DWORD PTR [zpatch_nolivery_dest], ecx
        lea ecx, [esp + 60h]
        mov DWORD PTR [zpatch_nolivery_bufptr], ecx
        call NoliveryCopyBody
        call NoliveryCountBody
        __emit 0x61  // popa
        push ebx
        mov ebx, DWORD PTR [zpatch_nolivery_count]
        mov BYTE PTR [edi + 0C2h], bl
        pop ebx
        jmp DWORD PTR [zpatch_nolivery_resume_d]
    }
}
