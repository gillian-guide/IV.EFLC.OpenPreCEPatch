#include <common.hxx>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

import ooc;

// The OutOfCommissionFix init (`sub_1004B750`, 0x1004B750, 991 instructions,
// `docs/rebuilt.md` Â§3 class B), asserted by behaviour with no game image.
//
// What is already pinned is the transform: `zp_dump --ooc-selftest` runs the
// decode, the registration and the archive build against the recovered resource
// and compares the results with the fingerprints the recovered bytes carry. What
// that does *not* pin is the init's own semantics -- when it runs, what it leaves
// in the module's slots, and what it does when its decode fails. This test pins
// exactly those three things by driving the module's own `OocInitialise`
// (`OocInit::Initialise`, `source/ooc.ixx`) and reading the slots it left.
//
// What the original's instructions say (image base 0x10000000, read off
// ../game_refs/ZolikaPatch_original.asi.off with a linear capstone sweep of the
// function's 991 instructions):
//
//   0x1004B77B  a1 00 9c 15 10     mov  eax,[0x10159C00]   ; dword_10159C00
//   0x1004B780  85 c0              test eax,eax
//   0x1004B782  0f 85 ab 0b 00 00  jne  0x1004C333         ; the shared epilogue
//
// and 0x1004C333 *is* the shared epilogue: `mov ecx,[ebp-0Ch] / mov fs:[0],ecx /
// pop ecx / pop edi / pop esi / pop ebx / mov ecx,[ebp-10h] / xor ecx,ebp /
// call __security_check_cookie / mov esp,ebp / pop ebp / ret`. So the guard is a
// *skip*, and on the skipped path EAX still holds the guard word: a guard that
// was ever set would make this function answer that word as if it were the
// archive pointer. The slot is named exactly **once** in the whole image -- at
// 0x1004B77C, the read itself -- so nothing writes it and the branch is never
// taken: every call of the wrapper re-runs the whole body.
//
// The body, in order:
//
//   0x1004B788  68 50 bd 02 00       push 0x2BD50
//   0x1004B78D  ff 15 fc 32 06 10    call dword ptr [0x100632FC]  ; malloc, unchecked
//   0x1004B793  8b c8                mov  ecx,eax
//   0x1004B798  e8 d3 fd ff ff       call 0x1004B570               ; the decode
//   0x1004B79D  83 c4 04             add  esp,4                    ; answer discarded
//
// i.e. there is no `test eax,eax` after the decode at all: the table is
// registered and wrapped whether the decode answered 0x2BD50 or 0 -- the
// original's own three checks (the `05 FA` terminator, the exact output length,
// the Adler-32 trailer) can all fail and the payload is still what gets mounted.
//
//   ...          the name is built one character at a time (the digit loops and
//                `strtol` that spell "finale2.sco"), then the entry is pushed:
//   0x1004C287  89 46 18             mov  [esi+18h],eax            ; 179536
//   0x1004C28D  89 46 1c             mov  [esi+1Ch],eax            ; 0
//   0x1004C293  89 46 20             mov  [esi+20h],eax            ; 0
//   0x1004C299  89 46 24             mov  [esi+24h],eax            ; 0
//   0x1004C29F  89 46 28             mov  [esi+28h],eax            ; the table
//   0x1004C2A2  83 05 40 e4 16 10 2c add  dword_1016E440,2Ch     ; push_back
//
//   0x1004C322  e8 b9 e9 ff ff       call 0x1004ACE0               ; the archive
//   0x1004C327  8b 0d 78 9c 15 10    mov  ecx,[0x10159C78]         ; the write cursor
//   0x1004C32D  89 0d fc 9b 15 10    mov  [0x10159BFC],ecx         ; the mount's byte count
//
// The cursor slot (`dword_10159C78`) is named five times in the image: twice in
// the transcribed append helper (`mov esi,[0x10159C78]` at 0x1004ABD7 and
// `mov [0x10159C78],esi` at 0x1004ABF3), twice in the builder (the `= 0` at
// 0x1004ACF4 and the `= 0x800` at 0x1004B00E), and once here, after the call. The
// published slot (`dword_10159BFC`) is named twice: this store, and the mounting
// wrapper's `push dword ptr [0x10159BFC]` at 0x1004C373 (the wrapper calls the
// init at 0x1004C364 and only formats the mount string when it answers non-null).
//
// The seam these checks drive is `OocInit` (`source/ooc.ixx`): `Initialise` is the
// function the wrapper calls, `Current` reads the three module slots, `Decode` is
// the transcribed driver, `Register`/`FrontEntry`/`ClearEntries` are the
// registration and the vector's own clear, and `ResourceBytes` is the recovered
// resource. Nothing is a second implementation, no site is written, no arm is
// installed and the test process has no game image.
//
// Assertion classes:
//   1. the guard has no writer, so the work happens on every call: three calls,
//      three fresh archives, three 182,272-byte archive allocations, and not one
//      of them ever freed -- the leak, measured, not inferred;
//   2. the cursor: after each call `dword_10159C78` is the archive's byte count
//      (182,272) and `dword_10159BFC` is that same count, and the archive it
//      counts hashes to the pinned fnv1a 0xB5AF7B10 with the pinned header words;
//   3. registration even on failure: a resource whose Adler-32 trailer is damaged
//      (the decode answers 0, having written the table) and one whose first opcode
//      can never terminate (the decode answers 0 having written nothing) are both
//      registered and built into an archive, with no throw;
//   4. the entry: the fake-pak entry is "finale2.sco", 179,536 bytes of table,
//      read out of the module's own vector, and the archive carries that name.
//
//     zp_ooc_init    the suite

