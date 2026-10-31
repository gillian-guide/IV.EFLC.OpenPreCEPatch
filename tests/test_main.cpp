// SPDX-License-Identifier: GPL-3.0-or-later
//
// Native tests for everything that does not need the game: INI parsing and the
// grouped/legacy lookup, build classification, the model catalogue, the vehicle
// light tables, the menu probes and the ZMenu guard.

#include "catalogue.hxx"
#include "common.hxx"
#include "config.hxx"
#include "feature_registry.hxx"
#include "game.hxx"
#include "helpers.hxx"
#include "hooks.h"
import opp.log;
#include "menu.hxx"
#include "patches.hxx"
#include "pattern.hxx"
#include "vehicle_lights.hxx"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>


namespace
{
    int failures = 0;

#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            std::fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x);              \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

    void WriteFile(const std::filesystem::path& path, const char* text)
    {
        std::FILE* file = std::fopen(path.string().c_str(), "wb");
        CHECK(file != nullptr);
        if (file)
        {
            std::fputs(text, file);
            std::fclose(file);
        }
    }

    void TestConfig()
    {
        const std::filesystem::path path = "opcep_test.ini";
        WriteFile(path,
                  "[MAIN]\nComment = test\n\n[STABILITY]\nAimProjectileCrashFix = 1\n"
                  "BlackscreenFix = 2\n\n[GRAPHICS]\nCarDynamicShadowFix = 0\n");
        opp::Config config(path);
        CHECK(config.loaded());
        CHECK(config.Option("AimProjectileCrashFix", "0"));
        CHECK(!config.Option("BlackscreenFix", "1")); // only exactly 1 enables
        CHECK(!config.Option("CarDynamicShadowFix", "1"));
        CHECK(config.Option("SkipIntro", "1")); // absent -> arm default
        CHECK(!config.Option("VSyncFix", "0"));
        CHECK(config.Has("STABILITY", "AimProjectileCrashFix"));
        CHECK(!config.Has("STABILITY", "MissingKey"));
        CHECK(config.String("MAIN", "Comment", "") == "test");
        std::filesystem::remove(path);

        // Old flat layout: the same key is found through [Options].
        const std::filesystem::path legacy = "opcep_test_legacy.ini";
        WriteFile(legacy, "[Options]\nMouseFix=1\nMiscFixes=0\n");
        opp::Config old(legacy);
        CHECK(old.Option("MouseFix", "0"));
        CHECK(old.Has("Options", "MiscFixes"));
        CHECK(!old.Option("MiscFixes", "1"));
        std::filesystem::remove(legacy);
    }

    void TestIntPrefix()
    {
        // std::stoi's base-10 grammar, without the throw.
        CHECK(opp::ParseIntPrefix("1").status == opp::IntStatus::ok);
        CHECK(opp::ParseIntPrefix("1").value == 1);
        CHECK(opp::ParseIntPrefix("1abc").value == 1);      // prefix accepted
        CHECK(opp::ParseIntPrefix("  +2").value == 2);
        CHECK(opp::ParseIntPrefix("-3").value == -3);
        CHECK(opp::ParseIntPrefix("0x2").value == 0);       // base 10 stops at 'x'
        CHECK(opp::ParseIntPrefix("").status == opp::IntStatus::empty);
        CHECK(opp::ParseIntPrefix("   ").status == opp::IntStatus::empty);
        CHECK(opp::ParseIntPrefix("abc").status == opp::IntStatus::not_a_number);
        CHECK(opp::ParseIntPrefix("").status != opp::IntStatus::ok);
        CHECK(opp::FlagExactlyOne("1"));
        CHECK(!opp::FlagExactlyOne("2"));
        CHECK(!opp::FlagExactlyOne("0"));
        CHECK(!opp::FlagExactlyOne(""));
        CHECK(!opp::FlagExactlyOne("abc"));
        // A value far outside int range is not "1" and must not throw.
        CHECK(!opp::FlagExactlyOne("999999999999999999999"));
    }

