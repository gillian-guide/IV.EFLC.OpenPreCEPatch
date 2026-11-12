module;

#include <common.hxx>

// Declares the /GS cookie the transcribed prologues read and the check their
// epilogues call (`__security_cookie`, `__security_check_cookie` in vcruntime.h),
// the `va_list` the rebuilt formatter takes, and the CRT calls the decoder, the
// archive builder and the speed-up body make.
#include <intrin.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

export module fixes;

import bodies;
import bytes;
import common;
import config;
import game;
import loader;
import options;
import patch;
import shared;

// FIXES: CutsceneFixes, OutOfCommissionFix, HighFPSBikePhysicsFix, BikeFeetFix,
// BikePhoneAnimsFix, HighFPSSpeedupFix, SpeedupBugFix, VSyncFix, MouseFix and
// DrivebyFOVFix -- the shipped INI's [FIXES] section, moved here from the
// handler carve-outs (`graphics`, `loading`, `vehicles`, `stability`, `misc`,
// `audio`, `ooc`).
export namespace Fixes
{
    void Prepare();

    void PublishCutsceneFixes();
    void PublishHighFPSBikePhysicsFix();
    void PublishMouseFix();
    void PublishDrivebyFOVFix();
    void PublishSpeedupBugFix();
    void PublishOutOfCommissionFix();

    void InstallCutsceneFixes();
    void InstallHighFPSBikePhysicsFix();
    void InstallBikeFeetFix();
    void InstallBikePhoneAnimsFix();
    void InstallHighFPSSpeedupFix();
    void InstallVSyncFix();
    void InstallMouseFix();
    void InstallDrivebyFOVFix();
    void InstallSpeedupBugFix();
    void InstallOutOfCommissionFix();

    // The eight published pointers SpeedupBugFix uses, named by the ASI dword each
    // one is; the test drives the rebuilt sequence through them.
    struct SpeedupSlots
    {
        float* t4c;  // dword_1015934C
        float* t50;  // dword_10159350
        float* t54;  // dword_10159354
        float* t58;  // dword_10159358
        float* t5c;  // dword_1015935C
        float* t60;  // dword_10159360
        float* t64;  // dword_10159364
        float* t68;  // dword_10159368
    };

    void SpeedupBugFixPublishForTest(const SpeedupSlots& slots);

    // `clock` is the scan result the body calls at `clock - 51`; 0 is a miss.
    // `read_frequency` is called only when dword_10159350 is zero.
    void* SpeedupBugFixForTest(void* self, uint32_t clock,
        uint64_t (__cdecl* read_frequency)());
}

// MouseFix's published state. Two pointers the game's mouse reader stores its axes
// through, the game byte that flips the axis sign, the game's own frame-time
// function, and the two resumes the site bodies tail-jump to. The floats and
// doubles are the original ASI's literal pool: recovered `.data`, kept as raw
// words so the bit patterns are exactly the original's.
extern "C"
{
    uintptr_t zpatch_mf_x = 0;       // dword_10159C50 -> the game's mouse-X int
    uintptr_t zpatch_mf_y = 0;       // dword_10159C3C -> the game's mouse-Y int
    uintptr_t zpatch_mf_flag = 0;    // dword_10159C5C -> the game's invert byte
    uintptr_t zpatch_mf_time = 0;    // dword_10159C34 -> the game's frame clock
    uint32_t zpatch_mf_resume_a = 0; // dword_10159C60 -> the angle site's match
    uint32_t zpatch_mf_resume_b = 0; // dword_10159C40 -> the advance site's +0x756

    // The game's VirtualProtect import, called the way the original's advance
    // helper calls it: `call dword ptr [slot]`.
    uintptr_t zpatch_mf_virtual_protect = 0;

    // The eight xmm words the two site bodies save and restore around their helper
    // (the original's scratch dwords at 0x10159C30..0x10159C64).
    float zpatch_mf_xmm_save[8] = {};

    // The scalar constants the site bodies read with mulsd/movsd QWORD PTR [slot]
    // and movss DWORD PTR [slot]. They are the original's .rdata values written as
    // the numbers they are; three of them are not the mathematical constants they
    // resemble, so each of those says so. No explicit alignment: a scalar load does
    // not need one on x86, and the compiler gives doubles and floats their natural
    // alignment anyway.
    double zpatch_mf_scale = 0.0078125;
    double zpatch_mf_scale_255 = 255.0;
    double zpatch_mf_time_mul = 0.075000003;    // 0.075f printed to 9 digits, read back as a double
    double zpatch_mf_time_add = 0.025;
    double zpatch_mf_mul_neg1_d = -1.0;
    double zpatch_mf_pi = 3.1415927;            // not pi: the original's value
    double zpatch_mf_two_pi = 6.2831855;        // not 2*pi, same reason
    double zpatch_mf_neg_pi = -3.1415927;
    float zpatch_mf_sign = 127.5f;
    float zpatch_mf_mul_neg1 = -1.0f;
    float zpatch_mf_step_scale = 0.1f;

    // The two xorps masks: four single-precision sign bits (0x101523F0) and two
    // double-precision ones (0x101523E0). Both are 16 bytes in the original and
    // `xorps` reads sixteen, so they must stay 16-byte aligned here too.
    alignas(16) uint32_t zpatch_mf_single_sign[4] = { 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u };
    alignas(16) uint64_t zpatch_mf_double_sign[2] = { 0x8000000000000000ull, 0x8000000000000000ull };
}

// The published state of the arms that came from other modules.

extern "C"
{
    // HighFPSBikePhysicsFix's detour state: the incoming value the clamp rewrites,
    // the caller's EAX saved across the clamp call, and the resume (the site's own
    // `call`, match+9).
    float zpatch_bike_value = 0.0f;    // dword_1015AD7C
    uint32_t zpatch_bike_saved = 0;    // dword_1015AD80
    uint32_t zpatch_bike_resume = 0;   // dword_1015AD84

    // VSyncFix: both call shapes load the vsync flag from an operand the original
    // retargets to its own dword.
    uint32_t zpatch_vsync_flag = 0;
}

// DrivebyFOVFix's and SpeedupBugFix's state, from the old `misc` module.
extern "C"
{
    // The layer's resolved salt (source/native.ixx), dword_101580D4, and the one the
    // cutscene chain resolves (dword_1015803C).
    extern uint32_t zpatch_native_salt_101580D4;
    extern uint32_t zpatch_native_salt_1015803C;
    extern uintptr_t zpatch_native_resolve_entry;

    float zpatch_driveby_fov = 45.0f;
    uint32_t zpatch_driveby_cached = 0;
    uint8_t zpatch_driveby_done = 0;
    uint32_t zpatch_driveby_resume = 0;

    // SpeedupBugFix: the eight game timing floats the handler publishes, in its
    // own read order (the last four repeat the first four for the episode path).
    float* zpatch_speedup_t4c = nullptr;  // dword_1015934C: copy of dword_18CAEB0
    float* zpatch_speedup_t50 = nullptr;  // dword_10159350: dword_18CAE9C, seconds per tick
    float* zpatch_speedup_t54 = nullptr;  // dword_10159354: copy of dword_18CAE9C
    float* zpatch_speedup_t58 = nullptr;  // dword_10159358: copy of dword_1752074
    float* zpatch_speedup_t5c = nullptr;  // dword_1015935C: dword_1752078, 1 / milliseconds
    float* zpatch_speedup_t60 = nullptr;  // dword_10159360: dword_18CAEA4, milliseconds * dword_FF2014
    float* zpatch_speedup_t64 = nullptr;  // dword_10159364: flt_18CAEA0, milliseconds per tick
    float* zpatch_speedup_t68 = nullptr;  // dword_10159368: copy of flt_18CAEA0
}

// The transcribed bodies are defined in the tail of this file. MSVC rejects a
// __declspec(naked) declaration (C2488), so the readable code above them names
// each body through a plain prototype; only the definition in the tail keeps the
// attribute.
static void DrivebyResolveEntry();
static void DrivebyFovHelperBody();
static void DrivebyFovShimBody();
static void MfAxisFloatBody();
static void MfAxisNegBody();
static void MfAxisIntBody();
static void MfAxisFlagBody();
static void MfAngleStepBody();
static void MfAngleBody();
static void MfAdvanceWindowsBody();
static void MfAdvanceBody();
static void BikePhysicsDetour();
static void CutsceneResolveEntry();
static void CutsceneChainBody();
static void* __fastcall SpeedupBugFixBody(void* self, int);
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