namespace
{
    constexpr uint32_t kTableBytes = 0x2BD50;      // 179,536
    constexpr uint32_t kArchiveBytes = 182272;     // 0x2C800
    constexpr uint32_t kResourceBytes = 163796;    // 0x27FD4
    constexpr uint32_t kTableHash = 0xC883859Fu;   // the decoded table's fnv1a
    constexpr uint32_t kArchiveHash = 0xB5AF7B10u; // the archive's fnv1a
    constexpr uint32_t kPages = 88;                // ceil(0x2BD50 / 0x800)
    constexpr uint32_t kPad = 688;                 // 0x800 - 0x2BD50 % 0x800
    constexpr uint32_t kHeaderBytes = 20;          // magic,3,count,names + 16/E9
    constexpr uint32_t kRecordOffset = 0x14;       // one 16-byte record
    constexpr uint32_t kNameOffset = 0x24;         // 0x14 + 16
    constexpr uint32_t kPayloadOffset = 0x800;
    constexpr std::string_view kName = "finale2.sco";

    // The trailer's four bytes, big-endian at the end of the resource: the decode
    // reads them at `[terminator + 2]` and compares them with its Adler-32.
    constexpr uint32_t kTrailerOffset = kResourceBytes - 4;

    int checks = 0;
    int failures = 0;

    void Check(bool condition, const std::string& what)
    {
        ++checks;
        if (condition)
            std::printf("PASS %s\n", what.c_str());
        else
        {
            ++failures;
            std::printf("FAIL %s\n", what.c_str());
        }
        std::fflush(stdout);
    }

    uint32_t Fnv1a(const uint8_t* data, size_t size)
    {
        uint32_t hash = 2166136261u;
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= data[index];
            hash *= 16777619u;
        }
        return hash;
    }

    uint16_t Word(const uint8_t* data, uint32_t offset)
    {
        return static_cast<uint16_t>(data[offset] | (data[offset + 1] << 8));
    }

    uint32_t Dword(const uint8_t* data, uint32_t offset)
    {
        uint32_t value = 0;
        std::memcpy(&value, data + offset, sizeof(value));
        return value;
    }

    // ---------------------------------------------------------------------
    // The archive-allocation ledger. `OocAllocate` is `::operator new` (the
    // original calls the CRT's scalar `operator new` thunk at 0x100603F1), so the
    // program's own `operator new` is where a build becomes visible: every
    // 182,272-byte block the module asks for is one archive build, and every
    // `operator delete` of such a block would be the archive being freed -- which
    // the original never does. The replacement family below is the complete
    // C++17 set so the static CRT's own new/delete objects are never pulled in;
    // every entry routes to `std::malloc`/`std::free`, exactly as the CRT's does.
    // ---------------------------------------------------------------------
    constexpr size_t kLedgerMax = 16;
    void* g_archives[kLedgerMax];
    size_t g_archive_count = 0;
    size_t g_archive_frees = 0;

    void RecordAllocation(void* block, size_t size)
    {
        if (block == nullptr || size != kArchiveBytes || g_archive_count >= kLedgerMax)
            return;
        g_archives[g_archive_count++] = block;
    }

    void* AllocateBlock(size_t size)
    {
        void* block = std::malloc(size == 0 ? 1 : size);
        if (block != nullptr)
            RecordAllocation(block, size);
        return block;
    }

    void ReleaseBlock(void* block)
    {
        if (block != nullptr)
        {
            for (size_t index = 0; index < g_archive_count; ++index)
            {
                if (g_archives[index] == block)
                    ++g_archive_frees;
            }
        }
        std::free(block);
    }
}

