module;

#include <common.hxx>

// Declares the /GS cookie the transcribed wrapper's prologue reads and the check
// its epilogue calls (`__security_cookie`, `__security_check_cookie` in
// vcruntime.h), the `va_list` the rebuilt formatter takes, and the CRT calls the
// decoder and the archive builder make.
#include <intrin.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

export module ooc;

import bodies;
import common;
import loader;
import patch;

// OutOfCommissionFix, the one key whose payload is data rather than code: the
// original embeds the compressed `finale2.sco` script resource in its own image,
// decodes it at run time, registers the decoded table as a fake pak entry, wraps
// it in an in-memory archive and mounts that archive through the game's own
// device register. The arm is then two writes: an `E9` that widens the game's
// script-resource magic check to accept the archive's `0x86723DFF`, and an `E8`
// that sends the game's device lookup through the mounting wrapper first.
//
// Recovered VAs (IDA `ZolikaPatch.asi.i64`):
//
//   0x1004C3F0  the key's handler: scan `A9 39 44 24 18`, publish match+0x11 as
//               the branch body's resume and `E9` the match's +7; scan
//               `E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56`, publish the target
//               of the call at match-0x33 (the game's device register) and the
//               original target of the call at the match, then retarget that call
//               to the wrapper. Compiler output (EH4 frame, /GS, std::string
//               temporaries, the string builder's digit loops), so it is rebuilt
//               below rather than transcribed and has no proof row.
//   0x1004B150  the decoder's opcode step.                transcribed, proved
//   0x1004B570  the decode driver + Adler-32.             transcribed, proved
//   0x1004B750  the init: decode, register, build the archive, publish the size.
//   0x1004ACE0  the in-memory archive builder.            transcribed, proved
//   0x1004C350  the wrapper the second `E8` is retargeted to.  transcribed, proved
//   0x1004C3C0  the branch body the `E9` jumps to.             transcribed, proved
//
// The decoder pair and the archive builder are transcribed instruction for
// instruction from the original's own streams: the decoder is an interpreter with
// sixteen exits and no register contract beyond "ECX in, EAX out", and the builder
// is one /GS frame with EBP-relative locals and direct `E8`s. Neither has an
// exception frame, a TLS static-init guard or an indexed absolute operand, so both
// are provable whole (`tests/bodies.tsv`). The builder's twelve internal helpers
// (0x1004AB70..0x10056500) are transcribed beside it, so its call graph is
// transcriptions down to the CRT boundaries. The remaining rebuilds are the init
// (C++ container plumbing, class B) and the handler (the original's own
// loader-shaped scan/publish/write code, EH4 + `std::string`), rebuilt in C++ with
// the original's arithmetic, bounds and call order.

// The original's own state, in C linkage because the transcribed bodies name the
// slots directly (the original keeps them in its .bss beside its bodies).
extern "C"
{
    uint32_t zpatch_ooc_resume = 0;    // dword_10158034: the branch body's resume
    uint32_t zpatch_ooc_mount = 0;     // dword_10159BF8: the game's device register
    uint32_t zpatch_ooc_original = 0;  // dword_10159C04: the retargeted call's target
    uint32_t zpatch_ooc_state = 0;     // dword_10159BFC: the archive's byte count

    // The decode stream's four words, which the original keeps in its own .bss and
    // its transcribed bodies read as absolute operands: the output buffer's start
    // (dword_101A842C), its cursor (dword_101A8430), its end (dword_10196374) and
    // the source's lower bound (dword_101887D4 = the resource's base - 16).
    uint32_t zpatch_ooc_begin = 0;
    uint32_t zpatch_ooc_cursor = 0;
    uint32_t zpatch_ooc_end = 0;
    uint32_t zpatch_ooc_lower = 0;

    // dword_10159C78: the archive builder's write cursor. The transcribed
    // builder sets it to zero, moves it to 0x800 for the payload region and
    // advances it on every append (0x1004ABD0), and the rebuilt init reads it back
    // after the call for the archive's byte count exactly as sub_1004B750 does.
    uint32_t zpatch_ooc_offset = 0;
}

// The two halves of the mount string the wrapper formats: "me" + "mor" + "y" +
// ':' + '$' + <archive> + ',' + <bytes> + ',' + '0' + ':' + "" is
// "memory:$<archive>,<bytes>,0:", which is the name the game's device register
// takes for a block that already sits in memory. The format and its one string
// argument are the original's own text (0x1012EB58 and 0x1012EB0C).
extern "C"
{
    const char zpatch_ooc_mount_format[] = "me%sy%c%c%p,%d,%c%c%s";
    const char zpatch_ooc_mount_prefix[] = "mor";
}

// The bounded formatter's entry, published by source/stability.ixx (sub_10001280).
extern "C"
{
    extern uint32_t zpatch_format_entry;
}

// Forward declarations for the transcribed bodies at the bottom of this
// file: the readable code above installs them (and a body may branch to
// another), so every one is named here. MSVC rejects `__declspec(naked)`
// on a declaration (C2488), so these carry the signature only; each
// definition in the tail is naked.
static void OocStepBody();
static int __fastcall OocDecodeBody(uint8_t*);
static void OocDestroyEntriesBody();
static void OocZeroHeaderBody();
static void OocZeroRecordBody();
static void OocSetRecordPadBody();
static void OocPageCountBody();
static void OocAppendBody();
static void OocNameBytesBody();
static void OocPageTotalBody();
static void OocVectorBeginBody();
static void OocVectorEndBody();
static void OocVectorSizeBody();
static void OocEntryNameBody();
static void OocVectorClearBody();
static void* OocBuildArchiveBody();
static void OocFormat();
static void OocClearPakBody();
static void OocBranchBody();
static void OocWrapBody();

// The recovered resource, verbatim. source/ooc_finale2.inc carries the pinned
// image SHA-256, the source VA and length, the extraction recipe and the decoded
// table's own hashes.
static const uint8_t kOocResource[] = {
#include "ooc_finale2.inc"
};
static_assert(sizeof(kOocResource) == 163796,
    "the recovered resource is the original's 163,796 bytes at 0x100B6798");

namespace
{
    // sub_1004B750's malloc argument and sub_1004B570's output bound
    // (dword_10196374 = begin + 0x2BD50): the decoded table's exact size.
    constexpr size_t kOocTableSize = 0x2BD50;  // 179,536
    // The name the one fake-pak entry carries (the archive's granularity, 0x800,
    // and its magic, 0x86723DFF, are the transcribed builder's own literals).
    constexpr char kOocName[] = "finale2.sco";
}

// The decode pair's one call, the CRT's memcpy. The inline assembler refuses
// `call memcpy` (it is a compiler intrinsic), so the three copy sites call this
// thin wrapper instead; the original calls the CRT directly, the call shape is the
// same (`call imm`), the caller cleans the three arguments and the copy itself is
// the CRT's.
static void __cdecl OocCopyBlock(void* destination, const void* source, std::size_t size)
{
    std::memcpy(destination, source, size);
}

namespace
{
    // One entry of the original's fake-pak vector (a `std::vector` at 0x1016E43C
    // with its three pointers at 0x1016E43C/40/44). The name is a std::string
    // there; the only name ever registered is the eleven-character "finale2.sco",
    // which fits its 16-byte small-string buffer, so the entry keeps that buffer
    // as a fixed array and the layout stays the 44 bytes the builder walks.
    struct OocPakEntry
    {
        char name[16];           // +0x00 the std::string's small-string buffer
        uint32_t name_size;      // +0x10
        uint32_t name_capacity;  // +0x14
        uint32_t file_size;      // +0x18
        uint32_t stored_offset;  // +0x1C
        uint32_t stored_size;    // +0x20
        uint32_t padding;        // +0x24
        uint32_t data;           // +0x28
    };
    static_assert(sizeof(OocPakEntry) == 44, "the original's fake-pak entry is 44 bytes");

    // The original's fake-pak vector, at a fixed address because the transcribed
    // builder names its storage the way the original does: `mov [ebp-60h], offset
    // Block` and `mov ecx, offset Block`, with the vector's begin/end read as
    // `[Block]` / `[Block+4]`. It is a real `std::vector` so the C++ side of the
    // module (the rebuilt init's push_back, the self-check) keeps using the
    // container's own growth, and its three pointers are the original's three
    // pointers at 0x1016E43C/40/44. C linkage gives the object the short `_name`
    // symbol the proof and the operand map resolve; the layout is the same either
    // way, and `OocPakTable()` is the only C++ path to it.
    extern "C"
    {
        std::vector<OocPakEntry> zpatch_ooc_pak;
    }

