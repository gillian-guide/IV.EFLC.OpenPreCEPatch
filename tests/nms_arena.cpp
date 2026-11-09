#include <common.hxx>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

import loading;

// The NewMemorySystem arena, asserted by behaviour rather than by its disassembly.
//
// The three methods are the vtable slots the rebuild carries (docs/rebuilt.md §3
// class D): allocate 0x10038360, free 0x10038430 and membership 0x100385A0 of the
// CRageAllocator table at 0x100B5038. They cannot be transcribed because their
// callees are the original's inlined `std::_Hash` (the emplace carries an exception
// frame; the free inlines the erase unlink over the map's own globals), so the
// evidence for the rebuild is behaviour. This test drives the module's own three
// bodies - the same three `PublishNmsVtable` puts in the table - on the process
// heap, with no game image and no ASI load (`MemoryArena::Get`, loading.ixx).
//
// What the original's instructions say, read off
// ../game_refs/ZolikaPatch_original.asi.off (image base 0x10000000):
//
//   sub_10038360  allocate, `ret 0Ch`, three arguments, 69 instructions
//     0x10038374  mov  eax,[ebp+0Ch]             ; align
//     0x10038378  mov  esi,10h
//     0x1003837D  cmp  eax,esi
//     0x10038383  cmovae esi,eax                 ; unit = align < 10h ? 10h : align
//     0x10038380  mov  edi,[ebp+8]               ; size
//     0x10038386  cmp  edi,esi
//     0x10038388  cmovb edi,esi                  ; want = size < unit ? unit : size
//     0x1003838B  lea  eax,[edi+esi*2]           ; want + 2*unit  (a 32-bit add)
//     0x10038391  call GetProcessHeap
//     0x10038398  call HeapAlloc                 ; HeapAlloc(heap, 0, want + 2*unit)
//     0x100383A0  test edx,edx / je 0x100383BA   ; a failed request answers null
//     0x100383A6  lea  eax,[edx-1]
//     0x100383AB  div  esi                       ; (base - 1) / unit, unsigned
//     0x100383AD  lea  edx,[eax+2]
//     0x100383B0  imul edx,esi                   ; ((base-1)/unit + 2) * unit
//     0x100383B3  sub  eax,ecx                   ; distance = aligned - base
//     0x100383B7  mov  [edx-4],eax               ; the distance word
//     0x100383C2  push edi / push 0CDh / push edx / call memset   ; `want` bytes of CD
//     0x100383D1  push 101A0808h / call EnterCriticalSection
//     0x100383E6  call 0x1005C610                ; std::_Hash::emplace(aligned)
//     0x100383EB  push 101A0808h / call LeaveCriticalSection
//     0x1003840A  ret  0Ch
//   sub_10038430  free, `ret 4`, one argument, 91 instructions
//     0x10038440  mov  edx,[ebp+8]
//     0x10038449  test edx,edx / je 0x1003852E   ; a null pointer is a no-op
//     0x10038457  sub  eax,[edx-4]               ; base = pointer - distance
//     0x1003845C  push eax / push 0 / GetProcessHeap / push eax
//     0x10038474  call HeapFree                  ; HeapFree(heap, 0, base)
//     0x1003847A  push 101A0808h / call EnterCriticalSection
//     0x100384C0  call 0x1005C5A0                ; std::_Hash::find
//     0x100384CC..0x10038519  the inlined erase unlink, call operator delete
//     0x10038521  push 101A0808h / call LeaveCriticalSection / ret 4
//   sub_100385A0  membership, `ret 4`, one argument, 40 instructions
//     0x100385A8  push 101A0808h / call EnterCriticalSection
//     0x100385B3  FNV-1a over the pointer's four bytes (0x811C9DC5, *0x1000193)
//     0x100385F0  call 0x1005C5A0                ; std::_Hash::find
//     0x10038602  mov  eax,[eax+4]               ; the node, or null
//     0x10038607  cmovne edi,eax
//     0x10038610  cmp  edi,esi / setne al        ; AL = found, and only AL
//     0x1003860A  push 101A0808h / call LeaveCriticalSection
//
// So the scheme this test holds the rebuild to is
//
//     unit    = align < 0x10 ? 0x10 : align
//     want    = size  < unit ? unit : size
//     base    = HeapAlloc(GetProcessHeap(), 0, want + 2*unit)
//     aligned = ((base - 1) / unit + 2) * unit          ; 32-bit unsigned arithmetic
//     [aligned - 4] = aligned - base                    ; the distance word
//     memset(aligned, 0xCD, want)                       ; the fill
//     registry += aligned                               ; under 0x101A0808's lock
//
// and four consequences of that arithmetic are what a test can observe. With
// `base = q*unit + r`, `0 <= r < unit`, the division at 0x100383AB is `(base-1)/unit`,
// which is `q-1` when `r == 0` and `q` otherwise, so the distance is `unit` when the
// base is unit-aligned and `2*unit - r` otherwise: it is always in
// `[unit, 2*unit - 1]`. That single fact pins three clauses at once - the `+ 2 * unit`
// the request is enlarged by, the `- 1` before the division and the `+ 2` after it -
// and it is also why the fill, which is `want` bytes from `aligned`, ends inside the
// block: `aligned + want <= base + (2*unit - 1) + want`.
//
// What each class of check pins:
//
//   * alignment scheme  - `aligned % unit == 0` pins `unit = max(0x10, align)` and the
//     `* unit` rounding; the distance range pins the `(base-1)/unit+2` rounding; the
//     block's size (HeapSize on the base) pins `want + 2*unit`; the write/read-back of
//     `want` bytes pins that the block is usable; pairwise disjointness pins that each
//     returned pointer is inside its own block.
//   * the distance word and the fill - the word at `[p-4]` equals the distance, the CD
//     pattern covers `want` bytes (in particular `want`, not `size`, when `size < unit`)
//     and stops there: a poisoned block shows the bytes past `want` untouched.
//   * membership - a live pointer is in, a freed one is not, a pointer never handed out
//     is not, and another address (even one inside a live block) is not. The original
//     answers in AL only (`setne al` after `LeaveCriticalSection`), so the byte is all
//     this test reads.
//   * free - a null is the original's no-op, a freed block goes back to the heap (a
//     same-size request gets it again) and stops being a member. A double free is
//     deliberately not run: see TestFree.
//   * the failed request - the 0x100383A0 `test edx,edx` answers null without throwing
//     or aborting, and the arena still works afterwards.
//
// The test writes no files: ctest `zp_nms_arena`, `tests/nms_arena.cpp`.

