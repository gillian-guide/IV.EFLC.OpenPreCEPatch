#include <common.hxx>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import launcher;

// The DFA close-handle body (0x10004790) and its four transcribed siblings, asserted
// by behaviour instead of by their disassembly. The launcher stage installs all five
// as `E9` branches at the game's own call sites (`0x10004850`), so nothing offline
// can make them run; `Launcher::DfaBodiesForTest` (source/launcher.ixx) is the
// test-only seam that hands this test the same five entry addresses and publishes
// the four forwarder slots the bodies read.
//
// What the disassembly says, because every assertion below is about one of these
// facts:
//
//   0x10004790 (45 instructions, 0x81 bytes, EH4 frame)
//     10004793  push -2                       ; try level -2
//     10004795  push 0x10153FC0               ; the scope table
//     1000479A  push 0x1005FE4C               ; __except_handler4
//     1000479F  mov eax,fs:[0] / push eax     ; the thread's exception chain
//     100047AC  mov eax,[0x1015800C]          ; __security_cookie
//     100047C3  mov [ebp-4],0                 ; state 0: the guarded region begins
//     100047CA  push DWORD PTR [ebp+8]        ; the handle, unexamined
//     100047CD  call DWORD PTR ds:[0x10063058] ; KERNEL32!CloseHandle
//     100047D3  mov [ebp-4],-2                ; state -2: the region is over
//     100047E4..100047EB  pop ecx/edi/esi/ebx; mov esp,ebp; pop ebp; ret
//     100047EC  83 C8 FF  C3                  ; the FILTER: or eax,-1; ret
//     100047F0  8B 65 E8  ...  B8 01 00 00 00 ; the HANDLER: unwind, then mov eax,1
//
// The scope table at 0x10153FC0 names them: its one record is {EnclosingLevel -2,
// FilterFunc 0x100047EC, HandlerFunc 0x100047F0}. So the filter answers
// EXCEPTION_CONTINUE_EXECUTION (-1), which is a *resumption*: a fault in the guarded
// call is retried, the handler's `return TRUE` is never entered, and the wrapper has
// no answer for a faulting call at all. That is the original's behaviour and the
// rebuild reproduces it byte for byte (docs/rebuilt.md §2l); the child below pins it.
//
// There is no argument guard anywhere in the body: the only instructions between the
// frame's registration and the call are `mov [ebp-4],0` and `push [ebp+8]`. A null,
// already-closed or never-opened handle therefore reaches CloseHandle unchanged and
// the wrapper answers whatever CloseHandle answers -- FALSE with
// ERROR_INVALID_HANDLE (6) -- rather than being a no-op. The same absence of state
// decides the double close: the body keeps nothing between calls, so the second close
// re-enters CloseHandle with the stale value and gets the same FALSE/6.
//
// What the frame protects, and what is reachable: the guarded region is exactly
// `push [ebp+8]; call ds:[CloseHandle]`. Its only faulting operation is the call, and
// no handle value can make CloseHandle fault -- it validates and returns FALSE. The
// fault path is therefore unreachable through the wrapper's own arguments, and the
// reachable surface is what this test asserts in-process: the round trip on a real
// file, the closed handle's answer, null, double close, never-opened, the pass-through
// equality with CloseHandle itself, and the frame's registration/removal (`fs:[0]` is
// the same before and after every call). The unreachable half -- what the frame does
// when the call *does* fault -- is reached the only way it can be: the child below
// replaces the module's own CloseHandle import slot (the original's equivalent is the
// IAT slot 0x10063058 the body calls through) with a faulting function and calls the
// body.
//
//     zp_dfa_close                       the suite
//     zp_dfa_close --child <report>      the faulting child, which must never answer

