#include <common.hxx>

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <typeinfo>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import config;
import options;

// The preserved faults in the INI loader, asserted by their *observable* behaviour
// instead of by prose. `docs/parity.md` lists them; `zp_tests` asserted the two
// conversion throws as values only (a `bool threw`) and left the rest to prose. This
// test is the behavioural half:
//
//   1. std::invalid_argument out of a non-numeric [Options] value  -- in-process,
//      asserted by exact type (`typeid`), on `Key=abc` and on `Key=<space>`;
//   2. std::out_of_range out of a numeric value outside `int`      -- same;
//   3. a first-line UTF-8 BOM                                      -- in-process for
//      the parse shape (there is no BOM branch, so the BOM line is not a section
//      header and the parse goes on), and in a child process for the fault that
//      leaves: the header lost was `[Options]`, so the key line under it stores into
//      the null current section and dies;
//   4. a key line before the first [section] dereferences a null section pointer
//      -- a child process, because it kills the process: the parent asserts the
//      child's exit code and that its report stopped *after* the diagnostic line
//      that precedes the faulting parse, not before it;
//   5. a line at or over the 256-byte buffer                        -- the original's
//      own hang, preserved: a 255-byte line parses and its child exits 0, while a
//      256-byte line leaves the child spinning, so the test asserts the child did
//      *not* exit inside the timeout and then terminates it. A split, or a dropped
//      file, would fail that check instead of hanging the suite.
//
// The oracle's parser is `sub_10059D70`, feather 1.41's
//
//     char line[256];                       // FINI_BUFFER_SIZE 256
//     while (!file.eof()) { file.getline(line, 256); ... }
//
// -- `getline(&String, 0x100)` at `0x10059E0E`, the loop tail testing only MSVC's
// eofbit (`test byte ptr [eax+ecx+0Ch], 1 / jz`) at `0x1005A53E`, and no compare
// against 0xEF anywhere in the body: the source's `line[0] == 0xEF` is dead because
// `char` is signed (`movsx` at `0x10059E93`), so the oracle reads a BOM-prefixed
// first line as an ordinary line. A line that fills the buffer sets `failbit` --
// measured against MSVCP140, the library the oracle imports `getline` from -- and
// nothing clears it, so every later `getline` is a sentry no-op that leaves the same
// 255 bytes in the buffer: the parse never terminates. The same measurement puts the
// boundary at 255: a line of exactly 255 bytes is stored and its delimiter consumed.
//
// Nothing here needs the game or the built ASI: the loader's reader is called
// directly, exactly as `Loader::Enabled` calls it.
//
//     zp_fault_parity                                the suite
//     zp_fault_parity --child <report>               the key-before-section fault
//     zp_fault_parity --child-bom <report>           the BOM fault
//     zp_fault_parity --child-long <report> <bytes>  the line-buffer child

namespace
{
    // The original's "the process died on an access violation" status, which is
    // what a child process's exit code is when the fault is unhandled.
    constexpr DWORD kStatusAccessViolation = 0xC0000005u;

    int checks = 0;
    int failures = 0;

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

    // Evidence that is worth reading on a green run too (the observed exception
    // type and message, the children's exit codes, the catalogue size).
    void Detail(std::string_view text)
    {
        std::printf("     %.*s\n", static_cast<int>(text.size()), text.data());
    }

    const Option* CatalogueEntry(std::string_view key)
    {
        return Catalogue::Find(key);
    }

    // The document the loader would have parsed from a file, installed as `Config`'s
    // one global document -- the same slot `Config::Load()` fills and `Catalogue::Read`
    // reads, without touching the file system.
    void SetDocument(std::string_view text)
    {
        Config::Ini() = Feather::Parse(text);
    }

    std::string OptionText(std::string_view key, std::string_view value)
    {
        return std::string("[Options]\n") + std::string(key) + "=" + std::string(value) + "\n";
    }

    struct Raised
    {
        bool any = false;
        const std::type_info* type = nullptr;
        std::string what;
    };

    // Reads one option through the loader's own entry point (`Catalogue::Read` ->
    // `Config::Integer` -> `std::stoi`) and reports what left it. The catch is here,
    // one frame above the loader: nothing between the throw site and this call
    // catches, which is what "uncaught out of the loader" means.
    Raised ReadOption(std::string_view key)
    {
        try
        {
            (void)Catalogue::Read(key);
        }
        catch (const std::exception& error)
        {
            return Raised{ true, &typeid(error), error.what() };
        }
        catch (...)
        {
            return Raised{ true, nullptr, "<not a std::exception>" };
        }
        return Raised{};
    }