namespace
{
    int checks = 0;
    int failures = 0;

    MemoryArena::Methods arena{};

    void Check(bool condition, const std::string& what)
    {
        ++checks;
        if (condition)
        {
            std::printf("PASS %s\n", what.c_str());
        }
        else
        {
            ++failures;
            std::printf("FAIL %s\n", what.c_str());
        }
        // A mutated scheme can corrupt the heap, and the process would then die with
        // its stdout still buffered under ctest: flush each line so the failure that
        // preceded the fault is the record of where it broke.
        std::fflush(stdout);
    }

    constexpr uint32_t kUnitFloor = 0x10;   // the `mov esi,10h` at 0x10038378
    constexpr uint8_t kPoison = 0xCD;       // the `push 0CDh` at 0x100383C3

    // `unit = align < 0x10 ? 0x10 : align` (0x10038383's cmovae).
    uint32_t Unit(uint32_t align)
    {
        return align < kUnitFloor ? kUnitFloor : align;
    }

    // `want = size < unit ? unit : size` (0x10038388's cmovb).
    uint32_t Want(uint32_t size, uint32_t align)
    {
        const uint32_t unit = Unit(align);
        return size < unit ? unit : size;
    }

    uint32_t Address(const void* pointer)
    {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
    }

    const uint8_t* Bytes(const void* pointer)
    {
        return static_cast<const uint8_t*>(pointer);
    }

    uint8_t* WritableBytes(void* pointer)
    {
        return static_cast<uint8_t*>(pointer);
    }

    // The word the allocate stores at `[pointer - 4]` (0x100383B7).
    uint32_t Distance(const void* pointer)
    {
        uint32_t distance = 0;
        std::memcpy(&distance, Bytes(pointer) - 4, sizeof(distance));
        return distance;
    }

    // The allocation the free recovers: `pointer - [pointer - 4]` (0x10038457).
    uint32_t Base(const void* pointer)
    {
        return Address(pointer) - Distance(pointer);
    }

    // True when every byte of `[pointer, pointer + count)` is `value`.
    bool Filled(const void* pointer, uint32_t count, uint8_t value)
    {
        const uint8_t* bytes = Bytes(pointer);
        for (uint32_t index = 0; index < count; ++index)
        {
            if (bytes[index] != value)
                return false;
        }
        return true;
    }

