#include <common.hxx>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <new>

import loading;

// The save converter's behavioural test: what `sub_10051B30` (0x10051B30, 337
// instructions, rebuilt as `NewSavesConvert`) decides about a save file, on the real
// saves in `game_refs/saves/` and on synthetic layouts.
//
// The converter is the only piece in the tree that can destroy user data, so its walk
// is pinned here by behaviour, not by shape. `Loading::WalkSaveFile` runs the same walk
// the production path runs (`NewSavesWalkFile`) and the same patch choice
// (`NewSavesChoosePatch`), and patches nothing; the values this program asserts are
// recomputed from the file by this file's own parser (an `std::ifstream` walk written
// from the recovered format, not from `source/loading.ixx`), so the test cannot agree
// with a bug by construction.
//
//     zp_save_walk <saves directory>          discover, then convert every save under it
//     zp_save_walk --child <file> [<report>]  one walk in a fresh process (internal)
//
// What the test cannot express offline: the three site patches and the two hooks only
// run when the game loads a savegame, so nothing here writes the image; and a file
// shorter than its header leaves the converter's magic test reading uninitialised
// stack bytes, so its outcome is a property of the process and is observed in a child
// (see `CheckFaults`).

namespace
{
    // ---------------------------------------------------------------------------
    // The recovered format and the recovered decision values, read off the original's
    // own instructions rather than out of `source/loading.ixx`.
    // ---------------------------------------------------------------------------
    constexpr std::size_t kHeaderSize = 0x110;       // fread(header, 0x110, 1) at 0x10051BAD
    constexpr std::size_t kMagicOffset = 0x0C;       // cmp dword [eax],45564153h at 0x10051BD1
    constexpr std::size_t kChunkCount = 18;          // cmp esi,12h / jl at 0x10051C8A
    constexpr std::size_t kChunkHeaderSize = 9;      // fread(&chunk, 9, 1) at 0x10051BFE
    constexpr std::size_t kChunkTagSize = 5;         // the header's first five bytes
    constexpr std::size_t kChunkSizeOffset = 5;      // mov eax,[ebp-13Bh] at 0x10051C03

    // The three sizes that identify the newer layout, and the branch opcode, read
    // size and branch word they select (0x10051C42, 0x10051C5A, 0x10051C72 and the
    // three `cmovne` pairs at 0x10051D35, 0x10051E3B, 0x10051E6E).
    constexpr uint32_t kChunk9Size = 0x4C45;
    constexpr uint32_t kChunk13Size = 0x1741;
    constexpr uint32_t kChunk16Size = 0x0E77;
    constexpr uint8_t kBranchOpcodeNew = 0xEB;
    constexpr uint8_t kBranchOpcodeOld = 0x74;
    constexpr uint32_t kReadSizeNew = 0x0E6E;
    constexpr uint32_t kReadSizeOld = 0x0CBE;
    constexpr uint16_t kReadBranchNew = 0x9090;
    constexpr uint16_t kReadBranchOld = 0x2675;

    int checks = 0;
    int failures = 0;

    void Check(bool condition, const std::string& what)
    {
        ++checks;
        if (condition)
        {
            std::printf("PASS %s\n", what.c_str());
            return;
        }
        ++failures;
        std::printf("FAIL %s\n", what.c_str());
    }

    uint32_t LittleEndianDword(const char* at)
    {
        return static_cast<uint32_t>(static_cast<unsigned char>(at[0])) |
               (static_cast<uint32_t>(static_cast<unsigned char>(at[1])) << 8) |
               (static_cast<uint32_t>(static_cast<unsigned char>(at[2])) << 16) |
               (static_cast<uint32_t>(static_cast<unsigned char>(at[3])) << 24);
    }

    // ---------------------------------------------------------------------------
    // The independent parser: the format as this test reads it, from the file.
    // ---------------------------------------------------------------------------
    struct ParsedChunk
    {
        std::string id;     // the 9-byte header's first five bytes
        uint32_t size = 0;  // the dword at +5
        std::size_t offset = 0;
    };

