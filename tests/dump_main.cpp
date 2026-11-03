// SPDX-License-Identifier: GPL-3.0-or-later
//
// Write-set dump: apply the installers to a mapped copy of a PE32 image and
// print "RVA byte" lines. Used by the parity harness and by contributors who
// want to diff an INI's effect without launching the game.

#include "common.hxx"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

import opp.common;
import opp.config;
import opp.game;

namespace opp
{
    namespace
    {
        std::vector<uint32_t>& DumpPointers()
        {
            static std::vector<uint32_t> pointers;
            return pointers;
        }
    } // namespace
} // namespace opp

// Recorded by AddressOf / FindGlobalValue / FindGlobalSymbol under
// OPCEP_DUMP_TOOL. The dump canonicalises these operands after the pass:
// their numeric value depends on this tool's module layout, not behaviour.
// C linkage so the modules (opp.common declares it) and this plain TU agree.
extern "C" void opp_record_dump_pointer(uint32_t value)
{
    opp::DumpPointers().push_back(value);
}

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

    // Per-arm attribution. `before` always mirrors the live image, so each arm
    // records only the bytes that arm left changed (the net write set of that
    // arm, not every transient store). Records are kept in memory so the
    // module-address canonicalisation below can rewrite both outputs.
    struct WriteRecord
    {
        size_t rva;
        uint8_t oldValue;
        uint8_t newValue;
        const char* arm;
    };

    struct Attribution
    {
        uint8_t* image{};
        size_t size{};
        std::vector<uint8_t> before;
        std::vector<WriteRecord> records;
    };

    void AttributionEnd(void* user, const char* arm)
    {
        auto* state = static_cast<Attribution*>(user);
        for (size_t i = 0; i < state->size; ++i)
        {
            const uint8_t now = state->image[i];
            const uint8_t old = state->before[i];
            if (now == old)
                continue;
            state->records.push_back({i, old, now, arm});
            state->before[i] = now;
        }
    }

    // Module-relative operands (AddressOf(&global), FindGlobalValue, ...) move
    // whenever unrelated code or data is added. Replace every 4-byte window that
    // holds one of the recorded addresses with a stable synthetic address, so a
    // pure refactor keeps the write set byte-identical. Game-image addresses are
    // left alone: the installers write those deliberately and they are stable.
    void CanonicaliseModulePointers(Attribution& state, const std::vector<uint8_t>& pristine,
                                    uintptr_t gameBase, size_t gameSize)
    {
        std::unordered_set<uint32_t> modulePointers;
        for (const uint32_t value : opp::DumpPointers())
        {
            if (value >= gameBase && value < gameBase + gameSize)
                continue;
            modulePointers.insert(value);
        }

        if (!state.records.empty() && !modulePointers.empty())
        {
            std::unordered_map<size_t, size_t> byRva;
            for (size_t i = 0; i < state.records.size(); ++i)
                byRva.emplace(state.records[i].rva, i);

            const size_t originalCount = state.records.size();
            for (size_t i = 0; i < originalCount; ++i)
            {
                const size_t rva = state.records[i].rva;
                const char* arm = state.records[i].arm;
                for (size_t back = 0; back <= 3 && back <= rva; ++back)
                {
                    const size_t start = rva - back;
                    if (start + 4 > state.size)
                        continue;
                    uint32_t value = 0;
                    std::memcpy(&value, state.image + start, sizeof(value));
                    if (modulePointers.find(value) == modulePointers.end())
                        continue;

                    // Site-derived, so the marker does not depend on how many
                    // pointers the pass recorded or in which order.
                    const uint32_t replacement =
                        0x0A000000u | (static_cast<uint32_t>(start) & 0x00FFFFFFu);
                    const uint8_t bytes[4] = {
                        static_cast<uint8_t>(replacement),
                        static_cast<uint8_t>(replacement >> 8),
                        static_cast<uint8_t>(replacement >> 16),
                        static_cast<uint8_t>(replacement >> 24),
                    };
                    for (size_t k = 0; k < 4; ++k)
                    {
                        const size_t target = start + k;
                        const auto existing = byRva.find(target);
                        if (existing == byRva.end())
                        {
                            state.records.push_back(
                                {target, pristine[target], bytes[k], arm});
                            byRva.emplace(target, state.records.size() - 1);
                        }
                        else
                        {
                            state.records[existing->second].newValue = bytes[k];
                        }
                        state.image[target] = bytes[k];
                    }
                }
            }
        }

        // Several arms can touch the same site (e.g. RemoveUselessChecks then
        // RestoreDeathMusic, or two options that patch the same flag). The final
        // image has one byte per RVA, so keep the last write for each and emit a
        // unique, order-independent set.
        std::unordered_map<size_t, WriteRecord> latest;
        latest.reserve(state.records.size());
        for (const WriteRecord& record : state.records)
            latest[record.rva] = record;
        state.records.clear();
        state.records.reserve(latest.size());
        for (const auto& entry : latest)
            state.records.push_back(entry.second);

        std::sort(state.records.begin(), state.records.end(),
                  [](const WriteRecord& left, const WriteRecord& right) {
                      return left.rva < right.rva;
                  });
    }

    // Exercise the real first-run importer without touching the source fixture:
    // copy it into a throwaway directory next to `outPath` and keep the result.
    int ImportLegacy(const char* legacyPath, const char* outPath)
    {
        namespace fs = std::filesystem;
        const fs::path out = fs::absolute(outPath);
        const fs::path work = out.parent_path() / (out.filename().string() + ".import");
        std::error_code error;
        fs::remove_all(work, error);
        fs::create_directories(work, error);
        fs::copy_file(legacyPath, work / "ZolikaPatch.ini",
                      fs::copy_options::overwrite_existing, error);
        if (error)
        {
            std::fprintf(stderr, "opcep_dump: cannot stage %s: %s\n", legacyPath,
                         error.message().c_str());
            return 6;
        }

        const fs::path created = opp::EnsureConfigFile(work);
        if (created.empty())
        {
            std::fprintf(stderr, "opcep_dump: import of %s failed\n", legacyPath);
            fs::remove_all(work, error);
            return 6;
        }
        fs::copy_file(created, out, fs::copy_options::overwrite_existing, error);
        fs::remove_all(work, error);
        if (error)
        {
            std::fprintf(stderr, "opcep_dump: cannot write %s: %s\n", outPath,
                         error.message().c_str());
            return 6;
        }
        std::fprintf(stderr, "opcep_dump: imported %s -> %s\n", legacyPath, outPath);
        return 0;
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc == 4 && std::strcmp(argv[1], "--import-legacy") == 0)
        return ImportLegacy(argv[2], argv[3]);

    // Print the effective install order, one name per line, tier by tier, in the
    // same name order PatchGame runs them. The order golden in
    // tests/order_golden.txt is the per-tier arm set captured with this mode.
    if (argc == 2 && std::strcmp(argv[1], "--order") == 0)
    {
        for (const opp::Event<opp::PatchContext&>* event : {&opp::PublishEvent(),
                                                           &opp::InstallEvent(),
                                                           &opp::PostEvent()})
        {
            std::vector<const char*> names;
            for (size_t i = 0; i < event->size(); ++i)
            {
                if (const char* name = event->name(i))
                    names.push_back(name);
            }
            std::sort(names.begin(), names.end(), [](const char* a, const char* b) {
                return std::strcmp(a, b) < 0;
            });
            for (const char* name : names)
                std::printf("%s\n", name);
        }
        return 0;
    }

    if (argc < 3)
    {
        std::fprintf(stderr,
                     "usage: opcep_dump <game.exe> <ini> <out.txt> "
                     "[--attribution <arm.txt>]\n"
                     "       opcep_dump --import-legacy <legacy.ini> <out.ini>\n"
                     "       opcep_dump --order\n");
        return 1;
    }

    const char* attributionPath = nullptr;
    uint32_t buildOverride = 0;
    for (int i = 4; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--attribution") == 0 && i + 1 < argc)
            attributionPath = argv[++i];
        else if (std::strcmp(argv[i], "--build") == 0 && i + 1 < argc)
            buildOverride = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
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
    if (!config.present())
    {
        std::fprintf(stderr, "opcep_dump: cannot read %s\n", argv[2]);
        return 4;
    }

    std::vector<uint8_t> pristine(image, image + size);

    Attribution attribution;
    attribution.image = image;
    attribution.size = size;
    attribution.before = pristine;
    opp::PatchObserver observer;
    observer.user = &attribution;
    observer.end = &AttributionEnd;

    // SkipLauncher belongs to the separate XLivelessAddon successor module; the
    // OpenPreCEPatch write set starts at the option pass.

    // The real ASI classifies the build from the running executable. The dump
    // maps an image without executing it, so a cross-build write set needs the
    // build named; --build 1060 etc. selects the same arm set the ASI would run.
    opp::GameBuild build = opp::GameBuild::v1080;
    if (buildOverride != 0)
    {
        build = opp::ClassifyGameBuild(buildOverride);
        if (build == opp::GameBuild::unknown)
        {
            std::fprintf(stderr, "opcep_dump: unsupported --build %u\n", buildOverride);
            return 7;
        }
        std::fprintf(stderr, "opcep_dump: build forced to %s\n", opp::GameBuildName(build));
    }

    opp::PatchGame(build, opp::GameImage{image, size}, config, observer);

    CanonicaliseModulePointers(attribution, pristine,
                               reinterpret_cast<uintptr_t>(image), size);

    std::FILE* out = std::fopen(argv[3], "wb");
    if (!out)
        return 5;
    for (const WriteRecord& record : attribution.records)
        std::fprintf(out, "%08zX %02X\n", record.rva, record.newValue);
    std::fclose(out);

    if (attributionPath != nullptr)
    {
        std::FILE* arms = std::fopen(attributionPath, "wb");
        if (arms == nullptr)
            return 5;
        for (const WriteRecord& record : attribution.records)
            std::fprintf(arms, "%08zX %02X %02X %s\n", record.rva, record.oldValue,
                         record.newValue, record.arm);
        std::fclose(arms);
        std::fprintf(stderr, "opcep_dump: %zu attributed bytes\n",
                     attribution.records.size());
    }

    std::fprintf(stderr, "opcep_dump: %zu bytes changed\n", attribution.records.size());
    return 0;
}
