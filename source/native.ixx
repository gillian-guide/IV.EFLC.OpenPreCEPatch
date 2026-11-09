module;

#include <common.hxx>

// Declares the /GS cookie the transcribed prologues read and the check their
// epilogues call (`__security_cookie`, `__security_check_cookie` in vcruntime.h),
// and the CRT routine the zeroing helper forwards to.
#include <intrin.h>
#include <cstring>

export module native;

import bodies;
import common;
import patch;

// The original ASI's native-script call layer: the code that lets a hook body call
// a game script native by hash. Seven pieces were recovered from the original
// binary, and their addresses are what tests/bodies.tsv proves:
//
//   0x100038C0    5  zero a pending output record              transcribed
//   0x1005B3F0   46  one-argument call, fixed hash, byte result transcribed
//   0x1005B150   44  zero-argument call, hash from ECX          transcribed
//   0x1005DE40   21  copy pending records out, dword result     transcribed
//   0x1005DE80   21  copy pending records out, byte result      transcribed
//   0x100038E0   59  guarded resolve-and-invoke                 rebuilt as C
//   0x10003980   52  guarded resolve-only                       rebuilt as C
//
// The two guarded entries are compiler output for a C function --
// `__try { ... } __except (EXCEPTION_EXECUTE_HANDLER) { ... }` -- and not a
// hand-written instruction stream: their frames carry a cookie-xored scope table
// whose handler fields name addresses inside the original image, which cannot be
// transcribed as instructions. They are rebuilt with the same ABI and the same
// observable behaviour; docs/parity.md records the split and why the instruction
// counts of those two do not appear in the manifest.
//
// The ABI the recovered bodies agree on. The game's resolver (dword_1015BDE0)
// takes the native hash in ESI, the 0xA0-byte call block in EDX, and answers the
// native's handler in EAX; the handler is then called __cdecl with the block as
// its one argument. The block is
//
//   +0x00  m_p_return   = &args        +0x0C  m_out_count
//   +0x04  m_arg_count                 +0x10  m_out_ptrs[4]
//   +0x08  m_p_args     = &args        +0x20  m_records[4] (3 words each)
//                                      +0x60  args[17] (element 0 is the result)
//
// and the two cleanup pieces copy each pending record out through its m_out_ptrs
// entry, then return args[0]. The zero-argument wrapper (0x1005B150) takes the
// hash in ECX; the one-argument wrapper (0x1005B3F0) takes the argument in EDX and
// uses the salt in dword_101580B8.
//
// The ten salt words the wrappers read are the original loader preamble's: the
// preamble probes the game image for `68 ? ? ? ? 68 A9 54 14 51 E8` (its own
// string at 0x1012B710) and takes `jnz 0x10052E82` past ten stores at
// 0x10052E1E..0x10052E78 when the probe hits, so the ASI's shipped `.data` values
// stand; a miss falls through the stores and substitutes the recovered fallback
// set. Native::Register() applies that rule once, before any arm, because the
// wrappers may run before an arm publishes its own copy of a slot.

// The published state, in C linkage because the transcribed code names the slots
// directly (the original keeps them in its own .bss beside its bodies).
extern "C"
{
    // dword_1015BDE0: the game's native-handle resolver, resolved below with the
    // original's own scan. A scan that misses leaves it at zero -- the original's
    // `jz` skips its store the same way -- and the next call through it faults.
    uintptr_t zpatch_native_resolver = 0;

    // The C entries for the two guarded pieces. They are published as addresses
    // because a naked body inside this unit calls them through a local `jmp`
    // forwarder: a module interface cannot export a naked function (C2488), and
    // the C function's decorated name is not nameable from inline asm.
    uintptr_t zpatch_native_resolve_entry = 0;
    uintptr_t zpatch_native_invoke_entry = 0;

    // The original loader preamble's ten salt words, each named by the VA it has in
    // the ASI and seeded with that slot's shipped `.data` value (the value a
    // successful probe leaves untouched). The consumer comments are the recovered
    // readers; Native::ResolveSalts() owns the probe and the miss fallbacks.
    uint32_t zpatch_native_salt_10158054 = 0x511454A9u;  // sub_1005B1E0, two-argument wrapper
    uint32_t zpatch_native_salt_101580C8 = 0x570F5725u;  // sub_1005B290, the BPM "who" wrapper
    uint32_t zpatch_native_salt_10158050 = 0x3B0C6738u;  // sub_10050A50
    uint32_t zpatch_native_salt_101580B8 = 0x4E61480Au;  // sub_1005B3F0, this layer's CallBool
    uint32_t zpatch_native_salt_10158028 = 0x6A6B4F18u;  // sub_1005B490, one-argument byte wrapper
    uint32_t zpatch_native_salt_101580A8 = 0x79A95BF9u;  // sub_10050A50
    uint32_t zpatch_native_salt_10158040 = 0x4BF36A32u;  // sub_10037AC0, the BPM payload
    uint32_t zpatch_native_salt_1015803C = 0x022B2DA9u;  // sub_10004C70, sub_100388C0
    uint32_t zpatch_native_salt_101580D4 = 0x75E005F1u;  // sub_10006450, drive-by FOV helper
    uint32_t zpatch_native_salt_1015805C = 0x62E319C6u;  // sub_10050A50
}

