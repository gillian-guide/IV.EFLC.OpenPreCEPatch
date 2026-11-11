#include <common.hxx>

#include <cstdio>

import config;

// The shipped OpenPreCEPatch INI: grouped sections, trailing `//` comments and
// a few lines longer than the original 256-byte getline buffer. LoadGrouped
// normalises those before the original grammar sees them; the flat parity file
// keeps the original (faulting) grammar untouched.

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
    const auto path = std::filesystem::path(ZP_SOURCE_DIR) /
                      "data" / "plugins" / "IV.EFLC.OpenPreCEPatch.ini";
    Check(std::filesystem::exists(path), "shipped INI: the file exists");

    const auto document = Feather::LoadGrouped(path);
    Check(document.Section("STABILITY") != nullptr, "shipped INI: STABILITY section parsed");
    Check(document.Section("FIXES") != nullptr, "shipped INI: FIXES section parsed");
    Check(document.Find("HeadlightConeOffsets", "Car1Name") != nullptr,
        "shipped INI: a section header with a trailing comment keeps its name");
    Check(document.Find("HeadlightConeOffsets", "Car1Offset") != nullptr,
        "shipped INI: Car1Offset parsed");
    Check(document.Find("VEHICLES", "MovingSteeringWheelSupport") != nullptr,
        "shipped INI: a 515-byte commented line parsed");

    if (const auto* value = document.Find("STABILITY", "BenchmarkFix"))
        Check(*value == "1", "shipped INI: BenchmarkFix is 1 without its comment");
    else
        Check(false, "shipped INI: BenchmarkFix is present");

    if (const auto* value = document.Find("PERFORMANCE", "NewMemorySystem"))
        Check(*value == "0", "shipped INI: NewMemorySystem is 0");
    else
        Check(false, "shipped INI: NewMemorySystem is present");

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
