#include <common.hxx>
#include <cstdio>
#include <system_error>

import common;
import game;
import native;
import patch;

// The salt rule, pinned on the real game images (files, not a running game).
//
// The original loader preamble scans the game image for
// `68 ? ? ? ? 68 A9 54 14 51 E8` and, on a hit, jumps over its ten salt stores;
// on a miss it writes the fallback set. Native::ResolveSalts() is the layer's
// implementation of that rule, so this program loads each image the same way
// zp_dump does, attaches it to the Game context, calls ResolveSalts() and checks
// the ten slots against the values recovered from the ASI. The three images are
// the two probe-miss builds (1.0.0.0, 1.0.4.0) and 1.0.8.0, where the probe hits.
//
//     zp_salt_check [game_refs]

namespace
{
    struct Slot
    {
        uint32_t reference;  // the ASI slot VA
        uint32_t shipped;    // the ASI's `.data` value
        uint32_t fallback;   // the miss path's store at 0x10052E1E..0x10052E78
    };

    constexpr Slot kSlots[] =
    {
        { 0x10158054u, 0x511454A9u, 0x6B86410Eu },
        { 0x101580C8u, 0x570F5725u, 0x406B4B20u },
        { 0x10158050u, 0x3B0C6738u, 0x6DCD115Au },
        { 0x101580B8u, 0x4E61480Au, 0x62BFDB37u },
        { 0x10158028u, 0x6A6B4F18u, 0x931F73A2u },
        { 0x101580A8u, 0x79A95BF9u, 0x7F6A60D3u },
        { 0x10158040u, 0x4BF36A32u, 0xC9DBDA90u },
        { 0x1015803Cu, 0x022B2DA9u, 0xA4EA0691u },
        { 0x101580D4u, 0x75E005F1u, 0x09399528u },
        { 0x1015805Cu, 0x62E319C6u, 0x1CC648EAu },
    };

    struct Case
    {
        const char* image;
        bool probe;  // whether the probe has a hit on this build
    };

    // The recovered probe result per build: 1.0.8.0 hits at game VA 0xB3878C;
    // 1.0.0.0 and 1.0.4.0 have no hit, so the preamble's stores run.
    constexpr Case kCases[] =
    {
        { "GTAIV.exe", true },        // 1.0.8.0
        { "GTAIV_1000.exe", false },  // 1.0.0.0
        { "GTAIV_1040.exe", false },  // 1.0.4.0
    };

    int checks = 0;
    int failures = 0;

    void Check(bool condition, const char* what, const std::string& detail = {})
    {
        ++checks;
        if (condition)
        {
            std::printf("PASS %s\n", what);
            return;
        }
        ++failures;
        std::printf("FAIL %s", what);
        if (!detail.empty())
            std::printf(" (%s)", detail.c_str());
        std::printf("\n");
    }

    // The mapping the dump uses (tests/dump_main.cpp): the headers and every section
    // at its virtual address, the rest zeroed, in an allocation at the image's own
    // preferred base when that address is free and at the same fixed fallback the
    // dump uses otherwise. Pinning the base is what makes the input a function of
    // the file and not of the run's ASLR; the tool is linked at /BASE:0x20000000
    // (CMakeLists.txt) so its own image is out of the way. The copy is not
    // relocated: nothing here reads a captured operand, and the bytes are the file
    // image at either base. A base that is neither is a loud failure.
    constexpr uintptr_t kFixedImageBase = 0x30000000;

    struct Image
    {
        uint8_t* bytes{};
        size_t size{};

        ~Image()
        {
            if (bytes != nullptr)
                VirtualFree(bytes, 0, MEM_RELEASE);
        }
        Image() = default;
        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;
    };

    enum class Mapped { ok, unreadable, unmappable };

    Mapped Load(const std::filesystem::path& path, Image& image)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return Mapped::unreadable;

        // The headers come first and the copy is mapped before the file body: the
        // whole file is itself a large allocation, and the CRT heap can otherwise
        // take the address the copy needs.
        IMAGE_DOS_HEADER dos{};
        file.read(reinterpret_cast<char*>(&dos), sizeof(dos));
        if (!file)
            return Mapped::unreadable;
        file.seekg(static_cast<std::streamoff>(dos.e_lfanew));
        IMAGE_NT_HEADERS nt{};
        file.read(reinterpret_cast<char*>(&nt), sizeof(nt));
        if (!file)
            return Mapped::unreadable;