    // The block's size as the heap reports it: the request rounded up to the heap's
    // granularity. This is the only observable of the `want + 2*unit` operand.
    uint32_t BlockSize(const void* pointer)
    {
        return static_cast<uint32_t>(HeapSize(GetProcessHeap(), 0, reinterpret_cast<LPCVOID>(Base(pointer))));
    }

    // A byte pattern that changes at every offset below 256: 37 is odd, so
    // `index * 37 + 11` is a permutation of the byte values and no two offsets share
    // one. The read-back below compares it exactly, so a coincidence with 0xCD at one
    // offset cannot mask a wrong fill.
    uint8_t Pattern(uint32_t index)
    {
        return static_cast<uint8_t>((index * 37u + 11u) & 0xFFu);
    }

    bool PatternSurvives(const void* pointer, uint32_t count)
    {
        const uint8_t* bytes = Bytes(pointer);
        for (uint32_t index = 0; index < count; ++index)
        {
            if (bytes[index] != Pattern(index))
                return false;
        }
        return true;
    }

    std::string Pair(uint32_t size, uint32_t align)
    {
        return std::format("size 0x{:X}, align 0x{:X}", size, align);
    }

    // A block the arena handed out, poisoned, freed and handed out again. Observing
    // this needs the heap to return the block, and the process heap is free to serve a
    // request from anywhere: it returns the just-freed block most of the time (the size
    // class's free list), and sometimes it does not (a free block can be coalesced into
    // a larger free region, or the request is served from fresh space). So the
    // construction does not assume it: it poisons one block, frees it, then asks for the
    // same size - holding whatever else comes back, so a probe cannot spin on its own
    // block - and repeats with a fresh block if the heap did not return it. Sixteen
    // attempts of sixteen held probes: 210 consecutive runs of the suite never missed.
    // A heap that never returned a freed block is reported as a failure, not skipped.
    struct Recycled
    {
        bool observed = false;
        void* pointer = nullptr; // live again, and poisoned around the fill, when observed
        uint32_t base = 0;
        uint32_t extent = 0;
        uint32_t attempts = 0;
    };

    Recycled Recycle(uint32_t size, uint32_t align)
    {
        const uint32_t unit = Unit(align);
        const uint32_t want = Want(size, align);

        Recycled result;
        result.extent = want + unit * 2u;

        for (uint32_t attempt = 0; attempt < 16 && !result.observed; ++attempt)
        {
            result.attempts = attempt + 1;
            void* const target = arena.allocate(size, align, 0);
            if (target == nullptr)
                break;

            result.base = Base(target);
            // Poison it while it is still ours, all of it except the four-byte distance
            // word: the free reads that word to find the base.
            const uint32_t distance = Distance(target);
            std::memset(reinterpret_cast<void*>(static_cast<uintptr_t>(result.base)),
                0xAA, distance - 4u);
            std::memset(reinterpret_cast<void*>(static_cast<uintptr_t>(result.base + distance)),
                0xAA, result.extent - distance);
            arena.release(target);

            std::vector<void*> held;
            for (uint32_t probe = 0; probe < 16 && !result.observed; ++probe)
            {
                void* const again = arena.allocate(size, align, 0);
                if (again == nullptr)
                    break;
                if (again == target)
                    result.observed = true;
                else
                    held.push_back(again);
            }
            for (void* block : held)
                arena.release(block);

            if (result.observed)
                result.pointer = target;
        }
        return result;
    }

    // The sizes and alignments this test crosses: the boundaries 0, 1, 0xF, 0x10,
    // 0x11, 0x1000 against alignments 0, 1, 4, 8, 0x10, 0x20, 0x100. `size 0` is the
    // `want = unit` case, `size 0x10`/`0x11` the boundary the `want` clause turns on,
    // and `align 0` the `unit` floor case (there is nothing to align to, and the
    // original still rounds to 0x10).
    constexpr uint32_t kSizes[] = { 0, 1, 0xF, 0x10, 0x11, 0x1000 };
    constexpr uint32_t kAligns[] = { 0, 1, 4, 8, 0x10, 0x20, 0x100 };
    constexpr std::size_t kPairCount = sizeof(kSizes) / sizeof(kSizes[0]) * (sizeof(kAligns) / sizeof(kAligns[0]));

