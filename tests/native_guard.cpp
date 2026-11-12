#include <common.hxx>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import native;

// The native layer's two guarded entries, asserted by behaviour. Their fault
// parity used to be argued from the disassembly; this test runs them. `0x100038E0`
// (59 instructions) and `0x10003980` (52) are compiler output for a C function
// with `__try`/`__except (EXCEPTION_EXECUTE_HANDLER)`, so the frame *is* the
// feature:
//
//   0x100038E0  invoke:     resolve, then `test eax,eax / je 0x1000393F` skips the
//                           handler call when the answer is null, `mov [ebp-4],-2;
//                           mov al,1` answers 1 on the normal path; the scope table
//                           (0x10153EE8) names the filter `mov eax,1; ret` (0x1000395A)
//                           and the handler `... xor al,al ...` (0x10003960), i.e. a
//                           fault inside the guarded region answers 0.
//   0x10003980  resolve:    no test at all -- it answers the resolver's pointer
//                           unchanged (`0x100039D0`..`0x100039EB`); a fault answers 0
//                           through the filter/handler pair at 0x100039EC/0x100039F2
//                           (scope table 0x10153F08).
//
// So a proven guard is four things: the normal answer, a fault inside the region
// swallowed to 0, the null-handler skip in the *invoke* only, and the null resolver
// left to the caller. The first three are asserted in this process; the fourth is
// what an original consumer does with the guard's zero (`DrivebyFOVFix`'s helper
// reads `[eax+0Eh]` at 0x1000646E), and reading through zero kills the process, so
// it runs in a child like `zp_save_walk`'s uninitialised-magic case.
//
// The guard has no game in this process: `Native::Register()` is never called, and
// `Native::SetResolverForTest` (native.ixx; it exists for this test only) installs a
// resolver the test controls. The fake resolver is naked because the layer calls its
// resolver with the hash in ESI and no stack argument (the game's resolver reads
// ESI), and it answers the target the current check wants -- a real handler, a
// faulting one, or null.
//
//     zp_native_guard              the suite
//     zp_native_guard --child <report>   one caller-side fault in a fresh process

