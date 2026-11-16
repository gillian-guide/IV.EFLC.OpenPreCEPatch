module;

#include <common.hxx>
// The /GS cookie the transcribed helpers' prologues read and their epilogues check
// (`__security_cookie`, `__security_check_cookie` in vcruntime.h), and the CRT entries
// the transcribed pieces call (`_stricmp`, `strcpy_s`).
#include <intrin.h>
#include <bit>
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
    // that array (docs/rebuilt.md Â§2g). This arm publishes the same value into it.
    extern uint32_t zpatch_carparts_shadow_fn;


    // ---- ForceCarHeadlightShadows --------------------------------------------
    // dword_1015BDD0: the object the hooked function receives in ECX (the shim
    // stores it, the checker reads +0x2E of it). dword_101580C0: the offset float
    // the site's `movss xmm2, [0xD95B68]` is retargeted at; the original seeds its
    // copy with 2.0f on disk, so this one starts there too. dword_1015B5BC: the
    // resume, published from the site's own prologue before the E9 replaces it, and
    // defined in source/config.ixx as the second word of the [HeadlightConeOffsets]
    // block (the original keeps it between the cone's hashes and its offsets, and the
    // cone walk can read it -- docs/rebuilt.md Â§2g).
    uint32_t zpatch_fchs_param = 0;
    float zpatch_fchs_offset = 2.0f;
    extern uint32_t zpatch_fchs_resume;


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


// Forward declarations for the transcribed bodies at the bottom of this
// file: the readable code above installs them (and a body may branch to
// another), so every one is named here. MSVC rejects `__declspec(naked)`
// on a declaration (C2488), so these carry the signature only; each
// definition in the tail is naked.
static void CarPartsShadowShim();
static void CarDynamicShadowShim();
static void CarDynamicShadowShimB();
static void FchsCheckBody();
static void FchsShimBody();
static void CarDecalsSelectorShim();
static void DualHeadlightsNewBody();
static void DualHeadlightsOldBody();
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

    // The address of a body another module transcribed, by its recovered VA. The
    // shared IDE parser hook lives in source/misc.ixx, and a module interface cannot
    // export a naked function; the body registry is what makes it reachable (the same
    // route the BPM arm uses for the native layer's pieces).
    uintptr_t RegisteredBody(uint32_t reference)
    {
        for (const auto& entry : Bodies::All())
        {
            if (entry.reference == reference)
                return entry.address;
        }
        return 0;
    }

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
        auto pattern = find_pattern("32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_steer_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 16));
            zpatch_steer_part_call = Rel32Target(match, 423, 427);
        }

        pattern = find_pattern("7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            zpatch_steer_resume = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + 16));
            zpatch_steer_part_call = Rel32Target(match, 140, 144);
        }

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            zpatch_ide_hook_slot = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(-1)));
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
            zpatch_garage_resume_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(6)));

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
    // this arm anywhere but 1.0.4.0. See docs/parity.md.
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
        auto pattern = find_pattern("32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(2),
                reinterpret_cast<void*>(&SteerDetourEaxBody), true);

        pattern = find_pattern("7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(2),
                reinterpret_cast<void*>(&SteerDetourEdxBody), true);

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(-10),
                reinterpret_cast<void*>(RegisteredBody(0x100040E0u)), true);
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

        auto pattern = find_pattern("8B 57 20 D9 42 34 D9 5E 04 8B 47 20");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&GarageBodyA), true);

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
// ---------------------------------------------------------------------------
// PlaneCrashFix, moved from the stability module.
// ---------------------------------------------------------------------------

extern "C"
{
    // PlaneCrashFix's published resumes, the site's own two continuations.
    uint32_t zpatch_plane_crash_resume_a = 0;   // dword_1015BE18
    uint32_t zpatch_plane_crash_resume_b = 0;   // dword_1015BE14
}

static void PlaneCrashShim();

namespace
{
    // PlaneCrashFix's published resumes: the normal one is the anchor's +14 (after
    // the vtable read the body replays), the null one is the anchor's -705, the head
    // of the same function.
    void PublishPlaneCrashFix()
    {
        if (!Loader::Enabled("PlaneCrashFix"))
            return;

        auto pattern = find_pattern("83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00");
        if (pattern.empty())
            return;

        const auto match = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        zpatch_plane_crash_resume_b = static_cast<uint32_t>(match + 14);
        zpatch_plane_crash_resume_a = static_cast<uint32_t>(match - 705);
    }

    // PlaneCrashFix: the anchor's +3 is the state store the body replays after its
    // null check.
    void InstallPlaneCrashFix()
    {
        if (!Loader::Enabled("PlaneCrashFix"))
            return;

        auto pattern = find_pattern("83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00");
        if (pattern.empty())
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&PlaneCrashShim), 0x1003D870u);
        injector::MakeJMP(pattern.get_first(3), reinterpret_cast<void*>(&PlaneCrashShim), true);
    }
}

// ---------------------------------------------------------------------------
// CopSirensFlag and PopupHeadlightsFlag, moved from the misc module.
// ---------------------------------------------------------------------------