    std::vector<OocPakEntry>& OocPakTable()
    {
        return zpatch_ooc_pak;
    }
}

// The archive's 20-byte header and its 16-byte per-entry record are built on
// the builder's own stack (`[ebp-28h]` and `[ebp-14h]`) by the two original
// helpers below; their field order is the stores the transcription keeps:
//
//   header  +0x00 magic 0x86723DFF   +0x04 version 3   +0x08 entry count
//           +0x0C name bytes        +0x10 page 0x10   +0x12 opcode 0xE9
//   record  +0x00 stored size       +0x04 stored offset
//           +0x08 index (1-based first page)  +0x0C pages | pad (0x2000 flag)
//
// The chunks the builder stamps are what OocInitialise and the self-check
// measure: the header's two words at +0x10 are the `16`/`0xE9` pair.

// The three CRT boundaries the builder crosses. The original calls a thunk to the
// CRT's scalar `operator new` (0x100603F1), `memset` (0x10060D6E) and `free`
// (0x1005FE89); the inline assembler cannot name the first two, and a direct call
// would not keep the original's argument order, so each site calls one of these
// cdecl forwarders instead -- one `E8`, the same arguments and the same routine.
static void* __cdecl OocAllocate(unsigned int size)
{
    return ::operator new(size);
}

static void __cdecl OocZeroBlock(void* destination, int value, unsigned int count)
{
    std::memset(destination, value, count);
}

static void __cdecl OocFreeBlock(void* block)
{
    std::free(block);
}

// 0x1005CD50, the destroy loop the vector's clear runs over its elements
// (`std::_Destroy_range` for the fake-pak entry). It is the one callee of the
// transcribed set that stays rebuilt: it is the CRT's std::string destructor
// loop, and its free path reads the string's internal capacity and, past
// 0x1000, the allocator's block header, before calling `_invalid_parameter_
// noinfo_noreturn` -- image-internal CRT state this module cannot name. The
// loop below performs the same stores on the same 44-byte stride, and frees a
// heap-allocated name the way the original does. Every entry this module ever
// registers keeps its name in the inline buffer (capacity 0x0F), so the free
// branch is unreachable from the tree's own data, exactly as it is in the
// original for the single "finale2.sco" entry.
static void __cdecl OocDestroyEntries(void* first, void* last)
{
    auto* entry = static_cast<uint8_t*>(first);
    const auto* end = static_cast<const uint8_t*>(last);
    while (entry != end)
    {
        const auto capacity = *reinterpret_cast<uint32_t*>(entry + 0x14);
        if (capacity >= 0x10u)
            std::free(*reinterpret_cast<void**>(entry));
        *reinterpret_cast<uint32_t*>(entry + 0x10) = 0;
        *reinterpret_cast<uint32_t*>(entry + 0x14) = 0x0Fu;
        *entry = 0;
        entry += 0x2C;
    }
}

// sub_1004B750's push_back: the name's fields, the payload's size at +0x18 and
// the table at +0x28. The three offsets the builder recomputes stay zero.
static void OocRegister(uint8_t* table)
{
    OocPakEntry entry{};
    std::memcpy(entry.name, kOocName, sizeof(kOocName));
    entry.name_size = static_cast<uint32_t>(sizeof(kOocName) - 1);
    entry.name_capacity = static_cast<uint32_t>(sizeof(entry.name) - 1);
    entry.file_size = static_cast<uint32_t>(kOocTableSize);
    entry.data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table));
    OocPakTable().push_back(entry);
}

// sub_1004B750: decode, register the entry, build the archive, publish the byte
// count the mount string carries and return the archive. Its one-shot guard
// dword_10159C00 is read and never written anywhere in the original, so every
// call re-runs all of this and leaks the previous archive: kept 1:1.
static uint32_t OocInitialise()
{
    // Unchecked, like the original: the decode writes through the block whether
    // or not the allocation succeeded.
    auto* table = static_cast<uint8_t*>(std::malloc(kOocTableSize));
    static_cast<void>(OocDecodeBody(table));  // the original discards the answer
    OocRegister(table);
    // The builder clears the vector itself (its last call), and its final write
    // cursor is the archive's byte count: the original's `mov ecx,
    // dword_10159C78 / mov dword_10159BFC, ecx` after the call.
    uint8_t* archive = static_cast<uint8_t*>(OocBuildArchiveBody());
    zpatch_ooc_state = zpatch_ooc_offset;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(archive));
}

// fnv1a over a recovered block, for the self-check below only: it is the
// fingerprint the recovered data's own header records.
    static uint32_t OocFnv1a(const uint8_t* data, size_t size)
    {
        uint32_t hash = 2166136261u;
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= data[index];
            hash *= 16777619u;
        }
        return hash;
    }

// The offline proof of the three rebuilt transforms: decode the recovered
// resource, register the table, build the archive, and compare each result with
// the fingerprint the recovered bytes must produce. Answers nullptr on success and
// a short description of the first difference otherwise.
//
// The shipped ASI never calls this. The payload path only runs when the game
// calls the wrapper at 0xB70733, which no offline tool can reach, so the decoder
// and the builder would otherwise be unexecuted code; `zp_dump --ooc-selftest`
// runs them against the pinned data instead (docs/parity.md). The two fnv1a
// values are recomputed from `game_refs/ZolikaPatch_original.asi.off` by the
// recipe in source/ooc_finale2.inc's header.
export const char* OocSelfCheck()
{
    auto* table = static_cast<uint8_t*>(std::malloc(kOocTableSize));
    if (table == nullptr)
        return "the decoded table's allocation failed";
    if (OocDecodeBody(table) == 0)
    {
        std::free(table);
        return "the resource did not decode: terminator, output length or Adler-32 trailer";
    }
    const uint32_t table_hash = OocFnv1a(table, kOocTableSize);
    OocRegister(table);
    uint8_t* archive = static_cast<uint8_t*>(OocBuildArchiveBody());
    const uint32_t final_offset = zpatch_ooc_offset;

    const char* failure = nullptr;
    if (table_hash != 0xC883859Fu)
        failure = "the decoded table is not the recovered 179,536 bytes";
    else if (final_offset != 182272u)
        failure = "the archive is not the recovered 182,272 bytes";
    else if (OocFnv1a(archive, final_offset) != 0xB5AF7B10u)
        failure = "the archive is not the recovered layout";
    else if ((archive[16] | (archive[17] << 8)) != 0x10u ||
             (archive[18] | (archive[19] << 8)) != 0xE9u)
        failure = "the archive header's page and opcode words are not 16 and 0xE9";
    std::free(archive);
    return failure;
}

// The test-only seam over the rebuilt init (tests/ooc_init.cpp, ctest
// `zp_ooc_init`). Every entry names something this unit already has: `Initialise`
// is the function `OocWrapBody` (0x1004C350) calls, `Decode` is the transcribed
// driver 0x1004B570, `Register` is the push_back `sub_1004B750` makes at
// 0x1004C287 (the five field stores and the `add dword_1016E440, 2Ch`),
// `FrontEntry` reads the fake-pak vector at 0x1016E43C, `ClearEntries` is the
// transcribed vector clear 0x10056500 that every init call ends with, and
// `ResourceBytes` is the decoded resource itself. Nothing here is a second
// implementation and nothing is instrumented: the init's own code is untouched, so
// a test that drives it measures the shipped sequence.
//
// What it can change: a call writes the module's own slots (`zpatch_ooc_offset`,
// `zpatch_ooc_state`, the fake-pak vector) exactly as the game's call would, and
// hands the caller the archive the init built -- a block the original never frees
// either, so a test that runs it N times owns N archives. It installs no arm, writes
// no site, and scans nothing. Nor does it reach the shipped image: nothing in the ASI
// calls these entries and they have no PE export, so /OPT:REF drops them (the linker
// map has no `OocInit` symbol), while a test links the same object and calls them.
//
// One fact the seam exists to make visible: `dword_10159C00` (the original's
// one-shot guard) has **no writer** -- it is named exactly once in the whole
// image, at the read itself -- so the init re-runs on every call. This rebuild has
// no guard variable at all, which is that fact stated in code: the second call
// does the whole body again and leaks the previous archive, and `Run.slots` is what
// lets a test watch each call leave the same 182,272-byte cursor behind.
export namespace OocInit
{
    // The three slots a call leaves, by the ASI dword each one is.
    struct Slots
    {
        uint32_t bytes;    // dword_10159C78: the builder's write cursor
        uint32_t state;    // dword_10159BFC: the archive's byte count the mount carries
        uint32_t entries;  // zpatch_ooc_pak.size(): the fake-pak registration
    };