    // The alignment scheme, on every pair: unit/rounding, the distance range, the
    // block's size, usability, and that no two live allocations overlap.
    void TestAlignmentScheme()
    {
        struct Live
        {
            void* pointer;
            uint32_t size;
        };
        std::vector<Live> live;

        for (const uint32_t size : kSizes)
        {
            for (const uint32_t align : kAligns)
            {
                const std::string pair = Pair(size, align);
                const uint32_t unit = Unit(align);
                const uint32_t want = Want(size, align);

                void* pointer = arena.allocate(size, align, 0);
                Check(pointer != nullptr, "scheme: " + pair + " answers a block");
                if (pointer == nullptr)
                    continue;

                // `unit = max(0x10, align)` and the `* unit` in
                // `((base-1)/unit + 2) * unit`: the returned pointer is a multiple of
                // the unit. (For `align 0` this is the 0x10 floor; for `align 3` below
                // it is the whole guarantee, because 3 does not divide 0x10.)
                Check(Address(pointer) % unit == 0,
                    std::format("scheme: {} -> the pointer 0x{:08X} is a multiple of unit 0x{:X}",
                        pair, Address(pointer), unit));

                // The distance is the `aligned - base` the allocate stores, and its
                // range is the `(base-1)/unit + 2` rounding's signature: always at
                // least one unit and never two, because `(base-1)/unit` is `q-1` for a
                // unit-aligned base and `q` otherwise.
                const uint32_t distance = Distance(pointer);
                Check(distance >= unit && distance < unit * 2,
                    std::format("scheme: {} -> the distance word 0x{:X} is in [0x{:X}, 0x{:X})",
                        pair, distance, unit, unit * 2));

                // `HeapAlloc(want + 2*unit)`: the heap reports the request back,
                // rounded up to its granularity. A smaller slack - the `+1*unit`
                // mutation - shows as a block below `want + 2*unit`.
                const uint32_t heap = BlockSize(pointer);
                Check(heap >= want + unit * 2u && heap < want + unit * 2u + 8u,
                    std::format("scheme: {} -> the block is want+2*unit = 0x{:X} bytes (HeapSize 0x{:X})",
                        pair, want + unit * 2u, heap));

                // Usable for the caller's `size` bytes: write a pattern, read it back.
                // `size` is the extent the caller asked for; the `want` bytes the fill
                // covers are asserted in TestDistanceAndFill. Writing only `size` also
                // keeps this safe under the mutation that drops the
                // `want = max(size, unit)` floor, where the block is one unit smaller.
                for (uint32_t index = 0; index < size; ++index)
                    WritableBytes(pointer)[index] = Pattern(index);
                Check(PatternSurvives(pointer, size),
                    std::format("scheme: {} -> all 0x{:X} requested bytes are writable and read back",
                        pair, size));

                live.push_back({ pointer, want });
            }
        }

        // No two live allocations overlap. Each returned pointer is inside its own heap
        // block (the distance is at least a unit and the fill fits), so this is the
        // statement that the arena hands out distinct memory.
        bool overlap = false;
        std::string where;
        for (std::size_t a = 0; a < live.size() && !overlap; ++a)
        {
            for (std::size_t b = a + 1; b < live.size() && !overlap; ++b)
            {
                const uint32_t firstBegin = Address(live[a].pointer);
                const uint32_t firstEnd = firstBegin + live[a].size;
                const uint32_t secondBegin = Address(live[b].pointer);
                const uint32_t secondEnd = secondBegin + live[b].size;
                if (firstBegin < secondEnd && secondBegin < firstEnd)
                {
                    overlap = true;
                    where = std::format("0x{:08X}+0x{:X} overlaps 0x{:08X}+0x{:X}",
                        firstBegin, live[a].size, secondBegin, live[b].size);
                }
            }
        }
        Check(!overlap && live.size() == kPairCount,
            std::format("scheme: none of the {} live allocations overlap", live.size())
                + (where.empty() ? "" : " (" + where + ")"));

        for (const Live& entry : live)
            arena.release(entry.pointer);
        Check(live.size() == kPairCount,
            std::format("scheme: all {} pairs answered a block", kPairCount));
    }