    void TestConfigSemantics()
    {
        const std::filesystem::path path = "opcep_semantics.ini";
        WriteFile(path,
                  "\xEF\xBB\xBF"  // UTF-8 BOM: the first section must still parse
                  "[Options]\n"
                  "Dup = 1\n"
                  "Dup = 0\n"                 // last duplicate wins
                  "Prec = 2\n"                // [Options] is searched before [STABILITY]
                  "Inline = 1 // note\n"      // space-preceded comment is stripped
                  "NoSpace = 1//note\n"       // not space-preceded: part of the value
                  "Quoted = \"1\"\n"
                  "Empty =\n"                 // present, unparseable
                  "Bad = abc\n"               // present, unparseable
                  "Escaped\\=Key = 1\n"
                  "; whole-line comment\n"
                  "# another\n"
                  "// slash comment\n"
                  "[STABILITY]\n"
                  "Prec = 1\n"                // loses to [Options]
                  "OnlySecond = 1\n"
                  "[Options]\n"               // a second block merges, last value wins
                  "Merged = 1\n");

        opp::Config config(path);
        CHECK(config.loaded());
        CHECK(!config.Option("Dup", "1"));                 // 0 wins over 1
        CHECK(!config.Option("Prec", "0"));                // [Options] order, not file order
        CHECK(config.Option("Inline", "0"));
        CHECK(config.Option("NoSpace", "0"));              // "1//note" -> prefix 1
        CHECK(config.Option("Quoted", "0"));
        CHECK(config.Option("Empty", "1"));                // invalid -> arm default
        CHECK(!config.Option("Empty", "0"));
        CHECK(config.Option("Bad", "1"));                  // no throw, default used
        CHECK(config.Option("Escaped=Key", "0"));          // `\=` is restored in the key
        CHECK(config.Has("STABILITY", "OnlySecond"));
        CHECK(config.Option("OnlySecond", "0"));
        CHECK(config.Has("Options", "Merged"));            // duplicate block merges

        // Sort order and case sensitivity are the reader's, as before.
        CHECK(!config.Has("stability", "OnlySecond"));
        CHECK(!config.Has("STABILITY", "onlysecond"));
        CHECK(config.InvalidValues() >= 2);                 // Empty + Bad
        std::filesystem::remove(path);

        // CRLF: the bundled reader drops the CR, so the values stay clean.
        const std::filesystem::path crlf = "opcep_semantics_crlf.ini";
        WriteFile(crlf, "[Options]\r\nCrlf = 1\r\nText = a b\r\n");
        opp::Config windows(crlf);
        CHECK(windows.loaded());
        CHECK(windows.Option("Crlf", "0"));
        CHECK(windows.String("Options", "Text", "") == "a b");
        std::filesystem::remove(crlf);

        // mINI's inline comment rule: a space-preceded `;`, `//` or `#` starts a
        // comment and is stripped after the first `=`; without the space it is
        // part of the value (TestConfig's NoSpace case).
        const std::filesystem::path comments = "opcep_semantics_comments.ini";
        WriteFile(comments,
                  "[Options]\n"
                  "Hash = 1 # note\n"
                  "Semi = 1 ; note\n"
                  "Slash = 1 // note\n"
                  "// whole-line slash\n"
                  "# whole-line hash\n"
                  "; whole-line semi\n");
        opp::Config commented(comments);
        CHECK(commented.loaded());
        CHECK(commented.Option("Hash", "0"));
        CHECK(commented.Option("Semi", "0"));
        CHECK(commented.Option("Slash", "0"));
        CHECK(!commented.Has("Options", "// whole-line slash"));
        std::filesystem::remove(comments);

        // A missing file loads nothing and every option keeps its default.
        opp::Config missing("opcep_semantics_missing.ini");
        CHECK(!missing.loaded());
        CHECK(!missing.Has("Options", "Dup"));
        CHECK(missing.Option("Dup", "1"));
        CHECK(!missing.Option("Dup", "0"));
        CHECK(missing.String("", "OverrideGTADatPathTo", "common:/data/gta.dat") ==
              "common:/data/gta.dat");

        // The section policy is data: [Options] first, then the subsystem groups.
        const auto sections = opp::OptionSections();
        CHECK(sections.size() == 15);
        CHECK(sections.front() == "Options");
        CHECK(sections.back() == "DEBUG");
    }

    void TestShippedTemplate()
    {
        // The flat option lookup is only correct while a key is unique across
        // the searched sections; this census pins that invariant (research 05
        // S9/T16) and that the shipped defaults are parseable flags.
        const std::filesystem::path templatePath =
            std::filesystem::path(OPCEP_DATA_DIR) / "IV.EFLC.OpenPreCEPatch.ini";
        opp::Config config(templatePath);
        CHECK(config.loaded());

        // Walk the file's own sections: every key may occur in one section only.
        std::FILE* file = std::fopen(templatePath.string().c_str(), "rb");
        CHECK(file != nullptr);
        if (file)
        {
            const auto sections = opp::OptionSections();
            std::string current;
            std::vector<std::pair<std::string, std::string>> seen;
            char line[512];
            while (std::fgets(line, sizeof(line), file))
            {
                std::string text(line);
                while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                    text.pop_back();
                if (text.empty() || text[0] == ';')
                    continue;
                if (text[0] == '[')
                {
                    const size_t close = text.find(']');
                    if (close != std::string::npos)
                        current = text.substr(1, close - 1);
                    continue;
                }
                const size_t equals = text.find('=');
                if (equals == std::string::npos)
                    continue;
                std::string key = text.substr(0, equals);
                while (!key.empty() && key.back() == ' ')
                    key.pop_back();

                bool searched = false;
                for (const std::string_view section : sections)
                    searched = searched || current == section;
                if (!searched)
                    continue;  // the two vehicle tables are read section-qualified

                for (const auto& entry : seen)
                    CHECK(!(entry.first == key && entry.second != current));
                seen.emplace_back(key, current);
            }
            std::fclose(file);
        }

        // Shipped defaults, read through the same accessor the installers use.
        CHECK(config.Option("LoadDLCs", "0"));
        CHECK(config.Option("RemoveModChecks", "0"));
        CHECK(!config.Option("SkipIntro", "0"));  // not in the grouped template
    }

    void TestIniImport()
    {
        const std::filesystem::path dir = "opcep_import_test";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        WriteFile(dir / "ZolikaPatch.ini",
                  "[Options]\nMouseFix=0\nMiscFixes=0\n"
                  "OverrideGTADatPathTo=common:/data/custom.dat\n"
                  "[PopupLightAngles]\nCar1Name=police\nCar1Angle=90\n");

        const std::filesystem::path created = opp::EnsureConfigFile(dir);
        CHECK(created == dir / "IV.EFLC.OpenPreCEPatch.ini");
        CHECK(std::filesystem::exists(created));

        opp::Config config(created);
        CHECK(!config.Option("MouseFix", "1")); // old value 0
        CHECK(config.Option("CombinedRadioFreezeFix", "1")); // template default
        CHECK(config.String("MISC", "OverrideGTADatPathTo", "") ==
              "common:/data/custom.dat");
        CHECK(config.String("PopupLightAngles", "Car1Name", "") == "police");
        std::filesystem::remove_all(dir);

        const std::filesystem::path fresh = "opcep_import_fresh";
        std::filesystem::remove_all(fresh);
        std::filesystem::create_directories(fresh);
        const std::filesystem::path defaultIni = opp::EnsureConfigFile(fresh);
        CHECK(defaultIni == fresh / "IV.EFLC.OpenPreCEPatch.ini");
        opp::Config defaults(defaultIni);
        CHECK(defaults.Option("CombinedRadioFreezeFix", "0"));
        std::filesystem::remove_all(fresh);
    }

