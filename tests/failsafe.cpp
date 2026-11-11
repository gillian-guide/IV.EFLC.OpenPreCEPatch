#include <common.hxx>

#include <cstdio>
#include <string>

import common;
import game;
import patch;

// The install-time failsafes: a missing pointer, a site outside the image or an
// unresolved body refuses the install; a resolved one proceeds. The multi-hit
// guard refuses a partial arm.

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

    void TestGuards()
    {
        Check(Failsafe::Missing("probe", true), "guard: a missing value is skipped");
        Check(!Failsafe::Missing("probe", false), "guard: a present value is not skipped");

        // A synthetic image: 0x400000..0x401000.
        Game::Get().AttachImage(0x400000u, 0x1000u, std::filesystem::path(L"test.exe"), {});
        Check(Failsafe::Site("probe", 0x400000u, 5), "guard: an inside site is allowed");
        Check(!Failsafe::Site("probe", 0x400FFEu, 5), "guard: a site crossing the end is refused");
        Check(!Failsafe::Site("probe", 0x401000u, 1), "guard: the first byte past the image is refused");
        Check(!Failsafe::Site("probe", 0x3FFFFFu, 1), "guard: the byte before the image is refused");
        Check(!Failsafe::Site("probe", 0x400000u, 0), "guard: a zero-length write is refused");

        Check(!Failsafe::Body("probe", 0), "guard: a missing body is refused");
        Check(Failsafe::Body("probe", 0x1234), "guard: a resolved body is allowed");

        SetMissingForTest("forced");
        Check(Failsafe::Missing("forced", false),
            "guard: the test seam forces a missing value");
        ClearMissingForTest();
    }

    void TestMatchCount()
    {
        Check(ExpectMatchCount(2, 2, "probe"), "guard: the expected count installs");
        Check(!ExpectMatchCount(1, 2, "probe"), "guard: a short multi-hit arm is skipped");
        Check(!ExpectMatchCount(3, 2, "probe"), "guard: a long multi-hit arm is skipped");
    }
}

int main()
{
    TestGuards();
    TestMatchCount();
    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
