// SPDX-License-Identifier: GPL-3.0-or-later
//
// stability
//
// One function per INI arm, in the original loader order. The bodies are the
// readable reconstruction's verified arms; only the wrapper and the arm trace
// are ours.

module;

#include "common.hxx"
#include "hooks.h"
#include "rebased.h"
#include "opcodes.hxx"

#include "data/ooc_payload.h"
export module opp.stability;

import opp.pipeline;
import opp.patch;
import opp.game;
import opp.common;
import opp.settings;
import opp.log;

// ---------------------------------------------------------------------------
// Hook bodies: the machine-level entry points stability installs.
//
// Each body keeps the original site's calling convention and register
// contract; the comment above it names the original destination VA. They are
// 32-bit only (the x64 parity dump resolves names to the original VAs).
// ---------------------------------------------------------------------------
#if !defined(_WIN64)

using namespace opp;

#include "body_helpers.inc"

extern "C"
{


// PlayerAsPedComponentsFix helper (original sub_1003D930): compare the object's
// model (+0x2E) against the "player" model index. The shim stores the object
// (EAX) first; C linkage so the shim's asm can call it by name.

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
        // The model info dword at object+0x3C is the model's name hash
        // (sub_10037AC0 passes it to the name resolver).
        constexpr size_t kBpmModelHashOffset = 0x3C;
        const uint32_t modelHash =
            *reinterpret_cast<const uint32_t*>(
                static_cast<const uint8_t*>(object) + kBpmModelHashOffset);
        const char* model_name = zpatch_bpm_model_name(modelHash);
        uint32_t entity = zpatch_bpm_obj;
        const char* who = reinterpret_cast<const char*>(static_cast<uintptr_t>(
            zpatch_script_call(zpatch_salt_c8, 1, &entity)));
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s used a non-ped model (%s)", who,
                      model_name);
        const uint32_t text = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(buffer));
        zpatch_script_call(kPrintScriptHash, 1, &text);
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
    // GarageLookupMemo: the hash -> index mapping is static after startup, so a
    // successful resolution is remembered (direct-mapped, a collision just
    // re-resolves). Negative results are never cached: a model that is not
    // loaded yet must still be found later.
    struct GarageMemo final
    {
        uint32_t hash;
        int32_t index;
    };
    static GarageMemo memo[64] = {};
    const size_t memo_slot = (hash * 2654435761u) >> 26;
    int32_t out = -1;
    if (memo[memo_slot].hash == hash && memo[memo_slot].index > 0)
    {
        out = memo[memo_slot].index;
    }
    else
    {
        lookup(hash, &out);
        if (out > 0)
        {
            memo[memo_slot].hash = hash;
            memo[memo_slot].index = out;
        }
    }
    if (out <= 0 || out >= 32000)
    {
        // The replacement model is a constant hash, so its index is resolved
        // once; the original looks it up on every miss.
        constexpr uint32_t kFallbackHash = 0xC703DB5Fu;
        static int32_t fallback_index = -1;
        if (fallback_index < 0)
            lookup(kFallbackHash, &fallback_index);
        out = fallback_index > 0 ? fallback_index : -1;
        *reinterpret_cast<int16_t*>(object + 16) = static_cast<int16_t>(out);
        // Clear bit 0 of the flags word at +48: the original's "model was
        // already replaced" marker.
        constexpr uint16_t kModelReplacedMask = 0xFFFEu;
        *reinterpret_cast<uint16_t*>(object + 48) &= kModelReplacedMask;
        return 65534;
    }
    *reinterpret_cast<int16_t*>(object + 16) = static_cast<int16_t>(out);
    return out;
}
}  // namespace
static void zpatch_garage_store_model();
// Field map (offsets recovered from the original sub_10046C80):
//   [edx+0x2E]  the model index (int16)
//   [eax+0x3C]  the model info dword unpacked into the four id bytes
//   [esi+0x13]  id byte 0 (low)
//   [esi+0x40]  id byte 1 (bits 8..15)
//   [esi+0x41]  id byte 2 (bits 16..23)
//   [esi+0x46]  id byte 3 (bits 24..31)
static __declspec(naked) void zpatch_garage_store_model()
{
    __asm {
        cmp  BYTE PTR [zpatch_citizen_present], 0
        push esi
        mov  esi, ecx
        jne  garage_done
        cmp  DWORD PTR [zpatch_pre_ped_array], 0
        je   garage_done
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
static int32_t zpatch_garage_lookup_model();
static __declspec(naked) int32_t zpatch_garage_lookup_model()
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

// @exact 0x10005590 AimProjectileCrashFix: __thiscall (this in ECX), three
// stack arguments, callee pops 12 bytes; MSVC rejects __thiscall on free
// functions, so the original instruction sequence is reproduced directly.
// Field map: [ecx+0x20] = arg2, [ecx+0x24] = arg3, [ecx+0x1c] = arg1 masked by
// the sign of arg3 (the original's `neg/sbb/and`); [esp+8]/[esp+0xC]/[esp+4]
// are those three stack arguments.
static __declspec(naked) void abi_aim_projectile_crash_fix()
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

// @exact 0x10005ed0 CombinedRadioFreezeFix: mov eax,13h / ret.
int __cdecl abi_combined_radio_freeze_fix() { return 19; }

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

// Crash-fix shims, transcribed from the original bodies' machine code: the C
// translation hid argument pushes, register setup and flags the resumed code
// consumes. Registers the resumed code depends on (AL, DL, flags) must survive
// intact; each shim ends in a tail jump to the published resume.

// @exact 0x1003d7b0 PathNodeCrashFix: eax = node index; the sentinel is
// 0x1FFFDF. Otherwise dl = [eax+0x1F] and its low bit feed the failure resume.
static __declspec(naked) void abi_path_node_shim()
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
static __declspec(naked) void abi_plane_crash_shim()
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
static __declspec(naked) void abi_wanted_level_shim()
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
// tests the flags of [ecx] against 0. The 0xC bound is the group table's
// twelve-byte stride check the original performs before that test.
static __declspec(naked) void abi_group_hack_shim()
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

// @exact 0x10046750 SMPA60Fix: ecx = this; the sentinel 0xDDDDDDDD (the MSVC
// debug-CRT poison value the original compares against) tails into the match+28
// resume. The normal path pushes five arguments for the match+14 resume:
// [eax+0x70] (a pointer), eax+0x40 (the sub-object), [ecx+0x10], ecx, ecx.
static __declspec(naked) void abi_smpa60_shim()
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
// @exact 0x10046780 SMPA60Fix tail: copies the dword at [eax+0x10] into
// [[eax+0x14]+0x10] unless either pointer is null or the 0xCDCDCDCD debug-heap
// poison value.
static __declspec(naked) void abi_smpa60_tail_shim()
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

// @exact 0x1003d9a0 PlayerAsPedComponentsFix: save eax, run the helper (stores
// 1/0 in zpatch_player_ped_computed), restore, then push the result and ecx and
// tail-jump to the resume (match+14).
static __declspec(naked) void abi_player_ped_components_shim()
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

// @exact 0x10046C80 GarageModelFix body A (installed at game 0xC68BB0): pushad;
// ecx=object(esi), edx=vehicle(edi); call the store helper; popad; replay
// mov edx,[edi+0x20]; fld [edx+0x34]; tail-jump to the resume (match+6).
[[maybe_unused]] static __declspec(naked) void abi_garage_body_a()
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
static __declspec(naked) void abi_garage_entry_a()
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
static __declspec(naked) void abi_garage_body_b()
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

// @exact 0x10037BA0 BadPlayerModelCrashFix trampoline: the handler E9s the game
// function at pattern "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C" (game 0x7CF803,
// 3 bytes in) and publishes match+0xA. The body captures [ebp+0xC] (entity) and
// [ebp+0x10] (model index), runs the check between pushad/popad, writes the
// corrected model index back to [ebp+0x10], then replays the overwritten
// prologue (and esp,0xF0; sub esp,0x54; xorps xmm0,xmm0; push ebx) and tails.
// EBP is the game function's live frame.
static __declspec(naked) void abi_bad_player_model()
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

// @exact 0x100469E0 SuperLODFix: not byte-exact (calls a compiled helper). The
// fail path resumes via dword_101580AC, the ok path via dword_101580B0; both are
// handler-published base+rva values, build-gated.
static __declspec(naked) void abi_super_lod_fix_shim()
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
        test byte ptr [eax + 0x72], 0x10  ; bit 4 of the byte at +0x72 = has LOD
        je slod_fail
    slod_ok:
        jmp DWORD PTR [zpatch_super_lod_ok]
    }
}