// CopSirensFlag: the two model checks (sub_10005F70 and sub_10006260) and the
// shared IDE hook (sub_100040E0), which fills the 512-slot model-hash tables the
// checks walk from the game's IDE parser entries.

extern "C"
{
    // dword_10159C0C, defined in source/shared.ixx: the game's model-index lookup,
    // `int lookup(uint32_t nameHash, int32_t *index)`.
    extern uintptr_t zpatch_model_lookup;

    // dword_1015937C / dword_10159374 (the resumes), dword_10159370 and
    // dword_1015936C (the two model indices the sites computed), dword_10159B80 /
    // dword_10159378 (the two flags), and the IDE parser's arguments and resume
    // (dword_1015BE08 the string, dword_1015BE0C the model object, dword_1015BE10
    // the continuation).
    uint32_t zpatch_copsirens_resume = 0;
    uint32_t zpatch_copsirens_resume_b = 0;
    uint32_t zpatch_copsirens_index = 0;
    uint32_t zpatch_copsirens_index_b = 0;
    uint32_t zpatch_copsirens_flag = 0;
    uint32_t zpatch_copsirens_flag_b = 0;
    uint32_t zpatch_ide_string = 0;
    uint32_t zpatch_ide_model = 0;
    uint32_t zpatch_ide_hook_slot = 0;

    // The 512-slot tables sub_10003FA0 fills: dword_10159380 (sirens),
    // dword_1015CE28 (popuplights), and dword_1015A538 / dword_10159D38
    // (wheelangle models and the float bits of their angles).
    uint32_t zpatch_copsirens_models[512] = {};
    uint32_t zpatch_popuplights_models[512] = {};
    uint32_t zpatch_wheelangle_models[512] = {};
    uint32_t zpatch_wheelangle_angles[512] = {};

    // The names the two siren checks resolve, each the original's own .rdata string
    // and each pushed with `push imm32`: the first check's thirteen built-in police
    // models in its order (0x1013A6B4 ... 0x10129DB4) and the second check's
    // "police2" (0x1013A734, the same string the first check uses second).
    const char zpatch_copsirens_police[] = "police";
    const char zpatch_copsirens_police2[] = "police2";
    const char zpatch_copsirens_police3[] = "police3";
    const char zpatch_copsirens_police4[] = "police4";
    const char zpatch_copsirens_noose[] = "noose";
    const char zpatch_copsirens_pstockade[] = "pstockade";
    const char zpatch_copsirens_nstockade[] = "nstockade";
    const char zpatch_copsirens_polpatriot[] = "polpatriot";
    const char zpatch_copsirens_fbi[] = "fbi";
    const char zpatch_copsirens_n_police[] = "n_police";
    const char zpatch_copsirens_n_police2[] = "n_police2";
    const char zpatch_copsirens_n_polpatriot[] = "n_polpatriot";
    const char zpatch_copsirens_n_pstockade[] = "n_pstockade";

    // The IDE parser's third arm compares the site's string against this literal
    // (original 0x10128DF4, "wheelangle:" and its NUL) and parses the degrees that
    // follow it with the CRT's strtof (original IAT slot 0x100632D4, `call ds:[...]`,
    // which stays a pointer slot because the original's call is memory indirect).
    const char zpatch_ide_wheelangle_prefix[] = "wheelangle:";
    uintptr_t zpatch_ide_strtof = 0;
}

// PopupHeadlightsFlag: the checkbox that makes a vehicle's pop-up headlights
// deserve their own light cone. The two shims (sub_10044530 and its EDX twin
// sub_10044560) run the popuplights checker (sub_10044310) and continue into the
// site's own headlight test.

extern "C"
{
    // dword_1015BE20 (the site's model word), dword_1015BE24 (the check's flag),
    // dword_1015BE1C (the popuplights model index), dword_101580CC (the angle the
    // site's `fld` loads), and dword_1015D628 / dword_1015D62C (the two resumes).
    uint32_t zpatch_popup_model = 0;
    uint32_t zpatch_popup_flag = 0;
    uint32_t zpatch_popup_index = 0;
    float zpatch_popup_angle = 1.57079632679489661923f;  // pi/2, dword_101580CC
    uint32_t zpatch_popup_resume_a = 0;
    uint32_t zpatch_popup_resume_b = 0;

    // The two 512-slot [PopupLightAngles] tables the checker walks
    // (dword_1015C628 / dword_1015BE28) are Config::PopupLightAngles()'s arrays, filled
    // by the loader preamble's reader and registered beside this arm. When the section
    // is empty the hashes are all zero, so the checker keeps the pi/2 above -- the
    // original's own default.
}

static void IdeParseBody();
static void CopSirensCheckABody();
static void CopSirensCheckBBody();
static void CopSirensShimABody();
static void CopSirensShimBBody();
static void IdeHookBody();
static void PopupHeadlightsCheckBody();
static void PopupHeadlightsBodyA();
static void PopupHeadlightsBodyB();

