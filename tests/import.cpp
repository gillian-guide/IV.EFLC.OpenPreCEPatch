#include <common.hxx>

#include <cstdio>
#include <fstream>

import common;
import config;
import improvements;
import options;

// The `import` group: the flat INI is rendered once into the grouped file, the
// grouped file is preferred on the next load, and the flat file is untouched.

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

    std::string_view SectionOf(std::string_view key)
    {
        const auto* option = Catalogue::Find(key);
        if (option == nullptr)
            return "Options";
        if (key == "BenchmarkFix" || option->module == "stability")
            return "STABILITY";
        if (key == "OutOfCommissionFix" || key == "CutsceneFixes")
            return "FIXES";
        if (option->module == "vehicles") return "VEHICLES";
        if (option->module == "graphics") return "GRAPHICS";
        if (option->module == "loading") return "COMPATIBILITY";
        return "MISC";
    }

    void TestImport()
    {
        const auto original = std::filesystem::current_path();
        const auto directory = original / "import_group";
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::filesystem::create_directories(directory, ignored);
        std::filesystem::current_path(directory, ignored);

        {
            std::ofstream flat("ZolikaPatch.ini", std::ios::out | std::ios::trunc);
            flat << "[Options]\nBenchmarkFix=0\nGarageModelFix=1\n";
        }

        SetImprovementsForTest(0);
        Check(Config::Load(), "import: the flat file is read with the group off");
        Check(!std::filesystem::exists("ZolikaPatch.grouped.ini"),
            "import: the group-off run creates no grouped file");
        Check(Catalogue::Read("GarageModelFix") == 1,
            "import: the flat value is the one the group-off run uses");

        SetImprovementsForTest(Improve(Improvement::import));
        Config::Ini() = {};
        Check(Config::Load(), "import: the flat file is read on the first grouped run");
        Check(Config::WriteGrouped(Config::Ini(), SectionOf),
            "import: the grouped file is written");
        Check(std::filesystem::exists("ZolikaPatch.grouped.ini"),
            "import: the grouped file exists");

        std::ifstream grouped("ZolikaPatch.grouped.ini");
        const std::string text((std::istreambuf_iterator<char>(grouped)),
            std::istreambuf_iterator<char>());
        Check(text.find("[STABILITY]") != std::string::npos &&
                  text.find("BenchmarkFix=0") != std::string::npos,
            "import: BenchmarkFix is under STABILITY");
        Check(text.find("[VEHICLES]") != std::string::npos &&
                  text.find("GarageModelFix=1") != std::string::npos,
            "import: GarageModelFix is under VEHICLES");

        Config::Ini() = {};
        Check(Config::Load(), "import: the grouped file is preferred");
        Check(Config::Ini().Find("VEHICLES", "GarageModelFix") != nullptr,
            "import: the loaded document is the grouped one");
        Check(Catalogue::Read("GarageModelFix") == 1 &&
                  Catalogue::Read("BenchmarkFix") == 0,
            "import: values read identically from the grouped file");

        SetImprovementsForTest(0);
        std::filesystem::current_path(original, ignored);
        std::filesystem::remove_all(directory, ignored);
    }
}

int main()
{
    TestImport();
    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
