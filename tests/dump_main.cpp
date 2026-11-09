#include <common.hxx>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <system_error>

import bodies;
import common;
import config;
import game;
import loader;
import options;
import shared;
import native;
import stability;
import vehicles;
import useless_checks;
import graphics;
import launcher;
import loading;
import audio;
import misc;
import menu;
import ooc;

// The write-set dump: load a copy of a game image into this process, apply the
// image's own relocations to it (which is what the game's loader does), run the
// same load pipeline the ASI runs, and print every byte it changed as
// `<RVA> <byte>` in RVA order. That is the parity artefact -- it is what the
// per-build baselines and the original's oracle are compared against.
//
// The copy is mapped at the image's own preferred base (`0x00400000`) when that
// address is free -- the loader delta is then exactly zero -- and otherwise at the
// fixed base `kFixedImageBase`. Either way the delta is a constant, so the dump's
// bytes are a function of the source image and the arms only, never of the run's
// ASLR. That pin is what makes this a stable oracle. A Win32 process cannot have
// 0x00400000: the loader maps the NLS section `C_1252.NLS` there before any user
// code runs and the heap follows it, so the 26 MB range is never free (measured,
// and the reference tree records the same -- `../readable/CMakeLists.txt` builds
// its dump 64-bit for this reason). The fixed fallback keeps the oracle stable;
// it is not the image's own coordinate system, so the `.reloc` pass runs with a
// non-zero constant delta and `ApplyRelocations` refuses any base it did not pin.
//
// The relocation pass moves every absolute operand the image carries by
// `hostBase - OptionalHeader.ImageBase`; at a *run-dependent* delta a signature
// whose fixed bytes span the high half of a HIGHLOW fixup matches on some runs and
// not others, and the row convention's high-half suppression can hide an arm's
// write on some runs and report it on others. Both were measured before the pin
// (docs/parity.md, "The per-option dump pins its image base"). A
// captured pattern operand read out of the buffer is a live address in *this*
// process at either pinned base -- exactly what it is in the game -- so an arm's
// identity cast is right in both places, and the dump cannot reward a per-arm
// rebase that would be wrong at run time.
//
// The tool itself is linked at `/BASE:0x20000000` (CMakeLists.txt) so its own
// image cannot occupy the address the copy needs. A base that is neither the
// preferred one nor the fixed one is a loud failure: an unstable oracle is worse
// than a missing one.

namespace
{
    // The base the copy falls back to when the image's preferred base is not free.
    // It is a constant on purpose: the whole point of the pin is that two runs of
    // this binary produce the same bytes, so the copy must never sit where the
    // allocator happens to put it. 0x30000000 is clear of the image's preferred
    // 0x00400000, of this tool's own /BASE:0x20000000 and of the ASI's 0x10000000.
    // The image's preferred base is tried first (Load), and delta 0 is preferred.
    constexpr uintptr_t kFixedImageBase = 0x30000000;

    struct Image
    {
        uint8_t* bytes{};                  // the copy the arms run against, at the pinned base
        size_t size{};
        uintptr_t imageBase{};             // the image's own preferred base
        std::vector<uint8_t> unrelocated;  // the copy before the .reloc pass
        std::vector<uint8_t> pristine;     // the copy after it, before any arm ran
        std::vector<size_t> fixupFields;   // the first byte of every HIGHLOW fixup

        ~Image()
        {
            if (bytes != nullptr)
                VirtualFree(bytes, 0, MEM_RELEASE);
        }
        Image() = default;
        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;
    };