    // A call's answer plus the slots it left. `archive` is EAX, the pointer the
    // wrapper mounts; the archive is the caller's and is never freed.
    struct Run
    {
        uint32_t archive;
        Slots slots;
    };

    // The entry the init registers, read back out of zpatch_ooc_pak (44 bytes, the
    // original's 0x1016E43C element). `entries` is the vector's size, not a field.
    struct PakEntry
    {
        char name[16];           // +0x00 the std::string's inline buffer
        uint32_t name_size;      // +0x10
        uint32_t name_capacity;  // +0x14
        uint32_t file_size;      // +0x18 the decoded table's size
        uint32_t stored_offset;  // +0x1C
        uint32_t stored_size;    // +0x20
        uint32_t padding;        // +0x24
        uint32_t data;           // +0x28 the decoded table
        uint32_t entries;
    };

    // The recovered resource the decode reads (source/ooc_finale2.inc), so a test
    // can hand the production init a damaged one.
    struct Resource
    {
        uint32_t address;
        uint32_t size;
    };

    // One call of the rebuilt init: the same function the mounting wrapper calls.
    export Run Initialise();

    // The slots as they stand, without calling anything.
    export Slots Current();

    // The transcribed decode driver (0x1004B570): 0x2BD50 on success, 0 otherwise.
    export uint32_t Decode(uint32_t table);

    // The init's registration step: answers zpatch_ooc_pak.size() afterwards.
    export uint32_t Register(uint32_t table);

    // The front entry of zpatch_ooc_pak; false when the vector is empty.
    export bool FrontEntry(PakEntry& entry);

    // The builder's own clear (0x10056500), which every init call ends with.
    export uint32_t ClearEntries();

    // The recovered resource's address and size, as the decode body names it.
    export Resource ResourceBytes();
}

OocInit::Run OocInit::Initialise()
{
    Run run{};
    run.archive = OocInitialise();
    run.slots = Current();
    return run;
}

OocInit::Slots OocInit::Current()
{
    return { zpatch_ooc_offset, zpatch_ooc_state,
        static_cast<uint32_t>(zpatch_ooc_pak.size()) };
}

uint32_t OocInit::Decode(uint32_t table)
{
    return static_cast<uint32_t>(OocDecodeBody(reinterpret_cast<uint8_t*>(table)));
}

uint32_t OocInit::Register(uint32_t table)
{
    OocRegister(reinterpret_cast<uint8_t*>(table));
    return static_cast<uint32_t>(zpatch_ooc_pak.size());
}

bool OocInit::FrontEntry(PakEntry& entry)
{
    if (zpatch_ooc_pak.empty())
        return false;
    const OocPakEntry& front = zpatch_ooc_pak.front();
    std::memcpy(entry.name, front.name, sizeof(entry.name));
    entry.name_size = front.name_size;
    entry.name_capacity = front.name_capacity;
    entry.file_size = front.file_size;
    entry.stored_offset = front.stored_offset;
    entry.stored_size = front.stored_size;
    entry.padding = front.padding;
    entry.data = front.data;
    entry.entries = static_cast<uint32_t>(zpatch_ooc_pak.size());
    return true;
}

uint32_t OocInit::ClearEntries()
{
    OocClearPakBody();
    return static_cast<uint32_t>(zpatch_ooc_pak.size());
}

OocInit::Resource OocInit::ResourceBytes()
{
    return { static_cast<uint32_t>(reinterpret_cast<uintptr_t>(kOocResource)),
        static_cast<uint32_t>(sizeof(kOocResource)) };
}

namespace
{
    // The two anchors, as the original handler's own scan strings name them. The
    // first is five bytes starting inside the `mov eax, <magic>` that precedes the
    // resource compare; the second is the device-lookup call the wrapper replaces.
    constexpr std::string_view kOocBranchSignature = "A9 39 44 24 18";
    constexpr std::string_view kOocWrapperSignature =
        "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56";

    // sub_10001670: the target a `call` at `site` names (`site + 5 + disp32`).
    uint32_t CallTarget(const std::byte* site)
    {
        int32_t displacement{};
        std::memcpy(&displacement, site + 1, sizeof(displacement));
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site) + 5 + displacement);
    }

    // The publish tier reads what the install tier overwrites: the resume is two
    // instructions past the five bytes the E9 covers, and the wrapper replays the
    // call at the site, so its original target has to be read before the retarget.
    void PublishOutOfCommissionFix()
    {
        if (!Loader::Enabled("OutOfCommissionFix"))
            return;

        auto pattern = find_pattern(kOocBranchSignature);
        if (!pattern.empty())
        {
            zpatch_ooc_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 0x11);
        }

        pattern = find_pattern(kOocWrapperSignature);
        if (!pattern.empty())
        {
            const auto* match = pattern.get_first<std::byte>(0);
            zpatch_ooc_mount = CallTarget(match - 0x33);
            zpatch_ooc_original = CallTarget(match);
        }
    }

    // The install tier: the E9 at the first anchor's +7 and the E8 retarget at the
    // second anchor, which is the original handler's write set on every build its
    // scan hits.
    void InstallOutOfCommissionFix()
    {
        if (!Loader::Enabled("OutOfCommissionFix"))
            return;

        auto pattern = find_pattern(kOocBranchSignature);
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&OocBranchBody), 0x1004C3C0u);
            injector::MakeJMP(pattern.get_first(7), reinterpret_cast<void*>(&OocBranchBody), true);
        }

        pattern = find_pattern(kOocWrapperSignature);
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&OocWrapBody), 0x1004C350u);
            injector::MakeCALL(pattern.get_first(0), reinterpret_cast<void*>(&OocWrapBody), true);
        }
    }
}

// Registered like every other arm: the publish tier reads the two sites the
// install tier overwrites, and the install tier owns the writes. The decoder pair
// carries its recovered VAs too, so a tool that ever branches into it can express
// the branch in the original's coordinates.
export void RegisterOoc()
{
    Bodies::Register(reinterpret_cast<uintptr_t>(&OocStepBody), 0x1004B150u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&OocDecodeBody), 0x1004B570u);

    Loader::PublishEvent() += []
    {
        PublishOutOfCommissionFix();
    };

    Loader::InstallEvent() += []
    {
        InstallOutOfCommissionFix();
    };
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// The transcribed bodies. They are file-scope `static` rather than members of the
// anonymous namespace above for one reason: a proof row names a body's symbol in
// the linker map, and MSVC decorates an anonymous-namespace function with a
// generated `?A0x…` scope this file's edits would churn. A file-scope `static`
// keeps the plain `?Ooc…@@YAXXZ` name the other transcribed bodies have, and it
// still cannot be exported (a naked function with external linkage in a module
// interface unit is written to the BMI and every importer fails with C2488).

