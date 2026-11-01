// ZolikaPatch ASI entry point.
//
// Everything runs synchronously from DllMain: the DRM has already rewritten the
// game's import-time code by the time a deferred worker would get to scan, so the
// signatures have to be matched now.
//
// The support code is split into FusionFix-style modules under `source/` (one
// logical piece per .ixx). This is the only plain translation unit left: it keeps
// the entry point, the loader pipeline (`SetStuff`), its helpers and the probe
// (`FindGamePointers`). The install arms live in the feature modules and
// `PatchGame` only walks them, in the order this tree has always run them.

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
import zp.vehicles;
import zp.graphics;
import zp.stability;
import zp.audio;
import zp.input;
import zp.loading;
import zp.mission;
import zp.world;
import zp.multiplayer;
import zp.misc;

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

// ---- the loader pipeline -------------------------------------------------
//
// `SetStuff` is the whole load-time pipeline, in the order the recovered loader
// runs its stages.
// Each phase is one named step; the install arms themselves live in the feature
// modules (`source/<feature>.ixx`) and `PatchGame` only walks them. The order is
// the behaviour: no phase may move.
//
// The two load-bearing edges: the ZMenuIV guard runs even when the config load
// fails (it follows config-object construction, unconditionally), and
// RemoveUselessChecks runs before MiscFixes/RestoreDeathMusic, whose signatures
// its tables destroy (`zp_dump --ordering` pins that one).

// FixPCIDKickbug: drop the GFWL CD-key value so keygen users stop kicking each
// other on join.
void ApplyFixPCIDKickbug(INI<>& ini)
{
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
}

// The menu integration: with AddOptionsInMenu on and the module available, the
// two runtime toggles are registered and RestoreDeathMusic's hooks install.
void SetupMenuIntegration(INI<>& ini)
{
	const bool menu = IsMenuIntegrationAvailable(ini);
	SetMenuIntegration(menu);
	if (menu)
		RegisterMenuOptions();
	if (menu || Option(ini, "DoNotPauseOnMinimize", "0"))
		zpatch_menu_prime_focus_loss();
}

// LocalSettingsPath captures the CWD and arms its call slot before installing.
void CaptureLocalSettings(INI<>& ini)
{
	if (Option(ini, "LocalSettingsPath", "0"))
	{
		TraceBoot("localsettings");
		zpatch_local_settings_capture_cwd();
	}
}

// CarDecalsFix's double-load guard: when its scan finds nothing, another ASI
// loader already patched the site.
void ReportDoubleLoad(INI<>& ini, GameImage image)
{
	if (Option(ini, "CarDecalsFix", "1"))
	{
		Match decals;
		const ModuleView view{image.base, image.size};
		if (!FindPattern(view, "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00", decals))
		{
			MessageBoxA(0,
			            "ZPatch was loaded twice! Make sure that you don't have multiple ASI "
			            "loaders installed! (e.g. xliveless + dsound, xliveless + dinput8, "
			            "dsound + dinput8, etc)",
			            "ZPatch Error", 0);
		}
	}
}

void SetStuff()
{
	if (!iniPath[0])
		return;

	// Phase 1: the INI and the config-derived globals.
	TraceBoot("ini");
	INI<> ini(iniPath, true);
	ini.select(kOptionsSection);
	ConfigDocument& config = ini.document();
	// Deferred on purpose: the load keeps its loader position, and the guard below
	// must run even when it fails (the single-use flag is the price of that order).
	const bool initialized = ini.loaded() && LoadConfigAndFlags(ini, config);
	TraceBoot("config");

	// Phase 2: the unconditional guards.
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

	// Phase 3: the loader's pre-install stages: DFA, SkipLauncher, PCID.
	// MiscFixes' loader arm runs before SkipLauncher's and calls the one-shot DFA
	// installer under the same guards; the SkipLauncher call is then a no-op.
	if (nt && nt->Signature == IMAGE_NT_SIGNATURE && Option(ini, "MiscFixes", "1"))
	{
		TraceBoot("dfa");
		ApplyDFA(build, GameImage{moduleBase, nt->OptionalHeader.SizeOfImage});
	}

	TraceBoot("skiplauncher");
	(void)ApplySkipLauncherIfEnabled(build, ini);

	TraceBoot("pcid");
	ApplyFixPCIDKickbug(ini);

	if (build == GameBuild::unknown)
		return;
	if (!nt || nt->Signature != IMAGE_NT_SIGNATURE)
		return;

	const GameImage gameImage{moduleBase, nt->OptionalHeader.SizeOfImage};

	// Phase 4: the runtime integration (menu toggles, the settings-path capture).
	TraceBoot("menu");
	SetupMenuIntegration(ini);
	CaptureLocalSettings(ini);

	// Phase 5: the probe -- the seven scans the arms read, plus the double-load
	// guard.
	TraceBoot("preamble");
	(void)FindGamePointers(gameImage);
	ReportDoubleLoad(ini, gameImage);

	// Phase 6: the install pass (the feature modules, in this tree's arm order).
	TraceBoot("patchgame");
	PatchGame(build, gameImage, config);

	// Phase 7: the post-install tail (the PlayGTAIV launcher slots).
	ApplyLaunchWithPlayGTAIV(build, GameImage{moduleBase, nt->OptionalHeader.SizeOfImage}, ini);
}
} // namespace