void* operator new(size_t size)
{
    for (;;)
    {
        if (void* block = AllocateBlock(size))
            return block;
        const std::new_handler handler = std::get_new_handler();
        if (handler == nullptr)
            throw std::bad_alloc();
        handler();
    }
}

void* operator new[](size_t size)
{
    return ::operator new(size);
}

void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return ::operator new(size);
    }
    catch (...)
    {
        return nullptr;
    }
}

void* operator new[](size_t size, const std::nothrow_t& tag) noexcept
{
    return ::operator new(size, tag);
}

void operator delete(void* block) noexcept
{
    ReleaseBlock(block);
}

void operator delete[](void* block) noexcept
{
    ReleaseBlock(block);
}

void operator delete(void* block, size_t) noexcept
{
    ReleaseBlock(block);
}

void operator delete[](void* block, size_t) noexcept
{
    ReleaseBlock(block);
}

void operator delete(void* block, const std::nothrow_t&) noexcept
{
    ReleaseBlock(block);
}

void operator delete[](void* block, const std::nothrow_t&) noexcept
{
    ReleaseBlock(block);
}

namespace
{
    OocInit::Resource g_resource{};
    std::vector<uint8_t> g_table;  // the test's own decode of the intact resource
    std::vector<uint8_t> g_scratch;

