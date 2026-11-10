#include <common.hxx>

#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>

import common;
import config;
import improvements;
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

        SetImprovementsForTest(0);
        bool invalid = false;
        try
        {
            (void)Config::Integer(document, "A", "1");
        }
        catch (const std::invalid_argument&)
        {
            invalid = true;
        }
        Check(invalid, "1:1: a non-numeric value throws invalid_argument");

        bool range = false;
        try
        {
            (void)Config::Integer(document, "B", "1");
        }
        catch (const std::out_of_range&)
        {
            range = true;
        }
        Check(range, "1:1: an out-of-range value throws out_of_range");

        SetImprovementsForTest(Improve(Improvement::config));
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
        SetImprovementsForTest(0);
        bool invalid = false;
        try
        {
            (void)Config::Float(document, "Options", "A", "1.5");
        }
        catch (const std::invalid_argument&)
        {
            invalid = true;
        }
        Check(invalid, "1:1: a non-numeric float throws invalid_argument");

        SetImprovementsForTest(Improve(Improvement::config));
        Check(Config::Float(document, "Options", "A", "1.5") == 1.5f,
            "config: a non-numeric float uses the default");
    }

    void TestModelTable()
    {
        const auto document = Feather::Parse(
            "[PopupLightAngles]\nCar1Name=police\nCar1Angle=abc\n");
        const auto resolve = [](const char*) -> uint32_t { return 0x1234u; };

        SetImprovementsForTest(0);
        bool invalid = false;
        try
        {
            Config::ReadModelTables(document, resolve);
        }
        catch (const std::invalid_argument&)
        {
            invalid = true;
        }
        Check(invalid, "1:1: a bad [PopupLightAngles] value throws");

        SetImprovementsForTest(Improve(Improvement::config));
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

    void TestFusionFixGate()
    {
        Config::Ini() = Feather::Parse(
            "[Options]\nBikeFeetFix=1\nAimProjectileCrashFix=1\n");

        SetImprovementsForTest(0);
        Config::SetFusionFixPresent(true);
        Check(Catalogue::Read("BikeFeetFix") == 1,
            "1:1: the FusionFix overlap gate is not applied");
        Config::SetFusionFixPresent(false);

        SetImprovementsForTest(Improve(Improvement::config));
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
    TestFusionFixGate();

    SetImprovementsForTest(0);
    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
