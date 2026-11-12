#include <common.hxx>

#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

import common;
import config;
import options;

// The `config` group: OpenPreCEPatch's no-throw read, its out-of-range
// saturation, the guarded vehicle-table value and the FusionFix overlap gate.
// The 1:1 side is the already-proved behaviour of std::stoi/std::stof; this test
// checks that the group changes exactly those outcomes and nothing else.

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

    void TestParse()
    {
        Check(Config::ParseInt("  +2").ok && Config::ParseInt("  +2").value == 2,
            "parse: leading space and a sign");
        Check(Config::ParseInt("1abc").ok && Config::ParseInt("1abc").value == 1,
            "parse: the decimal prefix is the value");
        Check(Config::ParseInt("0x2").ok && Config::ParseInt("0x2").value == 0,
            "parse: base 10 stops at x");
        Check(!Config::ParseInt("abc").ok, "parse: no digits is invalid");
        Check(!Config::ParseInt("").ok, "parse: empty is invalid");
        Check(!Config::ParseInt("   ").ok, "parse: spaces only is invalid");
        Check(Config::ParseInt("999999999999").ok &&
                  Config::ParseInt("999999999999").value == std::numeric_limits<int32_t>::max(),
            "parse: past int saturates instead of throwing");
    }

    void TestInteger()
    {
        const auto document = Feather::Parse("[Options]\nA=abc\nB=999999999999\n");
        Check(Config::Integer(document, "A", "7") == 7,
            "config: a non-numeric value uses the default");
        Check(Config::Integer(document, "B", "7") == std::numeric_limits<int32_t>::max(),
            "config: an out-of-range value saturates");
        Check(Config::Integer(document, "Absent", "7") == 7,
            "config: an absent value still uses the default");
        Check(Config::Integer(Feather::Parse("[Options]\nA=1abc\n"), "A", "7") == 1,
            "config: the prefix grammar is strtol's");
    }

    void TestFloat()
    {
        const auto document = Feather::Parse("[Options]\nA=abc\n");
        Check(Config::Float(document, "Options", "A", "1.5") == 1.5f,
            "config: a non-numeric float uses the default");
    }

    void TestModelTable()
    {
        const auto document = Feather::Parse(
            "[PopupLightAngles]\nCar1Name=police\nCar1Angle=abc\n");
        const auto resolve = [](const char*) -> uint32_t { return 0x1234u; };

        bool threw = false;
        try
        {
            Config::ReadModelTables(document, resolve);
        }
        catch (...)
        {
            threw = true;
        }
        const auto table = Config::PopupLightAngles();
        Check(!threw, "config: a bad table row does not throw");
        Check(table.hashes[0] == 0x1234u, "config: the row's hash is still stored");
        Check(table.values[0] == 0.0f, "config: the unparseable value stays zero");
    }

    void TestSections()
    {
        // A migrated OpenPreCEPatch INI keeps its grouped sections; the key is
        // found even though it is not under [Options].
        Config::Ini() = Feather::Parse("[STABILITY]\nAimProjectileCrashFix=0\n");
        Check(Catalogue::Read("AimProjectileCrashFix") == 0,
            "sections: a grouped key is read");

        // [Options] is searched first, so a flat file wins over a stale group.
        Config::Ini() = Feather::Parse(
            "[Options]\nAimProjectileCrashFix=1\n[STABILITY]\nAimProjectileCrashFix=0\n");
        Check(Catalogue::Read("AimProjectileCrashFix") == 1,
            "sections: [Options] has precedence");

        // A key in no searched section still uses the arm default.
        Config::Ini() = Feather::Parse("[STABILITY]\nBikeFeetFix=1\n");
        Check(Catalogue::Read("AimProjectileCrashFix") == 1,
            "sections: an absent key keeps the default");
    }

    void TestFusionFixProbe()
    {
        // The release name changed between `GTAIV.FusionFix.asi` and
        // `GTAIV.EFLC.FusionFix.asi`, and the loader matches file names without
        // case, so the probe scans the directory for any `*fusionfix*.asi`.
        const auto directory = std::filesystem::current_path() / "fusionfix_probe";
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::filesystem::create_directories(directory, ignored);

        Check(Config::LooksLikeFusionFix(directory / "GTAIV.EFLC.FusionFix.asi"),
            "probe: the current release name is detected");
        Check(Config::LooksLikeFusionFix(directory / "GTAIV.FusionFix.asi"),
            "probe: the older release name is detected");
        Check(Config::LooksLikeFusionFix(directory / "gtaiv.eflc.fusionfix.ASI"),
            "probe: a differently-cased name and extension is detected");
        Check(!Config::LooksLikeFusionFix(directory / "GTAIV.EFLC.FusionFix.ini"),
            "probe: a same-named INI is not an ASI");
        Check(!Config::LooksLikeFusionFix(directory / "IV.EFLC.OpenPreCEPatch.asi"),
            "probe: another ASI is not FusionFix");

        Check(Config::DetectFusionFix(directory) == false,
            "probe: an empty directory has no FusionFix");
        std::ofstream(directory / "GTAIV.FusionFix.asi", std::ios::binary | std::ios::trunc)
            << "stub";
        Check(Config::DetectFusionFix(directory),
            "probe: the directory half finds the old release name");

        std::filesystem::remove_all(directory, ignored);
    }

    void TestFusionFixGate()
    {
        Config::Ini() = Feather::Parse(
            "[Options]\nBikeFeetFix=1\nAimProjectileCrashFix=1\n");

        Config::SetFusionFixPresent(true);
        Check(Catalogue::Read("BikeFeetFix") == 0,
            "config: a gated option reads as off with FusionFix present");
        Check(Catalogue::Read("AimProjectileCrashFix") == 1,
            "config: a non-overlapping option is unaffected");
        Config::SetFusionFixPresent(false);
        Check(Catalogue::Read("BikeFeetFix") == 1,
            "config: the gate reopens when FusionFix is absent");
    }
}

int main()
{
    TestParse();
    TestInteger();
    TestFloat();
    TestModelTable();
    TestSections();
    TestFusionFixProbe();
    TestFusionFixGate();

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