namespace
{
    // CopSirensFlag's three sites: the two model checks and the shared IDE hook,
    // installed at the parser anchor's -10 (the seven-byte compare).
    void PublishCopSirensFlag()
    {
        if (!Loader::Enabled("CopSirensFlag"))
            return;

        auto pattern = find_pattern("E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00");
        if (!pattern.empty())
            zpatch_copsirens_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(5)));

        pattern = find_pattern("0F BF 41 2E 39 05");
        if (!pattern.empty())
            zpatch_copsirens_resume_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(28)));

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            zpatch_ide_hook_slot = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(-1)));
    }

    // PopupHeadlightsFlag's sites: the EAX anchor (jump, its +10 branch forced to
    // a short jump, and the `fld` operand at +82 retargeted to our pi/2 float),
    // the shared IDE hook, and the EDX twin when the image has it.
    void PublishPopupHeadlightsFlag()
    {
        if (!Loader::Enabled("PopupHeadlightsFlag"))
            return;

        auto pattern = find_pattern("0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            zpatch_popup_resume_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(10)));
            zpatch_popup_resume_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(12)));
        }

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            zpatch_ide_hook_slot = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(-1)));

        pattern = find_pattern("0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            zpatch_popup_resume_b = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(10)));
            zpatch_popup_resume_a = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(12)));
        }
    }

    void InstallCopSirensFlag()
    {
        if (!Loader::Enabled("CopSirensFlag"))
            return;

        auto pattern = find_pattern("E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(0),
                reinterpret_cast<void*>(&CopSirensShimABody), true);

        pattern = find_pattern("0F BF 41 2E 39 05");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(22),
                reinterpret_cast<void*>(&CopSirensShimBBody), true);

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(-10),
                reinterpret_cast<void*>(&IdeHookBody), true);
    }

    void InstallPopupHeadlightsFlag()
    {
        if (!Loader::Enabled("PopupHeadlightsFlag"))
            return;

        auto pattern = find_pattern("0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeJMP(match, reinterpret_cast<void*>(&PopupHeadlightsBodyA), true);
            injector::WriteMemory<std::byte>(match + 10, Bytes::ShortJmp, true);
            injector::WriteMemory<uint32_t>(match + 82,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_popup_angle)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(match + 82));
        }

        pattern = find_pattern("0C 83 88 94 00 00 00 04");
        if (!pattern.empty())
            injector::MakeJMP(pattern.get_first<std::byte>(-10),
                reinterpret_cast<void*>(&IdeHookBody), true);

        pattern = find_pattern("0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            injector::MakeJMP(match, reinterpret_cast<void*>(&PopupHeadlightsBodyB), true);
            injector::WriteMemory<std::byte>(match + 10, Bytes::ShortJmp, true);
            injector::WriteMemory<uint32_t>(match + 82,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_popup_angle)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(match + 82));
        }
    }

}