// The exported surface. Declared here and defined below out of line: an exported
// inline body would be written to the module's interface and would drag the naked
// declarations into it, which every importer then rejects (C2488).
export namespace Native
{
    // sub_1005B150: call the native named by `hash` with no arguments and return
    // its dword result.
    uint32_t Call(uint32_t hash);

    // sub_1005B3F0: call the native named by the salt dword_101580B8 with
    // `argument` and return its result's low byte (the original's `char`).
    bool CallBool(uint32_t argument);

    // sub_10003980: the native handler for `hash`, or null when the resolver faults.
    void* Resolve(uint32_t hash);

    // sub_100038E0: resolve `hash` and call its handler with `block`; the original's
    // `char` result.
    bool Invoke(uint32_t hash, void* block);

    // The resolved dword_1015BDE0 value, for the startup trace and the tools.
    uintptr_t Resolver();

    // Installs an alternative dword_1015BDE0. It exists for the guard test
    // (tests/native_guard.cpp, ctest `zp_native_guard`), which drives the two
    // guarded entries against targets of its own because a test process has no
    // game image to scan; nothing in the shipped path calls it, and `0` is the
    // original's unresolved slot, the case the guard's frame must survive.
    void SetResolverForTest(uintptr_t resolver);

    // The current value of one of the original preamble's ten salt words, named by
    // the VA it has in the ASI (0x10158054, 0x101580C8, ..., 0x1015805C). Those are
    // the slots the transcribed wrappers read; exposed for the startup trace, the
    // dump and the salt test.
    uint32_t Salt(uint32_t reference);

    // Applies the original preamble's salt rule to those ten words: a probe hit
    // leaves the shipped `.data` values, a miss writes the recovered fallback set.
    // Register() calls it once before any arm; the tools call it per image.
    void ResolveSalts();

    // Runs once, before any arm: resolves the game's native-handle resolver from
    // the game image, publishes the two C entries, resolves the ten salt words and
    // registers every transcribed piece with its recovered VA. Called from the
    // startup drivers next to the other Register functions.
    void Register();
}

// Forward declarations for the transcribed bodies at the bottom of this
// file: the readable code above installs them (and a body may branch to
// another), so every one is named here. MSVC rejects `__declspec(naked)`
// on a declaration (C2488), so these carry the signature only; each
// definition in the tail is naked.
static void NativeContextInit();
static void NativeInvokeEntry();
static unsigned int NativeCleanupBoolBody();
static unsigned int NativeCleanupDwordBody();
static unsigned int __fastcall NativeCallBoolBody(unsigned int, unsigned int);
static unsigned int __fastcall NativeCallHashBody(unsigned int);
static void* ResolveInEsi(unsigned int);

// The transcribed wrappers' `call memset` is a call to this cdecl forwarder
// instead: the inline assembler rejects a direct call to the memset intrinsic
// (C2420), so the call names a local function that performs the same zeroing with
// the same argument order. The call itself is still one `E8`, and the body is
// otherwise instruction-for-instruction (docs/parity.md).
static void __cdecl NativeZeroBlock(void* destination, int value, unsigned int count)
{
    std::memset(destination, value, count);
}