// sub_1004B150, recovered VA 0x1004B150 (406 instructions): the decoder's opcode
// step. ECX is the stream's source cursor on entry and EAX the next one on exit --
// the original's own contract, and a step that does not advance answers its input,
// which is the driver's stop signal. The stream's four words are absolute operands
// in the original (its own .bss), so they are module slots here; the three `call`
// sites are the CRT's memcpy through the wrapper above.
//
// The dispatch is the original's chain of comparisons, not a switch: `>= 0x80` is
// the two-byte back-reference, `>= 0x40` the three-byte one biased by 0x3FFF,
// `>= 0x20` the short literal whose count is the opcode less 31, and below that
// 0x18/0x10/0x08/7/6/4 in that order. Everything else (0, 1, 2, 3, 5) falls through
// to the shared epilogue with the cursor unchanged.
static __declspec(naked) void OocStepBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push ecx
        push ebx
        push esi
        mov esi, ecx
        push edi
        mov dl, byte ptr [esi]
        cmp dl, 20h
        jb ooc_step_below20
        movzx ebx, dl
        lea eax, [esi + 1]
        cmp dl, 80h
        jb ooc_step_40
        mov ecx, dword ptr [zpatch_ooc_cursor]
        lea edi, [ebx - 7Fh]
        movzx eax, byte ptr [eax]
        mov edx, ecx
        mov ebx, dword ptr [zpatch_ooc_end]
        sub edx, eax
        dec edx
        lea eax, [ecx + edi]
        cmp eax, ebx
        ja ooc_step_ov2
        cmp edx, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy2
        lea eax, [ebx + 1]
    ooc_step_ov2:
        add esi, 2
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_copy2:
        test edi, edi
        je ooc_step_ret2
        // The original's three-byte alignment NOP (0F 1F 00, `nop dword ptr [eax]`).
        // The inline assembler refuses `nop` with a memory operand and has no `db`,
        // so the bytes are emitted one at a time; they are load-bearing, because
        // every instruction after them sits at the same offset on both sides and
        // the proof compares internal branch targets by offset.
        _emit 0x0F
        _emit 0x1F
        _emit 0x00
    ooc_step_copy2_loop:
        mov al, byte ptr [edx]
        lea edx, [edx + 1]
        mov byte ptr [ecx], al
        inc ecx
        sub edi, 1
        jne ooc_step_copy2_loop
        mov dword ptr [zpatch_ooc_cursor], ecx
    ooc_step_ret2:
        add esi, 2
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_40:
        cmp dl, 40h
        jb ooc_step_20
        movzx edi, byte ptr [esi + 2]
        mov ecx, dword ptr [zpatch_ooc_cursor]
        inc edi
        movzx eax, byte ptr [eax]
        mov edx, ecx
        shl ebx, 8
        sub edx, ebx
        mov ebx, dword ptr [zpatch_ooc_end]
        sub edx, eax
        lea eax, [ecx + edi]
        add edx, 3FFFh
        cmp eax, ebx
        ja ooc_step_ov3
        cmp edx, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy3
        lea eax, [ebx + 1]
    ooc_step_ov3:
        add esi, 3
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
        // The original's five-byte alignment NOP (66 0F 1F 44 00 00, `nop word ptr
        // [eax+eax]` with a zero displacement). The inline assembler only forms the
        // four-byte encoding of that instruction, and the extra byte is
        // load-bearing: every instruction after it sits one byte later and the
        // proof compares internal branch targets by offset.
        _emit 0x66
        _emit 0x0F
        _emit 0x1F
        _emit 0x44
        _emit 0x00
        _emit 0x00
    ooc_step_copy3:
        mov al, byte ptr [edx]
        lea edx, [edx + 1]
        mov byte ptr [ecx], al
        inc ecx
        sub edi, 1
        jne ooc_step_copy3
        add esi, 3
        mov dword ptr [zpatch_ooc_cursor], ecx
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_20:
        mov edi, dword ptr [zpatch_ooc_cursor]
        add ebx, -31
        mov ecx, dword ptr [zpatch_ooc_end]
        add edi, ebx
        cmp edi, ecx
        ja ooc_step_lit_over
        cmp eax, dword ptr [zpatch_ooc_lower]
        jae ooc_step_lit_copy
        lea edi, [ecx + 1]
        movzx eax, dl
        add esi, -30
        mov dword ptr [zpatch_ooc_cursor], edi
        add esi, eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_lit_copy:
        push ebx
        push eax
        push dword ptr [zpatch_ooc_cursor]
        call OocCopyBlock
        mov dl, byte ptr [esi]
        add esp, 0Ch
    ooc_step_lit_over:
        movzx eax, dl
        add esi, -30
        add esi, eax
        mov dword ptr [zpatch_ooc_cursor], edi
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below20:
        cmp dl, 18h
        jb ooc_step_below18
        movzx eax, byte ptr [esi + 1]
        mov ecx, dword ptr [zpatch_ooc_cursor]
        mov edi, ecx
        movzx ebx, byte ptr [esi + 3]
        shl eax, 8
        inc ebx
        sub edi, eax
        movzx eax, dl
        mov edx, dword ptr [zpatch_ooc_end]
        shl eax, 10h
        sub edi, eax
        movzx eax, byte ptr [esi + 2]
        sub edi, eax
        lea eax, [ecx + ebx]
        add edi, 17FFFFh
        cmp eax, edx
        ja ooc_step_ov4
        cmp edi, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy4
        lea eax, [edx + 1]
    ooc_step_ov4:
        add esi, 4
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_copy4:
        mov al, byte ptr [edi]
        lea edi, [edi + 1]
        mov byte ptr [ecx], al
        inc ecx
        sub ebx, 1
        jne ooc_step_copy4
        add esi, 4
        mov dword ptr [zpatch_ooc_cursor], ecx
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below18:
        cmp dl, 10h
        jb ooc_step_below10
        movzx ecx, byte ptr [esi + 3]
        movzx ebx, byte ptr [esi + 4]
        movzx eax, byte ptr [esi + 1]
        inc ebx
        shl ecx, 8
        shl eax, 8
        add ebx, ecx
        mov ecx, dword ptr [zpatch_ooc_cursor]
        mov edi, ecx
        sub edi, eax
        movzx eax, dl
        mov edx, dword ptr [zpatch_ooc_end]
        shl eax, 10h
        sub edi, eax
        movzx eax, byte ptr [esi + 2]
        sub edi, eax
        lea eax, [ecx + ebx]
        add edi, 0FFFFFh
        cmp eax, edx
        ja ooc_step_ov5
        cmp edi, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy5
        lea eax, [edx + 1]
    ooc_step_ov5:
        add esi, 5
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_copy5:
        mov al, byte ptr [edi]
        lea edi, [edi + 1]
        mov byte ptr [ecx], al
        inc ecx
        sub ebx, 1
        jne ooc_step_copy5
        add esi, 5
        mov dword ptr [zpatch_ooc_cursor], ecx
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below10:
        cmp dl, 8
        jb ooc_step_below8
        mov bl, byte ptr [esi + 1]
        mov edi, dword ptr [zpatch_ooc_cursor]
        movzx ecx, dl
        shl ecx, 8
        movzx eax, bl
        add eax, 0FFFFF801h
        add ecx, eax
        lea eax, [esi + 2]
        add edi, ecx
        mov dword ptr [ebp - 4], ecx
        mov ecx, dword ptr [zpatch_ooc_end]
        cmp edi, ecx
        ja ooc_step_ov6
        cmp eax, dword ptr [zpatch_ooc_lower]
        jae ooc_step_copy6
        lea edi, [ecx + 1]
        jmp ooc_step_ov6
    ooc_step_copy6:
        push dword ptr [ebp - 4]
        push eax
        push dword ptr [zpatch_ooc_cursor]
        call OocCopyBlock
        mov dl, byte ptr [esi]
        add esp, 0Ch
        mov bl, byte ptr [esi + 1]
    ooc_step_ov6:
        movzx ecx, dl
        shl ecx, 8
        movzx eax, bl
        add eax, esi
        mov dword ptr [zpatch_ooc_cursor], edi
        pop edi
        lea esi, [ecx - 7FDh]
        add esi, eax
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below8:
        cmp dl, 7
        jne ooc_step_below7
        mov dl, byte ptr [esi + 1]
        mov bl, byte ptr [esi + 2]
        mov edi, dword ptr [zpatch_ooc_cursor]
        movzx ecx, dl
        shl ecx, 8
        movzx eax, bl
        inc eax
        add ecx, eax
        lea eax, [esi + 3]
        add edi, ecx
        mov dword ptr [ebp - 4], ecx
        mov ecx, dword ptr [zpatch_ooc_end]
        cmp edi, ecx
        ja ooc_step_ov7
        cmp eax, dword ptr [zpatch_ooc_lower]
        jae ooc_step_copy7
        lea edi, [ecx + 1]
        jmp ooc_step_ov7
    ooc_step_copy7:
        push dword ptr [ebp - 4]
        push eax
        push dword ptr [zpatch_ooc_cursor]
        call OocCopyBlock
        mov dl, byte ptr [esi + 1]
        add esp, 0Ch
        mov bl, byte ptr [esi + 2]
    ooc_step_ov7:
        movzx ecx, dl
        shl ecx, 8
        movzx eax, bl
        add eax, esi
        mov dword ptr [zpatch_ooc_cursor], edi
        pop edi
        lea esi, [ecx + 4]
        add esi, eax
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below7:
        cmp dl, 6
        jne ooc_step_below6
        movzx eax, byte ptr [esi + 1]
        mov edx, dword ptr [zpatch_ooc_cursor]
        mov ecx, edx
        movzx edi, byte ptr [esi + 4]
        mov ebx, dword ptr [zpatch_ooc_end]
        inc edi
        shl eax, 10h
        sub ecx, eax
        movzx eax, byte ptr [esi + 2]
        shl eax, 8
        sub ecx, eax
        movzx eax, byte ptr [esi + 3]
        sub ecx, eax
        lea eax, [edx + edi]
        dec ecx
        cmp eax, ebx
        ja ooc_step_ov8
        cmp ecx, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy8
        lea eax, [ebx + 1]
    ooc_step_ov8:
        add esi, 5
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_copy8:
        mov al, byte ptr [ecx]
        lea ecx, [ecx + 1]
        mov byte ptr [edx], al
        inc edx
        sub edi, 1
        jne ooc_step_copy8
        add esi, 5
        mov dword ptr [zpatch_ooc_cursor], edx
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_step_below6:
        cmp dl, 4
        jne ooc_step_none
        movzx ecx, byte ptr [esi + 4]
        movzx edi, byte ptr [esi + 5]
        movzx eax, byte ptr [esi + 1]
        inc edi
        mov ebx, dword ptr [zpatch_ooc_end]
        shl ecx, 8
        shl eax, 10h
        add edi, ecx
        mov ecx, dword ptr [zpatch_ooc_cursor]
        mov edx, ecx
        sub edx, eax
        movzx eax, byte ptr [esi + 2]
        shl eax, 8
        sub edx, eax
        movzx eax, byte ptr [esi + 3]
        sub edx, eax
        lea eax, [ecx + edi]
        dec edx
        cmp eax, ebx
        ja ooc_step_ov9
        cmp edx, dword ptr [zpatch_ooc_begin]
        jae ooc_step_copy9
        lea eax, [ebx + 1]
    ooc_step_ov9:
        add esi, 6
        mov dword ptr [zpatch_ooc_cursor], eax
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
        nop
    ooc_step_copy9:
        mov al, byte ptr [edx]
        lea edx, [edx + 1]
        mov byte ptr [ecx], al
        inc ecx
        sub edi, 1
        jne ooc_step_copy9
        mov dword ptr [zpatch_ooc_cursor], ecx
        add esi, 6
    ooc_step_none:
        pop edi
        mov eax, esi
        pop esi
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_1004B570, recovered VA 0x1004B570 (159 instructions): the decode driver. ECX
// is the output buffer on entry (`__fastcall`-shaped, which is how the original's
// init calls it: `mov ecx, eax; call`), the answer is 0x2BD50 on success and 0 on
// any failed check. It seeds the stream's four words from the recovered resource,
// steps until the opcode does not advance, then requires the `05 FA` terminator,
// the exact output length and the Adler-32 trailer -- which the original computes
// inline, not through a call, so its loop is transcribed here too: 1872 bytes
// first (the length modulo 5552, constant-folded by the original's compiler), then
// full 5552-byte blocks, each reduced modulo 65521 by the same magic multiply.
//
// The original's init discards the answer, so a resource that failed its own
// checksum is registered and mounted as it decoded, exactly as here.
static __declspec(naked) int __fastcall OocDecodeBody(uint8_t* table)
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 1Ch
        push ebx
        push esi
        push edi
        mov edi, ecx
        mov dword ptr [zpatch_ooc_lower], OFFSET kOocResource - 16
        mov ecx, OFFSET kOocResource
        mov dword ptr [zpatch_ooc_begin], edi
        mov dword ptr [zpatch_ooc_cursor], edi
        lea ebx, [edi + 2BD50h]
        mov dword ptr [zpatch_ooc_end], ebx
        call OocStepBody
        mov dword ptr [ebp - 14h], eax
        cmp eax, OFFSET kOocResource
        je ooc_decode_terminator
    ooc_decode_step:
        cmp dword ptr [zpatch_ooc_cursor], ebx
        ja ooc_decode_fail
        mov ecx, eax
        mov esi, eax
        call OocStepBody
        mov dword ptr [ebp - 14h], eax
        cmp eax, esi
        jne ooc_decode_step
    ooc_decode_terminator:
        cmp byte ptr [eax], 5
        jne ooc_decode_fail
        cmp byte ptr [eax + 1], 0FAh
        jne ooc_decode_fail
        cmp dword ptr [zpatch_ooc_cursor], ebx
        jne ooc_decode_fail
        mov dword ptr [ebp - 10h], 2BD50h
        mov ecx, 1
        xor esi, esi
        mov dword ptr [ebp - 4], 750h
    ooc_decode_block:
        mov dword ptr [ebp - 0Ch], edi
        xor ebx, ebx
    ooc_decode_unroll8:
        movzx eax, byte ptr [edi]
        mov edx, edi
        add ecx, eax
        sub edx, dword ptr [ebp - 0Ch]
        movzx eax, byte ptr [edi + 1]
        add esi, ecx
        add ecx, eax
        add edx, 0Fh
        movzx eax, byte ptr [edi + 2]
        add esi, ecx
        add ecx, eax
        add ebx, 8
        movzx eax, byte ptr [edi + 3]
        add esi, ecx
        add ecx, eax
        movzx eax, byte ptr [edi + 4]
        add esi, ecx
        add ecx, eax
        movzx eax, byte ptr [edi + 5]
        add esi, ecx
        add ecx, eax
        movzx eax, byte ptr [edi + 6]
        add esi, ecx
        add ecx, eax
        movzx eax, byte ptr [edi + 7]
        add esi, ecx
        add ecx, eax
        add edi, 8
        mov eax, dword ptr [ebp - 4]
        add esi, ecx
        cmp edx, eax
        jb ooc_decode_unroll8
        xor edx, edx
        mov dword ptr [ebp - 1Ch], esi
        mov dword ptr [ebp - 0Ch], 0
        cmp ebx, eax
        jae ooc_decode_modulo
        sub eax, ebx
        cmp eax, 2
        jb ooc_decode_single
        mov edx, dword ptr [ebp - 4]
        sub edx, ebx
        sub edx, 2
        shr edx, 1
        inc edx
        lea eax, [ebx + edx*2]
        mov ebx, dword ptr [ebp - 0Ch]
        mov dword ptr [ebp - 18h], eax
        mov esi, ebx
    ooc_decode_pairs:
        movzx eax, byte ptr [edi]
        add ecx, eax
        movzx eax, byte ptr [edi + 1]
        add ebx, ecx
        add edi, 2
        add ecx, eax
        add esi, ecx
        sub edx, 1
        jne ooc_decode_pairs
        mov eax, dword ptr [ebp - 18h]
        mov dword ptr [ebp - 8], esi
        mov esi, dword ptr [ebp - 1Ch]
        cmp eax, dword ptr [ebp - 4]
        jae ooc_decode_pair_tail
        movzx eax, byte ptr [edi]
        mov edx, dword ptr [ebp - 8]
        add ecx, eax
        add esi, ecx
        inc edi
        jmp ooc_decode_combine
    ooc_decode_single:
        movzx eax, byte ptr [edi]
        mov ebx, edx
        add ecx, eax
        add esi, ecx
        inc edi
        jmp ooc_decode_combine
    ooc_decode_pair_tail:
        mov edx, dword ptr [ebp - 8]
    ooc_decode_combine:
        lea eax, [edx + ebx]
        add esi, eax
    ooc_decode_modulo:
        mov eax, 80078071h
        mul ecx
        shr edx, 0Fh
        imul eax, edx, -65521
        add ecx, eax
        mov eax, 80078071h
        mul esi
        shr edx, 0Fh
        imul eax, edx, -65521
        add esi, eax
        mov eax, dword ptr [ebp - 10h]
        sub eax, dword ptr [ebp - 4]
        mov dword ptr [ebp - 10h], eax
        mov dword ptr [ebp - 4], 15B0h
        jne ooc_decode_block
        mov edi, dword ptr [ebp - 14h]
        shl esi, 10h
        add esi, ecx
        movzx edx, byte ptr [edi + 2]
        movzx eax, byte ptr [edi + 3]
        shl edx, 8
        add edx, eax
        movzx eax, byte ptr [edi + 4]
        shl edx, 8
        add edx, eax
        movzx eax, byte ptr [edi + 5]
        shl edx, 8
        add edx, eax
        cmp esi, edx
        jne ooc_decode_fail
        pop edi
        pop esi
        mov eax, 2BD50h
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    ooc_decode_fail:
        pop edi
        pop esi
        xor eax, eax
        pop ebx
        mov esp, ebp
        pop ebp
        ret
    }
}

