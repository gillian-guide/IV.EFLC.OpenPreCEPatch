module;

#include <common.hxx>

export module config;

import common;

// The INI boundary. The original reads ZolikaPatch.ini through feather-ini-parser
// 1.41, a third-party library it links statically; its grammar is the contract,
// quirks and all (tests/config_contract pins every one of them):
//
//   * a 255-character line buffer, and a fault: a line with a 256th byte sets the
//     stream's `failbit`, which nothing clears, so the original's loop -- whose only
//     exit is `eofbit` -- re-parses the same 255 bytes for ever. This reader models
//     the stream's two flags for that reason, and spins where the original spins
//     (docs/parity.md, Preserved faults);
//   * no BOM branch: the 1.41 source has `if (line[0] == 0xEF)`, but the oracle's
//     build compiles it out (`char` is signed, so a byte 0xEF promotes to -17 and
//     never equals the literal 239). A first line starting with the UTF-8 BOM is an
//     ordinary malformed line: it has no '=', so it is dropped -- and if it carried
//     `[Options]`, that header is lost and the first key line faults on the null
//     current section. The oracle faults here, and so does this tree (docs/parity.md,
//     Preserved faults);
//   * only `#` and `//` start a comment, and only at the start of a line;
//   * a section line is `[...]`, and re-opening a section starts it empty;
//   * a key line is split on '=' with strtok semantics, the key keeps any
//     trailing space (`Key = 1` is the key `Key `, which nothing asks for), and
//     the value keeps its leading space;
//   * an empty value is not stored, so `Key=` falls back to the default;
//   * a repeated key inside one section keeps the last value;
//   * a key before the first section dereferences a null section pointer -- the
//     original's fault, preserved here.
export namespace Feather
{
    using Keys = std::map<std::string, std::string>;

    struct Document
    {
        std::map<std::string, Keys> sections;

        const Keys* Section(std::string_view name) const
        {
            const auto found = sections.find(std::string(name));
            return found == sections.end() ? nullptr : &found->second;
        }

        const std::string* Find(std::string_view section, std::string_view key) const
        {
            const auto* keys = Section(section);
            if (keys == nullptr)
                return nullptr;
            const auto found = keys->find(std::string(key));
            return found == keys->end() ? nullptr : &found->second;
        }
    };

    inline Document Parse(std::string_view text)
    {
        Document document;
        Keys* current = nullptr;

        // The original's loop (`sub_10059D70`), over a text-mode file stream:
        //
        //     char line[256];
        //     while (!file.eof()) { file.getline(line, 256); <parse line> }
        //
        // `line`, `position`, `fail` and `eof` are that stream's buffer, read
        // position, failbit and eofbit. `getline` stores at most 255 bytes and
        // consumes the delimiter when the line fits; a line with a 256th byte sets
        // `failbit` and stops there, and because the loop's only exit is `eofbit` and
        // nothing in the body clears the stream, every later `getline` is a sentry
        // no-op that leaves the buffer alone. That is the original's hang, not a
        // simplification -- `docs/parity.md` records it as a preserved fault.
        std::string line;      // the original's `char line[256]`
        size_t position = 0;   // the stream's read position
        bool fail = false;     // failbit: sticky, set by a line that does not fit
        bool eof = false;      // eofbit: the loop's only exit

        while (!eof)
        {
            if (!fail)
            {
                line.clear();
                while (line.size() < 255 && position < text.size() && text[position] != '\n')
                    line.push_back(text[position++]);
                if (position < text.size() && text[position] == '\n')
                    ++position;
                else if (position < text.size())
                    fail = true;
                else
                    eof = true;
            }

            if (line.empty())
                continue;
            if ((line.size() >= 2 && line[0] == '/' && line[1] == '/') || line[0] == '#')
                continue;

            if (line[0] == '[')
            {
                size_t length = line.size() >= 2 ? line.size() - 2 : 0;
                while (length + 1 < line.size() && std::isspace(static_cast<unsigned char>(line[length + 1])))
                    --length;

                current = &document.sections[line.substr(1, length)];
                current->clear();
                continue;
            }

            const auto start = line.find_first_not_of('=');
            if (start == std::string::npos)
                continue;
            const auto keyEnd = line.find('=', start);
            if (keyEnd == std::string::npos)
                continue;

            std::string key = line.substr(start, keyEnd - start);
            const auto firstNonSpace = key.find_first_not_of(" \t\n\v\f\r");
            key.erase(0, firstNonSpace == std::string::npos ? key.size() : firstNonSpace);

            const auto value = line.substr(keyEnd + 1);
            if (value.empty())
                continue;

            // The original dereferences the selected section pointer here; a key
            // line before the first section line faults it.
            (*current)[key] = value;
        }

        return document;
    }

