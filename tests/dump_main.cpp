#include "pch.h"
// Write-set dump: applies the installers to a mapped copy of game_refs/GTAIV.exe
// and writes "RVA byte" lines, so the result can be diffed against the reference
// implementation. The hook bodies are stubbed (see stub_bodies.cpp): only their
// addresses matter here.
//
// Run: zp_dump <out.txt> [ZolikaPatch.ini]

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include <psapi.h>

#include "common.h"
#include "patches.h"

namespace
{
// PE32 layout the dump reads; 0x400000 is GTA IV's preferred image base.
constexpr std::size_t kDosLfanewOffset = 0x3C;
constexpr std::size_t kPeSignatureSize = 4;
constexpr std::size_t kCoffHeaderSize = 20;
constexpr std::size_t kSectionHeaderSize = 40;
constexpr std::size_t kMinPeSize = 0x100;
constexpr std::uint32_t kMaxImageSize = 0x10000000u;
constexpr unsigned kFaultFrameCount = 16;
constexpr std::size_t kSparePage = 0x1000;
// A rel32 branch: one opcode byte and a four-byte displacement.
constexpr std::uint32_t kBranchBytes = 5;
constexpr std::uint8_t kCallOpcode = 0xE8;
constexpr std::uint8_t kJumpOpcode = 0xE9;

bool MapPeImage(const char* path, std::vector<std::uint8_t>& image, std::uint32_t& imageBase,
                std::uint32_t& sizeOfImage)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
		return false;
	const std::vector<char> raw{std::istreambuf_iterator<char>(in),
	                            std::istreambuf_iterator<char>()};
	std::vector<std::uint8_t> file(raw.size());
	for (std::size_t i = 0; i < raw.size(); ++i)
		file[i] = static_cast<std::uint8_t>(raw[i]);
	if (file.size() < kMinPeSize)
		return false;

	const auto rd16 = [&](std::size_t off) {
		return static_cast<std::uint16_t>(file[off] | (file[off + 1] << 8));
	};
	const auto rd32 = [&](std::size_t off) {
		return static_cast<std::uint32_t>(file[off] | (file[off + 1] << 8) |
		                                  (file[off + 2] << 16) | (file[off + 3] << 24));
	};
	if (file[0] != 'M' || file[1] != 'Z')
		return false;

	const std::size_t e_lfanew = rd32(kDosLfanewOffset);
	if (e_lfanew + 0x60 > file.size())
		return false;
	const std::size_t coff = e_lfanew + kPeSignatureSize;
	const std::size_t optional = coff + kCoffHeaderSize;
	imageBase = rd32(optional + 28);
	sizeOfImage = rd32(optional + 56);
	const std::uint32_t sizeOfHeaders = rd32(optional + 60);
	if (sizeOfImage == 0 || sizeOfImage > kMaxImageSize)
		return false;

	image.assign(sizeOfImage, 0);
	std::memcpy(image.data(), file.data(),
	            sizeOfHeaders < file.size() ? sizeOfHeaders : file.size());
	// IMAGE_SECTION_HEADERs follow COFF + SizeOfOptionalHeader, 40 bytes each.
	const std::size_t sectionTable = optional + rd16(coff + 16);
	for (std::size_t sh = sectionTable,
	                 end = sectionTable + rd16(coff + 2) * kSectionHeaderSize;
	     sh < end; sh += kSectionHeaderSize)
	{
		if (sh + kSectionHeaderSize > file.size())
			return false;
		const std::uint32_t vsize = rd32(sh + 8);
		const std::uint32_t vaddr = rd32(sh + 12);
		const std::uint32_t rsize = rd32(sh + 16);
		const std::uint32_t rptr = rd32(sh + 20);
		const std::uint32_t count = rsize < vsize ? rsize : vsize;
		if (count && vaddr + count <= sizeOfImage && rptr + count <= file.size())
			std::memcpy(image.data() + vaddr, file.data() + rptr, count);
	}
	return true;
}
} // namespace

// The pass runs inside SEH so a fault can be reported; keep this function free
// of C++ objects (MSVC forbids __try in a frame that needs unwinding).
static ULONG_PTR g_access = 0;
static void* g_frames[kFaultFrameCount]{};
static unsigned short g_frameCount = 0;
static void* g_target = nullptr;