// The adapter between 0x10056500's call shape (first in ECX, last in EDX, the
// allocator pushed as a third argument the original ignores) and the cdecl
// helper above. The caller cleans the pushed word, as the original's
// `add esp, 4` does, so this adapter leaves it alone.
static __declspec(naked) void OocDestroyEntriesBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push edx
        push ecx
        call OocDestroyEntries
        add esp, 8
        pop ebp
        ret
    }
}

// 0x1004AB70 (5 instructions, 16 bytes): zero the 20-byte header -- one SSE
// 16-byte store and the dword at +0x10.
static __declspec(naked) void OocZeroHeaderBody()
{
    __asm
    {
        xorps xmm0, xmm0
        mov eax, ecx
        movups XMMWORD PTR [ecx], xmm0
        mov DWORD PTR [ecx + 10h], 0
        ret
    }
}

// 0x1004AB80 (4 instructions, 9 bytes): zero the 16-byte record.
static __declspec(naked) void OocZeroRecordBody()
{
    __asm
    {
        xorps xmm0, xmm0
        mov eax, ecx
        movups XMMWORD PTR [ecx], xmm0
        ret
    }
}

// 0x1004AB90 (9 instructions, 27 bytes): merge the pad length into the
// record's +0x0C word, keeping the top five bits (the stored-offset flag).
// `ret 4`: the pad length is a stack argument the callee pops.
static __declspec(naked) void OocSetRecordPadBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov ax, WORD PTR [ecx + 0Eh]
        mov edx, 0F800h
        and ax, dx
        or ax, WORD PTR [ebp + 8]
        mov WORD PTR [ecx + 0Eh], ax
        pop ebp
        ret 4
    }
}