// @exact 0x10036F00 CrashMsgFix: calls the game's message getter through the
// handler-published pointer (the site's original call target, *(match-4)) and
// replaces every '~' with a space. Entered via the handler's E8, so it returns
// normally. The original's two nops are omitted (not byte-exact).
static __declspec(naked) void abi_crash_msg_shim()
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

// BenchmarkFix: two match-site shims (E9 at match[0]/[1]+8). Each checks eax,
// runs the interposed callback (esi with edi), then tails to the published
// resume, or unwinds via the enclosing function's tail when eax is null.
// @exact 0x10005650
static __declspec(naked) void abi_benchmark_a()  // 0x10005650
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
static __declspec(naked) void abi_benchmark_b()  // 0x10005670
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

// @exact 0x1001b040 EpisodeSwitchFreezeFix: [ebx+0x20] is the episode-load
// object slot; stores eax there, null returns 0 (pop edi/esi/ebx, ret 0x10),
// else tails to match+5.
static __declspec(naked) void abi_episode_switch_store_shim()
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
// @exact 0x1001b060: [ebx+0x10] and [ebx+0x14] are the load object's
// begin/end pointers the original compares.
static __declspec(naked) void abi_episode_switch_shim_b()
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
// @exact 0x1001b0a0: [eax] is the vtable, [eax+4] the first argument, and
// vtable+0x30 the virtual the original calls.
static __declspec(naked) void abi_episode_switch_vcall()
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
// @exact 0x1001b0c0: [esi+0x14] is the pointer the resume tests for null.
static __declspec(naked) void abi_episode_switch_shim_c()
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

// @exact 0x10005d80 CollisionDeadlockFix: full replacement of the node-walk
// function the handler matches (prologue 55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48
// 66, rewritten with E9). The per-node predicate published in dword_1015BDD4 is
// NOT __stdcall: it takes the aligned entry pointer in EAX, a2 in EDX and the
// shift mask on the stack, and pops its argument (ret 4). The original also
// restores esp from ebp, discarding the predicate's cleanup. The iteration
// counter (dword_1015BDD8, zpatch_collision_deadlock_iterations) bounds the walk
// at 10000 steps.
// Field map: [ebp+0x8] = the graph, [ebp+0xC] = the start entry (its uint16
// index at +0x66), [graph+0x70] = the node table; a node is 8 bytes (uint16
// next at +0, pointer at +4 with its low 4 bits as flags); 0xFFFF is the
// end-of-chain index and 0xFFFFFFF0 the pointer mask. The 0x2710 comparison is
// the recovered 10 000-step iteration bound.
static __declspec(naked) void abi_collision_deadlock_walk()
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
        cmp DWORD PTR [zpatch_collision_deadlock_iterations], 10000  ; the recovered 10 000-step bound
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
static __declspec(naked) void abi_blackscreen_shim()
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