    std::string Describe(const Raised& raised)
    {
        if (!raised.any)
            return "nothing was thrown";
        return std::string("threw ") +
               (raised.type != nullptr ? raised.type->name() : "<unknown>") + " (\"" + raised.what + "\")";
    }

    int StoredInt(const Feather::Document& document, std::string_view key, int fallback)
    {
        return Config::Integer(document, key, std::to_string(fallback));
    }

    // --------------------------------------------------------------- faults 1, 2
    void CheckInvalidArgument()
    {
        const std::string key = "CarDynamicShadowFix";

        // The parse itself is quiet: `abc` is stored as text, and the loader converts
        // it only when it reads the key. The fault is the read, not the parse.
        SetDocument(OptionText(key, "abc"));
        Check(Config::Ini().Find("Options", key) != nullptr,
            "invalid_argument: the parse stores Key=abc as text");
        const auto letters = ReadOption(key);
        Check(letters.any && letters.type == &typeid(std::invalid_argument),
            "invalid_argument: Key=abc throws std::invalid_argument out of the loader",
            Describe(letters));
        Detail(Describe(letters));

        // `Key= ` -- one space. The value is not empty, so the grammar stores it, and
        // std::stoi finds no digit in it.
        SetDocument(OptionText(key, " "));
        const auto spaces = ReadOption(key);
        Check(spaces.any && spaces.type == &typeid(std::invalid_argument),
            "invalid_argument: Key=<space> also throws std::invalid_argument", Describe(spaces));
        Detail(Describe(spaces));

        // The boundary the fault does not cover: `Key=` is an empty value, which the
        // grammar does not store, so the arm's default is read and nothing throws.
        SetDocument(OptionText(key, ""));
        const auto empty = ReadOption(key);
        Check(!empty.any, "invalid_argument: Key= throws nothing (an empty value is not stored)",
            Describe(empty));
        const auto* option = CatalogueEntry(key);
        Check(option != nullptr && Catalogue::Read(key) == option->absent,
            "invalid_argument: ... Key= reads the arm's own default",
            std::format("read {}, default {}", Catalogue::Read(key),
                option != nullptr ? option->absent : 0));
    }

    void CheckOutOfRange()
    {
        const std::string key = "CarDynamicShadowFix";

        SetDocument(OptionText(key, "99999999999"));
        const auto large = ReadOption(key);
        Check(large.any && large.type == &typeid(std::out_of_range),
            "out_of_range: Key=99999999999 throws std::out_of_range out of the loader",
            Describe(large));
        Detail(Describe(large));

        SetDocument(OptionText(key, "-99999999999"));
        const auto negative = ReadOption(key);
        Check(negative.any && negative.type == &typeid(std::out_of_range),
            "out_of_range: a negative value below INT_MIN throws std::out_of_range",
            Describe(negative));
        Detail(Describe(negative));

        // The boundary is `int`, not something wider: INT_MAX and INT_MIN read, one
        // past each throws.
        SetDocument(OptionText(key, std::to_string(INT_MAX)));
        Check(Catalogue::Read(key) == INT_MAX,
            "out_of_range: INT_MAX is read, not thrown on");
        SetDocument(OptionText(key, std::to_string(INT_MIN)));
        Check(Catalogue::Read(key) == INT_MIN,
            "out_of_range: INT_MIN is read, not thrown on");
        SetDocument(OptionText(key, "2147483648"));
        const auto pastMax = ReadOption(key);
        Check(pastMax.any && pastMax.type == &typeid(std::out_of_range),
            "out_of_range: INT_MAX + 1 throws std::out_of_range", Describe(pastMax));
        SetDocument(OptionText(key, "-2147483649"));
        const auto pastMin = ReadOption(key);
        Check(pastMin.any && pastMin.type == &typeid(std::out_of_range),
            "out_of_range: INT_MIN - 1 throws std::out_of_range", Describe(pastMin));
    }

    // ------------------------------------------------------------------- fault 3
    void CheckBomInProcess()
    {
        // The BOM line has no '=', so the ordinary key-line path drops it and the parse
        // continues into the next line -- a whole-file drop would leave no section at
        // all, so this text tells the two apart without faulting. The oracle's
        // `[Options]` header is on that line, which is what makes the follow-up fatal.
        const auto document = Feather::Parse("\xEF\xBB\xBF[Options]\n[Other]\nA=1\n");
        Check(document.Section("Options") == nullptr,
            "bom: the BOM line is not a section header, so [Options] is lost");
        const auto* stored = document.Find("Other", "A");
        Check(stored != nullptr && *stored == "1",
            "bom: ... and the parse continues into the next line",
            stored != nullptr ? *stored : "absent");

        // The control, so the checks above cannot pass vacuously: the same text
        // without the BOM keeps [Options] as a section.
        const auto control = Feather::Parse("[Options]\n[Other]\nA=1\n");
        Check(control.Section("Options") != nullptr,
            "bom: control -- without the BOM the header is a section");
    }