    // Is `rva` inside a section the image marks writable? Used by the relocated
    // operand check: an arm that writes through a captured operand has to land in a
    // writable word of the image, or the survey has matched the wrong instruction.
    bool WritableRva(const Image& image, size_t rva)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.bytes);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.bytes + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            if (rva < section->VirtualAddress ||
                rva - section->VirtualAddress >= section->Misc.VirtualSize)
                continue;
            return (section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        }
        return false;
    }

    // The range a signature scan covers, from game.ixx's ImageScanRange: the image
    // base to the end of the last executable section. In this buffer the offset past
    // the first byte is the RVA, so the range needs no base arithmetic.
    std::pair<size_t, size_t> ExecutableRange(const Image& image)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.bytes);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.bytes + dos->e_lfanew);
        size_t end = 0;
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            const auto length = section->SizeOfRawData != 0 ? section->SizeOfRawData : section->Misc.VirtualSize;
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
                end = section->VirtualAddress + length;
        }
        if (end == 0)
            end = image.size;
        return { 0, std::min(end, image.size) };
    }

    // The check that the mapped copy speaks the image's own coordinate system: an
    // arm captures a dword out of the image and casts it to a pointer. Measured at
    // IncreaseVehicleModelLimit's anchor, whose `mov ecx, imm32` at match+77 names
    // the model-limit object: the unrelocated dword must be a preferred-base VA of
    // this image, the copy's dword must name `hostBase + RVA` (the .reloc pass has
    // run, so at the preferred base the two are the same value and at the fixed base
    // they differ by its constant delta), and the object it names must be a writable
    // word of the buffer. A copy mapped at neither pinned base, or an operand that
    // is not this image's VA, fails loudly here instead of the dump measuring the
    // arm against an address that is not in this process.
    bool VerifyRelocatedOperand(const Image& image, const std::vector<uint8_t>& unrelocated)
    {
        // The recovered 1.0.8.0 anchor is build-agnostic: it is the allocator's
        // model-bound loop, and every supported image carries it once in .text.
        static constexpr uint8_t kAnchor[] = { 0x83, 0xC7, 0x01, 0x81, 0xFF, 0x18, 0x79, 0x00, 0x00, 0x7C, 0xD2 };
        const auto [begin, end] = ExecutableRange(image);
        const auto* first = image.bytes + begin;
        const auto* last = image.bytes + end;
        const auto* found = std::search(first, last, std::begin(kAnchor), std::end(kAnchor));
        if (found == last)
        {
            std::printf("relocation check: IncreaseVehicleModelLimit's anchor is absent; skipped\n");
            return true;
        }

        const size_t anchor = static_cast<size_t>(found - image.bytes);
        const size_t field = anchor + 77;
        if (field + sizeof(uint32_t) > image.size)
        {
            std::printf("relocation check: FAILED: the operand field at RVA %zX is outside the buffer\n", field);
            return false;
        }

        uint32_t preferred{};
        uint32_t relocated{};
        std::memcpy(&preferred, unrelocated.data() + field, sizeof(preferred));
        std::memcpy(&relocated, image.bytes + field, sizeof(relocated));

        if (preferred < image.imageBase || preferred - image.imageBase >= image.size)
        {
            std::printf("relocation check: FAILED: the unrelocated operand at RVA %zX is %08X, "
                        "not a preferred-base VA of this image (base %zX)\n",
                field, preferred, static_cast<uint32_t>(image.imageBase));
            return false;
        }

        const auto expected = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(image.bytes) + (preferred - static_cast<uint32_t>(image.imageBase)));
        if (relocated != expected)
        {
            std::printf("relocation check: FAILED: the operand at RVA %zX is %08X, expected %08X "
                        "(host base %08X, image base %08X); the copy is not the image's "
                        "coordinate system\n",
                field, relocated, expected, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image.bytes)),
                static_cast<uint32_t>(image.imageBase));
            return false;
        }

        const auto target = static_cast<size_t>(relocated - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image.bytes)));
        if (target + sizeof(uint32_t) > image.size || !WritableRva(image, target))
        {
            std::printf("relocation check: FAILED: the relocated operand %08X (RVA %zX) is not a "
                        "writable dword of the buffer\n", relocated, target);
            return false;
        }

        std::printf("relocation check: RVA %zX operand %08X -> %08X (writable RVA %zX): OK\n",
            field, preferred, relocated, target);
        return true;
    }

    // Applies the image's `.reloc` entries with the delta the Windows loader would
    // use. Load maps the copy at the image's preferred base when that address is
    // free -- the delta is then zero and the pass adds nothing -- and at
    // kFixedImageBase otherwise, so the delta is one of exactly two constants. The
    // pass still runs in both modes, because that is the loader's behaviour and
    // because this is where the base is asserted; anything else is refused loudly
    // instead of being measured. A file with no `.reloc` cannot be modelled
    // (nothing would tell a relocated copy from a preferred-base one), and a fixup
    // outside the buffer is a corrupt image, so both are refused too.
    bool ApplyRelocations(Image& image, const std::vector<uint8_t>& unrelocated, const std::string& name)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.bytes);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.bytes + dos->e_lfanew);
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (directory.VirtualAddress == 0 || directory.Size == 0)
        {
            std::printf("%s: refused: the image has no .reloc directory, so a copy at another base "
                        "cannot be modelled\n", name.c_str());
            return false;
        }
        if (static_cast<size_t>(directory.VirtualAddress) + directory.Size > image.size)
        {
            std::printf("%s: refused: the .reloc directory (RVA %zX, %zX byte(s)) runs past the "
                        "image buffer (%zX)\n",
                name.c_str(), static_cast<size_t>(directory.VirtualAddress),
                static_cast<size_t>(directory.Size), image.size);
            return false;
        }

        const auto hostBase = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image.bytes));
        if (hostBase != static_cast<uint32_t>(image.imageBase) &&
            hostBase != static_cast<uint32_t>(kFixedImageBase))
        {
            std::printf("%s: refused: the copy is at %08X, neither the image's preferred base %08X "
                        "nor the fixed base %08X; the dump does not measure a base it did not pin\n",
                name.c_str(), hostBase, static_cast<uint32_t>(image.imageBase),
                static_cast<uint32_t>(kFixedImageBase));
            return false;
        }
        const auto delta = static_cast<uint32_t>(hostBase - static_cast<uint32_t>(image.imageBase));

        auto* bytes = image.bytes;
        image.fixupFields.clear();
        const size_t end = static_cast<size_t>(directory.VirtualAddress) + directory.Size;
        size_t applied = 0;
        size_t offset = directory.VirtualAddress;
        while (offset + sizeof(IMAGE_BASE_RELOCATION) <= end)
        {
            const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(bytes + offset);
            if (block->SizeOfBlock == 0)
                break;
            if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION) || offset + block->SizeOfBlock > end)
            {
                std::printf("%s: refused: malformed .reloc block at RVA %zX (%zX byte(s))\n",
                    name.c_str(), offset, static_cast<size_t>(block->SizeOfBlock));
                return false;
            }

            const auto count =
                (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
            const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
            for (size_t index = 0; index < count; ++index)
            {
                const auto type = entries[index] >> 12;
                const size_t rva = block->VirtualAddress + (entries[index] & 0x0FFF);
                if (type == IMAGE_REL_BASED_ABSOLUTE)
                    continue;
                if (type != IMAGE_REL_BASED_HIGHLOW)
                {
                    std::printf("%s: refused: relocation type %u at RVA %zX (a 32-bit image only "
                                "has HIGHLOW entries here)\n", name.c_str(), type, rva);
                    return false;
                }
                if (rva + sizeof(uint32_t) > image.size)
                {
                    std::printf("%s: refused: a HIGHLOW fixup at RVA %zX lands outside the "
                                "%zX-byte image buffer\n", name.c_str(), rva, image.size);
                    return false;
                }

                uint32_t value{};
                std::memcpy(&value, bytes + rva, sizeof(value));
                value += delta;
                std::memcpy(bytes + rva, &value, sizeof(value));
                image.fixupFields.push_back(rva);
                ++applied;
            }
            offset += block->SizeOfBlock;
        }

        if (applied == 0)
        {
            std::printf("%s: refused: the .reloc directory holds no HIGHLOW fixup\n", name.c_str());
            return false;
        }
        std::sort(image.fixupFields.begin(), image.fixupFields.end());

        std::printf("%s: %zu HIGHLOW fixup(s) applied, delta %08X\n", name.c_str(), applied, delta);
        return VerifyRelocatedOperand(image, unrelocated);
    }

    // Copies the headers and every section to its virtual address, leaving the
    // rest zeroed, which is what the loader maps, then relocates the copy. The
    // allocation is the image's preferred base when it is free and kFixedImageBase
    // otherwise, so the loader delta is a constant and the copy is reproducible.
    bool Load(const std::filesystem::path& path, Image& image)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            std::printf("cannot open %s\n", path.string().c_str());
            return false;
        }

        // The headers are read first and the copy is mapped before the file body:
        // the whole file is itself a large allocation, and the CRT heap can otherwise
        // take the very address the copy needs (measured: the 26 MB buffer landed on
        // 0x00400000 and the mapping then failed with ERROR_INVALID_ADDRESS).
        IMAGE_DOS_HEADER dos{};
        file.read(reinterpret_cast<char*>(&dos), sizeof(dos));
        if (!file)
            return false;
        file.seekg(static_cast<std::streamoff>(dos.e_lfanew));
        IMAGE_NT_HEADERS nt{};
        file.read(reinterpret_cast<char*>(&nt), sizeof(nt));
        if (!file)
            return false;

        image.size = nt.OptionalHeader.SizeOfImage;
        image.imageBase = nt.OptionalHeader.ImageBase;
        // The image's own preferred base first: that is its own coordinate system,
        // and the delta is then zero. A Win32 process cannot have it -- the loader
        // maps the NLS section C_1252.NLS at 0x00400000 before any user code runs
        // and the heap follows, so the 26 MB range is never free (docs/parity.md,
        // "The per-option dump pins its image base"). The copy then goes
        // to a fixed 64 KiB-aligned base, never to the allocator's choice: a
        // run-dependent delta is the defect this tool exists to avoid, and an
        // unstable oracle is worse than a missing one. The tool is linked at
        // /BASE:0x20000000 so its own image is out of both addresses' way.
        image.bytes = static_cast<uint8_t*>(VirtualAlloc(
            reinterpret_cast<void*>(image.imageBase), image.size,
            MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (image.bytes == nullptr)
        {
            const auto error = static_cast<int>(GetLastError());
            std::printf("%s: the preferred base %08zX is not available (VirtualAlloc: error %d: %s); "
                        "pinning the copy at the fixed base %08zX instead\n",
                path.string().c_str(), static_cast<size_t>(image.imageBase), error,
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
                return false;
            }
        }

        // Now the whole file, whose headers the copy above already read once.
        file.clear();
        file.seekg(0);
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (raw.size() < sizeof(IMAGE_DOS_HEADER))
            return false;
        const auto* rawDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
        const auto* rawNt = reinterpret_cast<const IMAGE_NT_HEADERS*>(raw.data() + rawDos->e_lfanew);
        std::memset(image.bytes, 0, image.size);

        const auto* section = IMAGE_FIRST_SECTION(rawNt);
        for (WORD index = 0; index < rawNt->FileHeader.NumberOfSections; ++index, ++section)
        {
            const auto count = std::min<size_t>(section->SizeOfRawData, image.size - section->VirtualAddress);
            if (section->PointerToRawData + count <= raw.size() && count <= image.size)
                std::memcpy(image.bytes + section->VirtualAddress, raw.data() + section->PointerToRawData, count);
        }
        // The headers are part of the image too.
        const auto headers = std::min<size_t>(rawNt->OptionalHeader.SizeOfHeaders, raw.size());
        std::memcpy(image.bytes, raw.data(), headers);

        // The unrelocated copy is what the relocated-operand check measures against,
        // and it is also the coordinate system the baselines speak: a row is a byte
        // the mod changed relative to the original image.
        std::vector<uint8_t> unrelocated(image.bytes, image.bytes + image.size);
        if (!ApplyRelocations(image, unrelocated, path.string()))
            return false;

        image.unrelocated = std::move(unrelocated);
        image.pristine.assign(image.bytes, image.bytes + image.size);
        return true;
    }

    // The dump speaks the baselines' convention: a row is a byte the mod changed,
    // expressed in the *original* image's coordinate system.
    //
    //  * A byte outside a fixup is compared against the unrelocated copy (which is
    //    what the image held there).
    //  * A byte in the low half of a fixup dword is also compared against the
    //    unrelocated copy: a loader delta is 64 KiB-aligned, so relocation does not
    //    move those bytes and anything that differs is the mod's.
    //  * A byte in the high half of a fixup dword is compared against the relocated
    //    pristine copy: at a non-zero delta relocation does move it, so a difference
    //    means the mod wrote there, not the loader. At delta zero (the preferred-base
    //    mode) the two copies are equal and this is the ordinary comparison; the rule
    //    stays because it is the convention the fixed-delta mode keeps.
    //  * A substituted published pointer is a stand-in for an arm's own pointer
    //    write, so its four bytes are always compared against the original image.
    void Dump(const Image& image, std::FILE* out, std::string_view prefix,
        const std::vector<size_t>& substituted)
    {
        std::vector<bool> placeholder(image.size, false);
        for (const auto rva : substituted)
            for (size_t index = 0; index < sizeof(uint32_t) && rva + index < image.size; ++index)
                placeholder[rva + index] = true;

        for (size_t rva = 0; rva < image.size; ++rva)
        {
            if (placeholder[rva])
            {
                if (image.bytes[rva] != image.unrelocated[rva])
                    std::fprintf(out, "%s%06zX %02X\n", prefix.data(), rva, image.bytes[rva]);
                continue;
            }

            // The fixup dword that covers this byte, if any. Its high half is the
            // half a 64 KiB-aligned delta moves: at a non-zero delta a high byte that
            // still equals the relocated pristine byte was moved by the loader, not
            // written by the mod, so it is not a row. At delta zero (the preferred-base
            // mode) the two copies are equal and the comparison below is the ordinary
            // one; anything the mod wrote is expressed against the original image,
            // exactly like every other byte.
            bool high = false;
            const auto next = std::upper_bound(image.fixupFields.begin(), image.fixupFields.end(), rva);
            if (next != image.fixupFields.begin())
            {
                const auto field = *(next - 1);
                high = rva < field + sizeof(uint32_t) && rva - field >= 2;
            }
            if (high && image.bytes[rva] == image.pristine[rva])
                continue;

            if (image.bytes[rva] != image.unrelocated[rva])
                std::fprintf(out, "%s%06zX %02X\n", prefix.data(), rva, image.bytes[rva]);
        }
    }

    size_t Changed(const Image& image)
    {
        size_t changed = 0;
        for (size_t rva = 0; rva < image.size; ++rva)
            changed += image.bytes[rva] != image.pristine[rva] ? 1 : 0;
        return changed;
    }
    // A published pointer is a *host* address, so it cannot equal the original's.
    // The baselines record one placeholder for every published module address
    // (0xD0000000), and only the arm knows which words those are: a scan for
    // "four bytes that look like an address in this process" cannot tell a pointer
    // from a displacement plus its neighbouring code, and rewriting the wrong
    // window silently destroys a site (it ate RemoveRGSCLoginRequirement's E9 and
    // then seven other keys' bytes). So the arms register their slots and the dump
    // rewrites exactly those. Returns the RVAs it substituted.
    std::vector<size_t> CanonicalisePublishedPointers(Image& image, uintptr_t hostBase)
    {
        static constexpr uint32_t kBaselinePlaceholder = 0xD0000000u;
        std::vector<size_t> substituted;

        // Two kinds of registered slot. A published *module* address is a host
        // pointer, so the slot holds our own layout and the baseline's placeholder
        // stands in for it. A published *game image* address is a real value in the
        // original too, but the reference dump recorded the same placeholder for it,
        // so the dump substitutes there as well - the arm still writes the true
        // address, and only the tool speaks the baseline's convention.
        for (const auto slot : Bodies::Published())
        {
            if (slot < hostBase || slot + sizeof(uint32_t) > hostBase + image.size)
                continue;

            const auto rva = static_cast<size_t>(slot - hostBase);
            // The registry is process-global, so a slot registered by an earlier
            // cell is still listed here. Only a slot this run actually wrote is a
            // published pointer in this cell: the loop restores the image from the
            // pristine copy before every arm, so an untouched slot still reads its
            // original bytes and must be left alone.
            if (std::memcmp(image.bytes + rva, image.pristine.data() + rva, sizeof(uint32_t)) == 0)
                continue;

            std::memcpy(image.bytes + rva, &kBaselinePlaceholder, sizeof(kBaselinePlaceholder));
            substituted.push_back(rva);
        }

        // A game-image address slot is an RVA in the image the dump is holding, so
        // its slot address is the host base plus that RVA - the same arithmetic as
        // above, and again only when this run actually wrote it.
        for (const auto slot : Bodies::PublishedImage())
        {
            if (slot < hostBase || slot + sizeof(uint32_t) > hostBase + image.size)
                continue;

            const auto rva = static_cast<size_t>(slot - hostBase);
            if (std::memcmp(image.bytes + rva, image.pristine.data() + rva, sizeof(uint32_t)) == 0)
                continue;

            std::memcpy(image.bytes + rva, &kBaselinePlaceholder, sizeof(kBaselinePlaceholder));
            substituted.push_back(rva);
        }
        return substituted;
    }
}