    // The clauses outside the pair table: the `unit` floor's consequence for an
    // alignment that does not divide it, a non-power-of-two alignment, and the
    // ignored flags argument.
    void TestSchemeLimits()
    {
        // `align 3` is not honoured as an alignment: `unit = 0x10`, so the pointer is a
        // multiple of 0x10 and not necessarily of 3. This is the original's scheme (the
        // rounding target is `unit`), not a rebuild defect, and the check states it
        // rather than asserting something the code cannot promise.
        {
            void* pointer = arena.allocate(0x20, 3, 0);
            Check(pointer != nullptr && Address(pointer) % kUnitFloor == 0,
                std::format("limit: align 3 -> unit 0x10, so the pointer 0x{:08X} is 0x10-aligned "
                    "(the original rounds to unit, not to the request)",
                    pointer != nullptr ? Address(pointer) : 0u));
            if (pointer != nullptr)
                arena.release(pointer);
        }

        // A non-power-of-two above the floor is taken at face value: `unit = 0x30`, and
        // the pointer is a multiple of it.
        {
            void* pointer = arena.allocate(0x40, 0x30, 0);
            const uint32_t distance = pointer != nullptr ? Distance(pointer) : 0u;
            Check(pointer != nullptr && Address(pointer) % 0x30u == 0
                && distance >= 0x30u && distance < 0x60u,
                std::format("limit: align 0x30 -> the pointer 0x{:08X} is 0x30-aligned, distance 0x{:X}",
                    pointer != nullptr ? Address(pointer) : 0u, distance));
            if (pointer != nullptr)
                arena.release(pointer);
        }

        // The third argument is never read by the original (`[ebp+10h]` has no reader in
        // 0x10038360's stream; the HeapAlloc flags are the literal 0 at 0x1003838F).
        // A caller passing junk still gets the same block.
        {
            uint8_t junk = 0;
            void* pointer = arena.allocate(0x20, 0x10, Address(&junk));
            const bool ok = pointer != nullptr && Filled(pointer, 0x20, kPoison)
                && Address(pointer) % 0x10u == 0;
            Check(ok, std::format("limit: the flags argument is ignored (0x{:08X} -> block 0x{:08X})",
                Address(&junk), pointer != nullptr ? Address(pointer) : 0u));
            if (pointer != nullptr)
                arena.release(pointer);
        }
    }