    // ------------------------------------------------------------------- fault 4
    void CheckKeyBeforeSectionInProcess()
    {
        // The fault is the store into the selected section (feather's
        // `(*current)[key] = value`). Every line shape below is a key line before
        // the first section, and each one returns before that store, so this
        // process survives: the boundary is the store, not the section pointer read.
        {
            const auto document = Feather::Parse("Key=\n[Options]\nA=1\n");
            Check(document.Find("Options", "Key") == nullptr && StoredInt(document, "A", 0) == 1,
                "key-before-section: an empty value before the section never reaches the store");
        }
        {
            const auto document = Feather::Parse("just text\n[Options]\nA=1\n");
            Check(StoredInt(document, "A", 0) == 1,
                "key-before-section: a line without '=' before the section never reaches the store");
        }
        {
            const auto document = Feather::Parse("=1\n[Options]\nA=1\n");
            Check(StoredInt(document, "A", 0) == 1,
                "key-before-section: a line starting with '=' before the section never reaches the store");
        }
        {
            const auto document = Feather::Parse("# comment\n[Options]\nA=1\n");
            Check(StoredInt(document, "A", 0) == 1,
                "key-before-section: a comment before the section is skipped harmlessly");
        }
        {
            const auto document = Feather::Parse("[Options]\nKey=1\n");
            Check(StoredInt(document, "Key", 0) == 1,
                "key-before-section: the same key line *after* a section is stored normally");
        }
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
        bool exited = false;      // terminated inside its wait
        bool timedOut = false;    // still running when the wait expired
        DWORD status = 0;
        DWORD waitedMs = 0;
        std::vector<std::string> lines;
        std::string why;
    };

    // Runs one child mode and reads its report. A child that is still running when
    // `timeoutMs` expires is the long-line fault, not a hung suite: it is terminated
    // here and reported as `timedOut`.
    ChildResult RunChildProcess(const std::filesystem::path& self, const std::string& mode,
        const std::filesystem::path& report, const std::vector<std::wstring>& extra, DWORD timeoutMs)
    {
        ChildResult result;
        std::error_code ignored;
        std::filesystem::remove(report, ignored);

        if (self.empty())
        {
            result.why = "the test could not name its own executable";
            return result;
        }

        std::wstring command = L"\"" + self.wstring() + L"\" \"" +
            std::wstring(mode.begin(), mode.end()) + L"\" \"" + report.wstring() + L"\"";
        for (const auto& argument : extra)
            command += L" \"" + argument + L"\"";
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
        result.waitedMs = timeoutMs;

        const DWORD wait = WaitForSingleObject(process.hProcess, timeoutMs);
        if (wait == WAIT_OBJECT_0)
        {
            result.exited = true;
            GetExitCodeProcess(process.hProcess, &result.status);
        }
        else if (wait == WAIT_TIMEOUT)
        {
            result.timedOut = true;
            TerminateProcess(process.hProcess, 0xDEADu);
            WaitForSingleObject(process.hProcess, 10000);
        }
        else
        {
            result.why = std::format("WaitForSingleObject failed (error {})", GetLastError());
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        std::ifstream stream(report, std::ios::binary);
        std::string text;
        while (std::getline(stream, text))
        {
            while (!text.empty() && (text.back() == '\r' || text.back() == ' '))
                text.pop_back();
            result.lines.push_back(text);
        }
        return result;
    }

    std::string Joined(const std::vector<std::string>& lines)
    {
        std::string joined;
        for (const auto& text : lines)
            joined += (joined.empty() ? "" : ", ") + text;
        return joined.empty() ? "<empty>" : joined;
    }

    std::string StatusText(DWORD status)
    {
        char text[32] = {};
        std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned>(status));
        return text;
    }

