// ZolikaPatch ASI entry point.
//
// Everything runs synchronously from DllMain: the DRM has already rewritten the
// game's import-time code by the time a deferred worker would get to scan, so the
// signatures have to be matched now.
//
// The support library is split into FusionFix-style modules under `source/`
// (one logical piece per .ixx); this TU keeps the entry point, the loader
// pipeline and the option pass.

#include "pch.h"

#include "common.hxx"

#include "common.h"
#include "hook_bodies.h"
#include "hooks.h"
#include "patches.h"
#include "rebased.h"
#include "xlive/xliveless.h"
import zp.common;
import zp.game;
import zp.patch;
import zp.log;
import zp.config;
import zp.vehicle_lights;
import zp.menu;
import zp.hook_bodies;
import zp.useless_checks;
import zp.catalogue;
import zp.launcher;

char iniPath[MAX_PATH];

// The menu-integration decision the RestoreDeathMusic arm reads
// (declared in src/patches.h so the feature modules link it).
bool bMenuIntegration = false;

namespace
{

// ---- loader --------------------------------------------------------------

constexpr std::uint64_t kOldDsoundLoaderSize = 91648ull;
constexpr std::uint32_t kUnsupportedVersionMin = 1201;
constexpr std::uint32_t kUnsupportedVersionMax = 12999;
constexpr DWORD kUnsupportedSleepMs = 500;
constexpr UINT kUnsupportedExitCode = E_UNEXPECTED;  // 0x8000FFFF
static_assert(kUnsupportedExitCode == 0x8000FFFFu);


// LoadDLCs (original handler sub_100234E0): the loader's return value is stored
// as the callback and the two DLC entry points it patches in are RVAs.
// provenance: GTAIV_1070.exe / GTAIV_1060.exe / GTAIV.exe, IDA 2026-10-31;
// instructions: `add edi, 414560h`, `lea edi, [eax+4136E0h]`, `lea edi,
// [eax+414080h]` (and the 1060/1080 equivalents).
constexpr GameRva kDlcLoadCallback_1070{0x414560u};
constexpr GameRva kDlcRegister_1070{0x4136E0u};
constexpr GameRva kDlcApply_1070{0x414080u};
constexpr GameRva kDlcLoadCallback_1060{0x414300u};
constexpr GameRva kDlcRegister_1060{0x413480u};
constexpr GameRva kDlcApply_1060{0x413E20u};
constexpr GameRva kDlcLoadCallback_1080{0x490460u};
constexpr GameRva kDlcRegister_1080{0x48F440u};
constexpr GameRva kDlcApply_1080{0x48FE00u};

// LoadDLCs rail: the game functions the original's handler calls/jumps to
// after patching the loader. provenance: same three images; instructions
// `mov byte ptr [edi], 0E9h` / `0E8h` at the site named by each constant.
constexpr GameRva kLoadDlcsJmpA_1070{0x4144B8u};   // 0x4144B8: jmp target
constexpr GameRva kLoadDlcsJmpB_1070{0x41452Cu};   // 0x41452C: jmp target
constexpr GameRva kLoadDlcsCall_1070{0x413E30u};   // 0x413E30: call target
constexpr GameRva kLoadDlcsJmpA_1060{0x414258u};   // 0x414258: jmp target
constexpr GameRva kLoadDlcsJmpB_1060{0x4142CCu};   // 0x4142CC: jmp target
constexpr GameRva kLoadDlcsCall_1060{0x413BD0u};   // 0x413BD0: call target
constexpr GameRva kLoadDlcsJmpA_1080{0x4903B8u};   // 0x4903B8: jmp target
constexpr GameRva kLoadDlcsJmpB_1080{0x49042Cu};   // 0x49042C: jmp target
constexpr GameRva kLoadDlcsCall_1080{0x48FBB0u};   // 0x48FBB0: call target

// SuperLODFix (original handler sub_10046A10): the three game addresses the
// shim tail-jumps to / calls, per build. provenance: IDA 2026-10-31; the
// original's `lea edi, [ecx+4A3CE9h]` site is the JumpToBody the arm patches.
constexpr GameRva kSuperLodFail_1080{0x4A3D37u};
constexpr GameRva kSuperLodOk_1080{0x4A3CEFu};
constexpr GameRva kSuperLodLookup_1080{0x58AAE0u};
constexpr GameRva kSuperLodFail_1070{0x540E67u};
constexpr GameRva kSuperLodOk_1070{0x540E1Fu};
constexpr GameRva kSuperLodLookup_1070{0x4DD2D0u};

// The packed entry-stub jumps the RemoveModChecks arms write (original handler
// sub_10044710). provenance: GTAIV_1060.exe / GTAIV_1070.exe, IDA 2026-10-31;
// the original's `add esi/edi, <rva>` instruction precedes each write.
constexpr GameRva kModChecksEntryJmp_1060{0x003A09u};
constexpr GameRva kModChecksMidJmp_1060{0x0038AEu};
constexpr GameRva kModChecksCaseJmp_1060{0x0039E9u};
constexpr GameRva kModChecksEntryJmp_1070{0x003A09u};
constexpr GameRva kModChecksMidJmp_1070{0x0038AEu};
constexpr GameRva kModChecksCaseJmp_1070{0x0039E9u};


// Structural values the arms and the compiled bodies would otherwise spell as
// bare hex. Each is a field offset, a size or a mask; the recovered literal is
// in the comment (research 07 §2).
// The two push immediates ForceCarHeadlightShadows' patterns embed (0x0504
// and 0x0104); the arm rewrites their two bytes.
constexpr std::uint16_t kShadowPushA = 0x0504u;
constexpr std::uint16_t kShadowPushB = 0x0104u;
constexpr std::uint8_t kMinimizePauseTick = 0x06u;  // DoNotPauseOnMinimize
constexpr std::uint8_t kZeroByte = 0x00u;                   // clear a byte
constexpr std::uint32_t kTrueValue = 1u;                    // mov eax,1
constexpr std::uint16_t kRet8Imm = 0x08C2u;     // C2 08 = ret 8
constexpr std::uint8_t kRet8High = 0x00u;       // high byte of the imm16
constexpr std::uint16_t kCutsceneBranchDisp = 0x009Eu;  // je +0x9E
constexpr std::uint8_t kCutsceneJmpDisp = 0x2Au;        // jmp +0x2A
constexpr std::uint8_t kMiscJmpDispC9 = 0xC9u;          // jmp +0xC9
constexpr std::uint8_t kMiscJmpDisp0D = 0x0Du;          // jmp +0x0D
constexpr std::uint32_t kVehicleStructLimit = 100u;     // 0x64: struct count
constexpr std::uint32_t kPtrNodeCount = 320'000u;       // 0x4E200
constexpr std::uint32_t kPtrNodeCountAlt = 64'000u;     // 0xFA00
constexpr std::uint32_t kPtrNodeInitial = 0x400u;       // 0x400
constexpr std::uint32_t kModelLimitIncrease = 0x100u;   // 0x100
constexpr std::uint32_t kForcedShadowSize = 0x100u;     // 0x100






bool bDFAInstalled = false;

void ResolvePaths(HMODULE module)
{
	char path[MAX_PATH]{};
	const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
	if (!length || length >= MAX_PATH)
		return;

	char* slash = path + length;
	while (slash != path && slash[-1] != '\\' && slash[-1] != '/')
		--slash;

	const std::size_t prefix = static_cast<std::size_t>(slash - path);
	const auto copyName = [&](const char* name, char* destination) {
		const std::size_t nameLength = std::strlen(name) + 1;
		if (prefix + nameLength > MAX_PATH)
			return;
		std::memcpy(destination, path, prefix);
		std::memcpy(destination + prefix, name, nameLength);
	};

	copyName("ZolikaPatch.ini", iniPath);
}

// The checks DllMain runs first: a specific old dsound.dll ASI loader gets a
// warning, and versions 1201..12999 (Complete Edition and newer) open the
// downgrade page and terminate.
void CheckGameVersion(std::uint32_t compactVersion)
{
	if (HMODULE dsound = GetModuleHandleW(L"dsound.dll"))
	{
		wchar_t path[MAX_PATH]{};
		WIN32_FILE_ATTRIBUTE_DATA info{};
		if (GetModuleFileNameW(dsound, path, MAX_PATH) &&
		    GetFileAttributesExW(path, GetFileExInfoStandard, &info))
		{
			const std::uint64_t size =
				(static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
			if (size == kOldDsoundLoaderSize)
			{
				MessageBoxA(0,
				            "Old dsound.dll detected! It's strongly recommended to use Ultimate ASI "
				            "Loader as either dinput8.dll or xlive.dll to load this mod.",
				            "ZPatch Warning", 0);
			}
		}
	}

	if (compactVersion >= kUnsupportedVersionMin && compactVersion <= kUnsupportedVersionMax)
	{
		std::system("start https://zolika1351.pages.dev/mods/ivpatch/downgrading");
		Sleep(kUnsupportedSleepMs);
		TerminateProcess(GetCurrentProcess(), kUnsupportedExitCode);
	}
}

// The three modules the option arms consult: the GFWL shim the mod patches by
// RVA, XLivelessAddon (which disables some arms) and CitizenIV (multiplayer).
void CaptureModules()
{
	zpatch_xlive_base =
		static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(GetModuleHandleA("xlive.dll")));
	zpatch_xliveless_present = GetModuleHandleA("XLivelessAddon.asi") ? 1u : 0u;
	zpatch_citizen_present = GetModuleHandleA("CitizenIV.exe") ? 1u : 0u;
}

bool LoadConfigAndFlags(INI<>& ini, ConfigDocument& config)
{
	VehicleLightTables tables;
	if (!LoadVehicleLightTables(config, tables))
		return false;

	for (std::size_t i = 0; i < tables.popupAngles.size(); ++i)
	{
		const VehicleLightValue& popup = tables.popupAngles[i];
		zpatch_popup_hashes[i] = popup.populated ? popup.modelHash : 0u;
		zpatch_popup_angles[i] = popup.populated ? popup.value : 0.0f;

		const VehicleLightValue& offset = tables.headlightOffsets[i];
		zpatch_fchs_hashes[i] = offset.populated ? offset.modelHash : 0u;
		zpatch_fchs_offsets[i] = offset.populated ? offset.value : 0.0f;
	}

	// The option globals the hook bodies read. An arm only ever sets its flag.
	if (Option(ini, "RestoreDeathMusic", "0"))
		zpatch_flag_death_music = 1u;
	if (Option(ini, "Patch4RemoveModChecks", "0"))
		zpatch_flag_patch4_modchecks = 1u;
	if (Option(ini, "RemoveGFWLUpdatePopup", "0"))
		zpatch_flag_gfwl_popup = 1u;
	if (Option(ini, "FreezeCarFix", "1"))
	{
		if (Option(ini, "DebugPrintSpawns", "0"))
			zpatch_flag_debug_spawns = 1u;
	}
	if (Option(ini, "DoNotPauseOnMinimize", "0"))
		zpatch_flag_donotpause = 1u;
	if (Option(ini, "MovingSteeringWheelSupport", "1"))
		zpatch_flag_steering = 1u;
	if (Option(ini, "MovingPlaneParts", "1"))
		zpatch_flag_plane = 1u;
	if (Option(ini, "ShakingEngineBlock", "1"))
		zpatch_flag_engine = 1u;

	if (const char* dat = ini.get("OverrideGTADatPathTo"))
		SetOverrideGtaDatPath(dat);

	// Patch6and7RemoveModChecks is also enabled by its undocumented companion.
	if (!Option(ini, "Patch6and7RemoveModChecks", "0") &&
	    Option(ini, "Patch7RemoveModChecks", "0"))
		config.entries.push_back({kOptionsSection, "Patch6and7RemoveModChecks", "1"});

	return true;
}

// ---- SkipLauncher --------------------------------------------------------

bool IsDFAAllowed(GameBuild build)
{
	if (zpatch_xliveless_present)
		return false;
	if (GetModuleHandleA("GTAC.dll"))
		return false;

	switch (build)
	{
	case GameBuild::v1100:
	case GameBuild::v1110:
	case GameBuild::v1120:
	case GameBuild::v1130:
		return false;
	default:
		return true;
	}
}

// The DFA stage installs once per process, whoever asks for it first.
void ApplyDFA(GameBuild build, const GameImage& image)
{
	if (!IsDFAAllowed(build) || bDFAInstalled)
		return;

	bDFAInstalled = true;
	SkipLauncherReport report;
	(void)ApplySkipLauncher(image, &ResolveSkipHook, nullptr, report, SkipLauncherStage::dfa);
	SetCreateFileSlot(report.createFileSlot);
}

// 0x10045CE0's XLivelessAddon guard covers the main writes and the DFA
// installer; the ROM service runs after it unconditionally. 0x10004850 adds the
// GTAC, build and one-shot guards.
SkipLauncherReport ApplySkipLauncherIfEnabled(GameBuild build, INI<>& ini)
{
	SkipLauncherReport report;
	if (!Option(ini, "SkipLauncher", "1"))
		return report;

	auto* module = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
	if (!module)
		return report;

	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return report;

	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return report;

	SkipLauncherStage stages = SkipLauncherStage::rom;
	if (!zpatch_xliveless_present)
		stages = stages | SkipLauncherStage::main;
	if (IsDFAAllowed(build) && !bDFAInstalled)
	{
		stages = stages | SkipLauncherStage::dfa;
		bDFAInstalled = true;
	}

	const GameImage image{module, nt->OptionalHeader.SizeOfImage};
	(void)ApplySkipLauncher(image, &ResolveSkipHook, nullptr, report, stages);

	// The DFA stage may already have captured the CreateFile slot; only a fresh
	// capture replaces it.
	if (report.createFileSlot)
		SetCreateFileSlot(report.createFileSlot);
	return report;
}

// ---- menu integration ----------------------------------------------------

// AddOptionsInMenu loads IVMenuAPI.asi once; a failed load is remembered so the
// probe is not repeated.
bool IsMenuIntegrationAvailable(INI<>& ini)
{
	if (!Option(ini, "AddOptionsInMenu", "1"))
		return false;
	if (GetModuleHandleA("IVMenuAPI.asi"))
		return true;

	static bool bLoadFailed = false;
	if (bLoadFailed)
		return false;
	if (LoadLibraryA("IVMenuAPI.asi"))
		return true;

	bLoadFailed = true;
	return false;
}

void EnforceZMenuVersion()
{
	// Only the guard's result matters; the version it parses is discarded.
	std::uint32_t out = 0;
	if (ZMenuVersionGuard(out) != ZMenuGuardResult::version_rejected)
		return;

	MessageBoxA(nullptr, kZMenuRejectText, kZMenuRejectCaption, kZMenuRejectFlags);
	TerminateProcess(GetCurrentProcess(), kZMenuTerminateExitCode);
}

// ---- LaunchWithPlayGTAIV -------------------------------------------------

// The launch-slot rewrite only runs when a PlayGTAIV.exe launcher is present;
// FindGameProcess matches the same process entry.
bool IsPlayGTAIVRunning()
{
	return FindGameProcess() != 0;
}

void ApplyLaunchWithPlayGTAIV(GameBuild build, GameImage image, INI<>& ini)
{
	if (!Option(ini, "LaunchWithPlayGTAIV", "0"))
		return;
	if (!IsPlayGTAIVRunning())
		return;

	// The original's table covers exactly these five builds; keep that gate
	// verbatim (the scan also hits builds the original never touched).
	switch (build)
	{
	case GameBuild::v1000:
	case GameBuild::v1040:
	case GameBuild::v1060:
	case GameBuild::v1070:
	case GameBuild::v1080:
		break;
	default:
		return;
	}

	// Collect every hit before writing: the write destroys the string a re-scan
	// would need (research 11 §3.4). Pattern(image, ...) is single-site; the
	// launcher needs both slots, so this uses the multi-hit helper explicitly.
	const ModuleView view{image.base, image.size};
	std::vector<const std::uint8_t*> sites;
	if (FindAllPatterns(view, zp::kLauncherSlotSignature, sites) == 0)
		return;
	for (const std::uint8_t* site : sites)
	{
		injector::WriteMemoryRaw(const_cast<std::uint8_t*>(site),
		                         const_cast<char*>(zp::kLauncherNamePatch.data()),
		                         sizeof(zp::kLauncherNamePatch), true); // launcher name
	}
}

// ---- loader --------------------------------------------------------------

void SetStuff()
{
	if (!iniPath[0])
		return;

	TraceBoot("ini");
	INI<> ini(iniPath, true);
	ini.select(kOptionsSection);
	ConfigDocument& config = ini.document();
	// Deferred on purpose: the load keeps its loader position, and the guard below
	// must run even when it fails (the single-use flag is the price of that order).
	const bool initialized = ini.loaded() && LoadConfigAndFlags(ini, config);
	TraceBoot("config");

	// The ZMenuIV guard follows config-object construction and is unconditional, so
	// a failed initialize must not suppress it.
	TraceBoot("zmenu");
	EnforceZMenuVersion();
	if (!initialized)
		return;

	// Only the classified build is used below; the compact version is DllMain's.
	std::uint32_t out = 0;
	GameBuild build = GameBuild::unknown;
	(void)QueryGameVersion(out, build);

	auto* moduleBase = reinterpret_cast<std::uint8_t*>(GetModuleHandleA(nullptr));
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
	const auto* nt = dos && dos->e_magic == IMAGE_DOS_SIGNATURE
	                     ? reinterpret_cast<const IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew)
	                     : nullptr;