// sub_10003980 (52 instructions), rebuilt: resolve a native's handler by hash,
// with the original's catch-all SEH around the call. A resolver call that faults
// (a null dword_1015BDE0, or a fault inside the game's resolver) answers zero
// instead of escaping, exactly as the original's `xor eax, eax` handler does.
static __declspec(noinline) void* __fastcall GuardedResolve(unsigned int hash) noexcept
{
    __try
    {
        return ResolveInEsi(hash);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

// sub_100038E0 (59 instructions), rebuilt: resolve a native's handler and call it
// with the block. On the normal path the result is always 1, even when the resolver
// answers null (the handler call is skipped, 0x10003937); a fault anywhere inside
// the guarded region -- the resolver call or the handler call -- is caught and
// answers 0. That is the original's split, preserved.
static __declspec(noinline) char __fastcall GuardedInvoke(unsigned int hash, void* block) noexcept
{
    __try
    {
        const auto handler = reinterpret_cast<void(__cdecl*)(void*)>(ResolveInEsi(hash));
        if (handler != nullptr)
            handler(block);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

namespace
{
    // One of the original preamble's ten stores: the ASI slot VA, the slot, the
    // value the ASI ships in `.data` and the value the miss path writes there.
    struct SaltWord
    {
        uint32_t reference;
        uint32_t* value;
        uint32_t shipped;
        uint32_t fallback;
    };

    // In the original's store order (0x10052E1E..0x10052E78), which is not address
    // order. Every shipped value is the dword at that VA in the ASI image; every
    // fallback is the immediate of the corresponding `mov` at that store.
    constexpr SaltWord kSaltWords[] =
    {
        { 0x10158054u, &zpatch_native_salt_10158054, 0x511454A9u, 0x6B86410Eu },
        { 0x101580C8u, &zpatch_native_salt_101580C8, 0x570F5725u, 0x406B4B20u },
        { 0x10158050u, &zpatch_native_salt_10158050, 0x3B0C6738u, 0x6DCD115Au },
        { 0x101580B8u, &zpatch_native_salt_101580B8, 0x4E61480Au, 0x62BFDB37u },
        { 0x10158028u, &zpatch_native_salt_10158028, 0x6A6B4F18u, 0x931F73A2u },
        { 0x101580A8u, &zpatch_native_salt_101580A8, 0x79A95BF9u, 0x7F6A60D3u },
        { 0x10158040u, &zpatch_native_salt_10158040, 0x4BF36A32u, 0xC9DBDA90u },
        { 0x1015803Cu, &zpatch_native_salt_1015803C, 0x022B2DA9u, 0xA4EA0691u },
        { 0x101580D4u, &zpatch_native_salt_101580D4, 0x75E005F1u, 0x09399528u },
        { 0x1015805Cu, &zpatch_native_salt_1015805C, 0x62E319C6u, 0x1CC648EAu },
    };
}

namespace Native
{
    uint32_t Call(uint32_t hash)
    {
        return NativeCallHashBody(hash);
    }

    bool CallBool(uint32_t argument)
    {
        // The recovered body returns a `char` in AL; the original's caller tests AL,
        // so only the low byte is the answer.
        return (NativeCallBoolBody(0, argument) & 0xFFu) != 0;
    }

    void* Resolve(uint32_t hash)
    {
        const auto handler = GuardedResolve(hash);
        // A resolve that answers null is the interesting case in play: the caller
        // usually dereferences the result, so the log records which hash failed.
        Log::Print("native resolve hash {:08X} -> {:08X}", hash,
                   reinterpret_cast<uintptr_t>(handler));
        return handler;
    }

    bool Invoke(uint32_t hash, void* block)
    {
        const bool answered = GuardedInvoke(hash, block) != 0;
        Log::Print("native invoke hash {:08X} block {:08X} -> {}", hash,
                   reinterpret_cast<uintptr_t>(block), answered ? 1 : 0);
        return answered;
    }

    uintptr_t Resolver()
    {
        return zpatch_native_resolver;
    }

    void SetResolverForTest(uintptr_t resolver)
    {
        // A single store to the slot Register() would fill: no other state, no
        // behaviour change on the shipped path, which never calls this.
        zpatch_native_resolver = resolver;
    }

    uint32_t Salt(uint32_t reference)
    {
        for (const auto& word : kSaltWords)
        {
            if (word.reference == reference)
                return *word.value;
        }
        return 0;
    }

    void ResolveSalts()
    {
        // The ASI ships the ten values below in its `.data`; the original only
        // overwrites them when its probe misses.
        for (const auto& word : kSaltWords)
            *word.value = word.shipped;

        // 0x10052E10..0x10052E1C: `mov ecx, offset "68 ? ? ? ? 68 A9 54 14 51 E8"
        // (the ASI's string at 0x1012B710); `test eax,eax`; `jnz 0x10052E82` --
        // a hit jumps over every store, so the hit path copies nothing from the
        // game image and the shipped values stand. 0x10052E1E..0x10052E78: a miss
        // falls through the ten `mov dword_10158xxx, imm` stores.
        if (find_pattern("68 ? ? ? ? 68 A9 54 14 51 E8").empty())
        {
            for (const auto& word : kSaltWords)
                *word.value = word.fallback;
        }
    }

    void Register()
    {
        // The preamble's salt block (0x10052E1E..0x10052E78) runs before its
        // resolver store (0x10052E88..0x10052E9E); neither reads the other.
        ResolveSalts();

        // dword_1015BDE0, resolved the way the original's loader preamble does it
        // (0x10052E88..0x10052E9E): scan the game module for `08 0B C2 8B F0 E8`
        // and take the target of the call whose opcode is the last byte --
        //   mov ecx, [eax+6]; add eax, 0Ah; add eax, ecx
        // so the target is match + 10 + the four bytes at match + 6. A miss skips
        // the store, as the original's `jz` does, and leaves the null resolver a
        // later call faults through.
        auto pattern = find_pattern("08 0B C2 8B F0 E8");
        if (!pattern.empty())
        {
            const auto* match = pattern.get_first<std::byte>(0);
            int32_t displacement{};
            std::memcpy(&displacement, match + 6, sizeof(displacement));
            zpatch_native_resolver = reinterpret_cast<uintptr_t>(match + 10) +
                static_cast<uintptr_t>(static_cast<intptr_t>(displacement));
        }

        zpatch_native_resolve_entry = reinterpret_cast<uintptr_t>(&GuardedResolve);
        zpatch_native_invoke_entry = reinterpret_cast<uintptr_t>(&GuardedInvoke);

        Bodies::Register(reinterpret_cast<uintptr_t>(&NativeContextInit), 0x100038C0u);
        // The forwarder stands in for the rebuilt sub_100038E0, so it carries that
        // piece's recovered VA: a branch that lands on it is the original's branch.
        Bodies::Register(reinterpret_cast<uintptr_t>(&NativeInvokeEntry), 0x100038E0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&NativeCallBoolBody), 0x1005B3F0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&NativeCallHashBody), 0x1005B150u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&NativeCleanupDwordBody), 0x1005DE40u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&NativeCleanupBoolBody), 0x1005DE80u);
    }
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// sub_100038C0, recovered VA 0x100038C0 (5 instructions): zero the three words of
// one pending output record and return the record in EAX (__thiscall).
static __declspec(naked) void NativeContextInit()
{
    __asm
    {
        mov DWORD PTR [ecx + 8], 0
        mov eax, ecx
        mov DWORD PTR [ecx + 4], 0
        mov DWORD PTR [ecx], 0
        ret
    }
}

// The guarded invoke's entry: the original's wrappers `call sub_100038E0` (E8), so
// ours must be a direct call as well. This six-byte forwarder jumps through the
// entry Register() publishes; it touches no register or stack slot.
static __declspec(naked) void NativeInvokeEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_native_invoke_entry]
    }
}