    void CheckKeyBeforeSectionChild(const std::filesystem::path& self)
    {
        // The documented preserved fault: `(*current)[key] = value` with `current`
        // null. It kills the process, so it runs in a child. The child writes one
        // line before the faulting parse (after proving, in that same process, that
        // the parser reads a normal document) and one after a *successful* parse,
        // so the parent can tell "the parse faulted" from "the child died earlier".
        const auto report = std::filesystem::current_path() / "fault_parity_child.report";
        const auto child = RunChildProcess(self, "--child", report, {}, 30000);

        const std::vector<std::string> expected{ "PHASE=control-parsed", "PHASE=parse-start" };
        Check(child.ran && child.exited && child.lines == expected,
            "key-before-section: the child wrote its diagnostic line and then never returned",
            child.ran ? "report " + Joined(child.lines) : child.why);

        Check(child.ran && child.exited && child.status == kStatusAccessViolation,
            "key-before-section: the child died on an access violation (exit 0xC0000005)",
            child.ran ? std::format("exit {}", StatusText(child.status)) : child.why);
        Detail(std::format("key-before-section child exit {} (expected 0x{:08X})",
            StatusText(child.status), kStatusAccessViolation));
    }

    void CheckBomChild(const std::filesystem::path& self)
    {
        // The fault the BOM leaves: the swallowed line was the `[Options]` header, so
        // the key line under it is a key line before any section and stores into the
        // null current section. The child first records the header being swallowed
        // *without* faulting (the same parse shape as the in-process checks), then
        // enters the faulting parse, so the parent can tell the fault from an earlier
        // death.
        const auto report = std::filesystem::current_path() / "fault_parity_bom_child.report";
        const auto child = RunChildProcess(self, "--child-bom", report, {}, 30000);

        const std::vector<std::string> expected{
            "PHASE=control-parsed", "PHASE=bom-header-swallowed", "PHASE=parse-start" };
        Check(child.ran && child.exited && child.lines == expected,
            "bom: the child swallowed the BOM line's header and then never returned",
            child.ran ? "report " + Joined(child.lines) : child.why);

        Check(child.ran && child.exited && child.status == kStatusAccessViolation,
            "bom: the child died on the null-section store (exit 0xC0000005)",
            child.ran ? std::format("exit {}", StatusText(child.status)) : child.why);
        Detail(std::format("bom child exit {} (expected 0x{:08X})", StatusText(child.status),
            kStatusAccessViolation));
    }

    // ------------------------------------------------------------------- fault 5
    void CheckLongLineInProcess()
    {
        // 255 bytes is the longest line that fits: stored whole, the delimiter
        // consumed, so the line after it is read normally. This is the non-faulting
        // side of the boundary. (256 bytes is not parsed here -- it hangs, which is
        // exactly what the child below pins.)
        {
            const auto document = Feather::Parse("[Options]\nA=" + std::string(253, 'x') + "\nB=1\n");
            const auto* value = document.Find("Options", "A");
            Check(value != nullptr && value->size() == 253,
                "long line: a 255-byte line is kept whole, not split",
                value != nullptr ? std::to_string(value->size()) : "absent");
            Check(StoredInt(document, "B", 0) == 1,
                "long line: ... and the line after it still parses");
        }
        // A 255-byte last line with no delimiter is still fine.
        {
            const auto document = Feather::Parse("[Options]\nA=" + std::string(253, 'x'));
            const auto* value = document.Find("Options", "A");
            Check(value != nullptr && value->size() == 253,
                "long line: a 255-byte line at end of file is kept whole",
                value != nullptr ? std::to_string(value->size()) : "absent");
        }
    }

    void CheckLongLineChild(const std::filesystem::path& self)
    {
        const auto report = std::filesystem::current_path() / "fault_parity_long_child.report";

        // 255 bytes: the parse returns, the child exits 0, and the record it wrote
        // proves both the parse and the values.
        const auto parsed = RunChildProcess(self, "--child-long", report, { L"255" }, 30000);
        const std::vector<std::string> expected{ "PHASE=parse-start", "PHASE=parse-returned A=253 B=1" };
        Check(parsed.ran && parsed.exited && parsed.status == 0 && parsed.lines == expected,
            "long line: a 255-byte line parses and the child exits 0",
            parsed.ran ? std::format("report {}, exit {}", Joined(parsed.lines), StatusText(parsed.status))
                       : parsed.why);

        // 256 bytes: the buffer fills, failbit is never cleared, and the parse re-reads
        // the same 255 bytes for ever. The child must still be running when the wait
        // expires; if it exited, the tree diverged again (it split the line, or dropped
        // the file) and this check fails -- it does not hang the suite, because the
        // child is terminated here either way.
        constexpr DWORD kSpinTimeoutMs = 5000;
        const auto spun = RunChildProcess(self, "--child-long", report, { L"256" }, kSpinTimeoutMs);
        Check(spun.ran && spun.timedOut && !spun.exited,
            "long line: a 256-byte line makes the parse spin (the child did not exit in 5 s)",
            spun.ran ? std::format("exit {} after {} ms, report {}", StatusText(spun.status),
                spun.waitedMs, Joined(spun.lines)) : spun.why);
        Check(spun.ran && Joined(spun.lines) == "PHASE=parse-start",
            "long line: ... and the child reached the parse (no earlier fault, no early return)",
            Joined(spun.lines));
        Detail(std::format("256-byte child: {} after {} ms, terminated by the test",
            spun.timedOut && !spun.exited ? "still spinning" : "exited", spun.waitedMs));
    }

