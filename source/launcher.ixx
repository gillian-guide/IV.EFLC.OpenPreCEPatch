module;

#include <common.hxx>

#include <cstring>
#include <tlhelp32.h>

export module launcher;

import bodies;
import bytes;
import common;
import game;
import loader;
import patch;

// The four transcribed file-API forwarders' operands. Each `call ds:[…]` in the
// original is the kernel32 import through its own IAT; the module has no IAT (static
// CRT), so the slot holds the same function. The create-file body also reads the
// address the DFA capture recorded (the original's dword_10159D30), which the
// capture writes before the install tier runs. All four are filled by
// PublishDfaForwarders() from RegisterLauncher(), before any tier.
extern "C"
{
    uint32_t zpatch_dfa_create_file = 0;          // the original's IAT slot 0x10063048
    uint32_t zpatch_dfa_create_file_slot = 0;     // dword_10159D30, the captured slot
    uint32_t zpatch_dfa_set_file_pointer = 0;     // the original's IAT slot 0x10063030
    uint32_t zpatch_dfa_get_file_size_ex = 0;     // 0x10063008
    uint32_t zpatch_dfa_read_file = 0;            // 0x10063000
}

// Forward declarations for the transcribed bodies at the bottom of this
// file: the readable code above installs them (and a body may branch to
// another), so every one is named here. MSVC rejects `__declspec(naked)`
// on a declaration (C2488), so these carry the signature only; each
// definition in the tail is naked.
static void DfaInitBody();
static void DfaCreateFileBody();
static void RomResetBody();
static void DfaSetFilePointerBody();
static void DfaGetFileSizeExBody();
static void DfaReadFileBody();
static void RomSetBody();

// ---------------------------------------------------------------------------
// SkipLauncher
//
// The original's launcher/SecuROM bypass, recovered from three handlers and run
// in their order:
//
//   0x10045CE0  the 25-entry main table and the two multi-hit collectors;
//   0x10004850  the DFA file-API hooks: one IAT capture, six E8/E9 installs and
//               two collected jumps;
//   0x10004620  the ROM message-window hooks: two E8/E9 installs whose bodies
//               start the WM_ROM service (0x100041A0..0x10004570).
//
// Everything here is in-place instruction patching except the eight hook bodies:
// the recovered rows overwrite a launcher check at the signature match (NOPs, an
// xor, a one-byte branch, a short jump, a long jump into the next row's match),
// so nothing in the tables is callable.
//
// Order inside one pass is the handler's own: main table, collectors, DFA
// capture, DFA installs, collected jumps, ROM installs. The two hook stages are
// gated the way the original gates them (see DfaSelected / the one-shot flags),
// and the ROM stage runs even when the main table is skipped.
namespace
{
    // Opcode bytes no other arm writes. bytes.ixx carries the shared ones; these
    // are named here because this module is the only writer.
    inline constexpr std::byte XorEaxOpcode{ 0x33 };   // xor eax, eax
    inline constexpr std::byte XorEaxModRm{ 0xC0 };
    inline constexpr std::byte MovMoffsEax{ 0xA3 };    // `mov [imm32], eax`, second byte
    inline constexpr std::byte LongJmp{ 0xE9 };        // jmp rel32
    inline constexpr std::byte MovAlOpcode{ 0xB0 };    // mov al, 1
    inline constexpr std::byte ImmediateOne{ 0x01 };

    using Run = std::span<const std::byte>;

    // The byte runs the rows write. Each name is the instruction the run forms;
    // a store row's length is its run's own size.
    inline constexpr std::array kJmpRel8{ Bytes::ShortJmp };
    inline constexpr std::array kJmp{ LongJmp };
    inline constexpr std::array kRet{ Bytes::Ret };
    inline constexpr std::array kNop2{ Bytes::Nop, Bytes::Nop };
    inline constexpr std::array kNop3{ Bytes::Nop, Bytes::Nop, Bytes::Nop };
    inline constexpr std::array kNop4{ Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop };
    inline constexpr std::array kNop6{ Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop,
        Bytes::Nop };
    inline constexpr std::array kNop8{ Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop,
        Bytes::Nop, Bytes::Nop, Bytes::Nop };
    inline constexpr std::array kNop11{ Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop,
        Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop };
    inline constexpr std::array kNop14{ Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop,
        Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop, Bytes::Nop,
        Bytes::Nop, Bytes::Nop };
    inline constexpr std::array kXorEax{ XorEaxOpcode, XorEaxModRm };
    inline constexpr std::array kNopMovMoffsEax{ Bytes::Nop, MovMoffsEax };
    inline constexpr std::array kXorEaxRetNop{ XorEaxOpcode, XorEaxModRm, Bytes::Ret, Bytes::Nop };
    // The collectors' whole window: `mov al, 1; ret; nop`.
    inline constexpr std::array kCollectorStub{ MovAlOpcode, ImmediateOne, Bytes::Ret, Bytes::Nop };

    // One in-place edit of a matched launcher window. A row's offset is
    // match-relative and lands on the instruction it rewrites.
    struct LauncherPatch
    {
        enum class Kind : uint8_t
        {
            store,   // write `bytes` at match + offset
            fill,    // write `count` NOPs at match + offset
            target,  // write the rel32 at match + offset for the `jmp` byte the
                     // previous patch of this row stored, landing on `targetEntry`'s match
        };

        Kind kind{};
        int32_t offset{};
        std::span<const std::byte> bytes{};
        uint16_t count{};
        uint32_t targetEntry{};
    };

    constexpr LauncherPatch Store(int32_t offset, Run bytes)
    {
        return { .kind = LauncherPatch::Kind::store, .offset = offset, .bytes = bytes };
    }