// HighFPSSpeedupFix: the nine frame-time clamps inside CTimer::Update. One becomes
// a short jump (skipping the following test), the other eight are two-byte NOPs.
struct SpeedupSite final
{
    ptrdiff_t offset;
    bool shortJump;
};

constexpr SpeedupSite kSpeedupSites[] = {
    { -7, false },
    { 3, false },
    { 224, true },
    { 231, false },
    { 367, false },
    { 377, false },
    { 607, false },
    { 614, false },
    { 764, false },
};

namespace Fixes
{
    void Prepare()
    {
        // OutOfCommissionFix's decoder pair: the original registers both bodies
        // before the arm runs, so a tool that branches into them can express the
        // branch in the original's coordinates.
        Bodies::Register(reinterpret_cast<uintptr_t>(&OocStepBody), 0x1004B150u);
        Bodies::Register(reinterpret_cast<uintptr_t>(&OocDecodeBody), 0x1004B570u);

        // SpeedupBugFix's entry, so the E9's target is expressed in the original's
        // coordinates.
        Bodies::Register(reinterpret_cast<uintptr_t>(&SpeedupBugFixBody), 0x10050540u);
    }
}

// The SpeedupBugFix machinery: the derived floats, the published store order and
// the one-time clock scan.

// SpeedupBugFix: the game's timer-init function (1.0.8.0 game 0x4571B0, the
// `50 64 89 25 00 00 00 00 83 EC 34` prologue's match - 21) is replaced by the
// original's sub_10050540. That body is compiled C -- an SEH frame, a /GS
// cookie, the MSVC thread-safe static guard for its one-time signature scan and
// SSE double arithmetic -- so this is a C++ reimplementation of the same
// behaviour, not a transcription.
//
// The original's handler has no build gate and reads its eight float pointers
// from fixed site-relative operands, so the row is the same on every build that
// carries the signature (0x4544B0 on 1.0.4.0 through 0x5AD550 on 1.1.3.0) and
// 1.0.0.0, which has no hit, is a clean skip. tests/signatures.tsv records the
// per-build sites and the eight operand encodings.
//
// It fills the game's eight timing floats from QueryPerformanceFrequency, then
// stores the game clock's 64-bit stamp in *self and answers self. The clock
// function is found by the original's own one-time scan ("1B D9 89 5C 24 24 0F
// 88", 1.0.8.0 site 0x456F93) and is called at match - 51 = 0x456F60.
static std::atomic<uint32_t> zpatch_speedup_scan_done{ 0 };
static uint32_t zpatch_speedup_clock = 0;

namespace
{
    using SpeedupFrequencyReader = uint64_t(__cdecl*)();

    // QueryPerformanceFrequency, the original's `call dword ptr [0x10063050]` at
    // 0x100505EF. The original reaches it only when dword_10159350 is still zero.
    uint64_t ReadQueryPerformanceFrequency()
    {
        LARGE_INTEGER frequency{};
        QueryPerformanceFrequency(&frequency);
        return static_cast<uint64_t>(frequency.QuadPart);
    }

    // The sequence sub_10050540 runs after its one-time scan: the
    // dword_10159350 guard, the four derived floats written through eight
    // pointers, then the clock call. It is split out so tests/speedup_clock.cpp
    // (ctest `zp_speedup_clock`) can inject the frequency, the scan result and
    // the eight destinations; the shipped body below passes the published globals,
    // the scan slot and QueryPerformanceFrequency, so the parameters change no
    // shipped behaviour.
    //
    // `targets` is in the original's *store* order (0x10050635..0x1005067F):
    // dword_10159364, _1360, _1350, _135C, _1368, _134C, _1354, _1358. That is not
    // the handler's read order (0x100506E8..0x10050738); the two must not be
    // conflated. `clock` is dword_101A849C, the scan's match; the original calls
    // `clock - 51`, so 0 is its missing-scan 0xFFFFFFCD.
    void* SpeedupBugFixRun(void* self, float** targets, uint32_t clock,
        SpeedupFrequencyReader read_frequency)
    {
        // The original tests the float at dword_10159350, not the pointer
        // (0x100505D1..0x100505E5), so the dereference stays unguarded; the arm
        // publishes all eight pointers before this body can run.
        if (*targets[2] == 0.0f)
        {
            const uint64_t frequency = read_frequency();

            // 0x10152380 is the double 1000.0 (0x408F400000000000) and 0x10152300
            // the double 0.0010000000474974513 (0x3F50624DE0000000). The original
            // divides, multiplies and truncates to float in this order
            // (0x10050600..0x10050653): scale = 1000.0 / frequency, then
            // (float)scale, (float)(scale * 1000.0),
            // (float)(scale * 0.0010000000474974513) and
            // (float)(0.0010000000474974513 / scale).
            const double scale = 1000.0 / static_cast<double>(frequency);
            const float perTickMilliseconds = static_cast<float>(scale);
            const float millisecondsProduct = static_cast<float>(scale * 1000.0);
            const float perTickSeconds = static_cast<float>(scale * 0.0010000000474974513);
            const float reciprocal = static_cast<float>(0.0010000000474974513 / scale);

            *targets[0] = perTickMilliseconds;
            *targets[1] = millisecondsProduct;
            *targets[2] = perTickSeconds;
            *targets[3] = reciprocal;
            *targets[4] = perTickMilliseconds;
            *targets[5] = millisecondsProduct;
            *targets[6] = perTickSeconds;
            *targets[7] = reciprocal;
        }

        // 0x10050683..0x10050692: `mov ecx,[dword_101A849C]; add ecx,-51;
        // call ecx; mov [esi],eax; mov [esi+4],edx`, and the answer is ESI. The
        // call is unguarded: a scan miss leaves the slot zero and the call faults
        // on 0xFFFFFFCD, exactly as the original's does.
        using ClockFunction = __int64(__cdecl*)(void);
        const __int64 stamp = reinterpret_cast<ClockFunction>(
            static_cast<uintptr_t>(clock) - 51u)();
        *reinterpret_cast<__int64*>(self) = stamp;
        return self;
    }

    // Builds the eight destinations in the original's store order and runs the
    // sequence. The body and the test-only seam below both go through this, so
    // the order cannot drift between the shipped path and the measured one.
    void* SpeedupBugFixRunPublished(void* self, uint32_t clock, SpeedupFrequencyReader read_frequency)
    {
        float* targets[8] = {
            zpatch_speedup_t64, zpatch_speedup_t60, zpatch_speedup_t50, zpatch_speedup_t5c,
            zpatch_speedup_t68, zpatch_speedup_t4c, zpatch_speedup_t54, zpatch_speedup_t58,
        };
        return SpeedupBugFixRun(self, targets, clock, read_frequency);
    }
}

namespace
{
    // A captured pattern operand is a 32-bit VA of the image *as the loader mapped
    // it*: the image's `.reloc` entries were applied before any arm ran, so the
    // dword in the instruction already names the live object. The game is relocated
    // on every run (GTAIV.exe carries DYNAMIC_BASE, and the observed bases include
    // 0x4B0000 and 0xA60000), so converting the operand through the preferred base
    // again would move every pointer by the run's whole relocation distance. The
    // offline dump applies the same fixups to its own copy of the image
    // (tests/dump_main.cpp), which is what makes this identity cast right there too.
    template <typename T>
    T* GameImagePointer(uint32_t value)
    {
        return reinterpret_cast<T*>(static_cast<uintptr_t>(value));
    }

    // SpeedupBugFix: the handler's eight operands, in its read order
    // (`mov eax,[ebx+2Fh]` .. `mov eax,[ebx+129h]`, ebx = match - 21), are the
    // absolute addresses of the floats the replacement body computes. Each one is
    // the disp32 field of a `movss` in the timer-init function, so the four bytes
    // before the field are that instruction's opcode and ModRM. Those bytes are the
    // per-build evidence that the dword really is one of the timing floats and not
    // something that happens to sit at the same distance from a coincidental
    // signature match: they are identical on every image that carries the
    // signature (tests/signatures.tsv records the recovered values).
    struct TimingOperand final
    {
        ptrdiff_t offset;               // the handler's [ebx+disp], from the site
        std::array<uint8_t, 4> opcode;  // the instruction bytes that precede it
        float** slot;
    };

