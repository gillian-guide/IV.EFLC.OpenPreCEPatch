module;

#include <common.hxx>

export module options;

import common;
import config;
import game;

// The recovered option catalogue: every [Options] key with the value the shipped
// INI carries, the default the original passes when the key is absent, and the
// builds the key applies to. Generated into options.inc by tools/gen_options.py;
// nothing here is hand-maintained data.
export struct Option
{
    std::string_view key;
    int32_t shipped;
    int32_t absent;
    uint32_t builds;
    std::string_view module;
};

export namespace Catalogue
{
    inline constexpr Option All[] = {
#define ZP_OPTION(key, shipped, absent, builds, module, kind) Option{#key, shipped, absent, builds, #module},
#include "options.inc"
#undef ZP_OPTION
    };

    inline const Option* Find(std::string_view key)
    {
        for (const auto& option : All)
        {
            if (option.key == key)
                return &option;
        }
        return nullptr;
    }

    inline size_t Count() { return std::size(All); }

    // The default the arm passes to std::stoi when the key is absent: the shipped
    // value is what the INI carries, the absent value is the arm's own default,
    // and they differ for a handful of keys.
    inline std::string DefaultText(std::string_view key, bool shippedValue)
    {
        const auto* option = Find(key);
        if (option == nullptr)
            return "0";
        return std::to_string(shippedValue ? option->shipped : option->absent);
    }

    // Reads the key the way the original does: std::stoi on the stored text, or
    // on the arm's default when the key is absent.
    // The recovered read plus the OpenPreCEPatch FusionFix gate (config group):
    // an overlapping option is off when a FusionFix ASI is installed, so two
    // hooks cannot fight over the same site. With the group off this is the
    // original read exactly.
    inline int32_t Read(const Option& option)
    {
        if (Config::FusionFixGated(option.key))
            return 0;
        return Config::Integer(Config::Ini(), option.key, std::to_string(option.absent));
    }

    inline int32_t Read(std::string_view key)
    {
        const auto* option = Find(key);
        return option != nullptr ? Read(*option) : 0;
    }

    // A key applies when the current build is in its mask. An unknown build has
    // id 0, which no mask includes: an unsupported executable installs nothing.
    inline bool AppliesTo(const Option& option, Build build)
    {
        return (option.builds & (1u << static_cast<uint32_t>(build))) != 0;
    }

    inline bool AppliesTo(std::string_view key, Build build)
    {
        const auto* option = Find(key);
        return option != nullptr && AppliesTo(*option, build);
    }
}