    struct ParsedSave
    {
        bool header = false;  // 0x110 bytes were read
        bool magic = false;   // "SAVE" at +0xC, from the file's own bytes
        std::vector<ParsedChunk> chunks;
    };

    ParsedSave Parse(const std::filesystem::path& path)
    {
        ParsedSave parsed;
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return parsed;

        std::array<char, kHeaderSize> header{};
        stream.read(header.data(), static_cast<std::streamsize>(header.size()));
        const auto available = static_cast<std::size_t>(stream.gcount());
        parsed.header = available == kHeaderSize;
        parsed.magic = available >= kMagicOffset + 4 &&
                       std::memcmp(header.data() + kMagicOffset, "SAVE", 4) == 0;

        const auto fileSize = static_cast<std::uint64_t>(std::filesystem::file_size(path));
        std::uint64_t offset = kHeaderSize;
        for (std::size_t index = 0; index < kChunkCount; ++index)
        {
            if (offset + kChunkHeaderSize > fileSize)
                break;

            std::array<char, kChunkHeaderSize> raw{};
            stream.clear();
            stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            stream.read(raw.data(), static_cast<std::streamsize>(raw.size()));
            if (static_cast<std::size_t>(stream.gcount()) != kChunkHeaderSize)
                break;

            ParsedChunk chunk;
            chunk.id.assign(raw.data(), kChunkTagSize);
            chunk.size = LittleEndianDword(raw.data() + kChunkSizeOffset);
            chunk.offset = static_cast<std::size_t>(offset);
            parsed.chunks.push_back(chunk);
            offset += chunk.size;
        }

        return parsed;
    }

    bool LooksLikeSave(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            return false;
        std::array<char, kMagicOffset + 4> header{};
        stream.read(header.data(), static_cast<std::streamsize>(header.size()));
        return static_cast<std::size_t>(stream.gcount()) == header.size() &&
               std::memcmp(header.data() + kMagicOffset, "SAVE", 4) == 0;
    }

    // A save is a file whose own bytes say so; that is how the two accounts under
    // `game_refs/saves` are found, and it keeps the discovery independent of names.
    std::vector<std::filesystem::path> FindSaves(const std::filesystem::path& root)
    {
        std::vector<std::filesystem::path> saves;
        std::error_code error;
        for (std::filesystem::recursive_directory_iterator it(root, error), last;
             !error && it != last; it.increment(error))
        {
            std::error_code entryError;
            if (!it->is_regular_file(entryError) || entryError)
                continue;
            if (LooksLikeSave(it->path()))
                saves.push_back(it->path());
        }
        std::sort(saves.begin(), saves.end());
        return saves;
    }

    // ---------------------------------------------------------------------------
    // Synthetic saves: the same image shape, written by this test.
    // ---------------------------------------------------------------------------
    bool WriteSave(const std::filesystem::path& path, const std::vector<uint32_t>& sizes,
        const std::string& chunkTag = "BLOCK")
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
            return false;

        std::array<char, kHeaderSize> header{};
        std::memcpy(header.data() + kMagicOffset, "SAVE", 4);
        stream.write(header.data(), static_cast<std::streamsize>(header.size()));