// The hook-body table under ZP_DUMP_TOOL (see dllmain.cpp): a body's address in
// the tool is a load-time value, so the dump canonicalises the branches it wrote
// to the body's reference VA. Without that the oracle would change whenever the
// tool's own layout did.
extern std::size_t DumpHookBodyCount() noexcept;
extern std::uint32_t DumpHookBodyReference(std::size_t index) noexcept;
extern std::uintptr_t DumpHookBodyAddress(std::size_t index) noexcept;

namespace
{
struct HookBodyStand
{
	std::uintptr_t address;     // where the stub sits in this process
	std::uint32_t referenceVa;  // the destination the original ASI uses
};

std::vector<HookBodyStand> HookBodyStands()
{
	std::vector<HookBodyStand> stands;
	for (std::size_t i = 0; i < DumpHookBodyCount(); ++i)
		stands.push_back({DumpHookBodyAddress(i), DumpHookBodyReference(i)});
	return stands;
}


// Rewrite every branch this pass wrote to a hook body so its displacement names
// the body's reference VA instead of the stub's load address. Byte-for-byte this
// keeps the installer's decisions and drops the tool's own layout from the
// oracle; a body branch the installer did not write is left alone.
void CanonicaliseHookBodyBranches(std::uint8_t* image, const std::uint8_t* pristine,
                                  std::uintptr_t imageBase, std::uint32_t size,
                                  const std::vector<HookBodyStand>& stands)
{
	for (std::uint32_t offset = 0; offset + kBranchBytes <= size; ++offset)
	{
		const std::uint8_t opcode = image[offset];
		if (opcode != kCallOpcode && opcode != kJumpOpcode)
			continue;
		const bool written = opcode != pristine[offset] ||
		                     std::memcmp(image + offset + 1, pristine + offset + 1,
		                                 kBranchBytes - 1) != 0;
		if (!written)
			continue;
		std::int32_t relative = 0;
		std::memcpy(&relative, image + offset + 1, sizeof(relative));
		// An x86 branch encodes a 32-bit displacement; compare the truncated
		// destination, exactly as the CPU resolves it.
		const std::uint32_t next = imageBase + offset + kBranchBytes;
		const std::uint32_t destination = next + static_cast<std::uint32_t>(relative);
		for (const HookBodyStand& stand : stands)
		{
			if (destination != static_cast<std::uint32_t>(stand.address))
				continue;
			const auto canonical = static_cast<std::int32_t>(stand.referenceVa - next);
			std::memcpy(image + offset + 1, &canonical, sizeof(canonical));
			break;
		}
	}
}
} // namespace