    constexpr LauncherPatch Fill(int32_t offset, uint16_t count)
    {
        return { .kind = LauncherPatch::Kind::fill, .offset = offset, .count = count };
    }

    // The original writes the E9 and its displacement as two separate stores, so
    // the row keeps that shape: this patch owns only the displacement.
    constexpr LauncherPatch JumpTarget(int32_t offset, uint32_t targetEntry)
    {
        return { .kind = LauncherPatch::Kind::target, .offset = offset, .targetEntry = targetEntry };
    }

    struct LauncherEntry
    {
        std::string_view signature;
        std::span<const LauncherPatch> patches;  // empty for the row that is only a jump target
    };

    // The main table, in the handler's order (0x1012DC74..0x1012E658). Entry 1 is
    // the EFC check's entry and the target of entry 9's jump; it has no edits of its
    // own. The build range in each comment is the recovered annotation for the
    // *launcher* version the row belongs to, not a GTA IV build.
    inline constexpr LauncherPatch kWrites0[] = { Store(0x22, kJmpRel8), Store(0xDF, kJmpRel8) };
    inline constexpr LauncherPatch kWrites2[] = { Store(0, kNop2), Store(5, kNop2) };
    inline constexpr LauncherPatch kWrites3[] = { Store(0, kNop2) };
    inline constexpr LauncherPatch kWrites4[] = { Store(3, kNop6) };
    inline constexpr LauncherPatch kWrites5[] = { Store(3, kNop6) };
    inline constexpr LauncherPatch kWrites6[] = { Store(0, kNop6) };
    inline constexpr LauncherPatch kWrites7[] = { Store(3, kXorEax), Store(10, kNopMovMoffsEax) };
    inline constexpr LauncherPatch kWrites8[] = { Fill(1, 447) };
    inline constexpr LauncherPatch kWrites9[] = {
        Store(0, kXorEax),
        Store(2, kJmp),
        JumpTarget(3, 1),
    };
    inline constexpr LauncherPatch kWrites10[] = { Store(0, kNopMovMoffsEax) };
    inline constexpr LauncherPatch kWrites11[] = { Store(0, kXorEax), Store(2, kNop4) };
    inline constexpr LauncherPatch kWrites12[] = { Store(0, kNop3), Fill(6, 11) };
    inline constexpr LauncherPatch kWrites13[] = { Store(0, kNop3), Fill(6, 11) };
    inline constexpr LauncherPatch kWrites14[] = { Store(0, kNop11) };
    inline constexpr LauncherPatch kWrites15[] = { Fill(0, 32) };
    inline constexpr LauncherPatch kWrites16[] = { Fill(0, 25) };
    inline constexpr LauncherPatch kWrites17[] = { Store(0, kNop8) };
    inline constexpr LauncherPatch kWrites18[] = { Store(0, kNop8) };
    inline constexpr LauncherPatch kWrites19[] = { Store(0, kNop14) };
    inline constexpr LauncherPatch kWrites20[] = { Store(0, kRet) };
    inline constexpr LauncherPatch kWrites21[] = { Fill(0, 16) };
    inline constexpr LauncherPatch kWrites22[] = { Store(0, kNop14) };
    inline constexpr LauncherPatch kWrites23[] = { Store(0, kXorEaxRetNop) };
    inline constexpr LauncherPatch kWrites24[] = { Store(0, kXorEaxRetNop) };

