// SPDX-License-Identifier: GPL-3.0-or-later
//
// Native tests for everything that does not need the game: INI parsing and the
// grouped/legacy lookup, build classification, the model catalogue, the vehicle
// light tables, the menu probes and the ZMenu guard.

#include "catalogue.hxx"
#include "common.hxx"
#include "config.hxx"
#include "feature_registry.hxx"
#include "fusionfix_compat.hxx"
#include "game.hxx"
#include "helpers.hxx"
#include "hooks.h"
#include "improvements.hxx"
#include "ini_import.hxx"
#include "menu.hxx"
#include "patches.hxx"
#include "pattern.hxx"
#include "vehicle_lights.hxx"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

extern "C" uint8_t zpatch_opt_model_index;

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

    void TestImprovements()
    {
        constexpr uint32_t all = opp::Improve(opp::Improvement::all);
        constexpr uint32_t newsaves = opp::Improve(opp::Improvement::new_saves);
        constexpr uint32_t ooc = opp::Improve(opp::Improvement::ooc);
        constexpr uint32_t trace = opp::Improve(opp::Improvement::trace);
        constexpr uint32_t skipintro = opp::Improve(opp::Improvement::skip_intro);

        CHECK(opp::ParseImprovements(nullptr) == all);
        CHECK(opp::ParseImprovements("") == all);
        CHECK(opp::ParseImprovements("all") == all);
        CHECK(opp::ParseImprovements("ON") == all);
        CHECK(opp::ParseImprovements("none") == 0);
        CHECK(opp::ParseImprovements("off") == 0);
        CHECK(opp::ParseImprovements("only:newsaves,ooc") == (newsaves | ooc));
        CHECK(opp::ParseImprovements("except:trace") == (all & ~trace));
        CHECK(opp::ParseImprovements("SkipIntro") == skipintro);
        CHECK(opp::ParseImprovements(" only: newsaves ; ooc ") == (newsaves | ooc));
        CHECK(opp::ParseImprovements("only:") == 0);
        CHECK(opp::ParseImprovements("except:") == all);

#if defined(OPCEP_PARITY_BASELINE)
        CHECK(opp::EnabledImprovements() == 0);
#else
        SetEnvironmentVariableA("OPCEP_IMPROVEMENTS", "none");
        CHECK(opp::EnabledImprovements() == 0);
        SetEnvironmentVariableA("OPCEP_IMPROVEMENTS", "skipintro");
        CHECK(opp::EnabledImprovements() == skipintro);
        SetEnvironmentVariableA("OPCEP_IMPROVEMENTS", "except:trace");
        CHECK(opp::EnabledImprovements() == (all & ~trace));
        SetEnvironmentVariableA("OPCEP_IMPROVEMENTS", nullptr);
        CHECK(opp::EnabledImprovements() == all);
#endif
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
        zpatch_opt_model_index = 1;
        CHECK(opp::ModelNameFromHash(opp::GameHash("police")) == "police");
        CHECK(opp::ModelNameFromHash(opp::GameHash("INFERNUS")) == "infernus");
        CHECK(opp::ModelNameFromHash(opp::GameHash("01_wires_mh1")) == "01_wires_mh1");
        CHECK(opp::ModelNameFromHash(0x1234567u) == "**UNKNOWN**");
        zpatch_opt_model_index = 0;
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
        CHECK(features.size() == 123);
        for (size_t i = 1; i < features.size(); ++i)
            CHECK(features[i - 1].order < features[i].order);
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
        // Every arm either does not match or is refused by the image check, so
        // the image is byte-identical afterwards and the skips are reported.
        std::vector<uint8_t> clean(buffer.size(), 0xCC);
        const std::vector<uint8_t> untouched(clean.size(), 0xCC);
        const opp::GameImage clean_image{clean.data(), clean.size()};
        opp::PatchContext clean_ctx{clean_image,
                                    reinterpret_cast<uintptr_t>(clean.data()),
                                    opp::GameBuild::v1080, config};
        const size_t skipped_before_pass = opp::ArmWritesSkipped();
        opp::PatchGame(opp::GameBuild::v1080, clean_image, config);
        CHECK(std::memcmp(clean.data(), untouched.data(), clean.size()) == 0);
        CHECK(opp::ArmWritesSkipped() > skipped_before_pass);
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

    void TestHooks()
    {
        CHECK(HookBodyCount() > 100);
        CHECK(AbiByName("abi_skip_intro_body") != nullptr);
        CHECK(AbiByName("does_not_exist") == nullptr);
        CHECK(zpatch_ooc_selftest());
        CHECK(zpatch_nms_registry_selftest());
    }
} // namespace

int main()
{
    TestConfig();
    TestIniImport();
    TestFusionFixGate();
    TestExecutableView();
    TestPatternScan();
    TestGame();
    TestImprovements();
    TestCatalogue();
    TestNewSavesScan();
    TestVehicleLights();
    TestMenu();
    TestFeatureOrder();
    TestArmFailsafes();
    TestRelocateCalls();
    TestHooks();

    if (failures != 0)
    {
        std::fprintf(stderr, "opcep tests: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("opcep tests: PASS\n");
    return 0;
}