namespace
{
    // sub_10003E50 (source/shared.ixx), the original's own name resolver/hasher.
    // The checks call it rather than a C++ hash, so the "hash:" and quoted forms
    // resolve exactly as in the original.
    uint32_t ResolveModelName(const char* name)
    {
        return reinterpret_cast<uint32_t(__cdecl*)(const char*)>(Shared::ValueResolver())(name);
    }
}

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

    // The IDE payload's strtof slot and the stubs' out-of-line bodies.
    zpatch_ide_strtof = reinterpret_cast<uintptr_t>(&strtof);
    [[maybe_unused]] const uintptr_t ideHelpers[] = {
        reinterpret_cast<uintptr_t>(&CopSirensCheckABody),
        reinterpret_cast<uintptr_t>(&CopSirensCheckBBody),
        reinterpret_cast<uintptr_t>(&IdeParseBody),
        reinterpret_cast<uintptr_t>(&PopupHeadlightsCheckBody),
    };

    Bodies::Register(reinterpret_cast<uintptr_t>(&CopSirensShimABody), 0x10006310u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&CopSirensShimBBody), 0x10006330u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&IdeHookBody), 0x100040E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&PopupHeadlightsBodyA), 0x10044530u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&PopupHeadlightsBodyB), 0x10044560u);
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_copsirens_models),
        0x10159380u, sizeof(zpatch_copsirens_models));   // 512 * 4 = 2048
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_popuplights_models),
        0x1015CE28u, sizeof(zpatch_popuplights_models));  // 512 * 4 = 2048
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_popup_angle_hashes),
        0x1015C628u, sizeof(zpatch_popup_angle_hashes));  // 512 * 4 = 2048
    Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_popup_angle_values),
        0x1015BE28u, sizeof(zpatch_popup_angle_values));  // 512 * 4 = 2048

    Loader::AddPublish(Loader::Entry::Vehicles, 1, PublishCarPartsShadowFix);
    Loader::AddPublish(Loader::Entry::Vehicles, 2, PublishCarDynamicShadowFix);
    Loader::AddPublish(Loader::Entry::Vehicles, 3, PublishForceCarHeadlightShadows);
    Loader::AddPublish(Loader::Entry::Vehicles, 5, PublishDualVehicleHeadlights);
    Loader::AddPublish(Loader::Entry::Vehicles, 6, PublishMovingSteeringWheelSupport);
    Loader::AddPublish(Loader::Entry::Vehicles, 7, PublishShakingEngineBlock);
    Loader::AddPublish(Loader::Entry::Vehicles, 8, PublishMovingPlaneParts);
    Loader::AddPublish(Loader::Entry::Vehicles, 9, PublishGarageModelFix);
    Loader::AddPublish(Loader::Entry::Vehicles, 10, PublishNoLiveryLimit);

    Loader::AddPublish(Loader::Entry::Stability, 3, PublishPlaneCrashFix);
    Loader::AddPublish(Loader::Entry::Misc, 3, PublishCopSirensFlag);
    Loader::AddPublish(Loader::Entry::Misc, 4, PublishPopupHeadlightsFlag);
    Loader::AddInstall(Loader::Entry::Vehicles, 0, InstallCarDecalsFix);
    Loader::AddInstall(Loader::Entry::Misc, 5, InstallCopSirensFlag);
    Loader::AddInstall(Loader::Entry::Misc, 6, InstallPopupHeadlightsFlag);
    Loader::AddInstall(Loader::Entry::Vehicles, 2, InstallCarPartsShadowFix);
    Loader::AddInstall(Loader::Entry::Vehicles, 3, InstallCarDynamicShadowFix);
    // After the shadow arm: the original's ForceCarHeadlightShadows handler runs
    // after the CarDynamicShadowFix block, and its tail restores what that block
    // rewrites -- so with both keys on, the restore is what the game sees.
    Loader::AddInstall(Loader::Entry::Vehicles, 4, InstallForceCarHeadlightShadows);
    Loader::AddInstall(Loader::Entry::Vehicles, 5, InstallInfiniteCarBottomDistance);
    Loader::AddInstall(Loader::Entry::Stability, 6, InstallPlaneCrashFix);
    Loader::AddInstall(Loader::Entry::Vehicles, 6, InstallReversingLightFix);
    Loader::AddInstall(Loader::Entry::Vehicles, 13, InstallDualVehicleHeadlights);
    Loader::AddInstall(Loader::Entry::Vehicles, 14, InstallIncreaseVehicleModelLimit);
    Loader::AddInstall(Loader::Entry::Vehicles, 15, InstallIncreaseVehicleStructLimit);
    Loader::AddInstall(Loader::Entry::Vehicles, 16, InstallMovingSteeringWheelSupport);
    Loader::AddInstall(Loader::Entry::Vehicles, 17, InstallShakingEngineBlock);
    Loader::AddInstall(Loader::Entry::Vehicles, 18, InstallMovingPlaneParts);
    Loader::AddInstall(Loader::Entry::Vehicles, 19, InstallGarageModelFix);
    Loader::AddInstall(Loader::Entry::Vehicles, 20, InstallNoLiveryLimit);

    // The bodies' recovered VAs, so the dump expresses each arm's branch in the
    // original's coordinates. The three steer keys all jump to the same two detour
    // bodies, so those are registered once here rather than in the shared installer.
    Bodies::Register(reinterpret_cast<uintptr_t>(&VehicleComponentBody), 0x10003F40u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&FchsShimBody), 0x1001ADB0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SteerDetourEaxBody), 0x100051B0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&SteerDetourEdxBody), 0x100051E0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GarageColourBody), 0x10046B90u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GarageLookupBody), 0x10046BD0u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&GarageBodyA), 0x10046C80u);
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
    // offline projection the proof resolves (docs/rebuilt.md). The arrays belong to
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
    // original's .data (docs/rebuilt.md Â§2g). The offsets array is registered at its
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
        reinterpret_cast<uintptr_t>(&FchsCheckBody),
    };
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------









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
// instead of a bounded walk (docs/rebuilt.md Â§2g). The two arrays are registered with
// their original VAs; the hash table's registration is the extended 2,052 bytes
// (512 words + the neighbour).
//
// The /GS frame is the original's (cookie slot, check call slot) and the
// `nop dword ptr [eax]` at 0x1001AD4D is emitted as bytes. No exception frame, no TLS
// static-init guard.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void FchsCheckBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        mov eax, DWORD PTR [zpatch_headlight_cone_hashes]
        push esi
        xor esi, esi
        mov DWORD PTR [zpatch_fchs_offset], 40000000h   // 2.0f, the .data seed
        push edi
        mov edi, DWORD PTR [zpatch_fchs_param]
        test eax, eax
        je fchs_done
        __emit 0x0F
        __emit 0x1F
        __emit 0x00                  // nop dword ptr [eax]
    fchs_loop:
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        movsx eax, WORD PTR [edi + 2Eh]
        add esp, 8
        cmp eax, DWORD PTR [ebp - 8]
        je fchs_found
        // The table's base plus four: ESI is the slot just consumed, so slot ESI+1 is
        // at [esi*4+base+4], and at ESI=511 that is the word past the table.
        mov eax, DWORD PTR [esi*4 + zpatch_headlight_cone_hashes + 4]
        inc esi
        test eax, eax
        jne fchs_loop
        pop edi
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    fchs_found:
        movss xmm0, DWORD PTR [esi*4 + zpatch_headlight_cone_values]
        movss DWORD PTR [zpatch_fchs_offset], xmm0
    fchs_done:
        mov ecx, DWORD PTR [ebp - 4]
        pop edi
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

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
        call FchsCheckBody
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
// (tests/bodies.tsv table rows `1015A538` / `10159D38`, docs/rebuilt.md Â§2c).
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
// (docs/rebuilt.md). A module interface cannot export a naked function, so this
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