    constexpr TimingOperand kTimingOperands[] = {
        { 0x02F, { 0xF3, 0x0F, 0x10, 0x05 }, &zpatch_speedup_t50 },  // movss xmm0,[dword_18CAE9C]
        { 0x0BA, { 0xF3, 0x0F, 0x11, 0x0D }, &zpatch_speedup_t64 },  // movss [flt_18CAEA0],xmm1
        { 0x0D1, { 0xF3, 0x0F, 0x11, 0x05 }, &zpatch_speedup_t60 },  // movss [dword_18CAEA4],xmm0
        { 0x0F7, { 0xF3, 0x0F, 0x11, 0x1D }, &zpatch_speedup_t5c },  // movss [dword_1752078],xmm3
        { 0x0FF, { 0xF3, 0x0F, 0x11, 0x0D }, &zpatch_speedup_t68 },  // movss [dword_18CAEAC],xmm1
        { 0x10E, { 0xF3, 0x0F, 0x11, 0x1D }, &zpatch_speedup_t4c },  // movss [dword_18CAEB0],xmm3
        { 0x11D, { 0xF3, 0x0F, 0x11, 0x15 }, &zpatch_speedup_t54 },  // movss [dword_18CAEA8],xmm2
        { 0x129, { 0xF3, 0x0F, 0x11, 0x05 }, &zpatch_speedup_t58 },  // movss [dword_1752074],xmm0
    };

    constexpr size_t kTimingOperandCount = sizeof(kTimingOperands) / sizeof(kTimingOperands[0]);

    // The body writes through all eight pointers, so every one of them has to be a
    // writable word of the image in use before any of them is published: a
    // signature that matched a different function would otherwise hand the body a
    // code address or a constant to store floats into. The operand is a live
    // address, so the *live* base is what says where it sits; the PE header still
    // records the preferred base and is not the image this runs in.
    bool WritableImageWord(uint32_t value)
    {
        const auto& game = Game::Get();
        const auto host = static_cast<uint32_t>(game.ImageBase());
        if (value < host ||
            value - host > game.ImageSize() - sizeof(uint32_t))
            return false;

        const auto rva = value - host;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game.ImageBase());
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(game.ImageBase() + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            if (rva < section->VirtualAddress || rva - section->VirtualAddress >= section->Misc.VirtualSize)
                continue;
            return (section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        }
        return false;
    }

    // SpeedupBugFix's site, validated. The original's handler has no build gate --
    // the loader calls it whenever the key is on (`cmp esi,1; jnz; call
    // sub_100506B0`) and reads its eight operands at fixed site-relative offsets,
    // so its row is the same on every build: a hit installs `E9` at match-21 and a
    // miss is the handler's own `test eax,eax; jz` return. Reproducing that means
    // reproducing it only where the site is really the timer-init function; the
    // operand encodings and their writable targets are what say so, and a site that
    // fails either test is not this build's layout, so nothing is published and
    // nothing is installed.
    std::byte* ResolveSpeedupBugFix()
    {
        auto pattern = find_pattern("50 64 89 25 00 00 00 00 83 EC 34");
        if (pattern.empty())
            return nullptr;

        auto* site = pattern.get_first<std::byte>(-21);
        uint32_t targets[kTimingOperandCount];
        for (size_t index = 0; index < kTimingOperandCount; ++index)
        {
            const TimingOperand& operand = kTimingOperands[index];
            const auto* field = site + operand.offset;
            if (std::memcmp(field - operand.opcode.size(), operand.opcode.data(),
                    operand.opcode.size()) != 0)
                return nullptr;

            targets[index] = *reinterpret_cast<const uint32_t*>(field);
            if (!WritableImageWord(targets[index]))
                return nullptr;
        }

        for (size_t index = 0; index < kTimingOperandCount; ++index)
            *kTimingOperands[index].slot = GameImagePointer<float>(targets[index]);
        return site;
    }

    // The publish tier runs before the install tier, so the site resolved there is
    // the one the install patches; a null one keeps the install off.
    std::byte* zpatch_speedup_site = nullptr;
}

static void* __fastcall SpeedupBugFixBody(void* self, int)
{
    // sub_10050587's MSVC static-init guard: the scan runs once per process. A
    // miss leaves the clock at zero, and the faithful `0 - 51` call below faults
    // on 0xFFFFFFCD exactly as the original's does.
    if (zpatch_speedup_scan_done.exchange(1u, std::memory_order_acq_rel) == 0)
    {
        auto pattern = find_pattern("1B D9 89 5C 24 24 0F 88");
        if (!pattern.empty())
            zpatch_speedup_clock = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)));
    }

    return SpeedupBugFixRunPublished(self, zpatch_speedup_clock, &ReadQueryPerformanceFrequency);
}

void Fixes::SpeedupBugFixPublishForTest(const Fixes::SpeedupSlots& slots)
{
    zpatch_speedup_t4c = slots.t4c;
    zpatch_speedup_t50 = slots.t50;
    zpatch_speedup_t54 = slots.t54;
    zpatch_speedup_t58 = slots.t58;
    zpatch_speedup_t5c = slots.t5c;
    zpatch_speedup_t60 = slots.t60;
    zpatch_speedup_t64 = slots.t64;
    zpatch_speedup_t68 = slots.t68;
}