// sub_1005DE80, recovered VA 0x1005DE80 (21 instructions): copy out every pending
// output record and return the low byte of args[0] (the original's `char`).
// __thiscall: the block is in ECX.
static __declspec(naked) unsigned int NativeCleanupBoolBody()
{
    __asm
    {
        push esi
        mov esi, ecx
        cmp DWORD PTR [esi + 0Ch], 0
        jbe native_bool_cleanup_done
        // The original's loop alignment (0F 1F 80 00 00 00 00), emitted as bytes so
        // the assembler cannot choose a shorter nop.
        __emit 0x0F
        __emit 0x1F
        __emit 0x80
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00
    native_bool_cleanup_loop:
        dec DWORD PTR [esi + 0Ch]
        mov eax, DWORD PTR [esi + 0Ch]
        mov edx, DWORD PTR [esi + eax * 4 + 10h]
        lea ecx, [eax + 2]
        add ecx, ecx
        mov eax, DWORD PTR [esi + ecx * 8]
        mov DWORD PTR [edx], eax
        mov eax, DWORD PTR [esi + ecx * 8 + 4]
        mov DWORD PTR [edx + 4], eax
        mov eax, DWORD PTR [esi + ecx * 8 + 8]
        mov DWORD PTR [edx + 8], eax
        cmp DWORD PTR [esi + 0Ch], 0
        ja native_bool_cleanup_loop
    native_bool_cleanup_done:
        mov al, BYTE PTR [esi + 60h]
        pop esi
        ret
    }
}