// ---- startup guards ------------------------------------------------------
//
// Generic startup logic, not a feature: the ZMenuIV guard asks the loaded menu
// for its version and `EnforceZMenuVersion` above turns a rejection into the
// original's message box and exit. The probe stays at global scope because the
// native tests drive it directly.

ZMenuGuardResult ZMenuVersionGuard(std::uint32_t& version) noexcept
{
	version = 0;
	const HMODULE module = GetModuleHandleA(kZMenuModuleName);
	if (!module)
		return ZMenuGuardResult::module_absent;

	const auto getVersion = reinterpret_cast<int(__cdecl*)(std::uint32_t*, std::uint32_t*)>(
		reinterpret_cast<void*>(GetProcAddress(module, kZMenuProcName)));
	if (!getVersion)
		return ZMenuGuardResult::export_absent;

	// The export reports two dwords; only the first is compared.
	std::uint32_t out = 0;
	(void)getVersion(&version, &out); // the original ignores the result
	return ZMenuVersionAccepted(version) ? ZMenuGuardResult::version_ok
	                                     : ZMenuGuardResult::version_rejected;
}

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



// The option pass: the arms live in the feature modules (`source/<feature>.ixx`),
// in the order this tree has always run them. `PatchGame` only walks that order.
void PatchGame(GameBuild build, GameImage image, const ConfigDocument& config)
{
	PatchContext ctx{image, reinterpret_cast<std::uintptr_t>(image.base), build,
	                  config};

	// Publish every slot before any arm writes a site: a site write can
	// overwrite the signature a later arm's slot publish scans for. The feature
	// modules own the blocks; the order here is the one this tree has always run, and
	// it is the only ordering there is (no phase graph).
	PublishBadPlayerModelCrashFix(ctx);
	PublishBlackscreenFix(ctx);
	PublishBuildingDynamicShadows(ctx);
	PublishCollisionDeadlockFix(ctx);
	PublishCopSirensFlag(ctx);
	PublishDualVehicleHeadlights(ctx);
	PublishEmissiveLerpFix(ctx);
	PublishEpisodeSwitchFreezeFix(ctx);
	PublishFreezeGunFix(ctx);
	PublishGroupHackFix(ctx);
	PublishHighFPSBikePhysicsFix(ctx);
	PublishLoadDLCs(ctx);
	PublishLocalSettingsPath(ctx);
	PublishNewFilesCompatibility(ctx);
	PublishNewMemorySystem(ctx);
	PublishNewSavesCompatibility(ctx);
	PublishNoLiveryLimit(ctx);
	PublishPathNodeCrashFix(ctx);
	PublishPedModelsInCutscenesFix(ctx);
	PublishPlaneCrashFix(ctx);
	PublishPlayerAsPedComponentsFix(ctx);
	PublishPopupHeadlightsFlag(ctx);
	PublishSMPA60Fix(ctx);
	PublishSkipIntro(ctx);
	PublishSkipMenu(ctx);
	PublishSpeedupBugFix(ctx);
	PublishWantedLevelCrashFix(ctx);
	PublishSuperLODFix(ctx);
	PublishDrivebyFOVFix(ctx);
	PublishCrashMsgFix(ctx);
	PublishBenchmarkFix(ctx);
	PublishCarPartsShadowFix(ctx);
	PublishForceCarHeadlightShadows(ctx);
	PublishPatch4RemoveModChecks(ctx);
	PublishFreezeCarFix(ctx);
	PublishCarDynamicShadowFix(ctx);
	PublishGarageModelFix(ctx);
	PublishMovingSteeringWheelSupport(ctx);
	PublishShakingEngineBlock(ctx);
	PublishMovingPlaneParts(ctx);
	PublishMouseFix(ctx);
	PublishOutOfCommissionFix(ctx);
	PublishHighQualityReflections(ctx);
	PublishCutsceneFixes(ctx);
	PublishRemoveGFWLUpdatePopup(ctx);

	// Installs, in the same arm order.
	ApplyAimProjectileCrashFix(ctx);
	ApplyAllowHashesForIDE(ctx);
	ApplyBadPlayerModelCrashFix(ctx);
	ApplyBetterMPSync(ctx);
	ApplyBikeFeetFix(ctx);
	ApplyBikePhoneAnimsFix(ctx);
	ApplyBlackscreenFix(ctx);
	ApplyBuildingAlphaFix(ctx);
	ApplyBuildingDynamicShadows(ctx);
	// The original applies its singles and RVA tables as the loader's 27th
	// option read, after BuildingDynamicShadows. The early position is
	// load-bearing: the tables overwrite one byte of the MiscFixes and
	// RestoreDeathMusic signatures, suppressing both later arms exactly as in
	// the original (`zp_dump --ordering` pins it).
	ApplyRemoveUselessChecks(ctx);
	ApplyCollisionDeadlockFix(ctx);
	ApplyCombinedRadioFreezeFix(ctx);
	ApplyCopSirensFlag(ctx);
	ApplyEmissiveLerpFix(ctx);
	ApplyEpisodeSwitchFreezeFix(ctx);
	ApplyEpisodicVehicleSupport(ctx);
	ApplyEpisodicWeaponSupport(ctx);
	ApplyForceDynamicShadowsEverywhere(ctx);
	ApplyForceShadowsOnObjects(ctx);
	ApplyFreezeGunFix(ctx);
	ApplyGroupHackFix(ctx);
	ApplyHighFPSBikePhysicsFix(ctx);
	ApplyHighFPSSpeedupFix(ctx);
	ApplyImprovedShaderStreaming(ctx);
	ApplyInfiniteCarBottomDistance(ctx);
	ApplyLoadDLCs(ctx);
	ApplyLocalSettingsPath(ctx);
	ApplyMPNikoCrashFix(ctx);
	ApplyMiscFixes(ctx);
	ApplyNewFilesCompatibility(ctx);
	ApplyNewMemorySystem(ctx);
	ApplyNewSavesCompatibility(ctx);
	ApplyNoLiveryLimit(ctx);
	ApplyPatch6and7RemoveModChecks(ctx);
	ApplyPathNodeCrashFix(ctx);
	ApplyPedModelsInCutscenesFix(ctx);
	ApplyPipeBombDropIconSupport(ctx);
	ApplyPlaneCrashFix(ctx);
	ApplyPlayerAsPedComponentsFix(ctx);
	ApplyPoliceEpisodicWeaponSupport(ctx);
	ApplyPopupHeadlightsFlag(ctx);
	ApplyRemoveAllWeaponsProtection(ctx);
	ApplyRemoveBoundingBoxCulling(ctx);
	ApplyRemoveRGSCLoginRequirement(ctx);
	ApplyRemoveStaticCarShadows(ctx);
	ApplyRemoveStaticCutsceneCarShadows(ctx);
	ApplyReversingLightFix(ctx);
	ApplySCOSignatureFix(ctx);
	ApplySMPA60Fix(ctx);
	ApplySavegameFix(ctx);
	ApplySkipIntro(ctx);
	ApplySkipLauncherOption(ctx);
	ApplySkipMenu(ctx);
	ApplySpeedupBugFix(ctx);
	ApplyTBoGTHeliHeightLimit(ctx);
	ApplyTeleportProtection(ctx);
	ApplyTryToSkipAllErrors(ctx);
	ApplyVRAMFix(ctx);
	ApplyWantedLevelCrashFix(ctx);
	ApplyPatch7RemoveModChecks(ctx);
	ApplySuperLODFix(ctx);
	ApplyDrivebyFOVFix(ctx);
	ApplyCrashMsgFix(ctx);
	ApplyBenchmarkFix(ctx);
	ApplyCarPartsShadowFix(ctx);
	ApplyForceCarHeadlightShadows(ctx);
	ApplyPatch4RemoveModChecks(ctx);
	ApplyFreezeCarFix(ctx);
	ApplyCarDynamicShadowFix(ctx);
	ApplyGarageModelFix(ctx);
	ApplyMovingSteeringWheelSupport(ctx);
	ApplyShakingEngineBlock(ctx);
	ApplyMovingPlaneParts(ctx);
	ApplyMouseFix(ctx);
	ApplyOutOfCommissionFix(ctx);
	ApplyHighQualityReflections(ctx);
	ApplyCutsceneFixes(ctx);
	ApplyRemoveGFWLUpdatePopup(ctx);
	ApplyCarDecalsFix(ctx);
	ApplyRestoreDeathMusic(ctx);
	ApplyCarDynamicShadowFixNoE(ctx);
	ApplyDualVehicleHeadlightsOld(ctx);
	ApplyDualVehicleHeadlightsNew(ctx);
	ApplyBorderlessWindowed(ctx);
	ApplyFastLoading(ctx);
	ApplyDoNotPauseOnMinimize(ctx);
	ApplyCarPartsShadowFixStandalone(ctx);
	ApplyIncreaseVehicleModelLimit(ctx);
	ApplyNoPhysicsSleep(ctx);
	ApplyNoImpostorHeightCutoff(ctx);
	ApplyStartupTimeFix(ctx);
	ApplyIncreasePtrNodes(ctx);
	ApplyIncreaseVehicleStructLimit(ctx);
	ApplyNoLODLightHeightCutoff(ctx);
	ApplyNoLoadingSleep(ctx);
	ApplyOverrideGTADatPath(ctx);
	ApplyVSyncFix(ctx);
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
