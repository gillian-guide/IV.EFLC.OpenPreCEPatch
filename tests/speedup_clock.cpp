#include <common.hxx>

#include <bit>
#include <cstdio>
#include <cstring>

import misc;

// SpeedupBugFix's body (`sub_10050540`, 0x10050540, 93 instructions to its `ret`)
// rebuilt in C++ (its SEH frame, `/GS` cookie and thread-safe static guard block
// transcription): the game's timer-init function.
// The key's payload is pure arithmetic and two unguarded lookups, so it is the
// class of rebuild that fails silently: a wrong scale or a swapped slot makes the
// whole frame-time fix subtly wrong instead of crashing. This test runs the
// rebuild's own sequence with a synthetic frequency, synthetic slots and a
// synthetic clock, with no game image.
//
// What the original does (addresses are in the original ASI, base 0x10000000):
//
//   0x100505D1  mov     eax,[0x10159350]        ; dword_10159350
//   0x100505D6  movss   xmm0,[eax]
//   0x100505DA  ucomiss xmm0,0.0f               ; `lst` + `jp` = `== 0.0f`
//   0x100505E5  jp      skip-the-whole-block
//
// so the frequency is read and the eight floats are derived only while
// dword_10159350 is zero. On the compute path:
//
//   0x100505EF  call    [0x10063050]            ; QueryPerformanceFrequency
//   0x100505FB  call    0x10060ED0              ; (double) frequency
//   0x10050600  movsd   xmm3,[0x10152380]       ; 1000.0 = 0x408F400000000000
//   0x10050608  divsd   xmm3,xmm0               ; scale = 1000.0 / frequency
//   0x10050617  movsd   xmm1,[0x10152300]       ; 0.0010000000474974513
//                                               ;   = 0x3F50624DE0000000
//   0x10050622  mulsd   xmm0,[0x10152380]       ; scale * 1000.0
//   0x1005062A  cvtpd2ps xmm5,xmm3              ; (float) scale
//   0x1005062E  cvtpd2ps xmm4,xmm0              ; (float) (scale * 1000.0)
//   0x10050639  mulsd   xmm0,xmm1               ; scale * 0.0010000000474974513
//   0x10050642  divsd   xmm1,xmm3               ; 0.0010000000474974513 / scale
//   0x1005064F  cvtpd2ps xmm2,xmm0              ; (float) (scale * K)
//   0x10050653  cvtpd2ps xmm0,xmm1              ; (float) (K / scale)
//
// The eight stores, in the original's own order
// (0x10050635..0x1005067F):
//
//   1. 0x10050635  *dword_10159364 = (float) scale
//   2. 0x10050646  *dword_10159360 = (float) (scale * 1000.0)
//   3. 0x10050657  *dword_10159350 = (float) (scale * K)
//   4. 0x10050660  *dword_1015935C = (float) (K / scale)
//   5. 0x10050669  *dword_10159368 = (float) scale
//   6. 0x10050672  *dword_1015934C = (float) (scale * 1000.0)
//   7. 0x1005067B  *dword_10159354 = (float) (scale * K)
//   8. 0x1005067F  *dword_10159358 = (float) (K / scale)
//
// and then the clock call, which is unguarded:
//
//   0x10050683  mov     ecx,[0x101A849C]        ; the one-time scan's match
//   0x10050689  add     ecx,0FFFFFFCDh          ; match - 51
//   0x1005068C  call    ecx                     ; the game's timer getter
//   0x1005068E  mov     [esi],eax
//   0x10050692  mov     [esi+4],edx
//
// The scan is `1B D9 89 5C 24 24 0F 88` (1.0.8.0 match 0x456F93, whose -51 is
// the clock function's `55 8B EC 83 E4 F8` prologue at 0x456F60). A miss leaves
// dword_101A849C zero, so the call is to 0xFFFFFFCD and faults; that is the
// original's preserved fault, and this test asserts it in-process.
//
// The seam these checks drive is `Misc::SpeedupBugFixPublishForTest` (the eight
// published pointers, which the arm would take from the site operands) and
// `Misc::SpeedupBugFixForTest` (the same `SpeedupBugFixRunPublished` the shipped
// body calls, with the scan result and the frequency read injected). No site is
// written, no body is installed, and the test process has no game image.
//
// Assertion classes:
//   1. arithmetic: five frequencies, the four derived floats compared as exact
//      bit patterns against the formula recomputed here;
//   2. placement: every one of the eight published slots, by the ASI dword it is,
//      carries the value the original writes there; a ninth float is untouched;
//   3. order: the eight stores happen in the original's instruction order, shown
//      by overlapping the destinations into one ladder and comparing every byte
//      the eight 4-byte stores leave behind;
//   4. clock: the call is `match - 51`, it stores the 64-bit answer in `*self`,
//      and a missing scan (`clock = 0`) faults on 0xFFFFFFCD;
//   5. determinism: a second call does not read the frequency and does not move
//      any slot, and the clock runs again.
//
//     zp_speedup_clock    the suite