    inline constexpr LauncherEntry kEntries[] = {
        // 0: `test bl,bl; jne` launcher check; both branch bytes forced to `jmp`
        { "84 DB 0F 85 43 01 00 00 8D 4C 24 28 8B C1", kWrites0 },
        // 1: the EFC check's entry and the target of entry 9's jump; no edits
        { "68 ? ? ? ? 89 35 ? ? ? ? E8 ? ? ? ? 83 C4 04 84 C0 0F 84", {} },
        // 2: RGSC initialization check [v1000 - v1001]: NOP both `jz`
        { "74 EC 38 5E 06 74 E7 68", kWrites2 },
        // 3: RGSC initialization check [v1002 - v1004]: NOP the leading `jz`
        { "74 C4 8B 07 3B 46 18 75 BD 68", kWrites3 },
        // 4: last RGSC init check [v1002 - v1004]: NOP the `jne rel32`
        { "3B 56 18 0F 85", kWrites4 },
        // 5: last RGSC init check [v1005]
        { "3B 4E 18 0F 85", kWrites5 },
        // 6: last RGSC init check [v1006 - v1008]
        { "0F 85 ? ? ? ? 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC C6", kWrites6 },
        // 7: RGSC initialization check [v1002 - v1004]: `xor eax,eax` + `nop; mov`
        { "3B 46 18 75 BD", kWrites7 },
        // 8: EFC20 [v1003 - v1004]: wipe 0x1BF bytes after the prologue
        { "57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24", kWrites8 },
        // 9: skip RGSC connect and EFC checks [v1005 - v1008]: xor + jmp entry 1
        { "8B 56 1C 3B 56 20 ? ? 6A 00 6A 00", kWrites9 },
        // 10: NOP; MOV [g_rgsc], eax [v1000 - v1008]
        { "89 35 ? ? ? ? E8 ? ? ? ? 83 C4 04 84 C0", kWrites10 },
        // 11: skip missing tests [v1005]: `mov edx,[eax+2C]; cmp edx,[eax+30]` -> 0
        { "8B 50 2C 3B 50 30", kWrites11 },
        // 12: skip missing tests [v1005]: `mov ecx,[eax+2C]; sub esp,0x78`
        { "8B 48 2C 83 EC 78", kWrites12 },
        // 13: skip missing tests [v1005]: the same with `sub esp,0x74`
        { "8B 48 2C 83 EC 74", kWrites13 },
        // 14: skip missing tests [v1005]: `... cmp ecx,[eax+30]; jz +3`
        { "8B 48 2C 3B 48 30 74 03", kWrites14 },
        // 15: skip missing tests [v1005]: the same compare followed by `push ebx`
        { "8B 48 2C 3B 48 30 53", kWrites15 },
        // 16: skip missing tests [v1005]: the same compare followed by `jz +0x11`
        { "8B 48 2C 3B 48 30 74 11", kWrites16 },
        // 17: skip missing tests [v1005]: `... jne -0x24; lea edx,[esp+18]`
        { "8B 48 2C 3B 48 30 75 DC 8D 54 24 18", kWrites17 },
        // 18: skip missing tests [v1005]: `... jne -0x24; lea edx,[esp+1C]`
        { "8B 48 2C 3B 48 30 75 DC 8D 54 24 1C", kWrites18 },
        // 19: skip missing tests [v1005]: `mov ecx,[imm32]; mov edx,[ecx+2C]`
        { "8B 0D ? ? ? ? 8B 51 2C", kWrites19 },
        // 20: skip missing tests [v1003 - v1005]: the whole function becomes `ret`
        { "51 53 55 56 57 8B 3D ? ? ? ? 8B 47 2C", kWrites20 },
        // 21: skip missing tests [v1005 - v1008]: `cmp eax,edi; jne rel32; test eax`
        { "3B C7 0F 85 ? ? ? ? 85 C0", kWrites21 },
        // 22: skip missing tests [v1005 - v1008]: `jne; je; call; test edx`
        { "0F 85 ? ? ? ? 85 C0 0F 84 ? ? ? ? E8 ? ? ? ? 85 D2", kWrites22 },
        // 23: skip missing tests [v1006 - v1007]: `sub esp,0x24; mov eax,[imm32]`
        { "83 EC 24 A1 ? ? ? ? 8B 48 14 53 55 56 57", kWrites23 },
        // 24: skip missing tests [v1006 - v1007]: `sub esp,8; mov ecx,[imm32]`
        { "83 EC 08 8B 0D ? ? ? ? 8B 51 14 8D 04 24 50 6A 00 52", kWrites24 },
    };

    static_assert(std::size(kEntries) == 25);

    // The two collector rules: the first two matches of one pattern and the last
    // two of the other, each widened by the same "mov al,1; ret; nop" stub. The
    // recovered code tests only that the pattern matched at all, so the arm keeps
    // that shape; a pattern that matches has at least two hits on every supported
    // image (1.0.0.0 has none for the first, so that collector is skipped).
    struct LauncherCollector
    {
        std::string_view signature;
        bool firstTwo;
        std::span<const std::byte> stub;
    };

    inline constexpr LauncherCollector kCollectors[] = {
        { "A1 ? ? ? ? 85 C0 8B 0D ? ? ? ? 75", true, kCollectorStub },
        { "33 C0 83 3D ? ? ? ? 01 0F 94 C0 C3", false, kCollectorStub },
    };

    // A reimplemented hook body: one of the eight the original installs branches
    // into its own module for.
    enum class SkipHook : uint8_t
    {
        dfa_init,           // 0x10004780: return 1
        create_file,        // 0x10004820: CreateFileA through the captured slot
        close_handle,       // 0x10004790: CloseHandle behind an exception guard
        set_file_pointer,   // 0x10004760
        get_file_size_ex,   // 0x10004720
        read_file,          // 0x10004740
        rom_reset,          // 0x10004570: start the WM_ROM service and reset the object
        rom_set,            // 0x10004610: *(u32*)(object + 0x20) = *(u32*)(object + 0x1C)
    };

    // One E8/E9 branch an installer writes: the signature that finds the site, the
    // match-relative offset, the opcode and the body.
    struct LauncherInstall
    {
        std::string_view signature;
        int32_t offset;
        std::byte opcode;
        SkipHook body;
    };

    // The DFA installs, from the installer at 0x10004850. `read_file` hooks the
    // enclosing prologue, 165 bytes before the match.
    inline constexpr LauncherInstall kDfaInstalls[] = {
        { "E8 ? ? ? ? 83 C4 0C 83 F8 01 74 11 6A 00 6A 00", 0, Bytes::Call, SkipHook::dfa_init },
        { "55 8B EC 6A 00 E8", 0, LongJmp, SkipHook::create_file },
        { "6A 00 E8 ? ? ? ? 83 F8 01 59 75 17 FF 15 ? ? ? ? 50 B9 ? ? ? ? E8 ? ? ? ? 8B 40 08 "
          "FF 60 0C 33 C0 C3", 0, LongJmp, SkipHook::close_handle },
        { "6A 00 E8 ? ? ? ? 83 F8 01 59 75 17 FF 15 ? ? ? ? 50 B9 ? ? ? ? E8 ? ? ? ? 8B 40 08 "
          "FF 60 ? 83 C8 FF C3", 0, LongJmp, SkipHook::set_file_pointer },
        { "56 8B 74 24 0C 8D 46 04 50 FF 74 24 0C E8 ? ? ? ? 59 59 33 C9 83 F8 FF 0F 95 C1 89 "
          "06 5E 8B C1 C3", 0, LongJmp, SkipHook::get_file_size_ex },
        { "FF 50 60 83 C4 14 39 75 F8 89 45 FC", -165, LongJmp, SkipHook::read_file },
    };

