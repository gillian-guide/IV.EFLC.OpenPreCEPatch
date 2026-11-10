#include <common.hxx>

#include <cstdio>
#include <string>

import common;
import game;
import improvements;
import patch;

// The improvement-group switch and the failsafe primitives it gates.
//
// The switch is proved by parsing, by the environment resolver's one-time
// default, and - the part that matters - by the same null dereference in a child
// process under both configurations: with the failsafe group off the child must
// die with 0xC0000005, and with it on the child must exit 0. That is the
// fault-parity contract the feature-level tests below reuse.

namespace
{
    int failures = 0;
    int checks = 0;

    void Check(bool condition, std::string_view what, std::string_view detail = {})
    {
        ++checks;
        if (condition)
        {
            std::printf("PASS %.*s\n", static_cast<int>(what.size()), what.data());
            return;
        }
        ++failures;
        std::printf("FAIL %.*s", static_cast<int>(what.size()), what.data());
        if (!detail.empty())
            std::printf(" (%s)", std::string(detail).c_str());
        std::printf("\n");
    }

    constexpr uint32_t kFailsafe = Improve(Improvement::failsafe);

    void TestParser()
    {
        Check(ParseImprovements(nullptr) == 0, "switch: null means the 1:1 path");
        Check(ParseImprovements("") == 0, "switch: empty means the 1:1 path");
        Check(ParseImprovements("none") == 0, "switch: none is empty");
        Check(ParseImprovements("off") == 0, "switch: off is empty");
        Check(ParseImprovements("0") == 0, "switch: 0 is empty");
        Check(ParseImprovements("all") == kFailsafe, "switch: all is the folded set");
        Check(ParseImprovements("on") == kFailsafe, "switch: on is the folded set");
        Check(ParseImprovements("1") == kFailsafe, "switch: 1 is the folded set");
        Check(ParseImprovements("failsafe") == kFailsafe, "switch: the group name selects it");
        Check(ParseImprovements("FAILSAFES") == kFailsafe, "switch: names are case-insensitive");
        Check(ParseImprovements("guards") == kFailsafe, "switch: the guard alias selects it");
        Check(ParseImprovements("only:failsafe") == kFailsafe, "switch: only: selects the named group");
        Check(ParseImprovements("except:failsafe") == 0, "switch: except:failsafe is the 1:1 configuration");
        Check(ParseImprovements("all, failsafe") == kFailsafe, "switch: a list keeps the selected groups");
        Check(ParseImprovements("nonsense") == 0, "switch: an unknown name selects nothing");
        Check(ParseImprovements("except:nonsense") == kFailsafe, "switch: except: an unknown name subtracts nothing");
    }

    void TestResolver()
    {
        SetEnvironmentVariableA("OPCEP_IMPROVEMENTS", nullptr);
        ResetImprovementsForTest();
        Check(EnabledImprovements() == 0, "resolver: unset is the 1:1 path");

        SetEnvironmentVariableA("OPCEP_IMPROVEMENTS", "all");
        ResetImprovementsForTest();
        Check(EnabledImprovements() == kFailsafe, "resolver: all selects the failsafe group");

        SetEnvironmentVariableA("OPCEP_IMPROVEMENTS", "except:failsafe");
        ResetImprovementsForTest();
        Check(EnabledImprovements() == 0, "resolver: except:failsafe restores the 1:1 path");

        SetEnvironmentVariableA("OPCEP_IMPROVEMENTS", "failsafe");
        ResetImprovementsForTest();
        Check(EnabledImprovements() == kFailsafe, "resolver: a bare group name selects it");

        SetEnvironmentVariableA("OPCEP_IMPROVEMENTS", nullptr);
        ResetImprovementsForTest();
        Check(EnabledImprovements() == 0, "resolver: unset again is the 1:1 path");
    }

