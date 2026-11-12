#include <common.hxx>

#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

import audio;
import config;
import patch;

// DeterministicRadioStart's contract, offline: the recovered track hash and scan
// anchor, the resume offset the naked body relies on (the prologue it replays),
// and the fill/no-fill rule. The arm is an OpenPreCEPatch addition rather than a
// transcription, so it has no body-proof row; the live install is a 5-byte
// signature-anchored jump and its failure mode is a clean skip.

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

    std::vector<std::uint8_t> ImageWith(std::span<const std::uint8_t> prologue)
    {
        std::vector<std::uint8_t> image(0x100, 0xCC);
        std::memcpy(image.data(), prologue.data(), prologue.size());
        return image;
    }

    void TestRecoveredData()
    {
        // RADIO_VLADIVOSTOK_SHVAYNE: the sound-name hash the modded playlist's
        // child dword holds (map_playlist.py over the modded SOUNDS.DAT15).
        Check(Audio::kShvayneTrackHash == 0x1975E2EFu,
            "radio_start: the forced track is RADIO_VLADIVOSTOK_SHVAYNE (0x1975E2EF)");

        // The body replays exactly these five bytes, so the jump covers one whole
        // prologue and the resume is the next instruction (`mov ebp, ecx`).
        Check(Audio::kRadioAdvancePrologue.size() == 5,
            "radio_start: the replayed prologue is five bytes");
        Check(Audio::kRadioAdvancePrologue[0] == 0x83 && Audio::kRadioAdvancePrologue[1] == 0xEC &&
                  Audio::kRadioAdvancePrologue[2] == 0x10,
            "radio_start: the prologue opens with `sub esp,10h`");
        Check(Audio::kRadioAdvancePrologue[3] == 0x53 && Audio::kRadioAdvancePrologue[4] == 0x55,
            "radio_start: the prologue saves EBX and EBP");

        // The scan anchor is the recovered station-advance prologue; it is unique
        // in GTAIV.exe 1.0.8.0 (one hit, 0x7D78B0).
        Check(NormaliseSignature(Audio::kDeterministicRadioStartSignature) ==
                  "83 EC 10 53 55 8B E9 8B 85 00 19 00 00 85 C0",
            "radio_start: the signature is the recovered station-advance prologue",
            NormaliseSignature(Audio::kDeterministicRadioStartSignature));
    }

    void TestResumeOffset()
    {
        auto image = ImageWith(Audio::kRadioAdvancePrologue);
        Check(Audio::DeterministicRadioStartResumeOffset(image, 0) == 5,
            "radio_start: the real prologue resumes five bytes in");

        // One byte different is a different build: refuse rather than replay the
        // wrong instruction count.
        image[2] = 0x18;
        Check(Audio::DeterministicRadioStartResumeOffset(image, 0) == 0,
            "radio_start: a different prologue is refused");

        image = ImageWith(Audio::kRadioAdvancePrologue);
        Check(Audio::DeterministicRadioStartResumeOffset(image, 0xFC) == 0,
            "radio_start: a prologue that would run past the image is refused");
        Check(Audio::DeterministicRadioStartResumeOffset(image, 0x101) == 0,
            "radio_start: an offset past the image is refused");
        Check(Audio::DeterministicRadioStartResumeOffset(std::span<const std::uint8_t>{}, 0) == 0,
            "radio_start: an empty image is refused");
    }

    void TestDecision()
    {
        Check(Audio::DeterministicRadioStartWantsRequest(false, true),
            "radio_start: no pending request and a resolved track fills the request");
        Check(!Audio::DeterministicRadioStartWantsRequest(true, true),
            "radio_start: a pending request is never overridden");
        Check(!Audio::DeterministicRadioStartWantsRequest(false, false),
            "radio_start: a track the category's set does not hold is not requested");
        Check(!Audio::DeterministicRadioStartWantsRequest(true, false),
            "radio_start: neither condition alone fills the request");
    }
    void TestShippedIni()
    {
        const auto path = std::filesystem::path(ZP_SOURCE_DIR) /
                          "data" / "plugins" / "IV.EFLC.OpenPreCEPatch.ini";
        Check(std::filesystem::exists(path), "radio_start: the shipped INI exists");

        // The shipped grouped INI enables the arm (the "shipped intent"), while
        // the code default stays 0 so a run without an INI -- the per-option
        // parity dumps' convention -- installs nothing and the write set they
        // prove is unchanged.
        const auto document = Feather::LoadGrouped(path);
        Check(Config::Integer(document, "DeterministicRadioStart", "0") == 1,
            "radio_start: the shipped INI enables the arm");
        Check(Config::Integer(Feather::Document{}, "DeterministicRadioStart", "0") == 0,
            "radio_start: with no INI the arm is off");
    }
}

int main()
{
    TestRecoveredData();
    TestResumeOffset();
    TestDecision();
    TestShippedIni();

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