    // The ROM installs, from the installer at 0x10004620.
    inline constexpr LauncherInstall kRomInstalls[] = {
        { "89 46 3C 89 4E 40 E8", 6, Bytes::Call, SkipHook::rom_reset },
        { "0F 85 ? ? ? ? 68 03 01 00 00 68 ? ? ? ? 68 04 01 00 00", -0x43, LongJmp,
          SkipHook::rom_set },
    };

    // The DFA capture: `FF 15 <slot>` is a call through the CreateFile IAT slot,
    // and the slot's VA is the operand at +2.
    inline constexpr std::string_view kDfaCapture = "FF 15 ? ? ? ? 8B F0 83 FE FF 74 24";
    inline constexpr int32_t kDfaCaptureOffset = 2;

    // The collector tail of 0x10004850: the two collected sites jump to another
    // pattern's single match.
    inline constexpr std::string_view kDfaCollector =
        "A1 ? ? ? ? B9 A7 FA DC 5C F7 E1 8B 0D ? ? ? ? 33 F6";
    inline constexpr std::string_view kDfaCollectedTargets[] = {
        "80 BC 24 ? ? ? ? ? 6A 00",
        "6A 00 68 80 00 00 00 6A 03 6A 00 6A 01 68 00 00 00 80 57",
    };

    // ---- the WM_ROM service ------------------------------------------------
    //
    // The original's window proc is 0x100041A0, its class creator 0x10004260 and
    // its service thread 0x10004300: register "MessageWindow" (falling back to
    // "MessageWindow2"), create a message-only window of that class, publish the
    // handle from WM_NCCREATE and pump messages until WM_DESTROY quits.
    constexpr UINT kRomMessage = 0xAB52;  // WM_ROM, 43858
    constexpr char kRomClass[] = "MessageWindow";
    constexpr char kRomClassFallback[] = "MessageWindow2";

    // The table the service hands the game: xmmword_10104050..0x10104080.
    constexpr uint32_t kRomTable[16] = {
        8, 11, 241, 1, 240, 2, 7, 12, 3, 243, 9, 6, 10, 5, 4, 242,
    };

    // The WM_ROM message carries this word array: [4] is the action and [5]/[6]
    // its operands.
    struct RomPayload
    {
        uint32_t reserved[4];
        uint32_t action;
        uint32_t arg0;
        uint32_t arg1;
    };
    static_assert(sizeof(RomPayload) == 7 * sizeof(uint32_t));

    // The actions the window proc implements; the values are the original's.
    enum class RomAction : uint32_t
    {
        copyTable = 0x12,  // copy kRomTable to the pointer in arg1
        add = 0x33,        // arg0 + arg1
        set = 0x64,        // *(u32*)arg0 = 1
    };

    // The object rom_reset clears is a 19-dword record: the window handle at [1],
    // the message id at [4], and [0], [2], [3], [5..18] zeroed before both are
    // written. The body is assembly, so the layout lives in its comment.

    // dword_101A8304 (written from WM_NCCREATE), byte_1015AD9B (window created),
    // byte_10158134 (service not started yet), and the std::thread object the
    // original keeps at 0x101806F4.
    std::atomic<uint32_t> gRomWindow{ 0 };
    std::atomic<bool> gRomWindowCreated{ false };
    bool gRomServicePending = true;
    std::thread gRomServiceThread;

    LRESULT CALLBACK RomWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
    {
        switch (message)
        {
        case WM_NCCREATE:
            gRomWindow.store(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(window)));
            gRomWindowCreated.store(true);
            return DefWindowProcA(window, message, wparam, lparam);
        case WM_DESTROY:
            gRomWindow.store(0);
            PostQuitMessage(0);
            return 0;
        case kRomMessage:
        {
            const auto* payload = reinterpret_cast<const RomPayload*>(lparam);
            switch (static_cast<RomAction>(payload->action))
            {
            case RomAction::copyTable:
                std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(payload->arg1)),
                    kRomTable, sizeof(kRomTable));
                return 0;
            case RomAction::add:
                return static_cast<LRESULT>(payload->arg0 + payload->arg1);
            case RomAction::set:
                *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(payload->arg0)) = 1;
                return 0;
            default:
                return 0;
            }
        }
        default:
            return DefWindowProcA(window, message, wparam, lparam);
        }
    }

    // 0x10004260: register the class and create its message-only window. The
    // atom is the return value; a failed window creation is not checked, exactly
    // as the original leaves it (the starter then waits for a WM_NCCREATE that
    // never arrives).
    ATOM CreateRomWindow(HINSTANCE instance, LPCSTR name)
    {
        WNDCLASSEXA windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &RomWindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = name;

        const ATOM atom = RegisterClassExA(&windowClass);
        if (atom == 0)
            return 0;

        CreateWindowExA(0, reinterpret_cast<LPCSTR>(MAKEINTATOM(atom)), "", 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, instance, nullptr);
        return atom;
    }

    // 0x10004300: the service thread.
    void RomService()
    {
        const auto instance = GetModuleHandleA(nullptr);
        ATOM atom = CreateRomWindow(instance, kRomClass);
        if (atom == 0)
        {
            atom = CreateRomWindow(instance, kRomClassFallback);
            if (atom == 0)
            {
                // No class: the starter must still be released.
                gRomWindowCreated.store(true);
                return;
            }
        }

        MSG message;
        while (GetMessageA(&message, nullptr, 0, 0) != 0)
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        UnregisterClassA(reinterpret_cast<LPCSTR>(MAKEINTATOM(atom)), instance);
    }

    // 0x100043D0: start the service once and wait until the window exists. The
    // original spins on Sleep(0) with no bound: if the window is never created the
    // game hangs here, and that fault is preserved.
    void StartRomService()
    {
        if (!gRomServicePending)
            return;
        gRomServicePending = false;
        gRomWindowCreated.store(false);
        gRomServiceThread = std::thread(RomService);
        while (!gRomWindowCreated.load())
            Sleep(0);
    }

    // ---- hook bodies -------------------------------------------------------

    // 0x10004790: CloseHandle behind an EH4 frame. The original's scope table
    // (0x10153FC0) names the filter at 0x100047EC (`83 C8 FF; C3` = `or eax,-1;
    // ret`, i.e. EXCEPTION_CONTINUE_EXECUTION) and the handler at 0x100047F0
    // (`mov eax,1` after the unwind, i.e. `return TRUE`). The filter's disposition
    // is a resumption, so a fault inside the guarded call is retried, not answered:
    // the handler is never entered. The rebuild must keep that shape -- a
    // `__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }` here would catch the
    // fault and answer, a fault-parity bug (docs/rebuilt.md §2l).
    BOOL __cdecl CloseHandleBody(HANDLE handle)
    {
        __try
        {
            return CloseHandle(handle);
        }
        __except (EXCEPTION_CONTINUE_EXECUTION)
        {
            return TRUE;
        }
    }
}