namespace
{
    constexpr DWORD kInvalidHandleError = ERROR_INVALID_HANDLE;  // 6
    constexpr DWORD kWatchdogMilliseconds = 1000;   // how long "no answer" is observed
    constexpr DWORD kStartupMilliseconds = 10000;   // how long the child may take to start
    constexpr DWORD kTerminatedByTest = 0xDEADu;    // the parent's own exit code for it
    constexpr DWORD kStatusAccessViolation = 0xC0000005u;

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
        // Some checks exist to show the process is still usable; when it is not, ctest
        // reads a pipe and an unflushed buffer dies with the process. Flushing keeps
        // the last line printed as the evidence of where it stopped.
        std::fflush(stdout);
    }

    // The fixture: a 32-byte file of known content that the round trip reads through
    // the DFA bodies. Written by the test itself (the bodies have no write path), in
    // the working directory ctest sets to the binary directory.
    constexpr char kFixtureName[] = "dfa_close_roundtrip.bin";
    constexpr char kContents[] = "0123456789abcdefghijklmnopqrstuv";
    constexpr DWORD kContentBytes = sizeof(kContents) - 1;   // 32

    const std::filesystem::path& FixturePath()
    {
        static const std::filesystem::path path =
            std::filesystem::current_path() / kFixtureName;
        return path;
    }

    bool WriteFixture()
    {
        HANDLE file = CreateFileA(FixturePath().string().c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        const BOOL ok = WriteFile(file, kContents, kContentBytes, &written, nullptr);
        CloseHandle(file);
        return ok != FALSE && written == kContentBytes;
    }

    // The thread's exception chain, read straight out of the TEB. The body under test
    // registers its EH4 frame here and removes it again; an answer that skipped either
    // half would show as a different address.
    __declspec(noinline) uintptr_t SehChain()
    {
        uintptr_t chain = 0;
        __asm
        {
            mov eax, fs:[0]
            mov chain, eax
        }
        return chain;
    }

    // The module's own CloseHandle import slot: the address the rebuilt body's
    // `call ds:[…]` reads. The original's body reads the same kind of slot
    // (0x10063058, KERNEL32!CloseHandle, from the original's import table); this is
    // the test process's IAT entry for the same import, found through the PE import
    // directory rather than by decoding the body's bytes.
    void** FindCloseHandleImportSlot()
    {
        auto* base = reinterpret_cast<std::byte*>(GetModuleHandleA(nullptr));
        if (base == nullptr)
            return nullptr;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return nullptr;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return nullptr;

        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (directory.VirtualAddress == 0 || directory.Size == 0)
            return nullptr;

        auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(
            base + directory.VirtualAddress);
        for (; descriptor->Name != 0; ++descriptor)
        {
            const auto* lookup = reinterpret_cast<const IMAGE_THUNK_DATA32*>(
                base + (descriptor->OriginalFirstThunk != 0 ? descriptor->OriginalFirstThunk
                                                            : descriptor->FirstThunk));
            auto* slot = reinterpret_cast<void**>(base + descriptor->FirstThunk);
            for (; lookup->u1.AddressOfData != 0; ++lookup, ++slot)
            {
                if (IMAGE_SNAP_BY_ORDINAL32(lookup->u1.Ordinal))
                    continue;
                const auto* name = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(
                    base + lookup->u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(name->Name), "CloseHandle") == 0)
                    return slot;
            }
        }
        return nullptr;
    }

    // The callee the child puts in that slot. It faults on every entry, so a retrying
    // filter faults forever; the address is read at run time so the compiler has to emit
    // the store. On its first entry it writes one byte to a file the child opened
    // *before* the patch (so the write itself uses no CloseHandle), which is how the
    // parent knows the guarded call was really entered rather than merely slow.
    uintptr_t g_unmapped_address = 0x10;  // the reserved null page, never mapped
    HANDLE g_probe_file = nullptr;
    volatile LONG g_probe_written = 0;

    BOOL __stdcall FaultingClose(HANDLE)
    {
        if (InterlockedExchange(&g_probe_written, 1) == 0)
        {
            DWORD written = 0;
            WriteFile(g_probe_file, "X", 1, &written, nullptr);
        }
        *reinterpret_cast<volatile uint32_t*>(g_unmapped_address) = 0xDEADBEEFu;
        return TRUE;
    }

    std::filesystem::path SelfPath()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return {};
            if (length < buffer.size())
                return std::filesystem::path(std::wstring(buffer.data(), length));
            buffer.resize(buffer.size() * 2);
        }
    }

    // ------------------------------------------------------------------ the suite

    // The round trip: the transcribed create wrapper opens the fixture, the
    // transcribed size/seek/read wrappers walk it, and the rebuilt close wrapper
    // closes it. Each answer is checked against the value the Win32 call answers, so a
    // wrong argument order (the only way a forwarder can be wrong) shows as a value.
    HANDLE CheckRoundTrip(const Launcher::DfaBodies& bodies)
    {
        HANDLE file = bodies.create_file(FixturePath().string().c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(file != INVALID_HANDLE_VALUE,
            std::format("create 0x10004820: opened the fixture through the DFA create body "
                        "(handle 0x{:08X}, not INVALID_HANDLE_VALUE)",
                reinterpret_cast<uintptr_t>(file)));
        if (file == INVALID_HANDLE_VALUE)
            return file;

        LARGE_INTEGER size{};
        const BOOL sized = bodies.get_file_size_ex(file, &size);
        Check(sized == TRUE && size.QuadPart == kContentBytes,
            std::format("size 0x10004720: answered TRUE and the file's {} bytes (got {}): the "
                        "body's two pushes are GetFileSizeEx's arguments",
                kContentBytes, size.QuadPart));

        char buffer[32] = {};
        DWORD read = 0;
        const BOOL readFirst = bodies.read_file(file, buffer, 8, &read, nullptr);
        Check(readFirst == TRUE && read == 8 && std::memcmp(buffer, kContents, 8) == 0,
            std::format("read 0x10004740 at 0: TRUE, 8 bytes, \"{}\" -- the five pushes are "
                        "ReadFile's arguments",
                std::string(buffer, read == 0 ? 0 : 8)));

        const DWORD position = bodies.set_file_pointer(file, 16, nullptr, FILE_BEGIN);
        Check(position == 16,
            std::format("seek 0x10004760 FILE_BEGIN 16: answered 16 (got 0x{:08X}), the four "
                        "pushes are SetFilePointer's arguments",
                position));

        read = 0;
        std::memset(buffer, 0, sizeof(buffer));
        const BOOL readSecond = bodies.read_file(file, buffer, 8, &read, nullptr);
        Check(readSecond == TRUE && read == 8 && std::memcmp(buffer, kContents + 16, 8) == 0,
            std::format("read 0x10004740 at 16: \"{}\" -- the seek really moved the file "
                        "pointer",
                std::string(buffer, read == 0 ? 0 : 8)));

        const DWORD fromEnd = bodies.set_file_pointer(file, -4, nullptr, FILE_END);
        Check(fromEnd == kContentBytes - 4,
            std::format("seek 0x10004760 FILE_END -4: answered {} (got 0x{:08X}): a negative "
                        "distance and the method word both arrive",
                kContentBytes - 4, fromEnd));

        read = 0;
        std::memset(buffer, 0, sizeof(buffer));
        const BOOL readLast = bodies.read_file(file, buffer, 4, &read, nullptr);
        Check(readLast == TRUE && read == 4 &&
                  std::memcmp(buffer, kContents + kContentBytes - 4, 4) == 0,
            std::format("read 0x10004740 at {}: \"{}\" -- the last four bytes",
                kContentBytes - 4, std::string(buffer, read == 0 ? 0 : 4)));
        return file;
    }

    void CheckClose(const Launcher::DfaBodies& bodies, HANDLE file)
    {
        SetLastError(0xFFFFFFFFu);
        const BOOL closed = bodies.close_handle(file);
        const DWORD error = GetLastError();
        Check(closed != FALSE,
            std::format("close 0x10004790: answered CloseHandle's nonzero ({}) for a live "
                        "handle",
                closed));
        Check(error == 0xFFFFFFFFu,
            std::format("close 0x10004790: the success path adds nothing -- last error still "
                        "0x{:08X}, so the frame touched no error itself",
                error));

        // The handle is really closed, checked two ways: the OS refuses to duplicate it,
        // and the transcribed reader gets ReadFile's own answer for a closed handle.
        SetLastError(0xFFFFFFFFu);
        HANDLE duplicate = nullptr;
        const BOOL duplicated = DuplicateHandle(GetCurrentProcess(), file, GetCurrentProcess(),
            &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS);
        Check(duplicated == FALSE && GetLastError() == kInvalidHandleError,
            std::format("the handle is really closed: DuplicateHandle refuses it with "
                        "ERROR_INVALID_HANDLE ({})",
                kInvalidHandleError));

        char buffer[32] = {};
        DWORD read = 5;
        SetLastError(0xFFFFFFFFu);
        const BOOL afterClose = bodies.read_file(file, buffer, 8, &read, nullptr);
        const DWORD afterCloseError = GetLastError();
        Check(afterClose == FALSE && afterCloseError == kInvalidHandleError,
            std::format("read 0x10004740 after the close: FALSE (0), GetLastError "
                        "ERROR_INVALID_HANDLE ({}), 0 bytes read -- ReadFile's own answer "
                        "for a closed handle, which is what the wrapper leaves the caller",
                kInvalidHandleError));

        // The body is a forwarder: its answer is ReadFile's answer for the same value.
        DWORD rawRead = 0;
        SetLastError(0xFFFFFFFFu);
        const BOOL raw = ReadFile(file, buffer, 8, &rawRead, nullptr);
        const DWORD rawError = GetLastError();
        Check(raw == afterClose && rawRead == read && rawError == afterCloseError,
            std::format("the reader is a forwarder: ReadFile itself answers FALSE/{} for the "
                        "same closed value, exactly as the body did",
                rawError));
    }

    void CheckNullAndInvalid(const Launcher::DfaBodies& bodies, HANDLE closedHandle)
    {
        char buffer[32] = {};
        DWORD read = 5;

        // A null handle: the body has no test on its argument (`push [ebp+8]` at
        // 0x100047CA is the only instruction before the call), so this is CloseHandle's
        // answer, not a no-op and not a fault.
        SetLastError(0xFFFFFFFFu);
        const BOOL nullClose = bodies.close_handle(nullptr);
        const DWORD nullError = GetLastError();
        Check(nullClose == FALSE && nullError == kInvalidHandleError,
            std::format("null: the wrapper has no argument guard, so CloseHandle(NULL) "
                        "answers FALSE (0) with ERROR_INVALID_HANDLE ({}), and the process "
                        "is alive to say so",
                kInvalidHandleError));

        SetLastError(0xFFFFFFFFu);
        const BOOL nullRead = bodies.read_file(nullptr, buffer, 8, &read, nullptr);
        Check(nullRead == FALSE && GetLastError() == kInvalidHandleError,
            "null through the transcribed reader: FALSE and ERROR_INVALID_HANDLE (6), the "
            "same pass-through");

        // The already-closed value: the second close reaches CloseHandle with the stale
        // handle and the body keeps no state that could answer differently.
        SetLastError(0xFFFFFFFFu);
        const BOOL doubleClose = bodies.close_handle(closedHandle);
        const DWORD doubleError = GetLastError();
        Check(doubleClose == FALSE && doubleError == kInvalidHandleError,
            std::format("closing the already-closed value again: FALSE (0), "
                        "ERROR_INVALID_HANDLE ({}): the deciding instruction is the "
                        "unconditional `call ds:[CloseHandle]` at 0x100047CD, and nothing "
                        "in the body remembers the first close",
                kInvalidHandleError));

        // A handle nobody opened. The low two bits of 0xDEADBEEF are set, so the kernel
        // rejects it without a table lookup, and the FALSE/6 answer proves it was not a
        // live handle (closing one answers nonzero).
        const HANDLE neverOpened = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0xDEADBEEFu));
        SetLastError(0xFFFFFFFFu);
        const BOOL neverClose = bodies.close_handle(neverOpened);
        const DWORD neverError = GetLastError();
        Check(neverClose == FALSE && neverError == kInvalidHandleError,
            std::format("a handle that was never opened (0xDEADBEEF): FALSE (0), "
                        "ERROR_INVALID_HANDLE ({}) -- and the answer itself proves the value "
                        "was not live, because a closable handle answers nonzero",
                kInvalidHandleError));

        // And the wrapper still works: the frame is per call, so the three failures above
        // left nothing behind.
        HANDLE again = bodies.create_file(FixturePath().string().c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Check(again != INVALID_HANDLE_VALUE && bodies.close_handle(again) != FALSE,
            "after null, a double close and a never-opened value, a fresh create/close "
            "round still works (the frame is per call)");
    }

    void CheckPassThrough()
    {
        // Two identical handles: one closed through the rebuild, one through CloseHandle
        // itself. The answers must agree, because the body is one push and one call.
        const Launcher::DfaBodies bodies = Launcher::DfaBodiesForTest();
        HANDLE mine = bodies.create_file(FixturePath().string().c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE raw = bodies.create_file(FixturePath().string().c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (mine == INVALID_HANDLE_VALUE || raw == INVALID_HANDLE_VALUE)
        {
            Check(false, "the pass-through comparison could not open its two handles");
            if (mine != INVALID_HANDLE_VALUE)
                bodies.close_handle(mine);
            if (raw != INVALID_HANDLE_VALUE)
                CloseHandle(raw);
            return;
        }

        SetLastError(0xFFFFFFFFu);
        const BOOL byWrapper = bodies.close_handle(mine);
        const DWORD wrapperError = GetLastError();
        SetLastError(0xFFFFFFFFu);
        const BOOL byRaw = CloseHandle(raw);
        const DWORD rawError = GetLastError();
        Check(byWrapper == byRaw && wrapperError == rawError,
            std::format("pass-through: the wrapper answered {} and CloseHandle answered {} "
                        "for two identical handles (errors 0x{:08X}/0x{:08X})",
                byWrapper, byRaw, wrapperError, rawError));
    }

    void CheckSehChain(const Launcher::DfaBodies& bodies)
    {
        // The frame's registration is the one thing about EH4 that is observable without
        // a fault: the body pushes the thread's chain, installs its own frame, and
        // restores the previous one on every path. A body that skipped the restore would
        // leave fs:[0] pointing into a dead frame.
        const uintptr_t before = SehChain();
        HANDLE file = bodies.create_file(FixturePath().string().c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        const uintptr_t afterCreate = SehChain();
        const BOOL closed = file == INVALID_HANDLE_VALUE ? FALSE : bodies.close_handle(file);
        const uintptr_t afterClose = SehChain();
        const BOOL nullAnswer = bodies.close_handle(nullptr);
        const uintptr_t afterNull = SehChain();

        Check(closed != FALSE && afterCreate == before,
            "the frame around the close: fs:[0] is the same before the call and after the "
            "successful close (registered and removed, not left dangling)");
        Check(nullAnswer == FALSE && afterNull == before,
            "the frame's failure path too: fs:[0] is unchanged after the null close");
    }

    // ------------------------------------------------------------------ the child
    //
    // The child reaches the guarded call's fault path: it replaces the module's own
    // CloseHandle import slot -- the same kind of slot the original's body reads at
    // 0x10063058 -- with a function that faults on entry, then calls the rebuild. The
    // filter's EXCEPTION_CONTINUE_EXECUTION makes the OS retry the call, so the child
    // never answers and never leaves: it is still running when the parent's watchdog
    // expires. Two markers make that measurable rather than assumed: the report's first
    // line is written *before* the patch ("the child got this far"), and the faulting
    // callee writes one byte to a probe file opened before the patch on its first entry
    // ("the guarded call was entered"). A second report line after the call exists so a
    // child that ever answers is caught rather than silently killed.
    int RunChild(const std::filesystem::path& report, const std::filesystem::path& probe)
    {
        const Launcher::DfaBodies bodies = Launcher::DfaBodiesForTest();
        void** slot = FindCloseHandleImportSlot();

        if (slot == nullptr)
        {
            std::ofstream stream(report, std::ios::binary | std::ios::trunc);
            stream << "NOSLOT\n";
            return 3;
        }

        DWORD protection = 0;
        if (VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection) == FALSE)
        {
            std::ofstream stream(report, std::ios::binary | std::ios::trunc);
            stream << "NOPROTECT\n";
            return 4;
        }

        g_probe_file = CreateFileA(probe.string().c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_probe_file == INVALID_HANDLE_VALUE)
        {
            std::ofstream stream(report, std::ios::binary | std::ios::trunc);
            stream << "NOPROBE\n";
            return 5;
        }
        void* previous = *slot;

        // The report line is written *before* the slot is replaced: the stream's own
        // close calls CloseHandle, which is the import being patched, so writing it
        // afterwards would fault outside the frame under test.
        {
            std::ofstream stream(report, std::ios::binary | std::ios::trunc);
            stream << "STARTED\n";
            stream.flush();
        }

        *slot = reinterpret_cast<void*>(&FaultingClose);

        // This call never returns while the slot faults: the exception is handled by
        // the body's own frame with EXCEPTION_CONTINUE_EXECUTION, which resumes the
        // faulting call. Reaching the next line means the frame answered instead, which
        // is the divergence this child exists to catch.
        const BOOL answer = bodies.close_handle(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(0x1234u)));

        *slot = previous;
        VirtualProtect(slot, sizeof(*slot), protection, &protection);
        {
            std::ofstream stream(report, std::ios::binary | std::ios::trunc);
            stream << "ANSWERED=" << std::format("{:08X}", static_cast<uint32_t>(answer)) << "\n";
        }
        std::printf("ANSWERED=%08X\n", static_cast<uint32_t>(answer));
        return 0;
    }

    std::string ReadReport(const std::filesystem::path& report)
    {
        std::ifstream stream(report, std::ios::binary);
        std::string text;
        std::string line;
        while (stream && std::getline(stream, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            if (!text.empty())
                text.push_back('|');
            text += line;
        }
        return text;
    }

    void CheckFaultIsRetried(const std::filesystem::path& self)
    {
        const auto report = std::filesystem::current_path() / "dfa_close_child.report";
        const auto probe = std::filesystem::current_path() / "dfa_close_child.probe";
        std::error_code ignored;
        std::filesystem::remove(report, ignored);
        std::filesystem::remove(probe, ignored);

        std::wstring command = L"\"" + self.wstring() + L"\" --child \"" + report.wstring() +
                               L"\" \"" + probe.wstring() + L"\"";
        std::vector<wchar_t> line(command.begin(), command.end());
        line.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (self.empty() || CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0,
                nullptr, nullptr, &startup, &process) == FALSE)
        {
            Check(false, std::format("the faulting child could not be started (error {})",
                GetLastError()));
            return;
        }

        // Wait for the child to say it is about to enter the guarded call, so the
        // watchdog measures "no answer", not "slow start". A child that dies before the
        // report line is a different failure and is reported as such.
        std::string text;
        bool started = false;
        const ULONGLONG deadline = GetTickCount64() + kStartupMilliseconds;
        for (;;)
        {
            text = ReadReport(report);
            if (text.rfind("STARTED", 0) == 0)
            {
                started = true;
                break;
            }
            if (WaitForSingleObject(process.hProcess, 10) == WAIT_OBJECT_0)
            {
                text = ReadReport(report);
                break;
            }
            if (GetTickCount64() > deadline)
                break;
        }

        DWORD status = 0;
        bool stillRunning = false;
        if (started)
        {
            stillRunning = WaitForSingleObject(process.hProcess, kWatchdogMilliseconds) ==
                           WAIT_TIMEOUT;
        }

        // Whatever happened, the child is not left behind: terminate it if it is still
        // in the retry loop, wait for it, and read its exit code (which is this test's
        // own code, so "killed while retrying" is distinguishable from any real answer).
        if (stillRunning)
        {
            TerminateProcess(process.hProcess, kTerminatedByTest);
            WaitForSingleObject(process.hProcess, INFINITE);
        }
        GetExitCodeProcess(process.hProcess, &status);
        const bool reaped = WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0;
        text = ReadReport(report);
        const std::string probeText = ReadReport(probe);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        // The probe byte is the difference between "still running" and "still running
        // *inside the retry loop*": the faulting callee wrote it from its first entry
        // through the patched slot.
        Check(probeText == "X",
            std::format("child: the faulting callee was entered through the patched slot (the "
                        "probe file holds \"{}\"), so the guarded call is where the child is "
                        "stuck",
                probeText));
        Check(started && text.rfind("STARTED", 0) == 0,
            std::format("child: the close body was reached after the slot was patched "
                        "(report \"{}\")",
                text));
        Check(started && stillRunning,
            std::format("child: the guarded call did not answer within {} ms -- the filter's "
                        "EXCEPTION_CONTINUE_EXECUTION (0x100047EC) makes the OS retry the "
                        "faulting call, so the frame has no answer for it (exit 0x{:08X}{}; "
                        "0x{:08X} would be the fault with no frame at all)",
                kWatchdogMilliseconds, status,
                text.find("ANSWERED") != std::string::npos ? ", and it answered: " + text : "",
                kStatusAccessViolation));
        Check(started && text.find("ANSWERED") == std::string::npos,
            std::format("child: no ANSWERED line -- the handler at 0x100047F0 (`return TRUE`) "
                        "is unreachable, so the rebuild does not answer where the original "
                        "retries (report \"{}\")",
                text));
        Check(reaped && status == kTerminatedByTest,
            std::format("child: terminated and reaped by the test (exit 0x{:08X}), so the "
                        "suite leaves no process behind",
                status));
        if (!started)
            Check(false, std::format("child: it never reached the guarded call (exit "
                                     "0x{:08X}, report \"{}\")",
                status, text));
    }

    int Run(const std::filesystem::path& self)
    {
        Check(WriteFixture(),
            std::format("fixture: the test wrote its own {}-byte file ({}), the input every "
                        "assertion below reads",
                kContentBytes, kFixtureName));

        // The four forwarder slots hold kernel32's functions, as RegisterLauncher leaves
        // them; the seam publishes the same four before handing the bodies over.
        const Launcher::DfaBodies bodies = Launcher::DfaBodiesForTest();
        Check(bodies.create_file != nullptr && bodies.read_file != nullptr &&
                  bodies.set_file_pointer != nullptr && bodies.get_file_size_ex != nullptr &&
                  bodies.close_handle != nullptr,
            "the seam answered the five DFA body addresses (four transcribed forwarders "
            "and the rebuilt close)");

        const HANDLE file = CheckRoundTrip(bodies);
        CheckSehChain(bodies);
        if (file != INVALID_HANDLE_VALUE)
            CheckClose(bodies, file);
        CheckNullAndInvalid(bodies, file);
        CheckPassThrough();
        CheckFaultIsRetried(self);

        std::error_code ignored;
        std::filesystem::remove(FixturePath(), ignored);
        std::filesystem::remove(std::filesystem::current_path() / "dfa_close_child.report",
            ignored);
        std::filesystem::remove(std::filesystem::current_path() / "dfa_close_child.probe",
            ignored);
        return failures == 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    // The child faults on purpose and the parent observes it, so the fault must be a
    // result in this process, never a Windows dialog on the desktop.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc >= 4 && std::strcmp(argv[1], "--child") == 0)
        return RunChild(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]));

    try
    {
        Run(SelfPath());
    }
    catch (const std::exception& error)
    {
        Check(false, std::format("the test itself threw {}", error.what()));
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