void* Fixes::SpeedupBugFixForTest(void* self, uint32_t clock,
    uint64_t (__cdecl* read_frequency)())
{
    return SpeedupBugFixRunPublished(self, clock, read_frequency);
}

    // The cutscene_entry group: one hook at the shared camera callee instead of
    // the three sites. The callee is relocated into private memory, its 13
    // direct calls are re-pointed, and the branch fix is baked into the copy.
    // Any shape mismatch falls back to the three original sites, so the fix is
    // never lost.
    namespace
    {
        constexpr size_t kCutsceneCalleeSize = 0x2D2u;
        constexpr uint32_t kCutscenePokeOffset = 0x4Fu;
        constexpr uint32_t kCutsceneBranchOffset = 0x200u;
        constexpr uint32_t kCutsceneCalls[] = {
            0x00Eu, 0x01Du, 0x092u, 0x0E9u, 0x0F1u, 0x101u, 0x118u,
            0x127u, 0x13Cu, 0x233u, 0x24Cu, 0x286u, 0x29Au,
        };

        bool RelocateCalls(uint8_t* destination, const uint8_t* source,
            std::span<const uint32_t> offsets) noexcept
        {
            for (const uint32_t offset : offsets)
            {
                int32_t displacement{};
                std::memcpy(&displacement, source + offset + 1, sizeof(displacement));
                const auto target = reinterpret_cast<uintptr_t>(source + offset + 5) +
                                    static_cast<intptr_t>(displacement);
                const auto next = reinterpret_cast<uintptr_t>(destination + offset + 5);
                const auto relative = static_cast<intptr_t>(target) -
                                      static_cast<intptr_t>(next);
                if (relative < INT32_MIN || relative > INT32_MAX)
                    return false;
                const auto narrowed = static_cast<int32_t>(relative);
                std::memcpy(destination + offset + 1, &narrowed, sizeof(narrowed));
            }
            return true;
        }

        void* AllocateNearImage(size_t size) noexcept
        {
            const auto base = Game::Get().ImageBase();
            const auto imageSize = Game::Get().ImageSize();
            constexpr uintptr_t kStep = 0x1000000u;
            constexpr uintptr_t kReach = 0x40000000u;
            constexpr uintptr_t kPageMask = 0xFFFFu;
            const uintptr_t first = (base + imageSize + kPageMask) & ~kPageMask;
            for (uintptr_t candidate = first; candidate < first + kReach; candidate += kStep)
            {
                if (void* const block = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                    return block;
            }
            const uintptr_t lower = base > kReach ? base - kReach : kStep;
            for (uintptr_t candidate = first - kStep;
                 candidate > lower && candidate > kStep; candidate -= kStep)
            {
                if (void* const block = VirtualAlloc(reinterpret_cast<void*>(candidate), size,
                        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                    return block;
            }
            return nullptr;
        }

        uint8_t* CutsceneCalleeCopy(uintptr_t callee) noexcept
        {
            static constexpr uint8_t kPrologue[5] = { 0x55u, 0x8Bu, 0xECu, 0x83u, 0xE4u };
            static constexpr uint8_t kPokeSite[5] = { 0xC6u, 0x44u, 0x24u, 0x1Cu, 0x01u };
            static constexpr uint8_t kBranchSite[8] = {
                0x74u, 0x24u, 0x8Bu, 0x44u, 0x24u, 0x2Cu, 0x83u, 0xF8u,
            };
            const auto& game = Game::Get();
            if (callee == 0 || !game.Contains(callee) ||
                !game.Contains(callee + kCutsceneCalleeSize - 1))
                return nullptr;

            const auto* source = reinterpret_cast<const uint8_t*>(callee);
            if (std::memcmp(source, kPrologue, sizeof(kPrologue)) != 0 ||
                std::memcmp(source + 0x4Au, kPokeSite, sizeof(kPokeSite)) != 0 ||
                std::memcmp(source + kCutsceneBranchOffset, kBranchSite,
                    sizeof(kBranchSite)) != 0)
                return nullptr;

            auto* copy = static_cast<uint8_t*>(AllocateNearImage(kCutsceneCalleeSize));
            if (copy == nullptr)
                return nullptr;
            if (!RelocateCalls(copy, source, kCutsceneCalls))
            {
                VirtualFree(copy, 0, MEM_RELEASE);
                return nullptr;
            }
            // The copy's `jz` at +0x200 becomes the unconditional `jmp`, with
            // the original displacement kept.
            copy[kCutsceneBranchOffset] = static_cast<uint8_t>(Bytes::ShortJmp);
            return copy;
        }
    } // namespace

    // which moves the stored int bit-for-bit. Four bytes each; the bytes around
    // them are untouched.
    constexpr std::array<uint8_t, 4> kMovdXmm0Eax{ 0x66, 0x0F, 0x6E, 0xC0 };
    constexpr std::array<uint8_t, 4> kMovdXmm1Eax{ 0x66, 0x0F, 0x6E, 0xC8 };

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

// The bounded formatter's entry, published by source/shared.ixx (sub_10001280).
extern "C"
{
    extern uint32_t zpatch_format_entry;
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
}

extern "C"
{
    // dword_1015BDE4: the camera function the site calls. The worker publishes
    // `match + [match - 4]` -- the destination of the `E8` it is about to
    // overwrite -- and this is that address.
    uint32_t zpatch_cutscene_target = 0;

    // dword_1015BDF0: the same address + 0x4F. The camera function's
    // `mov byte [esp + 1Ch], 1` sits at target + 0x4A and its immediate byte is the
    // last one (target + 0x4E); the body's `dec esi` lands the store there.
    uint32_t zpatch_cutscene_poke = 0;

    // dword_1015BDE8: the accumulated scene-frame time, a double that starts at
    // zero (the original's .bss).
    uint64_t zpatch_cutscene_accum = 0;

    // 0x10063010: the original's VirtualProtect import slot. The arm fills this
    // with the module's own import; the body still calls it indirectly (`call edi`
    // after `mov edi, [slot]`), exactly as the original does.
    uint32_t zpatch_cutscene_virtual_protect = 0;

    // The body's literal pool, from the original ASI's .rdata. Each is declared
    // with the type its instruction reads and initialised with the number it is;
    // every literal round-trips to the original's bytes at the VA in its comment.
    // The one mask stays a hex word because it is not a number. No explicit
    // alignment: a scalar load does not need one on x86.
    double zpatch_cutscene_zero = 0.0;        // 0x101522F0
    // 0x10152340: the double nearest 0.3333, not 1/3.
    double zpatch_cutscene_limit = 0.3333;
    // 0x101523D0: an `andps` sign mask, not a number (the body clears the sign bit
    // of a difference before comparing it), so this one stays a hex word.
    uint32_t zpatch_cutscene_abs_mask = 0x7FFFFFFFu;
    float zpatch_cutscene_eps_a = 0.05f;      // 0x101522F8
    float zpatch_cutscene_eps_b = 0.5f;       // 0x101522FC
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

namespace Fixes
{

    // CutsceneFixes' published state. The worker publishes two game pointers from
    // the primary site's own `E8` operand: the call's destination (the camera
    // function) and that destination + 0x4F, one past the immediate the body
    // pokes. Both are read here, before the install tier overwrites the call; the
    // secondary site calls the same function and publishes nothing, as the
    // original's second scan does.
    void PublishCutsceneFixes()
    {
        if (!Loader::Enabled("CutsceneFixes"))
            return;

        auto pattern = find_pattern("8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8");
        if (!pattern.empty())
        {
            auto* match = pattern.get_first<std::byte>(0);
            const auto displacement = *pattern.get_first<int32_t>(-4);
            zpatch_cutscene_target =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + displacement));
            zpatch_cutscene_poke =
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(match + displacement + 0x4F));
            Log::Print("cutscene: match {:08X} disp {:08X} target {:08X} poke {:08X}",
                       reinterpret_cast<uintptr_t>(match), displacement,
                       zpatch_cutscene_target, zpatch_cutscene_poke);
        }

        // The body's VirtualProtect call goes through a slot because the original
        // loads its IAT entry by absolute address; fill it from the module's own
        // import, as the mouse advance helper does.
        zpatch_cutscene_virtual_protect = reinterpret_cast<uintptr_t>(&VirtualProtect);
    }

    // CutsceneFixes: chain B's two camera call sites become calls into
    // sub_100388C0, and chain A's `je` in the camera function becomes the
    // unconditional `jmp` that skips its "target found" test. Each caller's
    // pattern starts at the epilogue/a byte the call precedes, so the site is the
    // five bytes before the match. Chain A's two texts are the original worker's
    // first and fallback patterns: the first is the one the supported 1.0.8.0-class
    // images carry, the second covers the older ones.
    void InstallCutsceneFixes()
    {
        if (!Loader::Enabled("CutsceneFixes"))
            return;

        Bodies::Register(reinterpret_cast<uintptr_t>(&CutsceneChainBody), 0x100388C0u);
        // The body's call names the resolve forwarder, which stands in for the
        // original's sub_10003980; recording that VA keeps the registered branch in
        // the original's coordinates.
        Bodies::Register(reinterpret_cast<uintptr_t>(&CutsceneResolveEntry), 0x10003980u);

        if (zpatch_cutscene_target != 0)
        {
            const auto callee = static_cast<uintptr_t>(zpatch_cutscene_target);
            if (uint8_t* copy = CutsceneCalleeCopy(callee))
            {
                zpatch_cutscene_target = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(copy));
                zpatch_cutscene_poke = static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(copy) + kCutscenePokeOffset);
                injector::MakeJMP(reinterpret_cast<void*>(callee),
                    reinterpret_cast<void*>(&CutsceneChainBody), true);
                Log::Print("cutscene: hooked callee {:08X}, copy {:08X}", callee,
                    reinterpret_cast<uintptr_t>(copy));
                return;
            }
        }

        auto pattern = find_pattern("8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8");
        if (!pattern.empty())
        {
            injector::MakeCALL(pattern.get_first(-5),
                reinterpret_cast<void*>(&CutsceneChainBody), true);
        }

        pattern = find_pattern("8B CF 88 44 24 0F E8 ? ? ? ? A1");
        if (!pattern.empty())
        {
            injector::MakeCALL(pattern.get_first(-5),
                reinterpret_cast<void*>(&CutsceneChainBody), true);
        }

        pattern = find_pattern("74 24 8B 44 24 2C 83 F8 03");
        if (pattern.empty())
            pattern = find_pattern("74 0B 83 7C 24 2C 03");
        if (!pattern.empty())
            injector::WriteMemory<std::byte>(pattern.get_first(0), Bytes::ShortJmp, true);

        Log::Print("cutscene: resolved={} target={:08X} poke={:08X} resolveEntry={:08X}",
                   zpatch_cutscene_target != 0 ? 1 : 0, zpatch_cutscene_target,
                   zpatch_cutscene_poke, zpatch_native_resolve_entry);
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

    void InstallHighFPSSpeedupFix()
    {
        if (!Loader::Enabled("HighFPSSpeedupFix"))
            return;

        auto pattern = find_pattern("0F 2F C1 76 03 0F 28 C8 F3 0F 10 15");
        if (pattern.empty())
            return;

        for (const auto& site : kSpeedupSites)
        {
            if (site.shortJump)
                injector::WriteMemory<std::byte>(pattern.get_first(site.offset), Bytes::ShortJmp, true);
            else
                injector::MakeNOP(pattern.get_first(site.offset), 2, true);
        }
    }

    // MouseFix's published words, read before the sites are written: the two axis
    // pointers, the invert byte, the game's frame clock and the two resumes. The
    // first three are absolute operands of the sites the install tier overwrites,
    // which is why the publish tier owns them. Each operand is a live VA (the
    // loader has already applied the image's relocations; the offline dump
    // relocates its copy), so the captured dword is the address and is stored as
    // read.
    void PublishMouseFix()
    {
        if (!Loader::Enabled("MouseFix"))
            return;

        auto pattern = find_pattern("51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C");
        if (!pattern.empty())
        {
            zpatch_mf_x = *pattern.get_first<uint32_t>(13);
            zpatch_mf_y = *pattern.get_first<uint32_t>(20);
        }

        pattern = find_pattern("8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D");
        if (!pattern.empty())
            zpatch_mf_flag = *pattern.get_first<uint32_t>(20);

        pattern = find_pattern("83 EC 08 DB 05 ? ? ? ? 68");
        if (!pattern.empty())
        {
            zpatch_mf_time =
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
        }

        pattern = find_pattern("53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46");
        if (!pattern.empty())
        {
            const auto match = reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0));
            zpatch_mf_resume_a = static_cast<uint32_t>(match);
            zpatch_mf_resume_b = static_cast<uint32_t>(match + 1878);
        }

        // The game's VirtualProtect, which the advance helper calls through its
        // IAT slot in the original (`FF 15`), so it stays an indirect call here.
        zpatch_mf_virtual_protect = reinterpret_cast<uintptr_t>(&VirtualProtect);
    }

    // MouseFix: the pair of axis readers, the three `cvtsi2ss` sites and the two
    // angle sites. Every write is the original handler's: an `E9` at the two axis
    // readers and the flagged reader, the movd pair at +5/+33 with the two-byte
    // nops at +3/+31, the movd single at +5 with its two-byte nop at +3, the two
    // `E8`s at +51/+90 of the int-axis site, and the two `E9`s at the angle site's
    // -15 and +1862.
    void InstallMouseFix()
    {
        if (!Loader::Enabled("MouseFix"))
            return;

        auto pattern = find_pattern("51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAxisFloatBody), 0x10024060u);
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(&MfAxisFloatBody), true);
        }

        // The negated-axis reader's anchor differs from the flagged one only in
        // the instruction after the call, so it is its own signature.
        pattern = find_pattern("8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAxisNegBody), 0x100240C0u);
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(&MfAxisNegBody), true);
        }

        constexpr std::string_view kMovdSites[] = {
            "83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
            "83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07",
        };
        for (const auto site : kMovdSites)
        {
            pattern = find_pattern(site);
            if (pattern.empty())
                continue;

            injector::WriteMemoryRaw(pattern.get_first(5),
                const_cast<uint8_t*>(kMovdXmm0Eax.data()), kMovdXmm0Eax.size(), true);
            injector::WriteMemoryRaw(pattern.get_first(33),
                const_cast<uint8_t*>(kMovdXmm0Eax.data()), kMovdXmm0Eax.size(), true);
            injector::MakeNOP(pattern.get_first(3), 2, true);
            injector::MakeNOP(pattern.get_first(31), 2, true);
        }

        pattern = find_pattern("83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46");
        if (!pattern.empty())
        {
            injector::WriteMemoryRaw(pattern.get_first(5),
                const_cast<uint8_t*>(kMovdXmm1Eax.data()), kMovdXmm1Eax.size(), true);
            injector::MakeNOP(pattern.get_first(3), 2, true);
        }

        pattern = find_pattern("8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAxisFlagBody), 0x10024170u);
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(&MfAxisFlagBody), true);
        }

        // The int-axis helper is called from two places of one function (the
        // anchor's +51 and +90), so both calls are retargeted from the one scan.
        pattern = find_pattern("56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAxisIntBody), 0x10024120u);
            injector::MakeCALL(pattern.get_first(51),
                reinterpret_cast<void*>(&MfAxisIntBody), true);
            injector::MakeCALL(pattern.get_first(90),
                reinterpret_cast<void*>(&MfAxisIntBody), true);
        }

        // The two site bodies: the angle hook 15 bytes before the anchor and the
        // advance hook 1862 bytes after it.
        pattern = find_pattern("53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAngleBody), 0x10024560u);
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAdvanceBody), 0x100243A0u);
            Bodies::Register(reinterpret_cast<uintptr_t>(&MfAdvanceWindowsBody), 0x100241E0u);
            injector::MakeJMP(pattern.get_first(-15),
                reinterpret_cast<void*>(&MfAngleBody), true);
            injector::MakeJMP(pattern.get_first(1862),
                reinterpret_cast<void*>(&MfAdvanceBody), true);
        }
    }

    void InstallVSyncFix()
    {
        if (!Loader::Enabled("VSyncFix"))
            return;

        auto pattern = find_pattern("8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(30),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_vsync_flag)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(30)));
        }

        pattern = find_pattern("8B 10 50 8B 82 A8 00 00 00 FF D0 83");
        if (!pattern.empty())
        {
            injector::WriteMemory<uint32_t>(pattern.get_first<std::byte>(13),
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&zpatch_vsync_flag)), true);
            Bodies::RegisterPublished(reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(13)));
        }
    }

    // DrivebyFOVFix: the site is the `movss xmm0,[esi+60h]` in front of the
    // per-frame drive-by FOV subtraction. The publish reads the site's own next
    // instruction (the `subss xmm1, xmm2` at the anchor's +5) for the resume before
    // the install overwrites the five bytes the jump lands on.
    void PublishDrivebyFOVFix()
    {
        if (!Loader::Enabled("DrivebyFOVFix"))
            return;

        auto pattern = find_pattern("F3 0F 10 46 60 F3 0F 5C CA");
        if (!pattern.empty())
            zpatch_driveby_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first(5)));
    }

    // Publishing the eight floats before the install is what keeps the body's
    // writes on the game's own words; the install only replaces the function's
    // entry.
    void PublishSpeedupBugFix()
    {
        if (!Loader::Enabled("SpeedupBugFix"))
            return;

        zpatch_speedup_site = ResolveSpeedupBugFix();
    }

    void InstallDrivebyFOVFix()
    {
        if (!Loader::Enabled("DrivebyFOVFix"))
            return;

        auto pattern = find_pattern("F3 0F 10 46 60 F3 0F 5C CA");
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&DrivebyFovShimBody), 0x100064B0u);
            Bodies::Register(reinterpret_cast<uintptr_t>(&DrivebyFovHelperBody), 0x10006450u);
            // The helper's call names the original's guarded resolve entry; recording
            // that VA keeps every registered branch in the original's coordinates.
            Bodies::Register(reinterpret_cast<uintptr_t>(&DrivebyResolveEntry), 0x10003980u);
            injector::MakeJMP(pattern.get_first(0),
                reinterpret_cast<void*>(&DrivebyFovShimBody), true);
        }
    }

    void InstallSpeedupBugFix()
    {
        if (!Loader::Enabled("SpeedupBugFix"))
            return;

        if (zpatch_speedup_site != nullptr)
            injector::MakeJMP(zpatch_speedup_site, reinterpret_cast<void*>(&SpeedupBugFixBody),
                true);
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

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// The helper's call target. The original's helper does `call sub_10003980` (E8), so
// ours must be a direct call; a module interface cannot name an imported function
// from inline asm, so this six-byte forwarder jumps through the entry the native
// layer publishes. ECX (the hash) and the stack reach the layer untouched, and the
// layer's __try/__except frame is the one the original's SEH entry installs.
static __declspec(naked) void DrivebyResolveEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_native_resolve_entry]
    }
}