// LaunchWithPlayGTAIV: the two launch slots carry `LaunchGTAIV.exe\0`; the mod
// rewrites both to `PlayGTAIV.exe\0e\0` so the game relaunches through the DRM
// bypass. The handler 0x100441B0 gates the whole switch on sub_10003A10, a
// Toolhelp32 walk for a live `PlayGTAIV.exe` process, so the rewrite happens only
// when the game was started through the launcher (a direct GTAIV.exe start leaves
// both slots alone). The scan is wildcard-free and hits exactly the two slots on
// the five builds the original gated, and unrelated slots on the builds it
// excluded - so the build gate is load-bearing too, not decoration.
namespace
{
    inline constexpr std::array<std::byte, 16> kLauncherName = {
        std::byte{ 'P' }, std::byte{ 'l' }, std::byte{ 'a' }, std::byte{ 'y' },
        std::byte{ 'G' }, std::byte{ 'T' }, std::byte{ 'A' }, std::byte{ 'I' },
        std::byte{ 'V' }, std::byte{ '.' }, std::byte{ 'e' }, std::byte{ 'x' },
        std::byte{ 'e' }, Bytes::Zero, std::byte{ 'e' }, Bytes::Zero,
    };

    // "LaunchGTAIV.exe\0", the text both slots currently hold.
    inline constexpr std::string_view kLauncherSlot = "4C 61 75 6E 63 68 47 54 41 49 56 2E 65 78 65 00";

    // sub_10003A10: `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)`,
    // `Process32First`, then `strcmp(pe.szExeFile, "PlayGTAIV.exe")` over
    // `Process32Next`; the handler writes only when the walk finds the launcher
    // alive and returns its pid. The `cmp esi,1` test at 0x10055351 gates the
    // write set on that answer. The tree builds with UNICODE, so the walk is the
    // W form of the same record and the comparison is `wcscmp`.
    bool PlayGtaivLauncherRunning()
    {
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return false;

        PROCESSENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        bool found = false;
        if (Process32First(snapshot, &entry) != FALSE)
        {
            do
            {
                if (wcscmp(entry.szExeFile, L"PlayGTAIV.exe") == 0)
                {
                    found = true;
                    break;
                }
            } while (Process32Next(snapshot, &entry) != FALSE);
        }
        CloseHandle(snapshot);
        return found;
    }

    void InstallLaunchWithPlayGTAIV()
    {
        if (!Loader::Enabled("LaunchWithPlayGTAIV"))
            return;

        // Two gates, both the original's: the module guard keeps the offline dump
        // (which attaches an image, not a loaded module) from depending on whether
        // this host happens to be running the game, and the process walk is the
        // handler's own sub_10003A10.
        if (Game::Get().Module() == nullptr)
            return;

        if (!PlayGtaivLauncherRunning())
            return;

        if (!BuildIs(Loader::Current(),
                { Build::v1000, Build::v1040, Build::v1060, Build::v1070, Build::v1080 }))
            return;

        auto pattern = find_pattern(kLauncherSlot);
        for (size_t index = 0; index < pattern.size(); ++index)
        {
            auto* slot = pattern.get(index).get<std::byte>();
            injector::WriteMemoryRaw(reinterpret_cast<uintptr_t>(slot),
                const_cast<std::byte*>(kLauncherName.data()), kLauncherName.size(), true);
        }
    }
}

namespace
{
    uintptr_t BodyAddress(SkipHook hook)
    {
        switch (hook)
        {
        case SkipHook::dfa_init: return reinterpret_cast<uintptr_t>(&DfaInitBody);
        case SkipHook::create_file: return reinterpret_cast<uintptr_t>(&DfaCreateFileBody);
        case SkipHook::close_handle: return reinterpret_cast<uintptr_t>(&CloseHandleBody);
        case SkipHook::set_file_pointer: return reinterpret_cast<uintptr_t>(&DfaSetFilePointerBody);
        case SkipHook::get_file_size_ex: return reinterpret_cast<uintptr_t>(&DfaGetFileSizeExBody);
        case SkipHook::read_file: return reinterpret_cast<uintptr_t>(&DfaReadFileBody);
        case SkipHook::rom_reset: return reinterpret_cast<uintptr_t>(&RomResetBody);
        case SkipHook::rom_set: return reinterpret_cast<uintptr_t>(&RomSetBody);
        }
        return 0;
    }