    void TestLegacyUnsupportedKeys()
    {
        // The legacy import is a text transform over the template, so a key the
        // template has no row for cannot be carried over. It must be reported
        // and logged instead of disappearing (missed-findings audit).
        const std::filesystem::path dir = "opcep_legacy_unsupported";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        WriteFile(dir / "ZolikaPatch.ini",
                  "[Options]\nMouseFix=0\nRemoveUselessChecks=1\nNoPhysicsSleep=1\n"
                  "SkipLauncher=1\nUnknownOldKey=1\n"
                  "[PopupLightAngles]\nCar1Name=police\nCar1Angle=90\n");

        opp::LegacyImportReport report;
        const std::filesystem::path created = opp::EnsureConfigFile(dir, &report);
        CHECK(created == dir / "IV.EFLC.OpenPreCEPatch.ini");

        const auto dropped = [&report](const char* key) -> const char* {
            for (const auto& entry : report.dropped)
            {
                if (entry.key == key)
                    return entry.reason.c_str();
            }
            return nullptr;
        };
        CHECK(dropped("RemoveUselessChecks") != nullptr);
        CHECK(std::strcmp(dropped("RemoveUselessChecks"), "not implemented in OpenPreCEPatch") == 0);
        CHECK(std::strcmp(dropped("NoPhysicsSleep"), "not implemented in OpenPreCEPatch") == 0);
        CHECK(std::strcmp(dropped("SkipLauncher"), "provided by the XLivelessAddon companion") == 0);
        CHECK(std::strcmp(dropped("UnknownOldKey"), "no row in the grouped INI") == 0);
        // Handled: the option is carried over, the vehicle table row is ported.
        CHECK(dropped("MouseFix") == nullptr);
        CHECK(dropped("Car1Name") == nullptr);

        opp::Config config(created);
        CHECK(!config.Has("Options", "RemoveUselessChecks"));
        CHECK(!config.Option("MouseFix", "1"));  // the old value, 0
        CHECK(config.String("PopupLightAngles", "Car1Name", "") == "police");

        CHECK(opp::UnsupportedLegacyKeys().size() == 7);
        CHECK(opp::CompanionLegacyKeys().size() == 7);
        std::filesystem::remove_all(dir);
    }

    void TestRemoveGFWLUpdatePopupSites()
    {
        // The four xlive.dll dwords the original's RemoveGFWLUpdatePopup handler
        // zeroes (0x10005421) must stay at the recovered RVAs. A previous rename
        // used 0x6B5B50/0x6B5B70 (wrong by 0x580) and the game-image write-set
        // tools could not see it because xlive.dll is not the game image.
        CHECK(::kXliveUpdateFlag0Rva == 6748016u);
        CHECK(::kXliveUpdateFlag1Rva == 6748020u);
        CHECK(::kXliveUpdateFlag2Rva == 7034320u);
        CHECK(::kXliveUpdateFlag3Rva == 7034352u);
        CHECK(::kXliveUpdateFlag0Rva == 0x66F770u);
        CHECK(::kXliveUpdateFlag1Rva == ::kXliveUpdateFlag0Rva + 4u);
        CHECK(::kXliveUpdateFlag2Rva == 0x6B55D0u);
        CHECK(::kXliveUpdateFlag3Rva == ::kXliveUpdateFlag2Rva + 0x20u);
    }

    void TestLegacyUnsupportedLogged()
    {
        // The production load path calls EnsureConfigFile without a report; the
        // dropped-key line must still reach the log (a previous version logged
        // only when the test asked for a report).
        const std::filesystem::path dir = "opcep_legacy_log";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        WriteFile(dir / "ZolikaPatch.ini",
                  "[Options]\nMouseFix=0\nRemoveUselessChecks=1\nNoPhysicsSleep=1\n");

        const std::filesystem::path log = dir / "opcep_test.log";
        opp::OpenLog(log);
        const std::filesystem::path created = opp::EnsureConfigFile(dir);
        CHECK(created == dir / "IV.EFLC.OpenPreCEPatch.ini");

        std::ifstream input(log, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
        CHECK(text.find("legacy key RemoveUselessChecks not imported") != std::string::npos);
        CHECK(text.find("legacy key NoPhysicsSleep not imported") != std::string::npos);
        CHECK(text.find("MouseFix") == std::string::npos);  // carried over, not logged

        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }

    void TestFusionFixGate()
    {
        const std::filesystem::path path = "opcep_gate.ini";
        WriteFile(path, "[FIXES]\nVSyncFix = 1\n[STABILITY]\nAimProjectileCrashFix = 1\n");
        opp::Config config(path);

        opp::SetFusionFixPresent(false);
        CHECK(config.Option("VSyncFix", "1"));

        opp::SetFusionFixPresent(true);
        CHECK(!config.Option("VSyncFix", "1"));
        CHECK(config.RawOption("VSyncFix", "0")); // notice still sees the INI value
        CHECK(config.Option("AimProjectileCrashFix", "1")); // not a FusionFix overlap
        CHECK(opp::IsFusionFixGated("VSyncFix"));
        CHECK(opp::FusionFixGateReason("VSyncFix") != nullptr);

        opp::SetFusionFixPresent(false);
        CHECK(config.Option("VSyncFix", "1"));
        std::filesystem::remove(path);

        const std::filesystem::path dir = "opcep_ff_test";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        WriteFile(dir / "GTAIV.EFLC.FusionFix.asi", "");
        CHECK(opp::DetectFusionFix(dir));
        CHECK(opp::FusionFixPresent());
        opp::SetFusionFixPresent(false);
        std::filesystem::remove_all(dir);
    }

    void BuildSyntheticPe(std::vector<uint8_t>& image)
    {
        image.assign(0x4000, 0x90);
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image.data());
        dos->e_magic = IMAGE_DOS_SIGNATURE;
        dos->e_lfanew = 0x40;
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(image.data() + dos->e_lfanew);
        nt->Signature = IMAGE_NT_SIGNATURE;
        nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
        nt->FileHeader.NumberOfSections = 2;
        nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
        nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(image.size());
        auto* sections = IMAGE_FIRST_SECTION(nt);
        std::memcpy(sections[0].Name, ".text", 5);
        sections[0].VirtualAddress = 0x1000;
        sections[0].Misc.VirtualSize = 0x1000;
        sections[0].Characteristics = IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
        std::memcpy(sections[1].Name, ".data", 5);
        sections[1].VirtualAddress = 0x2000;
        sections[1].Misc.VirtualSize = 0x1000;
        sections[1].Characteristics = IMAGE_SCN_MEM_READ;
    }