// 0x1004ABB0 (6 instructions, 18 bytes): `size / 0x800`, rounded up.
static __declspec(naked) void OocPageCountBody()
{
    __asm
    {
        mov eax, ecx
        shr eax, 0Bh
        test ecx, 7FFh
        lea ecx, [eax + 1]
        cmovnz eax, ecx
        ret
    }
}

// 0x1004ABD0 (20 instructions, 45 bytes): copy one chunk into the archive at
// the write cursor and advance the cursor. ECX is the source, EDX the element
// size, and the two stack words the count and the archive's base. The CRT
// memcpy is the module's `OocCopyBlock` (the inline assembler refuses a direct
// `call memcpy`); the shape -- three pushes, `add esp, 0Ch` -- is the
// original's.
static __declspec(naked) void OocAppendBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        push esi
        mov esi, DWORD PTR [zpatch_ooc_offset]
        add eax, esi
        push edi
        mov edi, edx
        imul edi, DWORD PTR [ebp + 8]
        push edi
        push ecx
        push eax
        call OocCopyBlock
        add esp, 0Ch
        add esi, edi
        mov DWORD PTR [zpatch_ooc_offset], esi
        pop edi
        pop esi
        pop ebp
        ret
    }
}

// 0x1004AC00 (28 instructions, 59 bytes): the total bytes the names occupy,
// each with its NUL. The name's first dword is the std::string's buffer
// pointer when the capacity is at least 0x10, and the inline buffer otherwise.
static __declspec(naked) void OocNameBytesBody()
{
    __asm
    {
        mov edx, DWORD PTR [zpatch_ooc_pak]
        push ebx
        mov ebx, DWORD PTR [zpatch_ooc_pak + 4]
        push esi
        xor esi, esi
        cmp edx, ebx
        je ooc_name_bytes_done
        push edi
    ooc_name_bytes_loop:
        cmp DWORD PTR [edx + 14h], 10h
        mov eax, edx
        jb ooc_name_bytes_short
        mov eax, DWORD PTR [edx]
    ooc_name_bytes_short:
        lea edi, [eax + 1]
    ooc_name_bytes_scan:
        mov cl, BYTE PTR [eax]
        inc eax
        test cl, cl
        jne ooc_name_bytes_scan
        sub eax, edi
        inc esi
        add edx, 2Ch
        add esi, eax
        cmp edx, ebx
        jne ooc_name_bytes_loop
        pop edi
    ooc_name_bytes_done:
        mov eax, esi
        pop esi
        pop ebx
        ret
    }
}

// 0x1004AC40 (23 instructions, 66 bytes): the archive's payload pages -- the
// same round-up as 0x1004ABB0, summed over every entry. The original's 11-byte
// loop padding (`66 66 66 0F 1F 84 00 00 00 00 00`) is emitted as bytes: the
// inline assembler's `nop` is one byte, and the instructions after it sit at
// the same relative offsets on both sides.
static __declspec(naked) void OocPageTotalBody()
{
    __asm
    {
        mov ecx, DWORD PTR [zpatch_ooc_pak]
        push ebx
        mov ebx, DWORD PTR [zpatch_ooc_pak + 4]
        push edi
        xor edi, edi
        cmp ecx, ebx
        je ooc_page_total_done
        push esi
        _emit 066h
        _emit 066h
        _emit 066h
        _emit 00Fh
        _emit 01Fh
        _emit 084h
        _emit 000h
        _emit 000h
        _emit 000h
        _emit 000h
        _emit 000h
    ooc_page_total_loop:
        mov esi, DWORD PTR [ecx + 18h]
        shr esi, 0Bh
        test DWORD PTR [ecx + 18h], 7FFh
        lea edx, [esi + 1]
        cmovne esi, edx
        add ecx, 2Ch
        add edi, esi
        cmp ecx, ebx
        jne ooc_page_total_loop
        pop esi
    ooc_page_total_done:
        mov eax, edi
        pop edi
        pop ebx
        ret
    }
}

// 0x100564F0 (2 instructions, 3 bytes): the vector's begin pointer.
static __declspec(naked) void OocVectorBeginBody()
{
    __asm
    {
        mov eax, DWORD PTR [ecx]
        ret
    }
}

// 0x100564E0 (2 instructions, 4 bytes): the vector's end pointer.
static __declspec(naked) void OocVectorEndBody()
{
    __asm
    {
        mov eax, DWORD PTR [ecx + 4]
        ret
    }
}

// 0x100564C0 (9 instructions, 23 bytes): the vector's element count -- the
// byte distance divided by the 44-byte stride through the exact reciprocal
// multiply the compiler emitted.
static __declspec(naked) void OocVectorSizeBody()
{
    __asm
    {
        mov edx, DWORD PTR [ecx + 4]
        mov eax, 2E8BA2E9h
        sub edx, DWORD PTR [ecx]
        imul edx
        sar edx, 3
        mov eax, edx
        shr eax, 1Fh
        add eax, edx
        ret
    }
}

// 0x10057710 (6 instructions, 12 bytes): the entry's name as a C string --
// the std::string's heap buffer when the capacity is at least 0x10, the inline
// buffer otherwise.
static __declspec(naked) void OocEntryNameBody()
{
    __asm
    {
        cmp DWORD PTR [ecx + 14h], 10h
        jb ooc_entry_name_short
        mov eax, DWORD PTR [ecx]
        ret
    ooc_entry_name_short:
        mov eax, ecx
        ret
    }
}

// 0x10056500 (11 instructions, 24 bytes): the vector's clear -- destroy the
// elements, then set end to begin. The destroy loop is 0x1005CD50 above.
static __declspec(naked) void OocVectorClearBody()
{
    __asm
    {
        push esi
        mov esi, ecx
        push ecx
        mov edx, DWORD PTR [esi + 4]
        mov ecx, DWORD PTR [esi]
        call OocDestroyEntriesBody
        mov eax, DWORD PTR [esi]
        add esp, 4
        mov DWORD PTR [esi + 4], eax
        pop esi
        ret
    }
}