    // The DFA installer's guards (0x1000487B..0x100048D5): the XLivelessAddon
    // companion suppresses the stage, a loaded GTAC.dll suppresses it, and the four
    // EFLC builds are excluded. The caller adds the installer's own one-shot.
    bool DfaSelected()
    {
        if (GetModuleHandleA("XLivelessAddon.asi") != nullptr)
            return false;
        if (GetModuleHandleA("GTAC.dll") != nullptr)
            return false;
        return !BuildIs(Loader::Current(),
            { Build::v1100, Build::v1110, Build::v1120, Build::v1130 });
    }

    // One E8/E9 per install row; a missing signature is the original's clean skip.
    void InstallBranches(std::span<const LauncherInstall> installs)
    {
        for (const auto& install : installs)
        {
            auto pattern = find_pattern(install.signature);
            if (pattern.empty())
                continue;

            auto* site = pattern.get_first<std::byte>(install.offset);
            injector::WriteMemory<std::byte>(site, install.opcode, true);
            const auto displacement = static_cast<int32_t>(
                BodyAddress(install.body) - (reinterpret_cast<uintptr_t>(site) + 5));
            injector::WriteMemory<int32_t>(site + 1, displacement, true);
        }
    }

    // The IAT capture: read before the create_file hook is written, which is the
    // order the original keeps (capture at 0x100048EE, first install at 0x10004901).
    void PublishDfaCapture()
    {
        if (!Loader::Enabled("SkipLauncher") || !DfaSelected())
            return;

        auto pattern = find_pattern(kDfaCapture);
        if (pattern.empty())
            return;
        zpatch_dfa_create_file_slot = *pattern.get_first<uint32_t>(kDfaCaptureOffset);
    }

    // The five DFA bodies' operands: the four `call ds:[…]` slots hold the same
    // kernel32 functions the original imported (the static CRT has no IAT), and
    // the create body's capture slot stays zero until the publish tier records the
    // game's own IAT address. One function, so the shipped publish step and the
    // test seam cannot drift: the bodies are reached only from the game's calls,
    // but the DFA stage installs them from the install tier and nothing may read an
    // empty slot.
    void PublishDfaForwarders()
    {
        zpatch_dfa_create_file = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&CreateFileA));
        zpatch_dfa_set_file_pointer =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&SetFilePointer));
        zpatch_dfa_get_file_size_ex =
            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&GetFileSizeEx));
        zpatch_dfa_read_file = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&ReadFile));
    }

    // ---- the stages --------------------------------------------------------

    // The main table. One entry can install a jump to another entry's match, so
    // every match is kept while the table runs; the original skips such an entry
    // whole when its target was absent, because the xor and the jump share one
    // guard.
    void ApplyMainTable()
    {
        std::array<const std::byte*, std::size(kEntries)> matches{};
        for (size_t index = 0; index < std::size(kEntries); ++index)
        {
            const auto& entry = kEntries[index];
            auto pattern = find_pattern(entry.signature);
            if (pattern.empty())
                continue;

            auto* match = pattern.get_first<std::byte>(0);
            matches[index] = match;

            bool targetAbsent = false;
            for (const auto& patch : entry.patches)
            {
                if (patch.kind != LauncherPatch::Kind::target)
                    continue;
                if (patch.targetEntry >= matches.size() || matches[patch.targetEntry] == nullptr)
                    targetAbsent = true;
            }
            if (targetAbsent)
                continue;

            for (const auto& patch : entry.patches)
            {
                auto* site = match + patch.offset;
                switch (patch.kind)
                {
                case LauncherPatch::Kind::store:
                    injector::WriteMemoryRaw(reinterpret_cast<uintptr_t>(site),
                        const_cast<std::byte*>(patch.bytes.data()), patch.bytes.size(), true);
                    break;
                case LauncherPatch::Kind::fill:
                    injector::MakeNOP(site, patch.count, true);
                    break;
                case LauncherPatch::Kind::target:
                {
                    // The E9 opcode sits one byte before the recorded site, so the
                    // displacement is measured from the end of that 5-byte branch.
                    auto* target = const_cast<std::byte*>(matches[patch.targetEntry]);
                    const auto displacement = static_cast<int32_t>(
                        reinterpret_cast<uintptr_t>(target) - (reinterpret_cast<uintptr_t>(site) + 4));
                    injector::WriteMemory<int32_t>(site, displacement, true);
                    break;
                }
                }
            }
        }
    }

    // The two collectors: the first two matches of the first pattern, the last
    // two of the second, each with the `mov al,1; ret; nop` stub. The recovered
    // code tests only that the pattern matched at all and then indexes the pair,
    // so the arm keeps that shape; a pattern that matches has at least two hits on
    // every supported image (measured in docs/parity.md).
    void ApplyCollectors()
    {
        for (const auto& collector : kCollectors)
        {
            auto pattern = find_pattern<0xFFFFFFFFu>(collector.signature);
            const size_t hits = pattern.size();
            if (hits == 0)
                continue;

            const size_t first = collector.firstTwo ? 0 : hits - 2;
            const size_t last = collector.firstTwo ? 1 : hits - 1;
            for (size_t hit = first; hit <= last; ++hit)
                injector::WriteMemoryRaw(
                    reinterpret_cast<uintptr_t>(pattern.get(hit).get<std::byte>()),
                    const_cast<std::byte*>(collector.stub.data()), collector.stub.size(), true);
        }
    }

    // The DFA stage: six installs and the two collected jumps to other patterns'
    // matches. The original's one-shot byte is set before the first scan, so a
    // second call (MiscFixes' 0x10024018) does nothing even if a scan failed.
    void InstallDfa()
    {
        static bool done = false;
        if (!DfaSelected() || done)
            return;
        done = true;

        InstallBranches(kDfaInstalls);

        auto collected = find_pattern<0xFFFFFFFFu>(kDfaCollector);
        if (collected.empty())
            return;

        for (size_t target = 0; target < std::size(kDfaCollectedTargets); ++target)
        {
            // The original indexes the collector's first two matches after a
            // non-empty check only; both are present on every image where the
            // collector matches at all.
            auto* site = collected.get(target).get<std::byte>();
            auto destination = find_pattern(kDfaCollectedTargets[target]);
            if (destination.empty())
                continue;

            injector::WriteMemory<std::byte>(site, LongJmp, true);
            const auto displacement = static_cast<int32_t>(
                reinterpret_cast<uintptr_t>(destination.get_first<std::byte>(0)) -
                (reinterpret_cast<uintptr_t>(site) + 5));
            injector::WriteMemory<int32_t>(site + 1, displacement, true);
        }
    }

    // The ROM stage: two installs, once. It runs even when the main table is
    // skipped (the original's handler jumps straight to this call).
    void InstallRom()
    {
        static bool done = false;
        if (done)
            return;
        done = true;

        InstallBranches(kRomInstalls);
    }

    // The original's one handler: the main table and the DFA stage are skipped
    // when the XLivelessAddon companion is loaded; the ROM stage always runs.
    void InstallSkipLauncher()
    {
        if (!Loader::Enabled("SkipLauncher"))
            return;

        if (GetModuleHandleA("XLivelessAddon.asi") == nullptr)
        {
            ApplyMainTable();
            ApplyCollectors();
            InstallDfa();
        }
        InstallRom();
    }
}