	// MiscFixes' loader arm runs before SkipLauncher's and calls the one-shot DFA
	// installer under the same guards; the SkipLauncher call is then a no-op.
	if (nt && nt->Signature == IMAGE_NT_SIGNATURE && Option(ini, "MiscFixes", "1"))
	{
		TraceBoot("dfa");
		ApplyDFA(build, GameImage{moduleBase, nt->OptionalHeader.SizeOfImage});
	}

	TraceBoot("skiplauncher");
	(void)ApplySkipLauncherIfEnabled(build, ini);

	// FixPCIDKickbug: drop the GFWL CD-key value so keygen users stop kicking
	// each other on join.
	TraceBoot("pcid");
	if (Option(ini, "FixPCIDKickbug", "1"))
	{
		if (HMODULE shlwapi = LoadLibraryW(L"shlwapi.dll"))
		{
			const auto deleteValue = reinterpret_cast<LONG(WINAPI*)(HKEY, LPCWSTR, LPCWSTR)>(
				GetProcAddress(shlwapi, "SHDeleteValueW"));
			if (deleteValue)
			{
				deleteValue(HKEY_CURRENT_USER, L"Software\\Classes\\Software\\Microsoft\\XLive",
				            L"PCID");
			}
		}
	}

	if (build == GameBuild::unknown)
		return;
	if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
		return;

	const GameImage gameImage{moduleBase, nt->OptionalHeader.SizeOfImage};

	// With AddOptionsInMenu on and the module available, the two runtime toggles
	// are registered and RestoreDeathMusic's hooks install.
	TraceBoot("menu");
	const bool menu = IsMenuIntegrationAvailable(ini);
	SetMenuIntegration(menu);
	if (menu)
		RegisterMenuOptions();
	if (menu || Option(ini, "DoNotPauseOnMinimize", "0"))
		zpatch_menu_prime_focus_loss();

	// LocalSettingsPath captures the CWD and arms its call slot before installing.
	if (Option(ini, "LocalSettingsPath", "0"))
	{
		TraceBoot("localsettings");
		zpatch_local_settings_capture_cwd();
	}

	TraceBoot("preamble");
	(void)FindGamePointers(gameImage);

	// CarDecalsFix's double-load guard: when its scan finds nothing, another ASI
	// loader already patched the site.
	if (Option(ini, "CarDecalsFix", "1"))
	{
		Match decals;
		const ModuleView view{gameImage.base, gameImage.size};
		if (!FindPattern(view, "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00", decals))
		{
			MessageBoxA(0,
			            "ZPatch was loaded twice! Make sure that you don't have multiple ASI "
			            "loaders installed! (e.g. xliveless + dsound, xliveless + dinput8, "
			            "dsound + dinput8, etc)",
			            "ZPatch Error", 0);
		}
	}

	TraceBoot("patchgame");
	PatchGame(build, gameImage, config);

	ApplyLaunchWithPlayGTAIV(build, GameImage{moduleBase, nt->OptionalHeader.SizeOfImage}, ini);
}
} // namespace

#if defined(ZP_PARITY_DRIVER)
// Offline state driver only (readable/tests/state_dump.cpp); never compiled into
// the shipped ASI. The driver must run the loader's own INI/flags arm and its
// own SkipLauncher staging against a caller-owned mapped image instead of the
// running process, so these forward to the same functions DllMain uses rather
// than re-implementing them in the harness.
bool ZpDriverLoadConfigAndFlags(INI<>& ini, ConfigDocument& config)
{
	return LoadConfigAndFlags(ini, config);
}

bool ZpDriverOption(INI<>& ini, const char* key, const char* defaultValue)
{
	return Option(ini, key, defaultValue);
}

void ZpDriverApplySkipLauncher(GameBuild build, GameImage image, INI<>& ini,
                               SkipHookResolver resolver) noexcept
{
	// SetStuff's order: MiscFixes' arm owns the one-shot DFA pass, then
	// ApplySkipLauncherIfEnabled adds whatever stages are still pending. The
	// driver has no XLivelessAddon/GTAC module, so the `main` stage is always on
	// and IsDFAAllowed depends only on the build.
	if (ZpDriverOption(ini, "MiscFixes", "1") && IsDFAAllowed(build) && !bDFAInstalled)
	{
		bDFAInstalled = true;
		SkipLauncherReport report;
		(void)ApplySkipLauncher(image, resolver, nullptr, report, SkipLauncherStage::dfa);
		SetCreateFileSlot(report.createFileSlot);
	}

	if (!ZpDriverOption(ini, "SkipLauncher", "1"))
		return;

	SkipLauncherStage stages = SkipLauncherStage::rom | SkipLauncherStage::main;
	if (IsDFAAllowed(build) && !bDFAInstalled)
	{
		stages = stages | SkipLauncherStage::dfa;
		bDFAInstalled = true;
	}

	SkipLauncherReport report;
	(void)ApplySkipLauncher(image, resolver, nullptr, report, stages);
	if (report.createFileSlot)
		SetCreateFileSlot(report.createFileSlot);
}
#endif  // ZP_PARITY_DRIVER

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID /*reserved*/)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		TraceBoot("dllmain");
		TraceBootValue("module", reinterpret_cast<std::uintptr_t>(module));
		DisableThreadLibraryCalls(module);
		InstallFaultTracer();
		ResolvePaths(module);
		TraceBoot("paths");

		std::uint32_t compactVersion = 0;
		GameBuild out = GameBuild::unknown; // the build itself is re-read in SetStuff
		(void)QueryGameVersion(compactVersion, out);

		TraceBoot("version");
		CheckGameVersion(compactVersion);
		TraceBoot("modules");
		CaptureModules();
		TraceBootValue("base", reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr)));
		SetStuff();
	}
	return TRUE;
}



// The ten spawn-probe case bodies: `offset` is the byte distance from the
// probe match to the case body (the original's jump-table selector).
struct SpawnCase final
{
	std::uint32_t offset;
	void (*body)();
};
constexpr SpawnCase kSpawnCases[] = {
	{0x24u, &abi_spawns_case_0x24},
	{0x3Du, &abi_spawns_case_0x3D},
	{0x56u, &abi_spawns_case_0x56},
	{0x6Fu, &abi_spawns_case_0x6F},
	{0x88u, &abi_spawns_case_0x88},
	{0x9Eu, &abi_spawns_case_0x9E},
	{0xB4u, &abi_spawns_case_0xB4},
	{0xCAu, &abi_spawns_case_0xCA},
	{0xE0u, &abi_spawns_case_0xE0},
	{0xF6u, &abi_spawns_case_0xF6},
};

