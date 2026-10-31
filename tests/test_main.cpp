#include "pch.h"
// Native tests for the parts of the mod that do not need the game.
// INI parsing/defaults, build classification and masks, the model catalogue,
// the vehicle-light tables, the menu probes, the ZMenuIV threshold and the
// table shapes. No game files are read.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "common.h"

#include "patch_data.h"
#include "patches.h"
#include "xlive/xliveless.h"

namespace
{
// PE32 layout the write-set dump reads; the tests never parse the shipped PE.
constexpr std::size_t kDosLfanewOffset = 0x3C;
constexpr std::size_t kPeSignatureSize = 4;
constexpr std::size_t kCoffHeaderSize = 20;
constexpr std::size_t kSectionHeaderSize = 40;
constexpr std::size_t kMinPeSize = 0x100;
constexpr std::uint32_t kMaxImageSize = 0x10000000u;
} // namespace

// ---- write-set dump (development aid) -----------------------------------
// Maps a PE32 file into a contiguous image, so a patch pass can be applied to
// the same bytes the loader would see and compared with the reference
// implementation's output. Run: zp_tests --dump-writes <out> [ini]
static bool MapPeImage(const char* path, std::vector<std::uint8_t>& image,
                       std::uint32_t& imageBase, std::uint32_t& sizeOfImage)
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

