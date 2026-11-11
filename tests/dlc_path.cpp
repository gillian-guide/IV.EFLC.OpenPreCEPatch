#include <common.hxx>

#include <cstdio>
#include <string>

import loading;

// The `dlc_path` group's bound: the discovery body's path buffer is 0x104 bytes
// including the NUL, so 259 bytes fit and 260 do not.

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
}

int main()
{
    Check(Loading::DlcPathFits(std::string(259, 'a')),
        "dlc_path: a 259-byte path fits the 0x104 buffer");
    Check(!Loading::DlcPathFits(std::string(260, 'a')),
        "dlc_path: a 260-byte path does not fit");
    Check(!Loading::DlcPathFits(""), "dlc_path: an empty path is refused");
    Check(Loading::DlcPathFits("C:\\GTAIV\\GTAIV.exe"),
        "dlc_path: a normal install path fits");

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