// sub_10006450, recovered VA 0x10006450 (20 instructions): computes the drive-by
// FOV once and caches the entry it reads it from. It resolves the native hash
// dword_101580D4 through the call layer, walks the handler's two structures at
// +0Eh/+0Fh/+14h, stores the entry in dword_1015BDF8 and reads the float at +60h
// into dword_101580C4. The one-shot flag makes every later call only refresh the
// default. The resolver result and the cached entry are dereferenced unguarded --
// the same faults the original has (0x1000646E and 0x10006490) -- and 45.0f is
// stored on every call, its own first instruction.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void DrivebyFovHelperBody()
{
    __asm
    {
        cmp BYTE PTR [zpatch_driveby_done], 0
        mov DWORD PTR [zpatch_driveby_fov], 42340000h  // 45.0f
        jne driveby_have
        mov ecx, DWORD PTR [zpatch_native_salt_101580D4]
        call DrivebyResolveEntry
        mov ecx, DWORD PTR [eax + 0Eh]
        add eax, 0Eh
        add ecx, eax
        mov BYTE PTR [zpatch_driveby_done], 1
        mov eax, DWORD PTR [ecx + 0Fh]
        mov eax, DWORD PTR [eax + ecx + 14h]
        mov DWORD PTR [zpatch_driveby_cached], eax
        jmp driveby_read
    driveby_have:
        mov eax, DWORD PTR [zpatch_driveby_cached]
    driveby_read:
        mov eax, DWORD PTR [eax]
        test eax, eax
        je driveby_done
        movss xmm0, DWORD PTR [eax + 60h]
        movss DWORD PTR [zpatch_driveby_fov], xmm0
    driveby_done:
        ret
    }
}