        std::array<char, 0x10000> zeros{};
        for (const uint32_t size : sizes)
        {
            std::array<char, kChunkHeaderSize> raw{};
            std::memcpy(raw.data(), chunkTag.data(), std::min(chunkTag.size(), static_cast<std::size_t>(5)));
            for (std::size_t byte = 0; byte < 4; ++byte)
                raw[kChunkSizeOffset + byte] = static_cast<char>((size >> (byte * 8)) & 0xFF);
            stream.write(raw.data(), static_cast<std::streamsize>(raw.size()));

            // A size below nine has no payload: the converter underflows the operand
            // before it reads anything, which is the fault this shape is for.
            std::uint32_t payload = size > kChunkHeaderSize ? size - kChunkHeaderSize : 0;
            while (payload > 0)
            {
                const auto block = std::min<std::uint32_t>(payload, zeros.size());
                stream.write(zeros.data(), static_cast<std::streamsize>(block));
                payload -= block;
            }
        }
        return stream.good();
    }

    bool WriteRaw(const std::filesystem::path& path, const std::vector<uint8_t>& bytes)
    {
        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream)
            return false;
        stream.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        return stream.good();
    }

    std::vector<uint32_t> Sizes(std::size_t count, uint32_t size)
    {
        return std::vector<uint32_t>(count, size);
    }

    // ---------------------------------------------------------------------------
    // One file, asserted twice: the walk and this test's own parse.
    // ---------------------------------------------------------------------------
    // Which values a file must select. A real save decides for itself (it may be a
    // newer layout, and then the newer triple must apply); a synthetic image names the
    // layout it was written for, so the three literals are asserted and the file's own
    // sizes must prove it is that layout.
    enum class Layout
    {
        fromFile,
        older,
        newer,
    };

    void CheckFile(const std::filesystem::path& path, const std::string& label,
        Layout layout = Layout::fromFile)
    {
        const ParsedSave parsed = Parse(path);
        Check(parsed.header,
            std::format("{}: 0x110-byte header present (test parser: ifstream, {} byte(s))",
                label, std::filesystem::file_size(path)));
        Check(parsed.magic,
            std::format("{}: \"SAVE\" at +0x{:X} (test parser: file bytes 0x0C..0x0F)", label, kMagicOffset));
        Check(parsed.chunks.size() == kChunkCount,
            std::format("{}: {} chunk header(s) at +0x{:X} (test parser)", label, parsed.chunks.size(),
                kHeaderSize));

        const Loading::SaveWalkDecision decision =
            Loading::WalkSaveFile(path.string().c_str());
        Check(decision.end == Loading::SaveWalkEnd::walked,
            std::format("{}: walk answered \"walked\" (converter, {} byte(s))", label,
                std::filesystem::file_size(path)));
        Check(decision.chunkCount == parsed.chunks.size(),
            std::format("{}: walk read {} chunk header(s), test parser found {}", label,
                decision.chunkCount, parsed.chunks.size()));

        const std::size_t common = std::min<std::size_t>(decision.chunkCount, parsed.chunks.size());
        for (std::size_t index = 0; index < common; ++index)
        {
            const std::string walkedId(decision.chunks[index].id, kChunkTagSize);
            Check(walkedId == parsed.chunks[index].id,
                std::format("{}: chunk {} tag \"{}\" (walk) = test parser at file+0x{:X}", label, index,
                    walkedId, parsed.chunks[index].offset));
            Check(decision.chunks[index].size == parsed.chunks[index].size,
                std::format("{}: chunk {} size 0x{:X} (walk) = test parser at file+0x{:X}", label, index,
                    decision.chunks[index].size, parsed.chunks[index].offset));
        }

        if (parsed.chunks.size() < kChunkCount)
            return;

        // The three flag tests, each decided from the size this test read itself.
        const uint8_t expectedForceRead = parsed.chunks[9].size == kChunk9Size ? 1 : 0;
        const uint8_t expectedExtraBlock = parsed.chunks[13].size == kChunk13Size ? 1 : 0;
        const uint8_t expectedNewChunk16 = parsed.chunks[16].size == kChunk16Size ? 1 : 0;
        Check(decision.forceRead == expectedForceRead,
            std::format("{}: forceRead={} (byte_1015AD98; chunk 9 size 0x{:X} vs 0x{:X})", label,
                decision.forceRead, parsed.chunks[9].size, kChunk9Size));
        Check(decision.extraBlock == expectedExtraBlock,
            std::format("{}: extraBlock={} (byte_1015AD99; chunk 13 size 0x{:X} vs 0x{:X})", label,
                decision.extraBlock, parsed.chunks[13].size, kChunk13Size));
        Check(decision.newChunk16 == expectedNewChunk16,
            std::format("{}: newChunk16={} (byte_1015AD9A; chunk 16 size 0x{:X} vs 0x{:X})", label,
                decision.newChunk16, parsed.chunks[16].size, kChunk16Size));

        // Which layout the file is, when the caller wrote it for one: the two sizes that
        // decide the three values, and the flag that does not.
        if (layout != Layout::fromFile)
        {
            const bool newer = layout == Layout::newer;
            Check(expectedForceRead == static_cast<uint8_t>(newer ? 1 : 0) &&
                      expectedNewChunk16 == static_cast<uint8_t>(newer ? 1 : 0),
                std::format("{}: the file's chunks 9 and 16 carry the {} sizes (0x{:X} / 0x{:X})", label,
                    newer ? "newer" : "older", parsed.chunks[9].size, parsed.chunks[16].size));
        }

        // The three patch values those flags choose, from the recovered literals: the
        // file's own layout for a real save, the written layout for a synthetic one.
        const bool branchIsNew = layout == Layout::newer ? true
                               : layout == Layout::older ? false
                                                         : expectedForceRead != 0;
        const bool readIsNew = layout == Layout::newer ? true
                             : layout == Layout::older ? false
                                                       : expectedNewChunk16 != 0;
        const uint8_t expectedBranch = branchIsNew ? kBranchOpcodeNew : kBranchOpcodeOld;
        const uint32_t expectedReadSize = readIsNew ? kReadSizeNew : kReadSizeOld;
        const uint16_t expectedReadBranch = readIsNew ? kReadBranchNew : kReadBranchOld;
        Check(decision.branchOpcode == expectedBranch,
            std::format("{}: branch opcode 0x{:02X} at site 872185h (chunk 9 size 0x{:X}; {} layout -> 0x{:02X})",
                label, decision.branchOpcode, parsed.chunks[9].size,
                expectedForceRead != 0 ? "newer" : "older", expectedBranch));
        Check(decision.readSize == expectedReadSize,
            std::format("{}: pushed read size 0x{:X} at site 7D8C50h (chunk 16 size 0x{:X}; {} layout -> 0x{:X})",
                label, decision.readSize, parsed.chunks[16].size,
                expectedNewChunk16 != 0 ? "newer" : "older", expectedReadSize));
        Check(decision.readBranch == expectedReadBranch,
            std::format("{}: branch 0x{:04X} at site 7D8C50h (chunk 16 size 0x{:X}; {} layout -> 0x{:04X})",
                label, decision.readBranch, parsed.chunks[16].size,
                expectedNewChunk16 != 0 ? "newer" : "older", expectedReadBranch));
    }

    // ---------------------------------------------------------------------------
    // The faults: what a call did, so the original's throw can be asserted by type.
    // ---------------------------------------------------------------------------
    std::string EndName(Loading::SaveWalkEnd end)
    {
        switch (end)
        {
        case Loading::SaveWalkEnd::walked:
            return "walked";
        case Loading::SaveWalkEnd::notASave:
            return "notASave";
        case Loading::SaveWalkEnd::noFile:
            return "noFile";
        }
        return "unknown";
    }

    std::string OutcomeOf(const std::filesystem::path& path)
    {
        try
        {
            return EndName(Loading::WalkSaveFile(path.string().c_str()).end);
        }
        catch (const std::bad_alloc&)
        {
            return "bad_alloc";
        }
        catch (const std::exception& error)
        {
            return std::format("exception:{}", error.what());
        }
        catch (...)
        {
            return "other";
        }
    }

    std::filesystem::path SelfPath()
    {
        std::wstring buffer(MAX_PATH, L'\0');
        for (;;)
        {
            const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (length == 0)
                return {};
            if (length < buffer.size())
                return std::filesystem::path(std::wstring(buffer.data(), length));
            buffer.resize(buffer.size() * 2);
        }
    }

    // One walk in a fresh process. The child prints its answer and also writes it to the
    // file the parent names, so the parent never has to go through a shell.
    int RunChild(const std::filesystem::path& path, const std::filesystem::path& report)
    {
        std::string end;
        try
        {
            end = EndName(Loading::WalkSaveFile(path.string().c_str()).end);
        }
        catch (const std::bad_alloc&)
        {
            end = "bad_alloc";
        }
        catch (const std::exception& error)
        {
            end = std::format("exception:{}", error.what());
        }
        catch (...)
        {
            end = "other";
        }

        std::printf("END=%s\n", end.c_str());
        if (!report.empty())
        {
            std::ofstream stream(report, std::ios::binary | std::ios::trunc);
            stream << "END=" << end << "\n";
        }
        return 0;
    }

    bool RunChildProcess(const std::filesystem::path& self, const std::filesystem::path& input,
        const std::filesystem::path& report, std::string& outcome, std::string& why)
    {
        std::error_code error;
        std::filesystem::remove(report, error);

        std::wstring command = L"\"" + self.wstring() + L"\" --child \"" + input.wstring() +
                               L"\" \"" + report.wstring() + L"\"";
        std::vector<wchar_t> line(command.begin(), command.end());
        line.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                &startup, &process) == FALSE)
        {
            why = std::format("CreateProcess failed for \"{}\" --child \"{}\" (error {})",
                self.string(), input.string(), GetLastError());
            return false;
        }

        WaitForSingleObject(process.hProcess, 30000);
        DWORD status = 0;
        GetExitCodeProcess(process.hProcess, &status);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (status != 0)
        {
            why = std::format("child exited {}", status);
            return false;
        }

        std::ifstream stream(report, std::ios::binary);
        std::string lineRead;
        if (!stream || !std::getline(stream, lineRead))
        {
            why = std::format("child wrote no report to {}", report.string());
            return false;
        }
        while (!lineRead.empty() && (lineRead.back() == '\r' || lineRead.back() == ' '))
            lineRead.pop_back();
        const auto at = lineRead.find("END=");
        if (at == std::string::npos)
        {
            why = std::format("child's report \"{}\" has no END= line", lineRead);
            return false;
        }
        outcome = lineRead.substr(at + 4);
        return true;
    }

    // ---------------------------------------------------------------------------
    // The fault cases.
    // ---------------------------------------------------------------------------
    void CheckFaults(const std::filesystem::path& inputs)
    {
        // (1) A chunk size below nine. The original's `add eax,0FFFFFFF7h`
        // (0x10051C14) feeds `operator new` (0x100603F1) unchecked, so the payload
        // underflows and the allocation throws. Deterministic, so it is asserted
        // in this process, with the original's own exception type.
        const auto sizeZero = inputs / "size_zero.bin";
        const auto sizeEight = inputs / "size_eight.bin";
        const std::string zeroOutcome = OutcomeOf(sizeZero);
        Check(zeroOutcome == "bad_alloc",
            std::format("{}: chunk 0's size field 0 (< 9) answered {} - the original's unchecked"
                        " add eax,0FFFFFFF7h (0x10051C14) -> operator new throws std::bad_alloc",
                sizeZero.filename().string(), zeroOutcome));
        const std::string eightOutcome = OutcomeOf(sizeEight);
        Check(eightOutcome == "bad_alloc",
            std::format("{}: chunk 9's size field 8 (< 9) answered {} after nine good chunks - same"
                        " fault, at chunk 9's own allocation",
                sizeEight.filename().string(), eightOutcome));

        // (2) A truncated header. The 0x110-byte read at 0x10051BB3 is unchecked, so
        // the bytes past the file are whatever the stack held. Two cases are decided
        // by the file itself, because the magic offset lies inside it.
        const auto truncNoMagic = inputs / "trunc_without_magic.bin";
        const std::string noMagicOutcome = OutcomeOf(truncNoMagic);
        Check(noMagicOutcome == "notASave",
            std::format("{}: 0x20-byte file without \"SAVE\" at +0xC answered {} - rejected on the"
                        " magic at 0x10051BD1 (the bytes it tests are in the file)",
                truncNoMagic.filename().string(), noMagicOutcome));

        const auto truncMagic = inputs / "trunc_with_magic.bin";
        const std::string magicOutcome = OutcomeOf(truncMagic);
        Check(magicOutcome == "walked" || magicOutcome == "bad_alloc",
            std::format("{}: 0x20-byte file with \"SAVE\" at +0xC answered {} - the header read is"
                        " unchecked, so a short file is not rejected on its header; it either"
                        " walks on stale bytes or throws from their underflow",
                truncMagic.filename().string(), magicOutcome));

        const auto notASave = inputs / "not_a_save.bin";
        const std::string plainOutcome = OutcomeOf(notASave);
        Check(plainOutcome == "notASave",
            std::format("{}: 0x110 zero bytes answered {} - the magic test is the only gate",
                notASave.filename().string(), plainOutcome));
        const std::string missingOutcome = OutcomeOf(inputs / "does_not_exist.bin");
        Check(missingOutcome == "noFile",
            std::format("does_not_exist.bin: a missing path answered {} - fopen_s's own answer",
                missingOutcome));

        // (3) A file shorter than the magic offset: the magic itself is uninitialised,
        // so which of the original's three paths runs is a property of the process,
        // not of the file. That is the case a subprocess carries: the answer has to
        // come from a fresh process, and the two files differ only in bytes the walk
        // never reads, so the two answers must agree.
        const auto shortA = inputs / "short_a.bin";
        const auto shortB = inputs / "short_b.bin";
        const auto reportA = inputs / "short_a.report";
        const auto reportB = inputs / "short_b.report";
        const auto self = SelfPath();
        std::string whyA;
        std::string whyB;
        std::string outcomeA;
        std::string outcomeB;
        const bool ranA = RunChildProcess(self, shortA, reportA, outcomeA, whyA);
        const bool ranB = RunChildProcess(self, shortB, reportB, outcomeB, whyB);
        std::string childDetail;
        if (!ranA)
            childDetail += whyA;
        if (!ranB)
            childDetail += (childDetail.empty() ? "" : "; ") + whyB;
        Check(!self.empty() && ranA && ranB,
            std::format("child processes ran on short_a/short_b (self \"{}\"{})", self.string(),
                childDetail.empty() ? "" : "; " + childDetail));
        Check(!outcomeA.empty() && outcomeA == outcomeB,
            std::format("short_a.bin and short_b.bin (4-byte files, the same length, no magic in"
                        " either) answered {} and {} in separate processes - equal, because the"
                        " magic comes from the child's stack",
                ranA ? outcomeA : whyA, ranB ? outcomeB : whyB));
        Check(outcomeA == "walked" || outcomeA == "notASave" || outcomeA == "bad_alloc",
            std::format("short_a.bin: {} is one of the original's three paths (magic rejected,"
                        " walked on stale bytes, or std::bad_alloc from the underflow)",
                ranA ? outcomeA : whyA));
    }

    int Run(const std::filesystem::path& savesRoot)
    {
        const auto saves = FindSaves(savesRoot);
        Check(!saves.empty(),
            std::format("saves: {} file(s) under {} whose own bytes carry \"SAVE\" at +0xC", saves.size(),
                savesRoot.string()));

        // The real saves. Every save the root holds is converted, and each one's
        // expectation comes from its own chunk sizes - so a newer-layout save, if one is
        // ever dropped in, is converted against the newer triple rather than failing.
        for (const auto& save : saves)
            CheckFile(save, std::filesystem::relative(save, savesRoot).string());

        const auto inputs = std::filesystem::current_path() / "save_walk_inputs";
        std::error_code error;
        std::filesystem::create_directories(inputs, error);
        if (error)
        {
            Check(false, std::format("could not create {}: {}", inputs.string(), error.message()));
            return 1;
        }

        // A synthetic newer layout: chunk 9, 13 and 16 carry the newer sizes, so the
        // converter would write EB / 0E6E / 90 90.
        {
            auto sizes = Sizes(kChunkCount, kChunkHeaderSize);
            sizes[9] = kChunk9Size;
            sizes[13] = kChunk13Size;
            sizes[16] = kChunk16Size;
            const auto path = inputs / "new_layout.bin";
            Check(WriteSave(path, sizes),
                std::format("{}: synthetic newer layout written (chunk 9 = 0x{:X}, 13 = 0x{:X}, 16 = 0x{:X})",
                    path.filename().string(), kChunk9Size, kChunk13Size, kChunk16Size));
            CheckFile(path, path.filename().string(), Layout::newer);
        }

        // Chunk 13's flag alone must not move any of the three patch values: it only
        // decides whether the second-load hook consumes its twelve bytes.
        {
            auto sizes = Sizes(kChunkCount, kChunkHeaderSize);
            sizes[13] = kChunk13Size;
            const auto path = inputs / "chunk13_only.bin";
            Check(WriteSave(path, sizes),
                std::format("{}: synthetic save with only chunk 13 newer (0x{:X})",
                    path.filename().string(), kChunk13Size));
            CheckFile(path, path.filename().string(), Layout::older);
        }

        // The older layout, built from the real save's own ids and sizes: the walk must
        // answer the old values even though the file is synthetic.
        if (!saves.empty())
        {
            const ParsedSave real = Parse(saves.front());
            std::vector<uint32_t> sizes;
            sizes.reserve(real.chunks.size());
            for (const auto& chunk : real.chunks)
                sizes.push_back(chunk.size);
            const auto path = inputs / "old_layout.bin";
            Check(WriteSave(path, sizes),
                std::format("{}: synthetic older layout written from {}'s own ids and sizes",
                    path.filename().string(), saves.front().filename().string()));
            CheckFile(path, path.filename().string(), Layout::older);
        }

        // The faults. The shapes are written here, then exercised by CheckFaults.
        {
            const auto path = inputs / "size_zero.bin";
            Check(WriteSave(path, { 0 }),
                std::format("{}: header + one chunk header whose size field is 0",
                    path.filename().string()));
        }
        {
            auto sizes = Sizes(9, kChunkHeaderSize);
            sizes.push_back(8);
            const auto path = inputs / "size_eight.bin";
            Check(WriteSave(path, sizes),
                std::format("{}: header + nine good chunks + a chunk whose size field is 8",
                    path.filename().string()));
        }
        {
            std::vector<uint8_t> header(kHeaderSize, 0);
            std::memcpy(header.data() + kMagicOffset, "SAVE", 4);
            header.resize(0x20);
            const auto path = inputs / "trunc_with_magic.bin";
            Check(WriteRaw(path, header),
                std::format("{}: 0x20 bytes carrying \"SAVE\" at +0xC (header truncated)",
                    path.filename().string()));
        }
        {
            std::vector<uint8_t> bytes(0x20, 0x39);
            const auto path = inputs / "trunc_without_magic.bin";
            Check(WriteRaw(path, bytes),
                std::format("{}: 0x20 bytes not carrying \"SAVE\" at +0xC (header truncated)",
                    path.filename().string()));
        }
        {
            std::vector<uint8_t> bytes(kHeaderSize, 0);
            const auto path = inputs / "not_a_save.bin";
            Check(WriteRaw(path, bytes),
                std::format("{}: a full 0x110-byte header with no magic", path.filename().string()));
        }
        {
            std::vector<uint8_t> bytes(4, 0x41);
            const auto path = inputs / "short_a.bin";
            Check(WriteRaw(path, bytes),
                std::format("{}: 4 bytes, shorter than the magic offset", path.filename().string()));
        }
        {
            std::vector<uint8_t> bytes(4, 0x42);
            const auto path = inputs / "short_b.bin";
            Check(WriteRaw(path, bytes),
                std::format("{}: 4 bytes, shorter than the magic offset", path.filename().string()));
        }

        CheckFaults(inputs);
        return failures == 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    // This test faults on purpose (and re-runs itself as a faulting child), so the
    // fault must be a result in this process, never a Windows dialog on the desktop.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (argc >= 3 && std::strcmp(argv[1], "--child") == 0)
        return RunChild(argv[2], argc >= 4 ? std::filesystem::path(argv[3])
                                           : std::filesystem::path());
    if (argc < 2)
    {
        std::printf("usage: zp_save_walk <saves directory>\n");
        return 1;
    }

    try
    {
        Run(argv[1]);
    }
    catch (const std::exception& error)
    {
        Check(false, std::format("the test itself threw {}", error.what()));
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