static bool RunPass(std::uint8_t* image, std::uint32_t size, const ConfigDocument* config,
                    DWORD& code, void*& where)
{
	__try
	{
		PatchGame(GameBuild::v1080, GameImage{image, size}, *config);
		return true;
	}
	__except (code = GetExceptionCode(),
	          where = GetExceptionInformation()->ExceptionRecord->ExceptionAddress,
	          g_access = GetExceptionInformation()->ExceptionRecord->ExceptionInformation[0],
	          g_target = reinterpret_cast<void*>(
	              GetExceptionInformation()->ExceptionRecord->ExceptionInformation[1]),
	          g_frameCount = RtlCaptureStackBackTrace(0, kFaultFrameCount, g_frames, nullptr),
	          EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: zp_dump [--per-option] <out.txt> [ZolikaPatch.ini]\n");
		return 1;
	}

	std::vector<std::uint8_t> file_image;
	std::uint32_t imageBase = 0;
	std::uint32_t size = 0;
	if (!MapPeImage("game_refs/GTAIV.exe", file_image, imageBase, size) &&
	    !MapPeImage("../game_refs/GTAIV.exe", file_image, imageBase, size))
	{
		std::fprintf(stderr, "zp_dump: GTAIV.exe not found\n");
		return 2;
	}

	// Absolute module addresses are part of the image: map it at its own base.
	// One spare page: signature scans may read a few bytes past the image end.
	auto* image = static_cast<std::uint8_t*>(
	    VirtualAlloc(reinterpret_cast<void*>(static_cast<std::uintptr_t>(imageBase)),
	                 size + kSparePage, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!image)
	{
		// 487 is ERROR_INVALID_ADDRESS: something (a Win32 process's heap and
		// loader mappings, or another image) already owns the low range. Describe
		// what covers the base so the failure is diagnosable from the log.
		MEMORY_BASIC_INFORMATION region{};
		std::fprintf(stderr, "zp_dump: cannot map at 0x%X (err=%lu), module base %p\n", imageBase,
		             GetLastError(), static_cast<void*>(GetModuleHandleA(nullptr)));
		if (VirtualQuery(reinterpret_cast<void*>(static_cast<std::uintptr_t>(imageBase)), &region,
		                 sizeof(region)))
		{
			char mapped[MAX_PATH]{};
			GetMappedFileNameA(GetCurrentProcess(),
			                   reinterpret_cast<void*>(static_cast<std::uintptr_t>(imageBase)), mapped,
			                   sizeof(mapped));
			std::fprintf(stderr, "zp_dump: 0x%X is state=%lu type=%lu in %p..%p file=%s\n",
			             imageBase, region.State, region.Type, region.BaseAddress,
			             static_cast<void*>(static_cast<std::uint8_t*>(region.BaseAddress) +
			                                region.RegionSize),
			             mapped[0] ? mapped : "(anonymous)");
		}
		return 3;
	}
	std::memcpy(image, file_image.data(), size);

	const bool perOption = argc >= 3 && std::strcmp(argv[1], "--per-option") == 0;
	const char* ini = perOption ? (argc >= 4 ? argv[3] : "game_refs/ZolikaPatch.ini")
	                            : (argc >= 3 ? argv[2] : "game_refs/ZolikaPatch.ini");
	ConfigDocument config;
	if (!LoadConfig(ini, config) && !LoadConfig("../game_refs/ZolikaPatch.ini", config))
	{
		std::fprintf(stderr, "zp_dump: cannot read %s\n", ini);
		return 4;
	}

	std::fprintf(stderr, "zp_dump: module base %p\n",
	             static_cast<void*>(GetModuleHandleA(nullptr)));

	// --per-option: run each arm alone (every other key forced to 0) and print
	// "option rva byte" so a divergence can be attributed to one installer.
	if (perOption)
	{
		struct OptionKey
		{
			const char* key;
			const char* defaultValue;
		};
#include "option_keys.inc"

		std::FILE* perOut = std::fopen(argv[2], "wb");
		if (!perOut)
			return 5;
		const std::vector<HookBodyStand> stands = HookBodyStands();
		for (std::size_t i = 0; i < kOptionKeyCount; ++i)
		{
			ConfigDocument single = config;
			for (std::size_t j = 0; j < kOptionKeyCount; ++j)
				single.entries.push_back({"Options", kOptionKeys[j].key, "0"});
			single.entries.push_back({"Options", kOptionKeys[i].key, "1"});

			std::memcpy(image, file_image.data(), size);
			PatchGame(GameBuild::v1080, GameImage{image, size}, single);
			CanonicaliseHookBodyBranches(image, file_image.data(), imageBase, size, stands);
			for (std::size_t b = 0; b < size; ++b)
			{
				if (image[b] != file_image[b])
					std::fprintf(perOut, "%s %08zX %02X\n", kOptionKeys[i].key, b, image[b]);
			}
		}
		std::fclose(perOut);
		return 0;
	}

	std::vector<std::uint8_t> pristine(image, image + size);

	// Report where the pass faults instead of dying silently: the installers
	// write the way the original does, so a fault is a finding.
	DWORD faultCode = 0;
	void* faultAddress = nullptr;
	if (!RunPass(image, size, &config, faultCode, faultAddress))
	{
		for (unsigned short i = 0; i < g_frameCount; ++i)
		{
			std::fprintf(stderr, "  frame %u: rva %llX\n", i,
			             static_cast<unsigned long long>(
			                 reinterpret_cast<std::uintptr_t>(g_frames[i]) -
			                 reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr))));
		}
		std::fprintf(stderr, "zp_dump: fault %08lX at %p access=%llu target=%p (image %p..%p)\n",
		             faultCode, faultAddress, static_cast<unsigned long long>(g_access), g_target,
		             static_cast<void*>(image), static_cast<void*>(image + size));
		return 6;
	}

	std::FILE* out = std::fopen(argv[1], "wb");
	if (!out)
		return 5;
	CanonicaliseHookBodyBranches(image, pristine.data(), imageBase, size, HookBodyStands());
	std::size_t changed = 0;
	for (std::size_t i = 0; i < size; ++i)
	{
		if (image[i] == pristine[i])
			continue;
		std::fprintf(out, "%08zX %02X\n", i, image[i]);
		++changed;
	}
	std::fclose(out);
	std::fprintf(stderr, "zp_dump: %zu bytes changed\n", changed);
	return 0;
}