// sub_100064B0, recovered VA 0x100064B0 (5 instructions): the site's shim. It saves
// every general register (the site resumes with its own GPRs and flags intact),
// computes the FOV, restores them, loads the value into xmm0 and tail-jumps to the
// site's own remaining `subss xmm1, xmm2` at the published resume.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void DrivebyFovShimBody()
{
    __asm
    {
        __emit 0x60  // pushad (pushal)
        call DrivebyFovHelperBody
        __emit 0x61  // popad (popal)
        movss xmm0, DWORD PTR [zpatch_driveby_fov]
        jmp DWORD PTR [zpatch_driveby_resume]
    }
}

// sub_10024060, recovered VA 0x10024060 (30 instructions): X or Y times 1/128,
// answered in st(0). Axis 0 is X, axis 1 is Y, anything else is +0.0.
static __declspec(naked) void MfAxisFloatBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        sub esp, 8
        test eax, eax
        jne mf_axis_float_y
        mov eax, DWORD PTR [zpatch_mf_x]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale]
        movsd QWORD PTR [ebp - 8], xmm0
        fld QWORD PTR [ebp - 8]
        mov esp, ebp
        pop ebp
        ret
    mf_axis_float_y:
        cmp eax, 1
        jne mf_axis_float_zero
        mov eax, DWORD PTR [zpatch_mf_y]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale]
        movsd QWORD PTR [ebp - 8], xmm0
        fld QWORD PTR [ebp - 8]
        mov esp, ebp
        pop ebp
        ret
    mf_axis_float_zero:
        fldz
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_100240C0, recovered VA 0x100240C0 (23 instructions): the negated axis, as a
// float scaled by -127.5. The sign mask is the original's xorps pair.
static __declspec(naked) void MfAxisNegBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        test eax, eax
        jne mf_axis_neg_y
        mov eax, DWORD PTR [zpatch_mf_x]
    mf_axis_neg_xy:
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale]
        jmp mf_axis_neg_scale
    mf_axis_neg_y:
        cmp eax, 1
        jne mf_axis_neg_zero
        mov eax, DWORD PTR [zpatch_mf_y]
        jmp mf_axis_neg_xy
    mf_axis_neg_zero:
        xorps xmm0, xmm0
    mf_axis_neg_scale:
        movss xmm1, DWORD PTR [zpatch_mf_sign]
        xorps xmm1, XMMWORD PTR [zpatch_mf_single_sign]
        cvtpd2ps xmm0, xmm0
        mulss xmm1, xmm0
        movss DWORD PTR [ebp + 0Ch], xmm1
        mov eax, DWORD PTR [ebp + 0Ch]
        pop ebp
        ret
    }
}

// sub_10024120, recovered VA 0x10024120 (22 instructions): the axis as an int,
// `(int)(value * 1/128 * 255)`.
static __declspec(naked) void MfAxisIntBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        test eax, eax
        jne mf_axis_int_y
        mov eax, DWORD PTR [zpatch_mf_x]
    mf_axis_int_xy:
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale]
        mulsd xmm0, QWORD PTR [zpatch_mf_scale_255]
        cvttsd2si eax, xmm0
        pop ebp
        ret
    mf_axis_int_y:
        cmp eax, 1
        jne mf_axis_int_zero
        mov eax, DWORD PTR [zpatch_mf_y]
        jmp mf_axis_int_xy
    mf_axis_int_zero:
        xorps xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale_255]
        cvttsd2si eax, xmm0
        pop ebp
        ret
    }
}

// sub_10024170, recovered VA 0x10024170 (28 instructions): the negated axis, with
// the sign flipped back when the game's invert byte is set.
static __declspec(naked) void MfAxisFlagBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        mov eax, DWORD PTR [ebp + 0Ch]
        test eax, eax
        jne mf_axis_flag_y
        mov eax, DWORD PTR [zpatch_mf_x]
    mf_axis_flag_xy:
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, QWORD PTR [zpatch_mf_scale]
        jmp mf_axis_flag_scale
    mf_axis_flag_y:
        cmp eax, 1
        jne mf_axis_flag_zero
        mov eax, DWORD PTR [zpatch_mf_y]
        jmp mf_axis_flag_xy
    mf_axis_flag_zero:
        xorps xmm0, xmm0
    mf_axis_flag_scale:
        movss xmm1, DWORD PTR [zpatch_mf_sign]
        xorps xmm1, XMMWORD PTR [zpatch_mf_single_sign]
        mov eax, DWORD PTR [zpatch_mf_flag]
        cvtpd2ps xmm0, xmm0
        cmp BYTE PTR [eax], 0
        mulss xmm1, xmm0
        movss DWORD PTR [ebp + 0Ch], xmm1
        jz mf_axis_flag_store
        mulss xmm1, DWORD PTR [zpatch_mf_mul_neg1]
        movss DWORD PTR [ebp + 0Ch], xmm1
    mf_axis_flag_store:
        mov eax, DWORD PTR [ebp + 0Ch]
        pop ebp
        ret
    }
}

// sub_10024440, recovered VA 0x10024440 (62 instructions): advance the vehicle's
// pitch (+144h) and yaw (+148h) by the mouse delta scaled by the frame-time
// factor, wrapping yaw into [-pi, pi]. Reads `this` from ECX, calls the game's
// frame clock through the published slot, and keeps the original's two exits.
static __declspec(naked) void MfAngleStepBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        sub esp, 0Ch
        push esi
        mov esi, ecx
        call DWORD PTR [zpatch_mf_time]
        movss xmm2, DWORD PTR [zpatch_mf_step_scale]
        mov eax, DWORD PTR [zpatch_mf_x]
        fstp QWORD PTR [esp + 8]
        movsd xmm0, QWORD PTR [esp + 8]
        mulsd xmm0, QWORD PTR [zpatch_mf_time_mul]
        movd xmm4, DWORD PTR [eax]
        mov eax, DWORD PTR [zpatch_mf_y]
        cvtdq2pd xmm4, xmm4
        addsd xmm0, QWORD PTR [zpatch_mf_time_add]
        movd xmm3, DWORD PTR [eax]
        mulsd xmm4, QWORD PTR [zpatch_mf_scale]
        mov eax, DWORD PTR [zpatch_mf_flag]
        cvtdq2pd xmm3, xmm3
        cmp BYTE PTR [eax], 0
        xorps xmm4, XMMWORD PTR [zpatch_mf_double_sign]
        mulsd xmm3, QWORD PTR [zpatch_mf_scale]
        cvtpd2ps xmm0, xmm0
        xorps xmm3, XMMWORD PTR [zpatch_mf_double_sign]
        mulss xmm2, xmm0
        mulss xmm2, DWORD PTR [esi + 60h]
        jz mf_angle_step_skip
        mulsd xmm3, QWORD PTR [zpatch_mf_mul_neg1_d]
    mf_angle_step_skip:
        movss xmm0, DWORD PTR [esi + 144h]
        cvtps2pd xmm2, xmm2
        cvtps2pd xmm0, xmm0
        movaps xmm1, xmm2
        mulsd xmm2, xmm4
        mulsd xmm1, xmm3
        addsd xmm1, xmm0
        cvtpd2ps xmm0, xmm1
        movss DWORD PTR [esi + 144h], xmm0
        movss xmm0, DWORD PTR [esi + 148h]
        cvtps2pd xmm0, xmm0
        addsd xmm2, xmm0
        cvtpd2ps xmm0, xmm2
        cvtps2pd xmm1, xmm0
        comisd xmm1, QWORD PTR [zpatch_mf_pi]
        jbe mf_angle_step_low
        subsd xmm1, QWORD PTR [zpatch_mf_two_pi]
        cvtpd2ps xmm0, xmm1
        movss DWORD PTR [esi + 148h], xmm0
        pop esi
        mov esp, ebp
        pop ebp
        ret
    mf_angle_step_low:
        movsd xmm0, QWORD PTR [zpatch_mf_neg_pi]
        comisd xmm0, xmm1
        jbe mf_angle_step_store
        addsd xmm1, QWORD PTR [zpatch_mf_two_pi]
    mf_angle_step_store:
        cvtpd2ps xmm0, xmm1
        movss DWORD PTR [esi + 148h], xmm0
        pop esi
        mov esp, ebp
        pop ebp
        ret
    }
}