// abi_plane_crash_shim, recovered VA 0x1003D870: the plane's destruction tail. The
// `mov [eax+26h],bl` at the anchor's +3 is replaced, so the body replays it after a
// null check and takes the null state's path (the anchor's -705, the head of the
// same function) instead of dereferencing a missing vehicle. Naked and internal: an
// external-linkage naked function is written to the module and every importer
// fails.
static __declspec(naked) void PlaneCrashShim()
{
    __asm
    {
        cmp eax, 0
        jz plane_crash_null
        mov BYTE PTR [eax + 26h], bl
        mov edx, DWORD PTR [esi]
        mov eax, DWORD PTR [edx + 0F4h]
        jmp DWORD PTR [zpatch_plane_crash_resume_b]
    plane_crash_null:
        jmp DWORD PTR [zpatch_plane_crash_resume_a]
    }
}

// sub_10003FA0, recovered VA 0x10003FA0 (92 instructions, 314 bytes): the IDE parser
// hook's payload, transcribed instruction for instruction.
//
// Three independent tests on the site's string (dword_1015BE08), in the original's
// order, each of which appends the model word (dword_1015BE0C + 60, read unguarded)
// to a 512-slot table when it is not already there: "popuplights" by three dword
// compares, "siren" by a dword and a word compare, and the "wheelangle:" prefix
// through the CRT's _strnicmp followed by strtof of the text eleven bytes in. The
// first two fall through to the next arm; the third ends the function.
//
// Every one of the four tables is a registered table (docs/rebuilt.md Â§2b-Â§2e): the
// popuplights list at 0x1015CE28, the siren list at 0x10159380, and the wheelangle
// model/angle pair at 0x1015A538 / 0x10159D38. Three of them are read through an
// absolute *with* a base and no index (`cmp [eax+table],esi`, the Â§2d shape) and the
// stores through an indexed absolute (`mov [ecx*4+table],eax`, the Â§2b shape); the
// slot-0 entry check is a plain absolute and therefore an `abs` slot.
//
// The three loop paddings are emitted as bytes because the inline assembler picks
// its own: `66 90` at 0x10003FDE, `0F 1F 00` at 0x1000402D and
// `66 0F 1F 44 00 00` at 0x1000409A. No exception frame and no TLS static-init
// guard, so nothing blocks the transcription.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void IdeParseBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push ecx
        mov edx, DWORD PTR [zpatch_ide_string]
        mov eax, 75706F70h            // "popu"
        push esi
        push edi
        mov edi, DWORD PTR [zpatch_ide_model]
        cmp eax, DWORD PTR [edx]
        jne ide_not_popuplights
        mov eax, 67696C70h            // "plig"
        cmp eax, DWORD PTR [edx + 4]
        jne ide_not_popuplights
        mov eax, 737468h              // "hts\0"
        cmp eax, DWORD PTR [edx + 8]
        jne ide_not_popuplights
        xor ecx, ecx
        cmp DWORD PTR [zpatch_popuplights_models], ecx
        je ide_popuplights_append
        mov esi, DWORD PTR [edi + 3Ch]
        xor eax, eax
        __emit 0x66
        __emit 0x90                   // nop (the original's two-byte form)
    ide_popuplights_walk:
        cmp DWORD PTR [eax + zpatch_popuplights_models], esi
        je ide_not_popuplights
        add eax, 4
        inc ecx
        cmp eax, 7FCh
        ja ide_done
        cmp DWORD PTR [eax + zpatch_popuplights_models], 0
        jne ide_popuplights_walk
    ide_popuplights_append:
        mov eax, DWORD PTR [edi + 3Ch]
        mov DWORD PTR [ecx*4 + zpatch_popuplights_models], eax
    ide_not_popuplights:
        mov eax, 65726973h            // "sire"
        cmp eax, DWORD PTR [edx]
        jne ide_not_siren
        mov eax, 6Eh                  // "n\0"
        cmp ax, WORD PTR [edx + 4]
        jne ide_not_siren
        xor ecx, ecx
        cmp DWORD PTR [zpatch_copsirens_models], ecx
        je ide_siren_append
        mov esi, DWORD PTR [edi + 3Ch]
        xor eax, eax
        __emit 0x0F
        __emit 0x1F
        __emit 0x00                  // nop dword ptr [eax]
    ide_siren_walk:
        cmp DWORD PTR [eax + zpatch_copsirens_models], esi
        je ide_not_siren
        add eax, 4
        inc ecx
        cmp eax, 7FCh
        ja ide_done
        cmp DWORD PTR [eax + zpatch_copsirens_models], 0
        jne ide_siren_walk
    ide_siren_append:
        mov eax, DWORD PTR [edi + 3Ch]
        mov DWORD PTR [ecx*4 + zpatch_copsirens_models], eax
    ide_not_siren:
        push 0Bh
        push OFFSET zpatch_ide_wheelangle_prefix
        push edx
        call DWORD PTR [zpatch_resolver_strnicmp]
        add esp, 0Ch
        test eax, eax
        jne ide_done
        push eax                      // endptr = the null the compare left in EAX
        mov eax, DWORD PTR [zpatch_ide_string]
        add eax, 0Bh
        push eax
        call DWORD PTR [zpatch_ide_strtof]
        mov esi, DWORD PTR [zpatch_ide_model]
        xor ecx, ecx
        add esp, 8
        fstp DWORD PTR [ebp - 4]
        cmp DWORD PTR [zpatch_wheelangle_models], ecx
        je ide_wheelangle_append
        mov edx, DWORD PTR [esi + 3Ch]
        xor eax, eax
        __emit 0x66
        __emit 0x0F
        __emit 0x1F
        __emit 0x44
        __emit 0x00
        __emit 0x00                  // nop word ptr [eax+eax]
    ide_wheelangle_walk:
        cmp DWORD PTR [eax + zpatch_wheelangle_models], edx
        je ide_done
        add eax, 4
        inc ecx
        cmp eax, 7FCh
        ja ide_done
        cmp DWORD PTR [eax + zpatch_wheelangle_models], 0
        jne ide_wheelangle_walk
    ide_wheelangle_append:
        movss xmm0, DWORD PTR [ebp - 4]
        mov eax, DWORD PTR [esi + 3Ch]
        mov DWORD PTR [ecx*4 + zpatch_wheelangle_models], eax
        movss DWORD PTR [ecx*4 + zpatch_wheelangle_angles], xmm0
    ide_done:
        pop edi
        pop esi
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10005F70, recovered VA 0x10005F70 (191 instructions, 750 bytes): the first
// siren check, transcribed instruction for instruction.
//
// Thirteen built-in police model names are resolved through the original's own
// resolver (sub_10003E50) and the game's model-index lookup, in the original's order;
// the first match sets the flag and leaves through the shared epilogue. Otherwise the
// IDE parser's +siren list is walked: `push dword ptr [edi + 0x10159380]` reads slot N
// of the 512-word table as a base-plus-displacement absolute, `cmove` keeps the flag
// byte the walk read, and the loop ends at the first zero word or past slot 511
// (`cmp edi, 7FCh`). The entry check reads the same table's first word as a plain
// absolute.
//
// The /GS frame is the original's, and the 3-byte `nop dword ptr [eax]` at
// 0x100061ED (0F 1F 00) is emitted as bytes so the assembler cannot choose another
// padding. No exception frame, no TLS static-init guard: nothing blocks the
// transcription.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void CopSirensCheckABody()
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
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_police
        mov DWORD PTR [zpatch_copsirens_flag], 0
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_police2
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_police3
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_police4
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_noose
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_pstockade
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_nstockade
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_polpatriot
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_fbi
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_n_police
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_n_police2
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_n_polpatriot
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        mov esi, DWORD PTR [zpatch_copsirens_index]
        push OFFSET zpatch_copsirens_n_pstockade
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], esi
        je copsirens_a_hit
        cmp DWORD PTR [zpatch_copsirens_models], 0
        je copsirens_a_no_walk
        push ebx
        push edi
        xor edi, edi
        lea ebx, [edi + 1]
        // The original's loop alignment: 0F 1F 00 (`nop dword ptr [eax]`).
        __emit 0x0F
        __emit 0x1F
        __emit 0x00
    copsirens_a_walk:
        mov esi, DWORD PTR [zpatch_copsirens_index]
        lea eax, [ebp - 8]
        push eax
        push DWORD PTR [edi + zpatch_copsirens_models]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        call DWORD PTR [zpatch_model_lookup]
        mov eax, DWORD PTR [zpatch_copsirens_flag]
        add esp, 8
        cmp DWORD PTR [ebp - 8], esi
        cmove eax, ebx
        add edi, 4
        mov DWORD PTR [zpatch_copsirens_flag], eax
        cmp edi, 7FCh
        ja copsirens_a_walk_done
        cmp DWORD PTR [edi + zpatch_copsirens_models], 0
        jne copsirens_a_walk
    copsirens_a_walk_done:
        pop edi
        pop ebx
    copsirens_a_no_walk:
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    copsirens_a_hit:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        mov DWORD PTR [zpatch_copsirens_flag], 1
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10006260, recovered VA 0x10006260 (53 instructions, 174 bytes): the second
// siren check, transcribed instruction for instruction.
//
// It resolves "police2" through the original's own resolver (sub_10003E50) and the
// game's model-index lookup, and answers flag 1 when the index matches. Otherwise it
// walks the IDE parser's +siren list: `push dword ptr [esi + 0x10159380]` reads slot
// N of the 512-word table, an absolute *with* a base register and no index -- the
// shape the table registry canonicalises as `[esi+table:<hash>]`, with the base
// register still compared (docs/rebuilt.md Â§2d). The walk stops at the first
// zero word (`cmp dword ptr [esi+0x10159380], 0`), and the check before the loop
// reads the same table's first word as a plain absolute.
//
// Its /GS frame is the original's: the cookie is an absolute slot and the check a
// call slot. The body has no exception frame and no TLS static-init guard, so
// nothing blocks a transcription.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void CopSirensCheckBBody()
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
        push edi
        mov edi, DWORD PTR [zpatch_copsirens_index_b]
        push OFFSET zpatch_copsirens_police2
        mov DWORD PTR [zpatch_copsirens_flag_b], 0
        call ResolveModelName
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        add esp, 0Ch
        cmp DWORD PTR [ebp - 8], edi
        je sirens_hit
        cmp DWORD PTR [zpatch_copsirens_models], 0
        je sirens_done
        xor esi, esi
    sirens_walk:
        lea eax, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push eax
        push DWORD PTR [esi + zpatch_copsirens_models]
        call DWORD PTR [zpatch_model_lookup]
        add esp, 8
        cmp DWORD PTR [ebp - 8], edi
        je sirens_hit
        add esi, 4
        cmp esi, 7FCh
        ja sirens_done
        cmp DWORD PTR [esi + zpatch_copsirens_models], 0
        jne sirens_walk
        pop edi
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    sirens_hit:
        mov DWORD PTR [zpatch_copsirens_flag_b], 1
    sirens_done:
        mov ecx, DWORD PTR [ebp - 4]
        pop edi
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10006310, recovered VA 0x10006310 (8 instructions): the first siren shim.
// It stores the site's model index (EDX), runs the check between pusha/pushfd and
// popa/popfd, and resumes with EAX = the flag, which the site's `shl al, 6;
// xor al, [esi+0F6Fh]` continues on. pusha/popa are emitted as bytes: the inline
// assembler renders both with a 0x66 operand-size prefix.
static __declspec(naked) void CopSirensShimABody()
{
    __asm
    {
        mov DWORD PTR [zpatch_copsirens_index], edx
        __emit 0x60  // pusha
        __emit 0x9C  // pushfd
        call CopSirensCheckABody
        __emit 0x9D  // popfd
        __emit 0x61  // popa
        mov eax, DWORD PTR [zpatch_copsirens_flag]
        jmp DWORD PTR [zpatch_copsirens_resume]
    }
}

