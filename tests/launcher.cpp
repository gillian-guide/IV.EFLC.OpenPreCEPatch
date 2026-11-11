#include <common.hxx>

#include <cstdio>
#include <cstring>

import common;
import game;
import launcher;
import options;
import patch;

// The launcher path, offline: the payload, the recovered sites, the agreement
// between the key's build mask and the handler's switch, and the two-slot write
// rule. The end-to-end check on the real images is
// `tools/check_launcher_dump.py` (CTest `zp_launcher_dump`), which runs this same
// arm through the pipeline the ASI runs.

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

    // `LaunchGTAIV.exe\0`, the text the scan looks for, as bytes.
    constexpr std::array<std::uint8_t, 16> kOldName = {
        'L', 'a', 'u', 'n', 'c', 'h', 'G', 'T', 'A', 'I', 'V', '.', 'e', 'x', 'e', 0,
    };

    // `PlayGTAIV.exe\0e\0`, the oracle's payload as bytes.
    constexpr std::array<std::uint8_t, 16> kPayload = {
        'P', 'l', 'a', 'y', 'G', 'T', 'A', 'I', 'V', '.', 'e', 'x', 'e', 0, 'e', 0,
    };

    std::string Hex(std::span<const std::uint8_t> bytes)
    {
        std::string text;
        for (const auto byte : bytes)
            text += std::format("{:02X} ", byte);
        if (!text.empty())
            text.pop_back();
        return text;
    }

    std::vector<std::uint8_t> ImageWith(std::initializer_list<std::size_t> slots,
                                        std::size_t size = 0x2000)
    {
        std::vector<std::uint8_t> image(size, 0xCC);
        for (const auto slot : slots)
        {
            std::memcpy(image.data() + slot, kOldName.data(), kOldName.size());
        }
        return image;
    }

    bool Carries(const std::vector<std::uint8_t>& image, std::size_t slot)
    {
        return std::memcmp(image.data() + slot, kPayload.data(), kPayload.size()) == 0;
    }

    void TestPayload()
    {
        Check(Launcher::kName.size() == 16, "launcher: the payload is 16 bytes");

        std::array<std::uint8_t, 16> name{};
        std::memcpy(name.data(), Launcher::kName.data(), name.size());
        Check(Hex(name) == Hex(kPayload), "launcher: the payload is the oracle's PlayGTAIV.exe\\0e\\0",
            Hex(name));

        // The scan text is the old name, and it is the recovered 16-byte string.
        Check(NormaliseSignature(Launcher::kSlotSignature) == Hex(kOldName),
            "launcher: the signature is the LaunchGTAIV.exe\\0 text");
    }

    void TestRecoveredSites()
    {
        const auto sites = Launcher::SlotsFor(Build::v1080);
        Check(sites.size() == 2 && sites[0] == 0xA508F8u && sites[1] == 0xA5A5E4u,
            "launcher: 1.0.8.0's slots are the recovered RVAs");

        Check(Launcher::SlotsFor(Build::v1000).size() == 2 &&
                  Launcher::SlotsFor(Build::v1040).size() == 2 &&
                  Launcher::SlotsFor(Build::v1060).size() == 2 &&
                  Launcher::SlotsFor(Build::v1070).size() == 2,
            "launcher: the switch's other four cases are present");

        for (const Build build : { Build::v1004, Build::v1061, Build::v1100, Build::v1110,
                                   Build::v1120, Build::v1130, Build::v12043, Build::unknown })
        {
            Check(Launcher::SlotsFor(build).empty(),
                "launcher: a build outside the switch has no sites");
        }

        // The catalogue mask and the recovered switch must name the same five
        // builds: the arm refuses a build with no case, and the loader refuses a
        // build outside the mask, so a drift between the two would silently drop a
        // case.
        bool agree = true;
        for (std::uint32_t id = 0; id <= 18; ++id)
        {
            const Build build = FromId(id);
            if (Catalogue::AppliesTo("LaunchWithPlayGTAIV", build) !=
                !Launcher::SlotsFor(build).empty())
            {
                agree = false;
            }
        }
        Check(agree, "launcher: the key's mask is the handler's five-build switch");
    }

    void TestWrites()
    {
        // Two slots at the recovered offsets: both rewritten, nothing else touched.
        {
            auto image = ImageWith({ 0x100, 0x1800 });
            auto pristine = image;
            const std::uint32_t sites[] = { 0x100, 0x1800 };
            const auto written = Launcher::RewriteSlots(image.data(), image.size(), sites);
            Check(written == 2, "launcher: two slots are two stores", std::to_string(written));
            Check(Carries(image, 0x100) && Carries(image, 0x1800),
                "launcher: both slots carry the payload");
            std::size_t changed = 0;
            for (std::size_t index = 0; index < image.size(); ++index)
                changed += image[index] != pristine[index] ? 1 : 0;
            std::size_t differing = 0;
            for (std::size_t index = 0; index < kPayload.size(); ++index)
                differing += kOldName[index] != kPayload[index] ? 1 : 0;
            Check(changed == differing * 2,
                "launcher: every payload byte that differs is stored, in both slots",
                std::to_string(changed));
        }

        // A one-slot expectation against a two-slot image is a view the arm was
        // not recovered from: it writes nothing.
        {
            auto image = ImageWith({ 0x100, 0x1800 });
            auto pristine = image;
            const std::uint32_t sites[] = { 0x100 };
            const auto written = Launcher::RewriteSlots(image.data(), image.size(), sites);
            Check(written == 0, "launcher: a short expectation refuses the arm");
            Check(image == pristine, "launcher: ... and leaves the image untouched");
        }

        // The reverse: a third occurrence refuses the arm too.
        {
            auto image = ImageWith({ 0x100, 0x1800, 0x1C00 });
            auto pristine = image;
            const std::uint32_t sites[] = { 0x100, 0x1800 };
            const auto written = Launcher::RewriteSlots(image.data(), image.size(), sites);
            Check(written == 0, "launcher: a longer view refuses the arm");
            Check(image == pristine, "launcher: ... and leaves the image untouched");
        }

        // A hit that is not at the recovered offset is not the slot the original
        // writes.
        {
            auto image = ImageWith({ 0x100, 0x1810 });
            auto pristine = image;
            const std::uint32_t sites[] = { 0x100, 0x1800 };
            const auto written = Launcher::RewriteSlots(image.data(), image.size(), sites);
            Check(written == 0, "launcher: a slot off its recovered RVA refuses the arm");
            Check(image == pristine, "launcher: ... and leaves the image untouched");
        }

        // The single-slot shape (the EFLC images' text count) writes one store when
        // asked for one.
        {
            auto image = ImageWith({ 0x40 }, 0x1000);
            const std::uint32_t sites[] = { 0x40 };
            Check(Launcher::RewriteSlots(image.data(), image.size(), sites) == 1 &&
                      Carries(image, 0x40),
                "launcher: a one-slot image is written when one site is asked for");
        }

        // No image, no sites: no store, no fault.
        {
            auto image = ImageWith({ 0x100 });
            Check(Launcher::RewriteSlots(nullptr, image.size(), Launcher::SlotsFor(Build::v1080)) == 0,
                "launcher: a null image is refused");
            Check(Launcher::RewriteSlots(image.data(), image.size(),
                      std::span<const std::uint32_t>{}) == 0,
                "launcher: an empty site set is refused");
        }
    }

    void TestProbe()
    {
        Check(Launcher::ProbeOverride() == -1, "launcher: the probe defaults to the process walk");
        Launcher::SetProbeOverride(1);
        Check(Launcher::ProbeOverride() == 1, "launcher: the seam forces the walk's answer on");
        Launcher::SetProbeOverride(0);
        Check(Launcher::ProbeOverride() == 0, "launcher: ... and off");
        Launcher::SetProbeOverride(-1);
        Check(Launcher::ProbeOverride() == -1, "launcher: ... and back to the process walk");
    }
}

int main()
{
    TestPayload();
    TestRecoveredSites();
    TestWrites();
    TestProbe();
    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
