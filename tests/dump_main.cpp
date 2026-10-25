// SPDX-License-Identifier: GPL-3.0-or-later
//
// Write-set dump: apply the installers to a mapped copy of a PE32 image and
// print "RVA byte" lines. Used by the parity harness and by contributors who
// want to diff an INI's effect without launching the game.

#include "common.hxx"
#include "patches.hxx"
#include "skip_launcher.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
    constexpr size_t kDosLfanewOffset = 0x3C;
    constexpr size_t kPeSignatureSize = 4;
    constexpr size_t kCoffHeaderSize = 20;
    constexpr size_t kSectionHeaderSize = 40;
    constexpr size_t kMinPeSize = 0x100;
    constexpr uint32_t kMaxImageSize = 0x10000000u;
    constexpr size_t kSparePage = 0x1000;

    bool MapPeImage(const char* path, std::vector<uint8_t>& image, uint32_t& imageBase,
                    uint32_t& sizeOfImage)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        const std::vector<char> raw{std::istreambuf_iterator<char>(in),
                                    std::istreambuf_iterator<char>()};
        std::vector<uint8_t> file(raw.size());
        for (size_t i = 0; i < raw.size(); ++i)
            file[i] = static_cast<uint8_t>(raw[i]);
        if (file.size() < kMinPeSize)
            return false;

        const auto rd16 = [&](size_t off) {
            return static_cast<uint16_t>(file[off] | (file[off + 1] << 8));
        };
        const auto rd32 = [&](size_t off) {
            return static_cast<uint32_t>(file[off] | (file[off + 1] << 8) |
                                              (file[off + 2] << 16) | (file[off + 3] << 24));
        };
        if (file[0] != 'M' || file[1] != 'Z')
            return false;

        const size_t e_lfanew = rd32(kDosLfanewOffset);
        if (e_lfanew + 0x60 > file.size())
            return false;
        const size_t coff = e_lfanew + kPeSignatureSize;
        const size_t optional = coff + kCoffHeaderSize;
        imageBase = rd32(optional + 28);
        sizeOfImage = rd32(optional + 56);
        const uint32_t sizeOfHeaders = rd32(optional + 60);
        if (sizeOfImage == 0 || sizeOfImage > kMaxImageSize)
            return false;

        image.assign(sizeOfImage, 0);
        std::memcpy(image.data(), file.data(),
                    sizeOfHeaders < file.size() ? sizeOfHeaders : file.size());
        const size_t sectionTable = optional + rd16(coff + 16);
        for (size_t sh = sectionTable,
                         end = sectionTable + rd16(coff + 2) * kSectionHeaderSize;
             sh < end; sh += kSectionHeaderSize)
        {
            if (sh + kSectionHeaderSize > file.size())
                return false;
            const uint32_t vsize = rd32(sh + 8);
            const uint32_t vaddr = rd32(sh + 12);
            const uint32_t rsize = rd32(sh + 16);
            const uint32_t rptr = rd32(sh + 20);
            const uint32_t count = rsize < vsize ? rsize : vsize;
            if (count && vaddr + count <= sizeOfImage && rptr + count <= file.size())
                std::memcpy(image.data() + vaddr, file.data() + rptr, count);
        }
        return true;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: opcep_dump <game.exe> <ini> <out.txt>\n");
        return 1;
    }

    std::vector<uint8_t> file_image;
    uint32_t imageBase = 0;
    uint32_t size = 0;
    if (!MapPeImage(argv[1], file_image, imageBase, size))
    {
        std::fprintf(stderr, "opcep_dump: cannot map %s\n", argv[1]);
        return 2;
    }

    auto* image = static_cast<uint8_t*>(
        VirtualAlloc(reinterpret_cast<void*>(static_cast<uintptr_t>(imageBase)),
                     size + kSparePage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    // A 32-bit host cannot always reserve the game's 0x400000 base. The write
    // set is still address-independent: rows use RVA + this mapped base.
    if (!image)
    {
        MEMORY_BASIC_INFORMATION mbi{};
        VirtualQuery(reinterpret_cast<void*>(static_cast<uintptr_t>(imageBase)), &mbi,
                     sizeof(mbi));
        std::fprintf(stderr,
                     "opcep_dump: preferred base 0x%X unavailable (err=%lu state=0x%lX type=0x%lX "
                     "region=%p size=0x%zX)\n",
                     imageBase, GetLastError(), mbi.State, mbi.Type, mbi.BaseAddress,
                     mbi.RegionSize);
        image = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, size + kSparePage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    }
    if (!image)
    {
        std::fprintf(stderr, "opcep_dump: cannot map image (err=%lu)\n", GetLastError());
        return 3;
    }
    std::memcpy(image, file_image.data(), size);

    opp::Config config(argv[2]);
    if (!config.loaded())
    {
        std::fprintf(stderr, "opcep_dump: cannot read %s\n", argv[2]);
        return 4;
    }

    std::vector<uint8_t> pristine(image, image + size);

    // The loader's launcher stage runs before the option pass, exactly as it
    // does in the ASI. Hook destinations are null here; the signature writes are
    // what the write-set comparison needs.
    if (config.Option("SkipLauncher", "1"))
    {
        SkipLauncherReport report;
        (void)ApplySkipLauncher({image, size}, nullptr, nullptr, report,
                                SkipLauncherStage::all);
    }

    opp::PatchGame(opp::GameBuild::v1080, opp::GameImage{image, size}, config);

    std::FILE* out = std::fopen(argv[3], "wb");
    if (!out)
        return 5;
    size_t changed = 0;
    for (size_t i = 0; i < size; ++i)
    {
        if (image[i] == pristine[i])
            continue;
        std::fprintf(out, "%08zX %02X\n", i, image[i]);
        ++changed;
    }
    std::fclose(out);
    std::fprintf(stderr, "opcep_dump: %zu bytes changed\n", changed);
    return 0;
}