// Registered like every other arm: the install tier owns the gate.
export void RegisterLauncher()
{
    // The transcribed forwarders' targets, filled before any tier runs. The capture
    // slot (zpatch_dfa_create_file_slot) is the publish tier's, not this one's.
    PublishDfaForwarders();

    Loader::PublishEvent() += [] { PublishDfaCapture(); };
    Loader::InstallEvent() += []
    {
        InstallLaunchWithPlayGTAIV();
        InstallSkipLauncher();
    };

    // The hook bodies carry their recovered VAs so the dump can express every
    // branch in the original's coordinates.
    Bodies::Register(reinterpret_cast<uintptr_t>(&DfaInitBody), 0x10004780u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&DfaCreateFileBody), 0x10004820u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&CloseHandleBody), 0x10004790u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&DfaSetFilePointerBody), 0x10004760u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&DfaGetFileSizeExBody), 0x10004720u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&DfaReadFileBody), 0x10004740u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&RomResetBody), 0x10004570u);
    Bodies::Register(reinterpret_cast<uintptr_t>(&RomSetBody), 0x10004610u);
}

// ---------------------------------------------------------------------------
// The test-only seam over the five DFA file-API bodies (tests/dfa_close.cpp,
// ctest `zp_dfa_close`). The four transcribed forwarders and the rebuilt
// close-handle body are the callable part of the launcher stage: the game reaches
// them only through the `E9` installs 0x10004850 writes at its own call sites, so
// nothing offline can make them run. The seam hands a test exactly those five
// addresses and the ABIs the originals have; it changes no body and adds no
// behaviour. It publishes the four forwarder slots because the bodies read them
// (the create body's fallback would otherwise `call` through zero), which is the
// same store `RegisterLauncher` makes before any tier.
//
// Nothing here reaches the shipped image: the entries have no caller inside the ASI
// and no PE export, so `/OPT:REF` drops them and the linker map gains no symbol,
// while the test links the same object file and calls them.
export namespace Launcher
{
    // The five entries, in the originals' ABIs (all `__cdecl`, the caller cleans):
    //   create_file       0x10004820  7 arguments, CreateFileA's
    //   read_file         0x10004740  5 arguments, ReadFile's
    //   set_file_pointer  0x10004760  4 arguments, SetFilePointer's
    //   get_file_size_ex  0x10004720  2 arguments, GetFileSizeEx's
    //   close_handle      0x10004790  1 argument, the rebuilt CloseHandle behind EH4
    struct DfaBodies
    {
        HANDLE(__cdecl* create_file)(LPCSTR file_name, DWORD access, DWORD share,
            LPSECURITY_ATTRIBUTES security, DWORD disposition, DWORD flags, HANDLE template_file);
        BOOL(__cdecl* read_file)(HANDLE file, LPVOID buffer, DWORD count, LPDWORD read,
            LPOVERLAPPED overlapped);
        DWORD(__cdecl* set_file_pointer)(HANDLE file, LONG distance, LONG* distance_high,
            DWORD method);
        BOOL(__cdecl* get_file_size_ex)(HANDLE file, LARGE_INTEGER* size);
        BOOL(__cdecl* close_handle)(HANDLE handle);
    };

    // The five bodies, with the four forwarder slots published.
    DfaBodies DfaBodiesForTest();
}