    // The file is opened by name exactly as the original asks for it. A missing
    // file is an empty document: every option keeps its default.
    inline Document Load(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return Document{};

        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // The original opens the file with `std::ifstream file(wfilename)` in text
        // mode -- `_Fiopen(wchar_t const*, 1, 0x40)` at `0x10059C5B`, where mode 1 is
        // `ios::in` alone (`ios::binary` is 0x20) -- so the CRT folds CRLF to LF
        // before the parser sees it. This reader takes the bytes in binary mode and
        // folds them itself. The `wstring_convert<codecvt_utf8_utf16>` around the
        // call converts the *file name* the loader passes ("ZolikaPatch.ini" at
        // `0x10052425`), not the contents, so nothing else touches the text.
        std::string normalised;
        normalised.reserve(text.size());
        for (size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n')
                continue;
            normalised.push_back(text[index]);
        }

        return Parse(normalised);
    }

    // The value the original would hand to std::stoi: the stored string, or the
    // caller's default text when the key is absent or empty.
    inline const std::string& Value(const Document& document, std::string_view section, std::string_view key, const std::string& fallback)
    {
        const auto* found = document.Find(section, key);
        return found != nullptr ? *found : fallback;
    }
}

// The two per-model light tables. The original's loader preamble `sub_100523C0`
// fills them from the INI before any handler installs, at `0x10052419..0x10052D9B`:
//
//   [PopupLightAngles]      Car<N>Name   -> table.hashes[N-1]  (sub_10003E50)
//                           Car<N>Angle  -> table.values[N-1]  (std::stof, degrees)
//   [HeadlightConeOffsets]  Car<N>Name   -> table.hashes[N-1]  (sub_10003E50)
//                           Car<N>Offset -> table.values[N-1]  (std::stof, raw)
//
// The section is read only when it exists (`setSection` returns false and the
// original jumps past the whole loop), the row counter runs 1..512 inclusive
// (`cmp ..., 200h / jl`), and a row whose name is absent or empty is skipped
// without even looking at its value -- the original compares the fetched value
// with `""` (`sub_1005AF10`) and jumps past both stores on equality. A row whose
// name is present but whose value is absent stores the hash and leaves the value
// at the table's zero, so the model resolves to an offset of 0.0f (or an angle of
// 0 radians), not to the checker's default.
//
// The value is parsed by `std::stof` with no guard (`strtof` plus MSVC's two
// throws at `0x100529B1` / `0x100529BC`), so a value with no digits escapes as
// std::invalid_argument and an out-of-range one as std::out_of_range; a value
// with trailing junk (`5abc`) is truncated to 5.0f, exactly as std::stof does.
// The hash is stored *before* the value is parsed (`0x1005265E` before
// `0x10052763`, `0x10052B0C` before `0x10052C14`), so a throwing value leaves its
// row's hash in the table.
//
// The angle is stored in radians: the original multiplies the parsed float by the
// double pi at `0x10152358` and divides by 180.0 at `0x10152370` in double, then
// narrows to float (`movss` / `cvtps2pd` / `mulsd` / `divsd` / `cvtpd2ps` at
// `0x1005286E..0x10052893`). The offset is stored as parsed.
export namespace Config
{
    // The original's file name, opened relative to the process working directory
    // exactly as it is written here.
    inline constexpr std::string_view FileName = "ZolikaPatch.ini";

    inline Feather::Document& Ini()
    {
        static Feather::Document document;
        return document;
    }

    // Reads the file once. A missing file leaves the document empty, which is the
    // original's behaviour: every option then keeps its default.
    inline bool Load()
    {
        std::error_code ignored;
        const bool exists = std::filesystem::exists(std::filesystem::path(FileName), ignored);
        Ini() = Feather::Load(std::filesystem::path(FileName));
        return exists;
    }