        image.size = nt.OptionalHeader.SizeOfImage;
        image.bytes = static_cast<uint8_t*>(VirtualAlloc(
            reinterpret_cast<void*>(nt.OptionalHeader.ImageBase), image.size,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (image.bytes == nullptr)
        {
            const auto error = static_cast<int>(GetLastError());
            std::printf("%s: the preferred base %08zX is not available (VirtualAlloc: error %d: %s); "
                        "pinning the copy at the fixed base %08zX instead\n",
                path.string().c_str(), static_cast<size_t>(nt.OptionalHeader.ImageBase), error,
                std::error_code(error, std::system_category()).message().c_str(),
                static_cast<size_t>(kFixedImageBase));
            image.bytes = static_cast<uint8_t*>(VirtualAlloc(
                reinterpret_cast<void*>(kFixedImageBase), image.size,
                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
            if (image.bytes == nullptr)
            {
                const auto fallback = static_cast<int>(GetLastError());
                std::printf("%s: cannot map the %zX-byte image at the fixed base %08zX either "
                            "(VirtualAlloc: error %d: %s)\n",
                    path.string().c_str(), image.size, static_cast<size_t>(kFixedImageBase),
                    fallback, std::error_code(fallback, std::system_category()).message().c_str());
                return Mapped::unmappable;
            }
        }

        file.clear();
        file.seekg(0);
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (raw.size() < sizeof(IMAGE_DOS_HEADER))
            return Mapped::unreadable;
        const auto* rawDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
        const auto* rawNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(raw.data() + rawDos->e_lfanew);
        std::memset(image.bytes, 0, image.size);

        const auto* section = IMAGE_FIRST_SECTION(rawNt);
        for (WORD index = 0; index < rawNt->FileHeader.NumberOfSections; ++index, ++section)
        {
            const auto count = std::min<size_t>(section->SizeOfRawData, image.size - section->VirtualAddress);
            if (section->PointerToRawData + count <= raw.size())
                std::memcpy(image.bytes + section->VirtualAddress, raw.data() + section->PointerToRawData, count);
        }
        const auto headers = std::min<size_t>(rawNt->OptionalHeader.SizeOfHeaders, raw.size());
        std::memcpy(image.bytes, raw.data(), headers);
        return Mapped::ok;
    }
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const std::filesystem::path gameRefs = argc > 1 ? argv[1] : std::filesystem::path("..\\game_refs");

    for (const auto& test : kCases)
    {
        const auto path = gameRefs / test.image;
        Image image;
        const auto mapped = Load(path, image);
        if (mapped == Mapped::unreadable)
        {
            std::printf("SKIP %s: cannot read %s\n", test.image, path.string().c_str());
            continue;
        }
        if (mapped == Mapped::unmappable)
        {
            // A failed mapping is a harness failure, not a skip: the whole point of
            // the pin is that the input must not depend on where the allocator puts
            // it.
            Check(false, (std::string(test.image) + ": the image maps at its preferred base").c_str());
            continue;
        }

        Game::Get().AttachImage(reinterpret_cast<uintptr_t>(image.bytes), image.size, path, {});
        Native::ResolveSalts();

        // The probe itself, through the layer's scan: a hit is the reason the
        // shipped values survive, so the test pins the branch as well as the
        // slots.
        const bool probe = !find_pattern("68 ? ? ? ? 68 A9 54 14 51 E8").empty();
        Check(probe == test.probe, (std::string(test.image) + ": the probe " +
            (test.probe ? "hits" : "misses")).c_str());

        const char* which = test.probe ? "shipped" : "fallback";
        for (const auto& slot : kSlots)
        {
            const uint32_t expected = test.probe ? slot.shipped : slot.fallback;
            const uint32_t actual = Native::Salt(slot.reference);
            char what[160];
            std::snprintf(what, sizeof(what), "%s: dword_%08X is the %s value", test.image,
                slot.reference, which);
            char detail[96];
            std::snprintf(detail, sizeof(detail), "0x%08X, expected 0x%08X", actual, expected);
            Check(actual == expected, what, detail);
        }
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
