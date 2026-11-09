#include <common.hxx>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>

import common;
import config;
import game;
import shared;

// The two per-model light tables the original's loader preamble
// (`sub_100523C0`, 0x10052419..0x10052D9B) fills from `[PopupLightAngles]` and
// `[HeadlightConeOffsets]`. No game and no ASI load: the reader is a pure function
// of a `Feather::Document` and the name resolver, so every recovered grammar rule
// and every recovered fault is pinned here directly.
//
// The hash the reader stores is whatever the resolver answers; `StubHash` is used
// for the grammar cases so a table expectation never depends on the hash
// algorithm, and `TestOriginalResolver` runs the same cases through the tree's
// transcribed `sub_10003E50` (`Shared::ValueResolver()`) against a reference
// implementation of its documented body.

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

    // A resolver that is deliberately not the original's: the reader must store
    // exactly what it is handed.
    uint32_t StubHash(const char* name)
    {
        uint32_t hash = 2166136261u;
        for (const auto* at = reinterpret_cast<const unsigned char*>(name); *at != 0; ++at)
            hash = (hash ^ *at) * 16777619u;
        return hash;
    }

    // sub_10003E50's body, written from the decompiled function, as the oracle for
    // the tree's transcribed resolver. `hash:` with a non-zero number is that
    // number; anything else is lower-cased and hashed, with a leading and trailing
    // quote stripped and `\` read as `/`, then the two-step final mix.
    uint32_t ReferenceHash(const char* text)
    {
        if (_strnicmp(text, "hash:", 5) == 0)
        {
            const auto number = std::strtoul(text + 5, nullptr, 0);
            if (number != 0)
                return static_cast<uint32_t>(number);
        }

        const bool quoted = text[0] == '"';
        const char* at = quoted ? text + 1 : text;
        uint32_t hash = 0;
        while (*at != 0)
        {
            if (quoted && *at == '"')
                break;
            const auto byte = static_cast<unsigned char>(*at);
            int32_t value = static_cast<signed char>(byte);  // the body's `movsx eax, al`
            if (byte >= 'A' && byte <= 'Z')
                value = static_cast<int32_t>(byte) + 0x20;
            else if (byte == '\\')
                value = '/';
            const uint32_t mixed = 1025u * (hash + static_cast<uint32_t>(value));
            hash = mixed ^ (mixed >> 6);
            ++at;
        }

        const uint32_t mixed = 9u * hash;
        const uint32_t value = 32769u * (mixed ^ (mixed >> 11));
        return value >= 2u ? value : value + 2u;
    }

    Config::NameResolver OriginalResolver()
    {
        return reinterpret_cast<Config::NameResolver>(Shared::ValueResolver());
    }

    struct Outcome
    {
        bool invalid = false;
        bool outOfRange = false;
        bool other = false;

        bool clean() const { return !invalid && !outOfRange && !other; }
    };

    // A throwing read leaves the half-filled tables behind, so every case here
    // catches the fault instead of letting it end the test.
    Outcome Read(const Feather::Document& document, Config::NameResolver resolve = StubHash)
    {
        Outcome outcome;
        try
        {
            Config::ReadModelTables(document, resolve);
        }
        catch (const std::invalid_argument&)
        {
            outcome.invalid = true;
        }
        catch (const std::out_of_range&)
        {
            outcome.outOfRange = true;
        }
        catch (...)
        {
            outcome.other = true;
        }
        return outcome;
    }

    bool Empty(const Config::ModelTable& table)
    {
        for (size_t index = 0; index < Config::ModelTableSlots; ++index)
        {
            if (table.hashes[index] != 0u || std::bit_cast<uint32_t>(table.values[index]) != 0u)
                return false;
        }
        return true;
    }

    uint32_t Bits(float value)
    {
        return std::bit_cast<uint32_t>(value);
    }

    void TestEmpty()
    {
        // The static tables start zeroed, exactly like the original's .bss words.
        Check(Empty(Config::HeadlightConeOffsets()) && Empty(Config::PopupLightAngles()),
            "tables: both tables start empty");

        // A document with no light sections at all reads nothing. That is the
        // shipped INI's shape: the sections exist but no row does.
        auto outcome = Read(Feather::Parse("[Options]\nForceCarHeadlightShadows=1\n"));
        Check(outcome.clean() && Empty(Config::HeadlightConeOffsets()) && Empty(Config::PopupLightAngles()),
            "tables: an absent section leaves both tables empty");

        // A section header with no rows is the same thing: the original's
        // `setSection` succeeds and the 512 lookups all miss.
        outcome = Read(Feather::Parse("[HeadlightConeOffsets]\n; nothing here\n[PopupLightAngles]\n"));
        Check(outcome.clean() && Empty(Config::HeadlightConeOffsets()) && Empty(Config::PopupLightAngles()),
            "tables: an empty section leaves both tables empty");

        // An empty value is not stored by the parser, so `Car1Name=` is skipped
        // and the row's value is never looked at -- even though one is present.
        outcome = Read(Feather::Parse("[HeadlightConeOffsets]\nCar1Name=\nCar1Offset=-3\n"));
        Check(outcome.clean() && Empty(Config::HeadlightConeOffsets()),
            "tables: an empty name skips the row, value and all");

        // A populated read followed by an empty one starts from zero again.
        Read(Feather::Parse("[HeadlightConeOffsets]\nCar1Name=admiral\nCar1Offset=-5\n"));
        Check(!Empty(Config::HeadlightConeOffsets()), "tables: a populated row is stored");
        Read(Feather::Parse("[Options]\n"));
        Check(Empty(Config::HeadlightConeOffsets()), "tables: a new read starts from zero");
    }

    void TestHeadlightOffsets()
    {
        // The live game_refs/ZolikaPatch.ini's rows: integers and a fraction, all
        // stored raw ("admiral" -5, "airtug" 5, "schafter" 3.5).
        auto outcome = Read(Feather::Parse(
            "[HeadlightConeOffsets]\n"
            "Car1Name=admiral\n"
            "Car1Offset=-5\n"
            "Car2Name=airtug\n"
            "Car2Offset=5\n"
            "Car3Name=schafter\n"
            "Car3Offset=3.5\n"));

        const auto& table = Config::HeadlightConeOffsets();
        Check(outcome.clean(), "offsets: a numeric section reads without a fault");
        Check(table.hashes[0] == StubHash("admiral") && table.values[0] == -5.0f,
            "offsets: Car1Name/Car1Offset fill slot 0 (admiral -5)");
        Check(table.hashes[1] == StubHash("airtug") && table.values[1] == 5.0f,
            "offsets: Car2Name/Car2Offset fill slot 1 (airtug 5)");
        Check(table.hashes[2] == StubHash("schafter") && table.values[2] == 3.5f,
            "offsets: a fractional Offset is stored as parsed (schafter 3.5)");
        Check(table.hashes[3] == 0u && table.values[3] == 0.0f, "offsets: the next slot stays empty");

        // The parser keeps the space after '=' and std::stof reads through it, as
        // it does in the original's inlined strtof.
        Read(Feather::Parse("[HeadlightConeOffsets]\nCar1Name=admiral\nCar1Offset= -5.5\n"));
        Check(Config::HeadlightConeOffsets().values[0] == -5.5f,
            "offsets: a leading space is read through, like std::stof");

        // The ordinal is the bare decimal: Car01Name is a different key and is
        // never asked for, and the case-insensitive section/key grammar of the INI
        // does not apply (Feather is case sensitive).
        outcome = Read(Feather::Parse(
            "[HeadlightConeOffsets]\n"
            "Car01Name=admiral\n"
            "Car1Offset=-5\n"
            "car2Name=airtug\n"
            "Car513Name=bus\n"
            "Car513Offset=-4\n"
            "Car0Name=bus\n"));
        Check(outcome.clean() && Empty(Config::HeadlightConeOffsets()),
            "offsets: Car01/Car0/Car513/lower-case keys are not the recovered grammar");
    }

    void TestPopupLightAngles()
    {
        auto outcome = Read(Feather::Parse(
            "[PopupLightAngles]\n"
            "Car1Name=admiral\n"
            "Car1Angle=90\n"
            "Car2Name=airtug\n"
            "Car2Angle=180\n"
            "Car3Name=schafter\n"
            "Car3Angle=45\n"));

        const auto& table = Config::PopupLightAngles();
        Check(outcome.clean(), "angles: a numeric section reads without a fault");
        Check(table.hashes[0] == StubHash("admiral") && Bits(table.values[0]) == 0x3FC90FDBu,
            "angles: 90 degrees is stored as pi/2 (0x3FC90FDB)");
        Check(Bits(table.values[1]) == 0x40490FDBu, "angles: 180 degrees is stored as pi (0x40490FDB)");
        Check(Bits(table.values[2]) == 0x3F490FDBu, "angles: 45 degrees is stored as pi/4 (0x3F490FDB)");

        // The conversion is done in double and narrowed once, as the original's
        // cvtps2pd/mulsd/divsd/cvtpd2ps pair does.
        Read(Feather::Parse("[PopupLightAngles]\nCar1Name=admiral\nCar1Angle=33\n"));
        const auto expected = static_cast<float>(static_cast<double>(33.0f) *
            std::bit_cast<double>(0x400921FB54442D18ull) / 180.0);
        Check(Bits(Config::PopupLightAngles().values[0]) == Bits(expected),
            "angles: the degrees-to-radians conversion is the original's double expression");
    }

    void TestMissingPair()
    {
        // A name with no value: the hash is stored and the value keeps the table's
        // zero, so the model resolves to 0.0f instead of the checker's default.
        auto outcome = Read(Feather::Parse("[HeadlightConeOffsets]\nCar2Name=boxville\n"));
        const auto& table = Config::HeadlightConeOffsets();
        Check(outcome.clean() && table.hashes[1] == StubHash("boxville"),
            "pairs: a name with no Offset still stores its hash");
        Check(table.values[1] == 0.0f,
            "pairs: ... and leaves the value at the table's zero, not the 2.0f default");

        // The reverse: a value with no name is never parsed, so even a value that
        // would throw is inert.
        outcome = Read(Feather::Parse("[HeadlightConeOffsets]\nCar3Offset=abc\n"));
        Check(outcome.clean() && table.hashes[2] == 0u && table.values[2] == 0.0f,
            "pairs: an Offset with no Name is never parsed and stores nothing");

        // A name the model store cannot resolve is still hashed and stored: the
        // resolver never fails, the row is simply inert.
        Read(Feather::Parse("[HeadlightConeOffsets]\nCar1Name=no_such_model_anywhere\nCar1Offset=4\n"));
        Check(table.hashes[0] == StubHash("no_such_model_anywhere") && table.values[0] == 4.0f,
            "pairs: an unresolvable name is hashed and stored inert");

        // A duplicate row keeps the last value, because the INI map does.
        Read(Feather::Parse("[HeadlightConeOffsets]\nCar1Name=admiral\nCar1Name=airtug\nCar1Offset=-5\n"));
        Check(table.hashes[0] == StubHash("airtug"),
            "pairs: a repeated Car1Name keeps the last value, as the INI parser does");
    }

    void TestHoles()
    {
        // Rows are stored at their own ordinal, not compacted. A missing row leaves
        // a zero hash, and the original's checker stops at the first zero -- so a
        // hole makes every later row unreachable. That is the recovered shape.
        auto outcome = Read(Feather::Parse("[HeadlightConeOffsets]\nCar1Name=a\nCar3Name=c\nCar3Offset=4\n"));
        const auto& table = Config::HeadlightConeOffsets();
        Check(outcome.clean(), "holes: a gap does not fault");
        Check(table.hashes[0] == StubHash("a") && table.hashes[1] == 0u && table.hashes[2] == StubHash("c"),
            "holes: a missing row leaves a zero hash and later rows keep their own slot");
        Check(table.values[2] == 4.0f, "holes: ... with their values in the same slot");
    }

    void TestFaults()
    {
        // std::stof with no digits: the original's unguarded parse (its
        // `_Xinvalid_argument("invalid stof argument")` at 0x100529BC).
        auto outcome = Read(Feather::Parse("[HeadlightConeOffsets]\nCar2Name=admiral\nCar2Offset=abc\n"));
        const auto& table = Config::HeadlightConeOffsets();
        Check(outcome.invalid, "faults: a value with no digits throws std::invalid_argument");
        Check(table.hashes[1] == StubHash("admiral") && table.values[1] == 0.0f,
            "faults: the hash was stored before the value was parsed (0x10052B0C before 0x10052C14)");

        // A space-only value is not empty to the parser, so it reaches std::stof
        // and throws -- the same fault `Key= ` gives std::stoi on the options side.
        outcome = Read(Feather::Parse("[HeadlightConeOffsets]\nCar2Name=x\nCar2Offset= \n"));
        Check(outcome.invalid, "faults: a space-only value throws std::invalid_argument");

        // strtof sets ERANGE: the original's `_Xout_of_range("stof argument out of
        // range")` at 0x100529B1.
        outcome = Read(Feather::Parse("[HeadlightConeOffsets]\nCar2Name=x\nCar2Offset=1e999\n"));
        Check(outcome.outOfRange, "faults: an out-of-range value throws std::out_of_range");
        Check(Config::HeadlightConeOffsets().hashes[1] == StubHash("x"),
            "faults: ... and the hash was stored first there too");

        // The angle section uses the same std::stof.
        outcome = Read(Feather::Parse("[PopupLightAngles]\nCar1Name=x\nCar1Angle=degrees\n"));
        Check(outcome.invalid, "faults: a non-numeric Angle throws the same way");

        // std::stof truncates at the first non-digit instead of throwing.
        outcome = Read(Feather::Parse("[HeadlightConeOffsets]\nCar2Name=admiral\nCar2Offset=5abc\n"));
        Check(outcome.clean() && Config::HeadlightConeOffsets().values[1] == 5.0f,
            "truncation: '5abc' is 5.0f, as std::stof stops at the first non-digit");
        Check(Config::HeadlightConeOffsets().hashes[1] == StubHash("admiral"),
            "truncation: the row is stored normally");
    }

    void TestSectionOrder()
    {
        // The original reads [PopupLightAngles] first (0x10052419) and
        // [HeadlightConeOffsets] second (0x10052901), whatever order the file has.
        // A throw in the first leaves the second untouched.
        auto outcome = Read(Feather::Parse(
            "[HeadlightConeOffsets]\n"
            "Car1Name=admiral\n"
            "Car1Offset=-5\n"
            "[PopupLightAngles]\n"
            "Car1Name=admiral\n"
            "Car1Angle=bad\n"));

        Check(outcome.invalid, "order: a malformed [PopupLightAngles] row throws");
        Check(Config::PopupLightAngles().hashes[0] == StubHash("admiral"),
            "order: the throwing section's hash is stored first");
        Check(Empty(Config::HeadlightConeOffsets()),
            "order: [HeadlightConeOffsets] is never reached, as in the original");

        // And when the second section is the one that throws, the first is already
        // complete.
        outcome = Read(Feather::Parse(
            "[PopupLightAngles]\n"
            "Car1Name=admiral\n"
            "Car1Angle=90\n"
            "[HeadlightConeOffsets]\n"
            "Car1Name=admiral\n"
            "Car1Offset=bad\n"));
        Check(outcome.invalid && Config::PopupLightAngles().hashes[0] == StubHash("admiral"),
            "order: [PopupLightAngles] is complete before [HeadlightConeOffsets] is read");
        Check(Config::HeadlightConeOffsets().hashes[0] == StubHash("admiral"),
            "order: the second section's hash is stored before its throw");
    }

    void TestFullTables()
    {
        // The row counter is 1..512 inclusive (`cmp ..., 200h / jl`): a full
        // section fills every slot, and Car513 is outside the loop.
        std::string offsets = "[HeadlightConeOffsets]\n";
        std::string angles = "[PopupLightAngles]\n";
        for (int index = 1; index <= 512; ++index)
        {
            const auto ordinal = std::to_string(index);
            offsets += "Car" + ordinal + "Name=m" + ordinal + "\nCar" + ordinal + "Offset=" + ordinal + "\n";
            angles += "Car" + ordinal + "Name=a" + ordinal + "\nCar" + ordinal + "Angle=" + ordinal + "\n";
        }
        offsets += "Car513Name=m513\nCar513Offset=513\n";
        angles += "Car513Name=a513\nCar513Angle=513\n";

        auto outcome = Read(Feather::Parse(offsets + angles));
        const auto& offsetTable = Config::HeadlightConeOffsets();
        const auto& angleTable = Config::PopupLightAngles();

        bool everyOffset = true;
        bool everyAngle = true;
        for (size_t index = 0; index < Config::ModelTableSlots; ++index)
        {
            const auto ordinal = std::to_string(index + 1);
            everyOffset = everyOffset &&
                offsetTable.hashes[index] == StubHash(("m" + ordinal).c_str()) &&
                offsetTable.values[index] == static_cast<float>(index + 1);
            everyAngle = everyAngle &&
                angleTable.hashes[index] == StubHash(("a" + ordinal).c_str());
        }
        Check(outcome.clean(), "full: 512 rows read without a fault or an overflow");
        Check(everyOffset, "full: every Offset slot holds its own row, in ordinal order");
        Check(everyAngle, "full: every Angle slot holds its own row, in ordinal order");
        Check(offsetTable.hashes[511] != StubHash("m513") && angleTable.hashes[511] != StubHash("a513"),
            "full: Car513 is outside the 1..512 bound and is ignored");
        Check(Bits(angleTable.values[179]) == 0x40490FDBu,
            "full: slot 180 holds 180 degrees in radians (pi)");
    }

    void TestOriginalResolver()
    {
        // The tree's transcribed sub_10003E50 has to be live for the reader: its two
        // CRT slots are published by Shared::Register(), after which the loader
        // preamble can call it.
        Game::Get().Initialise(GetThisModuleHandle());
        Shared::Register();
        const auto resolver = OriginalResolver();

        Check(resolver("admiral") == ReferenceHash("admiral"),
            "resolver: a plain name matches the documented body");
        Check(resolver("ADMIRAL") == resolver("admiral"), "resolver: names are case-folded");
        Check(resolver("\"admiral\"") == resolver("admiral"), "resolver: a quoted name loses its quotes");
        Check(resolver("a\\b") == resolver("a/b"), "resolver: a backslash is read as a slash");
        Check(resolver("hash:1234") == 1234u, "resolver: hash:1234 is the number, not a hash");
        Check(resolver("hash:xyz") == ReferenceHash("hash:xyz"),
            "resolver: a hash: prefix that is not a number is hashed as text");
        Check(resolver("") == 2u, "resolver: the empty name hashes to 2, as the body's +2 nudge does");

        // End to end: the reader hands the stored text to that resolver and stores
        // what it answers.
        auto outcome = Read(Feather::Parse(
            "[HeadlightConeOffsets]\n"
            "Car1Name=admiral\n"
            "Car1Offset=-5\n"
            "Car2Name=hash:1234\n"
            "Car2Offset=6\n"
            "Car3Name=\"admiral\"\n"
            "Car3Offset=1\n"), resolver);

        const auto& table = Config::HeadlightConeOffsets();
        Check(outcome.clean(), "resolver: the reader fills the table through sub_10003E50");
        Check(table.hashes[0] == resolver("admiral") && table.values[0] == -5.0f,
            "resolver: a plain model name is hashed by the original resolver");
        Check(table.hashes[1] == 1234u && table.values[1] == 6.0f,
            "resolver: hash:1234 is stored as 1234");
        Check(table.hashes[2] == resolver("admiral"),
            "resolver: a quoted model name resolves like the bare name");
    }
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    TestEmpty();
    TestHeadlightOffsets();
    TestPopupLightAngles();
    TestMissingPair();
    TestHoles();
    TestFaults();
    TestSectionOrder();
    TestFullTables();
    TestOriginalResolver();

    // Optional: read a real INI through the same reader. The ctest entry passes no
    // argument (documents only); a manual run against the user's ZolikaPatch.ini
    // proves the configuration the game actually sees is the one this parses.
    if (argc > 1)
    {
        Game::Get().Initialise(GetThisModuleHandle());
        Shared::Register();
        const auto document = Feather::Load(std::filesystem::path(argv[1]));

        size_t offsetRows = 0;
        size_t angleRows = 0;
        for (size_t index = 1; index <= Config::ModelTableSlots; ++index)
        {
            const auto ordinal = std::to_string(index);
            const auto* offsetName = document.Find("HeadlightConeOffsets", "Car" + ordinal + "Name");
            offsetRows += offsetName != nullptr && !offsetName->empty() ? 1 : 0;
            const auto* angleName = document.Find("PopupLightAngles", "Car" + ordinal + "Name");
            angleRows += angleName != nullptr && !angleName->empty() ? 1 : 0;
        }

        try
        {
            Config::ReadModelTables(document, OriginalResolver());
        }
        catch (const std::exception& error)
        {
            const auto line = std::format("ini {}: the reader threw {}", argv[1], error.what());
            Check(false, line);
            std::printf("%d check(s), %d failure(s)\n", checks, failures);
            return 1;
        }

        size_t offsetSlots = 0;
        size_t angleSlots = 0;
        for (size_t index = 0; index < Config::ModelTableSlots; ++index)
        {
            offsetSlots += Config::HeadlightConeOffsets().hashes[index] != 0u ? 1 : 0;
            angleSlots += Config::PopupLightAngles().hashes[index] != 0u ? 1 : 0;
        }
        const auto line = std::format("ini {}: {} offset row(s) -> {} slot(s), {} angle row(s) -> {} slot(s)",
            argv[1], offsetRows, offsetSlots, angleRows, angleSlots);
        Check(offsetRows == offsetSlots && angleRows == angleSlots, line);
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