// sub_10006330, recovered VA 0x10006330 (8 instructions): the second siren shim.
// It is installed at the anchor's +22 over the first five bytes of a six-byte
// `cmp`, and its own `cmp [flag], 1` is what the resumed `je` at the anchor's +28
// consumes; the replaced comparison's sixth byte is dead.
static __declspec(naked) void CopSirensShimBBody()
{
    __asm
    {
        mov DWORD PTR [zpatch_copsirens_index_b], eax
        __emit 0x60  // pusha
        __emit 0x9C  // pushfd
        call CopSirensCheckBBody
        __emit 0x9D  // popfd
        __emit 0x61  // popa
        cmp DWORD PTR [zpatch_copsirens_flag_b], 1
        jmp DWORD PTR [zpatch_copsirens_resume_b]
    }
}

// sub_100040E0, recovered VA 0x100040E0 (9 instructions): the shared IDE hook.
// After the payload it replays the site's `mov ecx,7; xor edx,edx; repe cmpsb`
// (the in-place copies are skipped by the resume at the anchor's -1), so the
// resumed `jne` still sees the compare's flags. `repe cmpsb` is emitted as bytes
// so the assembler cannot choose another form.
static __declspec(naked) void IdeHookBody()
{
    __asm
    {
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_ide_string], edi
        mov DWORD PTR [zpatch_ide_model], eax
        call IdeParseBody
        __emit 0x61  // popa
        mov ecx, 7
        xor edx, edx
        __emit 0xF3
        __emit 0xA6  // repe cmpsb
        jmp DWORD PTR [zpatch_ide_hook_slot]
    }
}