namespace
{
    constexpr uint32_t kGuardHash = 0x0BADF00D;   // the hash every call passes down
    constexpr uint32_t kBlockValue = 0xC0FFEE01;  // what the normal target writes into args[0]
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
        // The point of some of these checks is that the process must survive; when it
        // does not, stdout is a pipe (ctest) and the buffer would be lost with the
        // process. Flushing keeps the last line printed the evidence of where it died.
        std::fflush(stdout);
    }

    // The handler the fake resolver answers, the hash the guard put in ESI (what a
    // resolver the layer calls must read), and how many times the current target ran.
    // C linkage because the naked resolver names them from inline asm.
    extern "C"
    {
        static uintptr_t g_guard_test_answer = 0;
        static uint32_t g_guard_test_hash = 0;
        static uint32_t g_guard_test_calls = 0;
    }

    // The fake resolver: answers g_guard_test_answer and records ESI. It is what
    // Native::SetResolverForTest installs; the guard calls it exactly as it calls the
    // game's resolver (`call [dword_1015BDE0]`), with the hash in ESI.
    static __declspec(naked) void* __cdecl TestResolver()
    {
        __asm
        {
            mov DWORD PTR [g_guard_test_hash], esi
            mov eax, DWORD PTR [g_guard_test_answer]
            ret
        }
    }

    // A resolver that faults inside the guarded region: it reads through zero, the
    // shape of a resolver whose own lookup dereferences a missing slot. The guard's
    // `__except` must answer 0 and the process must survive.
    static __declspec(naked) void* __cdecl FaultingResolver()
    {
        __asm
        {
            xor eax, eax
            mov eax, DWORD PTR [eax]
            ret
        }
    }

    // The normal target. It writes a known word into args[0], counts its calls and
    // *returns* 7: the original's `push edi; call eax; add esp,4` ignores the
    // handler's EAX, and the invoke answers its own `mov al,1` regardless.
    int __cdecl TargetWritesBlock(void* block)
    {
        ++g_guard_test_calls;
        static_cast<uint32_t*>(block)[0] = kBlockValue;
        return 7;
    }

    // Targets that fault inside the guarded call. The addresses are read at run time
    // so the compiler must emit the access, not fold a known-bad constant.
    uintptr_t g_unmapped_address = 0x10;  // the null page: reserved, never mapped
    uintptr_t g_null_address = 0;

    void __cdecl TargetReadsNull(void*)
    {
        ++g_guard_test_calls;
        (void)*reinterpret_cast<volatile uint32_t*>(g_null_address);
    }

    void __cdecl TargetWritesUnmapped(void*)
    {
        ++g_guard_test_calls;
        *reinterpret_cast<volatile uint32_t*>(g_unmapped_address) = 0xDEADBEEFu;
    }

    // Run the normal path once and answer whether it still works. Called after every
    // fault, so "the process survived" is not just "the test did not crash yet": the
    // guard's frame has to be intact enough to answer the next call. It installs both
    // halves of the normal path -- the resolver and the handler it answers -- because
    // a fault case may have replaced either.
    bool NormalPathStillWorks()
    {
        Native::SetResolverForTest(reinterpret_cast<uintptr_t>(&TestResolver));
        g_guard_test_answer = reinterpret_cast<uintptr_t>(&TargetWritesBlock);
        g_guard_test_calls = 0;
        uint32_t block[0x28] = {};
        return Native::Invoke(kGuardHash, block) && g_guard_test_calls == 1 &&
               block[0] == kBlockValue;
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

    // ---------------------------------------------------------------- the suite
    void CheckNormalPath()
    {
        // The invoke calls the handler and answers the original's 1. The handler's
        // own return value (7) is not the guard's answer.
        Native::SetResolverForTest(reinterpret_cast<uintptr_t>(&TestResolver));
        g_guard_test_answer = reinterpret_cast<uintptr_t>(&TargetWritesBlock);
        g_guard_test_hash = 0;
        g_guard_test_calls = 0;
        uint32_t block[0x28] = {};
        const bool invoked = Native::Invoke(kGuardHash, block);
        Check(invoked,
            "invoke 0x100038E0: a target that returns 7 answered the original's 1 (success)");
        Check(g_guard_test_calls == 1,
            "invoke 0x100038E0: the handler ran exactly once (push edi; call eax; add esp,4)");
        Check(block[0] == kBlockValue,
            std::format("invoke 0x100038E0: the handler received the caller's block "
                        "(args[0] = 0x{:08X})", block[0]));
        Check(g_guard_test_hash == kGuardHash,
            std::format("invoke 0x100038E0: the resolver read the hash from ESI (0x{:08X})",
                g_guard_test_hash));

        // The resolve answers the handler address without calling it. That pointer is
        // the "known value" this entry returns; EDX (the block) is not passed to it.
        g_guard_test_calls = 0;
        const void* resolved = Native::Resolve(kGuardHash);
        Check(resolved == reinterpret_cast<const void*>(&TargetWritesBlock),
            std::format("resolve 0x10003980: answered the handler address (0x{:08X}), "
                        "not a result of calling it",
                reinterpret_cast<uintptr_t>(resolved)));
        Check(g_guard_test_calls == 0,
            "resolve 0x10003980: the handler was not called (resolve only)");
        Check(g_guard_test_hash == kGuardHash,
            std::format("resolve 0x10003980: the resolver read the hash from ESI (0x{:08X})",
                g_guard_test_hash));
    }

    void CheckFaultsInsideTheRegion()
    {
        // A handler that reads through null: the fault is raised inside the guarded
        // call, so the frame answers 0 and the process stays alive.
        Native::SetResolverForTest(reinterpret_cast<uintptr_t>(&TestResolver));
        g_guard_test_answer = reinterpret_cast<uintptr_t>(&TargetReadsNull);
        g_guard_test_calls = 0;
        uint32_t block[0x28] = {};
        const bool readNull = Native::Invoke(kGuardHash, block);
        Check(!readNull,
            "invoke 0x100038E0: a handler that dereferences null answered 0, not a crash");
        Check(g_guard_test_calls == 1,
            "invoke 0x100038E0: the faulting handler had been entered (the fault is inside "
            "the guarded call, not before it)");
        Check(NormalPathStillWorks(),
            "invoke 0x100038E0: the guard answered the normal path correctly after the fault "
            "(the SEH frame unwound and the process survived)");

        // A handler that writes to an unmapped address: same guarantee, the store side.
        g_guard_test_answer = reinterpret_cast<uintptr_t>(&TargetWritesUnmapped);
        g_guard_test_calls = 0;
        const bool writeUnmapped = Native::Invoke(kGuardHash, block);
        Check(!writeUnmapped,
            "invoke 0x100038E0: a handler that writes to an unmapped address answered 0, "
            "not a crash");
        Check(g_guard_test_calls == 1,
            "invoke 0x100038E0: the writing handler had been entered");
        Check(NormalPathStillWorks(),
            "invoke 0x100038E0: the guard still worked after the faulting write");

        // The resolver call is inside the same region. A resolver that faults is
        // caught too: this is the case a null dword_1015BDE0 produces, and the
        // rebuild's `__except` is what keeps it out of the caller.
        Native::SetResolverForTest(reinterpret_cast<uintptr_t>(&FaultingResolver));
        g_guard_test_calls = 0;
        const bool resolverFaultInvoke = Native::Invoke(kGuardHash, block);
        Check(!resolverFaultInvoke,
            "invoke 0x100038E0: a resolver that faults answered 0 (the guarded region "
            "covers the resolver call, 0x1000392B)");
        const void* resolverFaultResolve = Native::Resolve(kGuardHash);
        Check(resolverFaultResolve == nullptr,
            "resolve 0x10003980: a resolver that faults answered 0 (0x100039C9 in the "
            "same frame)");
        Check(g_guard_test_calls == 0,
            "the resolver fault happened before any handler could run");
        Check(NormalPathStillWorks(),
            "both guarded entries still worked after the resolver fault");
    }

    void CheckNullHandler()
    {
        // The resolver answers null (a native the game does not define). The invoke's
        // `test eax,eax / je 0x1000393F` skips the call and the entry still reports
        // success: `mov al,1`. The resolve has no such test; it answers the null.
        Native::SetResolverForTest(reinterpret_cast<uintptr_t>(&TestResolver));
        g_guard_test_answer = 0;
        g_guard_test_calls = 0;
        uint32_t block[0x28] = {};
        const bool invoked = Native::Invoke(kGuardHash, block);
        Check(invoked,
            "invoke 0x100038E0: a null handler skipped the call and answered 1 (the "
            "original's test eax,eax; je; mov al,1 at 0x10003935..0x10003946)");
        Check(g_guard_test_calls == 0,
            "invoke 0x100038E0: the null handler was not called");
        const void* resolved = Native::Resolve(kGuardHash);
        Check(resolved == nullptr,
            "resolve 0x10003980: a null handler was answered as null (that entry has no "
            "test at all)");
        Check(g_guard_test_calls == 0,
            "resolve 0x10003980: a null handler was not called");
    }

    // ------------------------------------------------------------------ the child
    // The child plays the caller of the guarded resolve against the original's
    // unresolved slot: `dword_1015BDE0` is zero, so the guard's resolver call faults
    // and the frame answers 0 (asserted by the parent in its own process). The child
    // then does what the original's consumer does with that 0 -- `DrivebyFOVFix`'s
    // helper reads `[eax+0Eh]` (0x1000646E) -- and that read is the fault the
    // original preserves. The report is written first, so the parent can tell "the
    // guard answered 0 and the caller faulted" from "the guard itself faulted".
    int RunChild(const std::filesystem::path& report)
    {
        Native::SetResolverForTest(0);
        void* handler = Native::Resolve(kGuardHash);

        {
            std::ofstream stream(report, std::ios::binary | std::ios::trunc);
            if (stream)
                stream << "RESOLVED=" << (handler == nullptr ? "0" : "nonzero") << "\n";
        }

        volatile uint32_t read = *reinterpret_cast<volatile uint32_t*>(
            static_cast<char*>(handler) + 0x0E);
        std::printf("RESOLVED=%p READ=%u\n", handler, read);
        return 0;
    }

    bool RunChildProcess(const std::filesystem::path& self, const std::filesystem::path& report,
        DWORD& status, std::string& outcome, std::string& why)
    {
        std::error_code error;
        std::filesystem::remove(report, error);

        std::wstring command = L"\"" + self.wstring() + L"\" --child \"" + report.wstring() + L"\"";
        std::vector<wchar_t> line(command.begin(), command.end());
        line.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                &startup, &process) == FALSE)
        {
            why = std::format("CreateProcess failed for \"{}\" --child \"{}\" (error {})",
                self.string(), report.string(), GetLastError());
            return false;
        }

        WaitForSingleObject(process.hProcess, 30000);
        GetExitCodeProcess(process.hProcess, &status);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        std::ifstream stream(report, std::ios::binary);
        std::string lineRead;
        if (stream && std::getline(stream, lineRead))
        {
            while (!lineRead.empty() && (lineRead.back() == '\r' || lineRead.back() == ' '))
                lineRead.pop_back();
            outcome = lineRead;
        }
        return true;
    }

    void CheckNullResolverCallerFault(const std::filesystem::path& self)
    {
        // The guard's own answer, in this process: the SEH frame catches the call
        // through the unresolved slot and answers 0. Nothing dies here.
        Native::SetResolverForTest(0);
        g_guard_test_calls = 0;
        uint32_t block[0x28] = {};
        const void* answer = Native::Resolve(kGuardHash);
        Check(answer == nullptr,
            "resolve 0x10003980: a null resolver (dword_1015BDE0 = 0) answered 0 in this "
            "process -- the frame caught the call, the caller did not");
        Check(!Native::Invoke(kGuardHash, block),
            "invoke 0x100038E0: a null resolver answered 0 (the call faulted inside the "
            "guarded region)");
        Check(g_guard_test_calls == 0,
            "no handler ran when the resolver itself was null");
        Check(NormalPathStillWorks(),
            "the guard still worked after the null resolver fault");

        // The caller side, in a child: reading through the guard's 0 must fault.
        const auto report = std::filesystem::current_path() / "native_guard_child.report";
        DWORD status = 0;
        std::string outcome;
        std::string why;
        const bool ran = !self.empty() && RunChildProcess(self, report, status, outcome, why);
        Check(ran && outcome == "RESOLVED=0",
            std::format("child: the guard answered 0 for the null resolver before the "
                        "caller read through it (report \"{}\"{})",
                outcome, ran ? "" : "; " + why));
        Check(ran && status == kStatusAccessViolation,
            std::format("child: the caller's [eax+0Eh] read through that 0 died with an "
                        "access violation (exit 0x{:08X}, expected 0xC0000005{})",
                status, ran ? "" : "; " + why));
        Native::SetResolverForTest(reinterpret_cast<uintptr_t>(&TestResolver));
    }

    int Run()
    {
        CheckNormalPath();
        CheckFaultsInsideTheRegion();
        CheckNullHandler();
        CheckNullResolverCallerFault(SelfPath());
        return failures == 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    // This test faults on purpose (and re-runs itself as a faulting child), so the
    // fault must be a result in this process, never a Windows dialog on the desktop.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc >= 2 && std::strcmp(argv[1], "--child") == 0)
        return RunChild(argc >= 3 ? std::filesystem::path(argv[2]) : std::filesystem::path());

    try
    {
        Run();
    }
    catch (const std::exception& error)
    {
        Check(false, std::format("the test itself threw {}", error.what()));
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
