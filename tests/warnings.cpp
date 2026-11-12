#include <common.hxx>

#include <cstdio>
#include <string>

import common;
import game;
import warnings;

// The `warnings` group's probes: the unsupported-version range with the
// 1.2.0.43 exception, the named-mutex single-instance claim, and the one-time
// FusionFix notice file.

namespace
{
    int failures = 0;
    int checks = 0;

    void Check(bool condition, std::string_view what)
    {
        ++checks;
        if (condition)
        {
            std::printf("PASS %.*s\n", static_cast<int>(what.size()), what.data());
            return;
        }
        ++failures;
        std::printf("FAIL %.*s\n", static_cast<int>(what.size()), what.data());
    }

    void TestVersion()
    {
        Check(Warnings::IsUnsupportedVersion({ 1, 2, 0, 1 }),
            "version: 1.2.0.1 is refused");
        Check(Warnings::IsUnsupportedVersion({ 1, 2, 9, 99 }),
            "version: 1.2.9.99 is refused");
        Check(!Warnings::IsUnsupportedVersion({ 1, 2, 0, 43 }),
            "version: 1.2.0.43 is the supported Complete Edition build");
        Check(!Warnings::IsUnsupportedVersion({ 1, 0, 8, 0 }),
            "version: 1.0.8.0 is supported");
        Check(!Warnings::IsUnsupportedVersion({ 1, 1, 3, 0 }),
            "version: 1.1.3.0 is supported");
    }

    void TestSingleInstance()
    {
        const std::wstring name = L"Local\\ZolikaPatch-opcep-selftest-" +
                                  std::to_wstring(GetCurrentProcessId());
        HANDLE first = CreateMutexW(nullptr, TRUE, name.c_str());
        Check(first != nullptr, "instance: the test claims the name");
        Check(!Warnings::ClaimSingleInstance(L"ZolikaPatch-opcep-selftest"),
            "instance: a second claim is refused");
        if (first != nullptr)
            CloseHandle(first);
    }

    void TestZMenu()
    {
        // The recovered threshold: `cmp [ebp+var_8], 35DD1h`.
        Check(Warnings::ZMenuVersionAccepted(0x35DD1u),
            "zmenu: 220625 (the recovered threshold) is accepted");
        Check(Warnings::ZMenuVersionAccepted(0x40000u), "zmenu: a newer library is accepted");
        Check(!Warnings::ZMenuVersionAccepted(0x35DD0u),
            "zmenu: one version below the threshold is refused");
        Check(!Warnings::ZMenuVersionAccepted(0u), "zmenu: a zero version is refused");
        // No ZMenuIV.asi is loaded into the test process, so the guard continues.
        Check(Warnings::CheckZMenuVersion(), "zmenu: an absent module is a continue");
    }

    void TestNotice()
    {
        const auto directory = std::filesystem::current_path() / "warnings_notice";
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::filesystem::create_directories(directory, ignored);

        const std::string_view gated[] = { "BikeFeetFix", "VSyncFix" };
        bool existed = true;
        const auto path = Warnings::WriteFusionFixNotice(directory, gated, existed);
        Check(!existed && !path.empty() && std::filesystem::exists(path),
            "notice: the first write creates the file");
        Check(std::filesystem::file_size(path) > 40, "notice: the file lists the options");

        bool existedAgain = false;
        const auto second = Warnings::WriteFusionFixNotice(directory, gated, existedAgain);
        Check(existedAgain && second == path, "notice: a later launch sees the file exists");

        std::filesystem::remove_all(directory, ignored);
    }
}

int main()
{
    TestVersion();
    TestSingleInstance();
    TestZMenu();
    TestNotice();

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