// @exact 0x1001b080 EpisodeSwitchFreezeFix sum: __thiscall, no stack arguments,
// returns [ecx+10h] + [ecx+0Ch] (0 when this is null).
static __declspec(naked) void abi_episode_switch_sum()
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
    // The pak trailer is the 5 (end-of-stream opcode) and 0xFA (pack marker)
    // byte pair, followed by the big-endian Adler-32 the fixture stores.
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
    if (archive == nullptr)
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
    // The builder's header: pak magic 0x86723DFF, version 3, one entry, header
    // size 0x10 and the 0xE9 flag word (the format's fixed layout).
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
        // Bits 0..10 hold the padding; bits 11..15 are zero in this format
        // (the mask documents the field width the original packs).
        constexpr uint16_t kPaddingMask = 0xF800u;
        const uint16_t packed =
            static_cast<uint16_t>((static_cast<uint16_t>(0u) & kPaddingMask) | padding);
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
    if (cached_archive != 0u)
    {
        zpatch_ooc_store(&zpatch_ooc_state, cached_state);
        return cached_archive;
    }

    auto* table = static_cast<unsigned char*>(std::malloc(kOocTableSize));
    if (table == nullptr) return 0;
    const bool decoded = zpatch_ooc_decode(table);
    if (!decoded)
    {
        std::free(table);
        return 0;
    }
    zpatch_ooc_store(&zpatch_ooc_table_size,
                     static_cast<uint32_t>(kOocTableSize));
    zpatch_ooc_register_entry(table);
    uint32_t final_offset = 0;
    unsigned char* archive = zpatch_ooc_build_archive(table, final_offset);
    if (archive == nullptr) return 0;
    zpatch_ooc_store(&zpatch_ooc_vector_end,
                     zpatch_ooc_vector_begin);
    zpatch_ooc_store(&zpatch_ooc_state, final_offset);
    cached_archive = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(archive));
    cached_state = final_offset;
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
    // The fixture's expected bytes: header size 0x10, then the 0xE9 flag word.
    header_ok = header_ok && archive[16] == 0x10u && archive[17] == 0u &&
                archive[18] == 0xE9u && archive[19] == 0u;
    const uint32_t record_size = 0x2BD50u;
    const uint32_t record_index = 1u;
    bool record_ok = std::memcmp(archive + 20, &record_size, 4) == 0;
    const uint32_t zero = 0u;
    record_ok = record_ok && std::memcmp(archive + 24, &zero, 4) == 0 &&
                std::memcmp(archive + 28, &record_index, 4) == 0;
    // The fixture's packed page count (88 = 0x58) and the 0x02B0 tail word.
    record_ok = record_ok && archive[32] == 88u && archive[33] == 0u &&
                archive[34] == 0xB0u && archive[35] == 0x02u;
    const bool name_ok = std::memcmp(archive + 36, "finale2.sco", 12) == 0;
    const bool ok = decoded && final_offset == 182272u && table_adler == 0x41E794ABu &&
                    header_ok && record_ok && name_ok &&
                    // The fixture's archive FNV-1a and packed length.
                    zpatch_ooc_fnv1a(archive, final_offset) == 0xB5AF7B10u;
    std::free(archive);
    return ok;
}

// The fake SCO pak the fix registers: magic 0x86723DFF, version 3, one entry,
// header size 0x10 with the 0xE9 flag word; the recorded entry keeps the packed
// size 0x2BD50 and the 12-byte name "finale2.sco". The two checksums
// (0x41E794AB table Adler, 0xB5AF7B10 archive FNV-1a) are the fixture's own
// values, produced by this builder.
// @exact 0x1004C3C0 OutOfCommissionFix branch body (E9 target of game
// 0xA22E97): replay `cmp [esp+0x30], eax` + `movq xmm0, [esp+0x30]`; when the
// comparison was unequal, re-compare against the original's 0x86723DFF magic so
// the game's following `jz` also accepts the injected value; resume at match+0x11.
static __declspec(naked) void abi_ooc_branch()
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
        // The original formats into a 68-byte stack buffer with a 0x40 size
        // argument; 0x3A/0x24/0x30 are the literal ':', '$' and '0' characters
        // its format string consumes (recovered logger call).
        constexpr size_t kOocLogBufferBytes = 0x44;
        char buffer[kOocLogBufferBytes];
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
}  // extern "C"


#endif  // !defined(_WIN64)

namespace opp::features
{

    // AimProjectileCrashFix

    void InstallAimProjectileCrashFix(PatchContext& ctx)
    {

        TraceArm("AimProjectileCrashFix");

        // AimProjectileCrashFix: 1 sigjump
        if (ctx.Option("AimProjectileCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 D8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_aim_projectile_crash_fix));
            }
        }

    }


    // publish BadPlayerModelCrashFix