    // The distance word and the fill.
    void TestDistanceAndFill()
    {
        // The distance word carries the offset to the base, and the fill starts at the
        // pointer, not at the base: the word is still there after the memset.
        {
            void* pointer = arena.allocate(0x11, 0x10, 0);
            Check(pointer != nullptr, "fill: size 0x11, align 0x10 answers a block");
            if (pointer != nullptr)
            {
                const uint32_t distance = Distance(pointer);
                const uint32_t base = Base(pointer);
                Check(distance == Address(pointer) - base && distance >= 0x10u && distance < 0x20u,
                    std::format("fill: [pointer-4] holds the distance 0x{:X} (0x{:08X} - 0x{:08X})",
                        distance, Address(pointer), base));
                Check(Filled(pointer, 0x11, kPoison),
                    "fill: the 0x11 bytes at a size-0x11 request are 0xCD (the `push 0CDh` count is want)");
                arena.release(pointer);
            }
        }

        // `want = max(size, unit)`, and the fill follows `want`: a request below one unit
        // still gets a whole unit of CD. With size 0 the `memset(size)` shape would fill
        // nothing at all.
        {
            void* pointer = arena.allocate(0, 0, 0);
            Check(pointer != nullptr && Filled(pointer, 0x10, kPoison),
                "fill: size 0 -> want 0x10, and all 0x10 bytes are 0xCD (want, not size)");
            if (pointer != nullptr)
                arena.release(pointer);
        }
        {
            void* pointer = arena.allocate(1, 4, 0);
            Check(pointer != nullptr && Filled(pointer, 0x10, kPoison),
                "fill: size 1, align 4 -> want 0x10, and all 0x10 bytes are 0xCD");
            if (pointer != nullptr)
                arena.release(pointer);
        }

        // The fill's end, made observable: poison the block with 0xAA, free it, and
        // take the same block again. The fill must restore 0xCD over exactly `want`
        // bytes and leave the block's slack alone. `Recycle` is what gets the block
        // back (see its comment); the poisoning skips the four-byte distance word the
        // free reads. The pair is chosen so the check is also safe for the mutation
        // that fills `size` instead of `want`: size 0xF sits one byte below the unit, so
        // every byte this test writes is inside the block under either count (the heap
        // rounds 0x2F up to 0x30).
        {
            constexpr uint32_t size = 0xF;
            constexpr uint32_t align = 1;
            const uint32_t want = Want(size, align);

            const Recycled recycled = Recycle(size, align);
            Check(recycled.observed,
                std::format("fill: the heap handed the poisoned block back after {} run(s) (base 0x{:08X})",
                    recycled.attempts, recycled.base));
            if (recycled.observed)
            {
                Check(Filled(recycled.pointer, want, kPoison),
                    std::format("fill: want 0x{:X} bytes are 0xCD over the poisoned 0xAA block "
                        "(the count is want, not size 0x{:X})", want, size));
                // The bytes after the fill are the slack the distance leaves
                // (`2*unit - distance` of them, at least one): untouched by the memset.
                uint32_t slack = 0;
                bool untouched = true;
                for (uint32_t address = Address(recycled.pointer) + want;
                     address < recycled.base + recycled.extent; ++address)
                {
                    ++slack;
                    if (*reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(address)) != 0xAA)
                        untouched = false;
                }
                Check(untouched,
                    std::format("fill: the 0x{:X} slack byte(s) past want stay poisoned 0xAA "
                        "(the memset stops at aligned + want)", slack));
                arena.release(recycled.pointer);
            }
        }
    }

    // Membership: the original answers in AL (`cmp edi,esi / setne al`), so only the
    // byte is read - a node pointer or a stale EAX's high bits are not the contract.
    void TestMembership()
    {
        Check(arena.contains(0) == 0, "member: 0 is not a member");
        Check(arena.contains(0xDEADBEEFu) == 0, "member: an address the arena never handed out is not a member");

        uint8_t local = 0;
        Check(arena.contains(Address(&local)) == 0, "member: a stack object's address is not a member");

        void* first = arena.allocate(0x20, 0x10, 0);
        void* second = arena.allocate(0x30, 0x10, 0);
        Check(first != nullptr && second != nullptr, "member: two live allocations");
        if (first != nullptr)
        {
            const uint32_t firstAddress = Address(first);
            if (second != nullptr)
            {
                const uint32_t secondAddress = Address(second);
                Check(arena.contains(firstAddress) == 1, "member: a live allocation is a member");
                Check(arena.contains(secondAddress) == 1, "member: the second live allocation is a member");
                // A different address inside the same block is not the allocation.
                Check(arena.contains(firstAddress + 1) == 0,
                    "member: one byte past the returned pointer is not a member");
                Check(arena.contains(firstAddress) == 1 && arena.contains(secondAddress) == 1,
                    "member: asking about one allocation does not consume the other");

                arena.release(first);
                Check(arena.contains(firstAddress) == 0, "member: a freed pointer is not a member");
                Check(arena.contains(secondAddress) == 1, "member: freeing one leaves the other a member");
                arena.release(second);
            }
            else
            {
                arena.release(first);
            }
        }
    }

    // Free semantics, and the one thing this test refuses to guess at.
    void TestFree()
    {
        // A null is the original's no-op (`0x10038449 test edx,edx / je 0x1003852E`), and
        // it reaches neither the heap nor the registry. The arena is usable after it.
        arena.release(nullptr);
        {
            void* pointer = arena.allocate(0x20, 0x10, 0);
            Check(pointer != nullptr && Filled(pointer, 0x20, kPoison) && arena.contains(Address(pointer)) == 1,
                "free: release(nullptr) leaves the arena usable");
            if (pointer != nullptr)
                arena.release(pointer);
        }

        // Freeing returns the memory: the next request of the same size is served from
        // the block that was freed. `Recycle` is what makes that observable (see its
        // comment); a heap that never returns it is a failure, not a silent skip.
        {
            constexpr uint32_t size = 0x30;
            constexpr uint32_t align = 0x10;
            const Recycled recycled = Recycle(size, align);
            Check(recycled.observed,
                std::format("free: the block at 0x{:08X} comes back on the next size-0x{:X} request "
                    "after {} run(s)", recycled.base, size, recycled.attempts));
            if (recycled.observed)
            {
                const uint32_t address = Address(recycled.pointer);
                Check(Filled(recycled.pointer, size, kPoison) && arena.contains(address) == 1,
                    "free: the reused block is filled 0xCD and registered again");
                Check(arena.contains(address + 1) == 0,
                    "free: the reused block's neighbour address is still no member");
                arena.release(recycled.pointer);
                Check(arena.contains(address) == 0,
                    "free: the reused block is not a member after its second release");
            }
        }

        // A freed pointer is not a member, and nothing else changes: free one of two
        // live blocks.
        {
            void* first = arena.allocate(0x20, 0x10, 0);
            void* second = arena.allocate(0x40, 0x10, 0);
            if (first != nullptr)
            {
                const uint32_t firstAddress = Address(first);
                if (second != nullptr)
                {
                    arena.release(first);
                    Check(arena.contains(firstAddress) == 0, "free: the freed pointer is not a member");
                    Check(arena.contains(Address(second)) == 1, "free: the other live pointer still is");
                    arena.release(second);
                }
                else
                {
                    arena.release(first);
                }
            }
        }

        // NO DOUBLE FREE. This is a finding, not an omission. The original's free has
        // no membership check in front of it: 0x10038449 tests the incoming pointer for
        // null and nothing else, 0x10038457 recovers the base from `[pointer - 4]`, and
        // 0x10038474 calls HeapFree on it unconditionally. A second free therefore reads
        // `[pointer - 4]` out of a block the heap owns again - the process heap writes
        // its free-list links into a freed block's first bytes, and the first free's
        // erase at 0x100384C0 removed the registry entry, so the second call takes the
        // same unguarded path with a distance word it did not write. What the second
        // HeapFree is then handed is the heap manager's state, not the original's
        // arithmetic: whether that is a no-op, a STATUS_HEAP_CORRUPTION termination or
        // silent corruption is decided by the heap implementation, so it is not knowable
        // offline and nothing here can be pinned by an assertion. What *is* knowable is
        // that the pointer stops being a member after the first free - asserted above -
        // so a second free is not intercepted by the registry either.
    }

    // The failure path: the original's `test edx,edx` at 0x100383A0, not an exception.
    void TestFailurePath()
    {
        // A request whose `want + 2*unit` does not wrap and that no 32-bit heap can
        // satisfy: 0x7FFFFFF0 + 0x20 = 0x80000010. (A request that *does* wrap the
        // 32-bit `lea` at 0x1003838B - e.g. 0xFFFFFFF0 - would "succeed" with a tiny
        // block and then memset almost four gigabytes: the original's fault, preserved,
        // and not run here.)
        constexpr uint32_t size = 0x7FFFFFF0;
        constexpr uint32_t align = 0x10;

        void* pointer = reinterpret_cast<void*>(1);
        bool threw = false;
        try
        {
            pointer = arena.allocate(size, align, 0);
        }
        catch (...)
        {
            threw = true;
        }

        Check(!threw, std::format("fail: a size-0x{:X} request throws nothing (the body is C: `ret 0Ch`)", size));
        Check(pointer == nullptr,
            std::format("fail: the request answers null, where the original's 0x100383A0 `test edx,edx` "
                "branches to its zero (got 0x{:08X})", pointer != nullptr ? Address(pointer) : 0u));

        // The arena is still usable: the failed call touched neither the heap nor the
        // registry, so the next allocation is a normal one.
        void* after = arena.allocate(0x20, 0x10, 0);
        Check(after != nullptr && Filled(after, 0x20, kPoison)
            && Address(after) % 0x10u == 0 && arena.contains(Address(after)) == 1,
            std::format("fail: the arena still allocates, fills and registers afterwards (0x{:08X})",
                after != nullptr ? Address(after) : 0u));
        if (after != nullptr)
        {
            for (uint32_t index = 0; index < 0x20; ++index)
                WritableBytes(after)[index] = Pattern(index);
            Check(PatternSurvives(after, 0x20), "fail: ... and the block it answers is writable and read back");
            arena.release(after);
            Check(arena.contains(Address(after)) == 0, "fail: ... and releases again");
        }
    }
}

int main()
{
    // A faulting heap is a test outcome here, never a message box on whoever runs the
    // suite (the same setting test_main uses).
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    arena = MemoryArena::Get();
    Check(arena.allocate != nullptr && arena.release != nullptr && arena.contains != nullptr,
        "seam: MemoryArena::Get answers the three vtable entries");

    if (arena.allocate != nullptr && arena.release != nullptr && arena.contains != nullptr)
    {
        TestAlignmentScheme();
        TestSchemeLimits();
        TestDistanceAndFill();
        TestMembership();
        TestFree();
        TestFailurePath();
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
