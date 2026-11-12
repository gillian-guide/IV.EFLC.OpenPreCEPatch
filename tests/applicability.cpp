#include <common.hxx>

#include <cstdio>
#include <string>

import common;
import config;
import game;
import loader;
import options;

// The build-applicability boundary: every catalogue key refuses `unknown` and
// every build its mask does not carry, and `Loader::Enabled` agrees with its
// mask when the key is present. This is the "refuse to install outside the
// table" rule in code; tests/parity/applicability.tsv is the generated table.

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

    void TestMasks()
    {
        bool unknownRefused = true;
        bool outOfMaskRefused = true;
        bool inMaskAccepted = true;
        bool maskAgrees = true;

        for (const auto& option : Catalogue::All)
        {
            if (Catalogue::AppliesTo(option.key, Build::unknown))
                unknownRefused = false;

            for (uint32_t id = 1; id < 32; ++id)
            {
                const Build build = FromId(id);
                if (build == Build::unknown)
                    continue;
                const bool inMask = (option.builds & (1u << id)) != 0;
                if (Catalogue::AppliesTo(option.key, build) != inMask)
                    maskAgrees = false;
                if (!inMask && Catalogue::AppliesTo(option.key, build))
                    outOfMaskRefused = false;
                if (inMask && !Catalogue::AppliesTo(option.key, build))
                    inMaskAccepted = false;
            }
        }

        Check(unknownRefused, "matrix: an unknown build is in no key's mask");
        Check(outOfMaskRefused, "matrix: an excluded build is refused");
        Check(inMaskAccepted, "matrix: an included build is admitted");
        Check(maskAgrees, "matrix: AppliesTo answers the stored mask exactly");
    }

    void TestEnabled()
    {
        // One present key on a build in its mask and on one outside it. The
        // catalogue masks are the loader's refusal, so the INI value cannot
        // widen them.
        Config::Ini() = Feather::Parse(
            "[Options]\nBenchmarkFix=1\nSuperLODFix=1\nAimProjectileCrashFix=1\n");
        Loader::Current() = Build::v1080;

        Check(Loader::Enabled("BenchmarkFix") == false,
            "gate: BenchmarkFix is refused on 1.0.8.0 (its mask excludes it)");
        Check(Loader::Enabled("SuperLODFix") == true,
            "gate: SuperLODFix is admitted on 1.0.8.0");
        Check(Loader::Enabled("AimProjectileCrashFix") == true,
            "gate: an all-builds key is admitted");

        Loader::Current() = FromId(1);
        Check(Loader::Enabled("BenchmarkFix") == true,
            "gate: BenchmarkFix is admitted on 1.0.0.0");
        Check(Loader::Enabled("SuperLODFix") == false,
            "gate: SuperLODFix is refused on 1.0.0.0");

        Loader::Current() = Build::unknown;
        Check(!Loader::Enabled("AimProjectileCrashFix"),
            "gate: an unknown build installs nothing");
    }
}

int main()
{
    TestMasks();
    TestEnabled();

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