// sub_1004ACE0, recovered VA 0x1004ACE0 (314 instructions, 1,121 bytes): the
// in-memory archive. Header, one 16-byte record per entry, the names, then at
// 0x800 the payloads with each entry's zero padding to the next page. The
// builder consumes the payload it was handed (the original frees each entry's
// block inside the payload loop) and returns an archive the caller owns and
// never frees -- the original leaks it on every call, because the init it runs
// from re-runs every time. The write cursor is the original's own global
// (dword_10159C78): the caller reads it back for the archive's byte count,
// exactly as sub_1004B750 does after its `call sub_1004ACE0`.
//
// Transcribed instruction for instruction. The stream has no exception frame
// and no TLS static-init guard: it is one /GS frame (the cookie slots are
// ordinary absolute operands), direct `E8` calls, and EBP-relative locals. Its
// twelve internal callees are the twelve bodies above, all transcribed, so the
// class-D objection that kept this rebuilt -- "every call lands in another
// rebuild" -- is gone; only the CRT boundaries remain (operator new, memset,
// memcpy, free) through the forwarders above, plus the CRT's string-destroy
// loop 0x1005CD50, which is documented at OocDestroyEntries.
//
// Two details are the original's and not the transcription's invention:
// - the `imul ecx, eax, 0` at 0x1004AD55 is the compiler's zero-offset multiply
//   (the first payload dword), not dead code: it shapes the `cmp [edx+ecx]`.
// - the record's index is the running page count plus one, and its +0x0C word
//   carries the pad length with bit 0x2000 set when the stored offset is
//   non-zero; `OocSetRecordPadBody` is the merge, and the `+0x20` store on the
//   entry itself is what the payload loop then sees.
//
// Naked and internal: an external-linkage naked function is written to the
// module and every importer fails (C2488).
static __declspec(naked) void* OocBuildArchiveBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 0A8h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push esi
        mov DWORD PTR [zpatch_ooc_offset], 0
        mov DWORD PTR [ebp - 60h], OFFSET zpatch_ooc_pak
        mov ecx, DWORD PTR [ebp - 60h]
        call OocVectorBeginBody
        mov DWORD PTR [ebp - 48h], eax
        mov ecx, DWORD PTR [ebp - 60h]
        call OocVectorEndBody
        mov DWORD PTR [ebp - 84h], eax
        jmp ooc_build_class_loop
    ooc_build_advance:
        mov eax, DWORD PTR [ebp - 48h]
        add eax, 2Ch
        mov DWORD PTR [ebp - 48h], eax
    ooc_build_class_loop:
        mov ecx, DWORD PTR [ebp - 48h]
        cmp ecx, DWORD PTR [ebp - 84h]
        je ooc_build_count
        mov edx, DWORD PTR [ebp - 48h]
        mov DWORD PTR [ebp - 2Ch], edx
        mov eax, DWORD PTR [ebp - 2Ch]
        mov ecx, DWORD PTR [eax + 28h]
        mov DWORD PTR [ebp - 58h], ecx
        mov edx, DWORD PTR [ebp - 2Ch]
        cmp DWORD PTR [edx + 18h], 10h
        jbe ooc_build_small
        mov eax, 1
        imul ecx, eax, 0
        mov edx, DWORD PTR [ebp - 58h]
        cmp DWORD PTR [edx + ecx], 5435352h
        jne ooc_build_plain
        mov eax, 1
        shl eax, 2
        mov ecx, DWORD PTR [ebp - 2Ch]
        mov edx, DWORD PTR [ebp - 58h]
        mov eax, DWORD PTR [edx + eax]
        mov DWORD PTR [ecx + 1Ch], eax
        mov ecx, 1
        shl ecx, 3
        mov edx, DWORD PTR [ebp - 2Ch]
        mov eax, DWORD PTR [ebp - 58h]
        mov ecx, DWORD PTR [eax + ecx]
        mov DWORD PTR [edx + 20h], ecx
        jmp ooc_build_class_done
    ooc_build_plain:
        mov edx, DWORD PTR [ebp - 2Ch]
        mov DWORD PTR [edx + 1Ch], 0
        mov eax, DWORD PTR [ebp - 2Ch]
        mov ecx, DWORD PTR [ebp - 2Ch]
        mov edx, DWORD PTR [ecx + 18h]
        mov DWORD PTR [eax + 20h], edx
    ooc_build_class_done:
        jmp ooc_build_advance_jump
    ooc_build_small:
        mov eax, DWORD PTR [ebp - 2Ch]
        mov DWORD PTR [eax + 1Ch], 0
        mov ecx, DWORD PTR [ebp - 2Ch]
        mov edx, DWORD PTR [ebp - 2Ch]
        mov eax, DWORD PTR [edx + 18h]
        mov DWORD PTR [ecx + 20h], eax
    ooc_build_advance_jump:
        jmp ooc_build_advance

    ooc_build_count:
        mov ecx, OFFSET zpatch_ooc_pak
        call OocVectorSizeBody
        mov DWORD PTR [ebp - 68h], eax
        call OocPageTotalBody
        shl eax, 0Bh
        add eax, 800h
        mov DWORD PTR [ebp - 64h], eax
        mov ecx, DWORD PTR [ebp - 64h]
        push ecx
        call OocAllocate
        add esp, 4
        mov DWORD PTR [ebp - 88h], eax
        mov edx, DWORD PTR [ebp - 88h]
        mov DWORD PTR [ebp - 38h], edx
        mov eax, DWORD PTR [ebp - 64h]
        push eax
        push 0
        mov ecx, DWORD PTR [ebp - 38h]
        push ecx
        call OocZeroBlock
        add esp, 0Ch
        lea ecx, [ebp - 28h]
        call OocZeroHeaderBody
        mov DWORD PTR [ebp - 28h], 86720000h
        mov edx, DWORD PTR [ebp - 28h]
        add edx, 3DFFh
        mov DWORD PTR [ebp - 28h], edx
        mov DWORD PTR [ebp - 24h], 3
        mov eax, DWORD PTR [ebp - 68h]
        mov DWORD PTR [ebp - 20h], eax
        mov esi, DWORD PTR [ebp - 68h]
        shl esi, 4
        call OocNameBytesBody
        add esi, eax
        mov DWORD PTR [ebp - 1Ch], esi
        mov ecx, 10h
        mov WORD PTR [ebp - 18h], cx
        mov edx, 0E9h
        mov WORD PTR [ebp - 16h], dx
        mov eax, DWORD PTR [ebp - 38h]
        push eax
        push 1
        mov edx, 14h
        lea ecx, [ebp - 28h]
        call OocAppendBody
        add esp, 8
        mov DWORD PTR [ebp - 5Ch], 0
        mov DWORD PTR [ebp - 7Ch], OFFSET zpatch_ooc_pak
        mov ecx, DWORD PTR [ebp - 7Ch]
        call OocVectorBeginBody
        mov DWORD PTR [ebp - 4Ch], eax
        mov ecx, DWORD PTR [ebp - 7Ch]
        call OocVectorEndBody
        mov DWORD PTR [ebp - 8Ch], eax
        jmp ooc_build_record_loop
    ooc_build_record_advance:
        mov ecx, DWORD PTR [ebp - 4Ch]
        add ecx, 2Ch
        mov DWORD PTR [ebp - 4Ch], ecx
    ooc_build_record_loop:
        mov edx, DWORD PTR [ebp - 4Ch]
        cmp edx, DWORD PTR [ebp - 8Ch]
        je ooc_build_names
        mov eax, DWORD PTR [ebp - 4Ch]
        mov DWORD PTR [ebp - 30h], eax
        lea ecx, [ebp - 14h]
        call OocZeroRecordBody
        mov ecx, DWORD PTR [ebp - 30h]
        mov edx, DWORD PTR [ecx + 20h]
        mov DWORD PTR [ebp - 14h], edx
        mov eax, DWORD PTR [ebp - 30h]
        mov ecx, DWORD PTR [eax + 1Ch]
        mov DWORD PTR [ebp - 10h], ecx
        mov edx, DWORD PTR [ebp - 5Ch]
        add edx, 1
        mov DWORD PTR [ebp - 0Ch], edx
        mov eax, DWORD PTR [ebp - 30h]
        mov ecx, DWORD PTR [eax + 18h]
        call OocPageCountBody
        mov WORD PTR [ebp - 8], ax
        mov ecx, DWORD PTR [ebp - 30h]
        mov eax, DWORD PTR [ecx + 18h]
        xor edx, edx
        mov ecx, 800h
        div ecx
        test edx, edx
        je ooc_build_record_emit
        mov edx, DWORD PTR [ebp - 30h]
        mov eax, DWORD PTR [edx + 18h]
        xor edx, edx
        mov ecx, 800h
        div ecx
        mov eax, 800h
        sub eax, edx
        mov ecx, DWORD PTR [ebp - 30h]
        mov DWORD PTR [ecx + 24h], eax
        mov edx, DWORD PTR [ebp - 30h]
        mov eax, DWORD PTR [edx + 24h]
        push eax
        lea ecx, [ebp - 14h]
        call OocSetRecordPadBody
        lea ecx, [ebp - 6]
        mov DWORD PTR [ebp - 80h], ecx
        cmp DWORD PTR [ebp - 10h], 0
        je ooc_build_record_emit
        mov edx, 1
        shl edx, 0
        mov eax, DWORD PTR [ebp - 80h]
        movzx ecx, BYTE PTR [eax + edx]
        add ecx, 20h
        mov edx, 1
        shl edx, 0
        mov eax, DWORD PTR [ebp - 80h]
        mov BYTE PTR [eax + edx], cl
    ooc_build_record_emit:
        mov ecx, DWORD PTR [ebp - 38h]
        push ecx
        push 1
        mov edx, 10h
        lea ecx, [ebp - 14h]
        call OocAppendBody
        add esp, 8
        movzx edx, WORD PTR [ebp - 8]
        add edx, DWORD PTR [ebp - 5Ch]
        mov DWORD PTR [ebp - 5Ch], edx
        jmp ooc_build_record_advance

    ooc_build_names:
        mov DWORD PTR [ebp - 6Ch], OFFSET zpatch_ooc_pak
        mov ecx, DWORD PTR [ebp - 6Ch]
        call OocVectorBeginBody
        mov DWORD PTR [ebp - 50h], eax
        mov ecx, DWORD PTR [ebp - 6Ch]
        call OocVectorEndBody
        mov DWORD PTR [ebp - 90h], eax
        jmp ooc_build_name_loop
    ooc_build_name_advance:
        mov eax, DWORD PTR [ebp - 50h]
        add eax, 2Ch
        mov DWORD PTR [ebp - 50h], eax
    ooc_build_name_loop:
        mov ecx, DWORD PTR [ebp - 50h]
        cmp ecx, DWORD PTR [ebp - 90h]
        je ooc_build_payloads
        mov edx, DWORD PTR [ebp - 50h]
        mov DWORD PTR [ebp - 70h], edx
        mov ecx, DWORD PTR [ebp - 70h]
        call OocEntryNameBody
        mov DWORD PTR [ebp - 40h], eax
        mov eax, DWORD PTR [ebp - 40h]
        add eax, 1
        mov DWORD PTR [ebp - 94h], eax
    ooc_build_name_count:
        mov ecx, DWORD PTR [ebp - 40h]
        mov dl, BYTE PTR [ecx]
        mov BYTE PTR [ebp - 39h], dl
        add DWORD PTR [ebp - 40h], 1
        cmp BYTE PTR [ebp - 39h], 0
        jne ooc_build_name_count
        mov eax, DWORD PTR [ebp - 40h]
        sub eax, DWORD PTR [ebp - 94h]
        mov DWORD PTR [ebp - 98h], eax
        mov ecx, DWORD PTR [ebp - 38h]
        push ecx
        mov edx, DWORD PTR [ebp - 98h]
        add edx, 1
        push edx
        mov ecx, DWORD PTR [ebp - 70h]
        call OocEntryNameBody
        mov edx, 1
        mov ecx, eax
        call OocAppendBody
        add esp, 8
        jmp ooc_build_name_advance

    ooc_build_payloads:
        mov DWORD PTR [zpatch_ooc_offset], 800h
        mov DWORD PTR [ebp - 74h], OFFSET zpatch_ooc_pak
        mov ecx, DWORD PTR [ebp - 74h]
        call OocVectorBeginBody
        mov DWORD PTR [ebp - 54h], eax
        mov ecx, DWORD PTR [ebp - 74h]
        call OocVectorEndBody
        mov DWORD PTR [ebp - 9Ch], eax
        jmp ooc_build_payload_loop
    ooc_build_payload_advance:
        mov eax, DWORD PTR [ebp - 54h]
        add eax, 2Ch
        mov DWORD PTR [ebp - 54h], eax
    ooc_build_payload_loop:
        mov ecx, DWORD PTR [ebp - 54h]
        cmp ecx, DWORD PTR [ebp - 9Ch]
        je ooc_build_done
        mov edx, DWORD PTR [ebp - 54h]
        mov DWORD PTR [ebp - 34h], edx
        mov eax, DWORD PTR [ebp - 38h]
        push eax
        mov ecx, DWORD PTR [ebp - 34h]
        mov edx, DWORD PTR [ecx + 18h]
        push edx
        mov edx, 1
        mov eax, DWORD PTR [ebp - 34h]
        mov ecx, DWORD PTR [eax + 28h]
        call OocAppendBody
        add esp, 8
        mov ecx, DWORD PTR [ebp - 34h]
        mov edx, DWORD PTR [ecx + 28h]
        mov DWORD PTR [ebp - 0A0h], edx
        mov eax, DWORD PTR [ebp - 0A0h]
        push eax
        call OocFreeBlock
        add esp, 4
        mov ecx, DWORD PTR [ebp - 34h]
        cmp DWORD PTR [ecx + 24h], 0
        je ooc_build_payload_next
        mov edx, DWORD PTR [ebp - 34h]
        mov eax, DWORD PTR [edx + 24h]
        push eax
        call OocAllocate
        add esp, 4
        mov DWORD PTR [ebp - 0A4h], eax
        mov ecx, DWORD PTR [ebp - 0A4h]
        mov DWORD PTR [ebp - 44h], ecx
        mov edx, DWORD PTR [ebp - 34h]
        mov eax, DWORD PTR [edx + 24h]
        push eax
        push 0
        mov ecx, DWORD PTR [ebp - 44h]
        push ecx
        call OocZeroBlock
        add esp, 0Ch
        mov edx, DWORD PTR [ebp - 38h]
        push edx
        mov eax, DWORD PTR [ebp - 34h]
        mov ecx, DWORD PTR [eax + 24h]
        push ecx
        mov edx, 1
        mov ecx, DWORD PTR [ebp - 44h]
        call OocAppendBody
        add esp, 8
        mov edx, DWORD PTR [ebp - 44h]
        mov DWORD PTR [ebp - 78h], edx
        mov eax, DWORD PTR [ebp - 78h]
        push eax
        call OocFreeBlock
        add esp, 4
        cmp DWORD PTR [ebp - 78h], 0
        jne ooc_build_pad_ok
        mov DWORD PTR [ebp - 0A8h], 0
        jmp ooc_build_payload_next
    ooc_build_pad_ok:
        mov DWORD PTR [ebp - 44h], 8123h
        mov ecx, DWORD PTR [ebp - 44h]
        mov DWORD PTR [ebp - 0A8h], ecx
    ooc_build_payload_next:
        jmp ooc_build_payload_advance

    ooc_build_done:
        mov ecx, OFFSET zpatch_ooc_pak
        call OocVectorClearBody
        mov eax, DWORD PTR [ebp - 38h]
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10001280, recovered VA 0x10001280: the bounded formatter, transcribed once
// in source/stability.ixx and proved there instruction for instruction
// (docs/rebuilt.md). A module interface cannot export a naked function, so this
// six-byte forwarder is what the wrapper's `call` names: the original's call is
// an immediate too, and the forwarder enters the body with the stack and the
// registers untouched.
static __declspec(naked) void OocFormat()
{
    __asm
    {
        jmp DWORD PTR [zpatch_format_entry]
    }
}

