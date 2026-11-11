module;

#include <common.hxx>

// Declares the /GS cookie the helper's prologue reads and the check its epilogue
// calls (`__security_cookie`, `__security_check_cookie` in vcruntime.h).
#include <intrin.h>

export module shared;

import bodies;
import common;
import improvements;
import patch;

// The helper library the original ASI keeps beside its bodies: the name hasher /
// "hash:NNNN" resolver (sub_10003E50) and the game lookup it and several features
// go through (dword_10159C0C). Both are transcribed as instruction streams, not
// reimplemented, so the recovered VA is in the comment above each piece and in
// tests/bodies.tsv.
//
// The two PlayerAsPedComponentsFix bodies are here rather than in stability.ixx
// because the helper reaches the resolver with a direct `call` (E8, the shape the
// original has) and MSVC rejects an external-linkage naked function in a module
// interface unit: a body that names the resolver has to live in the resolver's
// unit. They are exported by address; the arm that installs them lives in
// stability.ixx, where the key does.

// The exported surface. The functions are declared here and defined below out of
// line: an exported *inline* body would be written to the module's interface and
// would drag the naked declarations into it, which every importer then rejects
// (C2488).

export namespace Shared
{
    // sub_10003E50's address, for the arms that ask it to resolve a config value.
    uintptr_t ValueResolver();

    // sub_1003D9A0's address: the body the PlayerAsPedComponentsFix arm installs.
    uintptr_t PlayerPedComponentsShim();

    // dword_10159D2C. Published before the site is written: the original reads the
    // resume from the site it is about to overwrite.
    void PublishPlayerPedComponentsResume(uint32_t resume);

    // The cached model lookup the `caches` group publishes as
    // `zpatch_model_lookup`; the original function stays in
    // `zpatch_model_lookup_original`. A successful positive index is memoised;
    // a negative result is not (a model not loaded yet must be found later).
    void SetModelLookupForTest(uintptr_t original, bool cache);
    int CachedLookupForTest(uint32_t hash, int32_t* out);
    void ResetLookupCacheForTest();

    // Runs once, before any arm: it resolves the shared game lookup and publishes
    // the bodies' recovered addresses. Called from the startup drivers next to the
    // other Register functions.
    void Register();
}

// The published state, in C linkage because the transcribed code names the slots
// directly (the original keeps them in its own .bss beside its bodies).

extern "C"
{
    // dword_10159C0C (original 0x10159C0C): the game's model-index lookup,
    // `unsigned int lookup(unsigned int nameHash, int *index)`
    // (CModelInfoStore::getModelByKey, game 0x8DD2D0). Resolved below from the byte
    // pattern the original scans for; a scan that misses keeps the previous value,
    // as its `cmovnz` at 0x10052ED9 does, which for a first run is 0.
    uintptr_t zpatch_model_lookup = 0;

    // The `caches` group's original, saved before the cached wrapper replaces
    // the published slot.
    uintptr_t zpatch_model_lookup_original = 0;

    // The two CRT imports the resolver calls, through its IAT slots 0x100633A0
    // (`_strnicmp`) and 0x100632D0 (`strtoul`). They stay pointer slots because the
    // original's call is memory indirect (`FF 15`), and a direct call would be a
    // different instruction.
    uintptr_t zpatch_resolver_strnicmp = 0;
    uintptr_t zpatch_resolver_strtoul = 0;

    // PlayerAsPedComponentsFix's published words: dword_10159D20 (the computed
    // "is the player model" flag), dword_10159D28 (the object the shim saved) and
    // dword_10159D2C (the resume, published by the arm before the site is written).
    uint32_t zpatch_player_ped_computed = 0;
    uint32_t zpatch_player_ped_object = 0;
    uint32_t zpatch_player_ped_resume = 0;
}

// The two literals the transcribed code pushes: the "hash:" prefix (original
// 0x10128CD8) and the "player" model name (original 0x1013A038).

extern "C"
{
    const char zpatch_resolver_hash_prefix[] = "hash:";
    const char zpatch_resolver_player_name[] = "player";
}

// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488: "'naked' can only be applied to non-member
// function definitions"), so the readable code above them names each body through a
// plain prototype; only the definition in the tail keeps the attribute.
static void ValueResolverBody();
static void PlayerPedHelperBody();
static void PlayerPedComponentsShimBody();

