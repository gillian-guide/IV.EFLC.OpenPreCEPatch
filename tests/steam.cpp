#include <common.hxx>

#include <cstdio>
#include <string>

import common;
import steam;

// The Steam achievement bridge's recovered id -> Steam name table, and the
// no-op path when the process is not the game (the native test process has no
// xlive import and no steam_api.dll next to it).

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

    void TestNames()
    {
        Check(std::string_view(Steam::AchievementNameForTest(1)) == "THETOUR",
            "steam: id 1 is THE TOUR");
        Check(std::string_view(Steam::AchievementNameForTest(40)) == "GAMECOMPLETION100",
            "steam: id 40 is 100% completion");
        Check(std::string_view(Steam::AchievementNameForTest(41)) == "MPTEAMWIN1",
            "steam: id 41 starts the multiplayer block");
        Check(std::string_view(Steam::AchievementNameForTest(51)) == "E1INTRO",
            "steam: id 51 starts TLAD");
        Check(std::string_view(Steam::AchievementNameForTest(65)) == "E2ALLFLAIR",
            "steam: id 65 is the last TBoGT achievement");
        Check(Steam::AchievementNameForTest(0) == nullptr, "steam: id 0 has no name");
        Check(Steam::AchievementNameForTest(66) == nullptr, "steam: id 66 has no name");
    }

    void TestInstallRefusal()
    {
        // The test executable is not the game image, so the bridge refuses; a
        // second call is the same answer, not an install.
        Check(!Steam::Install(), "steam: the bridge refuses a non-game process");
        Check(!Steam::Install(), "steam: the refusal is stable");
    }
}

int main()
{
    TestNames();
    TestInstallRefusal();

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