// 0x10056500 takes its vector in ECX (the original's own frame: `push esi; mov
// esi, ecx`), and a C++ call cannot pass one there, so the seam's clear enters it
// exactly the way the builder does at its `ooc_build_done`: `mov ecx, OFFSET
// zpatch_ooc_pak; call`. Naked and internal, like every other body in this unit.
static __declspec(naked) void OocClearPakBody()
{
    __asm
    {
        mov ecx, OFFSET zpatch_ooc_pak
        jmp OocVectorClearBody
    }
}

// sub_1004C3C0, recovered VA 0x1004C3C0 (7 instructions): the branch body the E9
// at game 0xA22E97 jumps to. The five bytes it replaces are `cmp [esp+30h], eax`
// plus the first byte of the `movq xmm0, [esp+30h]` that follows, so it replays
// both. If the value is the game's own script magic the flags are that compare's;
// if not, it compares against the archive's 0x86723DFF, and the game's `je` two
// instructions after the resume consumes whichever flags are left. That is what
// makes the archive's resource acceptable to the check the arm widens.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void OocBranchBody()
{
    __asm
    {
        cmp DWORD PTR [esp + 30h], eax
        movq xmm0, QWORD PTR [esp + 30h]
        jne ooc_branch_check
        jmp DWORD PTR [zpatch_ooc_resume]
    ooc_branch_check:
        mov eax, 86723DFFh
        cmp DWORD PTR [esp + 30h], eax
        jmp DWORD PTR [zpatch_ooc_resume]
    }
}

// sub_1004C350, recovered VA 0x1004C350 (41 instructions to its `ret`): the
// wrapper the E8 at game 0xB70733 is retargeted to. It runs the init, formats the
// mount string from the archive's address and byte count, registers it with the
// game's device register (the call at match-0x33 the arm published), and then
// calls the function the site's E8 named before the retarget. The /GS frame is the
// original's: the same cookie, the same 0x44-byte buffer, the same 0x38-byte
// cleanup after the two calls.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void OocWrapBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 44h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push esi
        mov esi, DWORD PTR [ebp + 8]
        call OocInitialise
        test eax, eax
        je ooc_wrap_original
        push 0
        push 3Ah
        push 30h
        push DWORD PTR [zpatch_ooc_state]
        push eax
        push 24h
        push 3Ah
        push OFFSET zpatch_ooc_mount_prefix
        push OFFSET zpatch_ooc_mount_format
        lea eax, [ebp - 44h]
        push 40h
        push eax
        call OocFormat
        push -1
        lea eax, [ebp - 44h]
        push 1
        push eax
        call DWORD PTR [zpatch_ooc_mount]
        add esp, 38h
    ooc_wrap_original:
        push DWORD PTR [ebp + 0Ch]
        push esi
        call DWORD PTR [zpatch_ooc_original]
        mov ecx, DWORD PTR [ebp - 4]
        add esp, 8
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}