void PatchGame(GameBuild build, GameImage image, const ConfigDocument& config)
{
	PatchContext ctx{image, reinterpret_cast<std::uintptr_t>(image.base), build,
	                  config};

	// Publish every slot before any arm writes a site: a site write can
	// overwrite the signature a later arm's slot publish scans for.
	Trace("publish BadPlayerModelCrashFix");
	// BadPlayerModelCrashFix: 1 slot
	if (ctx.Option("BadPlayerModelCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C"); pattern.size() > 0)
		{
			zpatch_bpm_resume = MatchAddress(pattern, 10);
		}
	}
	Trace("publish BlackscreenFix");
	// BlackscreenFix: 2 slots
	if (ctx.Option("BlackscreenFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "75 54 8B 48 28 8B 40 2C"); pattern.size() > 0)
		{
			zpatch_blackscreen_resume = MatchTarget(pattern, 82, 86);
			const auto address = static_cast<std::uintptr_t>(MatchTarget(pattern, 82, 87));
			if (InImage(image, address))
				zpatch_blackscreen_value_ptr = *reinterpret_cast<const std::uint32_t*>(address);
		}
	}
	Trace("publish BuildingDynamicShadows");
	// BuildingDynamicShadows: 4 slots
	if (ctx.Option("BuildingDynamicShadows", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 47 28 C1 E8 06 83 E0 0F 83 C0"); pattern.size() > 0)
		{
			zpatch_bds_resume_a = MatchAddress(pattern, 15);
			zpatch_bds_resume_b = MatchAddress(pattern, 112);
		}

		// The two hash slots take the same field of the FIRST and the SECOND
		// match (recovered rows: dword_10159C1C index 0, dword_10159C24 index 1).
		// The two matches hold different pointers (0xD8FA2C / 0xDCF48C on 1080),
		// so publishing both from the first is not the original's state.
		auto hashes = Pattern(
		    image, "8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06");
		if (hashes.size() > 0)
			zpatch_bds_hash_a = *hashes.get(0).get<std::uint32_t>(20);
		if (hashes.size() > 1)
			zpatch_bds_hash_b = *hashes.get(1).get<std::uint32_t>(20);
	}
	Trace("publish CollisionDeadlockFix");
	// CollisionDeadlockFix: 1 slot
	if (ctx.Option("CollisionDeadlockFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66"); pattern.size() > 0)
		{
			zpatch_collision_deadlock_should_walk = MatchTarget(pattern, 91, 95);
		}
	}
	Trace("publish CopSirensFlag");
	// CopSirensFlag: 3 slots
	if (ctx.Option("CopSirensFlag", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00"); pattern.size() > 0)
		{
			zpatch_copsirens_resume = MatchAddress(pattern, 5);
		}

		if (auto pattern = Pattern(image, "0F BF 41 2E 39 05"); pattern.size() > 0)
		{
			zpatch_copsirens_resume_b = MatchAddress(pattern, 28);
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			zpatch_ide_hook_slot = MatchAddress(pattern, -1);
		}
	}
	Trace("publish DualVehicleHeadlights");
	// DualVehicleHeadlights: 5 slots
	if (ctx.Option("DualVehicleHeadlights", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
		{
			zpatch_dual_bcc = *pattern.get(0).get<std::uint32_t>(207);
			zpatch_dual_bd4 = MatchTarget(pattern, 331, 335);
			zpatch_dual_bd0 = MatchTarget(pattern, 140, 144);
			zpatch_dual_be4 = MatchTarget(pattern, 151, 155);
			zpatch_dual_be0 = MatchAddress(pattern, 338);
		}
	}
	Trace("publish EmissiveLerpFix");
	// EmissiveLerpFix: 2 slots
	if (ctx.Option("EmissiveLerpFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1"); pattern.size() > 0)
		{
			zpatch_emissive_const = MatchAddress(pattern, -16);
			zpatch_emissive_resume = MatchAddress(pattern, -12);
		}
	}
	Trace("publish EpisodeSwitchFreezeFix");
	// EpisodeSwitchFreezeFix: 3 slots
	if (ctx.Option("EpisodeSwitchFreezeFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84"); pattern.size() > 0)
		{
			zpatch_episode_switch_resume_a = MatchAddress(pattern, 5);
		}

		if (auto pattern = Pattern(image, "8B 4B 10 3B 4B 14 7D 0F"); pattern.size() > 0)
		{
			zpatch_episode_switch_resume_b = MatchAddress(pattern, 6);
		}

		if (auto pattern = Pattern(image, "56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05"); pattern.size() > 0)
		{
			zpatch_episode_switch_resume_c = MatchAddress(pattern, 7);
		}
	}
	Trace("publish FreezeGunFix");
	// FreezeGunFix: 1 slot
	if (ctx.Option("FreezeGunFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF"); pattern.size() > 0)
		{
			zpatch_freeze_gun_resume = MatchAddress(pattern, -12);
		}
	}
	Trace("publish GroupHackFix");
	// GroupHackFix: 1 slot
	if (ctx.Option("GroupHackFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04"); pattern.size() > 0)
		{
			zpatch_group_hack_resume = MatchAddress(pattern, 11);
		}
	}
	Trace("publish HighFPSBikePhysicsFix");
	// HighFPSBikePhysicsFix: 1 slot
	if (ctx.Option("HighFPSBikePhysicsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00"); pattern.size() > 0)
		{
			zpatch_bike_resume = MatchAddress(pattern, 9);
		}
	}
	Trace("publish LoadDLCs");
	// LoadDLCs: 9 rvas
	if (ctx.Option("LoadDLCs", "1"))
	{
		if (ctx.build == GameBuild::v1060)
		{
			zpatch_dlc_load_callback = static_cast<std::uint32_t>(kDlcLoadCallback_1070.site(ctx.gameBase));
			DlcRegisterRva() = kDlcRegister_1070.value;
			DlcApplyRva() = kDlcApply_1070.value;
		}

		if (ctx.build == GameBuild::v1070)
		{
			zpatch_dlc_load_callback = static_cast<std::uint32_t>(kDlcLoadCallback_1060.site(ctx.gameBase));
			DlcRegisterRva() = kDlcRegister_1060.value;
			DlcApplyRva() = kDlcApply_1060.value;
		}

		if (ctx.build == GameBuild::v1080)
		{
			zpatch_dlc_load_callback = static_cast<std::uint32_t>(kDlcLoadCallback_1080.site(ctx.gameBase));
			DlcApplyRva() = kDlcApply_1080.value;
			DlcRegisterRva() = kDlcRegister_1080.value;
		}
	}
	Trace("publish LocalSettingsPath");
	// LocalSettingsPath: 7 slots
	if (ctx.Option("LocalSettingsPath", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3"); pattern.size() > 0)
		{
			zpatch_local_path_1c = *pattern.get(0).get<std::uint32_t>(-178);
			zpatch_local_path_10 = *pattern.get(0).get<std::uint32_t>(-190);
			zpatch_local_episode_ptr = *pattern.get(0).get<std::uint32_t>(-151);
			zpatch_local_path_18 = *pattern.get(0).get<std::uint32_t>(-444);
			zpatch_local_path_08 = *pattern.get(0).get<std::uint32_t>(-432);
		}

		if (auto pattern = Pattern(image, "7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3"); pattern.size() > 0)
		{
			zpatch_local_path_18 = *pattern.get(0).get<std::uint32_t>(-160);
			zpatch_local_path_1c = *pattern.get(0).get<std::uint32_t>(-55);
		}
	}
	Trace("publish NewFilesCompatibility");
	// NewFilesCompatibility: 1 slot
	if (ctx.Option("NewFilesCompatibility", "1") && !NewFilesConflictModulesPresent())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 64 28 C8 67 E8"); pattern.size() > 0)
		{
			zpatch_newfiles_register_slot = MatchTarget(pattern, 6, 10);
		}
	}
	Trace("publish NewMemorySystem");
	// NewMemorySystem: 1 slot
	if (ctx.Option("NewMemorySystem", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "6A 01 B9 00 00 40 06"); pattern.size() > 0)
		{
			zpatch_nms_object = *pattern.get(0).get<std::uint32_t>(8);
		}
	}
	Trace("publish NewSavesCompatibility");
	// NewSavesCompatibility: 3 slots
	if (ctx.Option("NewSavesCompatibility", "1") && Pattern(ctx.image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08").size() > 0)
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08"); pattern.size() > 0)
		{
			zpatch_new_saves_src = *pattern.get(0).get<std::uint32_t>(1);
		}

		if (auto pattern = Pattern(image, "89 84 24 E0 01 00 00 8B 45 08 53 33 DB"); pattern.size() > 0)
		{
			zpatch_new_saves_resume = MatchAddress(pattern, 13);
		}
	}
	Trace("publish NoLiveryLimit");
	// NoLiveryLimit: 5 slots
	if (ctx.Option("NoLiveryLimit", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
		{
			zpatch_nolivery_fn = MatchTarget(pattern, -6, -2);
			zpatch_nolivery_resume_d = MatchAddress(pattern, 90);
		}

		if (auto pattern = Pattern(image, "8B 8C 83 3C 01 00 00 0F BF 53 48"); pattern.size() > 0)
		{
			zpatch_nolivery_resume = MatchAddress(pattern, 13);
		}

		if (auto pattern = Pattern(image, "8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
		{
			zpatch_nolivery_fn = MatchTarget(pattern, -8, -4);
			zpatch_nolivery_resume_d = MatchAddress(pattern, 84);
		}
	}
	Trace("publish PathNodeCrashFix");
	// PathNodeCrashFix: 2 slots
	if (ctx.Option("PathNodeCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 04 B1 0F B7 D0 81 FA FF FF 00 00"); pattern.size() > 0)
		{
			zpatch_path_node_resume_ok = MatchAddress(pattern);
			zpatch_path_node_resume_fail = MatchAddress(pattern, 59);
		}
	}
	Trace("publish PedModelsInCutscenesFix");
	// PedModelsInCutscenesFix: 1 slot
	if (ctx.Option("PedModelsInCutscenesFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"); pattern.size() > 0)
		{
			zpatch_model_lookup = MatchAddress(pattern);
		}
	}
	Trace("publish PlaneCrashFix");
	// PlaneCrashFix: 2 slots
	if (ctx.Option("PlaneCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00"); pattern.size() > 0)
		{
			zpatch_plane_crash_resume_b = MatchAddress(pattern, 14);
			zpatch_plane_crash_resume_a = MatchAddress(pattern, -705);
		}
	}
	Trace("publish PlayerAsPedComponentsFix");
	// PlayerAsPedComponentsFix: 2 slots
	if (ctx.Option("PlayerAsPedComponentsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 54 24 10 52 0F B6 90 19 02 00 00"); pattern.size() > 0)
		{
			zpatch_player_ped_resume = MatchAddress(pattern, 14);
		}

		if (auto pattern = Pattern(image, "8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"); pattern.size() > 0)
		{
			zpatch_model_lookup = MatchAddress(pattern);
		}
	}
	Trace("publish PopupHeadlightsFlag");
	// PopupHeadlightsFlag: 5 slots
	if (ctx.Option("PopupHeadlightsFlag", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1"); pattern.size() > 0)
		{
			zpatch_popup_resume_b = MatchAddress(pattern, 10);
			zpatch_popup_resume_a = MatchAddress(pattern, 12);
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			zpatch_ide_hook_slot = MatchAddress(pattern, -1);
		}

		if (auto pattern = Pattern(image, "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1"); pattern.size() > 0)
		{
			zpatch_popup_resume_b = MatchAddress(pattern, 10);
			zpatch_popup_resume_a = MatchAddress(pattern, 12);
		}
	}
	Trace("publish SMPA60Fix");
	// SMPA60Fix: 3 slots
	if (ctx.Option("SMPA60Fix", "1") && Pattern(ctx.image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C").size() > 0)
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 48 70 83 C0 40 50 51 8D 51 10"); pattern.size() > 0)
		{
			zpatch_smpa60_resume_a = MatchAddress(pattern, 28);
			zpatch_smpa60_resume_b = MatchAddress(pattern, 14);
		}

		if (auto pattern = Pattern(image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C"); pattern.size() > 0)
		{
			zpatch_smpa60_resume_c = MatchAddress(pattern, 136);
		}
	}
	Trace("publish SkipIntro");
	// SkipIntro: 1 slot
	if (ctx.Option("SkipIntro", "1") && !FusionFixGateClosed())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50"); pattern.size() > 0)
		{
			zpatch_skip_intro_match = MatchAddress(pattern);
		}
	}
	Trace("publish SkipMenu");
	// SkipMenu: 3 slots
	if (ctx.Option("SkipMenu", "0") && !FusionFixGateClosed())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E"); pattern.size() > 0)
		{
			zpatch_skip_menu_resume = MatchTarget(pattern, 24, 28);
		}

		if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3"); pattern.size() > 0)
		{
			zpatch_skip_menu_resume = MatchTarget(pattern, 3, 7);
		}

		if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3"); pattern.size() > 0)
		{
			zpatch_skip_menu_resume = MatchTarget(pattern, 3, 7);
		}
	}
	Trace("publish SpeedupBugFix");
	// SpeedupBugFix: 8 slots
	if (ctx.Option("SpeedupBugFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "50 64 89 25 00 00 00 00 83 EC 34"); pattern.size() > 0)
		{
			zpatch_speedup_t50 = *pattern.get(0).get<std::uint32_t>(26);
			zpatch_speedup_t64 = *pattern.get(0).get<std::uint32_t>(165);
			zpatch_speedup_t60 = *pattern.get(0).get<std::uint32_t>(188);
			zpatch_speedup_t5c = *pattern.get(0).get<std::uint32_t>(226);
			zpatch_speedup_t68 = *pattern.get(0).get<std::uint32_t>(234);
			zpatch_speedup_t4c = *pattern.get(0).get<std::uint32_t>(249);
			zpatch_speedup_t54 = *pattern.get(0).get<std::uint32_t>(264);
			zpatch_speedup_t58 = *pattern.get(0).get<std::uint32_t>(276);
		}
	}
	Trace("publish WantedLevelCrashFix");
	// WantedLevelCrashFix: 1 slot
	if (ctx.Option("WantedLevelCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00"); pattern.size() > 0)
		{
			zpatch_wanted_level_resume = MatchAddress(pattern, 12);
		}
	}
	Trace("publish SuperLODFix");
	// SuperLODFix: 6 slots
	if (ctx.Option("SuperLODFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (ctx.build == GameBuild::v1070)
		{
			zpatch_super_lod_fail = AddressOf(kSuperLodFail_1080.site_ptr(image.base));
			zpatch_super_lod_ok = AddressOf(kSuperLodOk_1080.site_ptr(image.base));
			zpatch_super_lod_lookup = AddressOf(kSuperLodLookup_1080.site_ptr(image.base));
		}

		if (ctx.build == GameBuild::v1080)
		{
			zpatch_super_lod_fail = AddressOf(kSuperLodFail_1070.site_ptr(image.base));
			zpatch_super_lod_ok = AddressOf(kSuperLodOk_1070.site_ptr(image.base));
			zpatch_super_lod_lookup = AddressOf(kSuperLodLookup_1070.site_ptr(image.base));
		}
	}
	Trace("publish DrivebyFOVFix");
	// DrivebyFOVFix: 2 slots
	if (ctx.Option("DrivebyFOVFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 10 46 60 F3 0F 5C CA"); pattern.size() > 0)
		{
			zpatch_driveby_resume = MatchAddress(pattern, 5);
		}

		if (auto pattern = Pattern(image, "08 0B C2 8B F0 E8"); pattern.size() > 0)
		{
			zpatch_driveby_resolver = MatchTarget(pattern, 6, 10);
		}
	}
	Trace("publish CrashMsgFix");
	// CrashMsgFix: 1 slot
	if (ctx.Option("CrashMsgFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 6A 10 4E 31"); pattern.size() > 0)
		{
			const auto site = static_cast<std::uintptr_t>(*pattern.get(0).get<std::uint32_t>(-4));
			if (InImage(image, site))
				zpatch_crash_msg_getter = static_cast<std::uint32_t>(
				            site + ReadDword(reinterpret_cast<const std::uint8_t*>(site) + 1) + 5);
		}
	}
	Trace("publish BenchmarkFix");
	// BenchmarkFix: 2 slots. The handler compares the build id before it reads
	// the INI (cmp 9 / cmp 0Dh), so 1.0.8.0 and 1.1.3.0 publish nothing either.
	if (ctx.Option("BenchmarkFix", "1") &&
	    !BuildIs(ctx.build, {GameBuild::v1080, GameBuild::v1130}))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20"); pattern.size() > 0)
		{
			zpatch_benchmark_resume_a = MatchAddress(pattern, 13);
			zpatch_benchmark_resume_b = MatchAddress(pattern, 13);
		}
	}
	Trace("publish CarPartsShadowFix");
	// CarPartsShadowFix: 2 slots
	if (ctx.Option("CarPartsShadowFix", "1") || ctx.Option("CarDynamicShadowFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "85 DB 7E 41 55 EB 0A"); pattern.size() > 0)
		{
			zpatch_carparts_resume_if = MatchAddress(pattern, 2);
			zpatch_carparts_resume_else = MatchAddress(pattern, 69);
		}
	}
	Trace("publish ForceCarHeadlightShadows");
	// ForceCarHeadlightShadows: 1 slot
	if (ctx.Option("ForceCarHeadlightShadows", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B"); pattern.size() > 0)
		{
			zpatch_fchs_resume = MatchAddress(pattern, 12);
		}
	}
	Trace("publish Patch4RemoveModChecks");
	// Patch4RemoveModChecks: 1 slot
	if (ctx.Option("Patch4RemoveModChecks", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08"); pattern.size() > 0)
		{
			zpatch_gfwl_resume = MatchAddress(pattern, 12);
		}
	}
	Trace("publish FreezeCarFix");
	// FreezeCarFix: 12 slots
	if (ctx.Option("FreezeCarFix", "1") &&
	    Pattern(ctx.image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0").size() > 0)
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0"); pattern.size() > 0)
		{
			zpatch_spawns_orig_a = MatchTarget(pattern, 8, 12);
		}

		if (auto pattern = Pattern(image, "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B"); pattern.size() > 0)
		{
			zpatch_spawns_orig_b = MatchTarget(pattern, 12, 16);
		}

		if (auto pattern = Pattern(image, "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00"); pattern.size() > 0)
		{
			zpatch_spawns_case9_resume = MatchTarget(pattern, 247, 251);
			zpatch_spawns_case6_resume = MatchTarget(pattern, 181, 185);
			zpatch_spawns_case1_resume = MatchTarget(pattern, 62, 66);
			zpatch_spawns_case2_resume = MatchTarget(pattern, 87, 91);
			zpatch_spawns_case4_resume = MatchTarget(pattern, 137, 141);
			zpatch_spawns_case7_resume = MatchTarget(pattern, 203, 207);
			zpatch_spawns_case3_resume = MatchTarget(pattern, 112, 116);
			zpatch_spawns_case5_resume = MatchTarget(pattern, 159, 163);
			zpatch_spawns_case8_resume = MatchTarget(pattern, 225, 229);
			zpatch_spawns_case0_resume = MatchTarget(pattern, 37, 41);
		}
	}
	Trace("publish CarDynamicShadowFix");
	// CarDynamicShadowFix: 5 slots
	if (ctx.Option("CarDynamicShadowFix", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
		{
			zpatch_carparts_resume_2 = MatchAddress(pattern, 11);
			zpatch_carparts_shadow_fn = MatchTarget(pattern, 7, 11);
		}

		if (auto pattern = Pattern(image, "56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
		{
			zpatch_carparts_resume_2 = MatchAddress(pattern, 13);
			zpatch_carparts_shadow_fn = MatchTarget(pattern, 9, 13);
		}

		if (auto pattern = Pattern(image, "8B 45 10 8B 16 8B 92 8C 00 00 00"); pattern.size() > 0)
		{
			zpatch_car_dynamic_d = MatchAddress(pattern);
		}
	}
	Trace("publish GarageModelFix");
	// GarageModelFix: 2 slots
	if (ctx.Option("GarageModelFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 57 20 D9 42 34 D9 5E 04 8B 47 20"); pattern.size() > 0)
		{
			zpatch_garage_resume_a = MatchAddress(pattern, 6);
		}

		if (auto pattern = Pattern(image, "0F B7 0E 50 51 E8"); pattern.size() > 0)
		{
			zpatch_garage_resume_b = MatchAddress(pattern, 5);
		}
	}
	Trace("publish MovingSteeringWheelSupport");
	// MovingSteeringWheelSupport: 5 slots
	if (ctx.Option("MovingSteeringWheelSupport", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 423, 427);
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 140, 144);
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			zpatch_ide_hook_slot = MatchAddress(pattern, -1);
		}
	}
	Trace("publish ShakingEngineBlock");
	// ShakingEngineBlock: 5 slots
	if (ctx.Option("ShakingEngineBlock", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 423, 427);
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 140, 144);
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			zpatch_ide_hook_slot = MatchAddress(pattern, -1);
		}
	}
	Trace("publish MovingPlaneParts");
	// MovingPlaneParts: 5 slots
	if (ctx.Option("MovingPlaneParts", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 423, 427);
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			zpatch_steer_resume = MatchAddress(pattern, 16);
			zpatch_steer_vtable_fn = MatchTarget(pattern, 140, 144);
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			zpatch_ide_hook_slot = MatchAddress(pattern, -1);
		}
	}
	Trace("publish MouseFix");
	// MouseFix: 6 slots
	if (ctx.Option("MouseFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C"); pattern.size() > 0)
		{
			zpatch_mf_x = *pattern.get(0).get<std::uint32_t>(13);
			zpatch_mf_y = *pattern.get(0).get<std::uint32_t>(20);
		}

		if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D"); pattern.size() > 0)
		{
			zpatch_mf_flag = *pattern.get(0).get<std::uint32_t>(20);
		}

		if (auto pattern = Pattern(image, "83 EC 08 DB 05 ? ? ? ? 68"); pattern.size() > 0)
		{
			zpatch_mf_time = MatchAddress(pattern);
		}

		if (auto pattern = Pattern(image, "53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46"); pattern.size() > 0)
		{
			zpatch_mf_resume_a = MatchAddress(pattern);
			zpatch_mf_resume_b = MatchAddress(pattern, 1878);
		}
	}
	Trace("publish OutOfCommissionFix");
	// OutOfCommissionFix: 3 slots
	if (ctx.Option("OutOfCommissionFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "A9 39 44 24 18"); pattern.size() > 0)
		{
			zpatch_ooc_resume = MatchAddress(pattern, 17);
		}

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56"); pattern.size() > 0)
		{
			zpatch_ooc_logger = MatchTarget(pattern, -50, -46);
			zpatch_ooc_orig = MatchTarget(pattern, 1, 5);
		}
	}
	Trace("publish HighQualityReflections");
	// HighQualityReflections: 2 slots
	if (ctx.Option("HighQualityReflections", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8"); pattern.size() > 0)
		{
			zpatch_hqr_resume = MatchAddress(pattern, 19);
			zpatch_hqr_builder = MatchTarget(pattern, 15, 19);
		}
	}
	Trace("publish CutsceneFixes");
	// CutsceneFixes: 2 slots
	if (ctx.Option("CutsceneFixes", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8"); pattern.size() > 0)
		{
			zpatch_cutscene_target = MatchAddress(pattern) + *pattern.get(0).get<std::uint32_t>(-4);
			zpatch_cutscene_poke = MatchTarget(pattern, -4, 79);
		}
	}
	Trace("publish RemoveGFWLUpdatePopup");
	// RemoveGFWLUpdatePopup: 1 slot
	if (ctx.Option("RemoveGFWLUpdatePopup", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08"); pattern.size() > 0)
		{
			zpatch_gfwl_resume = MatchAddress(pattern, 12);
		}
	}

	Trace("AimProjectileCrashFix");
	// AimProjectileCrashFix: 1 sigjump
	if (ctx.Option("AimProjectileCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 D8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_aim_projectile_crash_fix), true); //jmp
		}
	}
	Trace("AllowHashesForIDE");
	// AllowHashesForIDE: 1 sigjump
	if (ctx.Option("AllowHashesForIDE", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 4C 24 08 56 8B 74 24 08 80 3E 22"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_game_hash), true); //jmp
		}
	}
	Trace("BadPlayerModelCrashFix");
	// BadPlayerModelCrashFix: 1 siginstall, 1 sigjump
	if (ctx.Option("BadPlayerModelCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_bad_player_model), true); //jmp
		}
	}
	Trace("BetterMPSync");
	// BetterMPSync: 1 siginstall
	if (ctx.Option("BetterMPSync", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 90 E8 00 00 00 55 8B CE FF"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 6, reinterpret_cast<void*>(&abi_better_mp_sync), true); //call
		}
	}
	Trace("BikeFeetFix");
	// BikeFeetFix: 1 literal
	if (ctx.Option("BikeFeetFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "39 82 54 13 00 00 0F 85 94 04 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 12, 12, true); //nop
		}
	}
	Trace("BikePhoneAnimsFix");
	// BikePhoneAnimsFix: 1 literal
	if (ctx.Option("BikePhoneAnimsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 33, 6, true); //nop
		}
	}
	Trace("BlackscreenFix");
	// BlackscreenFix: 1 sigjump
	if (ctx.Option("BlackscreenFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "75 54 8B 48 28 8B 40 2C"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 81, reinterpret_cast<void*>(&abi_blackscreen_shim), true); //jmp
		}
	}
	Trace("BuildingAlphaFix");
	// BuildingAlphaFix: 1 literal, 1 siginstall
	if (ctx.Option("BuildingAlphaFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F7 46 24 00 00 00 40 0F 84"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			if (InImage(image, reinterpret_cast<std::uintptr_t>(match + 7)))
				injector::MakeJMP(match + 7, reinterpret_cast<void*>(BranchTarget(match, 7)), true); //jmp
		}

		if (auto pattern = Pattern(image, "F7 46 24 00 00 00 40 0F 85"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match, 13, true); //nop
		}
	}
	Trace("BuildingDynamicShadows");
	// BuildingDynamicShadows: 1 sigjump, 1 sigwrite
	if (ctx.Option("BuildingDynamicShadows", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "50 68 04 01 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(2), kZeroByte, true); //patch
			}
		}

		if (auto pattern = Pattern(image, "8B 47 28 C1 E8 06 83 E0 0F 83 C0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_building_dynamic_shadows), true); //jmp
		}
	}
	// RemoveUselessChecks: the original handler applies its singles and its RVA
	// tables in one go, as the loader's 27th option read (right after
	// BuildingDynamicShadows). The port's arm list is alphabetical, so this is
	// the nearest equivalent slot. The early position is load-bearing: the tables
	// overwrite one fixed byte of MiscFixes' and RestoreDeathMusic's signatures,
	// so those two later installs are suppressed exactly as in the original.
	Trace("RemoveUselessChecks");
	if (ctx.Option("RemoveUselessChecks", "0"))
		zp::ApplyRemoveUselessChecks(ctx.build, ctx.image);
	Trace("CollisionDeadlockFix");
	// CollisionDeadlockFix: 1 sigjump
	if (ctx.Option("CollisionDeadlockFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_collision_deadlock_walk), true); //jmp
		}
	}
	Trace("CombinedRadioFreezeFix");
	// CombinedRadioFreezeFix: 1 siginstall
	if (ctx.Option("CombinedRadioFreezeFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match, reinterpret_cast<void*>(&abi_combined_radio_freeze_fix), true); //call
		}
	}
	Trace("CopSirensFlag");
	// CopSirensFlag: 3 siginstalls, 2 sigjumps
	if (ctx.Option("CopSirensFlag", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_copsirens_a), true); //jmp
		}

		if (auto pattern = Pattern(image, "0F BF 41 2E 39 05"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 22, reinterpret_cast<void*>(&abi_copsirens_b), true); //jmp
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 10, reinterpret_cast<void*>(&abi_copsirens_ide), true); //jmp
		}
	}
	Trace("DualVehicleHeadlights");
	// DualVehicleHeadlights
	if (ctx.Option("DualVehicleHeadlights", "1"))
	{
	}
	Trace("EmissiveLerpFix");
	// EmissiveLerpFix: 1 siginstall
	if (ctx.Option("EmissiveLerpFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 24, reinterpret_cast<void*>(&abi_emissive_lerp_shim), true); //jmp
		}
	}
	Trace("EpisodeSwitchFreezeFix");
	// EpisodeSwitchFreezeFix: 5 sigjumps
	if (ctx.Option("EpisodeSwitchFreezeFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_episode_switch_shim_c), true); //jmp
		}

		if (auto pattern = Pattern(image, "89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_episode_switch_store_shim), true); //jmp
		}

		if (auto pattern = Pattern(image, "8B 41 10 03 41 0C C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_episode_switch_sum), true); //jmp
		}

		if (auto pattern = Pattern(image, "8B 4B 10 3B 4B 14 7D 0F"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_episode_switch_shim_b), true); //jmp
		}

		if (auto pattern = Pattern(image, "8B C1 8B 08 8B 11 8B 40 04 8B 52 30 50 FF D2 C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_episode_switch_vcall), true); //jmp
		}
	}
	Trace("EpisodicVehicleSupport");
	// EpisodicVehicleSupport: 23 literals, 4 rvas, 4 sigwrites
	if (ctx.Option("EpisodicVehicleSupport", "1"))
	{
		const GameImage& image = ctx.image;

		// The original wrote these four slots by RVA (0x021500/0x7D91AE on 1.0.7.0,
		// 0x095040/0x684ABE on 1.0.8.0). The recovered signatures are unique on
		// every gated build (research 08), so scan instead: same site, same bytes.
		if (ctx.build == GameBuild::v1070)
		{
			if (auto pattern = Pattern(image, "44 80 0F ?? 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint32_t>(pattern.get(0).get<std::uint8_t>(),
				                                     AddressOf(&zpatch_episode_flag), true); //patch
			}
			if (auto pattern = Pattern(image, "44 80 0F ?? 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint32_t>(pattern.get(0).get<std::uint8_t>(),
				                                     AddressOf(&zpatch_episode_flag), true); //patch
			}
		}

		if (ctx.build == GameBuild::v1080)
		{
			if (auto pattern = Pattern(image, "54 D7 12 01 02 75 ?? E8 ?? ?? ?? ?? 3B C3 74 ?? 8D"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint32_t>(pattern.get(0).get<std::uint8_t>(),
				                                     AddressOf(&zpatch_episode_flag), true); //patch
			}
			if (auto pattern = Pattern(image, "54 D7 12 01 02 0F 85 94 00 ?? ?? ?? ?? 24 ?? 85 D2 0F 84 ?? ?? ?? ?? 8B"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint32_t>(pattern.get(0).get<std::uint8_t>(),
				                                     AddressOf(&zpatch_episode_flag), true); //patch
			}
		}

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
		             GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
		             GameBuild::v1130, GameBuild::v12043}))
		{

			{
				if (auto pattern = Pattern(image, "7C 18 0F BF 4E 2E"); pattern.size() > 0)
				{
					injector::MakeNOP(pattern.get(0).get<std::uint8_t>() - 6, 8, true); //nop
					if (pattern.size() > 1)
						injector::MakeNOP(pattern.get(1).get<std::uint8_t>() - 6, 8, true); //nop
				}
			}

			{
				if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 08"); pattern.size() > 0)
				{
					injector::MakeNOP(pattern.get(0).get<std::uint8_t>(), 9, true); //nop
					if (pattern.size() > 1)
						injector::MakeNOP(pattern.get(1).get<std::uint8_t>(), 9, true); //nop
				}
			}

			if (auto pattern = Pattern(image, "39 35 ? ? ? ? 75 0E 80 3D"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 6, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 2, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "7C 22 83 F8 28 75 1D"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 8C 0E 05 00 00"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 13, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 08 3B 05"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 1F 8B 44 24 04"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 35 0F BF 46 2E"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 38 8B 16"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 11 8B 4E"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 1F 8B"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 35 0F BF 43 2E"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 9, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 43"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 13, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 89 44 24 38"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 11, 6, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 8A 0D"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 7, true); //nop
				injector::MakeNOP(match + 17, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 F3 0F 10 05 ? ? ? ? F3 0F 11 44 24 20 75 4C"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 21, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match - 15, 7, true); //nop
				injector::MakeNOP(match + 10, 2, true); //nop
			}
		}
	}
	Trace("EpisodicWeaponSupport");
	// EpisodicWeaponSupport: 8 literals
	if (ctx.Option("EpisodicWeaponSupport", "1"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
		             GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
		             GameBuild::v1130, GameBuild::v12043}))
		{

			if (auto pattern = Pattern(image, "75 5B 83 3D ? ? ? ? 00 75"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 85 8C 01 00 00"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 6, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 0F 8C D5 00"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match, 13, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 20 8B CD E8"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 75 50"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 21 8B 44 24 0C"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 32 8B 41"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}
		}
	}
	Trace("ForceDynamicShadowsEverywhere");
	// ForceDynamicShadowsEverywhere: 1 literal
	if (ctx.Option("ForceDynamicShadowsEverywhere", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F7 46 4C 00 80 00 00 74"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 7, 2, true); //nop
		}
	}
	Trace("ForceShadowsOnObjects");
	// ForceShadowsOnObjects: 1 literal
	if (ctx.Option("ForceShadowsOnObjects", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F7 47 40 00 10 00 00 74 07 81 4E 24 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 7, kShortJmpOpcode, true); //jmp
		}
	}
	Trace("FreezeGunFix");
	// FreezeGunFix: 1 siginstall
	if (ctx.Option("FreezeGunFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 21, reinterpret_cast<void*>(&abi_freeze_gun_shim), true); //jmp
		}
	}
	Trace("GroupHackFix");
	// GroupHackFix: 1 sigjump
	if (ctx.Option("GroupHackFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_group_hack_shim), true); //jmp
		}
	}
	Trace("HighFPSBikePhysicsFix");
	// HighFPSBikePhysicsFix: 1 sigjump
	if (ctx.Option("HighFPSBikePhysicsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_bike_physics_detour), true); //jmp
		}
	}
	Trace("HighFPSSpeedupFix");
	// HighFPSSpeedupFix: 9 literals
	if (ctx.Option("HighFPSSpeedupFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F 2F C1 76 03 0F 28 C8 F3 0F 10 15"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match - 7, 2, true); //nop
			injector::MakeNOP(match + 3, 2, true); //nop
			injector::WriteMemory<std::uint8_t>(match + 224, kShortJmpOpcode, true); //jmp
			injector::MakeNOP(match + 231, 2, true); //nop
			injector::MakeNOP(match + 367, 2, true); //nop
			injector::MakeNOP(match + 377, 2, true); //nop
			injector::MakeNOP(match + 607, 2, true); //nop
			injector::MakeNOP(match + 614, 2, true); //nop
			injector::MakeNOP(match + 764, 2, true); //nop
		}
	}
	Trace("ImprovedShaderStreaming");
	// ImprovedShaderStreaming: 1 siginstall
	if (ctx.Option("ImprovedShaderStreaming", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 85,
			                   reinterpret_cast<void*>(&abi_improved_shader_streaming_find_hpe), true); //call
			injector::MakeJMP(match + 98, reinterpret_cast<void*>(match + 245), true); //jmp
			injector::MakeCALL(match + 253,
			                   reinterpret_cast<void*>(&abi_improved_shader_streaming_find_v), true); //call
			injector::MakeJMP(match + 266, reinterpret_cast<void*>(match + 1158), true); //jmp
		}
	}
	Trace("InfiniteCarBottomDistance");
	// InfiniteCarBottomDistance: 1 sigwrite
	if (ctx.Option("InfiniteCarBottomDistance", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "0F 2F D0 76 25 0F 28 C2"); pattern.size() > 0)
			{
				// `jmp +0x2A` written as its two bytes (there is no short-jump API).
				auto* site = pattern.get(0).get<std::uint8_t>(3);
				injector::WriteMemory<std::uint8_t>(site, kShortJmpOpcode, true);     //jmp
				injector::WriteMemory<std::uint8_t>(site + 1, kCutsceneJmpDisp, true); // +0x2A
			}
		}
	}
	Trace("LoadDLCs");
	// LoadDLCs: 18 rvas, 6 rvajumps
	if (ctx.Option("LoadDLCs", "1"))
	{
		const GameImage& image = ctx.image;

		if (ctx.build == GameBuild::v1060)
		{
			// The original patched these LoadDLCs call/jump sites by RVA. Each
			// signature below is the research-08 candidate that verifies as exactly
			// one hit at its site on this build; the gate is unchanged. Sites are
			// scanned in ascending order, before any of them is written.
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 8C 24 ?? ?? ?? ?? ?? ?? ?? ?? ?? 52"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4137CD
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
				// the original patches this site twice; keep the duplicate write
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 6A ?? 8D 44 24 ?? 50 53 56 8D 4C 24"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x41445C
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpA_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? ?? E8"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4144D4
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
				// duplicate write kept
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
			}
			if (auto p = Pattern(image, "80 BE ?? ?? ?? ?? ?? 74 ?? 8B 44 24 ?? 50 E8 ?? ?? ?? ?? 83 C4 ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x41450E
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpB_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x41453A
				injector::MakeCALL(site, reinterpret_cast<void*>(kLoadDlcsCall_1070.site(ctx.gameBase)), true); //call
			}
			if (auto p = Pattern(image, "8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? B0 ?? 5E C2 ?? ?? CC CC CC CC"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x41453F
				injector::MakeNOP(site, 12, true); //nop
			}
		}

		if (ctx.build == GameBuild::v1070)
		{
			// The original patched these LoadDLCs call/jump sites by RVA. Each
			// signature below is the research-08 candidate that verifies as exactly
			// one hit at its site on this build; the gate is unchanged. Sites are
			// scanned in ascending order, before any of them is written.
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 8C 24 ?? ?? ?? ?? ?? ?? ?? ?? ?? 52"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x41356D
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
				// the original patches this site twice; keep the duplicate write
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 6A ?? 8D 44 24 ?? 50 53 56 8D 4C 24"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4141FC
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpA_1060.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6 ?? 83 E8 ?? 75 ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x414274
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
				// duplicate write kept
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
			}
			if (auto p = Pattern(image, "80 BE ?? ?? ?? ?? ?? 74 ?? 8B 44 24 ?? 50 E8 ?? ?? ?? ?? 83 C4 ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4142AE
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpB_1060.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4142DA
				injector::MakeCALL(site, reinterpret_cast<void*>(kLoadDlcsCall_1060.site(ctx.gameBase)), true); //call
			}
			if (auto p = Pattern(image, "8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? B0 ?? 5E C2 ?? ?? CC CC CC CC"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4142DF
				injector::MakeNOP(site, 12, true); //nop
			}
		}

		if (ctx.build == GameBuild::v1080)
		{
			// The original patched these LoadDLCs call/jump sites by RVA. Each
			// signature below is the research-08 candidate that verifies as exactly
			// one hit at its site on this build; the gate is unchanged. Sites are
			// scanned in ascending order, before any of them is written.
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 84 C0 74 ?? 53 8D 4C 24 ?? 51 8D 94 24 ?? ?? ?? ?? 52"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x48F558
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
				// the original patches this site twice; keep the duplicate write
				injector::MakeCALL(site, reinterpret_cast<void*>(&abi_load_dlcs_dummy), true); //call
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 6A ?? 8D 44 24 ?? 50 53 56 8D 4C 24"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x49035C
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpA_1080.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "B8 ?? ?? ?? ?? 8D A4 24 ?? ?? ?? ?? 88 1E 83 C6"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x4903D4
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
				// duplicate write kept
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_load_dlcs_struct_fixup), true); //jmp
			}
			if (auto p = Pattern(image, "80 BE ?? ?? ?? ?? ?? 74 ?? 8B 44 24 ?? 50 E8 ?? ?? ?? ?? 83 C4 ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x49040E
				injector::MakeJMP(site, reinterpret_cast<void*>(kLoadDlcsJmpB_1080.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? ?? ?? 5E"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x49043A
				injector::MakeCALL(site, reinterpret_cast<void*>(kLoadDlcsCall_1080.site(ctx.gameBase)), true); //call
			}
			if (auto p = Pattern(image, "8B CE E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? B0 ?? 5E C2 ?? ?? CC CC CC CC"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x49043F
				injector::MakeNOP(site, 12, true); //nop
			}
		}
	}
	Trace("LocalSettingsPath");
	// LocalSettingsPath: 3 siginstalls
	if (ctx.Option("LocalSettingsPath", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "53 8B 5C 24 08 56 6A 00 6A 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(match + 16, AddressOf(&zpatch_local_saves_slot), true); //patch
			injector::WriteMemory<std::uint32_t>(match + 32, AddressOf(&zpatch_local_iv_suffix), true); //patch
		}

		if (auto pattern = Pattern(image, "7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 14, reinterpret_cast<void*>(&abi_local_settings_detour), true); //jmp
		}

		if (auto pattern = Pattern(image, "7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 13, reinterpret_cast<void*>(&abi_local_settings_fallback_detour), true); //jmp
		}
	}
	Trace("MPNikoCrashFix");
	// MPNikoCrashFix: 1 literal
	if (ctx.Option("MPNikoCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "74 2B 57 E8 ? ? ? ? 8B BE A8 0E 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kShortJmpOpcode, true); //jmp
		}
	}
	Trace("MiscFixes");
	// MiscFixes: 12 literals
	if (!zpatch_xliveless_present && ctx.Option("MiscFixes", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "74 05 E8 ? ? ? ? 6A 05 E8 ? ? ? ? 83 C4 04 B9"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "75 28 6A 00 6A 00 68 ? ? ? ? E8 ? ? ? ? 83 C4 0C"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "81 EC ? ? ? ? A1 ? ? ? ? 33 C4 89 84 24 ? ? ? ? 53 ? 6A 3C 33 DB 8D 44 24 ? 53 50 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kRetOpcode, true); //ret
		}

		if (auto pattern = Pattern(image, "83 C4 ? 3B 46 20"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 12, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "83 F8 01 75 30 A3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kMovEaxOpcode, true); //patch
			injector::WriteMemory<std::uint32_t>(match + 1, kTrueValue, true); //patch
		}

		if (auto pattern = Pattern(image, "8B 54 24 04 52 A3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			// `jmp +0xC9` written as its two bytes (there is no short-jump API).
			injector::WriteMemory<std::uint8_t>(match, kShortJmpOpcode, true);      //jmp
			injector::WriteMemory<std::uint8_t>(match + 1, kMiscJmpDispC9, true);  // +0xC9
		}

		if (auto pattern = Pattern(image, "C7 05 ? ? ? ? 03 00 00 00 33 C0 5E 59 C3 C7"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			// `jmp +0x0D` written as its two bytes (there is no short-jump API).
			injector::WriteMemory<std::uint8_t>(match, kShortJmpOpcode, true);      //jmp
			injector::WriteMemory<std::uint8_t>(match + 1, kMiscJmpDisp0D, true);  // +0x0D
		}

		if (auto pattern = Pattern(image, "C7 06 ? ? ? ? 74 19"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 6, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 85 C0 75 04 32"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 8, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 85 C0 75 07 A0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 8, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 85 C0 75 18"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 8, 2, true); //nop
		}
	}
	Trace("NewFilesCompatibility");
	// NewFilesCompatibility: 2 siginstalls, 1 sigjump
	if (ctx.Option("NewFilesCompatibility", "1") && !NewFilesConflictModulesPresent())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 7B FF FF FF"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match, reinterpret_cast<void*>(&abi_newfiles_update_a), true); //call
		}

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 6C FF FF FF"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match, reinterpret_cast<void*>(&abi_newfiles_update_b), true); //call
		}

		if (auto pattern = Pattern(image, "68 64 28 C8 67 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 28, reinterpret_cast<void*>(&abi_newfiles_register_body), true); //jmp
		}
	}
	Trace("NewMemorySystem");
	// NewMemorySystem: 1 siginstall
	if (ctx.Option("NewMemorySystem", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "3D 80 99 99 59 73 05"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 34, reinterpret_cast<void*>(&abi_nms_apply), true); //call
			injector::MakeJMP(match - 29, reinterpret_cast<void*>(match - 16), true); //jmp
		}
	}
	Trace("NewSavesCompatibility");
	// NewSavesCompatibility: 1 siginstall, 1 sigjump
	if (ctx.Option("NewSavesCompatibility", "1") && Pattern(ctx.image, "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08").size() > 0)
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "89 84 24 E0 01 00 00 8B 45 08 53 33 DB"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_new_saves_compat), true); //jmp
		}
	}
	Trace("NoLiveryLimit");
	// NoLiveryLimit: 1 siginstall, 3 sigjumps
	if (ctx.Option("NoLiveryLimit", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 8C 83 3C 01 00 00 0F BF 53 48"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 237, reinterpret_cast<void*>(&abi_nolivery_b), true); //jmp
			injector::MakeJMP(match - 199, reinterpret_cast<void*>(&abi_nolivery_c), true); //jmp
			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_nolivery_a), true); //jmp
		}

		if (auto pattern = Pattern(image, "74 52 8D B5 3C 01 00 00 83 FB 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_nolivery_d), true); //jmp
		}

		if (auto pattern = Pattern(image, "8D AF 3C 01 00 00 8D 54 24 20"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_nolivery_e), true); //jmp
		}
	}
	Trace("Patch6and7RemoveModChecks");
	// Patch6and7RemoveModChecks: 22 literals, 3 rvas
	if (ctx.Option("Patch6and7RemoveModChecks", "0"))
	{
		const GameImage& image = ctx.image;

		if (ctx.build == GameBuild::v1060)
		{
			// The original wrote these packed-entry-stub bytes by RVA. Each
			// signature below is the candidate research 08 verified as exactly one
			// hit at its site on this build, re-validated by research 11.
			if (auto p = Pattern(image, "01 00 00 00 51 89 5C 24 ?? E8 ?? ?? ?? ?? 85 C0 75 ?? 39 44 24 ?? 74"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x0038AC
				injector::WriteMemory<std::uint8_t>(site, kZeroByte, true); //patch: clear the entry-stub byte
			}
			if (auto p = Pattern(image, "74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? 83 C4 ?? 68"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x00392E
				injector::WriteMemory<std::uint8_t>(site, kShortJmpOpcode, true); //jmp
			}
			if (auto p = Pattern(image, "01 00 00 00 75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C ?? ?? E8"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x00397E
				injector::WriteMemory<std::uint8_t>(site, kZeroByte, true); //patch: clear the entry-stub byte
			}
			if (auto p = Pattern(image, "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x003982
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksEntryJmp_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x003A14
				injector::WriteMemory<std::uint8_t>(site, kShortJmpOpcode, true); //jmp
			}
			// The original NOPs the seven bytes at 0x42E7..0x42ED. One scan at the
			// instruction plus match+0..+6 is equivalent and order-independent; the
			// old per-byte windows relied on the ascending write order (research 11).
			if (auto p = Pattern(image, "C6 05 ?? ?? ?? ?? ?? 74 ?? 8B 40 ?? 56 6A ?? 6A ?? 50 83 CE ?? E8"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x0042E7..0x0042ED
				injector::MakeNOP(site, 7, true); //nop x7
			}
		}

		if (ctx.build == GameBuild::v1070)
		{
			// The original wrote these packed-entry-stub bytes by RVA. Each
			// signature below is the candidate research 08 verified as exactly one
			// hit at its site on this build, re-validated by research 11.
			if (auto p = Pattern(image, "01 00 00 00 51 89 5C 24 ?? E8 ?? ?? ?? ?? 85 C0 75 ?? 39 44 24 ?? 74"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x00388C
				injector::WriteMemory<std::uint8_t>(site, kZeroByte, true); //patch: clear the entry-stub byte
			}
			if (auto p = Pattern(image, "75 ?? 39 44 24 ?? 74 ?? 8A C3 5F 5E 5D 5B 83 C4 ?? C3 8D 54 24 ?? 52 8D"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x00389C
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksMidJmp_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "74 ?? 8B 45 ?? 53 6A ?? 50 E8 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? ?? ?? ?? 68"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x00390E
				injector::WriteMemory<std::uint8_t>(site, kShortJmpOpcode, true); //jmp
			}
			if (auto p = Pattern(image, "01 00 00 00 75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C ?? ?? E8"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x00395E
				injector::WriteMemory<std::uint8_t>(site, kZeroByte, true); //patch: clear the entry-stub byte
			}
			if (auto p = Pattern(image, "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x003962
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksCaseJmp_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "74 ?? 8B 4D ?? 57 6A ?? 51 E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? ?? 0F"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x0039F4
				injector::WriteMemory<std::uint8_t>(site, kShortJmpOpcode, true); //jmp
			}
			// The original NOPs the seven bytes at 0x42C7..0x42CD; one scan at the
			// instruction plus match+0..+6 replaces the seven per-byte windows
			// (research 11 §5).
			if (auto p = Pattern(image, "C6 05 ?? ?? ?? ?? ?? 74 ?? 8B 40 ?? 56 6A ?? 6A ?? 50 83 CE ?? E8"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x0042C7..0x0042CD
				injector::MakeNOP(site, 7, true); //nop x7
			}
		}

	}
	Trace("PathNodeCrashFix");
	// PathNodeCrashFix: 1 sigjump
	if (ctx.Option("PathNodeCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 04 B1 0F B7 D0 81 FA FF FF 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 53, reinterpret_cast<void*>(&abi_path_node_shim), true); //jmp
		}
	}
	Trace("PedModelsInCutscenesFix");
	// PedModelsInCutscenesFix: 1 sigjump
	if (ctx.Option("PedModelsInCutscenesFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "56 8B F1 E8 ? ? ? ? 85 C0 74 0E 66 8B 40 2E"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_ped_models_in_cutscenes_check), true); //jmp
		}
	}
	Trace("PipeBombDropIconSupport");
	// PipeBombDropIconSupport: 1 literal
	if (ctx.Option("PipeBombDropIconSupport", "1"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
		             GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
		             GameBuild::v1130, GameBuild::v12043}))
		{

			if (auto pattern = Pattern(image, "83 7D 18 1C 75 14"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match - 9, 9, true); //nop
			}
		}
	}
	Trace("PlaneCrashFix");
	// PlaneCrashFix: 1 sigjump
	if (ctx.Option("PlaneCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 3, reinterpret_cast<void*>(&abi_plane_crash_shim), true); //jmp
		}
	}
	Trace("PlayerAsPedComponentsFix");
	// PlayerAsPedComponentsFix: 1 sigjump
	if (ctx.Option("PlayerAsPedComponentsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 54 24 10 52 0F B6 90 19 02 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 5, reinterpret_cast<void*>(&abi_player_ped_components_shim), true); //jmp
		}
	}
	Trace("PoliceEpisodicWeaponSupport");
	// PoliceEpisodicWeaponSupport: 1 literal
	if (ctx.Option("PoliceEpisodicWeaponSupport", "0"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
		             GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
		             GameBuild::v1130, GameBuild::v12043}))
		{

			if (auto pattern = Pattern(image, "39 2D ? ? ? ? 75 3E"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 6, 2, true); //nop
			}
		}
	}
	Trace("PopupHeadlightsFlag");
	// PopupHeadlightsFlag: 3 siginstalls, 1 sigjump
	if (ctx.Option("PopupHeadlightsFlag", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_popup_headlights), true); //jmp
			injector::WriteMemory<std::uint8_t>(match + 10, kShortJmpOpcode, true); //jmp
			injector::WriteMemory<std::uint32_t>(match + 82, AddressOf(&zpatch_popup_angle), true); //patch
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 10, reinterpret_cast<void*>(&abi_copsirens_ide), true); //jmp
		}

		if (auto pattern = Pattern(image, "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_popup_headlights_edx), true); //jmp
			injector::WriteMemory<std::uint8_t>(match + 10, kShortJmpOpcode, true); //jmp
			injector::WriteMemory<std::uint32_t>(match + 82, AddressOf(&zpatch_popup_angle), true); //patch
		}
	}
	Trace("RemoveAllWeaponsProtection");
	// RemoveAllWeaponsProtection: 1 literal
	if (ctx.Option("RemoveAllWeaponsProtection", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "E9 ? ? ? ? 57 8D 8F ? ? 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 5, 12, true); //nop
		}
	}
	Trace("RemoveBoundingBoxCulling");
	// RemoveBoundingBoxCulling: 1 siginstall
	if (ctx.Option("RemoveBoundingBoxCulling", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "3C 01 57 0F 85"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			if (InImage(image, reinterpret_cast<std::uintptr_t>(match + 3)))
				injector::MakeJMP(match + 3, reinterpret_cast<void*>(BranchTarget(match, 3)), true); //jmp
		}
	}
	Trace("RemoveRGSCLoginRequirement");
	// RemoveRGSCLoginRequirement: 1 rva, 1 rvajump, 1 sigjump
	if (ctx.Option("RemoveRGSCLoginRequirement", "0"))
	{
		const GameImage& image = ctx.image;

		if (ctx.build == GameBuild::v1080)
		{
			// The original derives the site from 0x498460 and writes the same jump
			// twice (the duplicate write is kept). The recovered signature is unique
			// at that RVA on 1.0.8.0, so the scan replaces the address (research 11).
			if (auto pattern = Pattern(image, "8B 44 24 ?? 85 C0 74 ?? F3 0F 7E 05 ?? ?? ?? ?? 66 0F D6 00"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(); // 0x498460
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_rgsc_login_removed), true); //jmp
				// The original writes the same jump twice (duplicate-write rule).
				injector::MakeJMP(site, reinterpret_cast<void*>(&abi_rgsc_login_removed), true); //jmp
			}
		}

		if (auto pattern = Pattern(image, "83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_rgsc_login_removed), true); //jmp
		}
	}
	Trace("RemoveStaticCarShadows");
	// RemoveStaticCarShadows: 1 literal
	if (ctx.Option("RemoveStaticCarShadows", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 11 4C 24 0C 76 5B D9 45 14"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 6, kShortJmpOpcode, true); //jmp
		}
	}
	Trace("RemoveStaticCutsceneCarShadows");
	// RemoveStaticCutsceneCarShadows: 1 literal
	if (ctx.Option("RemoveStaticCutsceneCarShadows", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 11 44 24 0C 76 5B D9 45 14"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 6, kShortJmpOpcode, true); //jmp
		}
	}
	Trace("ReversingLightFix");
	// ReversingLightFix: 1 literal
	if (ctx.Option("ReversingLightFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 8, kPushadOpcode, true); //patch
		}
	}
	Trace("SCOSignatureFix");
	// SCOSignatureFix: 1 literal
	if (ctx.Option("SCOSignatureFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 6C 24 1C 66 89 6E 14 64 8B 0D 2C 00 00 00 8B"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match - 32, kShortJmpOpcode, true); //jmp
		}
	}
	Trace("SMPA60Fix");
	// SMPA60Fix: 1 literal, 1 siginstall, 1 sigjump
	if (ctx.Option("SMPA60Fix", "1") && Pattern(ctx.image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C").size() > 0)
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 123, reinterpret_cast<void*>(&abi_smpa60_tail_shim), true); //jmp
			injector::WriteMemory<std::uint8_t>(match - 38, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "8B 48 70 83 C0 40 50 51 8D 51 10"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_smpa60_shim), true); //jmp
		}
	}
	Trace("SavegameFix");
	// SavegameFix: 2 siginstalls, 1 sigwrite
	if (ctx.Option("SavegameFix", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "75 ? 83 C1 01 83 C0 01 83 F9 04"); pattern.size() > 0)
			{
				std::uint8_t* match = pattern.get(0).get<std::uint8_t>();
				injector::MakeCALL(match - 73, reinterpret_cast<void*>(&abi_savegame_fix_close), true); //call
				injector::MakeCALL(match - 143, reinterpret_cast<void*>(&abi_savegame_fix_copy), true); //call
				injector::MakeCALL(match - 176, reinterpret_cast<void*>(&abi_savegame_fix_copy), true); //call
				injector::MakeCALL(match - 280, reinterpret_cast<void*>(&abi_savegame_fix_mutex), true); //call
				injector::MakeCALL(match - 240, reinterpret_cast<void*>(&abi_savegame_fix_null), true); //call
				injector::MakeNOP(pattern.get(0).get<std::uint8_t>(), 2, true); //nop
			}
		}

		if (auto pattern = Pattern(image, "8D 4C 24 18 51 6A 00 6A 0C 57"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 86, reinterpret_cast<void*>(&abi_savegame_fix_mutex), true); //call
			injector::MakeCALL(match - 47, reinterpret_cast<void*>(&abi_savegame_fix_null), true); //call
			injector::MakeCALL(match + 10, reinterpret_cast<void*>(&abi_savegame_fix_copy), true); //call
			injector::MakeCALL(match + 90, reinterpret_cast<void*>(&abi_savegame_fix_copy), true); //call
			injector::MakeCALL(match + 128, reinterpret_cast<void*>(&abi_savegame_fix_close), true); //call
		}
	}
	Trace("SkipIntro");
	// SkipIntro: 1 siginstall
	if (ctx.Option("SkipIntro", "1") && !FusionFixGateClosed())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 6, reinterpret_cast<void*>(&abi_skip_intro_body), true); //jmp
		}
	}
	Trace("SkipLauncher");
	// SkipLauncher: 7 literals
	if (ctx.Option("SkipLauncher", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F 85 ? ? ? ? 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC C6"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match, 6, true); //nop
		}

		if (auto pattern = Pattern(image, "3B 4E 18 0F 85"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 3, 6, true); //nop
		}

		if (auto pattern = Pattern(image, "3B 56 18 0F 85"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 3, 6, true); //nop
		}

		if (auto pattern = Pattern(image, "74 C4 8B 07 3B 46 18 75 BD 68"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "74 EC 38 5E 06 74 E7 68"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "84 DB 0F 85 43 01 00 00 8D 4C 24 28 8B C1"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 223, kShortJmpOpcode, true); //jmp
		}

		if (auto pattern = Pattern(image, "8B 56 1C 3B 56 20 ? ? 6A 00 6A 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kXorEaxOpcode, true);       //xor
			injector::WriteMemory<std::uint8_t>(match + 1, kXorEaxEaxModrm, true); // eax,eax
		}
	}
	Trace("SkipMenu");
	// SkipMenu: 3 siginstalls
	if (ctx.Option("SkipMenu", "0") && !FusionFixGateClosed())
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 23, reinterpret_cast<void*>(&abi_skip_menu_body), true); //call
		}

		if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 2, reinterpret_cast<void*>(&abi_skip_menu_body), true); //call
		}

		if (auto pattern = Pattern(image, "6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 2, reinterpret_cast<void*>(&abi_skip_menu_body), true); //call
		}
	}
	Trace("SpeedupBugFix");
	// SpeedupBugFix: 1 siginstall
	if (ctx.Option("SpeedupBugFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "50 64 89 25 00 00 00 00 83 EC 34"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 21, reinterpret_cast<void*>(&abi_speedup_body), true); //jmp
		}
	}
	Trace("TBoGTHeliHeightLimit");
	// TBoGTHeliHeightLimit: 1 literal
	if (ctx.Option("TBoGTHeliHeightLimit", "0"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1080,
		             GameBuild::v1061, GameBuild::v1100, GameBuild::v1110, GameBuild::v1120,
		             GameBuild::v1130, GameBuild::v12043}))
		{

			if (auto pattern = Pattern(image, "83 3D ? ? ? ? 02 7C 0A F3 0F"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::MakeNOP(match + 7, 2, true); //nop
			}
		}
	}
	Trace("TeleportProtection");
	// TeleportProtection: 1 literal
	if (ctx.Option("TeleportProtection", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			// The original writes exactly three bytes here: C2 08 over the two
			// opcode bytes and 00 over the byte after the immediate. injector::
			// MakeRET is NOT equivalent: its header stores the opcode through a
			// 4-byte int store, so it also zeroes match+3 (0x83 on 1.0.8.0, the
			// first byte of the next instruction). Keep the three-byte form.
			injector::WriteMemory<std::uint16_t>(injector::raw_ptr(match), kRet8Imm, true); //ret 8
			injector::WriteMemory<std::uint8_t>(injector::raw_ptr(match + 2), kRet8High, true); //ret 8
		}
	}
	Trace("TryToSkipAllErrors");
	// TryToSkipAllErrors: 1 literal
	if (ctx.Option("TryToSkipAllErrors", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match, kRetOpcode, true); //ret
		}
	}
	Trace("VRAMFix");
	// VRAMFix: 1 sigjump
	if (ctx.Option("VRAMFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 44 24 10 D9 2C 24 83 C4 1C C3"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_vram_fix_shim), true); //jmp
		}
	}
	Trace("WantedLevelCrashFix");
	// WantedLevelCrashFix: 1 sigjump
	if (ctx.Option("WantedLevelCrashFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_wanted_level_shim), true); //jmp
		}
	}
	Trace("Patch7RemoveModChecks");
	// Patch7RemoveModChecks: 3 rvas
	if (ctx.Option("Patch7RemoveModChecks", "0"))
	{
		const GameImage& image = ctx.image;

		if (ctx.build == GameBuild::v1060)
		{
			// The original wrote these packed-entry-stub bytes by RVA. Each
			// signature below is the candidate research 08 verified as exactly one
			// hit at its site on this build; the scans run in ascending address
			// order so no candidate is read after an earlier site was overwritten.
			if (auto p = Pattern(image, "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x003982
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksEntryJmp_1070.site(ctx.gameBase)), true); //jmp
			}
		}

		if (ctx.build == GameBuild::v1070)
		{
			// The original wrote these packed-entry-stub bytes by RVA. Each
			// signature below is the candidate research 08 verified as exactly one
			// hit at its site on this build; the scans run in ascending address
			// order so no candidate is read after an earlier site was overwritten.
			if (auto p = Pattern(image, "75 ?? 39 44 24 ?? 74 ?? 8A C3 5F 5E 5D 5B 83 C4 ?? C3 8D 54 24 ?? 52 8D"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x00389C
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksMidJmp_1070.site(ctx.gameBase)), true); //jmp
			}
			if (auto p = Pattern(image, "75 ?? 8B 4E ?? 8D 44 24 ?? 50 6A ?? 51 89 7C 24 ?? E8"); p.size() > 0)
			{
				auto* site = p.get(0).get<std::uint8_t>(); // 0x003962
				injector::MakeJMP(site, reinterpret_cast<void*>(kModChecksCaseJmp_1070.site(ctx.gameBase)), true); //jmp
			}
		}
	}
	Trace("SuperLODFix");
	// SuperLODFix: 2 rvajumps
	if (ctx.Option("SuperLODFix", "1"))
	{
		const GameImage& image = ctx.image;

		// The original jumped at 0x4A3CE9 (1.0.7.0) / 0x540E19 (1.0.8.0). The
		// recovered signature is the same on both and unique per gated build
		// (research 08), so scan inside the gate instead of naming the RVA.
		if (ctx.build == GameBuild::v1070)
		{
			if (auto pattern = Pattern(image, "F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8"); pattern.size() > 0)
			{
				injector::MakeJMP(pattern.get(0).get<std::uint8_t>(),
				                  reinterpret_cast<void*>(&abi_super_lod_fix_shim), true); //jmp
			}
		}

		if (ctx.build == GameBuild::v1080)
		{
			if (auto pattern = Pattern(image, "F6 40 ?? ?? 74 ?? 85 ED 0F 85 ?? ?? ?? ?? 55 6A ?? E8 ?? ?? ?? ?? 8B F8"); pattern.size() > 0)
			{
				injector::MakeJMP(pattern.get(0).get<std::uint8_t>(),
				                  reinterpret_cast<void*>(&abi_super_lod_fix_shim), true); //jmp
			}
		}
	}
	Trace("DrivebyFOVFix");
	// DrivebyFOVFix: 1 sigjump
	if (ctx.Option("DrivebyFOVFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "F3 0F 10 46 60 F3 0F 5C CA"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_driveby_fov_shim), true); //jmp
		}
	}
	Trace("CrashMsgFix");
	// CrashMsgFix: 1 siginstall
	if (ctx.Option("CrashMsgFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 6A 10 4E 31"); pattern.size() > 0)
		{
			std::uint8_t* site = MatchPointer(pattern, -4);
			injector::MakeCALL(site, reinterpret_cast<void*>(&abi_crash_msg_shim), true); //call
		}
	}
	Trace("BenchmarkFix");
	// BenchmarkFix: 2 matchjumps
	if (ctx.Option("BenchmarkFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1000, GameBuild::v1040, GameBuild::v1004, GameBuild::v1060,
		             GameBuild::v1070, GameBuild::v1061, GameBuild::v1100, GameBuild::v1110,
		             GameBuild::v1120, GameBuild::v12043}))
		{

			{
				if (auto pattern = Pattern(image, "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20"); pattern.size() > 0)
				{
					injector::MakeJMP(pattern.get(0).get<std::uint8_t>(8),
						reinterpret_cast<void*>(&abi_benchmark_a), true); //jmp
					if (pattern.size() > 1)
						injector::MakeJMP(pattern.get(1).get<std::uint8_t>(8),
							reinterpret_cast<void*>(&abi_benchmark_b), true); //jmp
				}
			}
		}
	}
	Trace("CarPartsShadowFix");
	// CarPartsShadowFix: 1 siginstall, 1 sigjump
	if (ctx.Option("CarPartsShadowFix", "1") || ctx.Option("CarDynamicShadowFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "85 DB 7E 41 55 EB 0A"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 6, reinterpret_cast<void*>(&abi_carparts_shadow), true); //jmp
		}

		if (auto pattern = Pattern(image, "485 DB 7E 41 55 EB 0A"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 6, reinterpret_cast<void*>(&abi_carparts_shadow), true); //jmp
		}
	}
	Trace("ForceCarHeadlightShadows");
	// ForceCarHeadlightShadows: 2 siginstalls, 5 sigwrites
	if (ctx.Option("ForceCarHeadlightShadows", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "3C 01 75 50 84 DB 75 79"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 6, kShortJmpOpcode, true); //jmp
			injector::MakeJMP(match + 95, reinterpret_cast<void*>(match + 476), true); //jmp
			injector::WriteMemory<std::uint8_t>(match + 379, kShortJmpOpcode, true); //jmp
			injector::WriteMemory<std::uint32_t>(match + 211, AddressOf(&zpatch_fchs_offset), true); //patch
		}

		if (auto pattern = Pattern(image, "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_fchs_shim), true); //jmp
		}

		{
			if (auto pattern = Pattern(image, "8D 4C 24 50 51 68 04 05 00 00"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(6),
				                                    kShadowPushA & kByteMask, true); //patch
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(7),
				                                    (kShadowPushA >> 8) & kByteMask, true); //patch
			}
		}

		{
			if (auto pattern = Pattern(image, "8D 44 24 50 50 68 04 01 00 00"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(6),
				                                    kShadowPushB & kByteMask, true); //patch
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(7),
				                                    (kShadowPushB >> 8) & kByteMask, true); //patch
			}
		}

		{
			if (auto pattern = Pattern(image, "F3 0F 11 44 24 1C 0F 84 9E 00 00 00"); pattern.size() > 0)
			{
				auto* site = pattern.get(0).get<std::uint8_t>(6);
				// `0F 84 9E 00 00 00` = je +0x9E; the arm rewrites the first four
				// bytes (the sixth was already 0).
				injector::WriteMemory<std::uint8_t>(site, kJeRel32First, true);      // 0F
				injector::WriteMemory<std::uint8_t>(site + 1, kJeRel32Second, true); // 84
				injector::WriteMemory<std::uint16_t>(site + 2, kCutsceneBranchDisp, true); // +0x9E
				injector::WriteMemory<std::uint8_t>(site + 4, kZeroByte, true); //jz rel32
			}
		}
	}
	Trace("Patch4RemoveModChecks");
	// Patch4RemoveModChecks: 1 siginstall
	if (ctx.Option("Patch4RemoveModChecks", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_gfwl_stub), true); //jmp
		}
	}
	Trace("FreezeCarFix");
	// FreezeCarFix: 7 siginstalls
	if (ctx.Option("FreezeCarFix", "1") &&
	    Pattern(ctx.image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0").size() > 0)
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 7, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
		}

		if (auto pattern = Pattern(image, "8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 11, reinterpret_cast<void*>(&abi_spawns_guard), true); //call
		}

		if (auto pattern = Pattern(image, "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			// The ten case bodies inside the probe: the original's jump-table
			// selectors are the byte offsets from the probe match.
			for (const SpawnCase& spawn : kSpawnCases)
				injector::MakeCALL(match + spawn.offset, reinterpret_cast<void*>(spawn.body), true); //call
		}

		if (auto pattern = Pattern(image, "88 87 40 01 00 00 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 6, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
		}

		if (auto pattern = Pattern(image, "88 5C 24 1F 8B 54 24 28 52 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 9, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
			injector::MakeCALL(match + 254, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
		}

		if (auto pattern = Pattern(image, "38 98 8C 00 00 00 75 2D 57 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 9, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
		}

		if (auto pattern = Pattern(image, "32 DB 8B 54 24 24 52 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 7, reinterpret_cast<void*>(&abi_spawns_probe), true); //call
		}
	}
	Trace("CarDynamicShadowFix");
	// CarDynamicShadowFix: 3 siginstalls, 2 sigwrites
	if (ctx.Option("CarDynamicShadowFix", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_carparts_shadow_2), true); //jmp
		}

		if (auto pattern = Pattern(image, "56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_carparts_shadow_2b), true); //jmp
		}

		if (auto pattern = Pattern(image, "83 F8 14 7D 05 83 F8 06 7D 14"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 3, reinterpret_cast<void*>(match + 30), true); //jmp
		}

		{
			if (auto pattern = Pattern(image, "83 E0 0F 83 F8 03 75 17 F6 86 6C 02 00 00 04"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(6), kShortJmpOpcode, true); //jmp
			}
		}

		{
			if (auto pattern = Pattern(image, "50 68 04 01 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(2), kZeroByte, true); //patch
			}
		}

		// Two further writes the original makes under this arm; they are in its
		// write log for a CarDynamicShadowFix-only cell but not in the recovered
		// row tables, so the reconstruction does not make them either. They are
		// the arm's @noFCHS variant: with ForceCarHeadlightShadows on, the
		// original takes the other path and makes neither write (probed:
		// CarDynamic+FCHS writes only 0x8F7422; CarDynamic alone also writes
		// 0x8F73EC and 0x8F719F).
		//
		//  * `0F 84 <rel32>` (jz) becomes `E9 <rel32+1>`: a jmp to the same
		//    target. The displacement grows by one because the jz encoding is one
		//    byte longer than the jmp (match+6+rel == match+5+newRel).
		//  * `push 0x504` becomes `push 0x100` (the shadow size the arm forces),
		//    written as a dword over the immediate like the original does.
		if (!ctx.Option("ForceCarHeadlightShadows", "1"))
		{
			if (auto pattern = Pattern(image, "0F 84 9E 00 00 00 80 7D 28"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				// The jz's own destination, re-encoded as a 5-byte jmp (rel32+1
				// falls out of dest-(match+5)); the old instruction's sixth byte
				// is left untouched, as before.
				const auto* target = reinterpret_cast<std::uint8_t*>(
				    injector::ReadRelativeOffset(injector::raw_ptr(match + 2), 4, true).as_int());
				injector::MakeJMP(injector::raw_ptr(match), injector::raw_ptr(target), true); //jmp
			}

			if (auto pattern = Pattern(image, "51 68 04 05 00 00 6A 02 6A 00 E8"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::WriteMemory<std::uint32_t>(match + 2, kModelLimitIncrease, true); //patch
			}
		}
	}
	Trace("GarageModelFix");
	// GarageModelFix: 2 siginstalls
	if (ctx.Option("GarageModelFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 57 20 D9 42 34 D9 5E 04 8B 47 20"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_garage_body_a), true); //jmp
		}

		if (auto pattern = Pattern(image, "0F B7 0E 50 51 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_garage_body_b), true); //jmp
		}
	}
	Trace("MovingSteeringWheelSupport");
	// MovingSteeringWheelSupport: 3 siginstalls
	if (ctx.Option("MovingSteeringWheelSupport", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_eax), true); //jmp
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_edx), true); //jmp
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 10, reinterpret_cast<void*>(&abi_copsirens_ide), true); //jmp
		}
	}
	Trace("ShakingEngineBlock");
	// ShakingEngineBlock: 3 siginstalls
	if (ctx.Option("ShakingEngineBlock", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_eax), true); //jmp
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_edx), true); //jmp
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 10, reinterpret_cast<void*>(&abi_copsirens_ide), true); //jmp
		}
	}
	Trace("MovingPlaneParts");
	// MovingPlaneParts: 3 siginstalls
	if (ctx.Option("MovingPlaneParts", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_eax), true); //jmp
		}

		if (auto pattern = Pattern(image, "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 2, reinterpret_cast<void*>(&abi_steer_detour_edx), true); //jmp
		}

		if (auto pattern = Pattern(image, "0C 83 88 94 00 00 00 04"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 10, reinterpret_cast<void*>(&abi_copsirens_ide), true); //jmp
		}
	}
	Trace("MouseFix");
	// MouseFix: 8 siginstalls
	if (ctx.Option("MouseFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_mouse_axis_float), true); //jmp
		}

		if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_mouse_axis_neg), true); //jmp
		}

		if (auto pattern = Pattern(image, "83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemoryRaw(injector::raw_ptr(match + 5),
			                         const_cast<std::uint8_t*>(kMovdXmm0Eax),
			                         sizeof(kMovdXmm0Eax), true); //movd xmm0, eax
			injector::WriteMemoryRaw(injector::raw_ptr(match + 33),
			                         const_cast<std::uint8_t*>(kMovdXmm0Eax),
			                         sizeof(kMovdXmm0Eax), true); //movd xmm0, eax
			injector::MakeNOP(match + 3, 2, true); //nop
			injector::MakeNOP(match + 31, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemoryRaw(injector::raw_ptr(match + 5),
			                         const_cast<std::uint8_t*>(kMovdXmm0Eax),
			                         sizeof(kMovdXmm0Eax), true); //movd xmm0, eax
			injector::WriteMemoryRaw(injector::raw_ptr(match + 33),
			                         const_cast<std::uint8_t*>(kMovdXmm0Eax),
			                         sizeof(kMovdXmm0Eax), true); //movd xmm0, eax
			injector::MakeNOP(match + 3, 2, true); //nop
			injector::MakeNOP(match + 31, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemoryRaw(injector::raw_ptr(match + 5),
			                         const_cast<std::uint8_t*>(kMovdXmm1Eax),
			                         sizeof(kMovdXmm1Eax), true); //movd xmm1, eax
			injector::MakeNOP(match + 3, 2, true); //nop
		}

		if (auto pattern = Pattern(image, "8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_mouse_axis_flag), true); //jmp
		}

		if (auto pattern = Pattern(image, "56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match + 51, reinterpret_cast<void*>(&abi_mouse_axis_int), true); //call
			injector::MakeCALL(match + 90, reinterpret_cast<void*>(&abi_mouse_axis_int), true); //call
		}

		if (auto pattern = Pattern(image, "53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match - 15, reinterpret_cast<void*>(&abi_mouse_angle), true); //jmp
			injector::MakeJMP(match + 1862, reinterpret_cast<void*>(&abi_mouse_advance), true); //jmp
		}
	}
	Trace("OutOfCommissionFix");
	// OutOfCommissionFix: 2 siginstalls
	if (ctx.Option("OutOfCommissionFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "A9 39 44 24 18"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match + 7, reinterpret_cast<void*>(&abi_ooc_branch), true); //jmp
		}

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match, reinterpret_cast<void*>(&abi_ooc_wrap), true); //call
		}
	}
	Trace("HighQualityReflections");
	// HighQualityReflections: 1 siginstall
	if (ctx.Option("HighQualityReflections", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_hqr_body), true); //jmp
		}
	}
	Trace("CutsceneFixes");
	// CutsceneFixes: 2 siginstalls, 1 sigwrite
	if (ctx.Option("CutsceneFixes", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 5, reinterpret_cast<void*>(&abi_cutscene_chain_b), true); //call
		}

		if (auto pattern = Pattern(image, "8B CF 88 44 24 0F E8 ? ? ? ? A1"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 5, reinterpret_cast<void*>(&abi_cutscene_chain_b), true); //call
		}

		{
			// 1080 has exactly one match of the primary pattern (0xC9A840); the
			// alternate is only for builds where it is absent. Falling back on
			// "<= 1 match" threw that single match away and applied nothing.
			auto pattern = Pattern(image, "74 24 8B 44 24 2C 83 F8 03");
			if (pattern.size() == 0)
				pattern = Pattern(image, "74 0B 83 7C 24 2C 03");
			if (pattern.size() > 0)
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(), kShortJmpOpcode, true); //jmp
		}
	}
	Trace("RemoveGFWLUpdatePopup");
	// RemoveGFWLUpdatePopup: 1 siginstall
	if (ctx.Option("RemoveGFWLUpdatePopup", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_gfwl_stub), true); //jmp
		}
	}
	Trace("CarDecalsFix");
	// CarDecalsFix: 1 siginstall
	if (ctx.Option("CarDecalsFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_car_decals_fix), true); //jmp
		}
	}
	Trace("RestoreDeathMusic");
	// RestoreDeathMusic: 3 siginstalls
	if (bMenuIntegration || ctx.Option("RestoreDeathMusic", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F 57 C0 F3 0F 11 86 20 02 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 13, reinterpret_cast<void*>(&abi_death_music_install), true); //call
		}

		if (auto pattern = Pattern(image, "C7 86 04 02 00 00 04 00 00 00 E9"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match - 13, reinterpret_cast<void*>(&abi_death_music_enabled), true); //call
		}

		if (auto pattern = Pattern(image, "E8 ? ? ? ? 84 C0 74 C6 8B BE"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeCALL(match, reinterpret_cast<void*>(&abi_death_music_enabled), true); //call
		}
	}
	Trace("CarDynamicShadowFixNoE");
	// CarDynamicShadowFix@noE: 1 siginstall
	if (ctx.Option("CarDynamicShadowFix", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 F8 14 7D 05 83 F8 06 7D 14"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match + 62, 6, true); //nop
		}
	}
	Trace("DualVehicleHeadlightsOld");
	// DualVehicleHeadlights@old: 1 siginstall
	if (ctx.Option("DualVehicleHeadlights", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_dual_headlights_old), true); //jmp
		}
	}
	Trace("DualVehicleHeadlightsNew");
	// DualVehicleHeadlights@new: 1 siginstall
	if (ctx.Option("DualVehicleHeadlights", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "8B 17 8B 42 64 8B CF FF D0 8B 54"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeJMP(match, reinterpret_cast<void*>(&abi_dual_headlights_new), true); //jmp
		}
	}
	Trace("BorderlessWindowed");
	// BorderlessWindowed: 2 matchjumps, 2 sigwrites
	if (ctx.Option("BorderlessWindowed", "1") && !FusionFixGateClosed())
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "FF 15 ? ? ? ? 5F 5E 5D 5B 83 C4 10 C2 10 00"); pattern.size() > 0)
			{
				// The call is installed before the 0x90: offset 5 is the fourth
				// byte of the `FF 15 <disp32>` operand, so writing it first would
				// make a read of the old destination dereference the corrupted
				// operand (0x00D6B460 becomes 0x90D6B460). The original's
				// collector writes opcode+rel32 without reading it, so do that.
				WriteCallNoRead(pattern.get(0).get<std::uint8_t>(), reinterpret_cast<void*>(&abi_borderless_w));
				if (pattern.size() > 1)
					WriteCallNoRead(pattern.get(1).get<std::uint8_t>(), reinterpret_cast<void*>(&abi_borderless_a));
				injector::MakeNOP(pattern.get(0).get<std::uint8_t>(5), 1, true); //nop
				if (pattern.size() > 1)
					injector::MakeNOP(pattern.get(1).get<std::uint8_t>(5), 1, true); //nop
			}
		}
	}
	Trace("FastLoading");
	// FastLoading: 1 sigwrite
	if (ctx.Option("FastLoading", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "6A 0A E8 ? ? ? ? 83 C7 0A 83 C4 04 81 FF E8 03 00 00 75 02 33 FF"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(1), kZeroByte, true); //patch
			}
		}
	}
	Trace("DoNotPauseOnMinimize");
	// DoNotPauseOnMinimize: 2 sigwrites
	if (ctx.Option("DoNotPauseOnMinimize", "0"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "6A 0A 56 50 FF D2 A1"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(1),
			                                    kMinimizePauseTick, true); //patch
			}
		}

		{
			if (auto pattern = Pattern(image, "75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15"); pattern.size() > 0)
			{
				injector::MakeNOP(pattern.get(0).get<std::uint8_t>(), 2, true); //nop
			}
		}
	}
	Trace("CarPartsShadowFixStandalone");
	// CarPartsShadowFix@standalone: 2 sigwrites
	if (ctx.Option("CarPartsShadowFix", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "83 F8 04 C6 44 24 0E 00 75 0F"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(8), kShortJmpOpcode, true); //jmp
			}
		}

		{
			if (auto pattern = Pattern(image, "83 FA 04 75 0C"); pattern.size() > 0)
			{
				injector::WriteMemory<std::uint8_t>(pattern.get(0).get<std::uint8_t>(3), kShortJmpOpcode, true); //jmp
			}
		}
	}
	Trace("IncreaseVehicleModelLimit");
	// IncreaseVehicleModelLimit: 1 indirect
	if (ctx.Option("IncreaseVehicleModelLimit", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "83 C7 01 81 FF 18 79 00 00 7C D2"); pattern.size() > 0)
		{
			auto* target = MatchPointer(pattern, 77);
			if (target != nullptr)
				injector::WriteMemory<std::uint32_t>(target, kPtrNodeInitial, true); //patch
		}
	}
	Trace("NoPhysicsSleep");
	// NoPhysicsSleep: 1 indirect
	if (ctx.Option("NoPhysicsSleep", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "0F 00 00 00 C7 41 14 14 00 00 00 C7 41 1C 00 00"); pattern.size() > 0)
		{
			auto* target = MatchPointer(pattern, -38);
			if (target != nullptr)
				injector::WriteMemory<std::uint32_t>(target, 0u, true); //patch
		}
	}
	Trace("NoImpostorHeightCutoff");
	// NoImpostorHeightCutoff: 2 matchliterals
	if (ctx.Option("NoImpostorHeightCutoff", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "76 0B F3 0F 11 8C 24 88 00 00 00 EB 09"); pattern.size() > 0)
			{
				injector::MakeNOP(pattern.get(0).get<std::uint8_t>(), kWordLength, true); //nop
				if (pattern.size() > 1)
					injector::MakeNOP(pattern.get(1).get<std::uint8_t>(), kWordLength, true); //nop
			}
		}
	}
	Trace("StartupTimeFix");
	// StartupTimeFix: 1 matchliteral
	if (ctx.Option("StartupTimeFix", "1"))
	{
		const GameImage& image = ctx.image;

		{
			if (auto pattern = Pattern(image, "68 00 C2 EC 80 68"); pattern.size() > 0)
			{
				for (std::size_t i = 0; i < pattern.size(); ++i)
					injector::WriteMemory<std::uint32_t>(
					    pattern.get(i).get<std::uint8_t>() + 6,
					    static_cast<std::uint32_t>(FindGlobalValue("StartupTimeFix")), true); //patch
			}
		}
	}
	Trace("IncreasePtrNodes");
	// IncreasePtrNodes: 3 literalstores
	if (ctx.Option("IncreasePtrNodes", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "6A 08 ? ? ? ? ? 68 80 38 01 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(match + 8, kPtrNodeCount, true); //patch
		}

		if (auto pattern = Pattern(image, "6A 10 ? ? ? ? ? 68 80 3E 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(match + 8, kPtrNodeCountAlt, true); //patch
		}

		if (auto pattern = Pattern(image, "6A 14 ? ? ? ? ? 68 80 3E 00 00"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(match + 8, kPtrNodeCountAlt, true); //patch
		}
	}
	Trace("IncreaseVehicleStructLimit");
	// IncreaseVehicleStructLimit: 1 literalstore
	if (ctx.Option("IncreaseVehicleStructLimit", "1"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 ? ? ? ? 6A 32 8B C8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint8_t>(match + 6,
			                                    static_cast<std::uint8_t>(kVehicleStructLimit),
			                                    true); //patch
		}
	}
	Trace("NoLODLightHeightCutoff");
	// NoLODLightHeightCutoff: 1 literalstore
	if (ctx.Option("NoLODLightHeightCutoff", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "76 08 F3 0F 11 4C 24 1C EB 06 F3 0F 11 44 24 1C 89 5C 24 64"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::MakeNOP(match, kWordLength, true); //nop
		}
	}
	Trace("NoLoadingSleep");
	// NoLoadingSleep: 1 literalstore
	if (ctx.Option("NoLoadingSleep", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 88 13 00 00 FF 15"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(match + 1, 0u, true); //patch
		}
	}
	Trace("OverrideGTADatPath");
	// OverrideGTADatPath: 1 literalstore
	if (ctx.Option("OverrideGTADatPath", "0"))
	{
		const GameImage& image = ctx.image;

		if (auto pattern = Pattern(image, "68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8"); pattern.size() > 0)
		{
			auto* match = pattern.get(0).get<std::uint8_t>();

			injector::WriteMemory<std::uint32_t>(
			    match + 1, static_cast<std::uint32_t>(FindGlobalValue("OverrideGTADatPath")), true); //patch
		}
	}
	Trace("VSyncFix");
	// VSyncFix: 2 literalstores
	if (ctx.Option("VSyncFix", "0"))
	{
		const GameImage& image = ctx.image;

		if (BuildIs(ctx.build,
		            {GameBuild::v1004, GameBuild::v1060, GameBuild::v1070, GameBuild::v1061,
		             GameBuild::v1100, GameBuild::v1110, GameBuild::v1120, GameBuild::v12043}))
		{

			if (auto pattern = Pattern(image, "8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::WriteMemory<std::uint32_t>(
			    match + 30, static_cast<std::uint32_t>(FindGlobalValue("VSyncFix")), true); //patch
			}

			if (auto pattern = Pattern(image, "8B 10 50 8B 82 A8 00 00 00 FF D0 83"); pattern.size() > 0)
			{
				auto* match = pattern.get(0).get<std::uint8_t>();

				injector::WriteMemory<std::uint32_t>(
			    match + 13, static_cast<std::uint32_t>(FindGlobalValue("VSyncFix")), true); //patch
			}
		}
	}
}

void SetMenuIntegration(bool available) noexcept
{
	bMenuIntegration = available;
}

// The loader's pointer scans: the offsets their signatures imply.
constexpr std::uint32_t kCallOperandOffset = 6u;
constexpr std::uint32_t kResolverSignatureLength = 10u;
constexpr std::uint32_t kPreFn59C74Offset = 42u;
constexpr std::uint32_t kPreFn5D63COffset = 48u;
constexpr std::uint32_t kPreFn59CDCOffset = 48u;

PreambleReport FindGamePointers(GameImage image) noexcept
{
	PreambleReport report;
	if (!image.base || image.size == 0)
		return report;

	const ModuleView view{image.base, image.size};
	const auto resolve = [&](std::string_view signature) -> const std::uint8_t*
	{
		Match found;
		return FindPattern(view, signature, found) ? found.address : nullptr;
	};
	// The seven counted scans; the build-probe salt fallback below is not one.
	const auto scan = [&](std::string_view signature) -> const std::uint8_t*
	{
		if (const std::uint8_t* hit = resolve(signature))
		{
			++report.resolved;
			return hit;
		}
		++report.missing;
		return nullptr;
	};

	if (const std::uint8_t* hit = scan("08 0B C2 8B F0 E8"))
	{
		zpatch_driveby_resolver = ReadDword(hit + kCallOperandOffset) +
		                          AddressOf(hit) + kResolverSignatureLength;
	}

	if (const std::uint8_t* hit = scan("85 F6 74 30 8B 16 8B 42 0C 8B CE FF D0 3C 02 75 23"))
		zpatch_pre_fn_59c74 = AddressOf(hit) - kPreFn59C74Offset;

	if (const std::uint8_t* hit = scan("8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E"))
		zpatch_model_lookup = AddressOf(hit);

	if (const std::uint8_t* hit = scan("85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07"))
		zpatch_pre_ped_array = ReadDword(hit - kDwordLength);

	if (const std::uint8_t* hit = scan("56 8B F1 8B 06 8B 90 A0 00 00 00 57 FF D2 85 C0 74 1C"))
		zpatch_pre_fn_59b88 = AddressOf(hit);

	if (const std::uint8_t* hit = scan("0F 10 5E 14 F3 0F 10 66 18 F3 0F 10 4C 24 0C F3"))
		zpatch_pre_fn_5d63c = AddressOf(hit) - kPreFn5D63COffset;

	if (const std::uint8_t* hit = scan(
	        "0F 10 16 F3 0F 10 5E 04 F3 0F 10 4C 24 0C F3 0F 10 66 08 F2 0F 5A C0"))
		zpatch_pre_fn_59cdc = AddressOf(hit) - kPreFn59CDCOffset;

	// Build-probe signature: absent on an unknown build, so all ten hash salts
	// fall back to these values.
	if (resolve("68 ? ? ? ? 68 A9 54 14 51 E8") == nullptr)
	{
		// opaque: fallback salt table, selected when the build-probe signature is
		// absent (research 07 §7.3); the primary values are the globals' defaults.
		zpatch_salt_28 = 0x931F73A2u; // opaque: fallback salt 0x28
		zpatch_salt_3c = 0xA4EA0691u; // opaque: fallback salt 0x3c
		zpatch_salt_40 = 0xC9DBDA90u; // opaque: fallback salt 0x40
		zpatch_salt_50 = 0x6B86410Eu; // opaque: fallback salt 0x50
		zpatch_salt_54 = 0x6DCD115Au; // opaque: fallback salt 0x54
		zpatch_salt_5c = 0x1CC648EAu; // opaque: fallback salt 0x5c
		zpatch_salt_a8 = 0x7F6A60D3u; // opaque: fallback salt 0xa8
		zpatch_salt_b8 = 0x62BFDB37u; // opaque: fallback salt 0xb8
		zpatch_salt_c8 = 0x406B4B20u; // opaque: fallback salt 0xc8
		zpatch_salt_d4 = 0x09399528u; // opaque: fallback salt 0xd4
	}
	return report;
}