// sub_10024560, recovered VA 0x10024560 (25 instructions): the angle site's body.
// It saves xmm0-7, advances the angles through the helper, restores them, replays
// the fifteen bytes of the game's prologue the jump replaced, and tail-jumps to
// the published resume (the site's own match).
static __declspec(naked) void MfAngleBody()
{
    __asm
    {
        pushad
        movss DWORD PTR [zpatch_mf_xmm_save], xmm0
        movss DWORD PTR [zpatch_mf_xmm_save + 4], xmm1
        movss DWORD PTR [zpatch_mf_xmm_save + 8], xmm2
        movss DWORD PTR [zpatch_mf_xmm_save + 12], xmm3
        movss DWORD PTR [zpatch_mf_xmm_save + 16], xmm4
        movss DWORD PTR [zpatch_mf_xmm_save + 20], xmm5
        movss DWORD PTR [zpatch_mf_xmm_save + 24], xmm6
        movss DWORD PTR [zpatch_mf_xmm_save + 28], xmm7
        call MfAngleStepBody
        movss xmm0, DWORD PTR [zpatch_mf_xmm_save]
        movss xmm1, DWORD PTR [zpatch_mf_xmm_save + 4]
        movss xmm2, DWORD PTR [zpatch_mf_xmm_save + 8]
        movss xmm3, DWORD PTR [zpatch_mf_xmm_save + 12]
        movss xmm4, DWORD PTR [zpatch_mf_xmm_save + 16]
        movss xmm5, DWORD PTR [zpatch_mf_xmm_save + 20]
        movss xmm6, DWORD PTR [zpatch_mf_xmm_save + 24]
        movss xmm7, DWORD PTR [zpatch_mf_xmm_save + 28]
        popad
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF0h
        xorps xmm0, xmm0
        sub esp, 94h
        jmp DWORD PTR [zpatch_mf_resume_a]
    }
}

// sub_100241E0, recovered VA 0x100241E0 (138 instructions): the advance helper.
// It evaluates Y and then X (both unguarded, as in the original), picks the 8-byte
// window image the resumed function must carry, and writes three 8-byte windows at
// resume+34h, +5Eh and +6Bh through VirtualProtect. The window bytes are the
// original's immediates: `movss [esi+144h], xmm0` / `movss [esi+148h], xmm1` /
// `fstp [esi+148h]` for the pull form, and their disabled forms (`nop` fill, and
// `fstp st(0)` for the x87 window because the enclosing routine pops that value
// later). The /GS frame is the original's.
static __declspec(naked) void MfAdvanceWindowsBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        and esp, 0FFFFFFF8h
        sub esp, 0Ch
        mov eax, DWORD PTR [__security_cookie]
        xor eax, esp
        mov DWORD PTR [esp + 8], eax
        mov eax, DWORD PTR [zpatch_mf_x]
        xorps xmm2, xmm2
        movsd xmm1, QWORD PTR [zpatch_mf_scale]
        push ebx
        push esi
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        push edi
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_0
        mov eax, DWORD PTR [zpatch_mf_y]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_0
        mov edi, 86110FF3h
        mov ebx, 144h
        jmp mf_advance_write_0
    mf_advance_disabled_0:
        mov edi, 90909090h
        mov ebx, edi
    mf_advance_write_0:
        mov esi, DWORD PTR [zpatch_mf_resume_b]
        lea eax, [esp + 0Ch]
        push eax
        push 40h
        push 8
        add esi, 34h
        push esi
        call DWORD PTR [zpatch_mf_virtual_protect]
        lea eax, [esp + 0Ch]
        mov DWORD PTR [esi + 4], ebx
        mov ebx, DWORD PTR [zpatch_mf_virtual_protect]
        push eax
        push DWORD PTR [esp + 10h]
        mov DWORD PTR [esi], edi
        push 8
        push esi
        call ebx
        mov eax, DWORD PTR [zpatch_mf_x]
        xorps xmm2, xmm2
        movsd xmm1, QWORD PTR [zpatch_mf_scale]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_1
        mov eax, DWORD PTR [zpatch_mf_y]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_1
        mov edi, 8E110FF3h
        mov DWORD PTR [esp + 0Ch], 148h
        jmp mf_advance_write_1
    mf_advance_disabled_1:
        mov edi, 90909090h
        mov DWORD PTR [esp + 0Ch], edi
    mf_advance_write_1:
        mov esi, DWORD PTR [zpatch_mf_resume_b]
        lea eax, [esp + 10h]
        push eax
        push 40h
        push 8
        add esi, 5Eh
        push esi
        call ebx
        mov eax, DWORD PTR [esp + 0Ch]
        mov DWORD PTR [esi + 4], eax
        lea eax, [esp + 10h]
        push eax
        push DWORD PTR [esp + 14h]
        mov DWORD PTR [esi], edi
        push 8
        push esi
        call ebx
        mov eax, DWORD PTR [zpatch_mf_x]
        xorps xmm2, xmm2
        movsd xmm1, QWORD PTR [zpatch_mf_scale]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_2
        mov eax, DWORD PTR [zpatch_mf_y]
        movd xmm0, DWORD PTR [eax]
        cvtdq2pd xmm0, xmm0
        mulsd xmm0, xmm1
        ucomisd xmm0, xmm2
        lahf
        test ah, 44h
        jp mf_advance_disabled_2
        mov edi, 1489ED9h
        mov ebx, 5C8B0000h
        jmp mf_advance_write_2
    mf_advance_disabled_2:
        mov edi, 9090D8DDh
        mov ebx, 5C8B9090h
    mf_advance_write_2:
        mov esi, DWORD PTR [zpatch_mf_resume_b]
        lea eax, [esp + 10h]
        push eax
        push 40h
        push 8
        add esi, 6Bh
        push esi
        call DWORD PTR [zpatch_mf_virtual_protect]
        lea eax, [esp + 10h]
        mov DWORD PTR [esi], edi
        push eax
        push DWORD PTR [esp + 14h]
        mov DWORD PTR [esi + 4], ebx
        push 8
        push esi
        call DWORD PTR [zpatch_mf_virtual_protect]
        mov ecx, DWORD PTR [esp + 14h]
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