namespace
{
    constexpr uint32_t kAccessViolation = 0xC0000005u;
    constexpr uint32_t kMissedScanAddress = 0xFFFFFFCDu;

    // 0x10152380 and 0x10152300, as the original's .rdata holds them.
    static_assert(std::bit_cast<uint64_t>(1000.0) == 0x408F400000000000ull,
        "0x10152380 must be the original's double 1000.0");
    static_assert(std::bit_cast<uint64_t>(0.0010000000474974513) == 0x3F50624DE0000000ull,
        "0x10152300 must be the original's double 0.0010000000474974513");

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

    uint32_t Bits(float value)
    {
        return std::bit_cast<uint32_t>(value);
    }

    void CheckBits(const std::string& what, float actual, float expected)
    {
        Check(Bits(actual) == Bits(expected),
            std::format("{} = 0x{:08X} (expected 0x{:08X})", what, Bits(actual), Bits(expected)));
    }

    // The eight published pointers, named by the ASI dword each one is. The
    // order here is only the test's array order; the publication is by name.
    enum Slot : size_t
    {
        kT4c = 0,
        kT50,
        kT54,
        kT58,
        kT5c,
        kT60,
        kT64,
        kT68,
        kSlotCount,
    };

    constexpr const char* kSlotNames[kSlotCount] = {
        "dword_1015934C (t4c)",
        "dword_10159350 (t50)",
        "dword_10159354 (t54)",
        "dword_10159358 (t58)",
        "dword_1015935C (t5c)",
        "dword_10159360 (t60)",
        "dword_10159364 (t64)",
        "dword_10159368 (t68)",
    };

    // The original's store order (0x10050635..0x1005067F), slot by slot.
    constexpr Slot kWriteOrder[8] = {
        kT64, kT60, kT50, kT5c, kT68, kT4c, kT54, kT58,
    };

    // Which of the four derived values each store in kWriteOrder writes.
    enum Derived : size_t
    {
        kScale = 0,           // (float) scale
        kScaleTimes1000,      // (float) (scale * 1000.0)
        kScaleTimesK,         // (float) (scale * 0.0010000000474974513)
        kKOverScale,          // (float) (0.0010000000474974513 / scale)
        kDerivedCount,
    };

    constexpr Derived kWriteKinds[8] = {
        kScale, kScaleTimes1000, kScaleTimesK, kKOverScale,
        kScale, kScaleTimes1000, kScaleTimesK, kKOverScale,
    };

    // The four derived floats, computed here from the formula the disassembly
    // shows. The constants are the original's .rdata doubles (pinned above).
    struct Values
    {
        float derived[kDerivedCount];
    };

    Values Derive(uint64_t frequency)
    {
        const double scale = 1000.0 / static_cast<double>(frequency);
        Values values{};
        values.derived[kScale] = static_cast<float>(scale);
        values.derived[kScaleTimes1000] = static_cast<float>(scale * 1000.0);
        values.derived[kScaleTimesK] = static_cast<float>(
            scale * 0.0010000000474974513);
        values.derived[kKOverScale] = static_cast<float>(
            0.0010000000474974513 / scale);
        return values;
    }

    // The value each published slot must receive, by slot identity.
    struct SlotValues
    {
        float value[kSlotCount];
    };

    SlotValues PerSlot(const Values& values)
    {
        SlotValues perSlot{};
        for (size_t index = 0; index < 8; ++index)
            perSlot.value[kWriteOrder[index]] = values.derived[kWriteKinds[index]];
        return perSlot;
    }

    Misc::SpeedupSlots SlotsByAddress(float* base)
    {
        return {
            base + kT4c, base + kT50, base + kT54, base + kT58,
            base + kT5c, base + kT60, base + kT64, base + kT68,
        };
    }