namespace Shared
{
    uintptr_t ValueResolver()
    {
        return reinterpret_cast<uintptr_t>(&ValueResolverBody);
    }

    uintptr_t PlayerPedComponentsShim()
    {
        return reinterpret_cast<uintptr_t>(&PlayerPedComponentsShimBody);
    }
}

namespace Shared
{
    void PublishPlayerPedComponentsResume(uint32_t resume)
    {
        zpatch_player_ped_resume = resume;
    }
}

namespace
{
    // A direct-mapped memo of successful model lookups. The table is static
    // after startup, so a hit cannot go stale; a collision just re-resolves.
    struct LookupCacheEntry
    {
        uint32_t hash;
        int32_t index;
        uint32_t valid;
    };
    constexpr size_t kLookupCacheSlots = 1024;
    LookupCacheEntry g_lookup_cache[kLookupCacheSlots] = {};

    int __cdecl CachedModelLookup(uint32_t hash, int32_t* out) noexcept
    {
        const size_t slot = (static_cast<size_t>(hash) * 2654435761u) >>
                            (32 - 10);  // 1024 slots
        auto& entry = g_lookup_cache[slot];
        if (entry.valid != 0 && entry.hash == hash)
        {
            if (out != nullptr)
                *out = entry.index;
            return 1;
        }

        const auto original = reinterpret_cast<int(__cdecl*)(uint32_t, int32_t*)>(
            zpatch_model_lookup_original);
        if (original == nullptr)
        {
            if (out != nullptr)
                *out = -1;
            return 0;
        }

        int32_t index = -1;
        const int answer = original(hash, &index);
        if (answer != 0 && index > 0)
        {
            entry.hash = hash;
            entry.index = index;
            entry.valid = 1;
        }
        if (out != nullptr)
            *out = index;
        return answer;
    }
}

namespace Shared
{
    void SetModelLookupForTest(uintptr_t original, bool cache)
    {
        zpatch_model_lookup_original = original;
        zpatch_model_lookup = cache
            ? reinterpret_cast<uintptr_t>(&CachedModelLookup)
            : original;
    }

    int CachedLookupForTest(uint32_t hash, int32_t* out)
    {
        return CachedModelLookup(hash, out);
    }

    void ResetLookupCacheForTest()
    {
        for (auto& entry : g_lookup_cache)
            entry = {};
    }
}