    void PublishBadPlayerModelCrashFix(PatchContext& ctx)
    {

        TraceArm("publish BadPlayerModelCrashFix");

        // BadPlayerModelCrashFix: 1 slot
        if (ctx.Option("BadPlayerModelCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C"); pattern.size() > 0)
            {
                zpatch_bpm_resume = MatchAddress(pattern, 10);
            }
        }

    }


    // BadPlayerModelCrashFix

    void InstallBadPlayerModelCrashFix(PatchContext& ctx)
    {

        TraceArm("BadPlayerModelCrashFix");

        // BadPlayerModelCrashFix: 1 siginstall, 1 sigjump
        if (ctx.Option("BadPlayerModelCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_bad_player_model));
            }
        }

    }


    // publish BenchmarkFix

    void PublishBenchmarkFix(PatchContext& ctx)
    {

        TraceArm("publish BenchmarkFix");

        // BenchmarkFix: 2 slots
        if (ctx.Option("BenchmarkFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20"); pattern.size() > 0)
            {
                zpatch_benchmark_resume_a = MatchAddress(pattern, 13);
                zpatch_benchmark_resume_b = MatchAddress(pattern, 13);
            }
        }

    }


    // BenchmarkFix

    void InstallBenchmarkFix(PatchContext& ctx)
    {

        TraceArm("BenchmarkFix");

        // BenchmarkFix: 2 matchjumps
        if (ctx.Option("BenchmarkFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (BuildIs(ctx.build,
                        {GameBuild::v1000, GameBuild::v1040, GameBuild::v1004, GameBuild::v1060,
                         GameBuild::v1070, GameBuild::v1061, GameBuild::v1100, GameBuild::v1110,
                         GameBuild::v1120, GameBuild::v12043}))
            {

                {
                    // The original patches exactly the two benchmark entries
                    // (bodies a and b); a different count is a view mismatch, so the
                    // arm installs nothing.
                    if (auto pattern = PatternAll(image, "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20"); pattern.size() > 0 &&
                        ExpectMatchCount(pattern, 2, "BenchmarkFix entries"))
                    {
                        JumpTo(ctx, pattern.get(0).get<uint8_t>(8), OPP_BODY(abi_benchmark_a));
                        JumpTo(ctx, pattern.get(1).get<uint8_t>(8), OPP_BODY(abi_benchmark_b));
                    }
                }
            }
        }

    }


    // publish BlackscreenFix

    void PublishBlackscreenFix(PatchContext& ctx)
    {

        TraceArm("publish BlackscreenFix");

        // BlackscreenFix: 2 slots
        if (ctx.Option("BlackscreenFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "75 54 8B 48 28 8B 40 2C"); pattern.size() > 0)
            {
                zpatch_blackscreen_resume = MatchTarget(pattern, 82, 86);
                const auto address = static_cast<uintptr_t>(MatchTarget(pattern, 82, 87));
                if (InImage(image, address))
                    zpatch_blackscreen_value_ptr = *reinterpret_cast<const uint32_t*>(address);
            }
        }

    }


    // BlackscreenFix

    void InstallBlackscreenFix(PatchContext& ctx)
    {

        TraceArm("BlackscreenFix");

        // BlackscreenFix: 1 sigjump
        if (ctx.Option("BlackscreenFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "75 54 8B 48 28 8B 40 2C"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match + 81, OPP_BODY(abi_blackscreen_shim));
            }
        }

    }


    // publish CollisionDeadlockFix

    void PublishCollisionDeadlockFix(PatchContext& ctx)
    {

        TraceArm("publish CollisionDeadlockFix");

        // CollisionDeadlockFix: 1 slot
        if (ctx.Option("CollisionDeadlockFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66"); pattern.size() > 0)
            {
                zpatch_collision_deadlock_should_walk = MatchTarget(pattern, 91, 95);
            }
        }

    }


    // CollisionDeadlockFix

    void InstallCollisionDeadlockFix(PatchContext& ctx)
    {

        TraceArm("CollisionDeadlockFix");

        // CollisionDeadlockFix: 1 sigjump
        if (ctx.Option("CollisionDeadlockFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_collision_deadlock_walk));
            }
        }

    }


    // CombinedRadioFreezeFix

    void InstallCombinedRadioFreezeFix(PatchContext& ctx)
    {

        TraceArm("CombinedRadioFreezeFix");

        // CombinedRadioFreezeFix: 1 siginstall
        if (ctx.Option("CombinedRadioFreezeFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallTo(ctx, match, OPP_BODY(abi_combined_radio_freeze_fix));
            }
        }

    }


    // publish CrashMsgFix

    void PublishCrashMsgFix(PatchContext& ctx)
    {

        TraceArm("publish CrashMsgFix");

        // CrashMsgFix: 1 slot
        if (ctx.Option("CrashMsgFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 6A 10 4E 31"); pattern.size() > 0)
            {
                const auto site = static_cast<uintptr_t>(*pattern.get(0).get<uint32_t>(-4));
                if (InImage(image, site))
                    zpatch_crash_msg_getter = static_cast<uint32_t>(
                                site + ReadDword(reinterpret_cast<const uint8_t*>(site) + 1) + 5);
            }
        }

    }


    // CrashMsgFix

    void InstallCrashMsgFix(PatchContext& ctx)
    {

        TraceArm("CrashMsgFix");

        // CrashMsgFix: 1 siginstall
        if (ctx.Option("CrashMsgFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "68 6A 10 4E 31"); pattern.size() > 0)
            {
                uint8_t* site = MatchPointer(pattern, -4);
                CallTo(ctx, site, OPP_BODY(abi_crash_msg_shim));
            }
        }

    }


    // publish EpisodeSwitchFreezeFix

    void PublishEpisodeSwitchFreezeFix(PatchContext& ctx)
    {

        TraceArm("publish EpisodeSwitchFreezeFix");

        // EpisodeSwitchFreezeFix: 3 slots
        if (ctx.Option("EpisodeSwitchFreezeFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84"); pattern.size() > 0)
            {
                zpatch_episode_switch_resume_a = MatchAddress(pattern, 5);
            }

            if (auto pattern = Pattern(image, "8B 4B 10 3B 4B 14 7D 0F"); pattern.size() > 0)
            {
                zpatch_episode_switch_resume_b = MatchAddress(pattern, 6);
            }

            if (auto pattern = Pattern(image, "56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05"); pattern.size() > 0)
            {
                zpatch_episode_switch_resume_c = MatchAddress(pattern, 7);
            }
        }

    }


    // EpisodeSwitchFreezeFix

    void InstallEpisodeSwitchFreezeFix(PatchContext& ctx)
    {

        TraceArm("EpisodeSwitchFreezeFix");

        // EpisodeSwitchFreezeFix: 5 sigjumps
        if (ctx.Option("EpisodeSwitchFreezeFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_episode_switch_shim_c));
            }

            if (auto pattern = Pattern(image, "89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_episode_switch_store_shim));
            }

            if (auto pattern = Pattern(image, "8B 41 10 03 41 0C C3"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_episode_switch_sum));
            }

            if (auto pattern = Pattern(image, "8B 4B 10 3B 4B 14 7D 0F"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_episode_switch_shim_b));
            }

            if (auto pattern = Pattern(image, "8B C1 8B 08 8B 11 8B 40 04 8B 52 30 50 FF D2 C3"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_episode_switch_vcall));
            }
        }

    }


    // publish GarageModelFix

    void PublishGarageModelFix(PatchContext& ctx)
    {

        TraceArm("publish GarageModelFix");

        // GarageModelFix: 2 slots
        if (ctx.Option("GarageModelFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 57 20 D9 42 34 D9 5E 04 8B 47 20"); pattern.size() > 0)
            {
                zpatch_garage_resume_a = MatchAddress(pattern, 6);

                // Site A is at the function's mid-body anchor; the hook goes to
                // the prologue, which starts with an 8-byte `movss xmm0, ds:X`,
                // so the resume is just past it and the float to reload is the
                // dword that instruction's operand points at.
                constexpr ptrdiff_t kGarageEntryDelta = -0x20;
                constexpr ptrdiff_t kGarageResumeDelta = kGarageEntryDelta + 8;
                constexpr ptrdiff_t kGarageScaleOperandDelta = kGarageResumeDelta - 4;
                static_assert(kGarageResumeDelta == -0x18);
                static_assert(kGarageScaleOperandDelta == -0x1C);
                zpatch_garage_entry_resume_a = MatchAddress(pattern, kGarageResumeDelta);
                zpatch_garage_scale = *reinterpret_cast<const float*>(
                    static_cast<uintptr_t>(
                        Field(pattern.get(0).get<uint8_t>(), kGarageScaleOperandDelta)));
            }

            if (auto pattern = Pattern(image, "0F B7 0E 50 51 E8"); pattern.size() > 0)
            {
                zpatch_garage_resume_b = MatchAddress(pattern, 5);
            }
        }

    }


    // GarageModelFix

    void InstallGarageModelFix(PatchContext& ctx)
    {

        TraceArm("GarageModelFix");

        // GarageModelFix: 2 siginstalls
        if (ctx.Option("GarageModelFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 57 20 D9 42 34 D9 5E 04 8B 47 20"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // Site A is hooked at the function prologue, 0x20 bytes before
                // the anchor; the body reloads the `xmm0` the skipped
                // instruction sets.
                constexpr ptrdiff_t kGarageEntryDelta = -0x20;
                JumpTo(ctx, match + kGarageEntryDelta, OPP_BODY(abi_garage_entry_a));
            }

            if (auto pattern = Pattern(image, "0F B7 0E 50 51 E8"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_garage_body_b));
            }
        }

    }


    // publish GroupHackFix

    void PublishGroupHackFix(PatchContext& ctx)
    {

        TraceArm("publish GroupHackFix");

        // GroupHackFix: 1 slot
        if (ctx.Option("GroupHackFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04"); pattern.size() > 0)
            {
                zpatch_group_hack_resume = MatchAddress(pattern, 11);
            }
        }

    }


    // GroupHackFix

    void InstallGroupHackFix(PatchContext& ctx)
    {

        TraceArm("GroupHackFix");

        // GroupHackFix: 1 sigjump
        if (ctx.Option("GroupHackFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_group_hack_shim));
            }
        }

    }


    // MPNikoCrashFix

    void InstallMPNikoCrashFix(PatchContext& ctx)
    {

        TraceArm("MPNikoCrashFix");

        // MPNikoCrashFix: 1 literal
        if (ctx.Option("MPNikoCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "74 2B 57 E8 ? ? ? ? 8B BE A8 0E 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match, opp::ops::kJmpRel8);
            }
        }

    }


    // publish OutOfCommissionFix

    void PublishOutOfCommissionFix(PatchContext& ctx)
    {

        TraceArm("publish OutOfCommissionFix");

        // OutOfCommissionFix: 3 slots. The finale archive path exists on
        // 1060-1130; gate it explicitly (12043 has no verified payload).
        if (ctx.Option("OutOfCommissionFix", "1") &&
            BuildIs(ctx.build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                GameBuild::v1080, GameBuild::v1100, GameBuild::v1110,
                                GameBuild::v1120, GameBuild::v1130}))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "A9 39 44 24 18"); pattern.size() > 0)
            {
                zpatch_ooc_resume = MatchAddress(pattern, 17);
            }

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56"); pattern.size() > 0)
            {
                zpatch_ooc_logger = MatchTarget(pattern, -50, -46);
                zpatch_ooc_orig = MatchTarget(pattern, 1, 5);
            }
        }

    }


    // OutOfCommissionFix

    void InstallOutOfCommissionFix(PatchContext& ctx)
    {

        TraceArm("OutOfCommissionFix");

        // OutOfCommissionFix: 2 siginstalls
        if (ctx.Option("OutOfCommissionFix", "1") &&
            BuildIs(ctx.build, {GameBuild::v1060, GameBuild::v1061, GameBuild::v1070,
                                GameBuild::v1080, GameBuild::v1100, GameBuild::v1110,
                                GameBuild::v1120, GameBuild::v1130}))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "A9 39 44 24 18"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match + 7, OPP_BODY(abi_ooc_branch));
            }

            if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                CallTo(ctx, match, OPP_BODY(abi_ooc_wrap));
            }
        }

    }


    // publish PathNodeCrashFix

    void PublishPathNodeCrashFix(PatchContext& ctx)
    {

        TraceArm("publish PathNodeCrashFix");

        // PathNodeCrashFix: 2 slots
        if (ctx.Option("PathNodeCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 04 B1 0F B7 D0 81 FA FF FF 00 00"); pattern.size() > 0)
            {
                zpatch_path_node_resume_ok = MatchAddress(pattern);
                zpatch_path_node_resume_fail = MatchAddress(pattern, 59);
            }
        }

    }


    // PathNodeCrashFix

    void InstallPathNodeCrashFix(PatchContext& ctx)
    {

        TraceArm("PathNodeCrashFix");

        // PathNodeCrashFix: 1 sigjump
        if (ctx.Option("PathNodeCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 04 B1 0F B7 D0 81 FA FF FF 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match + 53, OPP_BODY(abi_path_node_shim));
            }
        }

    }


    // publish PedModelsInCutscenesFix

    void PublishPedModelsInCutscenesFix(PatchContext& ctx)
    {

        TraceArm("publish PedModelsInCutscenesFix");

        // PedModelsInCutscenesFix: 1 slot
        if (ctx.Option("PedModelsInCutscenesFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"); pattern.size() > 0)
            {
                zpatch_model_lookup = MatchAddress(pattern);
            }
        }

    }


    // PedModelsInCutscenesFix

    void InstallPedModelsInCutscenesFix(PatchContext& ctx)
    {

        TraceArm("PedModelsInCutscenesFix");

        // PedModelsInCutscenesFix: 1 sigjump
        if (ctx.Option("PedModelsInCutscenesFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "56 8B F1 E8 ? ? ? ? 85 C0 74 0E 66 8B 40 2E"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_ped_models_in_cutscenes_check));
            }
        }

    }


    // publish PlaneCrashFix

    void PublishPlaneCrashFix(PatchContext& ctx)
    {

        TraceArm("publish PlaneCrashFix");

        // PlaneCrashFix: 2 slots
        if (ctx.Option("PlaneCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00"); pattern.size() > 0)
            {
                zpatch_plane_crash_resume_b = MatchAddress(pattern, 14);
                zpatch_plane_crash_resume_a = MatchAddress(pattern, -705);
            }
        }

    }


    // PlaneCrashFix

    void InstallPlaneCrashFix(PatchContext& ctx)
    {

        TraceArm("PlaneCrashFix");

        // PlaneCrashFix: 1 sigjump
        if (ctx.Option("PlaneCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match + 3, OPP_BODY(abi_plane_crash_shim));
            }
        }

    }


    // publish PlayerAsPedComponentsFix

    void PublishPlayerAsPedComponentsFix(PatchContext& ctx)
    {

        TraceArm("publish PlayerAsPedComponentsFix");

        // PlayerAsPedComponentsFix: 2 slots
        if (ctx.Option("PlayerAsPedComponentsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 54 24 10 52 0F B6 90 19 02 00 00"); pattern.size() > 0)
            {
                zpatch_player_ped_resume = MatchAddress(pattern, 14);
            }

            if (auto pattern = Pattern(image, "8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"); pattern.size() > 0)
            {
                zpatch_model_lookup = MatchAddress(pattern);
            }
        }

    }


    // PlayerAsPedComponentsFix

    void InstallPlayerAsPedComponentsFix(PatchContext& ctx)
    {

        TraceArm("PlayerAsPedComponentsFix");

        // PlayerAsPedComponentsFix: 1 sigjump
        if (ctx.Option("PlayerAsPedComponentsFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 54 24 10 52 0F B6 90 19 02 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match + 5, OPP_BODY(abi_player_ped_components_shim));
            }
        }

    }


    // publish SMPA60Fix

    void PublishSMPA60Fix(PatchContext& ctx)
    {

        TraceArm("publish SMPA60Fix");

        // SMPA60Fix: 3 slots
        if (ctx.Option("SMPA60Fix", "1") && Pattern(ctx.image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 48 70 83 C0 40 50 51 8D 51 10"); pattern.size() > 0)
            {
                zpatch_smpa60_resume_a = MatchAddress(pattern, 28);
                zpatch_smpa60_resume_b = MatchAddress(pattern, 14);
            }

            if (auto pattern = Pattern(image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C"); pattern.size() > 0)
            {
                zpatch_smpa60_resume_c = MatchAddress(pattern, 136);
            }
        }

    }


    // SMPA60Fix

    void InstallSMPA60Fix(PatchContext& ctx)
    {

        TraceArm("SMPA60Fix");

        // SMPA60Fix: 1 literal, 1 siginstall, 1 sigjump
        if (ctx.Option("SMPA60Fix", "1") && Pattern(ctx.image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C").size() > 0)
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                // The A60 branch's conditional jump sits 38 bytes before the
                // anchor and 123 bytes after it is the tail the shim resumes at.
                constexpr ptrdiff_t kSmpa60BranchDelta = -38;
                constexpr ptrdiff_t kSmpa60TailDelta = 123;
                JumpTo(ctx, match + kSmpa60TailDelta, OPP_BODY(abi_smpa60_tail_shim));
                WriteValue<uint8_t>(ctx, match + kSmpa60BranchDelta, opp::ops::kJmpRel8);
            }

            if (auto pattern = Pattern(image, "8B 48 70 83 C0 40 50 51 8D 51 10"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_smpa60_shim));
            }
        }

    }


    // publish SpeedupBugFix

    void PublishSpeedupBugFix(PatchContext& ctx)
    {

        TraceArm("publish SpeedupBugFix");

        // SpeedupBugFix: 8 slots
        if (ctx.Option("SpeedupBugFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "50 64 89 25 00 00 00 00 83 EC 34"); pattern.size() > 0)
            {
                zpatch_speedup_t50 = *pattern.get(0).get<uint32_t>(26);
                zpatch_speedup_t64 = *pattern.get(0).get<uint32_t>(165);
                zpatch_speedup_t60 = *pattern.get(0).get<uint32_t>(188);
                zpatch_speedup_t5c = *pattern.get(0).get<uint32_t>(226);
                zpatch_speedup_t68 = *pattern.get(0).get<uint32_t>(234);
                zpatch_speedup_t4c = *pattern.get(0).get<uint32_t>(249);
                zpatch_speedup_t54 = *pattern.get(0).get<uint32_t>(264);
                zpatch_speedup_t58 = *pattern.get(0).get<uint32_t>(276);
            }
        }

    }


    // SpeedupBugFix

    void InstallSpeedupBugFix(PatchContext& ctx)
    {

        TraceArm("SpeedupBugFix");

        // SpeedupBugFix: 1 siginstall
        if (ctx.Option("SpeedupBugFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "50 64 89 25 00 00 00 00 83 EC 34"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match - 21, OPP_BODY(abi_speedup_body));
            }
        }

    }


    // publish SuperLODFix

    void PublishSuperLODFix(PatchContext& ctx)
    {

        TraceArm("publish SuperLODFix");

        // SuperLODFix: 6 slots
        if (ctx.Option("SuperLODFix", "1"))
        {
            const GameImage& image = ctx.image;

            // The shim's three targets are derived from the arm's own site scan
            // and the lookup function's entry scan: the fail/ok blocks sit 0x4E
            // and 6 bytes past the site on both gated builds (verified on
            // 1.0.7.0 and 1.0.8.0), and the lookup window is unique (proved with
            // tools/opcep_sigcheck.py). The build gate keeps both scans on the
            // build the original patched.
            constexpr uintptr_t kSuperLodFailDelta = 0x4E;
            constexpr uintptr_t kSuperLodOkDelta = 0x6;
            const auto findSite = [&image](const char* signature) -> uint8_t* {
                auto pattern = Pattern(image, signature);
                return pattern.size() > 0 ? pattern.get(0).get<uint8_t>() : nullptr;
            };

            if (ctx.build == GameBuild::v1070)
            {
                if (auto* site = findSite("F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8"))
                {
                    zpatch_super_lod_fail = AddressOf(site + kSuperLodFailDelta);
                    zpatch_super_lod_ok = AddressOf(site + kSuperLodOkDelta);
                }
                if (auto* lookup = findSite("8B 44 24 04 89 44 24 04 66 A1 44 C2 F2 00 66 85 C0 76 3E 8B"))
                    zpatch_super_lod_lookup = AddressOf(lookup);
            }

            if (ctx.build == GameBuild::v1080)
            {
                if (auto* site = findSite("F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8"))
                {
                    zpatch_super_lod_fail = AddressOf(site + kSuperLodFailDelta);
                    zpatch_super_lod_ok = AddressOf(site + kSuperLodOkDelta);
                }
                if (auto* lookup = findSite("8B 44 24 04 89 44 24 04 66 A1 E0 E9 F3 00 66 85 C0 76 3E 8B"))
                    zpatch_super_lod_lookup = AddressOf(lookup);
            }
        }

    }


    // SuperLODFix

    void InstallSuperLODFix(PatchContext& ctx)
    {

        TraceArm("SuperLODFix");

        // SuperLODFix: 1 sigjump. The recovered handler writes the same site on
        // 1.0.7.0 (RVA 0x4A3CE9) and 1.0.8.0 (RVA 0x540E19); the scan is the
        // instruction pair at the site and resolves on both, so the RVA table is
        // gone. The build gate stays: the same bytes exist on other builds and
        // the original only installed it on these two.
        if (ctx.Option("SuperLODFix", "1") &&
            BuildIs(ctx.build, {GameBuild::v1070, GameBuild::v1080}))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8"); pattern.size() > 0)
            {
                JumpTo(ctx, pattern.get(0).get<uint8_t>(), OPP_BODY(abi_super_lod_fix_shim));
            }
        }

    }


    // TryToSkipAllErrors

    void InstallTryToSkipAllErrors(PatchContext& ctx)
    {

        TraceArm("TryToSkipAllErrors");

        // TryToSkipAllErrors: 1 literal
        if (ctx.Option("TryToSkipAllErrors", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                WriteValue<uint8_t>(ctx, match, opp::ops::kRet);
            }
        }

    }


    // publish WantedLevelCrashFix

    void PublishWantedLevelCrashFix(PatchContext& ctx)
    {

        TraceArm("publish WantedLevelCrashFix");

        // WantedLevelCrashFix: 1 slot
        if (ctx.Option("WantedLevelCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00"); pattern.size() > 0)
            {
                zpatch_wanted_level_resume = MatchAddress(pattern, 12);
            }
        }

    }


    // WantedLevelCrashFix

    void InstallWantedLevelCrashFix(PatchContext& ctx)
    {

        TraceArm("WantedLevelCrashFix");

        // WantedLevelCrashFix: 1 sigjump
        if (ctx.Option("WantedLevelCrashFix", "1"))
        {
            const GameImage& image = ctx.image;

            if (auto pattern = Pattern(image, "8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00"); pattern.size() > 0)
            {
                auto* match = pattern.get(0).get<uint8_t>();

                JumpTo(ctx, match, OPP_BODY(abi_wanted_level_shim));
            }
        }

    }

namespace
{
    class StabilityFeatures
    {
    public:
        StabilityFeatures()
        {
            PublishEvent().Add(&PublishBadPlayerModelCrashFix, "PublishBadPlayerModelCrashFix");
            PublishEvent().Add(&PublishBlackscreenFix, "PublishBlackscreenFix");
            PublishEvent().Add(&PublishCollisionDeadlockFix, "PublishCollisionDeadlockFix");
            PublishEvent().Add(&PublishEpisodeSwitchFreezeFix, "PublishEpisodeSwitchFreezeFix");
            PublishEvent().Add(&PublishGroupHackFix, "PublishGroupHackFix");
            PublishEvent().Add(&PublishPathNodeCrashFix, "PublishPathNodeCrashFix");
            PublishEvent().Add(&PublishPedModelsInCutscenesFix, "PublishPedModelsInCutscenesFix");
            PublishEvent().Add(&PublishPlaneCrashFix, "PublishPlaneCrashFix");
            PublishEvent().Add(&PublishPlayerAsPedComponentsFix, "PublishPlayerAsPedComponentsFix");
            PublishEvent().Add(&PublishSMPA60Fix, "PublishSMPA60Fix");
            PublishEvent().Add(&PublishSpeedupBugFix, "PublishSpeedupBugFix");
            PublishEvent().Add(&PublishWantedLevelCrashFix, "PublishWantedLevelCrashFix");
            PublishEvent().Add(&PublishSuperLODFix, "PublishSuperLODFix");
            PublishEvent().Add(&PublishCrashMsgFix, "PublishCrashMsgFix");
            PublishEvent().Add(&PublishBenchmarkFix, "PublishBenchmarkFix");
            PublishEvent().Add(&PublishGarageModelFix, "PublishGarageModelFix");
            PublishEvent().Add(&PublishOutOfCommissionFix, "PublishOutOfCommissionFix");
            InstallEvent().Add(&InstallAimProjectileCrashFix, "InstallAimProjectileCrashFix");
            InstallEvent().Add(&InstallBadPlayerModelCrashFix, "InstallBadPlayerModelCrashFix");
            InstallEvent().Add(&InstallBlackscreenFix, "InstallBlackscreenFix");
            InstallEvent().Add(&InstallCollisionDeadlockFix, "InstallCollisionDeadlockFix");
            InstallEvent().Add(&InstallCombinedRadioFreezeFix, "InstallCombinedRadioFreezeFix");
            InstallEvent().Add(&InstallEpisodeSwitchFreezeFix, "InstallEpisodeSwitchFreezeFix");
            InstallEvent().Add(&InstallGroupHackFix, "InstallGroupHackFix");
            InstallEvent().Add(&InstallMPNikoCrashFix, "InstallMPNikoCrashFix");
            InstallEvent().Add(&InstallPathNodeCrashFix, "InstallPathNodeCrashFix");
            InstallEvent().Add(&InstallPedModelsInCutscenesFix, "InstallPedModelsInCutscenesFix");
            InstallEvent().Add(&InstallPlaneCrashFix, "InstallPlaneCrashFix");
            InstallEvent().Add(&InstallPlayerAsPedComponentsFix, "InstallPlayerAsPedComponentsFix");
            InstallEvent().Add(&InstallSMPA60Fix, "InstallSMPA60Fix");
            InstallEvent().Add(&InstallSpeedupBugFix, "InstallSpeedupBugFix");
            InstallEvent().Add(&InstallTryToSkipAllErrors, "InstallTryToSkipAllErrors");
            InstallEvent().Add(&InstallWantedLevelCrashFix, "InstallWantedLevelCrashFix");
            InstallEvent().Add(&InstallSuperLODFix, "InstallSuperLODFix");
            InstallEvent().Add(&InstallCrashMsgFix, "InstallCrashMsgFix");
            InstallEvent().Add(&InstallBenchmarkFix, "InstallBenchmarkFix");
            InstallEvent().Add(&InstallGarageModelFix, "InstallGarageModelFix");
            InstallEvent().Add(&InstallOutOfCommissionFix, "InstallOutOfCommissionFix");
        }
    } stabilityFeatures;
} // namespace

} // namespace opp::features
