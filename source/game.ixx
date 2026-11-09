module;

#include <common.hxx>

export module game;

import common;

// Build identity and the game image. The original classifies the executable by
// its file version: the four fields concatenated as decimal text (1.0.7.0 ->
// 1070, 1.2.0.43 -> 12043) and a switch that maps each product version to the
// build id its masks index.
export enum class Build : uint8_t
{
    unknown = 0,
    v1000 = 1,
    v1004 = 6,
    v1040 = 5,
    v1060 = 7,
    v1061 = 15,
    v1070 = 8,
    v1080 = 9,
    v1100 = 10,
    v1110 = 11,
    v1120 = 12,
    v1130 = 13,
    v12043 = 18,
};

export std::string CompactVersion(const Game::Version& version)
{
    return std::format("{}{}{}{}", version[0], version[1], version[2], version[3]);
}

export Build ClassifyCompact(std::string_view compact)
{
    const auto id = std::atoi(std::string(compact).c_str());
    switch (id)
    {
    case 1000: return Build::v1000;
    case 1004: return Build::v1004;
    case 1040: return Build::v1040;
    case 1060: return Build::v1060;
    case 1061: return Build::v1061;
    case 1070: return Build::v1070;
    case 1080: return Build::v1080;
    case 1100: return Build::v1100;
    case 1110: return Build::v1110;
    case 1120: return Build::v1120;
    case 1130: return Build::v1130;
    case 12043: return Build::v12043;
    default: return Build::unknown;
    }
}

// The build id is the enumeration value and what the per-image dumps are keyed
// by: the tools take the id, not the product version text.
export Build FromId(uint32_t id)
{
    switch (id)
    {
    case 1: return Build::v1000;
    case 5: return Build::v1040;
    case 6: return Build::v1004;
    case 7: return Build::v1060;
    case 8: return Build::v1070;
    case 9: return Build::v1080;
    case 10: return Build::v1100;
    case 11: return Build::v1110;
    case 12: return Build::v1120;
    case 13: return Build::v1130;
    case 15: return Build::v1061;
    case 18: return Build::v12043;
    default: return Build::unknown;
    }
}

export Build Classify(const Game::Version& version)
{
    return ClassifyCompact(CompactVersion(version));
}

export bool BuildIs(Build build, std::initializer_list<Build> builds)
{
    for (const auto candidate : builds)
    {
        if (candidate == build)
            return true;
    }
    return false;
}

// A build mask is indexed by the original's build id: bit i is build i.
export uint32_t BuildMask(std::initializer_list<Build> builds)
{
    uint32_t mask = 0;
    for (const auto build : builds)
        mask |= 1u << static_cast<uint32_t>(build);
    return mask;
}

export std::string_view BuildName(Build build)
{
    switch (build)
    {
    case Build::v1000: return "1.0.0.0";
    case Build::v1004: return "1.0.0.4";
    case Build::v1040: return "1.0.4.0";
    case Build::v1060: return "1.0.6.0";
    case Build::v1061: return "1.0.6.1";
    case Build::v1070: return "1.0.7.0";
    case Build::v1080: return "1.0.8.0";
    case Build::v1100: return "1.1.0.0";
    case Build::v1110: return "1.1.1.0";
    case Build::v1120: return "1.1.2.0";
    case Build::v1130: return "1.1.3.0";
    case Build::v12043: return "1.2.0.43";
    default: return "unknown";
    }
}

// The range a signature scan covers: exactly what Hooking.Patterns' module scan
// covers, from the image base to the end of the last executable section. The
// offline tools load a copy of the image instead of the image itself, and the
// range has to be identical for their scans to answer the same.
export std::pair<uintptr_t, uintptr_t> ImageScanRange()
{
    const auto& game = Game::Get();
    const auto base = game.ImageBase();
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);

    uintptr_t end = 0;
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
    {
        const auto size = section->SizeOfRawData != 0 ? section->SizeOfRawData : section->Misc.VirtualSize;
        if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
            end = base + section->VirtualAddress + size;
    }
    if (end == 0)
        end = base + game.ImageSize();
    return { base, end };
}