    // ---------------------------------------------------------------------
    // The injected frequency read and the synthetic clock.
    // ---------------------------------------------------------------------
    uint64_t g_frequency = 0;
    int g_frequency_calls = 0;

    uint64_t __cdecl ReadTestFrequency()
    {
        ++g_frequency_calls;
        return g_frequency;
    }

    uint64_t g_second_frequency = 0;
    int g_second_frequency_calls = 0;

    uint64_t __cdecl ReadSecondFrequency()
    {
        ++g_second_frequency_calls;
        return g_second_frequency;
    }

    uint64_t g_clock_stamp = 0;
    int g_clock_calls = 0;

    __int64 __cdecl TestClock()
    {
        ++g_clock_calls;
        return static_cast<__int64>(g_clock_stamp);
    }

    // The scan match whose -51 is TestClock, i.e. the address the original's
    // scan would record.
    uint32_t ClockMatch()
    {
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&TestClock) + 51u);
    }

    uint64_t StampOf(const unsigned char* self)
    {
        uint64_t stamp = 0;
        std::memcpy(&stamp, self, sizeof(stamp));
        return stamp;
    }

    // ---------------------------------------------------------------------
    // 1 + 2: the arithmetic and the slot each value goes to.
    // ---------------------------------------------------------------------
    void CheckFrequency(uint64_t frequency)
    {
        const Values values = Derive(frequency);
        const SlotValues perSlot = PerSlot(values);

        float storage[kSlotCount + 2] = {};
        storage[kSlotCount] = 1234.5f;       // not in the published list
        storage[kSlotCount + 1] = -0.0f;     // neither is this
        Misc::SpeedupBugFixPublishForTest(SlotsByAddress(storage));

        g_frequency = frequency;
        g_frequency_calls = 0;
        g_clock_stamp = 0x0123456789ABCDEFull;
        g_clock_calls = 0;
        alignas(8) unsigned char self[16];
        std::memset(self, 0xA5, sizeof(self));

        void* answer = Misc::SpeedupBugFixForTest(self, ClockMatch(), &ReadTestFrequency);

        Check(g_frequency_calls == 1,
            std::format("frequency {}: QueryPerformanceFrequency was read once", frequency));
        Check(g_clock_calls == 1,
            std::format("frequency {}: the clock was called once", frequency));
        Check(answer == self,
            std::format("frequency {}: the entry answered self", frequency));
        Check(StampOf(self) == g_clock_stamp,
            std::format("frequency {}: *self carries the clock's 64-bit stamp 0x{:016X}",
                frequency, g_clock_stamp));

        for (size_t slot = 0; slot < kSlotCount; ++slot)
        {
            CheckBits(std::format("frequency {}: {}", frequency, kSlotNames[slot]),
                storage[slot], perSlot.value[slot]);
        }

        CheckBits(std::format("frequency {}: the ninth float (not published)", frequency),
            storage[kSlotCount], 1234.5f);
        CheckBits(std::format("frequency {}: the tenth float (not published)", frequency),
            storage[kSlotCount + 1], -0.0f);
    }

    // ---------------------------------------------------------------------
    // 3: the store order, through overlapping destinations. Slot
    // kWriteOrder[i] points at byte i of a 12-byte ladder, so the eight 4-byte
    // float stores overlap and the 11 bytes left behind encode the order: a
    // byte holds the value of the last store that covers it. The expected
    // ladder is simulated here from the documented order. The four values are
    // chosen to have distinct low bytes for this frequency, so for any two
    // stores with different values, swapping them moves at least the later
    // store's first byte; every reordering that is observable at all is
    // therefore caught, while swapping two equal-valued stores (e.g. t64/t68)
    // is invisible - and behaviourally identical.
    //
    // The destinations are unaligned `float*` into a byte buffer on purpose.
    // This suite is Win32/x86 only (the project refuses any other architecture),
    // where a scalar `movss` has no alignment requirement; the alternative,
    // aligned float pointers, cannot overlap four-byte stores.
    // ---------------------------------------------------------------------
    void CheckStoreOrder(uint64_t frequency)
    {
        const Values values = Derive(frequency);

        // The four values have to be distinguishable at the byte level for the
        // ladder to be an order check rather than a values check.
        const uint32_t lowA = Bits(values.derived[kScale]) & 0xFFu;
        const uint32_t lowB = Bits(values.derived[kScaleTimes1000]) & 0xFFu;
        const uint32_t lowC = Bits(values.derived[kScaleTimesK]) & 0xFFu;
        const uint32_t lowD = Bits(values.derived[kKOverScale]) & 0xFFu;
        Check(lowA != lowB && lowA != lowC && lowA != lowD && lowB != lowC &&
                  lowB != lowD && lowC != lowD,
            std::format("order at {}: the four values have distinct low bytes "
                        "({:02X}/{:02X}/{:02X}/{:02X}), so any observable swap moves a byte",
                frequency, lowA, lowB, lowC, lowD));

        alignas(4) unsigned char ladder[12] = {};
        // slot -> ladder byte; byte i is the i-th store of kWriteOrder.
        float* const at[kSlotCount] = {
            reinterpret_cast<float*>(ladder + 5),  // t4c: store 6
            reinterpret_cast<float*>(ladder + 2),  // t50: store 3
            reinterpret_cast<float*>(ladder + 6),  // t54: store 7
            reinterpret_cast<float*>(ladder + 7),  // t58: store 8
            reinterpret_cast<float*>(ladder + 3),  // t5c: store 4
            reinterpret_cast<float*>(ladder + 1),  // t60: store 2
            reinterpret_cast<float*>(ladder + 0),  // t64: store 1
            reinterpret_cast<float*>(ladder + 4),  // t68: store 5
        };
        Misc::SpeedupBugFixPublishForTest({ at[kT4c], at[kT50], at[kT54], at[kT58],
            at[kT5c], at[kT60], at[kT64], at[kT68] });

        g_frequency = frequency;
        g_frequency_calls = 0;
        g_clock_stamp = 0x0F1E2D3C4B5A6978ull;
        g_clock_calls = 0;
        alignas(8) unsigned char self[16] = {};
        Misc::SpeedupBugFixForTest(self, ClockMatch(), &ReadTestFrequency);

        unsigned char expected[12] = {};
        for (size_t index = 0; index < 8; ++index)
            std::memcpy(expected + index, &values.derived[kWriteKinds[index]], sizeof(float));

        for (size_t position = 0; position < 11; ++position)
        {
            Check(ladder[position] == expected[position],
                std::format("order at {}: byte {} = 0x{:02X} (expected 0x{:02X})", frequency,
                    position, ladder[position], expected[position]));
        }
        Check(ladder[11] == 0,
            std::format("order at {}: the byte past the last store is untouched", frequency));
    }

    // ---------------------------------------------------------------------
    // 4: the clock call, the missing-scan fault, and recovery afterwards.
    // ---------------------------------------------------------------------
    struct FaultInfo
    {
        bool raised = false;
        DWORD code = 0;
        uintptr_t address = 0;
    };

    int CaptureFault(PEXCEPTION_POINTERS pointers, FaultInfo* info)
    {
        info->code = pointers->ExceptionRecord->ExceptionCode;
        info->address = reinterpret_cast<uintptr_t>(
            pointers->ExceptionRecord->ExceptionAddress);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    // Runs the entry at `clock` and catches the access violation an execute at
    // 0xFFFFFFCD raises. The guarded function has no object with a destructor,
    // so it can use SEH.
    FaultInfo RunAtClock(uint32_t clock, void* self, uint64_t frequency)
    {
        FaultInfo fault;
        g_frequency = frequency;
        g_frequency_calls = 0;
        g_clock_stamp = 0;
        g_clock_calls = 0;
        __try
        {
            Misc::SpeedupBugFixForTest(self, clock, &ReadTestFrequency);
        }
        __except (CaptureFault(GetExceptionInformation(), &fault))
        {
            fault.raised = true;
        }
        return fault;
    }

    void CheckMissingScanFault()
    {
        const uint64_t frequency = 3579545;
        const Values values = Derive(frequency);
        const SlotValues perSlot = PerSlot(values);

        float storage[kSlotCount + 1] = {};
        storage[kSlotCount] = 42.0f;
        Misc::SpeedupBugFixPublishForTest(SlotsByAddress(storage));

        alignas(8) unsigned char self[16];
        std::memset(self, 0x5A, sizeof(self));

        const FaultInfo fault = RunAtClock(0, self, frequency);

        Check(fault.raised,
            "missing scan: the call through dword_101A849C = 0 faulted, not returned");
        Check(fault.code == kAccessViolation,
            std::format("missing scan: the fault is an access violation (0x{:08X}, expected "
                        "0x{:08X})", fault.code, kAccessViolation));
        Check(fault.address == kMissedScanAddress,
            std::format("missing scan: the faulting instruction is 0x{:08X} (the original's "
                        "`match - 51` with match = 0, expected 0x{:08X})",
                fault.address, kMissedScanAddress));

        // The arithmetic runs before the clock call, so the slots are already
        // written; the store into *self is not.
        for (size_t slot = 0; slot < kSlotCount; ++slot)
        {
            CheckBits(std::format("missing scan: {} was written before the fault",
                          kSlotNames[slot]),
                storage[slot], perSlot.value[slot]);
        }
        CheckBits("missing scan: the ninth float is untouched", storage[kSlotCount], 42.0f);
        bool selfUntouched = true;
        for (size_t index = 0; index < sizeof(self); ++index)
            selfUntouched = selfUntouched && self[index] == 0x5A;
        Check(selfUntouched,
            "missing scan: *self was not written (the fault is before `mov [esi],eax`)");
        Check(g_clock_calls == 0,
            "missing scan: no clock function ran (the call itself faulted)");

        // The process survives: the next call with a resolved clock works.
        float after[kSlotCount] = {};
        Misc::SpeedupBugFixPublishForTest(SlotsByAddress(after));
        g_clock_stamp = 0x1020304050607080ull;
        g_clock_calls = 0;
        alignas(8) unsigned char selfAfter[16] = {};
        void* answer = Misc::SpeedupBugFixForTest(selfAfter, ClockMatch(), &ReadTestFrequency);
        Check(answer == selfAfter && StampOf(selfAfter) == g_clock_stamp &&
                  g_clock_calls == 1,
            "after the missing-scan fault the entry still runs and returns the clock's stamp");
    }

    // ---------------------------------------------------------------------
    // 5: determinism. A second call must not read the frequency again (the
    // dword_10159350 guard) and must not move any slot; the clock runs again.
    // ---------------------------------------------------------------------
    void CheckDeterminism()
    {
        float storage[kSlotCount + 1] = {};
        storage[kSlotCount] = 7.25f;
        Misc::SpeedupBugFixPublishForTest(SlotsByAddress(storage));

        g_frequency = 1000;
        g_frequency_calls = 0;
        g_clock_stamp = 0x1111222233334444ull;
        g_clock_calls = 0;
        alignas(8) unsigned char self[16] = {};

        void* first = Misc::SpeedupBugFixForTest(self, ClockMatch(), &ReadTestFrequency);
        Check(first == self && g_frequency_calls == 1 && g_clock_calls == 1,
            "first call: read the frequency once, called the clock once");

        float snapshot[kSlotCount] = {};
        std::memcpy(snapshot, storage, sizeof(snapshot));

        // A different frequency source that must not be consulted.
        g_second_frequency = 10000000;
        g_second_frequency_calls = 0;
        g_clock_stamp = 0x5555666677778888ull;

        void* second = Misc::SpeedupBugFixForTest(self, ClockMatch(), &ReadSecondFrequency);
        Check(second == self && g_second_frequency_calls == 0,
            "second call: did not read the frequency (dword_10159350 is no longer zero)");
        Check(g_clock_calls == 2,
            "second call: the clock ran again (the call is outside the guard)");
        Check(StampOf(self) == 0x5555666677778888ull,
            "second call: *self carries the second clock's stamp");
        Check(std::memcmp(snapshot, storage, sizeof(snapshot)) == 0,
            "second call: every one of the eight slots is bit-identical");
        CheckBits("second call: the ninth float is untouched", storage[kSlotCount], 7.25f);
    }
}

int main()
{
    // The missing-clock case faults on purpose: suppress the Windows dialog.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    try
    {
        CheckFrequency(1);
        CheckFrequency(1000);
        CheckFrequency(3579545);
        CheckFrequency(10000000);
        CheckFrequency(0x7FFFFFFFull);

        CheckStoreOrder(3579545);
        CheckStoreOrder(10000000);

        CheckMissingScanFault();
        CheckDeterminism();
    }
    catch (const std::exception& error)
    {
        Check(false, std::format("the test itself threw {}", error.what()));
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