namespace Shared
{
    void Register()
    {
        // The original's resolver calls the CRT through its IAT; ours calls through
        // these slots, which is what keeps the instruction shape.
        zpatch_resolver_strnicmp = reinterpret_cast<uintptr_t>(&_strnicmp);
        zpatch_resolver_strtoul = reinterpret_cast<uintptr_t>(&strtoul);

        // dword_10159C0C, from the byte pattern the original scans for in the game
        // image (game 0x8DD2D0 on 1.0.8.0). A miss keeps the slot as it was.
        auto pattern = find_pattern("8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E");
        if (!pattern.empty())
            zpatch_model_lookup = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));

        zpatch_model_lookup_original = zpatch_model_lookup;
        if (ImprovementEnabled(Improvement::caches))
            zpatch_model_lookup = reinterpret_cast<uintptr_t>(&CachedModelLookup);

        Bodies::Register(reinterpret_cast<uintptr_t>(&ValueResolverBody), 0x10003E50u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&PlayerPedComponentsShimBody), 0x1003D9A0u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&PlayerPedHelperBody), 0x1003D930u);
    }
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// sub_10003E50, recovered VA 0x10003E50 (162 bytes, 67 instructions): the
// config-value resolver. A string that starts with "hash:" (case-insensitive,
// five characters) and parses to a non-zero number is that number; anything else
// is hashed the way the game hashes model names: 'A'-'Z' lower-cased, '\' read as
// '/', `* 0x401`, `^ (h >> 6)`, then one final mix (`* 9`, `^ (h >> 11)`,
// `* 0x8001`) with the +2 nudge for a result below 2. The original is
// `__cdecl unsigned int(char *)`; it reads no module word, so its only two
// dependencies are the CRT imports above.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void ValueResolverBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push edi
        mov edi, DWORD PTR [ebp + 8]
        push 5
        push OFFSET zpatch_resolver_hash_prefix
        push edi
        call DWORD PTR [zpatch_resolver_strnicmp]
        add esp, 0Ch
        test eax, eax
        jnz resolver_hashed
        push eax
        push eax
        lea eax, [edi + 5]
        push eax
        call DWORD PTR [zpatch_resolver_strtoul]
        add esp, 0Ch
        test eax, eax
        jnz resolver_done
    resolver_hashed:
        push ebx
        mov bl, BYTE PTR [edi]
        push esi
        xor esi, esi
        mov al, bl
        cmp bl, 22h
        jnz resolver_unquoted
        mov al, BYTE PTR [edi + 1]
    resolver_unquoted:
        lea edx, [edi + 1]
        cmovnz edx, edi
        test al, al
        jz resolver_tail
        // The original's loop alignment: 0F 1F 80 00 00 00 00, emitted as bytes so
        // the assembler cannot shorten it to the four-byte form.
        __emit 0x0F
        __emit 0x1F
        __emit 0x80
        __emit 0x00
        __emit 0x00
        __emit 0x00
        __emit 0x00
    resolver_loop:
        cmp bl, 22h
        jnz resolver_not_quote
        cmp al, bl
        jz resolver_tail
    resolver_not_quote:
        lea ecx, [eax - 41h]
        inc edx
        cmp cl, 19h
        jbe resolver_lowercase
        cmp al, 5Ch
        jnz resolver_mixed
        mov al, 2Fh
        jmp resolver_mixed
    resolver_lowercase:
        add al, 20h
    resolver_mixed:
        movsx eax, al
        add eax, esi
        imul eax, eax, 401h
        mov esi, eax
        shr esi, 6
        xor esi, eax
        mov al, BYTE PTR [edx]
        test al, al
        jnz resolver_loop
    resolver_tail:
        lea ecx, [esi + esi * 8]
        mov eax, ecx
        shr eax, 0Bh
        xor eax, ecx
        imul ecx, eax, 8001h
        pop esi
        pop ebx
        cmp ecx, 2
        lea eax, [ecx + 2]
        cmovnb eax, ecx
    resolver_done:
        pop edi
        pop ebp
        ret
    }
}

// sub_1003D930, recovered VA 0x1003D930 (103 bytes, 28 instructions): the
// PlayerAsPedComponentsFix helper. It resolves "player" through the shared
// resolver, asks the game lookup for that model's index, and records whether the
// object the shim saved has the same model word (+2Eh). The object is read
// unguarded and so is the lookup slot, exactly as the original's `call
// dword_10159C0C` is. The /GS prologue and epilogue are the original's, including
// its call to the CRT check (`@__security_check_cookie@4`, 0x1005FE7B).
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void PlayerPedHelperBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 8
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push OFFSET zpatch_resolver_player_name
        mov DWORD PTR [zpatch_player_ped_computed], 0
        call ValueResolverBody
        lea ecx, [ebp - 8]
        mov DWORD PTR [ebp - 8], 0FFFFFFFFh
        push ecx
        push eax
        call DWORD PTR [zpatch_model_lookup]
        mov eax, DWORD PTR [zpatch_player_ped_object]
        add esp, 0Ch
        mov ecx, DWORD PTR [zpatch_player_ped_computed]
        mov edx, 1
        movsx eax, WORD PTR [eax + 2Eh]
        cmp DWORD PTR [ebp - 8], eax
        cmovz ecx, edx
        mov DWORD PTR [zpatch_player_ped_computed], ecx
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_1003D9A0, recovered VA 0x1003D9A0 (25 bytes, 7 instructions): the site's
// shim. It saves the object, computes the flag, and replays the two stack
// arguments CPedVariationMgr::SetVariation is called with, then resumes at the
// anchor's +14: the `movzx edx,[eax+219h]; push edx` the site replaced is the flag
// push, and the `push ecx` at the anchor's +13 is the shim's own.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void PlayerPedComponentsShimBody()
{
    __asm
    {
        __emit 0x60  // pushad (pushal)
        mov DWORD PTR [zpatch_player_ped_object], eax
        call PlayerPedHelperBody
        __emit 0x61  // popad (popal)
        push DWORD PTR [zpatch_player_ped_computed]
        push ecx
        jmp DWORD PTR [zpatch_player_ped_resume]
    }
}