    // Runs `body` with byte `offset` of the module's resource replaced by `value`,
    // restoring the byte and the page protection afterwards. The decode body names
    // the resource as an immediate (`mov ecx, OFFSET kOocResource`), so a damaged
    // resource can only be handed to the production init by damaging that array:
    // it lives in `.rdata`, so the window is opened with VirtualProtect. A false
    // answer means the pages could not be made writable, and the caller fails
    // rather than skipping.
    template <typename Body>
    bool WithCorruptedResource(uint32_t offset, uint8_t value, Body body)
    {
        auto* base = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(g_resource.address));
        DWORD previous = 0;
        if (!VirtualProtect(base, g_resource.size, PAGE_READWRITE, &previous))
            return false;
        const uint8_t saved = base[offset];
        base[offset] = value;
        body();
        base[offset] = saved;
        DWORD ignored = 0;
        VirtualProtect(base, g_resource.size, previous, &ignored);
        return true;
    }

    void CheckNoThrowArchive(const char* what, OocInit::Run& run, bool& threw)
    {
        threw = false;
        try
        {
            run = OocInit::Initialise();
        }
        catch (const std::exception& error)
        {
            threw = true;
            Check(false, std::format("{}: the init threw {}", what, error.what()));
            return;
        }
        catch (...)
        {
            threw = true;
            Check(false, std::format("{}: the init threw a non-std exception", what));
            return;
        }
        Check(run.archive != 0, std::format("{}: the init answers an archive (0x{:08X})",
                                     what, run.archive));
    }

    // ---------------------------------------------------------------------
    // 1: the module's state before anything runs, and the resource itself.
    // ---------------------------------------------------------------------
    void CheckBaseline()
    {
        const OocInit::Slots slots = OocInit::Current();
        Check(slots.bytes == 0 && slots.state == 0 && slots.entries == 0,
            std::format("baseline: the three slots start zero "
                        "(cursor {}, mount count {}, entries {})",
                slots.bytes, slots.state, slots.entries));

        g_resource = OocInit::ResourceBytes();
        Check(g_resource.address != 0 && g_resource.size == kResourceBytes,
            std::format("baseline: the recovered resource is 0x{:08X}, {} bytes",
                g_resource.address, g_resource.size));
        const auto* resource = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(g_resource.address));
        Check(resource[0] == 0x2A && resource[1] == 0x73 && resource[2] == 0x63 &&
                  resource[3] == 0x72,
            std::format("baseline: it starts 2A 73 63 72 (got {:02X} {:02X} {:02X} {:02X})",
                resource[0], resource[1], resource[2], resource[3]));

        // The test's own decode of the intact resource: the payload the archive is
        // supposed to carry, and the premise of every corruption check below.
        g_table.assign(kTableBytes, 0);
        const uint32_t decoded = OocInit::Decode(
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_table.data())));
        Check(decoded == kTableBytes,
            std::format("decode: the transcribed driver answers 0x{:X} (0x{:X} expected)",
                decoded, kTableBytes));
        const uint32_t tableHash = Fnv1a(g_table.data(), g_table.size());
        Check(tableHash == kTableHash,
            std::format("decode: the table hashes 0x{:08X} (the recovered 0x{:08X})",
                tableHash, kTableHash));
        Check(Dword(g_table.data(), 0) == 0x0E726373u,
            std::format("decode: the table starts 73 63 72 0E (got 0x{:08X})",
                Dword(g_table.data(), 0)));

        g_scratch.assign(kTableBytes, 0);
    }

    // ---------------------------------------------------------------------
    // 2: one call of the init -- the archive, the header, the record, the name,
    // the payload, and the two slots the call publishes.
    // ---------------------------------------------------------------------
    void CheckFirstRun(OocInit::Run& run)
    {
        run = OocInit::Initialise();
        const auto* archive = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(run.archive));
        Check(run.archive != 0, std::format("run 1: the init answers an archive (0x{:08X})",
                                      run.archive));
        if (run.archive == 0)
            return;

        // The cursor is the archive's byte count, and the mount slot is that same
        // word: `mov ecx,[0x10159C78] / mov [0x10159BFC],ecx`.
        Check(run.slots.bytes == kArchiveBytes,
            std::format("run 1: dword_10159C78 (the cursor) is {} (expected {})",
                run.slots.bytes, kArchiveBytes));
        Check(run.slots.state == run.slots.bytes,
            std::format("run 1: dword_10159BFC (the mount's byte count) is {} and the cursor "
                        "is {}", run.slots.state, run.slots.bytes));
        Check(run.slots.entries == 0,
            std::format("run 1: the builder cleared the fake-pak vector (entries {})",
                run.slots.entries));

        // The archive's own bytes, over exactly the count the cursor published.
        const uint32_t hash = Fnv1a(archive, run.slots.bytes);
        Check(hash == kArchiveHash,
            std::format("run 1: the archive's {} bytes hash 0x{:08X} (the recovered "
                        "0x{:08X})", run.slots.bytes, hash, kArchiveHash));
        Check(Dword(archive, 0) == 0x86723DFFu,
            std::format("run 1: header magic 0x{:08X} (0x86723DFF)", Dword(archive, 0)));
        Check(Dword(archive, 4) == 3u,
            std::format("run 1: header version {} (3)", Dword(archive, 4)));
        Check(Dword(archive, 8) == 1u,
            std::format("run 1: header entry count {} (1 -- the registered entry)",
                Dword(archive, 8)));
        Check(Dword(archive, 12) == 16u + (kName.size() + 1),
            std::format("run 1: header name bytes {} (16 per entry + {} for \"{}\")",
                Dword(archive, 12), kName.size() + 1, kName));
        Check(Word(archive, 16) == 16u && Word(archive, 18) == 0xE9u,
            std::format("run 1: header words {} and 0x{:02X} (16 and 0xE9)", Word(archive, 16),
                Word(archive, 18)));

        // The one 16-byte record: stored size, stored offset, index, pages | pad.
        Check(Dword(archive, kRecordOffset) == kTableBytes,
            std::format("run 1: the record's stored size is {} (the table's {})",
                Dword(archive, kRecordOffset), kTableBytes));
        Check(Dword(archive, kRecordOffset + 4) == 0u,
            std::format("run 1: the record's stored offset is {} (the table is not a "
                        "0x05435352 block, so the builder stores 0 and the file size)",
                Dword(archive, kRecordOffset + 4)));
        Check(Dword(archive, kRecordOffset + 8) == 1u,
            std::format("run 1: the record's index is {} (the first page is 1)",
                Dword(archive, kRecordOffset + 8)));
        Check(Word(archive, kRecordOffset + 12) == kPages &&
                  Word(archive, kRecordOffset + 14) == kPad,
            std::format("run 1: the record's page/pad words are {}/{} (expected {}/{})",
                Word(archive, kRecordOffset + 12), Word(archive, kRecordOffset + 14), kPages,
                kPad));

        // The name, as the builder wrote the registered entry's c_str.
        Check(std::memcmp(archive + kNameOffset, kName.data(), kName.size()) == 0 &&
                  archive[kNameOffset + kName.size()] == 0,
            std::format("run 1: the name at +0x{:X} is \"{}\" and NUL-terminated",
                kNameOffset,
                std::string(reinterpret_cast<const char*>(archive + kNameOffset),
                    kName.size())));

        // The payload at 0x800 is the decoded table itself, and the tail is the
        // builder's zero padding to the next page.
        Check(std::memcmp(archive + kPayloadOffset, g_table.data(), kTableBytes) == 0,
            std::format("run 1: the payload at +0x{:X} is the decoded {} bytes (memcmp)",
                kPayloadOffset, kTableBytes));
        bool padZero = true;
        for (uint32_t index = 0; index < kPad; ++index)
            padZero = padZero && archive[kPayloadOffset + kTableBytes + index] == 0;
        Check(padZero, std::format("run 1: the {} bytes after the payload are zero", kPad));
    }

    // ---------------------------------------------------------------------
    // 3: the guard's real behaviour. `dword_10159C00` has no writer, so the
    // second and third calls do the whole body again: three archives, three
    // allocations, none of them freed.
    // ---------------------------------------------------------------------
    void CheckRepeatedRuns(const OocInit::Run& first)
    {
        const size_t before = g_archive_count;
        Check(before == 1, std::format("runs: the first call allocated one archive "
                                       "({} recorded)", before));

        OocInit::Run second = OocInit::Initialise();
        OocInit::Run third = OocInit::Initialise();
        Check(g_archive_count == before,
            std::format("runs: two more calls built no more archives ({} recorded)",
                g_archive_count));
        Check(second.archive != 0 && third.archive != 0 &&
                  second.archive == first.archive && third.archive == first.archive,
            std::format("runs: every call answers the same archive (0x{:08X}, 0x{:08X}, "
                        "0x{:08X})", first.archive, second.archive, third.archive));
        Check(second.slots.bytes == kArchiveBytes && second.slots.state == kArchiveBytes &&
                  third.slots.bytes == kArchiveBytes && third.slots.state == kArchiveBytes,
            std::format("runs: each call re-publishes the cursor and the mount count "
                        "(2nd {}/{}, 3rd {}/{})", second.slots.bytes, second.slots.state,
                third.slots.bytes, third.slots.state));

        const auto* firstBytes = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(first.archive));
        Check(Fnv1a(firstBytes, kArchiveBytes) == kArchiveHash,
            std::format("runs: the cached archive is still the recovered one (fnv1a "
                        "0x{:08X})", Fnv1a(firstBytes, kArchiveBytes)));
        Check(g_archive_frees == 0,
            std::format("runs: no archive was ever freed ({} free(s) of a recorded archive)",
                g_archive_frees));
    }

    // ---------------------------------------------------------------------
    // 4: the entry. The init's registration is transient -- the builder clears
    // the vector at its end -- so the entry is read out of the module's own
    // vector through the same `OocRegister` the init calls, and then compared
    // with the name the archive carries.
    // ---------------------------------------------------------------------
    void CheckRegistration()
    {
        const uint32_t scratchAddress =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch.data()));
        const uint32_t size = OocInit::Register(scratchAddress);
        Check(size == 1, std::format("entry: OocRegister leaves {} entries in the fake-pak "
                                     "vector (1)", size));
        OocInit::PakEntry entry{};
        const bool present = OocInit::FrontEntry(entry);
        Check(present && entry.entries == 1,
            std::format("entry: the module's vector holds one entry ({})", entry.entries));
        Check(std::memcmp(entry.name, kName.data(), kName.size()) == 0 &&
                  entry.name[kName.size()] == 0,
            std::format("entry: its name is \"{}\"", std::string(entry.name, entry.name_size)));
        Check(entry.name_size == kName.size() && entry.name_capacity == 15,
            std::format("entry: the name is a 15-capacity string of {} bytes (the original's "
                        "inline buffer; got {}/{})", kName.size(), entry.name_size,
                entry.name_capacity));
        Check(entry.file_size == kTableBytes,
            std::format("entry: file_size is {} (the decoded table's 0x{:X})",
                entry.file_size, kTableBytes));
        Check(entry.stored_offset == 0 && entry.stored_size == 0 && entry.padding == 0,
            std::format("entry: the three offsets the builder recomputes are zero "
                        "({}/{}/{})", entry.stored_offset, entry.stored_size, entry.padding));
        Check(entry.data == scratchAddress,
            std::format("entry: data is the table the registration was handed (0x{:08X}, "
                        "expected 0x{:08X})", entry.data, scratchAddress));
        Check(OocInit::ClearEntries() == 0,
            "entry: the builder's own clear (0x10056500) empties the vector again");
    }

    // ---------------------------------------------------------------------
    // 5: registration even when the decode fails. Two damages, one per failure
    // class: the Adler-32 trailer (the decode runs to the end and answers 0), and
    // the first opcode (the decode answers 0 having written nothing). Both the
    // decode's own verdict and the init's call happen *inside* the window the
    // damage is in place for: the premise ("the checks failed") and the result
    // ("it registered anyway") are then about the same bytes.
    // ---------------------------------------------------------------------
    void CheckRegistrationOnFailure(const OocInit::Run& good)
    {
        const auto* goodBytes = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(good.archive));
        const auto* resource = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(g_resource.address));

        // 5a: the trailer. The stream still terminates and fills all 179,536
        // bytes; only the checksum differs, so the decode answers 0 and the table
        // is the recovered one -- which is what the init then mounts.
        {
            OocInit::Run run{};
            bool threw = false;
            const bool opened = WithCorruptedResource(kTrailerOffset,
                static_cast<uint8_t>(resource[kTrailerOffset] ^ 0xFF), [&]
                {
                    std::fill(g_scratch.begin(), g_scratch.end(), 0);
                    const uint32_t decoded = OocInit::Decode(
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch.data())));
                    Check(decoded == 0,
                        std::format("trailer: the decode answers 0x{:X} with the trailer "
                                    "damaged ({} would be success)", decoded, kTableBytes));
                    Check(Fnv1a(g_scratch.data(), g_scratch.size()) == kTableHash,
                        std::format("trailer: the decode still wrote the recovered table "
                                    "(fnv1a 0x{:08X})",
                            Fnv1a(g_scratch.data(), g_scratch.size())));
                    CheckNoThrowArchive("trailer", run, threw);
                });
            Check(opened, "trailer: the recovered resource's page became writable");

            if (opened && !threw && run.archive != 0)
            {
                Check(run.slots.bytes == kArchiveBytes && run.slots.state == kArchiveBytes,
                    std::format("trailer: the failed decode's size is still the archive's "
                                "count ({}/{})", run.slots.bytes, run.slots.state));
                Check(std::memcmp(reinterpret_cast<const uint8_t*>(
                                      static_cast<uintptr_t>(run.archive)),
                          goodBytes, kArchiveBytes) == 0,
                    "trailer: the archive is the recovered one -- the payload was registered "
                    "and mounted as it decoded, despite the decode answering 0");
            }
        }

        // 5b: the first opcode. 0x05 is the no-progress opcode, so the driver's
        // first step answers its own cursor, the terminator test then reads the
        // resource's second byte (0x73, not 0xFA) and the decode gives up before
        // writing a single byte of the table. The init's own table block is
        // therefore never written, so the assertions below are about the
        // registration's shape (header, record, name, entry count), not the
        // payload -- and the one table-dependent read the builder makes (the
        // `cmp [table], 0x05435352` that would turn the entry into a sized block)
        // is a dword of heap content: with a fresh or a reused 179,536-byte block
        // it is never that magic, so the record stays the file-size form the
        // byte-for-byte comparison above pins.
        {
            OocInit::Run run{};
            bool threw = false;
            const bool opened = WithCorruptedResource(0, 0x05, [&]
                {
                    std::fill(g_scratch.begin(), g_scratch.end(), 0xA5);
                    const uint32_t decoded = OocInit::Decode(
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_scratch.data())));
                    Check(decoded == 0,
                        std::format("dead decode: the decode answers 0x{:X} when the stream "
                                    "cannot start ({} would be success)", decoded, kTableBytes));
                    bool untouched = true;
                    for (uint8_t byte : g_scratch)
                        untouched = untouched && byte == 0xA5;
                    Check(untouched, "dead decode: the decode wrote none of the table "
                                     "(the 0xA5 fill is intact)");
                    CheckNoThrowArchive("dead decode", run, threw);
                });
            Check(opened, "dead decode: the recovered resource's page became writable again");

            if (opened && !threw && run.archive != 0)
            {
                const auto* bytes = reinterpret_cast<const uint8_t*>(
                    static_cast<uintptr_t>(run.archive));
                Check(run.slots.bytes == kArchiveBytes && run.slots.state == kArchiveBytes,
                    std::format("dead decode: the init still publishes the archive's count "
                                "({}/{})", run.slots.bytes, run.slots.state));
                Check(std::memcmp(bytes, goodBytes, kPayloadOffset) == 0,
                    std::format("dead decode: the header, the record and the name "
                                "(+0x0..+0x{:X}) are the registered entry's, byte for byte",
                        kPayloadOffset));
                Check(Dword(bytes, 8) == 1u,
                    std::format("dead decode: the archive still carries one entry ({})",
                        Dword(bytes, 8)));
            }
        }

        // 5c: the restore. A clean call after the two damages reproduces the
        // pinned archive, so the two runs above really did read damaged bytes.
        OocInit::Run restored = OocInit::Initialise();
        const auto* restoredBytes = reinterpret_cast<const uint8_t*>(
            static_cast<uintptr_t>(restored.archive));
        Check(restored.archive != 0 && restored.archive != good.archive &&
                  Fnv1a(restoredBytes, kArchiveBytes) == kArchiveHash,
            std::format("restore: the resource decodes again after the damages (fnv1a "
                        "0x{:08X})", restored.archive != 0 ? Fnv1a(restoredBytes, kArchiveBytes)
                                                          : 0u));
    }
}

int main()
{
    // Nothing here faults on purpose, but the resource is damaged in place and a
    // copy of the init runs against it: suppress any error dialog regardless (the
    // same setting every faulting test in this suite uses).
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    try
    {
        CheckBaseline();
        OocInit::Run first{};
        CheckFirstRun(first);
        if (first.archive != 0)
        {
            CheckRepeatedRuns(first);
            CheckRegistration();
            Check(g_archive_count == 1,
                std::format("leak: one archive for the process lifetime ({})", g_archive_count));
        }

        // The ooc_cache group: the first call decodes/builds once, every later
        // call answers the same archive and re-publishes the same state. This is
        // the opt-in side of the leak above.
        const OocInit::Run cached_first = OocInit::Initialise();
        const OocInit::Run cached_second = OocInit::Initialise();
        Check(cached_first.archive != 0, "cache: the first call builds an archive");
        Check(cached_first.archive == cached_second.archive,
            "cache: the second call reuses the same archive");
        Check(cached_first.slots.bytes == cached_second.slots.bytes &&
                  cached_first.slots.state == cached_second.slots.state,
            "cache: the published cursor and state are the same");
    }
    catch (const std::exception& error)
    {
        Check(false, std::format("the test itself threw {}", error.what()));
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