static int DumpWrites(const char* outPath, const char* iniPath)
{
	std::vector<std::uint8_t> file_image;
	std::uint32_t imageBase = 0;
	std::uint32_t size = 0;
	if (!MapPeImage("game_refs/GTAIV.exe", file_image, imageBase, size) &&
	    !MapPeImage("../game_refs/GTAIV.exe", file_image, imageBase, size) &&
	    !MapPeImage("../../game_refs/GTAIV.exe", file_image, imageBase, size))
	{
		std::fprintf(stderr, "dump-writes: GTAIV.exe not found\n");
		return 2;
	}

	// The image must sit at its preferred base: several rows store and consume
	// absolute module addresses.
	auto* image = static_cast<std::uint8_t*>(
		VirtualAlloc(reinterpret_cast<void*>(static_cast<std::uintptr_t>(imageBase)),
	             size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
	if (!image)
	{
		std::fprintf(stderr, "dump-writes: cannot map at 0x%X (err=%lu, size=0x%X)\n", imageBase,
		             ::GetLastError(), size);
		return 5;
	}
	std::memcpy(image, file_image.data(), size);
	file_image.clear();

	ConfigDocument config;
	if (!LoadConfig(iniPath, config) && !LoadConfig("../game_refs/ZolikaPatch.ini", config) &&
	    !LoadConfig("game_refs/ZolikaPatch.ini", config))
	{
		std::fprintf(stderr, "dump-writes: cannot read %s\n", iniPath);
		return 3;
	}

	std::vector<std::uint8_t> pristine(image, image + size);
	PatchGame(GameBuild::v1080, GameImage{image, size}, config);

	std::FILE* out = std::fopen(outPath, "wb");
	if (!out)
		return 4;
	std::size_t changed = 0;
	for (std::size_t i = 0; i < size; ++i)
	{
		if (image[i] == pristine[i])
			continue;
		std::fprintf(out, "%08zX %02X\n", i, image[i]);
		++changed;
	}
	std::fclose(out);
	std::fprintf(stderr, "dump-writes: %zu bytes changed\n", changed);
	return 0;
}

#define CHECK(x)                                                                                   \
  do                                                                                               \
  {                                                                                                \
    if (!(x))                                                                                      \
    {                                                                                              \
      std::fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x);                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (false)

extern "C" std::uintptr_t zpatch_nms_vtable[24];
extern "C" std::uint32_t zpatch_nms_object;
extern "C" void __cdecl abi_nms_noop_ret0();
extern "C" bool __stdcall abi_nms_reject_ptr(void*);
extern "C" void* __fastcall abi_nms_self(void*, void*, void*);
extern "C" void* __fastcall abi_nms_zero_ptr(void*, void*, void*);
extern "C" void __stdcall abi_nms_noop_pair(void*, void*);
extern "C" bool __cdecl abi_nms_reject();
extern "C" void __stdcall abi_nms_noop_arg(void*);
extern "C" void __cdecl abi_nms_apply();
extern "C" bool zpatch_ooc_selftest() noexcept;

namespace
{

int Run()
{
	// The converted NewMemorySystem stubs keep their original ABI.
	{
		std::uintptr_t slot = 0;
		zpatch_nms_object = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&slot));
		abi_nms_apply();
		CHECK(slot == reinterpret_cast<std::uintptr_t>(zpatch_nms_vtable));
		zpatch_nms_object = 0;
		abi_nms_apply();
		CHECK(slot == reinterpret_cast<std::uintptr_t>(zpatch_nms_vtable));

		int object = 7;
		CHECK(abi_nms_self(&object, nullptr, nullptr) == &object);
		CHECK(abi_nms_zero_ptr(&object, nullptr, nullptr) == nullptr);
		CHECK(!abi_nms_reject_ptr(&object));
		CHECK(!abi_nms_reject());
		abi_nms_noop_ret0();
		abi_nms_noop_pair(&object, &object);
		abi_nms_noop_arg(&object);
	}

	// The old-style INI wrapper must select [Options] and find its keys.
	{
		const char* path = "readable_test.ini";
		std::FILE* file = std::fopen(path, "wb");
		CHECK(file != nullptr);
		std::fputs("[Options]\nMouseFix=1\n", file);
		std::fclose(file);
		INI<> ini(path, true);
		ini.select("Options");
		CHECK(ini.loaded());
		CHECK(std::strcmp(ini.get("MouseFix", "0"), "1") == 0);
		CHECK(std::strcmp(ini.get("Missing", "7"), "7") == 0);
		std::remove(path);
	}

	// The generated OOC payload decodes to the expected table and archive.
	CHECK(zpatch_ooc_selftest());

	// ---- tables ------------------------------------------------------------
	// The install rows are code now, not data; only the NOP list stayed a table.
	CHECK(kUselessCheckCount == 7489u);

	// ---- game hash and build classification --------------------------------
	CHECK(GameHash("police") == 0x79fbb0c5u);
	CHECK(GameHash("POLICE") == GameHash("police"));
	CHECK(GameHash("a\\b") == GameHash("a/b"));
	CHECK(GameHash("\"POLICE\"ignored") == GameHash("police"));
	CHECK(GameHash("") == 2u);
	CHECK(GameHash("hash:0x1234") == 0x1234u);
	CHECK(GameHash("HASH:123trailing") == 123u);
	CHECK(GameHash("hash:  -1tail") == 0xffffffffu);
	CHECK(GameHash("hash:077") == 63u);
	CHECK(GameHash("hash:0") == 0xf4856ffeu);

	CHECK(ClassifyGameBuild(1000) == GameBuild::v1000);
	CHECK(ClassifyGameBuild(1004) == GameBuild::v1004);
	CHECK(ClassifyGameBuild(1040) == GameBuild::v1040);
	CHECK(ClassifyGameBuild(1060) == GameBuild::v1060);
	CHECK(ClassifyGameBuild(1061) == GameBuild::v1061);
	CHECK(ClassifyGameBuild(1070) == GameBuild::v1070);
	CHECK(ClassifyGameBuild(1080) == GameBuild::v1080);
	CHECK(ClassifyGameBuild(1100) == GameBuild::v1100);
	CHECK(ClassifyGameBuild(1110) == GameBuild::v1110);
	CHECK(ClassifyGameBuild(1120) == GameBuild::v1120);
	CHECK(ClassifyGameBuild(1130) == GameBuild::v1130);
	CHECK(ClassifyGameBuild(12043) == GameBuild::v12043);
	CHECK(ClassifyGameBuild(9999) == GameBuild::unknown);

	std::uint32_t compact = 0;
	CHECK(ComposeGameVersion(1, 0, 8, 0, compact) && compact == 1080);
	CHECK(ComposeGameVersion(1, 0, 0, 0, compact) && compact == 1000);

	// ---- INI parsing and defaults -----------------------------------------
	{
		ConfigDocument config;
		CHECK(ParseConfig("\xEF\xBB\xBF; comment\r\n[Options]\r\n MouseFix = 1 \r\nMouseFix=2\n"
		                  "[Other]\nKey=value\n",
		                  config));
		CHECK(config.find("Options", "MouseFix") != nullptr);
		CHECK(config.find("Options", "MouseFix")->value == "2"); // last value wins
		CHECK(config.find("Other", "Key")->value == "value");
		CHECK(ReadIniString(config, "Options", "MouseFix") != nullptr);
		CHECK(ReadIniString(config, "Other", "Key") != nullptr);
		CHECK(ReadIniString(config, "Missing", "Key") == nullptr);
	}
	{
		// An arm's INI gate: a present value must parse to exactly 1, and an absent
		// key uses the default that arm passes.
		ConfigDocument absent;
		CHECK(ParseConfig("[Options]\nMouseFix=1\n", absent));
		const PatchContext ctx{GameImage{nullptr, 0}, 0u, GameBuild::unknown, absent};
		CHECK(ctx.Option("MouseFix", "0"));
		CHECK(ctx.Option("SkipIntro", "1"));  // absent -> the arm's default
		CHECK(!ctx.Option("VSyncFix", "0"));  // absent -> the arm's default
		CHECK(!ctx.Option("RemoveRGSCLoginRequirement", "0"));

		ConfigDocument two;
		CHECK(ParseConfig("[Options]\nMouseFix=2\nVSyncFix=2\n", two));
		const PatchContext ctxTwo{GameImage{nullptr, 0}, 0u, GameBuild::unknown, two};
		CHECK(!ctxTwo.Option("MouseFix", "1"));  // only exactly 1 enables
		CHECK(!ctxTwo.Option("VSyncFix", "1"));

		ConfigDocument one;
		CHECK(ParseConfig("[Options]\nVSyncFix=1\n", one));
		const PatchContext ctxOne{GameImage{nullptr, 0}, 0u, GameBuild::unknown, one};
		CHECK(ctxOne.Option("VSyncFix", "0"));
	}
	{
		ConfigDocument bad;
		CHECK(!ParseConfig("[Options\nMouseFix=1\n", bad));
		CHECK(!ParseConfig("[Options]\nMouseFix\n", bad));
		CHECK(ParseConfig("", bad));
	}

	// ---- catalogue ---------------------------------------------------------
	CHECK(ModelCatalogSize() == 38455);
	CHECK(ModelCatalogName(0) == "01_wires_mh1");
	CHECK(ModelCatalogName(38454) == "ztype");
	CHECK(ModelCatalogName(38455) == "**UNKNOWN**");
	CHECK(ModelNameFromHash(GameHash("police")) == "police");
	CHECK(ModelNameFromHash(GameHash("ztype")) == "ztype");
	CHECK(ModelNameFromHash(0) == "**UNKNOWN**");

	// ---- vehicle-light tables ---------------------------------------------
	{
		ConfigDocument lights;
		CHECK(ParseConfig(
		    "[PopupLightAngles]\nCar1Name=police\nCar1Angle=90\nCar512Name=hash:123\n"
		    "Car512Angle=-180suffix\n[HeadlightConeOffsets]\nCar2Name=POLICE\nCar2Offset=3.5\n",
		    lights));
		VehicleLightTables tables;
		CHECK(LoadVehicleLightTables(lights, tables));
		CHECK(tables.popupAngles[0].modelHash == GameHash("police"));
		CHECK(std::fabs(tables.popupAngles[0].value - 1.57079633f) < 0.00001f);
		CHECK(tables.popupAngles[511].modelHash == 123);
		CHECK(!tables.popupAngles[1].populated);
		CHECK(tables.headlightOffsets[1].value == 3.5f);
		ConfigDocument broken;
		CHECK(ParseConfig("[PopupLightAngles]\nCar1Name=police\nCar1Angle=invalid\n", broken));
		CHECK(!LoadVehicleLightTables(broken, tables));
		CHECK(tables.headlightOffsets[1].value == 3.5f); // atomic rejection
	}

	// ---- pattern parser ----------------------------------------------------
	{
		const std::uint8_t bytes[] = {std::uint8_t{0x90}, std::uint8_t{0x85}, std::uint8_t{0xdb},
		                              std::uint8_t{0x7e}, std::uint8_t{0x41}, std::uint8_t{0x55}};
		Match match;
		// "485" is parsed with strtoul semantics and compared as one byte (0x85).
		CHECK(FindPattern({bytes, sizeof(bytes)}, "485 DB ? 41", match));
		CHECK(match.address == bytes + 1);
		std::vector<const std::uint8_t*> all;
		CHECK(FindAllPatterns({bytes, sizeof(bytes)}, "? DB 7E", all) == 1);
		CHECK(all[0] == bytes + 1);
		CHECK(!FindPattern({bytes, sizeof(bytes)}, "DE AD BE EF", match));
	}

	// ---- menu probes (IVMenuAPI, separate from the ZMenuIV guard) ----------
	CHECK(ProbeMenuApi(false, false, false, true) == MenuProbeResult::disabled);
	CHECK(ProbeMenuApi(true, true, false, false) == MenuProbeResult::already_loaded);
	CHECK(ProbeMenuApi(true, false, false, true) == MenuProbeResult::loaded);
	CHECK(ProbeMenuApi(true, false, true, true) == MenuProbeResult::unavailable);
	{
		MenuIntegrationState state;
		CHECK(MenuIntegrationProbe(state, false) == MenuProbeResult::unavailable);
		CHECK(state.attemptMade && state.loadAttempts == 1);
		CHECK(MenuIntegrationProbe(state, true) == MenuProbeResult::unavailable);
		CHECK(state.loadAttempts == 1); // DAT_101a843c prevents a retry
	}
	{
		std::size_t rows = 0;
		const auto* registrations = MenuRegistrations(rows);
		CHECK(rows == 2);
		CHECK(std::strcmp(registrations[0].family, "PREF_DEATHMUSIC") == 0);
		CHECK(std::strcmp(registrations[1].family, "PREF_FOCUSLOSS") == 0);
	}

	// ---- ZMenuIV guard -----------------------------------------------------
	CHECK(!ZMenuVersionAccepted(0x35DD0u));
	CHECK(ZMenuVersionAccepted(0x35DD1u));
	CHECK(ZMenuVersionAccepted(0x35DD2u));
	CHECK(std::strcmp(kZMenuModuleName, "ZMenuIV.asi") == 0);
	CHECK(std::strcmp(kZMenuProcName, "GetZMenuVersion") == 0);
	CHECK(kZMenuRejectFlags == 0x10u);
	CHECK(kZMenuTerminateExitCode == 0u);
	{
		// Harness cell: the guard fills it only when the export is found.
		std::uint32_t version{};
		CHECK(ZMenuVersionGuard(version) == ZMenuGuardResult::module_absent);
	}

	// ---- SkipLauncher tables ----------------------------------------------
	{
		std::size_t n = 0;
		const auto* entries = SkipLauncherEntries(n);
		CHECK(entries != nullptr && n == 25);
		const auto* collectors = SkipLauncherCollectors(n);
		CHECK(collectors != nullptr && n == 2);
		const auto* dfa = SkipLauncherDfaInstalls(n);
		CHECK(dfa != nullptr && n == 6);
		const auto* rom = SkipLauncherRomInstalls(n);
		CHECK(rom != nullptr && n == 2);
		const auto* collected = SkipLauncherDfaCollected(n);
		CHECK(collected != nullptr && n == 1);
	}

	std::printf("readable tests: PASS\n");
	return 0;
}

} // namespace

int main(int argc, char** argv)
{
	if (argc >= 3 && std::strcmp(argv[1], "--dump-writes") == 0)
		return DumpWrites(argv[2], argc >= 4 ? argv[3] : "game_refs/ZolikaPatch.ini");

	return Run();
}