// sub_10044310, recovered VA 0x10044310 (125 instructions, 534 bytes): the
// popup-headlights checker, transcribed instruction for instruction.
//
// It seeds the site's angle slot (dword_101580CC) with pi/2 as a *dword* store
// (`mov dword ptr [0x101580CC], 3FC90FDBh`), asks the game's model store for the
// model the original names by the literal hash 0xEB9F21D3 (`push 0EB9F21D3h` at
// 0x1004432E), and answers flag 1 when that is the site's model word. Otherwise it
// walks the +popuplights list the IDE payload filled: the same lookup per slot, a
// bounded `cmp edi,7FCh` walk that ends at the first zero word, and on a match flag 1
// plus the column scan below. The lookup slot is unguarded, as the original's
// `call dword_10159C0C` is.
//
// The column scan is the reason this body needed the table registry extended: it
// reads the [PopupLightAngles] tables as **eight interleaved columns** of 64 rows,
// `mov ecx,ds:[eax+0x1015C628]` through `[eax+0x1015C644]` and the same eight at
// 0x1015BE28, with EAX stepping 0x20 (`add eax,20h` / `cmp eax,800h` / `jl`). Seven
// of each eight displacements are therefore four bytes into the table rather than at
// its base; the registry resolves a displacement inside a registered table to that
// table plus the offset, and the offset is compared (docs/rebuilt.md Â§2f). Each column
// keeps the last matching row's angle, which is what the original's in-place
// overwrite does.
//
// The /GS frame is the original's (cookie slot, check call slot). The two loop
// paddings are emitted as bytes because the inline assembler picks its own:
// `66 0F 1F 84 00 00 00 00 00` at 0x10044387 and `0F 1F 80 00 00 00 00` at
// 0x100443E9. No exception frame, no TLS static-init guard.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void PopupHeadlightsCheckBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        lea eax, [ebp - 8]
        mov DWORD PTR [zpatch_popup_angle], 3FC90FDBh   // pi/2, the .data seed
        push eax
        push 0EB9F21D3h
        mov DWORD PTR [zpatch_popup_flag], 0
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        call DWORD PTR [zpatch_model_lookup]
        mov eax, DWORD PTR [ebp - 8]
        add esp, 8
        mov DWORD PTR [zpatch_popup_index], eax
        cmp eax, DWORD PTR [zpatch_popup_model]
        jne popup_walk
        mov DWORD PTR [zpatch_popup_flag], 1
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    popup_walk:
        push esi
        xor esi, esi
        cmp DWORD PTR [zpatch_popuplights_models], esi
        je popup_no_list
        push edi
        xor edi, edi
        __emit 0x66
        __emit 0x0F
        __emit 0x1F
        __emit 0x84
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00                  // nop word ptr [eax+eax]
    popup_search_loop:
        lea eax, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push eax
        push DWORD PTR [edi + zpatch_popuplights_models]
        call DWORD PTR [zpatch_model_lookup]
        mov eax, DWORD PTR [zpatch_popup_model]
        add esp, 8
        cmp eax, DWORD PTR [ebp - 8]
        je popup_search_hit
        add edi, 4
        inc esi
        cmp edi, 7FCh
        ja popup_walk_done
        cmp DWORD PTR [edi + zpatch_popuplights_models], 0
        jne popup_search_loop
        pop edi
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    popup_search_hit:
        mov DWORD PTR [zpatch_popup_flag], 1
        xor eax, eax
        __emit 0x0F
        __emit 0x1F
        __emit 0x80
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00                  // nop dword ptr [eax]
    popup_column_0:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes]
        test ecx, ecx
        je popup_column_1
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_1
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_1:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 4]
        test ecx, ecx
        je popup_column_2
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_2
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 4]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_2:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 8]
        test ecx, ecx
        je popup_column_3
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_3
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 8]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_3:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 0Ch]
        test ecx, ecx
        je popup_column_4
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_4
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 0Ch]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_4:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 10h]
        test ecx, ecx
        je popup_column_5
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_5
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 10h]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_5:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 14h]
        test ecx, ecx
        je popup_column_6
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_6
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 14h]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_6:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 18h]
        test ecx, ecx
        je popup_column_7
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_7
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 18h]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_7:
        mov ecx, DWORD PTR [eax + zpatch_popup_angle_hashes + 1Ch]
        test ecx, ecx
        je popup_column_done
        cmp DWORD PTR [esi*4 + zpatch_popuplights_models], ecx
        jne popup_column_done
        movss xmm0, DWORD PTR [eax + zpatch_popup_angle_values + 1Ch]
        movss DWORD PTR [zpatch_popup_angle], xmm0
    popup_column_done:
        add eax, 20h
        cmp eax, 800h
        jl popup_column_0
    popup_walk_done:
        pop edi
    popup_no_list:
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10044530, recovered VA 0x10044530 (10 instructions): the EAX site's shim.
// It stores the site's model word, runs the check between pusha/popa, then takes
// the site's second branch when the model is a popuplights model, or falls into
// the anchor's +10 short jump (which the arm rewrites to unconditional) with the
// site's own `cmp eax, [index]` replayed first. pusha/popa are emitted as bytes
// because the inline assembler adds a 0x66 prefix.
static __declspec(naked) void PopupHeadlightsBodyA()
{
    __asm
    {
        movsx eax, WORD PTR [esi + 2Eh]
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_popup_model], eax
        call PopupHeadlightsCheckBody
        __emit 0x61  // popa
        cmp DWORD PTR [zpatch_popup_flag], 1
        je popup_body_a_resume_a
        cmp eax, DWORD PTR [zpatch_popup_index]
        jmp DWORD PTR [zpatch_popup_resume_b]
    popup_body_a_resume_a:
        jmp DWORD PTR [zpatch_popup_resume_a]
    }
}

// sub_10044560, recovered VA 0x10044560 (10 instructions): the EDX twin for the
// second site pattern, absent on 1.0.8.0 but present on the builds the key was
// gated for.
static __declspec(naked) void PopupHeadlightsBodyB()
{
    __asm
    {
        movsx edx, WORD PTR [esi + 2Eh]
        __emit 0x60  // pusha
        mov DWORD PTR [zpatch_popup_model], edx
        call PopupHeadlightsCheckBody
        __emit 0x61  // popa
        cmp DWORD PTR [zpatch_popup_flag], 1
        je popup_body_b_resume_a
        cmp edx, DWORD PTR [zpatch_popup_index]
        jmp DWORD PTR [zpatch_popup_resume_b]
    popup_body_b_resume_a:
        jmp DWORD PTR [zpatch_popup_resume_a]
    }
}
