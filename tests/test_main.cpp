#include <common.hxx>
#include <cstdio>
#include <cstring>

import bodies;
import common;
import config;
import game;
import loader;
import options;
import patch;

// The offline suite. No game files: the INI grammar, the build classification,
// the signature normaliser, the scanner on this executable and the event tiers.

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

    // The stored value, or "<absent>".
    std::string Stored(const Feather::Document& document, std::string_view section, std::string_view key)
    {
        const auto* value = document.Find(section, key);
        return value != nullptr ? *value : std::string("<absent>");
    }

    int32_t ReadInt(const Feather::Document& document, std::string_view key, std::string_view fallback)
    {
        return Config::Integer(document, key, fallback);
    }

    void TestConfigGrammar()
    {
        // A section and two keys.
        {
            const auto document = Feather::Parse("[Options]\nA=1\nB=2\n");
            Check(Stored(document, "Options", "A") == "1" && Stored(document, "Options", "B") == "2",
                "ini: a section and its keys are stored");
            Check(ReadInt(document, "A", "0") == 1, "ini: an integer key is read");
        }
        // An absent key yields the caller's default text.
        {
            const auto document = Feather::Parse("[Options]\nA=1\n");
            Check(ReadInt(document, "Missing", "7") == 7, "ini: an absent key keeps the default");
        }
        // An empty value is not stored at all.
        {
            const auto document = Feather::Parse("[Options]\nA=\n");
            Check(Stored(document, "Options", "A") == "<absent>", "ini: an empty value is absent");
            Check(ReadInt(document, "A", "5") == 5, "ini: ... so its default applies");
        }
        // A zero is a value, not an absence.
        {
            const auto document = Feather::Parse("[Options]\nA=0\n");
            Check(ReadInt(document, "A", "5") == 0, "ini: a zero value is read as zero");
        }
        // The key is not trimmed at its end: `Key = 1` is the key `Key `.
        {
            const auto document = Feather::Parse("[Options]\nKey = 1\n");
            Check(Stored(document, "Options", "Key") == "<absent>", "ini: a space before '=' belongs to the key");
            Check(Stored(document, "Options", "Key ") == " 1", "ini: ... so the key is 'Key '");
            Check(ReadInt(document, "Key", "9") == 9, "ini: ... and the documented key falls back to its default");
        }
        // The value keeps its leading space, which std::stoi tolerates.
        {
            const auto document = Feather::Parse("[Options]\nA= 1\n");
            Check(Stored(document, "Options", "A") == " 1", "ini: a space after '=' stays in the value");
            Check(ReadInt(document, "A", "9") == 1, "ini: ... and stoi reads through it");
        }
        // A value keeps trailing spaces too.
        {
            const auto document = Feather::Parse("[Options]\nA=1  \n");
            Check(Stored(document, "Options", "A") == "1  ", "ini: a trailing space stays in the value");
            Check(ReadInt(document, "A", "9") == 1, "ini: ... and stoi stops at it");
        }
        // Number parsing stops at the first non-digit, like std::stoi.
        {
            const auto document = Feather::Parse("[Options]\nA=1abc\n");
            Check(ReadInt(document, "A", "9") == 1, "ini: stoi stops at the first non-digit");
        }
        // Duplicate keys: the last one wins.
        {
            const auto document = Feather::Parse("[Options]\nA=1\nA=2\n");
            Check(ReadInt(document, "A", "9") == 2, "ini: a duplicate key keeps the last value");
        }
        // Re-opening a section starts it empty: its earlier keys are lost.
        {
            const auto document = Feather::Parse("[Options]\nA=1\n[Other]\nB=2\n[Options]\nC=3\n");
            Check(Stored(document, "Options", "A") == "<absent>", "ini: re-opened section loses earlier keys");
            Check(ReadInt(document, "A", "9") == 9, "ini: ... so A falls back to its default");
            Check(ReadInt(document, "C", "9") == 3, "ini: ... and later keys are kept");
            Check(Stored(document, "Other", "B") == "2", "ini: another section is untouched");
        }
        // Comments: '#' and '//' only, and only at the start of a line.
        {
            const auto document = Feather::Parse("[Options]\n#A=1\n//B=2\nC=3\n");
            Check(Stored(document, "Options", "A") == "<absent>" && Stored(document, "Options", "B") == "<absent>",
                "ini: '#' and '//' comment lines are skipped");
            Check(Stored(document, "Options", "C") == "3", "ini: a key after comments is read");
        }
        // ';' is not a comment: a ';' line with '=' becomes a key.
        {
            const auto document = Feather::Parse("[Options]\n; note=1\nA=2\n");
            Check(Stored(document, "Options", "; note") == "1", "ini: ';' is not a comment character");
            Check(Stored(document, "Options", "A") == "2", "ini: ... and does not disturb the next key");
        }
        // A key line with no '=' is dropped.
        {
            const auto document = Feather::Parse("[Options]\njust text\nA=1\n");
            Check(Stored(document, "Options", "just text") == "<absent>", "ini: a line without '=' is dropped");
            Check(ReadInt(document, "A", "9") == 1, "ini: ... and the next key is read");
        }
        // A line that starts with '=' has no key: dropped.
        {
            const auto document = Feather::Parse("[Options]\n=1\n");
            const auto* options = document.Section("Options");
            Check(options != nullptr && options->empty(), "ini: a line starting with '=' is dropped");
        }
        // strtok on '=': only the first '=' splits.
        {
            const auto document = Feather::Parse("[Options]\nA=B=C\n");
            Check(Stored(document, "Options", "A") == "B=C", "ini: only the first '=' splits a key line");
        }
        // A three-token line: key before the first '=', value after it.
        {
            const auto document = Feather::Parse("[Options]\nA=a=b=c\n");
            Check(Stored(document, "Options", "A") == "a=b=c", "ini: the value is everything after the first '='");
        }
        // Keys are case sensitive.
        {
            const auto document = Feather::Parse("[Options]\nKey=1\n");
            Check(Stored(document, "Options", "key") == "<absent>", "ini: keys are case sensitive");
            Check(Stored(document, "Options", "Key") == "1", "ini: ... and match exactly");
            Check(Stored(document, "options", "Key") == "<absent>", "ini: sections are case sensitive");
        }
        // There is no BOM branch: feather 1.41 has one, but the oracle's build
        // compiles it out (signed `char` against 239), so a BOM-prefixed first line
        // is an ordinary malformed line -- no '=' means it is dropped, and the
        // `[Options]` header it carried is lost. The fault that leaves when a key
        // line follows is pinned in `zp_fault_parity`.
        {
            const auto document = Feather::Parse("\xEF\xBB\xBF[Options]\n[Other]\nA=1\n");
            Check(document.Section("Options") == nullptr, "ini: a BOM line is not a section header");
            Check(Stored(document, "Other", "A") == "1", "ini: ... and the parse continues past it");
        }
        // A BOM is not special on any line: past the first it is part of the key.
        {
            const auto document = Feather::Parse("[Options]\n" "\xEF\xBB\xBF" "A=1\n");
            Check(Stored(document, "Options", "\xEF\xBB\xBF" "A") == "1", "ini: a BOM inside a line is part of its key");
        }
        // The 255-character line buffer's boundary: a 255-byte line is the longest
        // that fits. A 256th byte sets the stream's sticky failbit and the original
        // re-reads the same buffer for ever; `zp_fault_parity` pins that hang with a
        // child process, because it cannot be run in this one.
        {
            std::string text = "[Options]\nA=";
            text.append(253, 'x');
            text += "\nB=1\n";
            const auto document = Feather::Parse(text);
            const auto* value = document.Find("Options", "A");
            Check(value != nullptr && value->size() == 253, "ini: a 255-byte line is read whole",
                value != nullptr ? std::to_string(value->size()) : "absent");
            Check(ReadInt(document, "B", "0") == 1, "ini: ... and the next line still parses");
        }
        // An empty file is an empty document.
        {
            const auto document = Feather::Parse("");
            Check(document.sections.empty(), "ini: an empty file yields no section");
        }
        // A section line with trailing spaces still names the section.
        {
            const auto document = Feather::Parse("[Options]   \nA=1\n");
            Check(Stored(document, "Options", "A") == "1", "ini: trailing spaces after a section header are ignored");
        }
    }

    void TestMissingFile()
    {
        const auto document = Feather::Load(std::filesystem::path("this-file-does-not-exist.ini"));
        Check(document.sections.empty(), "ini: a missing file is an empty document");

        std::error_code ignored;
        const auto path = std::filesystem::current_path() / "zp-contract-crlf.ini";
        std::ofstream(path, std::ios::binary | std::ios::trunc) << "[Options]\r\nA=1\r\nB=2\r\n";
        const auto crlf = Feather::Load(path);
        std::filesystem::remove(path, ignored);

        Check(Stored(crlf, "Options", "A") == "1" && Stored(crlf, "Options", "B") == "2",
            "ini: a CRLF file parses like an LF file");
    }

    void TestBuilds()
    {
        Check(Classify(Game::Version{ 1, 0, 8, 0 }) == Build::v1080, "build: 1.0.8.0 is v1080");
        Check(Classify(Game::Version{ 1, 0, 7, 0 }) == Build::v1070, "build: 1.0.7.0 is v1070");
        Check(Classify(Game::Version{ 1, 0, 4, 0 }) == Build::v1040, "build: 1.0.4.0 is v1040");
        Check(Classify(Game::Version{ 1, 0, 0, 4 }) == Build::v1004, "build: 1.0.0.4 is v1004");
        Check(Classify(Game::Version{ 1, 0, 6, 1 }) == Build::v1061, "build: 1.0.6.1 is v1061");
        Check(Classify(Game::Version{ 1, 1, 2, 0 }) == Build::v1120, "build: 1.1.2.0 is v1120");
        Check(Classify(Game::Version{ 1, 2, 0, 43 }) == Build::v12043, "build: 1.2.0.43 is v12043");
        Check(Classify(Game::Version{ 1, 0, 9, 0 }) == Build::unknown, "build: an unsupported version is unknown");
        Check(CompactVersion(Game::Version{ 1, 2, 0, 43 }) == "12043", "build: fields are concatenated as text");
        Check(BuildIs(Build::v1080, { Build::v1070, Build::v1080 }), "build: a build list contains its member");
        Check(!BuildIs(Build::v1040, { Build::v1070, Build::v1080 }), "build: a build list excludes a stranger");
        Check(BuildMask({ Build::v1080 }) == (1u << 9), "build: the mask is indexed by the build id");
    }

    void TestSignatures()
    {
        Check(NormaliseSignature("E8 ?? ?? ?? ?? 90") == "E8 ? ? ? ? 90", "signature: IDA wildcards are collapsed");
        Check(NormaliseSignature("e8 11 22") == "E8 11 22", "signature: hex digits are uppercased");
        Check(NormaliseSignature("  E8   90  ") == "E8 90", "signature: runs of spaces are collapsed");
        Check(NormaliseSignature("E8 ? 90") == "E8 ? 90", "signature: a single wildcard stays one wildcard");

        // Game::Initialise is required before a scan: the range comes from the
        // image the context names.
        Game::Get().Initialise(GetThisModuleHandle());
        auto found = find_pattern("DE AD BE EF", "4D 5A 90 00 03 00 00 00");
        Check(!found.empty(), "signature: find_pattern falls through to the variant that matches");
        Check(found.get_first() == GetModuleHandleW(nullptr), "signature: ... at the image's DOS header");
    }

    void TestCatalogue()
    {
        Check(Catalogue::Count() == 84, "catalogue: the shipped keys are present", std::to_string(Catalogue::Count()));
        const auto* carShadow = Catalogue::Find("CarDynamicShadowFix");
        Check(carShadow != nullptr && carShadow->shipped == 1 && carShadow->absent == 0,
            "catalogue: shipped and absent defaults are both recorded");
        Check(Catalogue::Find("NoSuchKey") == nullptr, "catalogue: an unknown key is not in the catalogue");
        Check(!Catalogue::AppliesTo("BenchmarkFix", Build::v1080), "catalogue: BenchmarkFix does not apply to 1.0.8.0");
        Check(Catalogue::AppliesTo("BenchmarkFix", Build::v1070), "catalogue: ... and does apply to 1.0.7.0");
        Check(Catalogue::AppliesTo("RestoreDeathMusic", Build::v1080),
          "catalogue: an all-builds key applies to 1.0.8.0");
        Check(!Catalogue::AppliesTo("RestoreDeathMusic", Build::unknown),
          "catalogue: an unknown build applies nothing");
        Check(Catalogue::DefaultText("CarDynamicShadowFix", false) == "0", "catalogue: the absent default is the arm default");
    }

    void TestBodyCanonicalisation()
    {
        // A five-byte `jmp` written by a pass, pointing at a body that lives in
        // this process. The dump has to turn it into the body's recovered VA.
        std::array<uint8_t, 16> image{};
        std::array<uint8_t, 16> pristine{};
        constexpr uintptr_t base = 0x00400000;
        constexpr uint32_t reference = 0x10001234;
        static int fakeBody = 0;

        Bodies::Register(reinterpret_cast<uintptr_t>(&fakeBody), reference);

        image[0] = 0xE9;
        const auto site = static_cast<uintptr_t>(base);
        const auto displacement = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&fakeBody) - (site + 5));
        std::memcpy(image.data() + 1, &displacement, sizeof(displacement));
        pristine[0] = 0x90;  // the pass wrote this byte

        const auto rewritten = Bodies::Canonicalise(image.data(), pristine.data(), image.size(), base, base);
        Check(rewritten == 1, "bodies: the branch to a registered body is canonicalised");

        int32_t canonical{};
        std::memcpy(&canonical, image.data() + 1, sizeof(canonical));
        Check(static_cast<uint32_t>(site + 5 + canonical) == reference,
            "bodies: the rewritten displacement names the recovered VA");

        // A branch the pass did not write is left alone.
        std::array<uint8_t, 8> untouched{};
        std::array<uint8_t, 8> samePrinstine{};
        untouched[0] = 0xE9;
        samePrinstine[0] = 0xE9;
        Check(Bodies::Canonicalise(untouched.data(), samePrinstine.data(), untouched.size(), base, base) == 0,
            "bodies: a branch that was not written is left alone");
    }

    void TestEvents()
    {
        FusionFix::Event<> event;
        std::string order;
        auto first = event += [&] { order += 'A'; };
        event += [&] { order += 'B'; };
        event.executeAll();
        Check(order == "AB", "event: handlers run in subscription order", order);
        first();
        order.clear();
        event.executeAll();
        Check(order == "B", "event: the token unsubscribes its handler");
    }

    void TestLoads(const std::filesystem::path& asi)
    {
        HMODULE module = LoadLibraryW(asi.c_str());
        Check(module != nullptr, "load: the ASI loads", asi.string());
        if (module == nullptr)
            return;
        FreeLibrary(module);
    }
}

int main(int argc, char** argv)
{
    // A faulting body is a test outcome here, never a message box on the
    // desktop of whoever runs the suite.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    TestConfigGrammar();
    TestMissingFile();
    TestBuilds();
    TestSignatures();
    TestCatalogue();
    TestBodyCanonicalisation();
    TestEvents();


    if (argc > 1)
        TestLoads(std::filesystem::path(argv[1]));
    else
        Check(false, "load: the ASI path was passed to the test");

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