    void TestExecutableView()
    {
        // A synthetic PE32 with one executable and one read-only section: the
        // scanner must stop at the end of the executable section, like the
        // original hook::pattern(module) call did.
        std::vector<uint8_t> image;
        BuildSyntheticPe(image);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
            image.data() + reinterpret_cast<IMAGE_DOS_HEADER*>(image.data())->e_lfanew);
        auto* sections = IMAGE_FIRST_SECTION(nt);

        const opp::ModuleView pe{image.data(), image.size()};
        const opp::ModuleView view = opp::ExecutableView(pe);
        CHECK(view.base == image.data());
        CHECK(view.size == 0x2000);

        // A non-PE buffer falls back to the whole view (the scanners are also
        // used on synthetic buffers in tests and tools).
        std::vector<uint8_t> raw(64, 0x90);
        const opp::ModuleView fallback = opp::ExecutableView({raw.data(), raw.size()});
        CHECK(fallback.base == raw.data());
        CHECK(fallback.size == raw.size());

        // Header claims a section past the mapped image: keep the whole view
        // instead of scanning a range that is not mapped.
        sections[0].VirtualAddress = 0x3000;
        sections[0].Misc.VirtualSize = 0x4000;
        const opp::ModuleView invalid = opp::ExecutableView(pe);
        CHECK(invalid.base == image.data());
        CHECK(invalid.size == image.size());