// sub_100243A0, recovered VA 0x100243A0 (23 instructions): the advance site's
// body. The same xmm save/restore around the advance helper, then it replays the
// three instructions the jump replaced and tail-jumps to the published resume.
static __declspec(naked) void MfAdvanceBody()
{
    __asm
    {
        pushad
        movss DWORD PTR [zpatch_mf_xmm_save], xmm0
        movss DWORD PTR [zpatch_mf_xmm_save + 4], xmm1
        movss DWORD PTR [zpatch_mf_xmm_save + 8], xmm2
        movss DWORD PTR [zpatch_mf_xmm_save + 12], xmm3
        movss DWORD PTR [zpatch_mf_xmm_save + 16], xmm4
        movss DWORD PTR [zpatch_mf_xmm_save + 20], xmm5
        movss DWORD PTR [zpatch_mf_xmm_save + 24], xmm6
        movss DWORD PTR [zpatch_mf_xmm_save + 28], xmm7
        call MfAdvanceWindowsBody
        movss xmm0, DWORD PTR [zpatch_mf_xmm_save]
        movss xmm1, DWORD PTR [zpatch_mf_xmm_save + 4]
        movss xmm2, DWORD PTR [zpatch_mf_xmm_save + 8]
        movss xmm3, DWORD PTR [zpatch_mf_xmm_save + 12]
        movss xmm4, DWORD PTR [zpatch_mf_xmm_save + 16]
        movss xmm5, DWORD PTR [zpatch_mf_xmm_save + 20]
        movss xmm6, DWORD PTR [zpatch_mf_xmm_save + 24]
        movss xmm7, DWORD PTR [zpatch_mf_xmm_save + 28]
        popad
        mulss xmm0, xmm2
        addss xmm0, xmm3
        movss xmm3, DWORD PTR [esi + 150h]
        jmp DWORD PTR [zpatch_mf_resume_b]
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

// The body's `call 0x10003980` must stay a direct `call`: the original's is an
// `E8`, and an indirect `call [slot]` is a different instruction shape. A module
// interface cannot export a naked function either (C2488), so the call names this
// six-byte forwarder, which enters the layer's C entry with ECX (the salt) and
// the stack untouched -- the same shape as the drive-by forwarder.
static __declspec(naked) void CutsceneResolveEntry()
{
    __asm
    {
        jmp DWORD PTR [zpatch_native_resolve_entry]
    }
}

// sub_100388C0, recovered VA 0x100388C0: chain B's body, 146 instructions and 592
// bytes to its `ret 4`, plus the restore block at 0x10038B10..0x10038B3F that the
// six `ja` exits reach (the proof sweeps the 146; the restore block follows the
// `ret` and is not decoded by it). Its frame is the original's 0xA18: two
// 0x140-dword object copies (ebp-504h and ebp-0A04h), the 8-byte block
// VirtualProtect writes its old protection to, and the three scratch words the
// camera call uses.
//
// It resolves a game native through the loader salt dword_1015803C, copies the
// scene object it answers with into the first block and accumulates that object's
// frame delta (a float at +8) into the double accumulator. It then makes the
// camera function's `mov byte [esp + 1Ch], 1` immediate readable, writes 1 over
// it, calls the function with the body's own float argument and keeps the call's
// double result as a flag. Once the recovered limit (0.3333) has accumulated it
// calls again with that immediate forced to 0, and if the two runs' position
// floats differ by more than (0.05, 0.05, 0.05, 0.5, 0.5, 0.5) it restores the
// object as it was before the second call and resets the accumulator; otherwise
// it answers the second call's flag.
//
// The `ucomisd`/`lahf`/`test ah,44h`/`jnp` sequences are the original's own
// codegen for its `!= 0` tests: `mov al,1` when the comparison is non-zero or
// unordered. `rep movsd` is one instruction (F3 A5) on both sides.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void CutsceneChainBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 0A18h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push esi
        mov esi, ecx
        mov ecx, DWORD PTR [zpatch_native_salt_1015803C]
        push edi
        mov DWORD PTR [ebp - 0A10h], esi
        call CutsceneResolveEntry
        lea edi, [ebp - 504h]
        mov ecx, DWORD PTR [eax + 0Eh]
        mov eax, DWORD PTR [ecx + eax + 18h]
        mov ecx, 140h
        rep movsd
        movss xmm0, DWORD PTR [eax + 8]
        lea eax, [ebp - 0A08h]
        mov esi, DWORD PTR [zpatch_cutscene_poke]
        mov edi, DWORD PTR [zpatch_cutscene_virtual_protect]
        dec esi
        cvtps2pd xmm0, xmm0
        push eax
        push 40h
        push 1
        push esi
        addsd xmm0, QWORD PTR [zpatch_cutscene_accum]
        movsd QWORD PTR [zpatch_cutscene_accum], xmm0
        call edi
        lea eax, [ebp - 0A08h]
        mov BYTE PTR [esi], 1
        push eax
        push DWORD PTR [ebp - 0A08h]
        push 1
        push esi
        call edi
        mov eax, DWORD PTR [zpatch_cutscene_target]
        movss xmm0, DWORD PTR [ebp + 8]
        push ecx
        mov ecx, DWORD PTR [ebp - 0A10h]
        mov DWORD PTR [ebp - 0A14h], eax
        movss DWORD PTR [esp], xmm0
        call DWORD PTR [ebp - 0A14h]
        fstp QWORD PTR [ebp - 0A18h]
        movsd xmm0, QWORD PTR [ebp - 0A18h]
        ucomisd xmm0, QWORD PTR [zpatch_cutscene_zero]
        lahf
        test ah, 44h
        jnp ccb_al_zero
        mov al, 1
        jmp ccb_al_done
    ccb_al_zero:
        xor al, al
    ccb_al_done:
        mov edx, DWORD PTR [ebp - 0A10h]
        lea edi, [ebp - 0A04h]
        movsd xmm0, QWORD PTR [zpatch_cutscene_limit]
        mov ecx, 140h
        comisd xmm0, QWORD PTR [zpatch_cutscene_accum]
        mov esi, edx
        mov BYTE PTR [ebp - 0A09h], al
        rep movsd
        ja ccb_return_al
        mov edi, edx
        lea eax, [ebp - 0A08h]
        push eax
        mov ecx, 140h
        lea esi, [ebp - 504h]
        rep movsd
        mov esi, DWORD PTR [zpatch_cutscene_poke]
        mov edi, DWORD PTR [zpatch_cutscene_virtual_protect]
        dec esi
        push 40h
        push 1
        push esi
        call edi
        lea eax, [ebp - 0A08h]
        mov BYTE PTR [esi], 0
        push eax
        push DWORD PTR [ebp - 0A08h]
        push 1
        push esi
        call edi
        mov eax, DWORD PTR [zpatch_cutscene_target]
        movss xmm0, DWORD PTR [ebp + 8]
        push ecx
        mov ecx, DWORD PTR [ebp - 0A10h]
        mov DWORD PTR [ebp - 0A14h], eax
        movss DWORD PTR [esp], xmm0
        call DWORD PTR [ebp - 0A14h]
        fstp QWORD PTR [ebp - 0A18h]
        movsd xmm0, QWORD PTR [ebp - 0A18h]
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
        mov eax, DWORD PTR [ebp - 0A10h]
        lea edi, [ebp - 504h]
        movss xmm1, DWORD PTR [zpatch_cutscene_abs_mask]
        mov ecx, 140h
        movss xmm2, DWORD PTR [zpatch_cutscene_eps_a]
        mov esi, eax
        rep movsd
        movss xmm0, DWORD PTR [ebp - 4E4h]
        subss xmm0, DWORD PTR [ebp - 9E4h]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, DWORD PTR [ebp - 4E0h]
        subss xmm0, DWORD PTR [ebp - 9E0h]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, DWORD PTR [ebp - 4DCh]
        subss xmm0, DWORD PTR [ebp - 9DCh]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, DWORD PTR [ebp - 4C4h]
        subss xmm0, DWORD PTR [ebp - 9C4h]
        movss xmm2, DWORD PTR [zpatch_cutscene_eps_b]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, DWORD PTR [ebp - 4C0h]
        subss xmm0, DWORD PTR [ebp - 9C0h]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        movss xmm0, DWORD PTR [ebp - 4BCh]
        subss xmm0, DWORD PTR [ebp - 9BCh]
        andps xmm0, xmm1
        comiss xmm0, xmm2
        ja ccb_restore
        pop edi
        mov al, dl
        pop esi
        mov ecx, DWORD PTR [ebp - 4]
        xor ecx, ebp
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret 4
    ccb_restore:
        mov edi, eax
        movsd QWORD PTR [zpatch_cutscene_accum], xmm3
        mov al, BYTE PTR [ebp - 0A09h]
        lea esi, [ebp - 0A04h]
        mov ecx, 140h
        rep movsd
    ccb_return_al:
        mov ecx, DWORD PTR [ebp - 4]
        pop edi
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret 4
        __emit 0CCh
    }
}

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
    // The original's one-shot guard has no writer, so every call re-decodes,
    // re-registers and leaks the previous archive. Decode and mount once, then
    // answer the same archive for the process lifetime. The game can enter this
    // from more than one thread; the whole first build is serialised, and the
    // hit path is a single cached read.
    static std::mutex build_mutex;
    std::lock_guard lock(build_mutex);
    {
        static uint32_t cached_archive = 0;
        static uint32_t cached_state = 0;
        if (cached_archive != 0)
        {
            zpatch_ooc_state = cached_state;
            return cached_archive;
        }

        auto* table = static_cast<uint8_t*>(std::malloc(kOocTableSize));
        if (table == nullptr)
            return 0;
        if (OocDecodeBody(table) == 0)
        {
            std::free(table);
            return 0;
        }
        OocRegister(table);
        uint8_t* archive = static_cast<uint8_t*>(OocBuildArchiveBody());
        if (archive == nullptr)
            return 0;

        cached_archive = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(archive));
        cached_state = zpatch_ooc_offset;
        zpatch_ooc_state = cached_state;
        return cached_archive;
    }
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
// runs them against the pinned data instead. The two fnv1a
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
// in source/shared.ixx and proved there instruction for instruction
// A module interface cannot export a naked function, so this
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