int main(int argc, char** argv)
{
    // A faulting cell is data, not an interruption: a fault here must not raise a
    // Windows "Application Error" dialog on the machine that is running the harness
    // (the 1.0.0.0 cell faults on purpose, and a scan that writes through a stale
    // pointer must fail loudly in the log, not in a message box).
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::filesystem::path executable = "..\\game_refs\\GTAIV.exe";
    std::string build = "9";
    std::filesystem::path output;
    std::filesystem::path ini = "ZolikaPatch.ini";
    bool perOption = false;
    bool oocSelfTest = false;

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (argument == "--exe" && index + 1 < argc)
            executable = argv[++index];
        else if (argument == "--build" && index + 1 < argc)
            build = argv[++index];
        else if (argument == "--per-option")
            perOption = true;
        else if (argument == "--ooc-selftest")
            oocSelfTest = true;
        else if (output.empty())
            output = argv[index];
        else
            ini = argv[index];
    }

    // The payload path only runs when the game calls the wrapper the arm installs,
    // so this is the one way the rebuilt decoder and archive builder execute
    // offline: decode the pinned resource and check both results against the
    // fingerprints the recovered bytes carry (source/ooc.ixx, docs/parity.md).
    if (oocSelfTest)
    {
        const char* failure = OocSelfCheck();
        std::printf("ooc selftest: %s\n", failure == nullptr ? "PASS" : failure);
        return failure == nullptr ? 0 : 1;
    }

    if (output.empty())
    {
        std::printf("usage: zp_dump [--exe <image>] [--build <compact>] [--per-option] <out> [ini]\n"
                    "       zp_dump --ooc-selftest\n");
        return 2;
    }

    Image image;
    if (!Load(executable, image))
        return 1;

    auto& game = Game::Get();
    game.AttachImage(reinterpret_cast<uintptr_t>(image.bytes), image.size, executable, {});
    Loader::Current() = FromId(static_cast<uint32_t>(std::atoi(build.c_str())));
    if (!std::filesystem::exists(ini))
        std::printf("no ini at %s: every option keeps its default\n", ini.string().c_str());
    Config::Ini() = Feather::Load(ini);

    std::FILE* out = std::fopen(output.string().c_str(), "wb");
    if (out == nullptr)
        return 1;

    Shared::Register();
    // The same layer registration the ASI performs: it only resolves a pointer
    // and publishes body addresses, so it changes no byte of the image.
    Native::Register();
    RegisterStability();
    RegisterVehicles();
    RegisterUselessChecks();
    RegisterGraphics();
    RegisterLauncher();
    RegisterLoading();
    RegisterAudio();
    RegisterMisc();
    RegisterMenu();
    RegisterOoc();

    if (perOption)
    {
        for (const auto& option : Catalogue::All)
        {
            // Every key off, this one on -- the arm-isolation oracle's document.
            // Leaving the others absent would read their defaults, which for
            // most keys is "on", and every other arm would write into this cell.
            std::string text = "[Options]\n";
            for (const auto& other : Catalogue::All)
                text += std::string(other.key) + "=0\n";
            text += std::string(option.key) + "=1\n";

            Config::Ini() = Feather::Parse(text);
            std::memcpy(image.bytes, image.pristine.data(), image.size);
            Loader::Run();
            Bodies::Canonicalise(image.bytes, image.pristine.data(), image.size,
                reinterpret_cast<uintptr_t>(image.bytes), image.imageBase);
            const auto substituted =
                CanonicalisePublishedPointers(image, reinterpret_cast<uintptr_t>(image.bytes));
            Dump(image, out, std::string(option.key) + " ", substituted);
        }
    }
    else
    {
        Loader::Run();
        Bodies::Canonicalise(image.bytes, image.pristine.data(), image.size,
            reinterpret_cast<uintptr_t>(image.bytes), image.imageBase);
        const auto substituted =
            CanonicalisePublishedPointers(image, reinterpret_cast<uintptr_t>(image.bytes));
        Dump(image, out, "", substituted);
        std::printf("%s: %zu byte(s) changed, build %s\n", output.string().c_str(), Changed(image), build.c_str());
    }

    std::fclose(out);
    std::printf("%s: %zu byte(s) changed, build %s\n", output.string().c_str(), Changed(image), build.c_str());
    return 0;
}