// sub_1005DE40, recovered VA 0x1005DE40 (21 instructions): the same copy-out, then
// the whole of args[0].
static __declspec(naked) unsigned int NativeCleanupDwordBody()
{
    __asm
    {
        push esi
        mov esi, ecx
        cmp DWORD PTR [esi + 0Ch], 0
        jbe native_dword_cleanup_done
        __emit 0x0F
        __emit 0x1F
        __emit 0x80
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00
    native_dword_cleanup_loop:
        dec DWORD PTR [esi + 0Ch]
        mov eax, DWORD PTR [esi + 0Ch]
        mov edx, DWORD PTR [esi + eax * 4 + 10h]
        lea ecx, [eax + 2]
        add ecx, ecx
        mov eax, DWORD PTR [esi + ecx * 8]
        mov DWORD PTR [edx], eax
        mov eax, DWORD PTR [esi + ecx * 8 + 4]
        mov DWORD PTR [edx + 4], eax
        mov eax, DWORD PTR [esi + ecx * 8 + 8]
        mov DWORD PTR [edx + 8], eax
        cmp DWORD PTR [esi + 0Ch], 0
        ja native_dword_cleanup_loop
    native_dword_cleanup_done:
        mov eax, DWORD PTR [esi + 60h]
        pop esi
        ret
    }
}

// sub_1005B3F0, recovered VA 0x1005B3F0 (46 instructions): build the block, put
// the one argument in args[0], call the native at dword_101580B8 and answer its
// low byte. The argument travels in EDX (the first register argument is unused);
// the whole body is the original's, /GS frame included.
static __declspec(naked) unsigned int __fastcall NativeCallBoolBody(unsigned int, unsigned int)
{
    __asm
    {
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        sub esp, 0ACh
        mov eax, DWORD PTR [__security_cookie]
        xor eax, esp
        mov DWORD PTR [esp + 0A8h], eax
        mov eax, DWORD PTR [zpatch_native_salt_101580B8]
        push ebx
        push esi
        push edi
        push 0A0h
        mov DWORD PTR [esp + 10h], eax
        mov ebx, edx
        lea eax, [esp + 14h]
        push 0
        push eax
        call NativeZeroBlock
        add esp, 0Ch
        lea edi, [esp + 30h]
        mov esi, 4
    native_bool_records:
        mov ecx, edi
        call NativeContextInit
        add edi, 10h
        sub esi, 1
        jnz native_bool_records
        mov ecx, DWORD PTR [esp + 0Ch]
        lea eax, [esp + 70h]
        lea edx, [esp + 10h]
        mov DWORD PTR [esp + 18h], eax
        mov DWORD PTR [esp + 10h], eax
        mov DWORD PTR [esp + 1Ch], esi
        mov DWORD PTR [esp + 70h], ebx
        mov DWORD PTR [esp + 14h], 1
        call NativeInvokeEntry
        lea ecx, [esp + 10h]
        call NativeCleanupBoolBody
        mov ecx, DWORD PTR [esp + 0B4h]
        pop edi
        pop esi
        pop ebx
        xor ecx, esp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_1005B150, recovered VA 0x1005B150 (44 instructions): build the block, call
// the native named by ECX with no arguments and answer args[0].
static __declspec(naked) unsigned int __fastcall NativeCallHashBody(unsigned int)
{
    __asm
    {
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        sub esp, 0ACh
        mov eax, DWORD PTR [__security_cookie]
        xor eax, esp
        mov DWORD PTR [esp + 0A8h], eax
        push ebx
        push esi
        push edi
        push 0A0h
        lea eax, [esp + 14h]
        mov ebx, ecx
        push 0
        push eax
        call NativeZeroBlock
        add esp, 0Ch
        lea edi, [esp + 30h]
        mov esi, 4
        // The original's loop alignment (0F 1F 40 00).
        __emit 0x0F
        __emit 0x1F
        __emit 0x40
        __emit 0x00
    native_hash_records:
        mov ecx, edi
        call NativeContextInit
        add edi, 10h
        sub esi, 1
        jnz native_hash_records
        lea eax, [esp + 70h]
        mov DWORD PTR [esp + 14h], esi
        lea edx, [esp + 10h]
        mov DWORD PTR [esp + 18h], eax
        mov ecx, ebx
        mov DWORD PTR [esp + 10h], eax
        mov DWORD PTR [esp + 1Ch], esi
        call NativeInvokeEntry
        lea ecx, [esp + 10h]
        call NativeCleanupDwordBody
        mov ecx, DWORD PTR [esp + 0B4h]
        pop edi
        pop esi
        pop ebx
        xor ecx, esp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// The game resolver reads the hash from ESI (0x626AF0 opens with `mov eax, esi`),
// and both original guarded entries move their ECX argument into ESI around the
// call, saving and restoring the register (0x10003927/0x1000392E and
// 0x100039C5/0x100039CC). C++ has no way to ask for a register, so this stays
// naked; the two C entries above call it.
static __declspec(naked) void* ResolveInEsi(unsigned int)
{
    __asm
    {
        push esi
        mov esi, DWORD PTR [esp + 8]
        mov eax, DWORD PTR [zpatch_native_resolver]
        call eax
        pop esi
        ret
    }
}