    inline int32_t Integer(const Feather::Document& document, std::string_view key, std::string_view fallback)
    {
        const std::string defaultText(fallback);
        return std::stoi(Feather::Value(document, "Options", key, defaultText));
    }

    inline int32_t Integer(const Feather::Document& document, std::string_view section, std::string_view key, std::string_view fallback)
    {
        const std::string defaultText(fallback);
        return std::stoi(Feather::Value(document, section, key, defaultText));
    }

    inline float Float(const Feather::Document& document, std::string_view section, std::string_view key, std::string_view fallback)
    {
        const std::string defaultText(fallback);
        return std::stof(Feather::Value(document, section, key, defaultText));
    }

    // One recovered table: the 512 name hashes the checker walks and the 512 values
    // it publishes on a match (dword_1015C628 / dword_1015BE28 for
    // [PopupLightAngles], dword_1015ADB8 / dword_1015B5C0 for
    // [HeadlightConeOffsets]). Both start zeroed, as the original's .bss words do.
    inline constexpr size_t ModelTableSlots = 512;

    // A *view* of one table's two arrays. The arrays themselves are four C-linkage
    // objects of their own (declared below) rather than members of one struct: a
    // transcribed body reads each through an absolute displacement, and the offline
    // proof resolves that displacement through the linker map, which names an object
    // and never a member at an offset inside one. That is what lets the popup checker
    // and the cone check be proven at all -- they read the arrays as
    // `[eax+0x1015C628]`-shaped operands, and `tests/bodies.tsv` pairs each array with
    // the original's VA and length (docs/rebuilt.md §2f, §2g).
    struct ModelTable
    {
        std::span<uint32_t> hashes;
        std::span<float> values;
    };

    ModelTable PopupLightAngles();
    ModelTable HeadlightConeOffsets();

    // sub_10003E50's shape: __cdecl, the name, the hash in EAX. The caller passes
    // the transcribed resolver; the reader never hashes a name itself.
    using NameResolver = uint32_t(__cdecl*)(const char*);

    // Fills both tables from `document` in the original's order and with its
    // grammar, defaults and faults (the block comment above the namespace). Called
    // once at startup, before any option is read.
    void ReadModelTables(const Feather::Document& document, NameResolver resolve);
}

// The four recovered arrays, in C linkage because the transcribed bodies and the
// proof's `table` rows name them: dword_1015C628 (popup angles' hashes),
// dword_1015BE28 (their values), dword_1015ADB8 (the cone's hashes) and
// dword_1015B5C0 (its values). The original keeps all four in `.data`'s
// uninitialised tail, so the loader maps zeros and the shipped content is all-zero
// on both sides; the reader above is what fills them, at the same slots and in the
// same order as `sub_100523C0`.
extern "C"
{
    uint32_t zpatch_popup_angle_hashes[Config::ModelTableSlots] = {};
    float zpatch_popup_angle_values[Config::ModelTableSlots] = {};

    // The [HeadlightConeOffsets] block, in the original's `.data` order:
    // dword_1015ADB8's 512 hash slots, then **the two words the original keeps
    // between them and the values**, then dword_1015B5C0's 512 offsets. The
    // original's cone walk (`mov eax, ds:[esi*4+0x1015ADBC]`) runs with ESI up to
    // 511 and therefore reads dword_1015B5B8 as its 513th entry when all 512 slots
    // are filled; the word after that is dword_1015B5BC. Both are the *same slots the
    // rest of the tree already publishes into*, which is why they are defined here
    // rather than copied:
    //
    //   dword_1015B5B8  CarDynamicShadowFix's shadow-function pointer. It is zero in
    //                   the shipped image (the original's `.data` tail past the
    //                   file's raw size) and is written before this check can run by
    //                   the ASI's own car-shadow code, `mov [0x1015B5B8],ecx` at
    //                   0x100128B5 with `ecx = eax+0xB+[eax+7]` (the game call target
    //                   of the recovered `56 6A 02 53 53 57 E8` site, whose resume is
    //                   eax+0xB) and `mov [0x1015B5B8],eax` at 0x10014F98.
    //                   `zpatch_carparts_shadow_fn` is this tree's slot for exactly
    //                   that value, computed by the same formula from the same
    //                   recovered site (source/vehicles.ixx), so the walk reads the
    //                   value the original's does -- not a placeholder.
    //   dword_1015B5BC  this check's own resume, published by its installer
    //                   (`mov [0x1015B5BC],ecx` at 0x1001AEC2) and jumped through by
    //                   the shim. `zpatch_fchs_resume` is that slot here.
    //
    // Laying the four out in the original's order is what makes the walk's first read
    // past slot 511 land on the same word as the original's; the hash table is
    // registered with the original's extended 2,052-byte range (docs/rebuilt.md §2g).
    uint32_t zpatch_headlight_cone_hashes[Config::ModelTableSlots] = {};
    uint32_t zpatch_carparts_shadow_fn = 0;   // dword_1015B5B8
    uint32_t zpatch_fchs_resume = 0;          // dword_1015B5BC
    float zpatch_headlight_cone_values[Config::ModelTableSlots] = {};
}

