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

// The installer keys and their per-arm defaults (generated).
struct OptionKey
{
	const char* key;
	const char* defaultValue;
};
#include "option_keys.inc"

// The repository INI with every known key forced off, then `enabled` forced on:
// isolates a set of arms from whatever the config file happens to contain.
ConfigDocument IsolateOptions(const ConfigDocument& base, const char* const* enabled,
                              std::size_t enabledCount)
{
	ConfigDocument config = base;
	for (std::size_t i = 0; i < kOptionKeyCount; ++i)
		config.entries.push_back({"Options", kOptionKeys[i].key, "0"});
	for (std::size_t i = 0; i < enabledCount; ++i)
		config.entries.push_back({"Options", enabled[i], "1"});
	return config;
}

bool HasMatch(const std::uint8_t* image, std::uint32_t size, const char* signature)
{
	Match match;
	return FindPattern(ModuleView{image, size}, signature, match);
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

// Ordering guard: RemoveUselessChecks must stay ahead of MiscFixes and
// RestoreDeathMusic, as in the original loader. Its tables overwrite one fixed
// byte of each of their signatures (MiscFixes' `E8` at match+9 = 0x90631
// becomes 0x90, RestoreDeathMusic's `E8` at match+0 = 0x41E53A becomes 0x32),
// so with all three enabled both arms must find no match and leave their own
// patch byte untouched: 0x74 at 0x90628 for MiscFixes, and RestoreDeathMusic's
// third CALL (at 0x41E53A) absent. With RemoveUselessChecks off both arms must
// apply. Moving RemoveUselessChecks after them re-enables both installs and
// fails here.
int RunOrderingCheck(std::uint8_t* image, std::uint32_t size, const ConfigDocument& config)
{
	constexpr const char* kMiscFixesSignature =
	    "74 05 E8 ? ? ? ? 6A 05 E8 ? ? ? ? 83 C4 04 B9";
	constexpr const char* kDeathMusicSignature = "E8 ? ? ? ? 84 C0 74 C6 8B BE";
	// Build 9 (1.0.8.0) sites.
	constexpr std::uint32_t kMiscFixesPatchByte = 0x90628; // the `74` MiscFixes turns into EB
	constexpr std::uint32_t kMiscFixesScanByte = 0x90631;  // the `E8` RemoveUselessChecks NOPs
	constexpr std::uint32_t kDeathMusicScanByte = 0x41E53A; // the `E8` RemoveUselessChecks patches
	// RestoreDeathMusic's other two CALLs, which run after RemoveUselessChecks.
	constexpr std::uint32_t kDeathMusicCalls[] = {0x41E3B8, 0x41E4FC};

	if (size <= kMiscFixesScanByte + kBranchBytes)
	{
		std::fprintf(stderr, "zp_dump: ordering: image too small\n");
		return 1;
	}
	const std::vector<std::uint8_t> pristine(image, image + size);
	// The two anchor bytes prove the image is the build these RVAs belong to.
	if (pristine[kMiscFixesScanByte] != 0xE8 || pristine[kDeathMusicScanByte] != 0xE8)
	{
		std::fprintf(stderr, "zp_dump: ordering: image is not 1.0.8.0 (0x%X=%02X, 0x%X=%02X)\n",
		             kMiscFixesScanByte, pristine[kMiscFixesScanByte], kDeathMusicScanByte,
		             pristine[kDeathMusicScanByte]);
		return 1;
	}
	const char* all[] = {"RemoveUselessChecks", "MiscFixes", "RestoreDeathMusic"};
	int failures = 0;

	const auto pass = [&](const char* const* enabled, std::size_t count) {
		std::memcpy(image, pristine.data(), size);
		const ConfigDocument isolated = IsolateOptions(config, enabled, count);
		DWORD code = 0;
		void* where = nullptr;
		if (RunPass(image, size, &isolated, code, where))
			return true;
		std::fprintf(stderr, "zp_dump: ordering: pass faulted %08lX at %p\n", code, where);
		return false;
	};
	const auto expectByte = [&](std::uint32_t site, std::uint8_t want, const char* what) {
		if (image[site] == want)
			return 0;
		std::fprintf(stderr, "zp_dump: ordering: %s at 0x%X is %02X, expected %02X\n", what, site,
		             image[site], want);
		return 1;
	};

	// RemoveUselessChecks on: both installs must be suppressed.
	if (!pass(all, 3))
		return 1;
	failures += expectByte(kMiscFixesScanByte, 0x90, "the RemoveUselessChecks byte");
	failures += expectByte(kDeathMusicScanByte, 0x32, "the RemoveUselessChecks byte");
	failures += expectByte(kMiscFixesPatchByte, 0x74, "MiscFixes' patch byte");
	if (HasMatch(image, size, kMiscFixesSignature))
	{
		std::fprintf(stderr,
		             "zp_dump: ordering: MiscFixes' signature still matches with RemoveUselessChecks on\n");
		++failures;
	}
	if (HasMatch(image, size, kDeathMusicSignature))
	{
		std::fprintf(stderr, "zp_dump: ordering: RestoreDeathMusic's signature still matches with "
		                     "RemoveUselessChecks on\n");
		++failures;
	}
	for (const std::uint32_t site : kDeathMusicCalls)
	{
		if (std::memcmp(image + site, pristine.data() + site, kBranchBytes) == 0)
		{
			std::fprintf(stderr, "zp_dump: ordering: RestoreDeathMusic's call at 0x%X was not "
			                     "written\n", site);
			++failures;
		}
	}

	// RemoveUselessChecks off: both arms must apply. This is the control that
	// proves the suppression above is due to the tables, not to the arms.
	if (!pass(all + 1, 2))
		return 1;
	failures += expectByte(kMiscFixesPatchByte, 0xEB, "MiscFixes' patch byte with RC off");
	failures += expectByte(kMiscFixesScanByte, 0xE8, "the untouched signature byte");
	if (std::memcmp(image + kDeathMusicScanByte, pristine.data() + kDeathMusicScanByte,
	                kBranchBytes) == 0)
	{
		std::fprintf(stderr,
		             "zp_dump: ordering: RestoreDeathMusic's call at 0x%X was not written with RC off\n",
		             kDeathMusicScanByte);
		++failures;
	}

	std::fprintf(stderr, "zp_dump: ordering: %s\n", failures != 0 ? "FAIL" : "ok");
	return failures != 0 ? 1 : 0;
}

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		std::fprintf(stderr,
		             "usage: zp_dump [--per-option] <out.txt> [ZolikaPatch.ini]\n"
		             "       zp_dump --ordering [ZolikaPatch.ini]\n");
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
	const bool ordering = std::strcmp(argv[1], "--ordering") == 0;
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

	// Ordering guard (see RunOrderingCheck).
	if (ordering)
		return RunOrderingCheck(image, size, config);

	// --per-option: run each arm alone (every other key forced to 0) and print
	// "option rva byte" so a divergence can be attributed to one installer.
	if (perOption)
	{
		std::FILE* perOut = std::fopen(argv[2], "wb");
		if (!perOut)
			return 5;
		const std::vector<HookBodyStand> stands = HookBodyStands();
		for (std::size_t i = 0; i < kOptionKeyCount; ++i)
		{
			const char* enabled[] = {kOptionKeys[i].key};
			ConfigDocument single = IsolateOptions(config, enabled, 1);

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