        // No executable section at all: same whole-image fallback.
        sections[0].Characteristics = IMAGE_SCN_MEM_READ;
        sections[0].VirtualAddress = 0x1000;
        sections[0].Misc.VirtualSize = 0x1000;
        const opp::ModuleView no_text = opp::ExecutableView(pe);
        CHECK(no_text.size == image.size());
    }

    void TestPatternScan()
    {
        // Two copies of the signature in .text and one in .data. Pattern() must
        // stop at the first .text copy; PatternAll() must return both .text
        // copies and never the .data one.
        std::vector<uint8_t> image;
        BuildSyntheticPe(image);
        const uint8_t signature[4] = {0xAA, 0xBB, 0xCC, 0xDD};
        std::memcpy(image.data() + 0x1100, signature, sizeof(signature));
        std::memcpy(image.data() + 0x1500, signature, sizeof(signature));
        std::memcpy(image.data() + 0x2100, signature, sizeof(signature));

        const opp::GameImage pe{image.data(), image.size()};
        auto first = opp::Pattern(pe, "AA BB CC DD");
        CHECK(first.size() == 1);
        CHECK(first.get(0).get<uint8_t>() == image.data() + 0x1100);

        auto all = opp::PatternAll(pe, "AA BB CC DD");
        CHECK(all.size() == 2);
        CHECK(all.get(0).get<uint8_t>() == image.data() + 0x1100);
        CHECK(all.get(1).get<uint8_t>() == image.data() + 0x1500);

        // A miss is an empty pattern either way.
        CHECK(opp::Pattern(pe, "11 22 33 44").size() == 0);
        CHECK(opp::PatternAll(pe, "11 22 33 44").size() == 0);
    }

    void TestGame()
    {
        CHECK(opp::GameHash("police") == 0x79fbb0c5u);
        CHECK(opp::GameHash("POLICE") == opp::GameHash("police"));
        CHECK(opp::GameHash("a\\b") == opp::GameHash("a/b"));
        CHECK(opp::GameHash("\"POLICE\"ignored") == opp::GameHash("police"));
        CHECK(opp::GameHash("") == 2u);
        CHECK(opp::GameHash("hash:0x1234") == 0x1234u);
        CHECK(opp::GameHash("HASH:123trailing") == 123u);
        CHECK(opp::GameHash("hash:  -1tail") == 0xffffffffu);
        CHECK(opp::GameHash("hash:077") == 63u);
        CHECK(opp::GameHash("hash:0") == 0xf4856ffeu);

        CHECK(opp::ClassifyGameBuild(1000) == opp::GameBuild::v1000);
        CHECK(opp::ClassifyGameBuild(1080) == opp::GameBuild::v1080);
        CHECK(opp::ClassifyGameBuild(1130) == opp::GameBuild::v1130);
        CHECK(opp::ClassifyGameBuild(9999) == opp::GameBuild::unknown);

        uint32_t compact = 0;
        CHECK(opp::ComposeGameVersion(1, 0, 8, 0, compact) && compact == 1080);
        CHECK(opp::BuildIs(opp::GameBuild::v1080,
                           {opp::GameBuild::v1070, opp::GameBuild::v1080}));
    }

    void TestCatalogue()
    {
        CHECK(opp::ModelCatalogSize() == 38455);
        CHECK(opp::ModelCatalogName(0) == "01_wires_mh1");
        CHECK(opp::ModelCatalogName(38454) == "ztype");
        CHECK(opp::ModelCatalogName(38455) == "**UNKNOWN**");
        CHECK(opp::ModelNameFromHash(opp::GameHash("police")) == "police");
        CHECK(opp::ModelNameFromHash(0) == "**UNKNOWN**");

        // ModelNameIndex: the one-time hash index must answer identically.
        CHECK(opp::ModelNameFromHash(opp::GameHash("police")) == "police");
        CHECK(opp::ModelNameFromHash(opp::GameHash("INFERNUS")) == "infernus");
        CHECK(opp::ModelNameFromHash(opp::GameHash("01_wires_mh1")) == "01_wires_mh1");
        CHECK(opp::ModelNameFromHash(0x1234567u) == "**UNKNOWN**");
    }

    void WriteNewSaveChunk(std::FILE* file, uint32_t size)
    {
        uint8_t header[9] = {};
        std::memcpy(header + 5, &size, 4);
        std::fwrite(header, 1, sizeof(header), file);
        for (uint32_t i = 9; i < size; ++i)
            std::fputc(static_cast<int>(i & 0xFFu), file);
    }

    void WriteNewSaveFile(const std::filesystem::path& path, bool truncate, bool badSize)
    {
        std::FILE* file = std::fopen(path.string().c_str(), "wb");
        CHECK(file != nullptr);
        if (!file)
            return;
        uint8_t header[0x110] = {};
        std::memcpy(header + 0x0C, "SAVE", 4);
        std::fwrite(header, 1, sizeof(header), file);
        for (int i = 0; i < 18; ++i)
        {
            if (truncate && i == 6)
                break;
            uint32_t size = 50;
            if (badSize && i == 3)
                size = 4;
            else if (i == 9)
                size = 19525;
            else if (i == 13)
                size = 5953;
            else if (i == 16)
                size = 3703;
            WriteNewSaveChunk(file, size);
        }
        std::fclose(file);
    }

    void TestNewSavesScan()
    {
        const std::filesystem::path path = "opcep_new_saves_test.bin";
        uint32_t flag9 = 0;
        uint32_t flag16 = 0;
        uint32_t flag13 = 0;

        WriteNewSaveFile(path, false, false);
        for (int guarded = 0; guarded <= 1; ++guarded)
        {
            for (int seek = 0; seek <= 1; ++seek)
            {
                std::FILE* file = std::fopen(path.string().c_str(), "rb");
                CHECK(file != nullptr);
                if (!file)
                    continue;
                const bool ok = zpatch_new_saves_scan(file, guarded, seek, &flag9,
                                                      &flag16, &flag13);
                std::fclose(file);
                CHECK(ok);
                CHECK(flag9 == 1);
                CHECK(flag16 == 1);
                CHECK(flag13 == 1);
            }
        }

        WriteNewSaveFile(path, true, false);
        std::FILE* file = std::fopen(path.string().c_str(), "rb");
        CHECK(file != nullptr);
        if (file)
        {
            CHECK(!zpatch_new_saves_scan(file, 1, 0, &flag9, &flag16, &flag13));
            std::fclose(file);
        }

        WriteNewSaveFile(path, false, true);
        file = std::fopen(path.string().c_str(), "rb");
        CHECK(file != nullptr);
        if (file)
        {
            CHECK(!zpatch_new_saves_scan(file, 1, 0, &flag9, &flag16, &flag13));
            std::fclose(file);
        }

        // Header-only chunks (size == 9) are valid and must not be rejected by
        // the malloc(0) guard.
        file = std::fopen(path.string().c_str(), "wb");
        CHECK(file != nullptr);
        if (file)
        {
            uint8_t header[0x110] = {};
            std::memcpy(header + 0x0C, "SAVE", 4);
            std::fwrite(header, 1, sizeof(header), file);
            for (int i = 0; i < 18; ++i)
                WriteNewSaveChunk(file, 9);
            std::fclose(file);
            file = std::fopen(path.string().c_str(), "rb");
            CHECK(file != nullptr);
            if (file)
            {
                CHECK(zpatch_new_saves_scan(file, 1, 0, &flag9, &flag16, &flag13));
                std::fclose(file);
                file = std::fopen(path.string().c_str(), "rb");
                CHECK(file != nullptr);
                if (file)
                {
                    CHECK(zpatch_new_saves_scan(file, 1, 1, &flag9, &flag16, &flag13));
                    std::fclose(file);
                }
            }
        }
        std::filesystem::remove(path);
    }

    void TestVehicleLights()
    {
        const std::filesystem::path path = "opcep_lights.ini";
        WriteFile(path,
                  "[PopupLightAngles]\nCar1Name=police\nCar1Angle=90\n"
                  "Car512Name=hash:123\nCar512Angle=-180suffix\n"
                  "[HeadlightConeOffsets]\nCar2Name=POLICE\nCar2Offset=3.5\n");
        opp::Config config(path);
        opp::VehicleLightTables tables;
        CHECK(opp::LoadVehicleLightTables(config, tables));
        CHECK(tables.popupAngles[0].modelHash == opp::GameHash("police"));
        CHECK(std::fabs(tables.popupAngles[0].value - 1.57079633f) < 0.00001f);
        CHECK(tables.popupAngles[511].modelHash == 123);
        CHECK(!tables.popupAngles[1].populated);
        CHECK(tables.headlightOffsets[1].value == 3.5f);
        std::filesystem::remove(path);
    }

    void TestMenu()
    {
        CHECK(!opp::ZMenuVersionAccepted(0x35DD0u));
        CHECK(opp::ZMenuVersionAccepted(0x35DD1u));
        uint32_t version = 0;
        CHECK(opp::ZMenuVersionGuard(version) == opp::ZMenuGuardResult::module_absent);
    }

    void TestFeatureOrder()
    {
        const auto& features = opp::RegisteredFeatures();
        CHECK(features.size() == 122);

        // Phases never go backwards: every publish precedes every install.
        for (size_t i = 1; i < features.size(); ++i)
            CHECK(static_cast<int>(features[i - 1].phase) <=
                  static_cast<int>(features[i].phase));

        // The effective order is the frozen shipping order (captured from the
        // integer-ordered registry before this refactor). A reorder is a
        // behaviour change and must update the golden in the same commit.
        std::FILE* file = std::fopen(OPCEP_SOURCE_DIR "/tests/order_golden.txt", "rb");
        CHECK(file != nullptr);
        if (file)
        {
            std::vector<std::string> golden;
            char line[256];
            while (std::fgets(line, sizeof(line), file))
            {
                std::string name(line);
                while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
                    name.pop_back();
                if (!name.empty())
                    golden.push_back(name);
            }
            std::fclose(file);

            CHECK(golden.size() == features.size());
            for (size_t i = 0; i < features.size() && i < golden.size(); ++i)
            {
                if (features[i].name == golden[i])
                    continue;
                // One line per divergence, with the neighbour that proves where
                // the sequence split.
                std::fprintf(stderr,
                             "order: #%zu expected %s, got %s (previous %s)\n", i,
                             golden[i].c_str(), features[i].name,
                             i ? features[i - 1].name : "-");
                ++failures;
                break;
            }
        }
    }

    void TestOrderDiagnostics()
    {
        const auto noop = [](opp::PatchContext&) {};
        const auto find = [](const opp::OrderResult& result, const char* text) {
            for (const std::string& diagnostic : result.diagnostics)
            {
                if (diagnostic.find(text) != std::string::npos)
                    return true;
            }
            return false;
        };

        // A consistent two-arm graph orders by the shipping schedule.
        {
            const std::vector<opp::FeatureRegistration> features = {
                {opp::Phase::install, "InstallSCOSignatureFix", noop},
                {opp::Phase::install, "InstallLoadDLCs", noop},
            };
            const opp::OrderResult result = opp::BuildOrder(features);
            CHECK(result.valid);
            CHECK(result.order.size() == 2);
            CHECK(result.order[0].name == std::string_view("InstallLoadDLCs"));
            CHECK(result.order[1].name == std::string_view("InstallSCOSignatureFix"));

            // Reversed registration must produce the same order.
            const std::vector<opp::FeatureRegistration> reversed = {features[1], features[0]};
            const opp::OrderResult again = opp::BuildOrder(reversed);
            CHECK(again.order.size() == 2);
            CHECK(again.order[0].name == std::string_view("InstallLoadDLCs"));
        }

        // A cycle fails closed and names the path.
        {
            std::vector<opp::FeatureRegistration> features = {
                {opp::Phase::install, "InstallLoadDLCs", noop, {"InstallSCOSignatureFix"}},
                {opp::Phase::install, "InstallSCOSignatureFix", noop, {"InstallLoadDLCs"}},
            };
            const opp::OrderResult result = opp::BuildOrder(features);
            CHECK(!result.valid);
            CHECK(result.order.empty());
            CHECK(find(result, "cycle"));
        }

        // A missing `After` target is a soft edge; a missing `Requires` target
        // skips the arm.
        {
            std::vector<opp::FeatureRegistration> features = {
                {opp::Phase::install, "InstallLoadDLCs", noop, {"NotAnArm"}},
                {opp::Phase::install, "InstallSCOSignatureFix", noop, {}, {"NotAnArm"}},
            };
            const opp::OrderResult result = opp::BuildOrder(features);
            CHECK(result.valid);
            CHECK(result.order.size() == 1);
            CHECK(result.order[0].name == std::string_view("InstallLoadDLCs"));
            CHECK(find(result, "requires NotAnArm (absent)"));
        }

        // A dependency that points backwards across phases is a programming
        // error, not a silent reorder.
        {
            std::vector<opp::FeatureRegistration> features = {
                {opp::Phase::publish, "PublishLoadDLCs", noop, {"InstallLoadDLCs"}},
                {opp::Phase::install, "InstallLoadDLCs", noop},
            };
            const opp::OrderResult result = opp::BuildOrder(features);
            CHECK(!result.valid);
            CHECK(find(result, "phase inversion"));
        }

        // Duplicate and unknown names are refused, never deduplicated silently.
        {
            std::vector<opp::FeatureRegistration> features = {
                {opp::Phase::install, "InstallLoadDLCs", noop},
                {opp::Phase::install, "InstallLoadDLCs", noop},
            };
            const opp::OrderResult result = opp::BuildOrder(features);
            CHECK(!result.valid);
            CHECK(find(result, "duplicate feature"));
        }
        {
            std::vector<opp::FeatureRegistration> features = {
                {opp::Phase::install, "InstallNotInSchedule", noop},
            };
            const opp::OrderResult result = opp::BuildOrder(features);
            CHECK(!result.valid);
            CHECK(find(result, "not in the shipping schedule"));
        }
    }

    void TestArmFailsafes()
    {
        // A synthetic 4 KiB image of 0xCC: no signature can match it, and every
        // RVA-based arm points past its end. The checked write helpers must
        // accept an in-image write, refuse one that would leave the image, and
        // refuse a branch whose body did not resolve - all without touching a
        // byte and without a fault.
        std::vector<uint8_t> buffer(0x1000, 0xCC);
        const opp::GameImage image{buffer.data(), buffer.size()};
        const opp::Config config{"opcep_failsafe_no_such_file.ini"};
        opp::PatchContext ctx{image, reinterpret_cast<uintptr_t>(buffer.data()),
                              opp::GameBuild::v1080, config};

        CHECK(opp::SiteWritable(ctx, buffer.data(), 5));
        CHECK(opp::SiteWritable(ctx, buffer.data() + buffer.size() - 5, 5));
        CHECK(!opp::SiteWritable(ctx, buffer.data() + buffer.size() - 2, 5));
        CHECK(!opp::SiteWritable(ctx, buffer.data() - 1, 1));
        CHECK(!opp::SiteWritable(ctx, reinterpret_cast<const void*>(0x10), 1));
        // A module-scoped write (the GFWL option into xlive.dll) is accepted only
        // inside that module's own allocation.
        const auto* const self = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
        CHECK(opp::ModuleSiteWritable(self, self, 4));
        CHECK(!opp::ModuleSiteWritable(self, buffer.data(), 4));
        CHECK(!opp::ModuleSiteWritable(self, self + 0x40000000, 4));
        CHECK(!opp::SiteWritable(ctx, nullptr, 1));

        // A refused write is counted and leaves the image untouched.
        const size_t skipped = opp::ArmWritesSkipped();
        opp::NopAt(ctx, buffer.data() + buffer.size() - 2, 5);
        CHECK(buffer[buffer.size() - 1] == 0xCC);
        CHECK(opp::ArmWritesSkipped() == skipped + 1);

        // An accepted write reaches the image and is byte-identical to the
        // unchecked injector call it replaced.
        std::vector<uint8_t> reference(buffer.size(), 0xCC);
        injector::MakeNOP(reference.data(), 4, true);
        injector::WriteMemory<uint16_t>(reference.data() + 8, 0xBEEFu, true);
        opp::NopAt(ctx, buffer.data(), 4);
        opp::WriteValue<uint16_t>(ctx, buffer.data() + 8, 0xBEEFu);
        CHECK(std::memcmp(buffer.data(), reference.data(), 10) == 0);

        // An unregistered body is a no-op, not a jump to 0.
        std::vector<uint8_t> before(buffer);
        opp::JumpToBody(ctx, buffer.data() + 16, "abi_does_not_exist");
        CHECK(std::memcmp(buffer.data() + 16, before.data() + 16, 5) == 0);
        CHECK(opp::ArmWritesSkipped() == skipped + 2);

        // ... and a null destination is refused the same way.
        opp::CallTo(ctx, buffer.data() + 16, nullptr);
        CHECK(std::memcmp(buffer.data() + 16, before.data() + 16, 5) == 0);

        // A registered body writes the same five-byte branch the arm wrote
        // before: opcode + rel32 to the body, encoded from the site.
        void* const body = AbiByName("abi_aim_projectile_crash_fix");
        CHECK(body != nullptr);
        opp::JumpTo(ctx, buffer.data() + 32, body);
        CHECK(buffer[32] == 0xE9);
        int32_t jump_rel = 0;
        std::memcpy(&jump_rel, buffer.data() + 33, sizeof(jump_rel));
        CHECK(reinterpret_cast<uint8_t*>(buffer.data() + 32 + 5 + jump_rel) ==
              reinterpret_cast<uint8_t*>(body));
        opp::CallTo(ctx, buffer.data() + 40, body);
        CHECK(buffer[40] == 0xE8);
        int32_t call_rel = 0;
        std::memcpy(&call_rel, buffer.data() + 41, sizeof(call_rel));
        CHECK(reinterpret_cast<uint8_t*>(buffer.data() + 40 + 5 + call_rel) ==
              reinterpret_cast<uint8_t*>(body));

        // A missing signature is one logged skip, and the arm is named.
        opp::TraceArm("TestArmFailsafes");
        CHECK(std::strcmp(opp::CurrentArm(), "TestArmFailsafes") == 0);
        auto pattern = opp::Pattern(image, "DE AD BE EF");
        CHECK(pattern.size() == 0);
        CHECK(opp::PatternAll(image, "DE AD BE EF").size() == 0);

        // End to end: the whole option pass over that image installs nothing.
        // Every arm is a signature scan now (no arm writes a bare RVA), so a
        // synthetic image matches nothing and no write is even attempted; the
        // image must be byte-identical and no write may reach it.
        std::vector<uint8_t> clean(buffer.size(), 0xCC);
        const std::vector<uint8_t> untouched(clean.size(), 0xCC);
        const opp::GameImage clean_image{clean.data(), clean.size()};
        opp::PatchContext clean_ctx{clean_image,
                                    reinterpret_cast<uintptr_t>(clean.data()),
                                    opp::GameBuild::v1080, config};
        const size_t applied_before_pass = opp::ArmWritesApplied();
        opp::PatchGame(opp::GameBuild::v1080, clean_image, config);
        CHECK(std::memcmp(clean.data(), untouched.data(), clean.size()) == 0);
        CHECK(opp::ArmWritesApplied() == applied_before_pass);
    }

    void TestLauncherNamePatch()
    {
        // LaunchWithPlayGTAIV rewrites the game's two "LaunchGTAIV.exe" name
        // slots to the 16 bytes the original's strcpy_s leaves there:
        // "PlayGTAIV.exe\0" followed by the old name's surviving "e\0".
        std::vector<uint8_t> image;
        BuildSyntheticPe(image);
        const char launch_name[] = "LaunchGTAIV.exe";
        std::memcpy(image.data() + 0x1100, launch_name, sizeof(launch_name));
        std::memcpy(image.data() + 0x2100, launch_name, sizeof(launch_name));

        const std::filesystem::path ini = "opcep_launcher.ini";
        WriteFile(ini, "[Options]\nLaunchWithPlayGTAIV = 1\n");
        const opp::Config config(ini);
        opp::SetLauncherProbeOverride(true);
        opp::PatchContext ctx{{image.data(), image.size()},
                              reinterpret_cast<uintptr_t>(image.data()),
                              opp::GameBuild::v1080, config};
        opp::ApplyLaunchWithPlayGTAIV(ctx);
        opp::SetLauncherProbeOverride(false);
        for (const size_t offset : {0x1100u, 0x2100u})
        {
            const uint8_t* slot = image.data() + offset;
            CHECK(std::memcmp(slot, "PlayGTAIV.exe", 13) == 0);
            CHECK(slot[13] == 0);
            CHECK(slot[14] == 'e');
            CHECK(slot[15] == 0);
        }

        // Wrong build: the gate keeps both slots untouched.
        std::vector<uint8_t> other;
        BuildSyntheticPe(other);
        std::memcpy(other.data() + 0x2100, launch_name, sizeof(launch_name));
        opp::SetLauncherProbeOverride(true);
        opp::PatchContext other_ctx{{other.data(), other.size()},
                                    reinterpret_cast<uintptr_t>(other.data()),
                                    opp::GameBuild::v1110, config};
        opp::ApplyLaunchWithPlayGTAIV(other_ctx);
        opp::SetLauncherProbeOverride(false);
        CHECK(std::memcmp(other.data() + 0x2100, launch_name, sizeof(launch_name)) == 0);

        // The original writes exactly its two slots. An image with 0, 1 or 3
        // name strings must be left alone (a third slot is not the original's).
        const auto untouched_with_slots = [&config, &launch_name](
                                              std::initializer_list<size_t> slots) {
            std::vector<uint8_t> candidate;
            BuildSyntheticPe(candidate);
            for (const size_t offset : slots)
                std::memcpy(candidate.data() + offset, launch_name, sizeof(launch_name));
            opp::SetLauncherProbeOverride(true);
            opp::PatchContext candidate_ctx{{candidate.data(), candidate.size()},
                                            reinterpret_cast<uintptr_t>(candidate.data()),
                                            opp::GameBuild::v1080, config};
            opp::ApplyLaunchWithPlayGTAIV(candidate_ctx);
            opp::SetLauncherProbeOverride(false);
            for (const size_t offset : slots)
            {
                if (std::memcmp(candidate.data() + offset, launch_name,
                                sizeof(launch_name)) != 0)
                    return false;
            }
            return true;
        };
        CHECK(untouched_with_slots({}));                             // 0 hits
        CHECK(untouched_with_slots({0x2100u}));                      // 1 hit
        CHECK(untouched_with_slots({0x1100u, 0x2100u, 0x3100u}));    // 3 hits
        std::filesystem::remove(ini);
    }

    void TestRelocateCalls()
    {
        // A tiny "function" with two call rel32 instructions to fix up.
        static uint8_t target_a[1] = {0xC3};
        static uint8_t target_b[1] = {0xC3};
        uint8_t source[16] = {};
        constexpr uint32_t offset_a = 1;
        constexpr uint32_t offset_b = 7;
        source[offset_a] = 0xE8;
        source[offset_b] = 0xE8;
        const auto rel_a = static_cast<int32_t>(
            reinterpret_cast<uint8_t*>(target_a) - (source + offset_a + 5));
        const auto rel_b = static_cast<int32_t>(
            reinterpret_cast<uint8_t*>(target_b) - (source + offset_b + 5));
        std::memcpy(source + offset_a + 1, &rel_a, sizeof(rel_a));
        std::memcpy(source + offset_b + 1, &rel_b, sizeof(rel_b));

        const uint32_t offsets[2] = {offset_a, offset_b};
        uint8_t copy[16] = {};
        CHECK(opp::zpatch_relocate_calls(copy, source, sizeof(source), offsets, 2));
        CHECK(std::memcmp(copy, source, sizeof(source)) != 0);
        int32_t new_a = 0;
        int32_t new_b = 0;
        std::memcpy(&new_a, copy + offset_a + 1, sizeof(new_a));
        std::memcpy(&new_b, copy + offset_b + 1, sizeof(new_b));
        CHECK(reinterpret_cast<uint8_t*>(copy + offset_a + 5 + new_a) == target_a);
        CHECK(reinterpret_cast<uint8_t*>(copy + offset_b + 5 + new_b) == target_b);

        // A listed offset that does not hold an E8 fails the whole copy.
        uint8_t bad[16] = {};
        const uint32_t not_e8 = 3;  // source[3] is 0, not a call opcode
        CHECK(!opp::zpatch_relocate_calls(bad, source, sizeof(source), &not_e8, 1));

        // An offset outside the range fails too. (`far` is a Windows macro.)
        const uint32_t out_of_range = 0x100;
        CHECK(!opp::zpatch_relocate_calls(bad, source, sizeof(source), &out_of_range, 1));
    }

    void TestHookBodyRegistry()
    {
        // One table now backs both lookups: a name resolves to its body, and a
        // body's original handler VA resolves to the same body. The VA count is
        // pinned so a dropped row is a test failure, not a silent skip.
        CHECK(HookBodyCount() == 103);
        void* const aim = AbiByName("abi_aim_projectile_crash_fix");
        CHECK(aim != nullptr);
        CHECK(ResolveHookBody(0x10005590u, nullptr) ==
              reinterpret_cast<uintptr_t>(aim));
        CHECK(ResolveHookBody(0u, nullptr) == 0);
        CHECK(ResolveHookBody(0x10000000u, nullptr) == 0);
        CHECK(AbiByName("does_not_exist") == nullptr);
        CHECK(AbiByName(nullptr) == nullptr);
        // A name-only body (no recovered handler VA) still resolves by name.
        CHECK(AbiByName("abi_spawns_case_0x24") != nullptr);
    }

    void TestMakeReturn()
    {
        // injector::MakeRET must write exactly the instruction: one byte for
        // `ret`, three for `ret N`. It used to pass the opcode as an int, which
        // stored four bytes and clobbered the bytes after the site.
        uint8_t buffer[8];
        std::memset(buffer, 0xCC, sizeof(buffer));
        injector::MakeRET(buffer, 0, true);
        CHECK(buffer[0] == 0xC3);
        CHECK(buffer[1] == 0xCC);
        CHECK(buffer[2] == 0xCC);

        std::memset(buffer, 0xCC, sizeof(buffer));
        injector::MakeRET(buffer, 8, true);
        CHECK(buffer[0] == 0xC2);
        CHECK(buffer[1] == 0x08);
        CHECK(buffer[2] == 0x00);
        CHECK(buffer[3] == 0xCC);
        CHECK(buffer[4] == 0xCC);
    }

    void TestHooks()
    {
        CHECK(HookBodyCount() > 100);
        CHECK(AbiByName("abi_spawns_guard") != nullptr);
        CHECK(AbiByName("does_not_exist") == nullptr);
        CHECK(zpatch_ooc_selftest());
        CHECK(zpatch_nms_registry_selftest());
    }
} // namespace

int main()
{
    TestConfig();
    TestIntPrefix();
    TestConfigSemantics();
    TestShippedTemplate();
    TestIniImport();
    TestLegacyUnsupportedKeys();
    TestLegacyUnsupportedLogged();
    TestFusionFixGate();
    TestRemoveGFWLUpdatePopupSites();
    TestExecutableView();
    TestPatternScan();
    TestGame();
    TestCatalogue();
    TestNewSavesScan();
    TestVehicleLights();
    TestMenu();
    TestFeatureOrder();
    TestOrderDiagnostics();
    TestArmFailsafes();
    TestLauncherNamePatch();
    TestRelocateCalls();
    TestMakeReturn();
    TestHookBodyRegistry();
    TestHooks();

    if (failures != 0)
    {
        std::fprintf(stderr, "opcep tests: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("opcep tests: PASS\n");
    return 0;
}
