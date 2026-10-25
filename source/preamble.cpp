// SPDX-License-Identifier: GPL-3.0-or-later

#include "patches.hxx"

#include "pattern.hxx"
#include "rebased.h"

namespace opp
{
    namespace
    {
        constexpr uint32_t kCallOperandOffset = 6u;
        constexpr uint32_t kResolverSignatureLength = 10u;
        constexpr uint32_t kPreFn59C74Offset = 42u;
        constexpr uint32_t kPreFn5D63COffset = 48u;
        constexpr uint32_t kPreFn59CDCOffset = 48u;

        uint32_t ReadDword(const uint8_t* address)
        {
            uint32_t value = 0;
            std::memcpy(&value, address, sizeof(value));
            return value;
        }

        uint32_t AddressOf(const void* pointer)
        {
            return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
        }
    } // namespace

    PreambleReport FindGamePointers(GameImage image) noexcept
    {
        PreambleReport report;
        if (!image.base || image.size == 0)
            return report;

        const ModuleView view{image.base, image.size};
        const auto resolve = [&](std::string_view signature) -> const uint8_t* {
            Match found;
            return FindPattern(view, signature, found) ? found.address : nullptr;
        };
        // The seven counted scans; the build-probe salt fallback below is not one.
        const auto scan = [&](std::string_view signature) -> const uint8_t* {
            if (const uint8_t* hit = resolve(signature))
            {
                ++report.resolved;
                return hit;
            }
            ++report.missing;
            return nullptr;
        };

        if (const uint8_t* hit = scan("08 0B C2 8B F0 E8"))
        {
            zpatch_driveby_resolver =
                ReadDword(hit + kCallOperandOffset) + AddressOf(hit) + kResolverSignatureLength;
        }

        if (const uint8_t* hit = scan("85 F6 74 30 8B 16 8B 42 0C 8B CE FF D0 3C 02 75 23"))
            zpatch_pre_fn_59c74 = AddressOf(hit) - kPreFn59C74Offset;

        if (const uint8_t* hit =
                scan("8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"))
            zpatch_model_lookup = AddressOf(hit);

        if (const uint8_t* hit = scan("85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07"))
            zpatch_pre_ped_array = ReadDword(hit - sizeof(uint32_t));

        if (const uint8_t* hit =
                scan("56 8B F1 8B 06 8B 90 A0 00 00 00 57 FF D2 85 C0 74 1C"))
            zpatch_pre_fn_59b88 = AddressOf(hit);

        if (const uint8_t* hit =
                scan("0F 10 5E 14 F3 0F 10 66 18 F3 0F 10 4C 24 0C F3"))
            zpatch_pre_fn_5d63c = AddressOf(hit) - kPreFn5D63COffset;

        if (const uint8_t* hit =
                scan("0F 10 16 F3 0F 10 5E 04 F3 0F 10 4C 24 0C F3 0F 10 66 08 F2 0F 5A C0"))
            zpatch_pre_fn_59cdc = AddressOf(hit) - kPreFn59CDCOffset;

        // Build-probe signature: absent on an unknown build, so all ten hash salts
        // fall back to these values.
        if (resolve("68 ? ? ? ? 68 A9 54 14 51 E8") == nullptr)
        {
            zpatch_salt_28 = 0x931F73A2u;
            zpatch_salt_3c = 0xA4EA0691u;
            zpatch_salt_40 = 0xC9DBDA90u;
            zpatch_salt_50 = 0x6B86410Eu;
            zpatch_salt_54 = 0x6DCD115Au;
            zpatch_salt_5c = 0x1CC648EAu;
            zpatch_salt_a8 = 0x7F6A60D3u;
            zpatch_salt_b8 = 0x62BFDB37u;
            zpatch_salt_c8 = 0x406B4B20u;
            zpatch_salt_d4 = 0x09399528u;
        }
        return report;
    }
} // namespace opp