namespace
{
    // The two recovered doubles: pi (0x400921FB54442D18 at 0x10152358) and 180.0
    // (0x4066800000000000 at 0x10152370). The hex literal is exact.
    constexpr double kPi = 0x1.921fb54442d18p+1;
    constexpr double kDegreesPerHalfTurn = 180.0;

    void ReadModelTable(const Feather::Document& document, std::string_view section,
        std::string_view valueSuffix, Config::ModelTable table, bool degreesToRadians,
        Config::NameResolver resolve)
    {
        // The original asks `setSection` for the section and jumps over the whole
        // loop when it is absent; with the section absent every `Find` below would
        // miss anyway, so this only records the recovered shape.
        if (document.Section(section) == nullptr)
            return;

        for (size_t index = 1; index <= Config::ModelTableSlots; ++index)
        {
            const auto ordinal = std::to_string(index);

            // "Car" + ordinal + "Name", built as the original builds it: a decimal
            // ordinal with no padding, so `Car01Name` is a different key and is
            // never asked for.
            const auto* const name = document.Find(section, "Car" + ordinal + "Name");
            if (name == nullptr || name->empty())
                continue;

            table.hashes[index - 1] = resolve(name->c_str());

            const auto* const value = document.Find(section, "Car" + ordinal + std::string(valueSuffix));
            if (value == nullptr || value->empty())
                continue;

            // Unguarded, like the original's inlined std::stof: a value with no
            // digits throws std::invalid_argument and an out-of-range one throws
            // std::out_of_range out of the loader.
            const float parsed = std::stof(*value);
            table.values[index - 1] = degreesToRadians
                ? static_cast<float>(static_cast<double>(parsed) * kPi / kDegreesPerHalfTurn)
                : parsed;
        }
    }
}

namespace Config
{
    ModelTable PopupLightAngles()
    {
        return { zpatch_popup_angle_hashes, zpatch_popup_angle_values };
    }

    ModelTable HeadlightConeOffsets()
    {
        return { zpatch_headlight_cone_hashes, zpatch_headlight_cone_values };
    }

    void ReadModelTables(const Feather::Document& document, NameResolver resolve)
    {
        // The original's tables are zero-filled .bss written once; a read starts
        // from zero so the caller sees exactly that, and a throw leaves the
        // half-filled tables the original would leave. The four arrays are
        // zero-initialised globals, so this only matters if the reader ever runs
        // twice; it does not reach the cone's extra word, which stays zero (it is not
        // a table slot and the original never writes it).
        for (auto table : { PopupLightAngles(), HeadlightConeOffsets() })
        {
            for (auto& hash : table.hashes)
                hash = 0;
            for (auto& value : table.values)
                value = 0.0f;
        }

        // The original's order: [PopupLightAngles] first (0x10052419), then
        // [HeadlightConeOffsets] (0x10052901). A throw in the first section leaves
        // the second table untouched.
        ReadModelTable(document, "PopupLightAngles", "Angle", PopupLightAngles(), true, resolve);
        ReadModelTable(document, "HeadlightConeOffsets", "Offset", HeadlightConeOffsets(), false, resolve);
    }
}
