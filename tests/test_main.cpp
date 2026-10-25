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
#include "hooks.h"
#include "ini_import.hxx"
#include "menu.hxx"
#include "vehicle_lights.hxx"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

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
        CHECK(!config.Option("RemoveDataIntegrityChecks", "1")); // MiscFixes=0
        CHECK(config.Option("AimProjectileCrashFix", "1")); // template default
        CHECK(config.String("LOADING", "OverrideGTADatPathTo", "") ==
              "common:/data/custom.dat");
        CHECK(config.String("PopupLightAngles", "Car1Name", "") == "police");
        std::filesystem::remove_all(dir);

        const std::filesystem::path fresh = "opcep_import_fresh";
        std::filesystem::remove_all(fresh);
        std::filesystem::create_directories(fresh);
        const std::filesystem::path defaultIni = opp::EnsureConfigFile(fresh);
        CHECK(defaultIni == fresh / "IV.EFLC.OpenPreCEPatch.ini");
        opp::Config defaults(defaultIni);
        CHECK(defaults.Option("AimProjectileCrashFix", "0"));
        CHECK(defaults.Option("RemoveDataIntegrityChecks", "0"));
        std::filesystem::remove_all(fresh);
    }

    void TestFusionFixGate()
    {
        const std::filesystem::path path = "opcep_gate.ini";
        WriteFile(path, "[INPUT]\nSkipIntro = 1\n[STABILITY]\nAimProjectileCrashFix = 1\n");
        opp::Config config(path);

        opp::SetFusionFixPresent(false);
        CHECK(config.Option("SkipIntro", "1"));

        opp::SetFusionFixPresent(true);
        CHECK(!config.Option("SkipIntro", "1"));
        CHECK(config.RawOption("SkipIntro", "0")); // notice still sees the INI value
        CHECK(config.Option("AimProjectileCrashFix", "1")); // not a FusionFix overlap
        CHECK(opp::IsFusionFixGated("SkipIntro"));
        CHECK(opp::FusionFixGateReason("SkipIntro") != nullptr);

        opp::SetFusionFixPresent(false);
        CHECK(config.Option("SkipIntro", "1"));
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
        CHECK(opp::ProbeMenuApi(false, false, false, true) == opp::MenuProbeResult::disabled);
        CHECK(opp::ProbeMenuApi(true, true, false, false) ==
              opp::MenuProbeResult::already_loaded);
        CHECK(opp::ProbeMenuApi(true, false, false, true) == opp::MenuProbeResult::loaded);
        CHECK(opp::ProbeMenuApi(true, false, true, true) ==
              opp::MenuProbeResult::unavailable);

        size_t rows = 0;
        const auto* registrations = opp::MenuRegistrations(rows);
        CHECK(rows == 2);
        CHECK(std::strcmp(registrations[0].family, "PREF_DEATHMUSIC") == 0);
        CHECK(std::strcmp(registrations[1].family, "PREF_FOCUSLOSS") == 0);

        CHECK(!opp::ZMenuVersionAccepted(0x35DD0u));
        CHECK(opp::ZMenuVersionAccepted(0x35DD1u));
        uint32_t version = 0;
        CHECK(opp::ZMenuVersionGuard(version) == opp::ZMenuGuardResult::module_absent);
    }

    void TestFeatureOrder()
    {
        const auto& features = opp::RegisteredFeatures();
        CHECK(features.size() == 148);
        for (size_t i = 1; i < features.size(); ++i)
            CHECK(features[i - 1].order < features[i].order);
    }

    void TestHooks()
    {
        CHECK(HookBodyCount() > 100);
        CHECK(AbiByName("abi_skip_intro_body") != nullptr);
        CHECK(AbiByName("does_not_exist") == nullptr);
        CHECK(zpatch_ooc_selftest());
    }
} // namespace

int main()
{
    TestConfig();
    TestIniImport();
    TestFusionFixGate();
    TestGame();
    TestCatalogue();
    TestVehicleLights();
    TestMenu();
    TestFeatureOrder();
    TestHooks();

    if (failures != 0)
    {
        std::fprintf(stderr, "opcep tests: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("opcep tests: PASS\n");
    return 0;
}