    // ------------------------------------------------------------------- the children
    void AppendLine(const std::filesystem::path& report, std::string_view line)
    {
        std::ofstream stream(report, std::ios::binary | std::ios::app);
        stream << line << "\n";
    }

    int RunChild(const std::filesystem::path& report)
    {
        // The control: this same process parses a normal document. Written first, so
        // the parent knows the report was writable and the parser ran before the
        // faulting parse.
        const auto control = Feather::Parse("[Options]\nA=1\n");
        const auto* value = control.Find("Options", "A");
        AppendLine(report, value != nullptr && *value == "1" ? "PHASE=control-parsed" : "PHASE=control-failed");

        // The diagnostic point: the faulting parse starts here.
        AppendLine(report, "PHASE=parse-start");

        // A key line before the first section: the store into the null section must
        // die here.
        const auto document = Feather::Parse("Key=1\n[Options]\nA=1\n");
        AppendLine(report, std::format("PHASE=parse-returned sections={}", document.sections.size()));
        return 0;
    }

    int RunBomChild(const std::filesystem::path& report)
    {
        const auto control = Feather::Parse("[Options]\nA=1\n");
        const auto* a = control.Find("Options", "A");
        AppendLine(report, a != nullptr && *a == "1" ? "PHASE=control-parsed" : "PHASE=control-failed");

        // The shape the fault comes from, recorded without faulting: the BOM line is
        // dropped, `[Options]` is not a section, and the parse goes on into a later
        // section.
        const auto swallowed = Feather::Parse("\xEF\xBB\xBF[Options]\n[Other]\nB=1\n");
        const auto* b = swallowed.Find("Other", "B");
        AppendLine(report, swallowed.Section("Options") == nullptr && b != nullptr && *b == "1"
            ? "PHASE=bom-header-swallowed" : "PHASE=bom-header-kept");

        AppendLine(report, "PHASE=parse-start");

        // The faulting text: a key line under the lost header.
        const auto document = Feather::Parse("\xEF\xBB\xBF[Options]\nA=1\n");
        AppendLine(report, std::format("PHASE=parse-returned sections={}", document.sections.size()));
        return 0;
    }

    int RunLongChild(const std::filesystem::path& report, size_t lineBytes)
    {
        AppendLine(report, "PHASE=parse-start");

        // The line is `A=` plus the rest of `lineBytes`, so its total length is
        // exactly what the parent asked for.
        const size_t valueBytes = lineBytes >= 2 ? lineBytes - 2 : 0;
        const auto document = Feather::Parse("[Options]\nA=" + std::string(valueBytes, 'x') + "\nB=1\n");
        const auto* value = document.Find("Options", "A");
        AppendLine(report, std::format("PHASE=parse-returned A={} B={}",
            value != nullptr ? value->size() : 0, StoredInt(document, "B", 0)));
        return 0;
    }

    int Run()
    {
        const auto self = SelfPath();
        CheckInvalidArgument();
        CheckOutOfRange();
        CheckBomInProcess();
        CheckKeyBeforeSectionInProcess();
        CheckKeyBeforeSectionChild(self);
        CheckBomChild(self);
        CheckLongLineInProcess();
        CheckLongLineChild(self);
        return failures == 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    // This test kills a child process on purpose (and would kill its own process if
    // the child's case were ever run here), and one child is expected to spin until
    // it is terminated, so a fault must be a result, never a Windows "Application
    // Error" dialog on whoever runs the suite. The error mode is inherited by the
    // children, and each child calls this line too.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc >= 3 && std::strcmp(argv[1], "--child") == 0)
        return RunChild(std::filesystem::path(argv[2]));
    if (argc >= 3 && std::strcmp(argv[1], "--child-bom") == 0)
        return RunBomChild(std::filesystem::path(argv[2]));
    if (argc >= 4 && std::strcmp(argv[1], "--child-long") == 0)
        return RunLongChild(std::filesystem::path(argv[2]), std::strtoul(argv[3], nullptr, 10));

    try
    {
        Run();
    }
    catch (const std::exception& error)
    {
        Check(false, "the test itself threw", error.what());
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