Launcher::DfaBodies Launcher::DfaBodiesForTest()
{
    PublishDfaForwarders();
    return {
        reinterpret_cast<HANDLE(__cdecl*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD,
            DWORD, HANDLE)>(&DfaCreateFileBody),
        reinterpret_cast<BOOL(__cdecl*)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED)>(
            &DfaReadFileBody),
        reinterpret_cast<DWORD(__cdecl*)(HANDLE, LONG, LONG*, DWORD)>(&DfaSetFilePointerBody),
        reinterpret_cast<BOOL(__cdecl*)(HANDLE, LARGE_INTEGER*)>(&DfaGetFileSizeExBody),
        reinterpret_cast<BOOL(__cdecl*)(HANDLE)>(&CloseHandleBody),
    };
}

// ---------------------------------------------------------------------------
// Transcribed bodies. Each is the original's instruction stream, kept as asm
// because that is what it is; the code above is what installs it.
// ---------------------------------------------------------------------------

// The transcribed bodies are naked assembly, so they live at module scope: MSVC
// writes them to the BMI from anywhere, but only a module-scope `static` keeps the
// symbol the body manifest and the linker map name.

// 0x10004780, 2 instructions: the DFA init call reports success.
static __declspec(naked) void DfaInitBody()
{
    __asm
    {
        mov eax, 1
        ret
    }
}

// sub_10004820, recovered VA 0x10004820 (19 instructions): the DFA create-file
// body. It pushes the caller's seven arguments first, then reads the slot address
// the DFA capture recorded: a non-zero address means the game's own CreateFile IAT
// entry, which the body dereferences and calls (a null target faults, as in the
// original); zero falls back to the import. __cdecl like the original -- plain
// `ret`, the caller pops the seven arguments.
static __declspec(naked) void DfaCreateFileBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push DWORD PTR [ebp + 20h]
        mov eax, DWORD PTR [zpatch_dfa_create_file_slot]
        push DWORD PTR [ebp + 1Ch]
        push DWORD PTR [ebp + 18h]
        push DWORD PTR [ebp + 14h]
        push DWORD PTR [ebp + 10h]
        push DWORD PTR [ebp + 0Ch]
        push DWORD PTR [ebp + 8]
        test eax, eax
        je dfa_create_file_fallback
        mov eax, DWORD PTR [eax]
        call eax
        pop ebp
        ret
    dfa_create_file_fallback:
        call DWORD PTR [zpatch_dfa_create_file]
        pop ebp
        ret
    }
}

// 0x10004570, 28 instructions: start the WM_ROM service, clear the object and
// publish the window handle and the message id. The object is a 19-dword record:
// [0], [2], [3] and [5..18] are zeroed, [1] receives the window handle from the
// service's own word (which is also the return value) and [4] the message id.
static __declspec(naked) void RomResetBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push esi
        mov esi, [ebp + 8]
        call StartRomService
        mov dword ptr [esi], 0
        mov dword ptr [esi + 8], 0
        mov dword ptr [esi + 0xc], 0
        mov dword ptr [esi + 0x14], 0
        mov dword ptr [esi + 0x18], 0
        mov dword ptr [esi + 0x1c], 0
        mov dword ptr [esi + 0x20], 0
        mov dword ptr [esi + 0x24], 0
        mov dword ptr [esi + 0x28], 0
        mov dword ptr [esi + 0x2c], 0
        mov dword ptr [esi + 0x30], 0
        mov dword ptr [esi + 0x34], 0
        mov dword ptr [esi + 0x38], 0
        mov dword ptr [esi + 0x3c], 0
        mov dword ptr [esi + 0x40], 0
        mov dword ptr [esi + 0x44], 0
        mov dword ptr [esi + 0x48], 0
        mov eax, dword ptr [gRomWindow]
        mov dword ptr [esi + 4], eax
        mov dword ptr [esi + 0x10], 0xAB52
        pop esi
        pop ebp
        ret
    }
}

// sub_10004760, recovered VA 0x10004760 (9 instructions): the DFA SetFilePointer
// forwarder. __cdecl like the original -- plain `ret`, the caller pops the four
// arguments -- entered by the `jmp` the DFA install writes at game 0x917E92. The
// original's `call ds:[0x10063030]` is the kernel32 import through its own IAT; the
// module slot holds the same function.
static __declspec(naked) void DfaSetFilePointerBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push DWORD PTR [ebp + 14h]
        push DWORD PTR [ebp + 10h]
        push DWORD PTR [ebp + 0Ch]
        push DWORD PTR [ebp + 8]
        call DWORD PTR [zpatch_dfa_set_file_pointer]
        pop ebp
        ret
    }
}

// sub_10004720, recovered VA 0x10004720 (7 instructions): the DFA GetFileSizeEx
// forwarder, installed at game 0x917EE1.
static __declspec(naked) void DfaGetFileSizeExBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push DWORD PTR [ebp + 0Ch]
        push DWORD PTR [ebp + 8]
        call DWORD PTR [zpatch_dfa_get_file_size_ex]
        pop ebp
        ret
    }
}

// sub_10004740, recovered VA 0x10004740 (10 instructions): the DFA ReadFile
// forwarder, installed at game 0x917D38.
static __declspec(naked) void DfaReadFileBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        push DWORD PTR [ebp + 18h]
        push DWORD PTR [ebp + 14h]
        push DWORD PTR [ebp + 10h]
        push DWORD PTR [ebp + 0Ch]
        push DWORD PTR [ebp + 8]
        call DWORD PTR [zpatch_dfa_read_file]
        pop ebp
        ret
    }
}

// 0x10004610, 4 instructions: the object arrives in ESI (the patched site keeps it
// there) and the site pushed two arguments the callee must pop, so this is
// reproduced instruction for instruction.
static __declspec(naked) void RomSetBody()
{
    __asm
    {
        mov ecx, [esi + 0x1c]
        mov [esi + 0x20], ecx
        mov al, 1
        ret 8
    }
}