    void TestGuards()
    {
        SetImprovementsForTest(0);
        Check(!Failsafe::Missing("probe", true), "guard off: a missing value is not skipped");
        Check(!Failsafe::Missing("probe", false), "guard off: a present value is not skipped");

        SetImprovementsForTest(kFailsafe);
        Check(Failsafe::Missing("probe", true), "guard on: a missing value is skipped");
        Check(!Failsafe::Missing("probe", false), "guard on: a present value is not skipped");

        // A synthetic image: 0x400000..0x401000.
        Game::Get().AttachImage(0x400000u, 0x1000u, std::filesystem::path(L"test.exe"), {});
        SetImprovementsForTest(0);
        Check(Failsafe::Site("probe", 0x400000u, 5), "guard off: an inside site is allowed");
        Check(Failsafe::Site("probe", 0x900000u, 5), "guard off: an outside site is still written");
        SetImprovementsForTest(kFailsafe);
        Check(Failsafe::Site("probe", 0x400000u, 5), "guard on: an inside site is allowed");
        Check(!Failsafe::Site("probe", 0x400FFEu, 5), "guard on: a site crossing the end is refused");
        Check(!Failsafe::Site("probe", 0x401000u, 1), "guard on: the first byte past the image is refused");
        Check(!Failsafe::Site("probe", 0x3FFFFFu, 1), "guard on: the byte before the image is refused");
        Check(!Failsafe::Site("probe", 0x400000u, 0), "guard on: a zero-length write is refused");

        Check(!Failsafe::Body("probe", 0), "guard on: a missing body is refused");
        Check(Failsafe::Body("probe", 0x1234), "guard on: a resolved body is allowed");
        SetImprovementsForTest(0);
        Check(Failsafe::Body("probe", 0), "guard off: a missing body is still installed");
    }

    void TestMatchCount()
    {
        SetImprovementsForTest(0);
        Check(ExpectMatchCount(1, 2, "probe"), "guard off: a short multi-hit arm installs");
        SetImprovementsForTest(kFailsafe);
        Check(ExpectMatchCount(2, 2, "probe"), "guard on: the expected count installs");
        Check(!ExpectMatchCount(1, 2, "probe"), "guard on: a short multi-hit arm is skipped");
        Check(!ExpectMatchCount(3, 2, "probe"), "guard on: a long multi-hit arm is skipped");
    }

    // ---- The fault-parity child ------------------------------------------------
    //
    // `--child on|off` performs exactly one dereference of a null pointer through
    // the guard. The parent runs it twice. This is the smallest input for which
    // the two configurations must differ, and the same shape every feature-level
    // failsafe test uses.

    int RunChild(const char* mode)
    {
        SetImprovementsForTest(mode[0] == 'o' && mode[1] == 'n' ? kFailsafe : 0);
        volatile int* pointer = nullptr;
        if (!Failsafe::Missing("child probe", true))
            *pointer = 1;
        return 0;
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

    struct ChildResult
    {
        bool ran = false;
        bool exited = false;
        DWORD status = 0;
        std::string why;
    };

    ChildResult RunChildProcess(const std::filesystem::path& self, const wchar_t* mode)
    {
        ChildResult result;
        if (self.empty())
        {
            result.why = "the test could not name its own executable";
            return result;
        }

        std::wstring command = L"\"" + self.wstring() + L"\" --child " + mode;
        std::vector<wchar_t> line(command.begin(), command.end());
        line.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                &startup, &process) == FALSE)
        {
            result.why = std::format("CreateProcess failed (error {})", GetLastError());
            return result;
        }
        result.ran = true;

        if (WaitForSingleObject(process.hProcess, 30000) != WAIT_OBJECT_0)
        {
            result.why = "the child did not exit";
            TerminateProcess(process.hProcess, 0xDEADu);
            WaitForSingleObject(process.hProcess, 5000);
        }
        else
        {
            result.exited = true;
            GetExitCodeProcess(process.hProcess, &result.status);
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return result;
    }

    void TestFaultParity()
    {
        constexpr DWORD kStatusAccessViolation = 0xC0000005u;
        const auto self = SelfPath();

        const auto off = RunChildProcess(self, L"off");
        Check(off.ran && off.exited && off.status == kStatusAccessViolation,
            "fault parity: failsafe off reproduces the original null dereference",
            off.why);

        const auto on = RunChildProcess(self, L"on");
        Check(on.ran && on.exited && on.status == 0,
            "fault parity: failsafe on continues without the fix",
            on.why);
    }
}

int main(int argc, char** argv)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    if (argc >= 3 && std::string_view(argv[1]) == "--child")
        return RunChild(argv[2]);

    TestParser();
    TestResolver();
    TestGuards();
    TestMatchCount();
    TestFaultParity();

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
