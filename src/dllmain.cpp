// ZolikaPatch ASI entry point.
//
// Everything runs synchronously from DllMain: the DRM has already rewritten the
// game's import-time code by the time a deferred worker would get to scan, so the
// signatures have to be matched now.
//
// The support library is being split into FusionFix-style modules under
// `source/` (one logical piece per .ixx); this TU keeps the entry point and the
// installer pipeline.

#include "pch.h"

#include "common.hxx"

#include "common.h"
#include "hooks.h"
#include "patches.h"
#include "rebased.h"
#include "xlive/xliveless.h"
import zp.common;
import zp.game;
import zp.patch;
import zp.log;
import zp.config;
import zp.useless_checks;
import zp.catalogue;
import zp.launcher;

char iniPath[MAX_PATH];

#if defined(ZP_DUMP_TOOL)
// The write-set tool's own image (see IsToolPointer below).
extern "C" IMAGE_DOS_HEADER __ImageBase;
#endif

namespace
{
#if defined(ZP_DUMP_TOOL)
// The dump never runs the game, so a pointer into the tool itself can never be
// used there. Publishing one would only record where the tool happened to load,
// which is not part of the installer logic, so the dump substitutes a fixed
// stand-in: the write still shows up, at the same site, with a value that does
// not depend on the tool's layout.
constexpr std::uint32_t kDumpToolPointer = 0xD0000000u;

bool IsToolPointer(std::uintptr_t address) noexcept
{
	const auto* base = reinterpret_cast<const std::uint8_t*>(&__ImageBase);
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(base);
	return address >= start && address < start + nt->OptionalHeader.SizeOfImage;
}
#endif

// A value an installer publishes for the game. The dump (ZP_DUMP_TOOL)
// substitutes a fixed stand-in for its own addresses; in the ASI every value is
// published unchanged.
std::uintptr_t PublishedAddress(std::uintptr_t value) noexcept
{
#if defined(ZP_DUMP_TOOL)
	return IsToolPointer(value) ? std::uintptr_t{kDumpToolPointer} : value;
#else
	return value;
#endif
}

// ---- loader --------------------------------------------------------------

constexpr std::uint64_t kOldDsoundLoaderSize = 91648ull;
constexpr std::uint32_t kUnsupportedVersionMin = 1201;
constexpr std::uint32_t kUnsupportedVersionMax = 12999;
constexpr DWORD kUnsupportedSleepMs = 500;
constexpr UINT kUnsupportedExitCode = E_UNEXPECTED;  // 0x8000FFFF
static_assert(kUnsupportedExitCode == 0x8000FFFFu);

// Recovered float values. The value is the source of truth: a float target
// takes it directly, and a uint32_t global/table (the bodies read/write those
// dwords) takes std::bit_cast. The static_asserts pin the recovered bit
// patterns, so a changed constant is a compile error.
constexpr float kFloatOne = 1.0f;
constexpr float kFloatQuarter = 0.25f;
constexpr float kFloatHalf = 0.5f;
constexpr float kDrivebyFov = 45.0f;
constexpr float kPiOver2 = 1.5707964f;  // the original's rounded pi/2
constexpr float kFchsOffset = 2.0f;
constexpr float kBikeSpeedThreshold = 0.0066666668f;
constexpr float kCutsceneEps = 0.05f;
constexpr float kCutsceneSpeedLimit = 0.5f;
constexpr double kCutsceneLimit = 0.3333;  // the original's stored double
constexpr double kCutsceneZero = 0.0;
static_assert(std::bit_cast<std::uint32_t>(kFloatOne) == 0x3F800000u);
static_assert(std::bit_cast<std::uint32_t>(kFloatQuarter) == 0x3E800000u);
static_assert(std::bit_cast<std::uint32_t>(kFloatHalf) == 0x3F000000u);
static_assert(std::bit_cast<std::uint32_t>(kDrivebyFov) == 0x42340000u);
static_assert(std::bit_cast<std::uint32_t>(kPiOver2) == 0x3FC90FDBu);
static_assert(std::bit_cast<std::uint32_t>(kFchsOffset) == 0x40000000u);
static_assert(std::bit_cast<std::uint32_t>(kBikeSpeedThreshold) == 0x3BDA740Eu);
static_assert(std::bit_cast<std::uint32_t>(kCutsceneEps) == 0x3D4CCCCDu);
static_assert(std::bit_cast<std::uint32_t>(kCutsceneSpeedLimit) == 0x3F000000u);
static_assert(std::bit_cast<std::uint64_t>(kCutsceneLimit) == 0x3FD554C985F06F69ull);
static_assert(std::bit_cast<std::uint64_t>(kCutsceneZero) == 0x0ull);
constexpr std::uint32_t kFloatAbsMask = 0x7FFFFFFFu;  // clears a float's sign bit
constexpr std::uint32_t kFloatSignBit = 0x80000000u;  // 1u << 31
static_assert(kFloatSignBit == (1u << 31));

// Opaque recovered layouts: the model registry's entry stride and the OOC
// record's 5-bit padding field (the numbers are the original's).
constexpr std::size_t kModelRegistryStride = 0xE0;
static_assert(kModelRegistryStride == 224u);
constexpr std::uint16_t kOocPadMask = 0x1Fu << 11;

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

// The 1.0.8.0 driveby resume RVA the diagnostic reconstructs the game base from
// (original signature: `E8 ? ? ? ?` at 0x85031C). Kept as a named value: the
// loader's own base is not passed to this path.
constexpr std::uint32_t kDrivebyResumeRva_1080 = 0x85031Fu;
// The model-table key the driveby diagnostic searches for (opaque: an entry id
// in the game's model table, not derivable from the name).
constexpr std::uint32_t kDrivebyModelKey = 0x9399528u;

// Structural values the arms and the compiled bodies would otherwise spell as
// bare hex. Each is a field offset, a size or a mask; the recovered literal is
// in the comment (research 07 §2).
constexpr std::uint16_t kClearedLowBit = 0xFFFEu;          // clears flag bit 0
// The two push immediates ForceCarHeadlightShadows' patterns embed (0x0504
// and 0x0104); the arm rewrites their two bytes.
constexpr std::uint16_t kShadowPushA = 0x0504u;
constexpr std::uint16_t kShadowPushB = 0x0104u;
constexpr std::uint8_t kMinimizePauseTick = 0x06u;  // DoNotPauseOnMinimize
constexpr std::size_t kSpawnsNameCapacity = 0x40u;  // ooc name buffer
constexpr std::uint32_t kFlagBit4 = 1u << 4;               // object flag bit 4
constexpr std::uint32_t kMaxModelCount = 0x100000u;        // model-table cap
constexpr std::uint32_t kModelRegistryCountOffset = 0x14u; // +0: count
constexpr std::uint32_t kModelEntryBoneOffset = 0x14u;     // name +0, bone +0x14
constexpr std::uint32_t kPedModelHashOffset = 0x3Cu;       // CPed model hash
constexpr std::uint32_t kVehicleComponentP1 = 0xB4u;        // ped+8 -> +0xB4
constexpr std::uint32_t kVehicleComponentRegistry = 0xCu;   // -> +0xC
constexpr std::uint32_t kNewFilesNameA = 0x10u;             // name, update A
constexpr std::uint32_t kNewFilesNameB = 0x10Fu;            // name, update B
constexpr std::size_t kNoLiveryNameCapacity = 0x10u;        // strcpy_s extent
constexpr std::size_t kEpisodeSuffixCapacity = 0x40u;       // "\\Episode%d\\"
constexpr std::uint8_t kUninitialisedHeapPoison = 0xCDu;    // MSVC debug CRT
constexpr std::uint8_t kZeroByte = 0x00u;                   // clear a byte
constexpr std::uint8_t kJeRel8Opcode = 0x74u;               // je rel8
constexpr std::uint8_t kShortJmpOpcode = 0xEBu;              // jmp rel8
constexpr std::uint32_t kTrueValue = 1u;                    // mov eax,1
constexpr std::uint8_t kMovssStorePrefix = 0xF3u;           // movss dword ptr
constexpr std::uint8_t kMovssTwoByteOpcode = 0x0Fu;
constexpr std::uint8_t kMovssStoreOpcode = 0x11u;
constexpr std::uint8_t kModrmXmm0Esi = 0x86u;   // /r 0x86: xmm0, [esi+disp32]
constexpr std::uint8_t kModrmXmm1Esi = 0x8Eu;   // /r 0x8E: xmm1, [esi+disp32]
constexpr std::uint8_t kFstpSt0Bytes[2] = {0xDD, 0xD8};  // fstp st(0)
constexpr std::uint8_t kMovEbxHead[2] = {0x8B, 0x5C};    // mov ebx,[esp+..] head
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
// The borderless arm's GWL_STYLE mask: the recovered 0xFF30FFFF clears the
// framed-window bits (caption, system menu, thick frame, minimise/maximise).
constexpr std::uint32_t kBorderlessStyleMask =
    ~static_cast<std::uint32_t>(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
                                WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
static_assert(kBorderlessStyleMask == 0xFF30FFFFu);
// The short-jump and je opcodes the NewSaves flag toggle chooses between.
constexpr std::size_t kOocPathCapacity = 0x44u;  // ooc name buffer
constexpr std::uint32_t kModelLimitIncrease = 0x100u;   // 0x100
constexpr std::uint32_t kForcedShadowSize = 0x100u;     // 0x100

// opaque: the OOC archive's build tag (the first dword of the original's
// embedded resource). provenance: ZolikaPatch_original.asi resource at
// 0x100B6798, IDA 2026-10-31; the self-test compares it byte-for-byte.
// why: it is a build-time tag of the original's resource, not a derivable value.
constexpr std::uint32_t kOocArchiveMagic = 0x86723DFFu;
constexpr std::uint16_t kOocHeaderPage = 0x10u;  // header words[0]
constexpr std::uint16_t kOocHeaderOp = 0xE9u;    // header words[1]
constexpr std::uint8_t kOocPakMarker = 0xFAu;    // second byte of the pak blob
constexpr std::uint8_t kOocRecordTag = 0xB0u;    // record[14]
constexpr std::uint8_t kOocRecordTag2 = 0x02u;   // record[15]
constexpr std::uint32_t kOocDecodedLength = 182272u;  // decoded stream length
// grade B: recomputed from the decoded blob; the literals are the proof values.
constexpr std::uint32_t kOocTableAdler = 0x41E794ABu;
constexpr std::uint32_t kOocArchiveFnv = 0xB5AF7B10u;

// opaque: model hashes the original handlers look up by hash (the game's
// name->hash result); provenance: ZolikaPatch_original.asi sub_100469A0 /
// sub_10044XXX, IDA 2026-10-31; why: the input strings are not in this repo.
constexpr std::uint32_t kSuperLodLookupHash = 0xAE4B15D6u;
constexpr std::uint32_t kSuperLodFallbackHash = 0xC703DB5Fu;
constexpr std::uint32_t kScriptCallHash = 0x79023CFCu;  // zpatch_script_call
constexpr std::uint32_t kPopupLightsHash = 0xEB9F21D3u;  // popuplights script
constexpr std::uint32_t kNewFilesRegistrarKey = 0x2C89A1F9u;


// NewSaves: header layout and the "SAVE" magic the reader checks.
constexpr std::size_t kSaveHeaderSize = 0x110u;
constexpr std::size_t kSaveMagicOffset = 0x0C;
// NewSaves' flag16 restore branch: 75 26 = jne +0x26 (the two bytes the original
// writes back over the forced short jump).
constexpr std::uint16_t kJneRel8Plus38 = 0x2675u;
constexpr std::uint32_t kSaveMagic = static_cast<std::uint32_t>('S') |
                                     (static_cast<std::uint32_t>('A') << 8) |
                                     (static_cast<std::uint32_t>('V') << 16) |
                                     (static_cast<std::uint32_t>('E') << 24);
static_assert(kSaveMagic == 0x45564153u);


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

// ---------------------------------------------------------------------------
// vehicle light tables
// ---------------------------------------------------------------------------

namespace
{
// One row of the two 512-slot tables: "Car<N>Name" or "Car<N>Angle"/"Car<N>Offset",
// N in 1..512, no leading zero. Anything else in the section is ignored, exactly
// like the per-slot lookups this replaces.
struct VehicleLightKey
{
	std::size_t slot; // 0-based
	bool isName;
};

bool ParseVehicleLightKey(std::string_view key, std::string_view valueSuffix,
                          VehicleLightKey& output) noexcept
{
	constexpr std::string_view kPrefix = "Car";
	if (key.size() <= kPrefix.size() || key.substr(0, kPrefix.size()) != kPrefix)
		return false;

	std::size_t at = kPrefix.size();
	const std::size_t digits = at;
	std::size_t number = 0;
	while (at < key.size() && key[at] >= '0' && key[at] <= '9')
	{
		number = number * 10 + static_cast<std::size_t>(key[at] - '0');
		if (number > 512)
			return false;
		++at;
	}
	if (at == digits || number == 0)
		return false;
	if (at - digits > 1 && key[digits] == '0')
		return false; // "Car01Name" never matched a slot before; keep that

	const std::string_view rest = key.substr(at);
	if (rest == "Name")
		output = {number - 1, true};
	else if (rest == valueSuffix)
		output = {number - 1, false};
	else
		return false;
	return true;
}
} // namespace

PatchResult LoadVehicleLightTables(const ConfigDocument& config,
                                   VehicleLightTables& output) noexcept
{
	try
	{
		VehicleLightTables parsed;
		constexpr std::string_view kSections[] = {"PopupLightAngles", "HeadlightConeOffsets"};
		constexpr std::string_view kValueSuffixes[] = {"Angle", "Offset"};

		// One pass over the document: the original asks 2 x 512 key pairs, but
		// the section only ever holds those rows, so walk them once and keep the
		// last entry per key (the reader's last-value-wins rule).
		const ConfigEntry* names[std::size(kSections)][512]{};
		const ConfigEntry* values[std::size(kSections)][512]{};
		for (const ConfigEntry& entry : config.entries)
		{
			std::size_t table = std::size(kSections);
			for (std::size_t candidate = 0; candidate < std::size(kSections); ++candidate)
			{
				if (entry.section == kSections[candidate])
				{
					table = candidate;
					break;
				}
			}
			if (table == std::size(kSections))
				continue;

			VehicleLightKey key{};
			if (!ParseVehicleLightKey(entry.key, kValueSuffixes[table], key))
				continue;
			if (key.isName)
				names[table][key.slot] = &entry;
			else
				values[table][key.slot] = &entry;
		}

		for (std::size_t table = 0; table < std::size(kSections); ++table)
		{
			auto& slots = table == 0 ? parsed.popupAngles : parsed.headlightOffsets;
			for (std::size_t i = 0; i < slots.size(); ++i)
			{
				const ConfigEntry* name = names[table][i];
				if (name == nullptr || name->value.empty())
					continue; // no row (an empty name is not a row, as before)

				// A row without its value, or with an unparseable one, is skipped:
				// the original's 512-slot loop keeps going (it has no way to reject
				// the whole file from inside the loader), so a single typo must not
				// disable every unrelated installer.
				const ConfigEntry* value = values[table][i];
				if (value == nullptr)
					continue;

				char* end = nullptr;
				errno = 0;
				const float numeric = std::strtof(value->value.c_str(), &end);
				if (end == value->value.c_str() || errno == ERANGE)
					continue;

				// The popup table is written in degrees and read in radians.
				VehicleLightValue& slot = slots[i];
				slot.modelHash = GameHash(name->value);
				slot.populated = true;
				slot.value = table == 0 ? static_cast<float>(numeric * 3.141592653589793 / 180.0)
				                        : numeric;
			}
		}

		output = parsed;
		return {};
	}
	catch (...)
	{
		return {PatchResultCode::invalid_input, "vehicle table allocation failed"};
	}
}

PopupLightSelection SelectPopupLight(const VehicleLightTables& tables,
                                     const std::array<std::uint32_t, 512>& supportedModels,
                                     int modelIndex, ModelIndexResolver resolve,
                                     void* context) noexcept
{
	PopupLightSelection result;
	if (!resolve || modelIndex < 0)
		return result;

	// One built-in special model is handled before the +popuplights list.
	constexpr std::uint32_t kSpecialScriptHash = 0xEB9F21D3u;
	const int specialIndex = resolve(kSpecialScriptHash, context);
	if (specialIndex >= 0 && specialIndex == modelIndex)
	{
		result.supported = true;
		return result;
	}

	for (const std::uint32_t hash : supportedModels)
	{
		if (!hash)
			break;
		if (resolve(hash, context) != modelIndex)
			continue;

		result.supported = true;
		// The original scans all 512 slots; the last duplicate wins.
		for (const VehicleLightValue& entry : tables.popupAngles)
		{
			if (entry.populated && entry.modelHash != 0 && entry.modelHash == hash)
				result.angleRadians = entry.value;
		}
		break;
	}
	return result;
}

float SelectHeadlightOffset(const VehicleLightTables& tables, int modelIndex,
                            ModelIndexResolver resolve, void* context) noexcept
{
	if (!resolve || modelIndex < 0)
		return 2.0f;

	// FUN_1001ad20 stops at the first zero hash rather than skipping holes.
	for (const VehicleLightValue& entry : tables.headlightOffsets)
	{
		if (!entry.modelHash)
			break;
		if (entry.populated && resolve(entry.modelHash, context) == modelIndex)
			return entry.value;
	}
	return 2.0f;
}

// ---------------------------------------------------------------------------
// IVMenuAPI
// ---------------------------------------------------------------------------

namespace
{
// The two toggles the shipped mod registers; the callbacks are the ABI bodies in
// the hook bodies.
const MenuRegistration kMenuRegistrations[] = {
	{"PREF_DEATHMUSIC", L"Death Music", "MENU_DISPLAY_ON_OFF", "PREF_MUSIC_VOLUME", 1, 0,
	 reinterpret_cast<std::uintptr_t>(&abi_menu_death_get),
	 reinterpret_cast<std::uintptr_t>(&abi_menu_death_set)},
	{"PREF_FOCUSLOSS", L"Pause Game On Focus Loss", "MENU_DISPLAY_ON_OFF", "PREF_VSYNC", 1, 0,
	 reinterpret_cast<std::uintptr_t>(&abi_menu_focus_get),
	 reinterpret_cast<std::uintptr_t>(&abi_menu_focus_set)},
};
} // namespace

MenuProbeResult ProbeMenuApi(bool optionEnabled, bool modulePresent, bool attemptAlreadyMade,
                             bool loadSucceeded) noexcept
{
	if (!optionEnabled)
		return MenuProbeResult::disabled;
	if (modulePresent)
		return MenuProbeResult::already_loaded;
	if (!attemptAlreadyMade && loadSucceeded)
		return MenuProbeResult::loaded;
	return MenuProbeResult::unavailable;
}

MenuProbeResult MenuIntegrationProbe(MenuIntegrationState& state, bool loadSucceeded) noexcept
{
	if (!state.optionEnabled)
		return MenuProbeResult::disabled;
	if (state.modulePresent)
		return MenuProbeResult::already_loaded;
	if (state.attemptMade)
		return MenuProbeResult::unavailable;

	++state.loadAttempts;
	if (loadSucceeded)
	{
		state.modulePresent = true;
		return MenuProbeResult::loaded;
	}

	state.attemptMade = true; // DAT_101a843c = 1, so it is not retried
	return MenuProbeResult::unavailable;
}

const MenuRegistration* MenuRegistrations(std::size_t& count) noexcept
{
	count = sizeof(kMenuRegistrations) / sizeof(kMenuRegistrations[0]);
	return kMenuRegistrations;
}

std::size_t RegisterMenuOptions() noexcept
{
	const HMODULE menu = GetModuleHandleA("IVMenuAPI.asi");
	if (!menu)
		return 0;

	using AddOptionFn =
		int(__cdecl*)(const char*, const wchar_t*, const char*, const char*, int, int, int, int);
	const auto add = reinterpret_cast<AddOptionFn>(
		reinterpret_cast<void*>(GetProcAddress(menu, "AddIVMenuOption")));
	if (!add)
		return 0;

	std::size_t count = 0;
	std::size_t total = 0;
	const MenuRegistration* rows = MenuRegistrations(count);
	for (std::size_t i = 0; i < count; ++i)
	{
		const MenuRegistration& row = rows[i];
		const int result = add(row.family, row.title, row.display, row.pref, row.a, row.b,
		                       static_cast<int>(row.getter), static_cast<int>(row.setter));
		if (result)
			++total;
	}
	return total;
}

// ---------------------------------------------------------------------------
// ZMenuIV guard
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Rebased globals: the live globals the hook bodies address by name.
// ---------------------------------------------------------------------------

// C linkage so the naked LoadDLCs body can address it from inline asm.
extern "C"
{
	std::uint32_t zpatch_dlc_load_callback = 0;              // dword_1015BDDC
	std::uint32_t zpatch_skip_intro_match = 0;               // dword_1015AD74
	std::uint32_t zpatch_skip_menu_resume = 0;               // dword_10159348
	std::uint8_t zpatch_skip_menu_flag = 0;                  // byte_101A8450
	std::uint32_t zpatch_blackscreen_resume = 0;             // dword_1015AD58
	std::uint32_t zpatch_blackscreen_value_ptr = 0;          // dword_1015AD5C
	std::uint32_t zpatch_path_node_resume_ok = 0;            // dword_1015BDFC
	std::uint32_t zpatch_path_node_resume_fail = 0;          // dword_1015BE00
	std::uint32_t zpatch_plane_crash_resume_a = 0;           // dword_1015BE18
	std::uint32_t zpatch_plane_crash_resume_b = 0;           // dword_1015BE14
	std::uint32_t zpatch_wanted_level_resume = 0;            // dword_1015AD6C
	std::uint32_t zpatch_group_hack_resume = 0;              // dword_1015AD60
	std::uint32_t zpatch_smpa60_resume_a = 0;                // dword_10159C70
	std::uint32_t zpatch_smpa60_resume_b = 0;                // dword_10159C68
	std::uint32_t zpatch_smpa60_resume_c = 0;                // dword_10159C6C (tail shim)
	std::uint32_t zpatch_freeze_gun_resume = 0;              // dword_1015AD64
	std::uint32_t zpatch_player_ped_object = 0;              // dword_10159D28
	std::uint32_t zpatch_player_ped_computed = 0;            // dword_10159D20
	std::uint32_t zpatch_player_ped_resume = 0;              // dword_10159D2C
	std::uint32_t zpatch_model_lookup = 0;                   // dword_10159C0C (shared)
	std::uint32_t zpatch_emissive_ecx = 0;                   // dword_10159CC0
	std::uint32_t zpatch_emissive_edx = 0;                   // dword_10159CCC
	std::uint32_t zpatch_emissive_const = 0;                 // dword_10159CC4
	std::uint32_t zpatch_emissive_resume = 0;                // dword_10159CC8
	std::uint32_t zpatch_carparts_resume_if = 0;             // dword_1015BDC8 (match+2)
	std::uint32_t zpatch_carparts_resume_else = 0;           // dword_1015BDC0 (match+0x45)
	std::uint32_t zpatch_super_lod_fail = 0;                 // dword_101580AC
	std::uint32_t zpatch_super_lod_ok = 0;                   // dword_101580B0
	std::uint32_t zpatch_super_lod_lookup = 0;               // dword_101580B4
	std::uint32_t zpatch_episode_switch_resume_a = 0;        // dword_10159BBC
	std::uint32_t zpatch_episode_switch_resume_b = 0;        // dword_10159BB4
	std::uint32_t zpatch_episode_switch_resume_c = 0;        // dword_10159BB8
	std::uint32_t zpatch_episode_flag = 2;                   // unk_10158128 (int 2)
	std::uint32_t zpatch_bike_value = 0;                     // dword_1015AD7C
	std::uint32_t zpatch_bike_saved = 0;                     // dword_1015AD80
	std::uint32_t zpatch_bike_resume = 0;                    // dword_1015AD84
	std::uint32_t zpatch_collision_deadlock_should_walk = 0; // dword_1015BDD4
	std::uint32_t zpatch_collision_deadlock_iterations = 0;  // dword_1015BDD8
	std::uint32_t zpatch_vram_arg_a = 0;                     // dword_1015D630
	std::uint32_t zpatch_vram_arg_b = 0;                     // dword_1015D634
	std::uint8_t zpatch_vram_flag = 0;                       // byte_1015D638
	std::uint32_t zpatch_driveby_resolver = 0;               // dword_1015BDE0
	std::uint32_t zpatch_driveby_resume = 0;                 // dword_1015BDF4
	// dword_101580C4 (45.0f, original on-disk)
	std::uint32_t zpatch_driveby_fov = std::bit_cast<std::uint32_t>(kDrivebyFov);
	std::uint32_t zpatch_driveby_cached = 0;           // dword_1015BDF8
	std::uint8_t zpatch_driveby_done = 0;              // byte_1015AD4A
	std::uint32_t zpatch_driveby_last_result = 0;      // resolver result (diagnostic)
	std::uint32_t zpatch_nolivery_name = 0;            // dword_10159BA8 (char*)
	std::uint32_t zpatch_nolivery_index = 0;           // dword_10159BB0
	std::uint32_t zpatch_nolivery_hash = 0;            // dword_10159B98
	std::uint32_t zpatch_nolivery_resume = 0;          // dword_10159B9C
	std::uint32_t zpatch_nolivery_fn = 0;              // dword_10159BA4
	std::uint32_t zpatch_nolivery_resume_d = 0;        // dword_10159BAC
	std::uint32_t zpatch_nolivery_arg = 0;             // dword_10159B94
	std::uint32_t zpatch_nolivery_count = 0;           // dword_10159B90
	std::uint32_t zpatch_nolivery_bufptr = 0;          // dword_10159BA0 (char*)
	std::uint32_t zpatch_nolivery_dest = 0;            // dword_10159B8C (char*)
	std::uint32_t zpatch_crash_msg_getter = 0;         // dword_1015AD68
	std::uint32_t zpatch_newfiles_register_slot = 0;   // dword_10159C08
	std::uint32_t zpatch_benchmark_resume_a = 0;       // dword_1015AD50
	std::uint32_t zpatch_benchmark_resume_b = 0;       // dword_1015AD54
	std::uint32_t zpatch_bds_hash_a = 0;               // dword_10159C1C
	std::uint32_t zpatch_bds_hash_b = 0;               // dword_10159C24
	std::uint32_t zpatch_bds_resume_a = 0;             // dword_10159C20
	std::uint32_t zpatch_bds_resume_b = 0;             // dword_10159C28
	std::uint32_t zpatch_copsirens_resume = 0;         // dword_1015937C
	std::uint32_t zpatch_copsirens_resume_b = 0;       // dword_10159374
	std::uint32_t zpatch_copsirens_index = 0;          // dword_10159370
	std::uint32_t zpatch_copsirens_index_b = 0;        // dword_1015936C
	std::uint32_t zpatch_copsirens_flag = 0;           // dword_10159B80
	std::uint32_t zpatch_copsirens_flag_b = 0;         // dword_10159378
	std::uint32_t zpatch_ide_string = 0;               // dword_1015BE08
	std::uint32_t zpatch_ide_model = 0;                // dword_1015BE0C
	std::uint32_t zpatch_ide_hook_slot = 0;            // dword_1015BE10
	std::uint32_t zpatch_copsirens_models[512] = {};   // dword_10159380
	std::uint32_t zpatch_popuplights_models[512] = {}; // dword_1015CE28
	std::uint32_t zpatch_wheelangle_models[512] = {};  // dword_1015A538
	std::uint32_t zpatch_wheelangle_angles[512] = {};  // dword_10159D38
	std::uint8_t zpatch_borderless_w_done = 0;         // byte_101A8445
	std::uint8_t zpatch_borderless_a_done = 0;         // byte_101A8444
	std::uint32_t zpatch_speedup_t4c = 0;              // dword_1015934C
	std::uint32_t zpatch_speedup_t50 = 0;              // dword_10159350
	std::uint32_t zpatch_speedup_t54 = 0;              // dword_10159354
	std::uint32_t zpatch_speedup_t58 = 0;              // dword_10159358
	std::uint32_t zpatch_speedup_t5c = 0;              // dword_1015935C
	std::uint32_t zpatch_speedup_t60 = 0;              // dword_10159360
	std::uint32_t zpatch_speedup_t64 = 0;              // dword_10159364
	std::uint32_t zpatch_speedup_t68 = 0;              // dword_10159368
	// OverrideGTADatPath: the original pushed its own "common:/data/gta.dat" global;
	// we push this buffer instead (filled from OverrideGTADatPathTo at load).
	char zpatch_override_gta_dat_path[260] = "common:/data/gta.dat";
	// PopupHeadlightsFlag (see rebased.h). dword_101580CC is pre-set to pi/2 on
	// disk and rewritten by the shim's checker on every call.
	std::uint32_t zpatch_popup_model = 0;
	std::uint32_t zpatch_popup_flag = 0;
	std::uint32_t zpatch_popup_index = 0;
	std::uint32_t zpatch_popup_angle = std::bit_cast<std::uint32_t>(kPiOver2);
	std::uint32_t zpatch_popup_resume_a = 0;
	std::uint32_t zpatch_popup_resume_b = 0;
	std::uint32_t zpatch_popup_hashes[512] = {};
	float zpatch_popup_angles[512] = {};
	// ForceCarHeadlightShadows: dword_101580C0 is pre-set to 2.0f on disk.
	std::uint32_t zpatch_fchs_param = 0;
	std::uint32_t zpatch_fchs_resume = 0;
	std::uint32_t zpatch_fchs_offset = std::bit_cast<std::uint32_t>(kFchsOffset);
	std::uint32_t zpatch_fchs_hashes[512] = {};
	float zpatch_fchs_offsets[512] = {};
	// BadPlayerModelCrashFix scratch.
	std::uint32_t zpatch_bpm_obj = 0;
	std::uint32_t zpatch_bpm_model_index = 0;
	std::uint32_t zpatch_bpm_resume = 0;
	// NewSavesCompatibility state.
	std::uint32_t zpatch_new_saves_src = 0;
	std::uint32_t zpatch_new_saves_resume = 0;
	std::uint32_t zpatch_new_saves_chunk_len = 0;
	std::uint8_t zpatch_new_saves_flag9 = 0;
	std::uint8_t zpatch_new_saves_flag13 = 0;
	std::uint8_t zpatch_new_saves_flag16 = 0;
	// Loader-preamble resolutions (unconditional in the original).
	std::uint32_t zpatch_pre_ped_array = 0;
	std::uint32_t zpatch_pre_fn_59c74 = 0;
	std::uint32_t zpatch_pre_fn_59b88 = 0;
	std::uint32_t zpatch_pre_fn_5d63c = 0;
	std::uint32_t zpatch_pre_fn_59cdc = 0;
	// Hash salts: the on-disk (probe-hit) values of the original's ten dwords.
	std::uint32_t zpatch_salt_28 = 0x6A6B4F18u;
	std::uint32_t zpatch_salt_3c = 0x022B2DA9u;
	std::uint32_t zpatch_salt_40 = 0x4BF36A32u;
	std::uint32_t zpatch_salt_50 = 0x3B0C6738u;
	std::uint32_t zpatch_salt_54 = 0x511454A9u;
	std::uint32_t zpatch_salt_5c = 0x62E319C6u;
	std::uint32_t zpatch_salt_a8 = 0x79A95BF9u;
	std::uint32_t zpatch_salt_b8 = 0x4E61480Au;
	std::uint32_t zpatch_salt_c8 = 0x570F5725u;
	std::uint32_t zpatch_salt_d4 = 0x75E005F1u;
	// Module-presence flags (set by the loader, exactly like the original).
	std::uint8_t zpatch_xliveless_present = 0;
	std::uint8_t zpatch_citizen_present = 0;
	std::uint32_t zpatch_xlive_base = 0;
	// Original one-time scan caches and the shared ASI TlsIndex.
	std::uint32_t zpatch_gfwl_scan_guard = 0;
	std::uint32_t zpatch_gfwl_site = 0;
	std::uint32_t zpatch_speedup_scan_guard = 0;
	std::uint32_t zpatch_speedup_clock = 0;
	std::uint32_t zpatch_tls_index = 0;
	// Live option-flag globals. Every one starts at 0: the loader arms only raise
	// a flag to 1, so an option that is off leaves it at 0.
	std::uint8_t zpatch_flag_death_music = 0u;      // byte_10159B86
	std::uint8_t zpatch_flag_patch4_modchecks = 0u; // byte_10159C14
	std::uint8_t zpatch_flag_gfwl_popup = 0u;       // byte_10159B87
	std::uint8_t zpatch_flag_debug_spawns = 0u;     // byte_10159C15
	std::uint8_t zpatch_flag_donotpause = 0u;       // byte_1015AD43
	// RestoreDeathMusic's lazily resolved site.
	std::uint32_t zpatch_death_music_site = 0;
	// NewSavesCompatibility's element counter.
	std::uint32_t zpatch_new_saves_element_count = 0;
	std::uint32_t zpatch_new_saves_loaddata = 0;
	std::uint32_t zpatch_new_saves_p3_orig = 0;
	std::uint32_t zpatch_gfwl_resume = 0;
	std::uint32_t zpatch_carparts_shadow_fn = 0; // dword_1015B5B8
	std::uint32_t zpatch_carparts_resume_2 = 0;  // dword_1015BDCC
	std::uint32_t zpatch_car_dynamic_d = 0;      // dword_1015BDC4
	// DualVehicleHeadlights.
	std::uint32_t zpatch_dual_bcc = 0;      // dword_10159BCC
	std::uint32_t zpatch_dual_bd0 = 0;      // dword_10159BD0
	std::uint32_t zpatch_dual_bd4 = 0;      // dword_10159BD4
	std::uint32_t zpatch_dual_be0 = 0;      // dword_10159BE0
	std::uint32_t zpatch_dual_be4 = 0;      // dword_10159BE4
	std::uint32_t zpatch_dual_light_a = 1u; // dword_1015802C (original on-disk 1)
	std::uint32_t zpatch_dual_light_b = 2u; // dword_10158030 (original on-disk 2)
	std::uint32_t zpatch_dual_r_eax = 0;    // dword_10159BC4
	std::uint32_t zpatch_dual_r_ebx = 0;    // dword_10159BEC
	std::uint32_t zpatch_dual_r_ecx = 0;    // dword_10159BD8
	std::uint32_t zpatch_dual_r_edx = 0;    // dword_10159BF0
	std::uint32_t zpatch_dual_r_esi = 0;    // dword_10159BC8
	std::uint32_t zpatch_dual_r_edi = 0;    // dword_10159BE8
	std::uint32_t zpatch_dual_r_ebp = 0;    // dword_10159BDC
	std::uint32_t zpatch_dual_r_esp = 0;    // dword_10159BF4
	// LocalSettingsPath.
	char zpatch_local_ansi_cwd[260] = {};
	wchar_t zpatch_local_wide_cwd[260] = {};
	char zpatch_local_iv_suffix[8] = "\\GTA IV";
	std::uint32_t zpatch_local_saves_slot = 0;
	std::uint32_t zpatch_local_path_08 = 0;
	std::uint32_t zpatch_local_path_10 = 0;
	std::uint32_t zpatch_local_episode_ptr = 0;
	std::uint32_t zpatch_local_path_18 = 0;
	std::uint32_t zpatch_local_path_1c = 0;
	// FreezeCarFix / DebugPrintSpawns.
	std::uint32_t zpatch_spawns_kind = std::numeric_limits<std::uint32_t>::max();
	std::uint32_t zpatch_spawns_class = 0;
	std::uint32_t zpatch_spawns_skip = 0;
	std::uint32_t zpatch_spawns_orig_a = 0;
	std::uint32_t zpatch_spawns_orig_b = 0;
	std::uint32_t zpatch_spawns_case0_resume = 0;
	std::uint32_t zpatch_spawns_case1_resume = 0;
	std::uint32_t zpatch_spawns_case2_resume = 0;
	std::uint32_t zpatch_spawns_case3_resume = 0;
	std::uint32_t zpatch_spawns_case4_resume = 0;
	std::uint32_t zpatch_spawns_case5_resume = 0;
	std::uint32_t zpatch_spawns_case6_resume = 0;
	std::uint32_t zpatch_spawns_case7_resume = 0;
	std::uint32_t zpatch_spawns_case8_resume = 0;
	std::uint32_t zpatch_spawns_case9_resume = 0;
	char zpatch_fmt_spawned_car[] = "%s spawned a car (%s)";
	char zpatch_fmt_nonspawned_car[] = "%s spawned a non-car car (%s)";
	char zpatch_fmt_spawned_heli[] = "%s spawned a heli (%s)";
	char zpatch_fmt_nonspawned_heli[] = "%s spawned a non-heli heli (%s)";
	char zpatch_fmt_spawned_boat[] = "%s spawned a boat (%s)";
	char zpatch_fmt_nonspawned_boat[] = "%s spawned a non-boat boat (%s)";
	char zpatch_fmt_spawned_bike[] = "%s spawned a bike (%s)";
	char zpatch_fmt_nonspawned_bike[] = "%s spawned a non-bike bike (%s)";
	char zpatch_fmt_spawned_train[] = "%s spawned a train (%s)";
	char zpatch_fmt_nonspawned_train[] = "%s spawned a non-train train (%s)";
	char zpatch_fmt_spawned_plane[] = "%s spawned a plane (%s)";
	char zpatch_fmt_nonspawned_plane[] = "%s spawned a non-plane plane (%s)";
	char zpatch_fmt_spawned_ped[] = "%s spawned a ped (%s)";
	char zpatch_fmt_nonspawned_ped[] = "%s spawned a non-ped ped (%s)";
	char zpatch_fmt_spawned_vehicle_object[] = "%s spawned a vehicle object (%s)";
	char zpatch_fmt_spawned_object[] = "%s spawned an object (%s)";
	char zpatch_fmt_spawned_generic[] = "%s spawned (%s)";
	char zpatch_fmt_used_nonped[] = "%s used a non-ped model (%s)";
	wchar_t zpatch_str_gtac[] = L"GTAC.dll";
	// The spawn probe's GetModuleHandleW call goes through this slot (MSVC's inline
	// assembler cannot name the @-decorated import pointer); the hook bodies fill it at load.
	std::uint32_t zpatch_spawns_getmodulehandlew = 0;
	std::uint32_t zpatch_garage_resume_a = 0;
	std::uint32_t zpatch_garage_resume_b = 0;
	std::uint32_t zpatch_steer_vehicle = 0;
	std::uint32_t zpatch_steer_resume = 0;
	std::uint32_t zpatch_steer_vtable_fn = 0;
	float zpatch_engine_last_timestep = 0.0f;
	float zpatch_engine_phase = 0.0f;
	std::uint8_t zpatch_engine_descending = 0;
	std::uint8_t zpatch_flag_steering = 0u; // byte_1015AD40 (on-disk 0)
	std::uint8_t zpatch_flag_engine = 0u;   // byte_1015AD41 (on-disk 0)
	std::uint8_t zpatch_flag_plane = 0u;    // byte_10159C17 (on-disk 0)
	// MouseFix (see the hook bodies). The constants are not const so the
	// inline assembler can address them as plain external symbols; nothing writes
	// them. Values are the original .rdata bits (hex floats keep them exact).
	std::uint32_t zpatch_mf_x = 0;
	std::uint32_t zpatch_mf_y = 0;
	std::uint32_t zpatch_mf_flag = 0;
	std::uint32_t zpatch_mf_time = 0;
	std::uint32_t zpatch_mf_resume_a = 0;
	std::uint32_t zpatch_mf_resume_b = 0;
	float zpatch_mf_xmm_save[8] = {};
	double zpatch_mf_scale = 0.0078125;
	double zpatch_mf_scale_255 = 255.0;
	float zpatch_mf_sign = 127.5f;
	float zpatch_mf_mul_neg1 = -1.0f;
	float zpatch_mf_step_scale = 0.1f;
	double zpatch_mf_mul_neg1_d = -1.0;
	double zpatch_mf_time_mul = 0x1.333334015bc21p-4;
	double zpatch_mf_time_add = 0.025;
	double zpatch_mf_pi = 0x1.921fb5a7ed197p+1;
	double zpatch_mf_two_pi = 0x1.921fb6134ce3ep+2;
	double zpatch_mf_neg_pi = -0x1.921fb5a7ed197p+1;
	// `xorps xmm, xmmword ptr [mask]` needs a 16-byte aligned address: the
	// original's .rdata constants are naturally aligned, so the masks are
	// alignas(16) (a misaligned mask faults with #GP/AV).
	alignas(16) unsigned char zpatch_mf_neg_mask[16] = {0, 0, 0, 0x80, 0, 0, 0, 0x80,
	                                                    0, 0, 0, 0x80, 0, 0, 0, 0x80};
	alignas(16) unsigned char zpatch_mf_double_sign[16] = {0, 0, 0, 0, 0, 0, 0, 0x80,
	                                                       0, 0, 0, 0, 0, 0, 0, 0x80};
	// OutOfCommissionFix (see the hook bodies).
	std::uint32_t zpatch_ooc_resume = 0;
	std::uint32_t zpatch_ooc_logger = 0;
	std::uint32_t zpatch_ooc_orig = 0;
	std::uint32_t zpatch_ooc_state = 0;
	std::uint32_t zpatch_ooc_input_bound = 0;
	std::uint32_t zpatch_ooc_output_begin = 0;
	std::uint32_t zpatch_ooc_output_cursor = 0;
	std::uint32_t zpatch_ooc_output_end = 0;
	std::uint32_t zpatch_ooc_table_size = 0;
	std::uint32_t zpatch_ooc_archive_offset = 0;
	std::uint32_t zpatch_ooc_vector_begin = 0;
	std::uint32_t zpatch_ooc_vector_end = 0;
	std::uint32_t zpatch_ooc_vector_capacity = 0;
	char zpatch_ooc_fmt_a[] = "mor";
	char zpatch_ooc_fmt_b[] = "me%sy%c%c%p,%d,%c%c%s";
	// HighQualityReflections (see the hook bodies).
	std::uint32_t zpatch_hqr_resume = 0;
	std::uint32_t zpatch_hqr_builder = 0;
	std::uint32_t zpatch_hqr_dump[8] = {};
	std::uint32_t zpatch_hqr_flags = 0x11Du; // the original's dword_10158060
	std::uint32_t zpatch_hqr_mode = 0;       // the original's BSS slot stays 0
	// CutsceneFixes chain B (see the hook bodies): the worker's two publishes, the body's
	// frame-time accumulator (the original's .data BSS, past the raw data, so 0) and
	// the read-only constants it compares against (original .rdata/.data bits).
	std::uint32_t zpatch_cutscene_target = 0;                    // dword_1015BDE4
	std::uint32_t zpatch_cutscene_poke = 0;                      // dword_1015BDF0
	std::uint64_t zpatch_cutscene_accum = 0;                     // qword_1015BDE8 (BSS: starts 0)
	std::uint64_t zpatch_cutscene_zero = std::bit_cast<std::uint64_t>(kCutsceneZero);
	std::uint64_t zpatch_cutscene_limit = std::bit_cast<std::uint64_t>(kCutsceneLimit);
	std::uint32_t zpatch_cutscene_abs_mask = kFloatAbsMask;           // 0x101523D0
	std::uint32_t zpatch_cutscene_eps_a = std::bit_cast<std::uint32_t>(kCutsceneEps);
	std::uint32_t zpatch_cutscene_eps_b = std::bit_cast<std::uint32_t>(kCutsceneSpeedLimit);
	// NewMemorySystem: the allocator singleton the handler's second scan publishes
	// (the immediate of `mov esi, 0x19cf9f0`); the E8 body writes our vtable there.
	std::uint32_t zpatch_nms_object = 0; // dword_10159CD8
}
namespace
{
// Rebased originals the handlers address directly: StartupTimeFix pushes the
// address of g_startup_time (the original pushed dword_101580d8).
std::uint32_t g_startup_time = 0;
std::uint32_t g_dlc_register_rva = 0;
std::uint32_t g_dlc_apply_rva = 0;
// VSyncFix: the game code reads/writes this dword through the operand the
// handler patches (the original pushed &unk_10159BC0).
std::uint32_t g_vsync_flag = 0;
} // namespace

// Copies `path` into the buffer the retargeted push reads, clamped and terminated.
void SetOverrideGtaDatPath(std::string_view path) noexcept
{
	if (path.empty())
		path = "common:/data/gta.dat";
	const std::size_t n = path.size() < sizeof(zpatch_override_gta_dat_path) - 1
	    ? path.size()
	    : sizeof(zpatch_override_gta_dat_path) - 1;
	std::memcpy(zpatch_override_gta_dat_path, path.data(), n);
	zpatch_override_gta_dat_path[n] = '\0';
}

// Accessors for the rebased globals whose address a handler publishes.
std::uint32_t& StartupTimeGlobal() noexcept
{
	return g_startup_time;
}
std::uint32_t& DlcRegisterRva() noexcept
{
	return g_dlc_register_rva;
}
std::uint32_t& DlcApplyRva() noexcept
{
	return g_dlc_apply_rva;
}

// Rebases a value that addresses the original module, keyed by INI option.
std::uintptr_t FindGlobalValue(std::string_view option) noexcept
{
	if (option == "StartupTimeFix")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&g_startup_time));
	if (option == "VSyncFix")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&g_vsync_flag));
	if (option == "OverrideGTADatPath")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(zpatch_override_gta_dat_path));
	// PopupHeadlightsFlag: the `fld` operand at +82 now loads our pi/2 float.
	if (option == "PopupHeadlightsFlag")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&zpatch_popup_angle));
	// ForceCarHeadlightShadows: the operand at +211 now loads our 2.0f dword.
	if (option == "ForceCarHeadlightShadows")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&zpatch_fchs_offset));
	// LocalSettingsPath: the path-helper call goes through our 4-byte slot.
	if (option == "LocalSettingsPath")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&zpatch_local_saves_slot));
	return 0;
}

// Named plugin-data symbols the sig-install `plugin_data_ptr` kind addresses
// (a 4-byte value written at the site: the ADDRESS of the named global).
std::uintptr_t FindGlobalSymbol(std::string_view symbol) noexcept
{
	if (symbol == "zpatch_local_iv_suffix")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(zpatch_local_iv_suffix));
	if (symbol == "zpatch_episode_flag")
		return PublishedAddress(reinterpret_cast<std::uintptr_t>(&zpatch_episode_flag));
	return 0;
}

// Slot publishes are keyed by the original global's name; 0 skips the publish.
std::uintptr_t FindGlobalSlot(std::string_view slot_name) noexcept
{
	// LoadDLCs: the callback slot receives the game's loader address; the two
	// constants are the register/apply RVAs the discovery routine adds to the
	// module base at use time.
	if (slot_name == "dword_1015BDDC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_dlc_load_callback);
	if (slot_name == "dword_101580D0")
		return reinterpret_cast<std::uintptr_t>(&g_dlc_apply_rva);
	if (slot_name == "dword_1015804C")
		return reinterpret_cast<std::uintptr_t>(&g_dlc_register_rva);
	if (slot_name == "dword_1015AD74")
		return reinterpret_cast<std::uintptr_t>(&zpatch_skip_intro_match);
	if (slot_name == "dword_10159348")
		return reinterpret_cast<std::uintptr_t>(&zpatch_skip_menu_resume);
	if (slot_name == "byte_101A8450")
		return reinterpret_cast<std::uintptr_t>(&zpatch_skip_menu_flag);
	if (slot_name == "dword_1015AD58")
		return reinterpret_cast<std::uintptr_t>(&zpatch_blackscreen_resume);
	if (slot_name == "dword_1015AD5C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_blackscreen_value_ptr);
	if (slot_name == "dword_1015BDFC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_path_node_resume_ok);
	if (slot_name == "dword_1015BE00")
		return reinterpret_cast<std::uintptr_t>(&zpatch_path_node_resume_fail);
	if (slot_name == "dword_1015BE18")
		return reinterpret_cast<std::uintptr_t>(&zpatch_plane_crash_resume_a);
	if (slot_name == "dword_1015BE14")
		return reinterpret_cast<std::uintptr_t>(&zpatch_plane_crash_resume_b);
	if (slot_name == "dword_1015AD6C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_wanted_level_resume);
	if (slot_name == "dword_1015AD60")
		return reinterpret_cast<std::uintptr_t>(&zpatch_group_hack_resume);
	if (slot_name == "dword_10159C70")
		return reinterpret_cast<std::uintptr_t>(&zpatch_smpa60_resume_a);
	if (slot_name == "dword_10159C68")
		return reinterpret_cast<std::uintptr_t>(&zpatch_smpa60_resume_b);
	if (slot_name == "dword_10159C6C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_smpa60_resume_c);
	if (slot_name == "dword_1015AD64")
		return reinterpret_cast<std::uintptr_t>(&zpatch_freeze_gun_resume);
	if (slot_name == "dword_10159D28")
		return reinterpret_cast<std::uintptr_t>(&zpatch_player_ped_object);
	if (slot_name == "dword_10159D20")
		return reinterpret_cast<std::uintptr_t>(&zpatch_player_ped_computed);
	if (slot_name == "dword_10159D2C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_player_ped_resume);
	if (slot_name == "dword_10159C0C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_model_lookup);
	if (slot_name == "dword_10159CC0")
		return reinterpret_cast<std::uintptr_t>(&zpatch_emissive_ecx);
	if (slot_name == "dword_10159CCC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_emissive_edx);
	if (slot_name == "dword_10159CC4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_emissive_const);
	if (slot_name == "dword_10159CC8")
		return reinterpret_cast<std::uintptr_t>(&zpatch_emissive_resume);
	if (slot_name == "dword_1015BDC8")
		return reinterpret_cast<std::uintptr_t>(&zpatch_carparts_resume_if);
	if (slot_name == "dword_1015BDC0")
		return reinterpret_cast<std::uintptr_t>(&zpatch_carparts_resume_else);
	if (slot_name == "dword_101580AC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_super_lod_fail);
	if (slot_name == "dword_101580B0")
		return reinterpret_cast<std::uintptr_t>(&zpatch_super_lod_ok);
	if (slot_name == "dword_101580B4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_super_lod_lookup);
	if (slot_name == "dword_10159BBC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_episode_switch_resume_a);
	if (slot_name == "dword_10159BB4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_episode_switch_resume_b);
	if (slot_name == "dword_10159BB8")
		return reinterpret_cast<std::uintptr_t>(&zpatch_episode_switch_resume_c);
	if (slot_name == "dword_1015AD7C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_bike_value);
	if (slot_name == "dword_1015AD80")
		return reinterpret_cast<std::uintptr_t>(&zpatch_bike_saved);
	if (slot_name == "dword_1015AD84")
		return reinterpret_cast<std::uintptr_t>(&zpatch_bike_resume);
	if (slot_name == "dword_1015BDD4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_collision_deadlock_should_walk);
	if (slot_name == "dword_1015BDE0")
		return reinterpret_cast<std::uintptr_t>(&zpatch_driveby_resolver);
	if (slot_name == "dword_1015BDF4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_driveby_resume);
	if (slot_name == "dword_1015BDE4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_cutscene_target);
	if (slot_name == "dword_1015BDF0")
		return reinterpret_cast<std::uintptr_t>(&zpatch_cutscene_poke);
	if (slot_name == "dword_10159CD8")
		return reinterpret_cast<std::uintptr_t>(&zpatch_nms_object);
	if (slot_name == "dword_10159B9C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_nolivery_resume);
	if (slot_name == "dword_10159BA4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_nolivery_fn);
	if (slot_name == "dword_10159BAC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_nolivery_resume_d);
	if (slot_name == "dword_1015AD68")
		return reinterpret_cast<std::uintptr_t>(&zpatch_crash_msg_getter);
	if (slot_name == "dword_10159C08")
		return reinterpret_cast<std::uintptr_t>(&zpatch_newfiles_register_slot);
	if (slot_name == "dword_1015AD50")
		return reinterpret_cast<std::uintptr_t>(&zpatch_benchmark_resume_a);
	if (slot_name == "dword_1015AD54")
		return reinterpret_cast<std::uintptr_t>(&zpatch_benchmark_resume_b);
	if (slot_name == "dword_10159C1C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_bds_hash_a);
	if (slot_name == "dword_10159C24")
		return reinterpret_cast<std::uintptr_t>(&zpatch_bds_hash_b);
	if (slot_name == "dword_10159C20")
		return reinterpret_cast<std::uintptr_t>(&zpatch_bds_resume_a);
	if (slot_name == "dword_10159C28")
		return reinterpret_cast<std::uintptr_t>(&zpatch_bds_resume_b);
	if (slot_name == "dword_1015937C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_copsirens_resume);
	if (slot_name == "dword_10159374")
		return reinterpret_cast<std::uintptr_t>(&zpatch_copsirens_resume_b);
	if (slot_name == "dword_1015BE10")
		return reinterpret_cast<std::uintptr_t>(&zpatch_ide_hook_slot);
	if (slot_name == "dword_1015934C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_speedup_t4c);
	if (slot_name == "dword_10159350")
		return reinterpret_cast<std::uintptr_t>(&zpatch_speedup_t50);
	if (slot_name == "dword_10159354")
		return reinterpret_cast<std::uintptr_t>(&zpatch_speedup_t54);
	if (slot_name == "dword_10159358")
		return reinterpret_cast<std::uintptr_t>(&zpatch_speedup_t58);
	if (slot_name == "dword_1015935C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_speedup_t5c);
	if (slot_name == "dword_10159360")
		return reinterpret_cast<std::uintptr_t>(&zpatch_speedup_t60);
	if (slot_name == "dword_10159364")
		return reinterpret_cast<std::uintptr_t>(&zpatch_speedup_t64);
	if (slot_name == "dword_10159368")
		return reinterpret_cast<std::uintptr_t>(&zpatch_speedup_t68);
	// PopupHeadlightsFlag resumes (match+12 / match+10) and the checker's tables.
	if (slot_name == "dword_1015D628")
		return reinterpret_cast<std::uintptr_t>(&zpatch_popup_resume_a);
	if (slot_name == "dword_1015D62C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_popup_resume_b);
	// ForceCarHeadlightShadows resume (match+12 of its second pattern).
	if (slot_name == "dword_1015B5BC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_fchs_resume);
	// BadPlayerModelCrashFix resume (match+10).
	if (slot_name == "dword_10159CB8")
		return reinterpret_cast<std::uintptr_t>(&zpatch_bpm_resume);
	// NewSavesCompatibility: Src is captured from the game's push immediate;
	// dword_1015AD88 is the tramp resume.
	if (slot_name == "Src")
		return reinterpret_cast<std::uintptr_t>(&zpatch_new_saves_src);
	if (slot_name == "dword_1015AD88")
		return reinterpret_cast<std::uintptr_t>(&zpatch_new_saves_resume);
	if (slot_name == "dword_10159C18")
		return reinterpret_cast<std::uintptr_t>(&zpatch_gfwl_resume);
	if (slot_name == "dword_1015B5B8")
		return reinterpret_cast<std::uintptr_t>(&zpatch_carparts_shadow_fn);
	if (slot_name == "dword_1015BDC4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_car_dynamic_d);
	if (slot_name == "dword_1015BDCC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_carparts_resume_2);
	// DualVehicleHeadlights's five published game pointers.
	if (slot_name == "dword_10159BCC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_dual_bcc);
	if (slot_name == "dword_10159BD0")
		return reinterpret_cast<std::uintptr_t>(&zpatch_dual_bd0);
	if (slot_name == "dword_10159BD4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_dual_bd4);
	if (slot_name == "dword_10159BE0")
		return reinterpret_cast<std::uintptr_t>(&zpatch_dual_be0);
	if (slot_name == "dword_10159BE4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_dual_be4);
	// LocalSettingsPath's five game path pointers.
	if (slot_name == "dword_10159D08")
		return reinterpret_cast<std::uintptr_t>(&zpatch_local_path_08);
	if (slot_name == "dword_10159D10")
		return reinterpret_cast<std::uintptr_t>(&zpatch_local_path_10);
	if (slot_name == "dword_10159D14")
		return reinterpret_cast<std::uintptr_t>(&zpatch_local_episode_ptr);
	if (slot_name == "dword_10159D18")
		return reinterpret_cast<std::uintptr_t>(&zpatch_local_path_18);
	if (slot_name == "dword_10159D1C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_local_path_1c);
	// FreezeCarFix / DebugPrintSpawns resume slots.
	if (slot_name == "dword_10159CB0")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_orig_a);
	if (slot_name == "dword_10159C98")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_orig_b);
	if (slot_name == "dword_10159CBC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_case0_resume);
	if (slot_name == "dword_10159C9C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_case1_resume);
	if (slot_name == "dword_10159CA0")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_case2_resume);
	if (slot_name == "dword_10159CA8")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_case3_resume);
	if (slot_name == "dword_10159C7C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_case4_resume);
	if (slot_name == "dword_10159C88")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_case5_resume);
	if (slot_name == "dword_10159CAC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_case6_resume);
	if (slot_name == "dword_10159C90")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_case7_resume);
	if (slot_name == "dword_10159C94")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_case8_resume);
	if (slot_name == "dword_10159C84")
		return reinterpret_cast<std::uintptr_t>(&zpatch_spawns_case9_resume);
	// GarageModelFix resumes.
	if (slot_name == "dword_10159CD4")
		return reinterpret_cast<std::uintptr_t>(&zpatch_garage_resume_a);
	if (slot_name == "dword_10159CD0")
		return reinterpret_cast<std::uintptr_t>(&zpatch_garage_resume_b);
	// Trio resumes / call target.
	if (slot_name == "dword_1015AD44")
		return reinterpret_cast<std::uintptr_t>(&zpatch_steer_resume);
	if (slot_name == "dword_1015AD4C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_steer_vtable_fn);
	// MouseFix publishes (the callback's slot writes).
	if (slot_name == "dword_10159C50")
		return reinterpret_cast<std::uintptr_t>(&zpatch_mf_x);
	if (slot_name == "dword_10159C3C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_mf_y);
	if (slot_name == "dword_10159C5C")
		return reinterpret_cast<std::uintptr_t>(&zpatch_mf_flag);
	if (slot_name == "dword_10159C34")
		return reinterpret_cast<std::uintptr_t>(&zpatch_mf_time);
	if (slot_name == "dword_10159C60")
		return reinterpret_cast<std::uintptr_t>(&zpatch_mf_resume_a);
	if (slot_name == "dword_10159C40")
		return reinterpret_cast<std::uintptr_t>(&zpatch_mf_resume_b);
	// OutOfCommissionFix publishes.
	if (slot_name == "dword_10158034")
		return reinterpret_cast<std::uintptr_t>(&zpatch_ooc_resume);
	if (slot_name == "dword_10159BF8")
		return reinterpret_cast<std::uintptr_t>(&zpatch_ooc_logger);
	if (slot_name == "dword_10159C04")
		return reinterpret_cast<std::uintptr_t>(&zpatch_ooc_orig);
	// HighQualityReflections publishes.
	if (slot_name == "dword_10159CFC")
		return reinterpret_cast<std::uintptr_t>(&zpatch_hqr_resume);
	if (slot_name == "dword_10159D04")
		return reinterpret_cast<std::uintptr_t>(&zpatch_hqr_builder);
	// SuperLODFix's resume slots are not rebased yet (their ABI body is pending);
	// the publishes are skipped and counted.
	return 0;
}

// ---------------------------------------------------------------------------
// Hook bodies: the game jumps or calls straight into these.
// ---------------------------------------------------------------------------

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>

#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <xmmintrin.h>
#include <cstring>
#include <new>
#include <string>
#include <unordered_set>
#include <intrin.h>

#include "common.h"
#include "rebased.h"
#include "driveby_seh.h"

// The original ASI's compressed "finale2.sco" resource (OutOfCommissionFix's
// payload); extracted from the shipped mod.
#include "ooc_payload.h"

// Game structure offsets used by the helpers below; the inline asm keeps the
// numeric operands because MASM needs compile-time constants.
constexpr std::size_t kModelIndexOffset = 0x2E;
constexpr std::size_t kVehicleSteeringOffset = 0x10D8;
constexpr std::size_t kVehicleBoneOffset = 0xE18;
constexpr std::size_t kVehicleLockOwnerOffset = 0x84;
constexpr std::size_t kVehicleRpmOffset = 0x10C8;
constexpr std::size_t kVehicleFlagsOffset = 0xF64;
constexpr std::size_t kVehicleEngineStateOffset = 0x1354;
constexpr std::size_t kVehicleSuspensionTravelOffset = 0x1F3C;
constexpr std::size_t kVehiclePitchOffset = 0x1F08;
constexpr std::size_t kVehicleRollOffset = 0x1F04;
constexpr std::size_t kVehicleHeaveOffset = 0x1F00;
constexpr std::uint32_t kModelTablePointerRva = 0x150FDD0u;
constexpr std::uint32_t kModelTableCountRva = 0x150FDD4u;

// The bodies below are 32-bit assembly. The x64 write-set dump never runs
// them - it only needs their addresses, which tests/stub_bodies.cpp provides -
// so the whole assembly region is compiled out for it.
#if !defined(ZP_DUMP_TOOL)

extern "C"
{

// PlayerAsPedComponentsFix helper (original sub_1003D930): compare the object's
// model (+0x2E) against the "player" model index. The shim stores the object
// (EAX) first; C linkage so the shim's asm can call it by name.
static void zpatch_cookie_tail() noexcept;  // defined with the cookie shim

std::int32_t abi_player_ped_helper() noexcept
{
	std::int32_t index = -1;
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	// 0x1003D960 `call dword_10159C0C` is unguarded, so a null lookup faults.
	zpatch_player_ped_computed = 0;  // original writes 0 before the lookup
	lookup(GameHash("player"), &index);
	const auto* object = reinterpret_cast<const std::uint8_t*>(
	    static_cast<std::uintptr_t>(zpatch_player_ped_object));
	const std::int16_t model = *reinterpret_cast<const std::int16_t*>(object + kModelIndexOffset);
	zpatch_player_ped_computed = (index == model) ? 1u : 0u;
	// Tail contract: EDX=1 (the cmove source), ECX=cookie via the /GS check,
	// EAX=the object's model (the movsx eax,[eax+2Eh] the resume sees).
	__asm { mov edx, 1 }
	zpatch_cookie_tail();
	return model;
}

// EmissiveLerpFix has no compiled helpers: the original tramp (sub_1001AF20)
// is integer math plus a cvtsi2ss/mulss tail that must touch only xmm0 (the
// game keeps xmm1..xmm7 live across the site).

// SuperLODFix helper (original sub_100469A0): resolve 0xAE4B15D6 through the
// handler-published lookup (dword_101580B4) and compare the object's model
// (+0x2E). The object is the stack argument the shim pushes.
bool abi_super_lod_helper(const void* object) noexcept
{
	std::int32_t index = -1;
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_super_lod_lookup));
	lookup(kSuperLodLookupHash, &index);  // sub_100469A0's call is unguarded
	const auto* o = static_cast<const std::uint8_t*>(object);
	return *reinterpret_cast<const std::int16_t*>(o + kModelIndexOffset) == index;
}

// HighFPSBikePhysicsFix helper (original sub_1001EF30): clamp zpatch_bike_value
// below 0.006666666665 to 0x3BDA740E. SSE in the original.
void abi_bike_physics_helper() noexcept
{
	float value = 0.0f;
	std::memcpy(&value, &zpatch_bike_value, sizeof(value));
	if (0.006666666665 > static_cast<double>(value))
		zpatch_bike_value = std::bit_cast<std::uint32_t>(kBikeSpeedThreshold);
}

// PopupHeadlightsFlag checker (original sub_10044310), called between
// pushad/popad: look up 0xEB9F21D3 ("popuplights"), walk the +popuplights list,
// then copy the last matching [PopupLightAngles] angle (starts at pi/2). A
// failed lookup leaves -1, which matches a model of -1, as in the original.
void abi_popup_check() noexcept
{
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	zpatch_popup_angle = std::bit_cast<std::uint32_t>(kPiOver2);  // dword_101580CC: pi/2
	zpatch_popup_flag = 0;             // dword_1015BE24
	std::int32_t out = -1;
	lookup(kPopupLightsHash, &out);  // sub_10044310's call is unguarded
	zpatch_popup_index = static_cast<std::uint32_t>(out);  // dword_1015BE1C
	if (out == static_cast<std::int32_t>(zpatch_popup_model))
	{
		zpatch_popup_flag = 1;
		return;
	}
	for (std::uint32_t i = 0; i < 512; ++i)
	{
		const std::uint32_t hash = zpatch_popuplights_models[i];
		if (hash == 0) return;
		out = -1;
		lookup(hash, &out);
		if (out != static_cast<std::int32_t>(zpatch_popup_model)) continue;
		zpatch_popup_flag = 1;
		for (std::uint32_t k = 0; k < 512; ++k)
		{
			if (zpatch_popup_hashes[k] != 0 &&
			    zpatch_popup_hashes[k] == hash)
			{
				std::uint32_t bits = 0;
				std::memcpy(&bits, &zpatch_popup_angles[k], sizeof(bits));
				zpatch_popup_angle = bits;
			}
		}
		return;
	}
}

// ForceCarHeadlightShadows checker (original sub_1001AD20), called between
// pushad/popad: walk the [HeadlightConeOffsets] table (dword_1015ADB8) and
// publish the first matching offset (dword_1015B5C0) into dword_101580C0;
// default 2.0f. The lookup and the object's model (+0x2E) are unguarded.
void abi_fchs_check() noexcept
{
	const std::uint32_t* const hashes = zpatch_fchs_hashes;
	std::uint32_t hash = hashes[0];
	zpatch_fchs_offset = std::bit_cast<std::uint32_t>(kFchsOffset);  // 2.0f
	if (hash == 0u) return;
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	// 0x1001AD5C `call dword_10159C0C` and 0x1001AD62 `movsx eax,word [v2+2Eh]`
	// are unguarded in the original.
	const auto* base = reinterpret_cast<const std::uint8_t*>(
	    static_cast<std::uintptr_t>(zpatch_fchs_param));
	for (std::uint32_t i = 0;;)
	{
		std::int32_t index = -1;
		lookup(hash, &index);
		if (static_cast<std::int32_t>(
		        *reinterpret_cast<const std::int16_t*>(base + kModelIndexOffset)) == index)
		{
			float value = 0.0f;
			std::memcpy(&value, &zpatch_fchs_offsets[i], sizeof(value));
			std::memcpy(&zpatch_fchs_offset, &value, sizeof(value));
			return;
		}
		++i;
		// The original walks into adjacent .data instead; layout-specific and
		// unreproducible.
		if (i == 512u) return;
		hash = hashes[i];
		if (hash == 0u) return;
	}
}

// Native-script "call by hash" layer (sub_100038E0 and the sub_1005Bxxx
// wrappers): the resolver (dword_1015BDE0, game 0x626AF0 on 1.0.8.0) takes the
// hash in ECX/ESI and the 0xA4-byte call block in EDX; the handler is cdecl with
// one argument. The original always returns (char)1 and skips a null handler.
extern "C" std::uint32_t zpatch_script_resolve(std::uint32_t hash);
extern "C" std::uint32_t zpatch_script_resolve_with_block(std::uint32_t hash,
                                                           void* block);
extern "C" std::uint32_t __cdecl zpatch_script_resolve_seh(std::uint32_t hash,
                                                            std::uint32_t edx_arg);
// Unmangled so the inline asm can call it (the declaration is in namespace
// zpatch).
__declspec(naked) std::uint32_t zpatch_script_resolve(std::uint32_t)
{
	// Naked: EDX carries the caller's block, so push it explicitly and keep the
	// original's cleanup.
	__asm {
		push edx
		push dword ptr [esp + 8]
		call zpatch_script_resolve_seh
		add  esp, 8
		ret
	}
}
__declspec(naked) std::uint32_t zpatch_script_resolve_with_block(std::uint32_t,
                                                                  void*)
{
	// Naked: hash goes in ECX, block in EDX; ESI must survive the call.
	__asm {
		push esi
		mov  esi, dword ptr [esp + 8]
		mov  ecx, esi
		mov  edx, dword ptr [esp + 0Ch]
		mov  eax, DWORD PTR [zpatch_driveby_resolver]
		call eax
		pop  esi
		ret
	}
}

static std::uint32_t __cdecl zpatch_resolver_call(std::uint32_t hash,
                                                  std::uint32_t edx_arg)
{
	return zpatch_script_resolve_with_block(
	    hash, reinterpret_cast<void*>(static_cast<std::uintptr_t>(edx_arg)));
}

extern "C" __declspec(noinline) std::uint32_t __cdecl
zpatch_script_resolve_seh(std::uint32_t hash, std::uint32_t edx_arg)
{
	return detail::driveby_resolve_seh(&zpatch_resolver_call, hash, edx_arg);
}

extern "C" __declspec(noinline) std::uint32_t __cdecl
zpatch_driveby_resolve_seh(std::uint32_t hash, std::uint32_t edx_arg)
{
	return zpatch_script_resolve_seh(hash, edx_arg);
}

struct zpatch_native_block final
{
	std::uint32_t m_p_return;   // +0x00 = &args
	std::uint32_t m_arg_count;  // +0x04
	std::uint32_t m_p_args;     // +0x08 = &args
	std::uint32_t m_out_count;  // +0x0C
	std::uint32_t m_out_ptrs[4];  // +0x10
	struct record final { std::uint32_t v[3]; std::uint32_t padding; };
	record m_records[4];        // +0x20 (sub_100038C0 zeroes v[0..2] of each)
	std::uint32_t args[17];     // +0x60 (arg 0 is the return slot)
};

extern "C" char zpatch_script_call_by_hash(std::uint32_t hash, void* block) noexcept
{
	const auto handler = reinterpret_cast<void(__cdecl*)(void*)>(
	    static_cast<std::uintptr_t>(zpatch_script_resolve_with_block(hash, block)));
	if (handler) handler(block);
	return 1;
}

// One builder covers every wrapper: the sub_1005DE40/DE80 cleanup loops are
// identical (copy out each pending record, then return args[0]; DE80 returns
// only its low byte).
extern "C" std::uint32_t zpatch_script_call(std::uint32_t hash, std::uint32_t nargs,
                                            const std::uint32_t* args) noexcept
{
	zpatch_native_block block;
	// The original clears 0xA0 bytes (args[16] stays dirty; preserved).
	std::memset(&block, 0, offsetof(zpatch_native_block, args[16]));
	for (auto& record : block.m_records)
	{
		record.v[0] = 0;
		record.v[1] = 0;
		record.v[2] = 0;
	}
	const auto base = static_cast<std::uint32_t>(
	    reinterpret_cast<std::uintptr_t>(block.args));
	block.m_p_return = base;
	block.m_p_args = base;
	block.m_arg_count = nargs;
	for (std::uint32_t i = 0; i < nargs && i < 17u; ++i) block.args[i] = args[i];
	zpatch_script_call_by_hash(hash, &block);
	while (block.m_out_count != 0u)
	{
		--block.m_out_count;
		auto* destination = reinterpret_cast<std::uint32_t*>(
		    static_cast<std::uintptr_t>(block.m_out_ptrs[block.m_out_count]));
		const auto& source = block.m_records[block.m_out_count];
		destination[0] = source.v[0];
		destination[1] = source.v[1];
		destination[2] = source.v[2];
	}
	return block.args[0];
}

extern "C" std::uint8_t zpatch_script_call_bool(std::uint32_t hash, std::uint32_t nargs,
                                                const std::uint32_t* args) noexcept
{
	return static_cast<std::uint8_t>(zpatch_script_call(hash, nargs, args));
}

// The MP-only diagnostic name lookup of sub_10003F00 walks the original ASI's
// own 38455-entry model-name table (0x100DE770); single-player never reaches
// this path (the caller is gated on GTAC.dll).
extern "C" const char* zpatch_bpm_model_name(std::uint32_t hash) noexcept
{
	// Return the first table name whose GameHash matches; "**UNKNOWN**" on a miss.
	return zp::ModelNameFromHash(hash).data();
}

// FreezeCarFix / DebugPrintSpawns helper shims: the original's helpers take
// their single argument in ECX or EDX. The probe body calls these by name.
extern "C" const char* zpatch_spawns_name_ecx();  // sub_10003F00 twin, arg in ECX
__declspec(naked) const char* zpatch_spawns_name_ecx()
{
	__asm {
		push ecx
		call zpatch_bpm_model_name
		add  esp, 4
		ret
	}
}

extern "C" const char* zpatch_spawns_who_edx();  // sub_1005B290 twin, arg in EDX
namespace
{
const char* zpatch_spawns_who_c(std::uint32_t id) noexcept
{
	// The original reads the native hash from [0x101580C8] (zpatch_salt_c8); the
	// loader's probe-miss fallback rewrites that slot on 1000/1010/1040.
	return reinterpret_cast<const char*>(static_cast<std::uintptr_t>(
	    zpatch_script_call(zpatch_salt_c8, 1, &id)));
}
}  // namespace
__declspec(naked) const char* zpatch_spawns_who_edx()
{
	__asm {
		push edx
		call zpatch_spawns_who_c
		add  esp, 4
		ret
	}
}

extern "C" void zpatch_spawns_print_edx();  // sub_1005B530 twin, arg in EDX
namespace
{
void zpatch_spawns_print_c(const char* text) noexcept
{
	const std::uint32_t arg = static_cast<std::uint32_t>(
	    reinterpret_cast<std::uintptr_t>(text));
	zpatch_script_call(kScriptCallHash, 1, &arg);
}
}  // namespace
__declspec(naked) void zpatch_spawns_print_edx()
{
	__asm {
		push edx
		call zpatch_spawns_print_c
		add  esp, 4
		ret
	}
}

// sub_10001280: the original's bounded formatter (buf, size, fmt, ...).
extern "C" int zpatch_spawns_sprintf(char* buffer, std::size_t size,
                                     const char* format, ...)
{
	va_list args;
	va_start(args, format);
	const int result = _vsnprintf_s(buffer, size, _TRUNCATE, format, args);
	va_end(args);
	return result;
}

// sub_1005FE7B: the /GS cookie check (cookie in ECX). It is the last call on
// the probe's return path, so it must preserve EAX (the model index); the
// original is `cmp ecx,[0x1015800C]; jne fail; ret`.
extern "C" void zpatch_spawns_cookie_check_ecx();
extern "C" std::uintptr_t __security_cookie;
__declspec(naked) void zpatch_spawns_cookie_check_ecx()
{
	__asm {
		cmp ecx, dword ptr [__security_cookie]
		jne spawns_cookie_fail
		ret
	spawns_cookie_fail:
		mov ecx, 2
		int 29h
	}
}

// The original helpers compiled with /GS end with
//   mov ecx,[ebp-N]; xor ecx,ebp; call <cookie check>; ...
// and the shim resumes *without* restoring EFLAGS (pushal/popal do not), so the
// resume sees the flags from `cmp ecx,[__security_cookie]`; keep that tail.
static void zpatch_cookie_tail() noexcept
{
	__asm {
		mov ecx, dword ptr [__security_cookie]
		call zpatch_spawns_cookie_check_ecx
	}
}

// BadPlayerModelCrashFix's check (original sub_10037AC0): if the model at
// zpatch_bpm_model_index is not a ped (class id != 6), fall back to
// "m_y_multiplayer". Called between pushad/popad, so GPR clobbering is fine.
extern "C" void abi_bpm_check() noexcept
{
	const auto* models = reinterpret_cast<void* const*>(
	    static_cast<std::uintptr_t>(zpatch_pre_ped_array));
	void* object = models[zpatch_bpm_model_index];
	if (object == nullptr) return;
	void** vtable = *reinterpret_cast<void***>(object);
	// sub_10037AC0 leaves ECX = the models-table base (dword_1015AD78) before
	// `call eax`; preserve that contract (odd as it looks).
	const char class_id = reinterpret_cast<char(__thiscall*)(void*)>(vtable[3])(
	    const_cast<void*>(reinterpret_cast<const void*>(models)));
	if (class_id == 6) return;
	if (GetModuleHandleW(L"GTAC.dll") != nullptr)
	{
		const std::uint32_t modelHash =
		    *reinterpret_cast<const std::uint32_t*>(
		        static_cast<const std::uint8_t*>(object) + kPedModelHashOffset);
		const char* model_name = zpatch_bpm_model_name(modelHash);
		std::uint32_t entity = zpatch_bpm_obj;
		const char* who = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(
		    zpatch_script_call(zpatch_salt_c8, 1, &entity)));
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%s used a non-ped model (%s)", who,
		              model_name);
		const std::uint32_t text = static_cast<std::uint32_t>(
		    reinterpret_cast<std::uintptr_t>(buffer));
		zpatch_script_call(kScriptCallHash, 1, &text);
	}
	const std::uint32_t hash = GameHash("m_y_multiplayer");
	if (zpatch_script_call_bool(zpatch_salt_b8, 1, &hash) == 0)
	{
		reinterpret_cast<void(__cdecl*)(std::uint32_t, std::uint32_t)>(
		    static_cast<std::uintptr_t>(zpatch_pre_fn_59c74))(hash, 0);
		zpatch_script_call(zpatch_salt_40, 0, nullptr);
	}
	std::int32_t new_index = -1;
	reinterpret_cast<void(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup))(hash, &new_index);
	zpatch_bpm_model_index = static_cast<std::uint32_t>(new_index);
}

// RestoreDeathMusic (original sub_10050A50): the death-music gate, gated on
// byte_10159B86 and answered through the shipped hash-salt script calls.
extern "C" std::uint8_t abi_death_music_enabled() noexcept
{
	if (!zpatch_flag_death_music) return 0;
	std::uint32_t queried = 0;
	const std::uint32_t query_args[2] = {
	    zpatch_script_call(zpatch_salt_5c, 0, nullptr),
	    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&queried))};
	zpatch_script_call(zpatch_salt_54, 2, query_args);
	if (zpatch_script_call_bool(zpatch_salt_28, 1, &queried) != 0) return 1;
	if (zpatch_script_call_bool(zpatch_salt_a8, 0, nullptr) != 0 &&
	    zpatch_script_call_bool(zpatch_salt_50, 0, nullptr) != 0)
		return 1;
	return 0;
}

// RestoreDeathMusic (original sub_10050AD0): resolve "6A 06 68 ? ? ? ? 8D 8C"
// once and randomize the push immediate to a track 1..5 (rand() % 5 + 1).
extern "C" char abi_death_music_install() noexcept
{
	if (!abi_death_music_enabled()) return 0;
	if (zpatch_death_music_site == 0)
	{
		const auto* module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleA(nullptr));
		if (module)
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
			if (dos->e_magic == IMAGE_DOS_SIGNATURE)
			{
				const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
				    module + dos->e_lfanew);
				if (nt->Signature == IMAGE_NT_SIGNATURE)
				{
					Match match;
					const ModuleView view{module, nt->OptionalHeader.SizeOfImage};
					if (FindPattern(view, "6A 06 68 ? ? ? ? 8D 8C", match))
						zpatch_death_music_site = static_cast<std::uint32_t>(
						    reinterpret_cast<std::uintptr_t>(match.address));
				}
			}
		}
	}
	if (zpatch_death_music_site != 0)
	{
		auto* site = reinterpret_cast<std::uint8_t*>(
		    static_cast<std::uintptr_t>(zpatch_death_music_site) + 1);
		injector::WriteMemory<std::uint8_t>(  // push imm8
		    site, static_cast<std::uint8_t>(std::rand() % 5 + 1), true);
	}
	return 1;
}

// The NewSavesCompatibility converter (original sub_10051B30): parse the save's
// 0x110-byte header (magic "SAVE" at +0xC), walk 18 chunks of 9-byte header +
// payload, record the new-format chunk sizes, then patch three game sites.
namespace
{
// Byte/word/dword stores through VirtualProtect. As in every original handler,
// the result is ignored and the write happens anyway (an unmapped target faults).
void wp_byte(void* address, std::uint8_t value) noexcept
{
	injector::WriteMemory<std::uint8_t>(address, value, true);  // byte store
}
void wp_dword(void* address, std::uint32_t value) noexcept
{
	injector::WriteMemory<std::uint32_t>(address, value, true);  // dword store
}
void wp_word(void* address, std::uint16_t value) noexcept
{
	injector::WriteMemory<std::uint16_t>(address, value, true);  // word store
}
const std::uint8_t* scan_game(std::string_view signature) noexcept
{
	const auto* module = reinterpret_cast<const std::uint8_t*>(GetModuleHandleA(nullptr));
	if (!module) return nullptr;
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
	Match match;
	const ModuleView view{module, nt->OptionalHeader.SizeOfImage};
	if (!FindPattern(view, signature, match)) return nullptr;
	return match.address;
}
}  // namespace

// The original's path/name helpers call the secure CRT (`strcpy_s`/`wcscpy_s`)
// through its IAT; the volatile pointer keeps a distinct secure-copy call.
namespace
{
using ZpStrcpyS = int(__cdecl*)(char*, std::size_t, const char*);
volatile ZpStrcpyS zpatch_secure_strcpy_s =
    static_cast<ZpStrcpyS>(&strcpy_s);
}  // namespace

// The AddOptionsInMenu integration's four callbacks (originals sub_10050A30/40
// and sub_1001EDB0/C0). The focus-loss setter re-asserts the game branch
// sub_1001ECB0 patches, using the pre-patch word saved by
// zpatch_menu_prime_focus_loss.
namespace
{
std::uint8_t* g_focus_loss_site = nullptr;
std::uint16_t g_focus_loss_original = 0;
}  // namespace

extern "C" void zpatch_menu_prime_focus_loss() noexcept
{
	if (g_focus_loss_site != nullptr) return;
	const std::uint8_t* found =
	    scan_game("75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15");
	if (found == nullptr) return;
	g_focus_loss_site = const_cast<std::uint8_t*>(
	    reinterpret_cast<const std::uint8_t*>(found));
	std::memcpy(&g_focus_loss_original, g_focus_loss_site, 2);
}

extern "C" std::uint8_t abi_menu_death_get() noexcept
{
	return zpatch_flag_death_music;
}
extern "C" void abi_menu_death_set(std::uint32_t, std::uint32_t value) noexcept
{
	zpatch_flag_death_music = value != 0 ? 1u : 0u;
}
extern "C" std::uint8_t abi_menu_focus_get() noexcept
{
	return !zpatch_flag_donotpause;
}
extern "C" void abi_menu_focus_set(std::uint32_t, std::uint32_t value) noexcept
{
	zpatch_flag_donotpause = value == 0 ? 1u : 0u;
	// No null check: a failed prime scan leaves the saved site null and the
	// original's 2-byte write faults at 0.
	if (zpatch_flag_donotpause != 0)
		injector::MakeNOP(injector::raw_ptr(g_focus_loss_site), 2, true); //nop
	else
		wp_word(g_focus_loss_site, g_focus_loss_original);
}

// The 0x104-byte path buffer the original's LocalSettingsPath helpers use.
constexpr std::size_t kLocalPathCapacity = 0x104u;

// LocalSettingsPath: the CWD captured at load, the "\GTA IV" folder suffix the
// game's path helper is retargeted to push, the four-byte call slot the game
// calls through (original off_10158064 = sub_10051320), and the five path
// pointers the detour rewrites.
extern "C" int __stdcall zpatch_local_copy_saves(std::uint32_t, std::uint32_t,
                                                 std::uint32_t, std::uint32_t,
                                                 char* dest) noexcept
{
	if (dest != nullptr)
		std::snprintf(dest, kLocalPathCapacity, "%s\\saves", zpatch_local_ansi_cwd);
	return 0;
}

// Captures the CWD and fills the call slot the retargeted game call goes
// through (original off_10158064 = sub_10051320).
extern "C" void zpatch_local_settings_capture_cwd() noexcept
{
	GetCurrentDirectoryA(kLocalPathCapacity, zpatch_local_ansi_cwd);
	GetCurrentDirectoryW(kLocalPathCapacity, zpatch_local_wide_cwd);
	zpatch_local_saves_slot = static_cast<std::uint32_t>(
	    reinterpret_cast<std::uintptr_t>(&zpatch_local_copy_saves));
}

// The detours' helper (original sub_10050D10): rewrites the game's four
// just-computed settings/save path buffers to "<...>\saves\<episode>\" (the
// first ANSI destination is hardcoded "GTA IV"). Called between pushad/popad.
namespace
{
void zpatch_build_ansi_path(char* out, const char* name) noexcept
{
	const char prefix[] = "\\saves\\";
	std::size_t o = 0;
	for (const char* s = prefix; *s; ++s) out[o++] = *s;
	for (const char* s = name; *s; ++s) out[o++] = *s;
	out[o++] = '\\';
	out[o] = '\0';
}
void zpatch_build_wide_path(wchar_t* out, const wchar_t* name) noexcept
{
	const wchar_t prefix[] = L"\\saves\\";
	std::size_t o = 0;
	for (const wchar_t* s = prefix; *s; ++s) out[o++] = *s;
	for (const wchar_t* s = name; *s; ++s) out[o++] = *s;
	out[o++] = L'\\';
	out[o] = 0;
}
// The original copies with strcpy_s/wcscpy_s (cap 0x200); these write the same
// bytes without the static CRT's secure-copy path.
void zpatch_copy_path(char* dst, std::size_t cap, const char* src) noexcept
{
	std::size_t n = 0;
	while (n + 1 < cap && src[n] != '\0')
	{
		dst[n] = src[n];
		++n;
	}
	dst[n] = '\0';
}
void zpatch_copy_wpath(wchar_t* dst, std::size_t cap, const wchar_t* src) noexcept
{
	std::size_t n = 0;
	while (n + 1 < cap && src[n] != 0)
	{
		dst[n] = src[n];
		++n;
	}
	dst[n] = 0;
}
}  // namespace

// The cert cap the original's strcpy_s/wcscpy_s path copies use.
constexpr std::size_t kSettingsPathCapacity = 0x200u;

extern "C" void zpatch_local_settings_step() noexcept
{
	const int episode = zpatch_local_episode_ptr
	                        ? *reinterpret_cast<const int*>(
	                              static_cast<std::uintptr_t>(
	                                  zpatch_local_episode_ptr))
	                        : 0;
	const char* ansi_name = "GTA IV";
	const wchar_t* wide_name = L"GTA IV";
	if (episode == 1)
	{
		ansi_name = "TLaD";
		wide_name = L"TLaD";
	}
	else if (episode == 2)
	{
		ansi_name = "TBoGT";
		wide_name = L"TBoGT";
	}
	char ansi_iv[16];
	char ansi_episode[32];
	wchar_t wide_episode[32];
	// sub_10050D10 copies the literal "\saves\GTA IV\" into ansi_iv, independent
	// of the episode.
	zpatch_build_ansi_path(ansi_iv, "GTA IV");
	zpatch_build_ansi_path(ansi_episode, ansi_name);
	zpatch_build_wide_path(wide_episode, wide_name);
	if (zpatch_local_path_18)
		zpatch_copy_path(reinterpret_cast<char*>(static_cast<std::uintptr_t>(
		                     zpatch_local_path_18)),
		                 kSettingsPathCapacity, ansi_iv);
	if (zpatch_local_path_08)
		zpatch_copy_path(reinterpret_cast<char*>(static_cast<std::uintptr_t>(
		                     zpatch_local_path_08)),
		                 kSettingsPathCapacity, ansi_episode);
	if (zpatch_local_path_1c)
		zpatch_copy_wpath(reinterpret_cast<wchar_t*>(static_cast<std::uintptr_t>(
		                      zpatch_local_path_1c)),
		                  kSettingsPathCapacity, wide_episode);
	if (zpatch_local_path_10)
		zpatch_copy_wpath(reinterpret_cast<wchar_t*>(static_cast<std::uintptr_t>(
		                      zpatch_local_path_10)),
		                  kSettingsPathCapacity, wide_episode);
	zpatch_cookie_tail();
}

// GarageModelFix helpers (originals sub_10046B90 / sub_10046BD0): stash the
// vehicle's model hash across four scattered bytes of the garage object
// (skipped in MP), then rebuild it and repair out-of-range garage models.
// The originals take ECX=object, EDX=vehicle; naked wrappers forward to these.
namespace
{
void zpatch_garage_store_c(std::uint8_t* object, const std::uint8_t* vehicle) noexcept
{
	if (zpatch_citizen_present) return;
	const std::int16_t model =
	    *reinterpret_cast<const std::int16_t*>(vehicle + 46);
	const auto base = static_cast<std::uintptr_t>(zpatch_pre_ped_array);
	const std::uint32_t entry = *reinterpret_cast<const std::uint32_t*>(
	    base + 4u * static_cast<std::uint32_t>(model));
	const std::uint32_t hash = *reinterpret_cast<const std::uint32_t*>(
	    reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(entry)) + 60);
	object[64] = static_cast<std::uint8_t>(hash >> 8);
	object[19] = static_cast<std::uint8_t>(hash);
	object[65] = static_cast<std::uint8_t>(hash >> 16);
	object[70] = static_cast<std::uint8_t>(hash >> 24);
}
std::int32_t zpatch_garage_lookup_c(std::uint8_t* object) noexcept
{
	std::uint32_t hash = object[19];
	hash |= (static_cast<std::uint32_t>(object[64]) |
	         ((static_cast<std::uint32_t>(object[65]) |
	           (static_cast<std::uint32_t>(object[70]) << 8))
	          << 8))
	        << 8;
	const std::int32_t original = static_cast<std::int32_t>(object[19]);
	if (hash == 0u) return original;
	// 0x10046C10 / 0x10046C4A call dword_10159C0C unguarded.
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	std::int32_t out = -1;
	lookup(hash, &out);
	if (out <= 0 || out >= 32000)
	{
		out = -1;
		lookup(kSuperLodFallbackHash, &out);
		*reinterpret_cast<std::int16_t*>(object + 16) = static_cast<std::int16_t>(out);
		*reinterpret_cast<std::uint16_t*>(object + 48) &= kClearedLowBit;
		return 65534;
	}
	*reinterpret_cast<std::int16_t*>(object + 16) = static_cast<std::int16_t>(out);
	return out;
}
}  // namespace
extern "C" void zpatch_garage_store_model();
__declspec(naked) void zpatch_garage_store_model()
{
	__asm {
		cmp  BYTE PTR [zpatch_citizen_present], 0
		push esi
		mov  esi, ecx
		jne  garage_done
		movsx edx, WORD PTR [edx + 0x2e]
		mov  eax, DWORD PTR [zpatch_pre_ped_array]
		mov  eax, DWORD PTR [eax + edx * 4]
		mov  eax, DWORD PTR [eax + 0x3c]
		mov  ecx, eax
		shr  ecx, 8
		mov  BYTE PTR [esi + 0x40], cl
		mov  ecx, eax
		shr  ecx, 0x10
		mov  BYTE PTR [esi + 0x13], al
		shr  eax, 0x18
		mov  BYTE PTR [esi + 0x41], cl
		mov  BYTE PTR [esi + 0x46], al
	garage_done:
		pop  esi
		ret
	}
}
extern "C" std::int32_t zpatch_garage_lookup_model();
__declspec(naked) std::int32_t zpatch_garage_lookup_model()
{
	__asm {
		push ecx
		call zpatch_garage_lookup_c
		add  esp, 4
		push eax
		mov ecx, dword ptr [__security_cookie]
		call zpatch_spawns_cookie_check_ecx
		pop eax
		ret
	}
}

// MovingSteeringWheelSupport / ShakingEngineBlock / MovingPlaneParts.
// abi_vehicle_component is the original sub_10003F40: a name lookup in the
// model registry (entries at [registry], stride 0xE0, count +0x14, name +0,
// bone id +0x14). abi_steer_body is the shared consumer (original
// sub_10004C70), called between pushad/popad by the two detours.
extern "C" const void* abi_vehicle_component(const void* registry,
                                             const char* name) noexcept
{
	// sub_10003F40 dereferences the registry and each candidate name unguarded.
	const auto* entries = *reinterpret_cast<const std::uint8_t* const*>(registry);
	const auto count = *reinterpret_cast<const std::uint16_t*>(
	    static_cast<const std::uint8_t*>(registry) + kModelRegistryCountOffset);
	for (std::uint16_t i = 0; i < count; ++i)
	{
		const auto* entry = entries + static_cast<std::size_t>(i) * kModelRegistryStride;
		const char* candidate = *reinterpret_cast<const char* const*>(entry);
		if (_stricmp(candidate, name) == 0) return entry;
	}
	return nullptr;
}

// sub_10003980's script-float layout: ctx+0x0E holds the slot offset, the slot
// is at ctx+offset+0x18 and the float at slot+8.
constexpr std::size_t kScriptOffsetField = 0x0E;
constexpr std::size_t kScriptSlotBase = 0x18;
constexpr std::size_t kScriptFloatOffset = 8;

extern "C" std::uint32_t abi_steer_body() noexcept
{
	auto* const vehicle = reinterpret_cast<std::uint8_t*>(
	    static_cast<std::uintptr_t>(zpatch_steer_vehicle));

	using BoneFn = std::uint32_t*(__thiscall*)(void*, std::uint32_t);
	using SetFn = void(__thiscall*)(std::uint32_t*, float);
	using PartFn = void(__thiscall*)(void*, int, int, float, int, int, int);
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	const auto get_bone = reinterpret_cast<BoneFn>(
	    static_cast<std::uintptr_t>(zpatch_pre_fn_59b88));
	const auto set_a = reinterpret_cast<SetFn>(
	    static_cast<std::uintptr_t>(zpatch_pre_fn_5d63c));
	const auto set_b = reinterpret_cast<SetFn>(
	    static_cast<std::uintptr_t>(zpatch_pre_fn_59cdc));
	const auto part_call = reinterpret_cast<PartFn>(
	    static_cast<std::uintptr_t>(zpatch_steer_vtable_fn));

	// sub_10003980: read the current script float (resolver hash salt_3c).
	const auto script_float = [&]() -> float
	{
		const std::uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
		const std::uint32_t offset =
		    *reinterpret_cast<const std::uint32_t*>(
		        static_cast<std::uintptr_t>(ctx) + kScriptOffsetField);
		const std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(ctx) + offset + kScriptSlotBase);
		return *reinterpret_cast<const float*>(
		    static_cast<std::uintptr_t>(value) + kScriptFloatOffset);
	};
	const auto model_of = [vehicle]()
	{
		return *reinterpret_cast<const std::int16_t*>(vehicle + kModelIndexOffset);
	};
	const auto identity = [](std::uint32_t* m)
	{
		m[0] = std::bit_cast<std::uint32_t>(kFloatOne);
		m[1] = 0;
		m[2] = 0;
		m[4] = 0;
		m[5] = std::bit_cast<std::uint32_t>(kFloatOne);
		m[6] = 0;
		m[8] = 0;
		m[9] = 0;
		m[10] = std::bit_cast<std::uint32_t>(kFloatOne);
	};
	const auto bone_matrix = [&](std::int16_t model, const char* name) -> std::uint32_t*
	{
		const auto base = static_cast<std::uintptr_t>(zpatch_pre_ped_array);
		const std::uint32_t ped = *reinterpret_cast<const std::uint32_t*>(
		    base + 4u * static_cast<std::uint32_t>(model));
		const std::uint32_t p1 = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(ped) + 8);
		const std::uint32_t p2 = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(p1) + kVehicleComponentP1);
		const std::uint32_t registry = *reinterpret_cast<const std::uint32_t*>(
		    static_cast<std::uintptr_t>(p2) + kVehicleComponentRegistry);
		const auto* entry = static_cast<const std::uint8_t*>(abi_vehicle_component(
		    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(registry)), name));
		if (entry == nullptr) return nullptr;  // the original's own `if (v6)` check
		// sub_10004C70 0x10004D4A calls get_bone unguarded (it is null only when
		// the loader's preamble missed).
		const std::uint32_t bone =
		    *reinterpret_cast<const std::uint16_t*>(entry + kModelEntryBoneOffset);
		return get_bone(vehicle, bone);
	};

	if (zpatch_flag_steering)
	{
		const std::int16_t model = model_of();
		const float steering = *reinterpret_cast<const float*>(vehicle + kVehicleSteeringOffset);
		const std::uint32_t lock_owner = *reinterpret_cast<const std::uint32_t*>(
		    vehicle + kVehicleBoneOffset);
		const float lock = *reinterpret_cast<const float*>(
		    static_cast<std::uintptr_t>(lock_owner) + kVehicleLockOwnerOffset);
		const float turn = static_cast<float>(static_cast<double>(steering / lock) *
		                                      -3.141592653589793);
		float tilt = 0.0f;
		for (int i = 0; i < 512; ++i)
		{
			const std::uint32_t hash = zpatch_wheelangle_models[i];
			if (hash == 0u) break;
			std::int32_t resolved = -1;
			lookup(hash, &resolved);  // sub_10004C70 0x10004CE7: unconditional
			if (resolved == model)
			{
				tilt = static_cast<float>(
				    static_cast<double>(zpatch_wheelangle_angles[i]) *
				    3.141592653589793 / 180.0);
				break;
			}
		}
		if (model != -1)
		{
			std::uint32_t* matrix = bone_matrix(model, "steeringwheel");
			if (matrix != nullptr)
			{
				identity(matrix);
				if (set_a) set_a(matrix, tilt);
				if (set_b) set_b(matrix, turn);
			}
		}
	}
	if (zpatch_flag_engine)
	{
		if (zpatch_engine_last_timestep != script_float())
		{
			const double step = zpatch_engine_descending ? -0.5 : 0.5;
			const float phase = static_cast<float>(
			    static_cast<double>(script_float()) * step +
			    static_cast<double>(zpatch_engine_phase));
			zpatch_engine_phase = phase;
			std::uint8_t direction = zpatch_engine_descending;
			if (static_cast<double>(phase) > 0.01) direction = 1;
			if (static_cast<double>(phase) < -0.01) direction = 0;
			zpatch_engine_descending = direction;
		}
		const std::int16_t model = model_of();
		if (model != -1)
		{
			std::uint32_t* matrix = bone_matrix(model, "engineblock");
			if (matrix != nullptr)
			{
				identity(matrix);
				const std::uint32_t flags =
				    *reinterpret_cast<const std::uint32_t*>(vehicle + kVehicleEngineStateOffset);
				float scale = 0.0f;
				if ((flags & 8u) != 0u)
				{
					const float rpm = *reinterpret_cast<const float*>(vehicle + kVehicleRpmOffset);
					scale = static_cast<float>(static_cast<double>(rpm) * 0.75 + 0.25);
				}
				if ((flags & kFlagBit4) != 0u) scale = 1.0f;
				if (set_b) set_b(matrix, scale * zpatch_engine_phase);
			}
		}
	}
	if (zpatch_flag_plane &&
	    *reinterpret_cast<const std::uint32_t*>(vehicle + kVehicleFlagsOffset) == 5u &&
	    part_call != nullptr)
	{
		const float travel = *reinterpret_cast<const float*>(vehicle + kVehicleSuspensionTravelOffset);
		part_call(vehicle, 89, 4, script_float() * travel * 100.0f, 0, 0, 0);
		part_call(vehicle, 91, 4, script_float() * travel * 100.0f, 0, 0, 0);
		part_call(vehicle, 90, 4, script_float() * travel * 100.0f, 0, 0, 0);
		part_call(vehicle, 92, 4, script_float() * travel * 100.0f, 0, 0, 0);
		const float pitch = *reinterpret_cast<const float*>(vehicle + kVehiclePitchOffset);
		const float roll = *reinterpret_cast<const float*>(vehicle + kVehicleRollOffset);
		const float heave = *reinterpret_cast<const float*>(vehicle + kVehicleHeaveOffset);
		part_call(vehicle, 96, 3,
		          static_cast<float>(static_cast<double>(pitch) * 0.5), 1, 0, 0);
		part_call(vehicle, 97, 3,
		          static_cast<float>(static_cast<double>(pitch) * -0.5), 1, 0, 0);
		part_call(vehicle, 94, 3,
		          static_cast<float>(static_cast<double>(roll) * -0.5), 1, 0, 0);
		part_call(vehicle, 95, 3,
		          static_cast<float>(static_cast<double>(roll) * -0.5), 1, 0, 0);
		part_call(vehicle, 93, 5,
		          static_cast<float>(static_cast<double>(heave) * 0.5), 1, 0, 0);
	}
	// Unconditional tail: snapshot the last script timestep and return its slot.
	const std::uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
	const std::uint32_t offset = *reinterpret_cast<const std::uint32_t*>(
	    static_cast<std::uintptr_t>(ctx) + kScriptOffsetField);
	const std::uint32_t value_ptr = *reinterpret_cast<const std::uint32_t*>(
	    static_cast<std::uintptr_t>(ctx) + offset + kScriptSlotBase);
	const float last = *reinterpret_cast<const float*>(
	    static_cast<std::uintptr_t>(value_ptr) + kScriptFloatOffset);
	zpatch_engine_last_timestep = last;
	return value_ptr;
}

// The shared flag-gated helper of the Patch4RemoveModChecks /
// RemoveGFWLUpdatePopup handler (original sub_10005340): with Patch4 on, zero
// the dword the matched game code points at (match+30); with the GFWL popup
// removal on, store 0x10000E83 over the four xlive.dll update-window flag slots.
// Called between pushad/popad from abi_gfwl_stub.
namespace
{
// The original guards its one-time pattern scans with the MSVC thread-safe
// static protocol. Its epoch (*(TLS[_tls_index]+4)) is loader state that stays
// non-negative when the ASI is mapped without CRT TLS init, which made the
// scan skip; this keeps the same one-time semantics with a tri-state guard
// (0 = uninitialised, -1 = initialising, 1 = done).
bool zpatch_tls_static_header(std::uint32_t& guard) noexcept
{
	for (;;)
	{
		const LONG observed = InterlockedCompareExchange(
		    reinterpret_cast<volatile LONG*>(&guard), -1, 0);
		if (observed == 0) return true;
		if (observed == -1)
		{
			Sleep(1);  // another thread owns the one-time scan
			continue;
		}
		return false;
	}
}

void zpatch_tls_static_footer(std::uint32_t& guard) noexcept
{
	InterlockedExchange(reinterpret_cast<volatile LONG*>(&guard), 1);
}
}  // namespace

extern "C" void abi_gfwl_patch() noexcept
{
	if (zpatch_flag_patch4_modchecks)
	{
		if (zpatch_tls_static_header(zpatch_gfwl_scan_guard))
		{
			// _Init_thread_header claimed the guard; scan then publish the epoch.
			const std::uint8_t* found =
			    scan_game("33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57");
			zpatch_gfwl_site = found == nullptr
			                               ? 0u
			                               : static_cast<std::uint32_t>(
			                                     reinterpret_cast<std::uintptr_t>(found));
			zpatch_tls_static_footer(zpatch_gfwl_scan_guard);
		}
		if (zpatch_gfwl_site != 0)
		{
			std::uint32_t target = 0;
			std::memcpy(&target,
			            reinterpret_cast<const void*>(
			                static_cast<std::uintptr_t>(zpatch_gfwl_site) + 30u),
			            4);
			// 0x10005427 writes *[site+30] without a null check.
			wp_dword(reinterpret_cast<void*>(target), 0);
		}
	}
	if (zpatch_flag_gfwl_popup)
	{
		// 0x10005421 writes xlive base + 0x66F770.. with no base check; with
		// xlive.dll absent the base is 0 and the original faults on that page.
		// The offsets were decimal in the original's constants (6748016 /
		// 6748020 / 7034320 / 7034352); they are one dword apart at 0x66F770 and
		// two more slots at 0x6B55D0/0x6B55F0.
		constexpr std::uintptr_t kXliveUpdateFlagA = 0x66F770u;
		constexpr std::uintptr_t kXliveUpdateFlagB = kXliveUpdateFlagA + sizeof(std::uint32_t);
		constexpr std::uintptr_t kXliveUpdateFlagC = 0x6B55D0u;
		constexpr std::uintptr_t kXliveUpdateFlagD = 0x6B55F0u;
		constexpr std::uint32_t kXliveUpdateFlagValue = 0x10000E83u;  // documented opaque
		static_assert(kXliveUpdateFlagA == 6748016u);
		static_assert(kXliveUpdateFlagB == 6748020u);
		static_assert(kXliveUpdateFlagC == 7034320u);
		static_assert(kXliveUpdateFlagD == 7034352u);
		const auto base = static_cast<std::uintptr_t>(zpatch_xlive_base);
		wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlagA), kXliveUpdateFlagValue);
		wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlagB), kXliveUpdateFlagValue);
		wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlagC), kXliveUpdateFlagValue);
		wp_dword(reinterpret_cast<void*>(base + kXliveUpdateFlagD), kXliveUpdateFlagValue);
	}
}

extern "C" std::uint8_t abi_new_saves_read_hook(void*, std::uint32_t size) noexcept
{
	std::uint8_t scratch[3704];
	if (zpatch_new_saves_loaddata == 0) return 0;
	using LoadFn = std::uint8_t(__cdecl*)(void*, std::uint32_t);
	return reinterpret_cast<LoadFn>(
	    static_cast<std::uintptr_t>(zpatch_new_saves_loaddata))(scratch, size);
}

extern "C" std::int32_t abi_new_saves_p3_hook() noexcept
{
	std::uint8_t buffer[12];
	using LoadFn = std::int32_t(__cdecl*)(void*, std::uint32_t);
	const auto load = reinterpret_cast<LoadFn>(
	    static_cast<std::uintptr_t>(zpatch_new_saves_loaddata));
	if (zpatch_new_saves_flag13 != 0 && load)
		static_cast<void>(load(buffer, 12));
	using OrigFn = std::int32_t(__cdecl*)();
	const std::int32_t result = reinterpret_cast<OrigFn>(
	    static_cast<std::uintptr_t>(zpatch_new_saves_p3_orig))();
	if (zpatch_new_saves_flag13 != 0 && load) return load(buffer, 12);
	return result;
}

extern "C" void abi_new_saves_convert(const char* path) noexcept
{
	std::FILE* file = std::fopen(path, "rb");
	if (!file) return;
	std::uint8_t header[kSaveHeaderSize];
	std::fread(header, kSaveHeaderSize, 1, file);  // return ignored, like the original
	std::uint32_t magic = 0;
	std::memcpy(&magic, header + kSaveMagicOffset, 4);
	if (magic != kSaveMagic)  // "SAVE"
	{
		std::fclose(file);
		return;
	}
	for (int i = 0; i < 18; ++i)
	{
		std::uint8_t chunk[9];
		std::fread(chunk, 9, 1, file);
		std::uint32_t size = 0;
		std::memcpy(&size, chunk + 5, 4);
		zpatch_new_saves_chunk_len = size;                  // dword_1015AD90
		zpatch_new_saves_element_count = size - 9u;         // dword_1015ADA0
		void* payload = std::malloc(zpatch_new_saves_element_count);
		std::fread(payload, 1, zpatch_new_saves_element_count, file);
		std::free(payload);
		if (i == 9) zpatch_new_saves_flag9 = size == 19525u ? 1u : 0u;
		if (i == 16) zpatch_new_saves_flag16 = size == 3703u ? 1u : 0u;
		if (i == 13) zpatch_new_saves_flag13 = size == 5953u ? 1u : 0u;
	}
	std::fclose(file);

	// P1 (game 0x872185): force the 0x3C-byte read path for new saves.
	static const std::uint8_t* p1 = nullptr;
	if (p1 == nullptr) p1 = scan_game("74 37 80 3D ? ? ? ? 00 75 2E 8D 84 ? ? ? ? 00 6A 1E");
	if (p1 != nullptr)
		wp_byte(const_cast<std::uint8_t*>(p1),
		        zpatch_new_saves_flag9 != 0 ? kShortJmpOpcode : kJeRel8Opcode);

	// P2 (game 0x7D8C50): route the oversized read through the plugin's scratch
	// buffer, resize the read, and force/NOP the branch per the save format.
	static const std::uint8_t* p2 = nullptr;
	if (p2 == nullptr) p2 = scan_game("68 BE 0C 00 00 50 E8");
	if (p2 != nullptr)
	{
		auto* site = const_cast<std::uint8_t*>(p2);
		const auto original_target = static_cast<std::uint32_t>(
		    injector::ReadRelativeOffset(injector::raw_ptr(site + 7), 4, true).as_int());
		injector::MakeCALL(injector::raw_ptr(site + 6),
		                   injector::raw_ptr(reinterpret_cast<void*>(&abi_new_saves_read_hook)),
		                   true); //call
		if (zpatch_new_saves_loaddata == 0)
			zpatch_new_saves_loaddata = original_target;
		wp_dword(site + 1, zpatch_new_saves_flag16 != 0 ? 3694u : 3262u);
		if (zpatch_new_saves_flag16 != 0)
			injector::MakeNOP(injector::raw_ptr(site - 5), 2, true); //nop
		else
			wp_word(site - 5, kJneRel8Plus38);  // 75 26 = jne +0x26
	}

	// P3 (game 0x8DA090): route the second LoadData call through the plugin body
	// that consumes the extra 12-byte block for new saves, and repoint the
	// shared LoadData slot at the function's own later call target.
	static const std::uint8_t* p3 = nullptr;
	if (p3 == nullptr) p3 = scan_game("56 E8 ? ? ? ? E8 ? ? ? ? A1 ? ? ? ? 8D 0C 85 00 00 00 00");
	if (p3 != nullptr)
	{
		auto* site = const_cast<std::uint8_t*>(p3);
		const auto original_target = static_cast<std::uint32_t>(
		    injector::ReadRelativeOffset(injector::raw_ptr(site + 7), 4, true).as_int());
		injector::MakeCALL(injector::raw_ptr(site + 6),
		                   injector::raw_ptr(reinterpret_cast<void*>(&abi_new_saves_p3_hook)),
		                   true); //call
		if (zpatch_new_saves_p3_orig == 0)
			zpatch_new_saves_p3_orig = original_target;
		zpatch_new_saves_loaddata = static_cast<std::uint32_t>(
		    injector::ReadRelativeOffset(injector::raw_ptr(site + 30), 4, true).as_int());
	}
}

// The prepare step of the tramp (original sub_10051FD0): if the handler
// captured the game's save-name buffer, make sure the savegame directory
// exists (game function found by signature) and convert "<dir>\\<save name>".
extern "C" void abi_new_saves_prepare() noexcept
{
	const auto* name = reinterpret_cast<const char*>(
	    static_cast<std::uintptr_t>(zpatch_new_saves_src));
	if (name == nullptr) return;
	static const std::uint8_t* get_dir = nullptr;
	if (get_dir == nullptr)
		get_dir = scan_game("55 8B EC 83 E4 F8 83 EC 4C 56 33 C0 8D 4C 24 28");
	if (get_dir == nullptr) return;
	char directory[312]{};
	char suffix = 0;
	if (reinterpret_cast<std::uint8_t(__cdecl*)(int, char*, char*)>(
	        const_cast<std::uint8_t*>(get_dir))(0, directory, &suffix) == 0)
		return;
	char path[560];
	std::snprintf(path, sizeof(path), "%s\\%s", directory, name);
	abi_new_saves_convert(path);
}
}  // extern "C"

// The spawn probe's GetModuleHandleW call goes through our own slot (MSVC's
// inline assembler cannot name the @-decorated import pointer).
struct SpawnsGetModuleHandleInit final
{
	SpawnsGetModuleHandleInit() noexcept
	{
		zpatch_spawns_getmodulehandlew = static_cast<std::uint32_t>(
		    reinterpret_cast<std::uintptr_t>(&GetModuleHandleW));
	}
};
SpawnsGetModuleHandleInit g_spawns_getmodulehandle_init;

// CutsceneFixes chain B's VirtualProtect slot: the body loads this slot instead
// of the @-decorated import pointer, which the inline assembler cannot name
// (same value).
std::uint32_t zpatch_cutscene_virtualprotect = 0;
struct CutsceneVirtualProtectInit final
{
	CutsceneVirtualProtectInit() noexcept
	{
		zpatch_cutscene_virtualprotect = static_cast<std::uint32_t>(
		    reinterpret_cast<std::uintptr_t>(&VirtualProtect));
	}
};
CutsceneVirtualProtectInit g_cutscene_virtualprotect_init;

// The two game functions the DLC discovery routine calls, published as RVAs by
// the LoadDLCs handler (dword_1015804C register, dword_101580D0 apply).
using DlcRegisterFn = void(__stdcall*)(int id, char* request, int flag);
using DlcApplyFn = void(__thiscall*)(void* object, char* request);

// @exact 0x10005590 AimProjectileCrashFix: __thiscall (this in ECX), three
// stack arguments, callee pops 12 bytes; MSVC rejects __thiscall on free
// functions, so the original instruction sequence is reproduced directly.
__declspec(naked) void abi_aim_projectile_crash_fix()
{
	__asm {
		cmp ecx, 0
		je skip
		mov eax, [esp + 8]
		mov [ecx + 0x20], eax
		mov eax, [esp + 0xc]
		mov [ecx + 0x24], eax
		neg eax
		sbb eax, eax
		and eax, [esp + 4]
		mov [ecx + 0x1c], eax
	skip:
		ret 12
	}
}

// @exact 0x100057f0 BetterMPSync: xor eax,eax / ret.
int __cdecl abi_better_mp_sync() { return 0; }

// @exact 0x10005ed0 CombinedRadioFreezeFix: mov eax,13h / ret.
int __cdecl abi_combined_radio_freeze_fix() { return 19; }

// @orig 0x10003e50 AllowHashesForIDE: the game's model-name hash function, replaced so
// that "hash:<number>" literals are accepted (see GameHash). The original is
// __cdecl(char*) -> unsigned int; this wrapper keeps that ABI.
unsigned int __cdecl abi_game_hash(const char* text)
{
	return GameHash(text ? std::string_view{text} : std::string_view{});
}

// @orig 0x10050870 PedModelsInCutscenesFix: replaces the game's "does this object use
// the player model" check (the original calls a game helper and compares +0x2E).
// The original is __thiscall(this) -> bool in al; __fastcall puts `this` in ECX
// and returns bool in AL the same way.
bool __fastcall abi_ped_models_in_cutscenes_check(const void* self, void*)
{
	std::int32_t index = -1;
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	// `call dword_10159C0C` is unguarded in the original.
	lookup(GameHash("player"), &index);
	const auto* o = static_cast<const std::uint8_t*>(self);
	return *reinterpret_cast<const std::int16_t*>(o + kModelIndexOffset) == index;
}

// 0x10045a30 / 0x10045a80 ImprovedShaderStreaming: replacements for the two
// "find shader entry" functions. Originals are __thiscall(this, query) -> entry
// or 0 with one callee-popped argument, so __fastcall(self, edx, query)
// reproduces the ECX=this / stack-arg / ret 4 contract: walk the vector at
// this+8 (count at this+0xC) and return the first non-null entry whose
// vtable[+0x10]() name passes the filter (sub_100459E0: name[4]=='h' or
// name[4]=='p' && name[5]=='e'; sub_10045A10: name[4]=='v').
using ShaderEntryNameFn = const void*(__fastcall*)(void* entry, int);
using ShaderStreamFilterFn = int(__cdecl*)(const void* query, const void* name);
int __cdecl abi_shader_stream_filter_hpe(const void*, const void* name)
{
	const auto* p = static_cast<const unsigned char*>(name);
	if (!p) return 2;
	if (p[4] == 'h') return 0;
	if (p[4] == 'p' && p[5] == 'e') return 0;
	return 2;
}
int __cdecl abi_shader_stream_filter_v(const void*, const void* name)
{
	const auto* p = static_cast<const unsigned char*>(name);
	return (p && p[4] == 'v') ? 0 : 2;
}
namespace
{
void* shader_streaming_find(void* self, const void* query, ShaderStreamFilterFn filter)
{
	auto* base = static_cast<unsigned char*>(self);
	const unsigned count = *reinterpret_cast<const unsigned short*>(base + 0xc);
	auto** array = *reinterpret_cast<void***>(base + 8);
	for (unsigned i = 0; i < count; ++i)
	{
		void* entry = array[i];
		if (!entry) continue;
		auto* vt = *reinterpret_cast<void* const* const*>(entry);
		const auto* name = reinterpret_cast<ShaderEntryNameFn>(vt[0x10 / sizeof(void*)])(entry, 0);
		if (filter(query, name) == 0) return entry;
	}
	return nullptr;
}
}  // namespace
void* __fastcall abi_improved_shader_streaming_find_hpe(void* self, int, const void* query)
{
	return shader_streaming_find(self, query, &abi_shader_stream_filter_hpe);
}
void* __fastcall abi_improved_shader_streaming_find_v(void* self, int, const void* query)
{
	return shader_streaming_find(self, query, &abi_shader_stream_filter_v);
}

// @exact 0x10023480 LoadDLCs: __stdcall, two arguments, mov al,1 / ret 8. The
// original sets only AL, so the upper 24 bits of EAX are preserved and a plain
// `return 1` (mov eax,1) is observable.
__declspec(naked) int __stdcall abi_load_dlcs_dummy(int, int)
{
	__asm {
		mov al, 1
		ret 8
	}
}

// @exact 0x10038550 RemoveRGSCLoginRequirement: no arguments, mov al,1 / ret.
__declspec(naked) int __cdecl abi_rgsc_login_removed()
{
	__asm {
		mov al, 1
		ret
	}
}

// @exact 0x10038570 SavegameFix: xor eax,eax / ret 8 (__stdcall, two arguments).
int __stdcall abi_savegame_fix_null(int, int) { return 0; }

// 0x10045580 SavegameFix close: push [ebp+8] / call CloseHandle / xor eax,eax /
// ret 4 (__stdcall, one argument).
int __stdcall abi_savegame_fix_close(void* handle)
{
	if (handle) CloseHandle(handle);
	return 0;
}

// 0x100455e0 SavegameFix mutex: if (!out) return 1;
// *out = CreateMutexA(0,0,0); return 0; __stdcall, two arguments.
int __stdcall abi_savegame_fix_mutex(void*, void** out_handle)
{
	if (!out_handle) return 1;
	*out_handle = CreateMutexA(nullptr, FALSE, nullptr);
	return 0;
}

// Crash-fix shims, transcribed from the original bodies' machine code: the C
// translation hid argument pushes, register setup and flags the resumed code
// consumes. Registers the resumed code depends on (AL, DL, flags) must survive
// intact; each shim ends in a tail jump to the published resume.

// @exact 0x1003d7b0 PathNodeCrashFix: eax = node index; the sentinel is
// 0x1FFFDF. Otherwise dl = [eax+0x1F] and its low bit feed the failure resume.
__declspec(naked) void abi_path_node_shim()
{
	__asm {
		cmp eax, 0x1FFFDF
		je ok
		mov dl, BYTE PTR [eax + 0x1F]
		test dl, 1
		jmp DWORD PTR [zpatch_path_node_resume_fail]
	ok:
		jmp DWORD PTR [zpatch_path_node_resume_ok]
	}
}

// @exact 0x1003d870 PlaneCrashFix: eax = object; null takes the null resume.
// The normal path stores bl at +0x26 and loads eax from [[esi]+0xF4].
__declspec(naked) void abi_plane_crash_shim()
{
	__asm {
		cmp eax, 0
		je null_path
		mov BYTE PTR [eax + 0x26], bl
		mov edx, DWORD PTR [esi]
		mov eax, DWORD PTR [edx + 0xF4]
		jmp DWORD PTR [zpatch_plane_crash_resume_b]
	null_path:
		jmp DWORD PTR [zpatch_plane_crash_resume_a]
	}
}

// @exact 0x10050490 WantedLevelCrashFix: ecx = object; null returns 0. The
// normal path calls vtable+0x128; the resume consumes al and the following
// test's flags.
__declspec(naked) void abi_wanted_level_shim()
{
	__asm {
		cmp ecx, 0
		je null_path
		mov eax, DWORD PTR [ecx]
		mov edx, DWORD PTR [eax + 0x128]
		call edx
		test al, al
		jmp DWORD PTR [zpatch_wanted_level_resume]
	null_path:
		pop ebx
		ret
	}
}

// @exact 0x10036e90 GroupHackFix: eax = 0, ecx += 4, edx = 7, then the resume
// tests the flags of [ecx] against 0.
__declspec(naked) void abi_group_hack_shim()
{
	__asm {
		xor eax, eax
		add ecx, 4
		lea edx, [eax + 7]
		cmp ecx, 0xC
		je null_path
		cmp DWORD PTR [ecx], 0
		jmp DWORD PTR [zpatch_group_hack_resume]
	null_path:
		ret
	}
}

// @exact 0x10046750 SMPA60Fix: ecx = this; the sentinel 0xDDDDDDDD tails into
// the match+28 resume. The normal path pushes five arguments for the match+14
// resume before tailing into it.
__declspec(naked) void abi_smpa60_shim()
{
	__asm {
		cmp ecx, 0xDDDDDDDD
		je sentinel
		mov ecx, DWORD PTR [eax + 0x70]
		add eax, 0x40
		push eax
		push ecx
		lea edx, [ecx + 0x10]
		push edx
		push ecx
		push ecx
		jmp DWORD PTR [zpatch_smpa60_resume_b]
	sentinel:
		jmp DWORD PTR [zpatch_smpa60_resume_a]
	}
}

// @exact 0x10046780 SMPA60Fix tail shim: if [eax+0x14] is a valid pointer (not
// null, not the 0xCDCDCDCD heap marker) copy [eax+0x10] into [ptr+0x10], then
// tail-jump to the resume the handler publishes (match+136, dword_10159C6C).
__declspec(naked) void abi_smpa60_tail_shim()
{
	__asm {
		mov ecx, DWORD PTR [eax + 0x14]
		test ecx, ecx
		je skip
		cmp ecx, 0xCDCDCDCD
		je skip
		mov edx, DWORD PTR [eax + 0x10]
		mov DWORD PTR [ecx + 0x10], edx
	skip:
		jmp DWORD PTR [zpatch_smpa60_resume_c]
	}
}

// @exact 0x10036eb0 FreezeGunFix: eax = object; if [eax+0x40] is a valid handle
// (not 0xCDCDCDCD, not -1, not 0) push edi and tail-jump into the original
// path, else return 0. The null path pops esi and does ret 8 because the site
// sits just after the enclosing function's push esi.
__declspec(naked) void abi_freeze_gun_shim()
{
	__asm {
		mov ecx, DWORD PTR [eax + 0x38]
		test ecx, ecx
		mov esi, DWORD PTR [eax + 0x40]
		cmp esi, 0xCDCDCDCD
		je freeze_gun_null
		cmp esi, -1
		je freeze_gun_null
		cmp esi, 0
		je freeze_gun_null
		push edi
		jmp DWORD PTR [zpatch_freeze_gun_resume]
	freeze_gun_null:
		xor eax, eax
		pop esi
		ret 8
	}
}

// @exact 0x1003d9a0 PlayerAsPedComponentsFix: save eax, run the helper (stores
// 1/0 in zpatch_player_ped_computed), restore, then push the result and ecx and
// tail-jump to the resume (match+14).
__declspec(naked) void abi_player_ped_components_shim()
{
	__asm {
		pushad
		mov DWORD PTR [zpatch_player_ped_object], eax
		call abi_player_ped_helper
		popad
		push DWORD PTR [zpatch_player_ped_computed]
		push ecx
		jmp DWORD PTR [zpatch_player_ped_resume]
	}
}

// @orig 0x1001af60 EmissiveLerpFix: register-exact transcription of the original
// tramp. It saves ecx/edx, runs sub_1001AF20 (inlined: the integer state
// machine over dword_10159CC0/CCC), then does the SSE tail and tail-jumps to
// the resume (match-12, game 0x7F8BAD).
//
// The tail must stay bit-exact.
//   cvtsi2ss xmm0, edx           merges, keeping xmm0[127:32] from the caller
//   mulss    xmm0, [0x10159cc4]  multiplies by the slot's BYTES (match-16),
//                                not a dereference
//   xmm1..xmm7 untouched         the game keeps them live across the site
// MSVC inline asm cannot spell cvtsi2ss/mulss, so the four-byte encodings are
// emitted. Not byte-exact (the helper is inlined).
__declspec(naked) void abi_emissive_lerp_shim()
{
	__asm {
		push eax
		mov DWORD PTR [zpatch_emissive_ecx], ecx
		mov DWORD PTR [zpatch_emissive_edx], edx
		// --- inlined sub_1001AF20 (0x1001af20) ---
		mov ecx, DWORD PTR [zpatch_emissive_ecx]
		xor edx, edx
		mov eax, DWORD PTR [zpatch_emissive_edx]
		shl ecx, 4
		sub ecx, DWORD PTR [zpatch_emissive_ecx]
		mov DWORD PTR [zpatch_emissive_edx], 0
		lea eax, [eax + ecx*4]
		mov ecx, 0Ch
		add eax, -405
		cmp eax, 2EDh
		__emit 0Fh
		__emit 46h
		__emit 0D1h                  // cmovbe edx, ecx
		mov DWORD PTR [zpatch_emissive_ecx], edx
		// --- reload ecx/edx exactly like the original tramp ---
		mov ecx, DWORD PTR [zpatch_emissive_ecx]
		mov edx, DWORD PTR [zpatch_emissive_edx]
		__emit 0F3h
		__emit 0Fh
		__emit 2Ah
		__emit 0C2h                  // cvtsi2ss xmm0, edx (merge upper bits)
		mov eax, OFFSET zpatch_emissive_const
		__emit 0F3h
		__emit 0Fh
		__emit 59h
		__emit 00h                   // mulss xmm0, DWORD PTR [eax]
		pop eax
		jmp DWORD PTR [zpatch_emissive_resume]
	}
}

// @exact 0x100068A0 CarPartsShadowFix's other slot body: calls the published
// thunk [dword_1015B5B8] twice with (esi, 2|0, ebx, ebx, edi), pops the 20
// argument bytes and tails to [dword_1015BDCC]. The NOPs are original bytes.
__declspec(naked) void abi_carparts_shadow_2()
{
	__asm {
		push esi
		nop
		push 2
		nop
		push ebx
		nop
		push ebx
		nop
		push edi
		nop
		call dword ptr [zpatch_carparts_shadow_fn]
		push esi
		nop
		push 0
		nop
		push ebx
		nop
		push ebx
		nop
		push edi
		nop
		call dword ptr [zpatch_carparts_shadow_fn]
		nop
		add  esp, 14h
		nop
		jmp  dword ptr [zpatch_carparts_resume_2]
		__emit 0CCh   // body delimiter for verify_hook_bodies
	}
}

// @exact 0x100068D0 CarDynamicShadowFix body B (pattern-B variant, unused on
// 1.0.8.0 where pattern A is found): same double call through
// [dword_1015B5B8] with (esi, 2, 0, 0, edi) / (esi, 0, 0, 0, edi), 20 argument
// bytes popped, tail-jump to [dword_1015BDCC].
__declspec(naked) void abi_carparts_shadow_2b()
{
	__asm {
		push esi
		nop
		push 2
		nop
		push 0
		nop
		push 0
		nop
		push edi
		nop
		call dword ptr [zpatch_carparts_shadow_fn]
		nop
		push esi
		nop
		push 0
		nop
		push 0
		nop
		push 0
		nop
		push edi
		nop
		call dword ptr [zpatch_carparts_shadow_fn]
		nop
		add  esp, 14h
		nop
		jmp  dword ptr [zpatch_carparts_resume_2]
		__emit 0CCh   // body delimiter for verify_hook_bodies
	}
}

// @exact 0x10006880 CarPartsShadowFix shim (NOPs are original bytes).
//   cmp edi,0 / nop / je else / nop
//   movzx ebx,word [edi+1Ch] / nop / xor esi,esi / nop
//   test ebx,ebx / nop / jmp [dword_1015BDC8]   ; resume = match+2
// else: nop / jmp [dword_1015BDC0]              ; resume = match+0x45
// The EBX/ESI setup and its flags are consumed by the resumed code; edi and the
// rest of the register state must reach the resume untouched.
__declspec(naked) void abi_carparts_shadow()
{
	__asm {
		cmp  edi, 0
		nop
		je   carparts_else
		nop
		movzx ebx, word ptr [edi + 1Ch]
		nop
		xor  esi, esi
		nop
		test ebx, ebx
		nop
		jmp  dword ptr [zpatch_carparts_resume_if]
	carparts_else:
		nop
		jmp  dword ptr [zpatch_carparts_resume_else]
		__emit 0CCh   // body delimiter for verify_hook_bodies
	}
}

// @exact 0x10044530 PopupHeadlightsFlag (pattern "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1",
// live on 1.0.8.0 at game 0xA53A64).
//   movsx eax,word [esi+2Eh]; pushad; mov [dword_1015BE20],eax
//   call sub_10044310; popad; cmp [dword_1015BE24],1; je resume_a
//   cmp eax,[dword_1015BE1C]; jmp [dword_1015D62C]  ; resume B = site+0xA
// resume_a: jmp [dword_1015D628]                   ; resume A = site+0xC
// pushad/popad is essential: the helper may clobber every GPR, and the resumes
// consume ECX/EDX/ESI exactly as the site left them.
__declspec(naked) void abi_popup_headlights()
{
	__asm {
		movsx eax, word ptr [esi + 2Eh]
		pushad
		mov   dword ptr [zpatch_popup_model], eax
		call  abi_popup_check
		popad
		cmp   dword ptr [zpatch_popup_flag], 1
		je    popup_resume_a
		cmp   eax, dword ptr [zpatch_popup_index]
		jmp   dword ptr [zpatch_popup_resume_b]
	popup_resume_a:
		jmp   dword ptr [zpatch_popup_resume_a]
	}
}
// @exact 0x10044560 PopupHeadlightsFlag: the EDX twin for the second site
// pattern (absent on 1.0.8.0).
__declspec(naked) void abi_popup_headlights_edx()
{
	__asm {
		movsx edx, word ptr [esi + 2Eh]
		pushad
		mov   dword ptr [zpatch_popup_model], edx
		call  abi_popup_check
		popad
		cmp   dword ptr [zpatch_popup_flag], 1
		je    popup_edx_resume_a
		cmp   edx, dword ptr [zpatch_popup_index]
		jmp   dword ptr [zpatch_popup_resume_b]
	popup_edx_resume_a:
		jmp   dword ptr [zpatch_popup_resume_a]
	}
}

// @exact 0x1001ADB0 ForceCarHeadlightShadows thunk (installer E9s the game
// function at pattern "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B",
// game 0x8F7590, resume = match+12).
//   mov [dword_1015BDD0],ecx; pushad; call sub_1001AD20; popad
//   push ebp / mov ebp,esp / and esp,-16 / sub esp,0xD4  ; replayed prologue
//   jmp [dword_1015B5BC]
// The replay is essential: those 12 bytes belong to the function the E9
// replaced, so the resume must run with the frame already established.
__declspec(naked) void abi_fchs_shim()
{
	__asm {
		mov  dword ptr [zpatch_fchs_param], ecx
		pushad
		call abi_fchs_check
		popad
		push ebp
		mov  ebp, esp
		and  esp, 0FFFFFFF0h
		sub  esp, 0D4h
		jmp  dword ptr [zpatch_fchs_resume]
	}
}

// DualVehicleHeadlights (handler 0x10006770, E9 at the game match): bodies
// 0x100064F0 (new builds) / 0x10006630 (builds 1..6) duplicate the headlight-
// cone draw call with the game's custom register convention (args in
// eax/edx/ecx/ebx/ebp/edi/esi plus stack floats), saving every GPR and drawing
// slots 1 and 2 through the published pointers before tailing to match+338.
// abi_dual_headlights_init sets the constants the original's sub_100064D0 sets.
extern "C" void __cdecl abi_dual_headlights_init()
{
	zpatch_dual_light_a = 1;
	zpatch_dual_light_b = 2;
}
// @exact 0x100064F0
extern "C" __declspec(naked) void abi_dual_headlights_new()
{
	__asm {
		pushad
		call abi_dual_headlights_init
		popad
		mov DWORD PTR [zpatch_dual_r_eax], eax
		mov DWORD PTR [zpatch_dual_r_ebx], ebx
		mov DWORD PTR [zpatch_dual_r_ecx], ecx
		mov DWORD PTR [zpatch_dual_r_edx], edx
		mov DWORD PTR [zpatch_dual_r_esi], esi
		mov DWORD PTR [zpatch_dual_r_edi], edi
		mov DWORD PTR [zpatch_dual_r_ebp], ebp
		mov DWORD PTR [zpatch_dual_r_esp], esp
		push 0
		call DWORD PTR [zpatch_dual_bd0]
		add esp, 4
		push eax
		mov ecx, edi
		call DWORD PTR [zpatch_dual_be4]
		push eax
		mov eax, DWORD PTR [esp + 30h]
		fld DWORD PTR [esp + 1Ch]
		push 1
		push DWORD PTR [zpatch_dual_light_a]
		mov ecx, DWORD PTR [ebp + 24h]
		sub esp, 10h
		fstp DWORD PTR [esp + 0Ch]
		add eax, 30h
		fld DWORD PTR [esp + 38h]
		fstp DWORD PTR [esp + 8]
		fld DWORD PTR [esp + 30h]
		fstp DWORD PTR [esp + 4]
		fld DWORD PTR [esp + 2Ch]
		fstp DWORD PTR [esp]
		push eax
		push ecx
		movsx edx, BYTE PTR [edi + 40h]
		mov esi, DWORD PTR [edi + 48h]
		mov edi, DWORD PTR [zpatch_dual_bcc]
		lea ecx, [esp + 54h]
		call DWORD PTR [zpatch_dual_bd4]
		add esp, 24h
		mov eax, DWORD PTR [zpatch_dual_r_eax]
		mov ebx, DWORD PTR [zpatch_dual_r_ebx]
		mov ecx, DWORD PTR [zpatch_dual_r_ecx]
		mov edx, DWORD PTR [zpatch_dual_r_edx]
		mov esi, DWORD PTR [zpatch_dual_r_esi]
		mov edi, DWORD PTR [zpatch_dual_r_edi]
		mov ebp, DWORD PTR [zpatch_dual_r_ebp]
		mov esp, DWORD PTR [zpatch_dual_r_esp]
		push 0
		call DWORD PTR [zpatch_dual_bd0]
		add esp, 4
		push eax
		mov ecx, edi
		call DWORD PTR [zpatch_dual_be4]
		fld DWORD PTR [esp + 18h]
		mov ecx, DWORD PTR [esp + 28h]
		push eax
		push 1
		push DWORD PTR [zpatch_dual_light_b]
		mov eax, DWORD PTR [ebp + 24h]
		sub esp, 10h
		fstp DWORD PTR [esp + 0Ch]
		add ecx, 30h
		fld DWORD PTR [esp + 38h]
		fstp DWORD PTR [esp + 8]
		fld DWORD PTR [esp + 30h]
		fstp DWORD PTR [esp + 4]
		fld DWORD PTR [esp + 2Ch]
		fstp DWORD PTR [esp]
		push ecx
		push eax
		movsx edx, BYTE PTR [edi + 40h]
		mov esi, DWORD PTR [edi + 48h]
		mov edi, DWORD PTR [zpatch_dual_bcc]
		lea ecx, [esp + 54h]
		call DWORD PTR [zpatch_dual_bd4]
		add esp, 24h
		jmp DWORD PTR [zpatch_dual_be0]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}
// @exact 0x10006630
extern "C" __declspec(naked) void abi_dual_headlights_old()
{
	__asm {
		pushad
		call abi_dual_headlights_init
		popad
		mov DWORD PTR [zpatch_dual_r_eax], eax
		mov DWORD PTR [zpatch_dual_r_ebx], ebx
		mov DWORD PTR [zpatch_dual_r_ecx], ecx
		mov DWORD PTR [zpatch_dual_r_edx], edx
		mov DWORD PTR [zpatch_dual_r_esi], esi
		mov DWORD PTR [zpatch_dual_r_edi], edi
		mov DWORD PTR [zpatch_dual_r_ebp], ebp
		mov DWORD PTR [zpatch_dual_r_esp], esp
		push 0
		call DWORD PTR [zpatch_dual_bd0]
		add esp, 4
		push eax
		mov ecx, edi
		call DWORD PTR [zpatch_dual_be4]
		push eax
		mov eax, DWORD PTR [esp + 30h]
		fld DWORD PTR [esp + 20h]
		push 1
		push DWORD PTR [zpatch_dual_light_a]
		mov ecx, DWORD PTR [ebp + 24h]
		sub esp, 10h
		fstp DWORD PTR [esp + 0Ch]
		add eax, 30h
		fld DWORD PTR [esp + 30h]
		fstp DWORD PTR [esp + 8]
		fld DWORD PTR [esp + 2Ch]
		fstp DWORD PTR [esp + 4]
		fld DWORD PTR [esp + 34h]
		fstp DWORD PTR [esp]
		push eax
		push ecx
		movsx edx, BYTE PTR [edi + 40h]
		mov esi, DWORD PTR [edi + 48h]
		mov edi, DWORD PTR [zpatch_dual_bcc]
		lea ecx, [esp + 54h]
		call DWORD PTR [zpatch_dual_bd4]
		add esp, 24h
		mov eax, DWORD PTR [zpatch_dual_r_eax]
		mov ebx, DWORD PTR [zpatch_dual_r_ebx]
		mov ecx, DWORD PTR [zpatch_dual_r_ecx]
		mov edx, DWORD PTR [zpatch_dual_r_edx]
		mov esi, DWORD PTR [zpatch_dual_r_esi]
		mov edi, DWORD PTR [zpatch_dual_r_edi]
		mov ebp, DWORD PTR [zpatch_dual_r_ebp]
		mov esp, DWORD PTR [zpatch_dual_r_esp]
		push 0
		call DWORD PTR [zpatch_dual_bd0]
		add esp, 4
		push eax
		mov ecx, edi
		call DWORD PTR [zpatch_dual_be4]
		fld DWORD PTR [esp + 1Ch]
		mov ecx, DWORD PTR [esp + 28h]
		push eax
		push 1
		push DWORD PTR [zpatch_dual_light_b]
		mov eax, DWORD PTR [ebp + 24h]
		sub esp, 10h
		fstp DWORD PTR [esp + 0Ch]
		add ecx, 30h
		fld DWORD PTR [esp + 30h]
		fstp DWORD PTR [esp + 8]
		fld DWORD PTR [esp + 2Ch]
		fstp DWORD PTR [esp + 4]
		fld DWORD PTR [esp + 34h]
		fstp DWORD PTR [esp]
		push ecx
		push eax
		movsx edx, BYTE PTR [edi + 40h]
		mov esi, DWORD PTR [edi + 48h]
		mov edi, DWORD PTR [zpatch_dual_bcc]
		lea ecx, [esp + 54h]
		call DWORD PTR [zpatch_dual_bd4]
		add esp, 24h
		jmp DWORD PTR [zpatch_dual_be0]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}

// @exact 0x100512F0 LocalSettingsPath detour: entered via E9 at the path-setup
// function's epilogue (game 0x856EE6, swallowing pop edi/esi/ebx; ret plus one
// int3 pad), runs the path-rewrite helper between pushad/popad and replays it.
__declspec(naked) void abi_local_settings_detour()
{
	__asm {
		pushad
		call zpatch_local_settings_step
		popad
		pop edi
		pop esi
		pop ebx
		ret
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after ret)
	}
}
// @exact 0x10051300 LocalSettingsPath fallback detour (pattern absent on
// 1.0.8.0): same helper, then re-issues the original 5-arg call through ESI and
// the two-pop epilogue.
__declspec(naked) void abi_local_settings_fallback_detour()
{
	__asm {
		pushad
		call zpatch_local_settings_step
		popad
		push 0
		push 0
		push 1Ch
		push 0
		call esi
		pop esi
		pop edi
		ret
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after ret)
	}
}

// @exact 0x100051B0 MovingSteeringWheelSupport detour: the installer E9s the
// game site at match+2 (game 0xA538A0); the body caches ESI (the vehicle), runs
// the shared consumer between pushad/popad, calls vfunc 296 (al = its result)
// and tail-jumps to the resume (match+16, the jne after the replayed test).
__declspec(naked) void abi_steer_detour_eax()
{
	__asm {
		mov  dword ptr [zpatch_steer_vehicle], esi
		pushad
		call abi_steer_body
		popad
		mov  edx, dword ptr [esi]
		mov  eax, dword ptr [edx + 128h]
		mov  ecx, esi
		call eax
		test al, al
		jmp  dword ptr [zpatch_steer_resume]
	}
}
// @exact 0x100051E0 MovingSteeringWheelSupport fallback detour (pattern absent
// on 1.0.8.0): same, with the vfunc fetched through EAX/EDX instead.
__declspec(naked) void abi_steer_detour_edx()
{
	__asm {
		mov  dword ptr [zpatch_steer_vehicle], esi
		pushad
		call abi_steer_body
		popad
		mov  eax, dword ptr [esi]
		mov  edx, dword ptr [eax + 128h]
		mov  ecx, esi
		call edx
		test al, al
		jmp  dword ptr [zpatch_steer_resume]
	}
}

// @exact 0x10046C80 GarageModelFix body A (installed at game 0xC68BB0): pushad;
// ecx=object(esi), edx=vehicle(edi); call the store helper; popad; replay
// mov edx,[edi+0x20]; fld [edx+0x34]; tail-jump to the resume (match+6).
__declspec(naked) void abi_garage_body_a()
{
	__asm {
		pushad
		mov  ecx, esi
		mov  edx, edi
		call zpatch_garage_store_model
		popad
		mov  edx, dword ptr [edi + 20h]
		fld  dword ptr [edx + 34h]
		jmp  dword ptr [zpatch_garage_resume_a]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}
// @exact 0x10046CA0 GarageModelFix body B (installed at game 0x9659C3): pushad;
// call the lookup helper (object in ECX); popad; replay
// movzx ecx,[esi]; push eax; push ecx; tail-jump to the resume (match+5).
__declspec(naked) void abi_garage_body_b()
{
	__asm {
		pushad
		call zpatch_garage_lookup_model
		popad
		movzx ecx, word ptr [esi]
		push eax
		push ecx
		jmp  dword ptr [zpatch_garage_resume_b]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}

// FreezeCarFix / DebugPrintSpawns bodies: the sub_100372F0 guard, the ten
// factory-dispatch thunks and the sub_10037510 spawn probe (installer 0x10037C70;
// the FreezeCarFix loader arm gates the install).
// @exact 0x100372F0
__declspec(naked) void abi_spawns_guard()
{
	// Keeps the original callee's EBP frame and ret 0Ch cleanup, so it stays naked.
	__asm {
		push ebp
		mov ebp, esp
		cmp dword ptr [zpatch_spawns_skip], 0
		jne spawns_guard_taken
		push dword ptr [ebp + 10h]
		mov eax, dword ptr [zpatch_spawns_orig_b]
		push dword ptr [ebp + 0Ch]
		push dword ptr [ebp + 8]
		call eax
		mov dword ptr [zpatch_spawns_skip], 0
		pop ebp
		ret 0Ch
	spawns_guard_taken:
		xor eax, eax
		mov dword ptr [zpatch_spawns_skip], eax
		pop ebp
		ret 0Ch
		__emit 0CCh
	}
}

// The ten factory-dispatch thunks (P3 sites). NOTE: case 1 (+0x3D) and case 2
// (+0x56) both set selector 3 in the original bytes; kept 1:1.

// @exact 0x10037330
// Spawn probes: the game's 10 class cases all do the same thing. The macro
// keeps the shim body in one place; only the kind and resume differ.
#define SPAWNS_CASE(name, kind, resume)        \
	__declspec(naked) void name() {               \
		__asm push ebp                              \
		__asm mov ebp, esp                          \
		__asm mov ecx, dword ptr [ebp + 0Ch]        \
		__asm movsx eax, cl                         \
		__asm mov dword ptr [zpatch_spawns_kind], kind \
		__asm mov dword ptr [zpatch_spawns_class], eax \
		__asm mov dword ptr [ebp + 0Ch], ecx        \
		__asm pop ebp                               \
		__asm jmp dword ptr [resume]                \
	}

SPAWNS_CASE(abi_spawns_case_0x24, 5, zpatch_spawns_case0_resume)
SPAWNS_CASE(abi_spawns_case_0x3D, 3, zpatch_spawns_case1_resume)
SPAWNS_CASE(abi_spawns_case_0x56, 3, zpatch_spawns_case2_resume)
SPAWNS_CASE(abi_spawns_case_0x6F, 0, zpatch_spawns_case3_resume)
SPAWNS_CASE(abi_spawns_case_0x88, 4, zpatch_spawns_case4_resume)
SPAWNS_CASE(abi_spawns_case_0x9E, 6, zpatch_spawns_case5_resume)
SPAWNS_CASE(abi_spawns_case_0xB4, 7, zpatch_spawns_case6_resume)
SPAWNS_CASE(abi_spawns_case_0xCA, 1, zpatch_spawns_case7_resume)
SPAWNS_CASE(abi_spawns_case_0xE0, 2, zpatch_spawns_case8_resume)
SPAWNS_CASE(abi_spawns_case_0xF6, 8, zpatch_spawns_case9_resume)

// sub_10037510, the spawn probe (P1/P4/P5/P6/P7 sites): byte-exact naked body,
// 394 instructions, 0x10037510..0x10037ABC. ECX at the [vtable+0xC] call is the
// selector, exactly like the original - do not "fix" it.
// @exact 0x10037510
__declspec(naked) void abi_spawns_probe()
{
	__asm {

		push ebp
		mov ebp, esp
		sub esp, 4Ch
		mov eax, dword ptr [__security_cookie]
		xor eax, ebp
		mov dword ptr [ebp - 8], eax
		push esi
		mov esi, dword ptr [zpatch_pre_ped_array]
		xor eax, eax
		push edi
		mov edi, dword ptr [ebp + 8]
		__emit 0Fh
		__emit 1Fh
		__emit 00h
L_10037530:
		mov ecx, dword ptr [esi + eax*4]
		test ecx, ecx
		je L_10037540
		cmp dword ptr [ecx + 3Ch], edi
		je L_100375FA
L_10037540:
		inc eax
		cmp eax, 7918h
		jl L_10037530
		or eax, 0FFFFFFFFh
L_1003754B:
		mov ecx, dword ptr [zpatch_spawns_kind]
		cwde
		mov dword ptr [zpatch_spawns_kind], 0FFFFFFFFh
		mov esi, dword ptr [esi + eax*4]
		cmp eax, -1
		je L_10037AA2
		test esi, esi
		je L_10037AA2
		cmp esi, 0Ch
		je L_10037AA2
		cmp esi, 0CDCDCDCDh
		je L_10037AA2
		mov eax, dword ptr [esi]
		test eax, eax
		je L_10037AA2
		cmp eax, 0Ch
		je L_10037AA2
		cmp eax, 0CDCDCDCDh
		je L_10037AA2
		test ecx, ecx
		jne L_10037635
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_10037602
		cmp dword ptr [esi + 6Ch], 0
		jne L_10037602
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_car
		jmp L_10037A8C
L_100375FA:
		movzx eax, ax
		jmp L_1003754B
L_10037602:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_car
		jmp L_10037A27
L_10037635:
		cmp ecx, 1
		jne L_100376C0
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_1003768D
		cmp dword ptr [esi + 6Ch], 4
		jne L_1003768D
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_heli
		jmp L_10037A8C
L_1003768D:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_heli
		jmp L_10037A27
L_100376C0:
		cmp ecx, 2
		jne L_1003774B
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_10037718
		cmp dword ptr [esi + 6Ch], 2
		jne L_10037718
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_boat
		jmp L_10037A8C
L_10037718:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_boat
		jmp L_10037A27
L_1003774B:
		cmp ecx, 6
		jne L_100377D6
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_100377A3
		cmp dword ptr [esi + 6Ch], 1
		jne L_100377A3
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_bike
		jmp L_10037A8C
L_100377A3:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_bike
		jmp L_10037A27
L_100377D6:
		cmp ecx, 7
		jne L_10037861
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_1003782E
		cmp dword ptr [esi + 6Ch], 3
		jne L_1003782E
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_train
		jmp L_10037A8C
L_1003782E:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_train
		jmp L_10037A27
L_10037861:
		cmp ecx, 8
		jne L_100378EC
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_100378B9
		cmp dword ptr [esi + 6Ch], 5
		jne L_100378B9
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_plane
		jmp L_10037A8C
L_100378B9:
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_plane
		jmp L_10037A27
L_100378EC:
		cmp ecx, 3
		jne L_1003796D
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 6
		je L_1003792D
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_nonspawned_ped
		jmp L_10037A27
L_1003792D:
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_ped
		jmp L_10037A8C
L_1003796D:
		cmp ecx, 4
		jne L_100379EB
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 5
		jne L_100379AB
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_vehicle_object
		jmp L_10037A27
L_100379AB:
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_object
		jmp L_10037A8C
L_100379EB:
		cmp ecx, 5
		jne L_10037AA2
		mov eax, dword ptr [eax + 0Ch]
		call eax
		cmp al, 6
		je L_10037A59
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037A3D
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_used_nonped
L_10037A27:
		lea eax, [ebp - 4Ch]
		push 40h
		push eax
		call zpatch_spawns_sprintf
		add esp, 14h
		lea edx, [ebp - 4Ch]
		call zpatch_spawns_print_edx
L_10037A3D:
		mov dword ptr [zpatch_spawns_skip], 1
		xor eax, eax
		pop edi
		pop esi
		mov ecx, dword ptr [ebp - 8]
		xor ecx, ebp
		call zpatch_spawns_cookie_check_ecx
		mov esp, ebp
		pop ebp
		ret
L_10037A59:
		cmp byte ptr [zpatch_flag_debug_spawns], 0
		je L_10037AA2
		push OFFSET zpatch_str_gtac
		call dword ptr [zpatch_spawns_getmodulehandlew]
		test eax, eax
		je L_10037AA2
		mov ecx, edi
		call zpatch_spawns_name_ecx
		mov edx, dword ptr [zpatch_spawns_class]
		mov esi, eax
		call zpatch_spawns_who_edx
		push esi
		push eax
		push OFFSET zpatch_fmt_spawned_generic
L_10037A8C:
		lea eax, [ebp - 4Ch]
		push 40h
		push eax
		call zpatch_spawns_sprintf
		add esp, 14h
		lea edx, [ebp - 4Ch]
		call zpatch_spawns_print_edx
L_10037AA2:
		mov eax, dword ptr [zpatch_spawns_orig_a]
		push edi
		call eax
		mov ecx, dword ptr [ebp - 8]
		add esp, 4
		xor ecx, ebp
		pop edi
		pop esi
		call zpatch_spawns_cookie_check_ecx
		mov esp, ebp
		pop ebp
		ret
		__emit 0CCh
	}
}

// @exact 0x1001B2E0 CarDecalsFix: the original scans for
// "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00" (unique on
// 1.0.8.0, game 0x6F7F50) and E9s the first match to this replacement of the
// game function's entry.
//   a2 = [esp+8]; if ((a2[0x28] & 0x3C0) == 0x80) { a1 = [esp+4];
//      if (a1[0x38]) return a1[0x34]; } else { a1 = [esp+4]; }
//   return a1[0x30];   (__stdcall, ret 8)
__declspec(naked) void abi_car_decals_fix()
{
	__asm {
		mov eax, dword ptr [esp + 8]
		mov ecx, dword ptr [eax + 28h]
		and ecx, 3C0h
		cmp ecx, 80h
		jne car_decals_other
		mov eax, dword ptr [esp + 4]
		cmp dword ptr [eax + 38h], 0
		je  car_decals_load
		mov eax, dword ptr [eax + 34h]
		ret 8
	car_decals_other:
		mov eax, dword ptr [esp + 4]
	car_decals_load:
		mov eax, dword ptr [eax + 30h]
		ret 8
	}
}

// @exact 0x100054E0 Patch4RemoveModChecks / RemoveGFWLUpdatePopup stub: runs the
// flag-gated helper between pushad/popad, replays the original's first two
// instructions (its 64-bit sub of ECX:ESI from EDX:EAX into the caller's stack
// slots) and tail-jumps to the handler-published continuation (match+12).
__declspec(naked) void abi_gfwl_stub()
{
	__asm {
		pushad
		call abi_gfwl_patch
		popad
		sub  eax, ecx
		sbb  edx, esi
		mov  dword ptr [esp + 0Ch], eax
		mov  dword ptr [esp + 10h], edx
		jmp  dword ptr [zpatch_gfwl_resume]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}

// @exact 0x10037BA0 BadPlayerModelCrashFix trampoline: the handler E9s the game
// function at pattern "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C" (game 0x7CF803,
// 3 bytes in) and publishes match+0xA. The body captures [ebp+0xC] (entity) and
// [ebp+0x10] (model index), runs the check between pushad/popad, writes the
// corrected model index back to [ebp+0x10], then replays the overwritten
// prologue (and esp,0xF0; sub esp,0x54; xorps xmm0,xmm0; push ebx) and tails.
// EBP is the game function's live frame.
__declspec(naked) void abi_bad_player_model()
{
	__asm {
		push edi
		mov  edi, dword ptr [ebp + 0Ch]
		mov  dword ptr [zpatch_bpm_obj], edi
		mov  edi, dword ptr [ebp + 10h]
		mov  dword ptr [zpatch_bpm_model_index], edi
		pushad
		call abi_bpm_check
		popad
		mov  edi, dword ptr [zpatch_bpm_model_index]
		mov  dword ptr [ebp + 10h], edi
		pop  edi
		and  esp, 0FFFFFFF0h
		sub  esp, 54h
		xorps xmm0, xmm0
		push ebx
		jmp  dword ptr [zpatch_bpm_resume]
		__emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
	}
}

// @exact 0x100521E0 NewSavesCompatibility tramp: the handler E9s the savegame-
// load entry at "89 84 24 E0 01 00 00 8B 45 08 53 33 DB" (game 0x85B093) so this
// runs the converter first, then replicates the overwritten instruction (the
// stack-cookie store) and the next two, and tail-jumps to match+13. Entry is by
// JMP, so the [esp+0x1E0] store targets the same game frame slot as the original.
__declspec(naked) void abi_new_saves_compat()
{
	__asm {
		pushad
		call abi_new_saves_prepare
		popad
		mov  dword ptr [esp + 1E0h], eax
		mov  eax, dword ptr [ebp + 8]
		push ebx
		xor  ebx, ebx
		jmp  dword ptr [zpatch_new_saves_resume]
	}
}

// @exact 0x100469E0 SuperLODFix: not byte-exact (calls a compiled helper). The
// fail path resumes via dword_101580AC, the ok path via dword_101580B0; both are
// handler-published base+rva values, build-gated.
__declspec(naked) void abi_super_lod_fix_shim()
{
	__asm {
		jmp slod_main
	slod_fail:
		jmp DWORD PTR [zpatch_super_lod_fail]
	slod_main:
		push eax
		push ecx
		push esi
		call abi_super_lod_helper
		add esp, 4
		pop ecx
		test al, al
		pop eax
		jne slod_ok
		test byte ptr [eax + 0x72], 0x10
		je slod_fail
	slod_ok:
		jmp DWORD PTR [zpatch_super_lod_ok]
	}
}

// 0x1004AA80: the VRAM verdict helper, called between pushad/popad by the shim
// below. It only reads the two argument globals and sets the flag, so ordinary
// C++ is enough.
extern "C" void abi_vram_fix_helper() noexcept
{
	const auto argB = static_cast<std::int32_t>(zpatch_vram_arg_b);
	zpatch_vram_flag = 0;
	// The original compares the dword's value as a signed int (the float bits).
	const auto quarter_bits = static_cast<std::int32_t>(std::bit_cast<std::uint32_t>(kFloatQuarter));
	if (argB < 0 || zpatch_vram_arg_a != 0 || argB < quarter_bits)
		zpatch_vram_flag = 1;
}
// @orig 0x1004aab0 VRAMFix: stashes two of the game function's arguments, calls the
// verdict helper under pushad/popad, then reproduces the original tail
// (fldcw [esp]; add esp,0x1c; ret), returning INT_MAX when the verdict is set.
// The E9 site replaced that tail, so this must be naked.
// @exact 0x1004AAB0
__declspec(naked) void abi_vram_fix_shim()
{
	__asm {
		mov eax, DWORD PTR [esp + 0x14]
		mov DWORD PTR [zpatch_vram_arg_a], eax
		mov eax, DWORD PTR [esp + 0x10]
		mov DWORD PTR [zpatch_vram_arg_b], eax
		pushad
		call abi_vram_fix_helper
		popad
		cmp BYTE PTR [zpatch_vram_flag], 0
		je vram_skip
		mov eax, 7FFFFFFFh
	vram_skip:
		fldcw WORD PTR [esp]
		add esp, 1Ch
		ret
	}
}

// One-shot diagnostic for DrivebyFOVFix: scans the resolver's registry table
// (base+0x150FDD0/count 0x150FDD4, build-9 RVAs) for the key 0x9399528. Naked so
// the shim can call it without disturbing the live xmm1..xmm7 (xmm0 is scratch).
// The scan result is not consumed.
extern "C" void abi_driveby_fov_note_impl()
{
	static bool logged = false;
	if (logged) return;
	logged = true;
	const std::uintptr_t game_base =
	    static_cast<std::uintptr_t>(zpatch_driveby_resume) - 5u - kDrivebyResumeRva_1080;  // build-9 sig RVA
	std::uint32_t count = 0;
	std::uint32_t table = 0;
	if (game_base > zp::kMinPlausiblePointer && game_base < zp::kUserModeCeiling)
	{
		count = *reinterpret_cast<const std::uint32_t*>(game_base + kModelTableCountRva);
		table = *reinterpret_cast<const std::uint32_t*>(game_base + kModelTablePointerRva);
		if (table > zp::kMinPlausiblePointer && count > 0u && count < kMaxModelCount)
		{
			for (std::uint32_t i = 0; i < count; ++i)
			{
				const std::uint32_t key = *reinterpret_cast<const std::uint32_t*>(
				    static_cast<std::uintptr_t>(table) + i * 8u);
				if (key == kDrivebyModelKey) break;
			}
		}
	}
}
extern "C" __declspec(naked) void abi_driveby_fov_note()
{
	__asm {
		sub esp, 70h
		__emit 0Fh
		__emit 11h
		__emit 4Ch
		__emit 24h
		__emit 00h                    // movups [esp], xmm1
		__emit 0Fh
		__emit 11h
		__emit 54h
		__emit 24h
		__emit 10h                    // movups [esp+0x10], xmm2
		__emit 0Fh
		__emit 11h
		__emit 5Ch
		__emit 24h
		__emit 20h                    // movups [esp+0x20], xmm3
		__emit 0Fh
		__emit 11h
		__emit 64h
		__emit 24h
		__emit 30h                    // movups [esp+0x30], xmm4
		__emit 0Fh
		__emit 11h
		__emit 6Ch
		__emit 24h
		__emit 40h                    // movups [esp+0x40], xmm5
		__emit 0Fh
		__emit 11h
		__emit 74h
		__emit 24h
		__emit 50h                    // movups [esp+0x50], xmm6
		__emit 0Fh
		__emit 11h
		__emit 7Ch
		__emit 24h
		__emit 60h                    // movups [esp+0x60], xmm7
		call abi_driveby_fov_note_impl
		__emit 0Fh
		__emit 10h
		__emit 4Ch
		__emit 24h
		__emit 00h                    // movups xmm1, [esp]
		__emit 0Fh
		__emit 10h
		__emit 54h
		__emit 24h
		__emit 10h                    // movups xmm2, [esp+0x10]
		__emit 0Fh
		__emit 10h
		__emit 5Ch
		__emit 24h
		__emit 20h                    // movups xmm3, [esp+0x20]
		__emit 0Fh
		__emit 10h
		__emit 64h
		__emit 24h
		__emit 30h                    // movups xmm4, [esp+0x30]
		__emit 0Fh
		__emit 10h
		__emit 6Ch
		__emit 24h
		__emit 40h                    // movups xmm5, [esp+0x40]
		__emit 0Fh
		__emit 10h
		__emit 74h
		__emit 24h
		__emit 50h                    // movups xmm6, [esp+0x50]
		__emit 0Fh
		__emit 10h
		__emit 7Ch
		__emit 24h
		__emit 60h                    // movups xmm7, [esp+0x60]
		add esp, 70h
		ret
	}
}

// @orig 0x100064b0 DrivebyFOVFix: computes the drive-by FOV once and feeds it to the
// resume as xmm0. The original is `pushal; call sub_10006450; popal;
// movss xmm0,[0x101580c4]; jmp [0x1015bdf4]`; sub_10006450 resolves the handle
// dword_101580D4 (salt 0x09399528) through dword_1015BDE0, walks two structures,
// caches the entry and reads the float at +0x60.
// The resolver result and cached entry are dereferenced unguarded, like the
// original (0x1000646E / 0x10006490); 45.0f is its own first instruction
// (0x10006457). Kept separate and naked so the shim's call keeps the stack shape.
extern "C" __declspec(naked) void abi_driveby_fov_helper()
{
	__asm {
		cmp BYTE PTR [zpatch_driveby_done], 0
		mov DWORD PTR [zpatch_driveby_fov], 42340000h  // 45.0f default
		jne drv_have
		mov eax, DWORD PTR [zpatch_salt_d4]
		push edx
		push eax
		call zpatch_driveby_resolve_seh
		add esp, 8
		mov ecx, DWORD PTR [eax + 0Eh]    // AVs when the handle did not resolve
		add eax, 0Eh
		add ecx, eax
		mov BYTE PTR [zpatch_driveby_done], 1
		mov eax, DWORD PTR [ecx + 0Fh]    // AVs on a garbage chain
		mov eax, DWORD PTR [eax + ecx + 14h]
		mov DWORD PTR [zpatch_driveby_cached], eax
		jmp drv_read
	drv_have:
		mov eax, DWORD PTR [zpatch_driveby_cached]
	drv_read:
		mov eax, DWORD PTR [eax]          // AVs when the cached entry is null
		test eax, eax
		je drv_done
		mov edx, OFFSET zpatch_driveby_fov
		__emit 0F3h
		__emit 0Fh
		__emit 10h
		__emit 40h
		__emit 60h                   // movss xmm0, DWORD PTR [eax + 0x60]
		__emit 0F3h
		__emit 0Fh
		__emit 11h
		__emit 02h                   // movss DWORD PTR [edx], xmm0
	drv_done:
		ret
	}
}
// 0x100064b0 shim: the original restores every GPR before the SSE load; EAX is
// scratch (pushed/popped), so no register the resume consumes is clobbered.
__declspec(naked) void abi_driveby_fov_shim()
{
	__asm {
		pushad
		call abi_driveby_fov_helper
		popad
		push eax
		mov eax, OFFSET zpatch_driveby_fov
		__emit 0F3h
		__emit 0Fh
		__emit 10h
		__emit 00h                   // movss xmm0, DWORD PTR [eax]
		pop eax
		jmp DWORD PTR [zpatch_driveby_resume]
	}
}

// NoLiveryLimit helpers (sub_1003D220 / sub_1003D2F0 / sub_1003D270). The naked
// shims below stash their register inputs in globals, call a compiled helper,
// then tail-jump to the handler-published resume. The count helper calls the
// published function as `__thiscall(this, hash)`.
extern "C" void abi_nolivery_hash_sign()  // sub_1003D220
{
	char buffer[64];
	// Original: sprintf(buffer, 0x40, "%s_sign_%d", name, index + 1).
	zpatch_spawns_sprintf(
	    buffer, sizeof(buffer), "%s_sign_%d",
	    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_nolivery_name)),
	    static_cast<int>(zpatch_nolivery_index) + 1);
	zpatch_nolivery_hash = GameHash(buffer);
	zpatch_cookie_tail();
}
extern "C" void abi_nolivery_copy_name()  // sub_1003D2F0
{
	// push [bufptr]; push 0x10; push [dest]; call [strcpy_s]; add esp,0xc; ret.
	// Reads both globals unguarded and has no /GS cookie tail (leaf).
	char* const dst = reinterpret_cast<char*>(static_cast<std::uintptr_t>(zpatch_nolivery_dest));
	const char* const src =
	    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_nolivery_bufptr));
	zpatch_secure_strcpy_s(dst, kNoLiveryNameCapacity, src);
}
extern "C" void abi_nolivery_count_loop()  // sub_1003D270
{
	using LookupFn = int(__fastcall*)(void* self, int, std::uint32_t hash);
	const auto fn = reinterpret_cast<LookupFn>(static_cast<std::uintptr_t>(zpatch_nolivery_fn));
	void* const self = reinterpret_cast<void*>(static_cast<std::uintptr_t>(zpatch_nolivery_arg));
	const char* const name =
	    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_nolivery_bufptr));
	zpatch_nolivery_count = 0;
	for (std::uint32_t i = 1;; ++i)
	{
		char buffer[64];
		zpatch_spawns_sprintf(buffer, sizeof(buffer), "%s_sign_%d", name,
		                      static_cast<int>(i));
		const int result = fn ? fn(self, 0, GameHash(buffer)) : 0;
		if (result == 0) break;
		zpatch_nolivery_count = i;
	}
	zpatch_cookie_tail();
}
// @exact 0x1003d310: name = &ebx[0x13c], index = eax; resume with (hash, movsx ebx[0x48]).
extern "C" __declspec(naked) void abi_nolivery_a()
{
	__asm {
		pushad
		lea ecx, [ebx + 13Ch]
		mov DWORD PTR [zpatch_nolivery_name], ecx
		mov DWORD PTR [zpatch_nolivery_index], eax
		call abi_nolivery_hash_sign
		popad
		mov ecx, DWORD PTR [zpatch_nolivery_hash]
		movsx edx, WORD PTR [ebx + 48h]
		push ecx
		push edx
		jmp DWORD PTR [zpatch_nolivery_resume]
	}
}
// @exact 0x1003d340: name = &ebx[0x13c], index = ecx; resume with (hash, movsx ebx[0x48]).
extern "C" __declspec(naked) void abi_nolivery_b()
{
	__asm {
		pushad
		lea edx, [ebx + 13Ch]
		mov DWORD PTR [zpatch_nolivery_name], edx
		mov DWORD PTR [zpatch_nolivery_index], ecx
		call abi_nolivery_hash_sign
		popad
		mov edx, DWORD PTR [zpatch_nolivery_hash]
		movsx eax, WORD PTR [ebx + 48h]
		push edx
		push eax
		jmp DWORD PTR [zpatch_nolivery_resume]
	}
}
// @exact 0x1003d370: like a, but the original also drops 8 bytes of args first.
extern "C" __declspec(naked) void abi_nolivery_c()
{
	__asm {
		pushad
		lea edx, [ebx + 13Ch]
		mov DWORD PTR [zpatch_nolivery_name], edx
		mov DWORD PTR [zpatch_nolivery_index], eax
		call abi_nolivery_hash_sign
		popad
		mov edx, DWORD PTR [zpatch_nolivery_hash]
		movsx eax, WORD PTR [ebx + 48h]
		add esp, 8
		push edx
		push eax
		jmp DWORD PTR [zpatch_nolivery_resume]
	}
}
// @exact 0x1003d3a0: copies the frame name into a 16-byte buffer, counts the
// "<name>_sign_N" lookups, writes the count to [ebp+0xc2], then resumes.
extern "C" __declspec(naked) void abi_nolivery_d()
{
	__asm {
		pushad
		mov DWORD PTR [zpatch_nolivery_arg], edi
		lea ecx, [ebp + 13Ch]
		mov DWORD PTR [zpatch_nolivery_dest], ecx
		lea ecx, [esp + 60h]
		mov DWORD PTR [zpatch_nolivery_bufptr], ecx
		call abi_nolivery_copy_name
		call abi_nolivery_count_loop
		popad
		push ebx
		mov ebx, DWORD PTR [zpatch_nolivery_count]
		mov BYTE PTR [ebp + 0C2h], bl
		pop ebx
		jmp DWORD PTR [zpatch_nolivery_resume_d]
	}
}
// @exact 0x1003d3e0: the same sequence as 0x1003d3a0 with ESI as the object
// (name = &edi[0x13c], count written to [edi+0xc2]); same resume slot.
extern "C" __declspec(naked) void abi_nolivery_e()
{
	__asm {
		pushad
		mov DWORD PTR [zpatch_nolivery_arg], esi
		lea ecx, [edi + 13Ch]
		mov DWORD PTR [zpatch_nolivery_dest], ecx
		lea ecx, [esp + 60h]
		mov DWORD PTR [zpatch_nolivery_bufptr], ecx
		call abi_nolivery_copy_name
		call abi_nolivery_count_loop
		popad
		push ebx
		mov ebx, DWORD PTR [zpatch_nolivery_count]
		mov BYTE PTR [edi + 0C2h], bl
		pop ebx
		jmp DWORD PTR [zpatch_nolivery_resume_d]
	}
}

// @exact 0x10036F00 CrashMsgFix: calls the game's message getter through the
// handler-published pointer (the site's original call target, *(match-4)) and
// replaces every '~' with a space. Entered via the handler's E8, so it returns
// normally. The original's two nops are omitted (not byte-exact).
extern "C" __declspec(naked) void abi_crash_msg_shim()
{
	__asm {
		call DWORD PTR [zpatch_crash_msg_getter]
		mov dl, BYTE PTR [eax]
		test dl, dl
		je cms_done
		mov ecx, eax
		__emit 066h
		__emit 090h
	cms_loop:
		cmp dl, 7Eh
		jne cms_skip
		mov BYTE PTR [ecx], 20h
	cms_skip:
		mov dl, BYTE PTR [ecx + 1]
		inc ecx
		test dl, dl
		jne cms_loop
	cms_done:
		ret
	}
}

// NewFilesCompatibility: body 0x10051600 registers a callback with the game's
// file-registration function; bodies 0x10051890 / 0x100518D0 are the per-file
// update hooks (E8 sites) that resolve the asset object, hash the name and
// register it. Helpers mirror sub_100515D0 / sub_100515E0 / sub_10051730 /
// sub_10051610 / sub_100517E0.
namespace
{
std::uintptr_t newfiles_find(std::string_view signature)
{
	const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleA(nullptr));
	const auto* dos = base ? reinterpret_cast<const IMAGE_DOS_HEADER*>(base) : nullptr;
	const auto* nt = dos && dos->e_magic == IMAGE_DOS_SIGNATURE
	                     ? reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew)
	                     : nullptr;
	if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) return 0;
	Match match;
	if (!FindPattern({base, nt->OptionalHeader.SizeOfImage}, signature, match)) return 0;
	return reinterpret_cast<std::uintptr_t>(match.address);
}
}  // namespace
extern "C" void* abi_newfiles_cb(void** a1)  // 0x100515D0
{
	void* const result = *a1;
	*reinterpret_cast<int*>(*a1) = 1;
	return result;
}
// 0x100515E0 is five instructions: push callback, push hash, call the
// published register function, add esp,8, ret. The flags at the resume come
// from that add esp,8, so abi_newfiles_register_body keeps the naked stack
// shape around this C helper.
extern "C" void __cdecl abi_newfiles_register()
{
	// The slot is a cdecl two-argument registrar; the original cleans both
	// arguments after the call.
	const auto register_file = reinterpret_cast<void(__cdecl*)(std::uint32_t, void*)>(
	    static_cast<std::uintptr_t>(zpatch_newfiles_register_slot));
	register_file(kNewFilesRegistrarKey, reinterpret_cast<void*>(&abi_newfiles_cb));
}
extern "C" void* abi_newfiles_find_obj()  // 0x10051730
{
	// The original re-dereferences the container global on EVERY call
	// (**(_DWORD**)(match+2)), so it is picked up again whenever the game rebuilds
	// its registry; caching the container itself left a stale one that faulted in
	// the game's container search (0x43a490).
	static std::uintptr_t match = 0;
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		match = newfiles_find(
		    "8B 35 ? ? ? ? 6A 00 50 E8 ? ? ? ? 83 C4 08 50 8B CE E8 ? ? ? ? 85 C0 89 07 5E 74 05");
	}
	if (!match) return nullptr;
	const std::uint32_t global =
	    *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const void*>(match + 2u));
	return global ? reinterpret_cast<void*>(*reinterpret_cast<const std::uint32_t*>(
	                    reinterpret_cast<const void*>(global)))
	              : nullptr;
}
extern "C" int abi_newfiles_call_obj(void* obj, std::uint32_t hash)  // 0x10051610
{
	using Fn = int(__fastcall*)(void* self, int, std::uint32_t);
	static std::uintptr_t cached = 0;
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		cached = newfiles_find("85 C9 53 55 56 57 74 40 8B 6C 24 14 8D 64 24 00");
		if (!cached) cached = newfiles_find("53 55 8B 6C 24 0C 56 57 EB 06");
	}
	if (!cached) return 0;
	return reinterpret_cast<Fn>(cached)(obj, 0, hash);
}
extern "C" int abi_newfiles_call_add(void* self, void* str)  // 0x100517E0
{
	using Fn = int(__fastcall*)(void* self, int, void*);
	static std::uintptr_t cached = 0;
	static bool resolved = false;
	if (!resolved)
	{
		resolved = true;
		const std::uintptr_t m = newfiles_find("8B 0F 85 C9 74 ? 0F B7 41 0A 66 85 C0");
		cached = m ? m - 20u : 0;
	}
	if (!cached) return 0;
	return reinterpret_cast<Fn>(cached)(self, 0, str);
}
// @exact 0x10051600
extern "C" __declspec(naked) void abi_newfiles_register_body()  // 0x10051600
{
	__asm {
		pushad
		call abi_newfiles_register
		popad
		ret
	}
}
extern "C" char __fastcall abi_newfiles_update_a(void* self, int)  // 0x10051890
{
	auto* base = static_cast<unsigned char*>(self);
	if (base[kNewFilesNameA] == 0) return 0;
	if (abi_newfiles_call_obj(abi_newfiles_find_obj(),
	                          GameHash(reinterpret_cast<const char*>(base + kNewFilesNameA))))
		abi_newfiles_call_add(base + 8, base + kNewFilesNameA);
	return 1;
}
extern "C" char __fastcall abi_newfiles_update_b(void* self, int)  // 0x100518D0
{
	auto* base = static_cast<unsigned char*>(self);
	if (base[kNewFilesNameB] == 0) return 0;
	if (abi_newfiles_call_obj(abi_newfiles_find_obj(),
	                          GameHash(reinterpret_cast<const char*>(base + kNewFilesNameB))))
		abi_newfiles_call_add(base + 4, base + kNewFilesNameB);
	return 1;
}

// BenchmarkFix: two match-site shims (E9 at match[0]/[1]+8). Each checks eax,
// runs the interposed callback (esi with edi), then tails to the published
// resume, or unwinds via the enclosing function's tail when eax is null.
// @exact 0x10005650
extern "C" __declspec(naked) void abi_benchmark_a()  // 0x10005650
{
	__asm {
		test eax, eax
		je bf_a_zero
		push edi
		mov ebx, eax
		call esi
		jmp DWORD PTR [zpatch_benchmark_resume_a]
	bf_a_zero:
		xor al, al
		pop esi
		pop ebp
		pop ebx
		add esp, 10h
		ret
	}
}
// @exact 0x10005670
extern "C" __declspec(naked) void abi_benchmark_b()  // 0x10005670
{
	__asm {
		test eax, eax
		je bf_b_zero
		push edi
		mov ebx, eax
		call esi
		jmp DWORD PTR [zpatch_benchmark_resume_b]
	bf_b_zero:
		xor al, al
		pop esi
		pop ebp
		pop ebx
		add esp, 10h
		ret
	}
}

// BuildingDynamicShadows: shim installed at the sig
// `8B 47 28 C1 E8 06 83 E0 0F 83 C0`. If [edi] is one of the two handler-
// published model hashes it takes the special resume, else it derives the shadow
// level `(([edi+0x28]>>6)&0xf)-1` and takes the normal resume (match+0xf /
// match+0x70).
// @exact 0x10005AF0
extern "C" __declspec(naked) void abi_building_dynamic_shadows()
{
	__asm {
		mov eax, DWORD PTR [edi]
		cmp eax, DWORD PTR [zpatch_bds_hash_a]
		je bds_special
		cmp eax, DWORD PTR [zpatch_bds_hash_b]
		je bds_special
		mov eax, DWORD PTR [edi + 28h]
		shr eax, 6
		and eax, 0Fh
		add eax, -1
		jmp DWORD PTR [zpatch_bds_resume_a]
	bds_special:
		jmp DWORD PTR [zpatch_bds_resume_b]
	}
}

// CopSirensFlag helpers (sub_10005F70 / sub_10006260) and the reimplemented IDE
// parser (sub_10003FA0). The shims stash the model index, run the check and tail-
// jump to the resume; the IDE hook fills the model-hash tables the checks read.
extern "C" int abi_copsirens_check_a()  // sub_10005F70
{
	static const char* const kPolice[] =
	{
			"police",     "police2",    "police3",  "police4",   "noose",      "pstockade",
			"nstockade",  "polpatriot", "fbi",      "n_police",  "n_police2",  "n_polpatriot",
			"n_pstockade", nullptr};
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	const std::int32_t index = static_cast<std::int32_t>(zpatch_copsirens_index);
	zpatch_copsirens_flag = 0;
	// sub_10005F70's `call dword_10159C0C` is unguarded.
	for (int i = 0; kPolice[i]; ++i)
	{
		std::int32_t out = -1;
		lookup(GameHash(kPolice[i]), &out);
		if (out == index)
		{
			zpatch_copsirens_flag = 1;
			return 1;
		}
	}
	// Original sub_10005F70 re-stores the flag before the models loop
	// (mov eax,[0x10159B80]; ...; mov [0x10159B80],eax); keep the store.
	{
		volatile std::uint32_t* flag = &zpatch_copsirens_flag;
		*flag = *flag;
	}
	for (int i = 0; i < 512 && zpatch_copsirens_models[i]; ++i)
	{
		std::int32_t out = -1;
		lookup(zpatch_copsirens_models[i], &out);
		if (out == index)
		{
			zpatch_copsirens_flag = 1;
			return 1;
		}
	}
	return 0;
}
extern "C" int abi_copsirens_check_b()  // sub_10006260
{
	const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
	    static_cast<std::uintptr_t>(zpatch_model_lookup));
	const std::int32_t index = static_cast<std::int32_t>(zpatch_copsirens_index_b);
	zpatch_copsirens_flag_b = 0;
	std::int32_t out = -1;
	// sub_10006260's `call dword_10159C0C` is unguarded.
	lookup(GameHash("police2"), &out);
	if (out != index)
	{
		if (!zpatch_copsirens_models[0]) return 0;
		for (int i = 0; i < 512 && zpatch_copsirens_models[i]; ++i)
		{
			out = -1;
			lookup(zpatch_copsirens_models[i], &out);
			if (out == index)
			{
				zpatch_copsirens_flag_b = 1;
				return 1;
			}
		}
		return 0;
	}
	zpatch_copsirens_flag_b = 1;
	return 1;
}
// _strnicmp equivalent: the original's IDE parser takes the case-insensitive
// prefix branch (strnicmp(v0, "wheelangle:", 0xB)); stops at a NUL like the CRT.
bool prefix_ci(const char* text, const char* prefix, std::size_t length) noexcept
{
	for (std::size_t i = 0; i < length; ++i)
	{
		const unsigned char a = static_cast<unsigned char>(text[i]);
		const unsigned char b = static_cast<unsigned char>(prefix[i]);
		if (a == 0 || b == 0) return a == b;
		const unsigned char lower_a =
		    (a >= 'A' && a <= 'Z') ? static_cast<unsigned char>(a + 32) : a;
		const unsigned char lower_b =
		    (b >= 'A' && b <= 'Z') ? static_cast<unsigned char>(b + 32) : b;
		if (lower_a != lower_b) return false;
	}
	return true;
}

extern "C" void abi_ide_parse()  // sub_10003FA0
{
	// The original tests the string unguarded; a null zpatch_ide_string faults.
	const char* const str =
	    reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_ide_string));
	const std::uint32_t model = *reinterpret_cast<const std::uint32_t*>(
	    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(zpatch_ide_model) + 60u));
	const auto append = [](std::uint32_t* array, std::uint32_t value)
	{
		for (int i = 0; i < 512; ++i)
		{
			if (array[i] == value) return;
			if (array[i] == 0)
			{
				array[i] = value;
				return;
			}
		}
	};
	// The original's arms are independent `if`s on the same string (the
	// popuplights block falls through to the siren check), not else-if.
	if (std::strcmp(str, "popuplights") == 0)
	{
		append(zpatch_popuplights_models, model);
	}
	if (std::strcmp(str, "siren") == 0)
	{
		append(zpatch_copsirens_models, model);
	}
	if (prefix_ci(str, "wheelangle:", 11))
	{
		// sub_10003FA0's third arm: strnicmp(...,0xB) then strtof(String1+11). An
		// existing model entry is left untouched (the original's scan only breaks
		// out); the angle is stored only on append.
		const float angle = std::strtof(str + 11, nullptr);
		std::uint32_t bits = 0;
		std::memcpy(&bits, &angle, sizeof(bits));
		for (int i = 0; i < 512; ++i)
		{
			if (zpatch_wheelangle_models[i] == model) return;
			if (zpatch_wheelangle_models[i] == 0)
			{
				zpatch_wheelangle_models[i] = model;
				zpatch_wheelangle_angles[i] = bits;
				return;
			}
		}
	}
}
// @exact 0x10006310
extern "C" __declspec(naked) void abi_copsirens_a()  // 0x10006310
{
	__asm {
		mov DWORD PTR [zpatch_copsirens_index], edx
		pushad
		pushfd
		call abi_copsirens_check_a
		popfd
		popad
		mov eax, DWORD PTR [zpatch_copsirens_flag]
		jmp DWORD PTR [zpatch_copsirens_resume]
	}
}
// @exact 0x10006330
extern "C" __declspec(naked) void abi_copsirens_b()  // 0x10006330
{
	__asm {
		mov DWORD PTR [zpatch_copsirens_index_b], eax
		pushad
		pushfd
		call abi_copsirens_check_b
		popfd
		popad
		cmp DWORD PTR [zpatch_copsirens_flag_b], 1
		jmp DWORD PTR [zpatch_copsirens_resume_b]
	}
}
// @exact 0x100040E0
extern "C" __declspec(naked) void abi_copsirens_ide()  // 0x100040E0
{
	__asm {
		pushad
		mov DWORD PTR [zpatch_ide_string], edi
		mov DWORD PTR [zpatch_ide_model], eax
		call abi_ide_parse
		popad
		mov ecx, 7
		xor edx, edx
		__emit 0F3h
		__emit 0A6h               // repe cmpsb
		jmp DWORD PTR [zpatch_ide_hook_slot]
	}
}

// BorderlessWindowed: the two window-proc shims (0x10005890 = Unicode /
// 0x10005900 = ANSI). The installer replaces the game's `call [DefWindowProc]`
// with E8 -> body and NOPs the leftover byte; the body does the one-time style
// fixup then defers to the real DefWindowProc. Its `ret 0x10` consumes the
// DefWindowProc argument slots like the replaced call, so the game's own
// `pop edi/esi/ebp/ebx; add esp,0x10; ret 0x10` still unwinds correctly.
extern "C" LRESULT __stdcall abi_borderless_w(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam)
{
	if (!zpatch_borderless_w_done)
	{
		SetWindowPos(hwnd, reinterpret_cast<HWND>(static_cast<INT_PTR>(-2)), 0, 0, 0, 0, 3);
		SetWindowLongA(hwnd, GWL_STYLE,
		               GetWindowLongA(hwnd, GWL_STYLE) & static_cast<LONG>(kBorderlessStyleMask));
		SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) & ~static_cast<LONG>(8));
		zpatch_borderless_w_done = 1;
	}
	return DefWindowProcW(hwnd, msg, wparam, lparam);
}
extern "C" LRESULT __stdcall abi_borderless_a(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam)
{
	if (!zpatch_borderless_a_done)
	{
		SetWindowPos(hwnd, reinterpret_cast<HWND>(static_cast<INT_PTR>(-2)), 0, 0, 0, 0, 3);
		SetWindowLongA(hwnd, GWL_STYLE,
		               GetWindowLongA(hwnd, GWL_STYLE) & static_cast<LONG>(kBorderlessStyleMask));
		SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) & ~static_cast<LONG>(8));
		zpatch_borderless_a_done = 1;
	}
	return DefWindowProcA(hwnd, msg, wparam, lparam);
}

// SpeedupBugFix: replaces the game's timing helper (0x10050540, __thiscall on a
// _QWORD*). It lazily fills the game's eight timing-constant floats (the handler
// publishes pointers to them) from QueryPerformanceFrequency, then stores a
// 64-bit timestamp from the game's own clock fn (match-51) into *this. Compiled.
extern "C" void* __fastcall abi_speedup_body(void* self, int)
{
	if (zpatch_tls_static_header(zpatch_speedup_scan_guard))
	{
		// Mirror sub_10050587's MSVC static-init guard and dword_101A849C cache.
		// The original locates the clock fn with this signature and calls match-51
		// (the handler's `50 64 ...` sig locates the E9 site, not this fn).
		zpatch_speedup_clock = static_cast<std::uint32_t>(newfiles_find(
		    "1B D9 89 5C 24 24 0F 88"));
		// Diagnostic only: the original has no check here; a missed match makes the
		// faithful `clock - 51` call fault immediately (0xFFFFFFCD).
		zpatch_tls_static_footer(zpatch_speedup_scan_guard);
	}
	// The original dereferences the timing pointers unconditionally (its one-time
	// test is on the float, not on the pointers).
	float* const t50 = reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t50));
	if (*t50 == 0.0f)
	{
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		const double scale = 1000.0 / static_cast<double>(frequency.QuadPart);
		const float v5 = static_cast<float>(scale);
		const float v6 = static_cast<float>(scale * 1000.0);
		const float v7 = static_cast<float>(scale * 0.001000000047497451);
		const float v8 = static_cast<float>(0.001000000047497451 / scale);
		*reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t64)) = v5;
		*reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t60)) = v6;
		*reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t50)) = v7;
		*reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t5c)) = v8;
		*reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t68)) = v5;
		*reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t4c)) = v6;
		*reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t54)) = v7;
		*reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t58)) = v8;
	}
	using ClockFn = __int64(__cdecl*)(void);
	// The original calls its resolved clock function unconditionally; a failed
	// scan faults at match-51.
	const __int64 stamp = reinterpret_cast<ClockFn>(
	    static_cast<std::uintptr_t>(zpatch_speedup_clock) - 51u)();
	*reinterpret_cast<__int64*>(self) = stamp;
	return self;
}

// @exact 0x1001b040 EpisodeSwitchFreezeFix: stores eax into [ebx+0x20]; null
// returns 0 (pop edi/esi/ebx, ret 0x10), else tail-jumps to match+5.
__declspec(naked) void abi_episode_switch_store_shim()
{
	__asm {
		mov DWORD PTR [ebx + 0x20], eax
		cmp eax, 0
		je ep_store_null
		mov edx, DWORD PTR [eax]
		jmp DWORD PTR [zpatch_episode_switch_resume_a]
	ep_store_null:
		xor al, al
		pop edi
		pop esi
		pop ebx
		mov esp, ebp
		pop ebp
		ret 16
	}
}
// @exact 0x1001b060
__declspec(naked) void abi_episode_switch_shim_b()
{
	__asm {
		cmp ebx, 0
		je ep_b_null
		mov ecx, DWORD PTR [ebx + 0x10]
		cmp ecx, DWORD PTR [ebx + 0x14]
		jmp DWORD PTR [zpatch_episode_switch_resume_b]
	ep_b_null:
		mov BYTE PTR [edi], 0
		pop edi
		pop esi
		mov eax, ebp
		pop ebp
		pop ebx
		pop ecx
		ret
	}
}
// @exact 0x1001b0a0
__declspec(naked) void abi_episode_switch_vcall()
{
	__asm {
		cmp ecx, 0
		je ep_vc_null
		mov eax, ecx
		mov ecx, DWORD PTR [eax]
		mov edx, DWORD PTR [ecx]
		mov eax, DWORD PTR [eax + 4]
		mov edx, DWORD PTR [edx + 0x30]
		push eax
		call edx
		ret
	ep_vc_null:
		mov eax, 0
		ret
	}
}
// @exact 0x1001b0c0
__declspec(naked) void abi_episode_switch_shim_c()
{
	__asm {
		cmp ecx, 0
		je ep_c_null
		push esi
		mov esi, ecx
		cmp DWORD PTR [esi + 0x14], 0
		jmp DWORD PTR [zpatch_episode_switch_resume_c]
	ep_c_null:
		mov eax, 0
		ret
	}
}

// @orig 0x1001ef60 HighFPSBikePhysicsFix: saves the incoming value, clamps it,
// restores the registers, then tail-jumps to the resume (match+9).
// @exact 0x1001EF60
__declspec(naked) void abi_bike_physics_detour()
{
	__asm {
		fld DWORD PTR [ebp + 8]
		fstp DWORD PTR [zpatch_bike_value]
		pushad
		call abi_bike_physics_helper
		popad
		mov DWORD PTR [zpatch_bike_saved], eax
		mov eax, DWORD PTR [zpatch_bike_value]
		mov DWORD PTR [ebp + 8], eax
		mov eax, DWORD PTR [zpatch_bike_saved]
		fld DWORD PTR [ebp + 8]
		push ecx
		mov ecx, esi
		fstp DWORD PTR [esp]
		jmp DWORD PTR [zpatch_bike_resume]
	}
}

// @exact 0x10005d80 CollisionDeadlockFix: full replacement of the node-walk
// function the handler matches (prologue 55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48
// 66, rewritten with E9). The per-node predicate published in dword_1015BDD4 is
// NOT __stdcall: it takes the aligned entry pointer in EAX, a2 in EDX and the
// shift mask on the stack, and pops its argument (ret 4). The original also
// restores esp from ebp, discarding the predicate's cleanup. The iteration
// counter (dword_1015BDD8, zpatch_collision_deadlock_iterations) bounds the walk
// at 10000 steps.
__declspec(naked) void abi_collision_deadlock_walk()
{
	__asm {
		mov DWORD PTR [zpatch_collision_deadlock_iterations], 0
		push ebp
		mov ebp, esp
		and esp, 0xFFFFFFF8
		push ecx
		mov eax, DWORD PTR [ebp + 0x0C]
		movzx ecx, WORD PTR [eax + 0x66]
		push ebx
		mov ebx, DWORD PTR [ebp + 0x08]
		mov edx, DWORD PTR [ebx + 0x70]
		push esi
		push edi
		movzx edi, WORD PTR [edx + ecx*8]
		cmp di, 0xFFFF
		je coll_return_minus1
	coll_loop:
		mov ecx, DWORD PTR [ebx + 0x70]
		movzx eax, di
		movzx edx, WORD PTR [ecx + eax*8]
		lea esi, [ecx + eax*8]
		mov eax, DWORD PTR [esi + 4]
		and eax, 0xFFFFFFF0
		cmp dx, 0xFFFF
		je coll_skip_prefetch
		movzx edx, dx
		mov ecx, DWORD PTR [ecx + edx*8 + 4]
		and ecx, 0xFFFFFFF0
		prefetcht0 BYTE PTR [ecx]
	coll_skip_prefetch:
		mov ecx, DWORD PTR [esi + 4]
		and ecx, 3
		mov edx, 1
		shl edx, cl
		push edx
		mov edx, DWORD PTR [ebp + 0x0C]
		call DWORD PTR [zpatch_collision_deadlock_should_walk]
		test al, al
		jne coll_return_di
		movzx edi, WORD PTR [esi]
		add DWORD PTR [zpatch_collision_deadlock_iterations], 1
		; 0x2710 = 10,000: the original's walk-step bound (inline asm needs a literal)
		cmp DWORD PTR [zpatch_collision_deadlock_iterations], 0x2710
		je coll_return_minus1
		cmp di, 0xFFFF
		jne coll_loop
	coll_return_minus1:
		or ax, 0xFFFF
		pop edi
		pop esi
		pop ebx
		mov esp, ebp
		pop ebp
		ret 8
	coll_return_di:
		mov ax, di
		pop edi
		pop esi
		pop ebx
		mov esp, ebp
		pop ebp
		ret 8
	}
}

// @exact 0x10036ee0 BlackscreenFix shim: the handler publishes the original call
// target (dword_1015AD58) and a pointer to the value the game reads
// (dword_1015AD5C). Values 8..10 pass through unchanged; others run the original.
__declspec(naked) void abi_blackscreen_shim()
{
	__asm {
		mov eax, DWORD PTR [zpatch_blackscreen_value_ptr]
		mov eax, DWORD PTR [eax]
		cmp eax, 7
		jle tail
		cmp eax, 11
		jl keep
	tail:
		jmp DWORD PTR [zpatch_blackscreen_resume]
	keep:
		ret
	}
}

// 0x100455a0 SavegameFix read/copy: if (*out >= size) { if (dst) memcpy(dst, src, size);
// *out = size; return 0; } else { *out = size; return 0x8007007A; }
// __stdcall, five arguments (the fifth is unused by the original).
int __stdcall abi_savegame_fix_copy(const void* src, std::uint32_t size, void* dst,
                                    std::uint32_t* out_size, int)
{
	if (*out_size >= size)
	{
		if (dst) std::memcpy(dst, src, size);
		*out_size = size;
		return 0;
	}
	*out_size = size;
	return static_cast<int>(HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
}

// @exact 0x1001b080 EpisodeSwitchFreezeFix sum: __thiscall, no stack arguments,
// returns [ecx+10h] + [ecx+0Ch] (0 when this is null).
__declspec(naked) void abi_episode_switch_sum()
{
	__asm {
		cmp ecx, 0
		je skip
		mov eax, [ecx + 0x10]
		add eax, [ecx + 0xc]
		ret
	skip:
		mov eax, 0
		ret
	}
}

// @exact 0x10045C30 SkipIntro body: entered via E9 from match-6 with the site's
// ecx and edx; resumes by jumping to the handler-published match address
// (dword_1015AD74). The original zeroes edx below the 0x1F40 threshold, i.e. the
// intro screens are skipped outright.
__declspec(naked) void abi_skip_intro_body()
{
	__asm {
		; 0x190 = 400 (the game's per-second tick scale) and 0x1f40 = 8000 (the
		; intro-skip threshold); inline asm takes literals for both.
		imul ecx, ecx, 0x190
		cmp edx, 0x1f40
		jge ok
		xor edx, edx
	ok:
		jmp DWORD PTR [zpatch_skip_intro_match]
	}
}

// @exact 0x10046610 SkipMenu body: entered via E8 from the patched site; the
// first call with a zero argument switches the menu id to 6 unconditionally (the
// one-shot flag is the original's byte_101A8450), then resumes by jumping to the
// published address (dword_10159348 = *(match+24) + match + 28).
__declspec(naked) void abi_skip_menu_body()
{
	__asm {
		push ebp
		mov ebp, esp
		cmp BYTE PTR [zpatch_skip_menu_flag], 0
		mov eax, DWORD PTR [ebp + 8]
		jne skip
		test eax, eax
		jne skip
		mov eax, 6
		mov BYTE PTR [zpatch_skip_menu_flag], 1
	skip:
		mov DWORD PTR [ebp + 8], eax
		pop ebp
		jmp DWORD PTR [zpatch_skip_menu_resume]
	}
}

// DLC discovery post step (sub_10023150): enumerate \dlc\tlad\content\ / \TLAD\,
// \dlc\TBoGT\content\ / \TBoGT\ and \EpisodeN\ next to the executable and
// register each through the two published game RVAs. The original writes the
// module base + RVA into the struct and hands it to the register/apply functions.
extern "C" __declspec(naked) void __cdecl zpatch_dlc_apply_capture_edx(
    void*, char*, DlcApplyFn, std::uint32_t*)
{
	__asm {
		push ebp
		mov  ebp, esp
		mov  ecx, dword ptr [ebp + 8]
		push dword ptr [ebp + 0Ch]
		call dword ptr [ebp + 10h]
		mov  eax, dword ptr [ebp + 14h]
		mov  dword ptr [eax], edx
		mov  esp, ebp
		pop  ebp
		ret
	}
}

// The 0x104-byte path buffers sub_10023150 builds for each DLC folder.
constexpr std::size_t kDlcPathCapacity = 0x104u;

void* dlc_discovery_post(void* object)
{
	auto* const module_path = static_cast<char*>(::operator new(kDlcPathCapacity));
	// Original ignores the return value and continues with the allocator buffer
	// (sub_10023150); a failed GetModuleFileNameA is not a clean early return.
	GetModuleFileNameA(nullptr, module_path, kDlcPathCapacity);
	PathRemoveFileSpecA(module_path);
	std::uint32_t last_apply_edx = 0;
	bool episode254_registered = false;

	// The original picks ONE path per group: the "content" layout if it exists,
	// otherwise the legacy fallback; it never registers both.
	const auto try_paths = [&](const char* primary, const char* fallback, int id)
	{
		char path[kDlcPathCapacity];
		strcpy_s(path, kDlcPathCapacity, module_path);
		strcat_s(path, kDlcPathCapacity, primary);
		if (!PathFileExistsA(path))
		{
			if (std::strcmp(primary, fallback) == 0) return;
			strcpy_s(path, kDlcPathCapacity, module_path);
			strcat_s(path, kDlcPathCapacity, fallback);
			if (!PathFileExistsA(path)) return;
		}
		char request[360]{};
		strcpy_s(request, kDlcPathCapacity, path);
		request[358] = 1;
		*reinterpret_cast<std::uint32_t*>(request + 344) = 1;
		const auto game_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
		const auto register_fn = reinterpret_cast<DlcRegisterFn>(
		    game_base + DlcRegisterRva());
		const auto apply_fn = reinterpret_cast<DlcApplyFn>(
		    game_base + DlcApplyRva());
		register_fn(id, request, 1);
		zpatch_dlc_apply_capture_edx(object, request, apply_fn, &last_apply_edx);
		if (id == 508) episode254_registered = true;
	};

	try_paths("\\dlc\\tlad\\content\\", "\\TLAD\\", 2);
	try_paths("\\dlc\\TBoGT\\content\\", "\\TBoGT\\", 4);
	for (int i = 3; i < 255; ++i)
	{
		char suffix[kEpisodeSuffixCapacity];
		zpatch_spawns_sprintf(suffix, sizeof(suffix), "\\Episode%d\\", i);
		try_paths(suffix, suffix, 2 * i);
	}
	*reinterpret_cast<std::uint8_t*>(static_cast<std::uint8_t*>(object) + 358) = 2;
	// sub_10023150's final sub_10001280 call leaves its 0x40 buffer size in EDX;
	// PathFileExistsA preserves EDX and the caller's resume observes it. If
	// Episode254 exists, its final apply call is the last EDX producer instead
	// (captured by the asm helper above).
	if (episode254_registered)
	{
		__asm { mov edx, last_apply_edx }
	}
	else
	{
		__asm { mov edx, 40h }
	}
	return object;
}

// @exact 0x10023490 LoadDLCs struct fixup: the object arrives in ESI and the
// epilogue pops esi/ebx and cleans 0x14 stack bytes; the original instruction
// sequence is reproduced (with our own callback slot and post step).
__declspec(naked) void abi_load_dlcs_struct_fixup()
{
	__asm {
		mov BYTE PTR [esi], 0
		mov BYTE PTR [esi + 0x40], 0
		mov BYTE PTR [esi + 0x80], 0
		mov BYTE PTR [esi + 0xc0], 0
		mov BYTE PTR [esi + 0x100], 0
		cmp BYTE PTR [esi + 0x167], 0
		je load
		mov BYTE PTR [esi + 0x168], 1
	exit_label:
		pop esi
		pop ebx
		add esp, 0x14
		ret
	load:
		mov ecx, esi
		call DWORD PTR [zpatch_dlc_load_callback]
		push esi
		call dlc_discovery_post
		pop esi
		mov BYTE PTR [esi + 0xa4], 1
		jmp exit_label
	}
}

// MouseFix (callback 0x10024600): the four axis helpers the callback E9s the
// game's mouse-read functions to (0x10024060/C0/120/170), the two bodies its
// module scan installs (0x10024560 at match-0xF of game 0xA81B1F, 0x100243A0 at
// match+0x746 of the same scan) and the internal 0x10024440 helper it calls.
// Naked: they return in st(0), keep the xmm save/restore contract and round the
// doubles exactly. The one compiled helper (advance = original 0x100241E0) has
// no register contract.
// ---------------------------------------------------------------------------
extern "C" void abi_mf_angle_step();

namespace
{
// The game stores the mouse axes as 1/128 fixed point (2^-7); multiplying by the
// scale is exact, so `v * scale == 0.0` is `v == 0`.
constexpr double kAxisScale = 0.0078125;

// Vehicle fields the resume windows store to (the original's +0x144 / +0x148).
constexpr std::size_t kResumePitchOffset = 0x144;
constexpr std::size_t kResumeYawOffset = kResumePitchOffset + sizeof(float);
static_assert(kResumeYawOffset == 0x148);

// The three windows are resume-relative offsets (resume = the MouseFix handler's
// published match+0x756, dword_10159C40), not function-relative: they are stable
// only because the shape after the hook point is build-invariant.
constexpr std::size_t kPitchWindow = 52;
constexpr std::size_t kYawWindow = 94;
constexpr std::size_t kYawFpuWindow = 107;

// `movss dword ptr [esi+disp], xmmN` = F3 0F 11 /r <disp32>. /r 0x86 is xmm0,
// 0x8E is xmm1; both use the disp32 form (mod = 10, r/m = 110b = esi).
constexpr std::array<std::uint8_t, 8> MovssToEsi(std::uint8_t modrm, std::size_t field) noexcept
{
	return {kMovssStorePrefix,
	        kMovssTwoByteOpcode,
	        kMovssStoreOpcode,
	        modrm,
	        static_cast<std::uint8_t>(field & kByteMask),
	        static_cast<std::uint8_t>((field >> 8) & kByteMask),
	        static_cast<std::uint8_t>((field >> 16) & kByteMask),
	        static_cast<std::uint8_t>((field >> 24) & kByteMask)};
}

// 8x nop: the window's disabled form.
constexpr std::uint8_t kNop = 0x90;
constexpr std::array<std::uint8_t, 8> kEightNops = {kNop, kNop, kNop, kNop,
                                                    kNop, kNop, kNop, kNop};

// Window 0: store pitch from xmm0 (8 bytes exactly: prefix, opcode, modrm, disp32).
constexpr std::array<std::uint8_t, 8> kPitchStore = MovssToEsi(kModrmXmm0Esi, kResumePitchOffset);
// Window 1: store yaw from xmm1.
constexpr std::array<std::uint8_t, 8> kYawStore = MovssToEsi(kModrmXmm1Esi, kResumeYawOffset);
// Window 2: `fstp dword ptr [esi+148h]` (D9 9E + disp32) then `8B 5C`, the head of
// the next instruction, which never changes.
constexpr std::array<std::uint8_t, 8> kYawFpuStore = {
    0xD9,  // fstp dword ptr [esi+disp32]
    0x9E,
    static_cast<std::uint8_t>(kResumeYawOffset & 0xFF),
    static_cast<std::uint8_t>((kResumeYawOffset >> 8) & 0xFF),
    static_cast<std::uint8_t>((kResumeYawOffset >> 16) & 0xFF),
    static_cast<std::uint8_t>((kResumeYawOffset >> 24) & 0xFF),
    kMovEbxHead[0],
    kMovEbxHead[1]};
// Window 2's disabled form is NOT nop fill: DD D8 is `fstp st(0)`. The enclosing
// routine pushes one x87 value and pops it later, so the pop must stay or the x87
// stack overflows after eight calls. Bytes 6-7 are the preserved next instruction.
constexpr std::array<std::uint8_t, 8> kYawFpuNops = {
    kFstpSt0Bytes[0], kFstpSt0Bytes[1], kNop, kNop, kNop, kNop,
    kMovEbxHead[0], kMovEbxHead[1]};

struct ResumeWindow final
{
	std::size_t offset;
	std::array<std::uint8_t, 8> bothAxesZero;
	std::array<std::uint8_t, 8> otherwise;
};

// The recovered byte sequences (research 07 §3.1.1, original sub_100241E0).
// The body verifier reports this compiled replacement as INFO; these asserts are
// its byte proof for the data it writes, so a change is a compile error.
constexpr bool BytesEqual(const std::array<std::uint8_t, 8>& left,
                          const std::array<std::uint8_t, 8>& right) noexcept
{
	for (std::size_t i = 0; i < left.size(); ++i)
		if (left[i] != right[i]) return false;
	return true;
}
static_assert(BytesEqual(kPitchStore, std::array<std::uint8_t, 8>{
                                          0xF3, 0x0F, 0x11, 0x86, 0x44, 0x01, 0x00, 0x00}));
static_assert(BytesEqual(kYawStore, std::array<std::uint8_t, 8>{
                                        0xF3, 0x0F, 0x11, 0x8E, 0x48, 0x01, 0x00, 0x00}));
static_assert(BytesEqual(kEightNops, std::array<std::uint8_t, 8>{
                                         kNop, kNop, kNop, kNop, kNop, kNop, kNop, kNop}));
static_assert(BytesEqual(kYawFpuStore, std::array<std::uint8_t, 8>{
                                           0xD9, 0x9E, 0x48, 0x01, 0x00, 0x00, 0x8B, 0x5C}));
static_assert(BytesEqual(kYawFpuNops, std::array<std::uint8_t, 8>{
                                          0xDD, 0xD8, kNop, kNop, kNop, kNop, 0x8B, 0x5C}));

constexpr ResumeWindow kResumeWindows[] = {
    {kPitchWindow, kPitchStore, kEightNops},
    {kYawWindow, kYawStore, kEightNops},
    {kYawFpuWindow, kYawFpuStore, kYawFpuNops},
};

void zpatch_mf_advance_c() noexcept
{
	// sub_100241E0: when both mouse axes are zero, patch three 8-byte windows of
	// the resumed game function (dword_10159C40 + 52/94/107) with the "pull"
	// stores; otherwise write their disabled forms. VirtualProtect failures are
	// ignored like the original (a null resume would fault; unreachable once
	// installed).
	const auto base = static_cast<std::uintptr_t>(zpatch_mf_resume_b);
	// sub_100241E0 dereferences the resume slot unconditionally.
	const auto* x = reinterpret_cast<const std::int32_t*>(
	    static_cast<std::uintptr_t>(zpatch_mf_x));
	const auto* y = reinterpret_cast<const std::int32_t*>(
	    static_cast<std::uintptr_t>(zpatch_mf_y));
	// sub_100241E0 evaluates Y first, then X, and dereferences both unguarded.
	const bool zero = (static_cast<double>(*y) * kAxisScale == 0.0) &&
	                  (static_cast<double>(*x) * kAxisScale == 0.0);
	for (const ResumeWindow& window : kResumeWindows)
	{
		const auto& bytes = zero ? window.bothAxesZero : window.otherwise;
		// One 8-byte store keeps the window from being half-written.
		injector::WriteMemoryRaw(injector::raw_ptr(base + window.offset),
		                         const_cast<std::uint8_t*>(bytes.data()), bytes.size(), true); //write
	}
	zpatch_cookie_tail();
}
}  // namespace

// @exact 0x10024060 axis-float helper: X/Y * 0.0078125 returned in st(0).
__declspec(naked) void abi_mouse_axis_float()
{
	__asm {
		push ebp
		mov  ebp, esp
		mov  eax, dword ptr [ebp + 0Ch]
		sub  esp, 8
		test eax, eax
		jnz  mf_axis_float_y
		mov  eax, dword ptr [zpatch_mf_x]
		movd xmm0, dword ptr [eax]
		cvtdq2pd xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale]
		movsd qword ptr [ebp - 8], xmm0
		fld  qword ptr [ebp - 8]
		mov  esp, ebp
		pop  ebp
		ret
	mf_axis_float_y:
		cmp  eax, 1
		jnz  mf_axis_float_zero
		mov  eax, dword ptr [zpatch_mf_y]
		movd xmm0, dword ptr [eax]
		cvtdq2pd xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale]
		movsd qword ptr [ebp - 8], xmm0
		fld  qword ptr [ebp - 8]
		mov  esp, ebp
		pop  ebp
		ret
	mf_axis_float_zero:
		fldz
		mov  esp, ebp
		pop  ebp
		ret
		__emit 0CCh
	}
}
// @exact 0x100240C0 negated axis helper: -127.5 * value.
__declspec(naked) void abi_mouse_axis_neg()
{
	__asm {
		push ebp
		mov  ebp, esp
		mov  eax, dword ptr [ebp + 0Ch]
		test eax, eax
		jnz  mf_axis_neg_y
		mov  eax, dword ptr [zpatch_mf_x]
	mf_axis_neg_xy:
		movd xmm0, dword ptr [eax]
		cvtdq2pd xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale]
		jmp  mf_axis_neg_scale
	mf_axis_neg_y:
		cmp  eax, 1
		jnz  mf_axis_neg_zero
		mov  eax, dword ptr [zpatch_mf_y]
		jmp  mf_axis_neg_xy
	mf_axis_neg_zero:
		xorps xmm0, xmm0
	mf_axis_neg_scale:
		movss xmm1, dword ptr [zpatch_mf_sign]
		xorps xmm1, xmmword ptr [zpatch_mf_neg_mask]
		cvtpd2ps xmm0, xmm0
		mulss xmm1, xmm0
		movss dword ptr [ebp + 0Ch], xmm1
		mov  eax, dword ptr [ebp + 0Ch]
		pop  ebp
		ret
		__emit 0CCh
	}
}
// @exact 0x10024120 int axis helper: (int)(value * 0.0078125 * 255).
__declspec(naked) void abi_mouse_axis_int()
{
	__asm {
		push ebp
		mov  ebp, esp
		mov  eax, dword ptr [ebp + 0Ch]
		test eax, eax
		jnz  mf_axis_int_y
		mov  eax, dword ptr [zpatch_mf_x]
	mf_axis_int_xy:
		movd xmm0, dword ptr [eax]
		cvtdq2pd xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale]
		mulsd xmm0, qword ptr [zpatch_mf_scale_255]
		cvttsd2si eax, xmm0
		pop  ebp
		ret
	mf_axis_int_y:
		cmp  eax, 1
		jnz  mf_axis_int_zero
		mov  eax, dword ptr [zpatch_mf_y]
		jmp  mf_axis_int_xy
	mf_axis_int_zero:
		xorps xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale_255]
		cvttsd2si eax, xmm0
		pop  ebp
		ret
		__emit 0CCh
	}
}
// @exact 0x10024170 flagged float helper: negated variant that flips sign again
// when the published game byte is non-zero.
__declspec(naked) void abi_mouse_axis_flag()
{
	__asm {
		push ebp
		mov  ebp, esp
		mov  eax, dword ptr [ebp + 0Ch]
		test eax, eax
		jnz  mf_axis_flag_y
		mov  eax, dword ptr [zpatch_mf_x]
	mf_axis_flag_xy:
		movd xmm0, dword ptr [eax]
		cvtdq2pd xmm0, xmm0
		mulsd xmm0, qword ptr [zpatch_mf_scale]
		jmp  mf_axis_flag_scale
	mf_axis_flag_y:
		cmp  eax, 1
		jnz  mf_axis_flag_zero
		mov  eax, dword ptr [zpatch_mf_y]
		jmp  mf_axis_flag_xy
	mf_axis_flag_zero:
		xorps xmm0, xmm0
	mf_axis_flag_scale:
		movss xmm1, dword ptr [zpatch_mf_sign]
		xorps xmm1, xmmword ptr [zpatch_mf_neg_mask]
		mov  eax, dword ptr [zpatch_mf_flag]
		cvtpd2ps xmm0, xmm0
		cmp  byte ptr [eax], 0
		mulss xmm1, xmm0
		movss dword ptr [ebp + 0Ch], xmm1
		jz   mf_axis_flag_store
		mulss xmm1, dword ptr [zpatch_mf_mul_neg1]
		movss dword ptr [ebp + 0Ch], xmm1
	mf_axis_flag_store:
		mov  eax, dword ptr [ebp + 0Ch]
		pop  ebp
		ret
		__emit 0CCh
	}
}
// @exact 0x10024440 internal FPU helper called by abi_mouse_angle: advances the
// per-vehicle yaw (+0x148) and pitch (+0x144) by the mouse delta scaled by the
// frame-time factor, wrapping yaw into [-pi, pi]. Reads ECX (`this`).
__declspec(naked) void abi_mf_angle_step()
{
	__asm {
		push ebp
		mov  ebp, esp
		and  esp, 0FFFFFFF8h
		sub  esp, 0Ch
		push esi
		mov  esi, ecx
		call dword ptr [zpatch_mf_time]
		movss xmm2, dword ptr [zpatch_mf_step_scale]
		mov  eax, dword ptr [zpatch_mf_x]
		fstp qword ptr [esp + 8]
		movsd xmm0, qword ptr [esp + 8]
		mulsd xmm0, qword ptr [zpatch_mf_time_mul]
		movd xmm4, dword ptr [eax]
		mov  eax, dword ptr [zpatch_mf_y]
		cvtdq2pd xmm4, xmm4
		addsd xmm0, qword ptr [zpatch_mf_time_add]
		movd xmm3, dword ptr [eax]
		mulsd xmm4, qword ptr [zpatch_mf_scale]
		mov  eax, dword ptr [zpatch_mf_flag]
		cvtdq2pd xmm3, xmm3
		cmp  byte ptr [eax], 0
		xorps xmm4, xmmword ptr [zpatch_mf_double_sign]
		mulsd xmm3, qword ptr [zpatch_mf_scale]
		cvtpd2ps xmm0, xmm0
		xorps xmm3, xmmword ptr [zpatch_mf_double_sign]
		mulss xmm2, xmm0
		mulss xmm2, dword ptr [esi + 60h]
		jz   mf_angle_step_skip
		mulsd xmm3, qword ptr [zpatch_mf_mul_neg1_d]
	mf_angle_step_skip:
		movss xmm0, dword ptr [esi + 144h]
		cvtps2pd xmm2, xmm2
		cvtps2pd xmm0, xmm0
		movaps xmm1, xmm2
		mulsd xmm2, xmm4
		mulsd xmm1, xmm3
		addsd xmm1, xmm0
		cvtpd2ps xmm0, xmm1
		movss dword ptr [esi + 144h], xmm0
		movss xmm0, dword ptr [esi + 148h]
		cvtps2pd xmm0, xmm0
		addsd xmm2, xmm0
		cvtpd2ps xmm0, xmm2
		cvtps2pd xmm1, xmm0
		comisd xmm1, qword ptr [zpatch_mf_pi]
		jbe  mf_angle_step_low
		subsd xmm1, qword ptr [zpatch_mf_two_pi]
		cvtpd2ps xmm0, xmm1
		movss dword ptr [esi + 148h], xmm0
		pop  esi
		mov  esp, ebp
		pop  ebp
		ret
	mf_angle_step_low:
		movsd xmm0, qword ptr [zpatch_mf_neg_pi]
		comisd xmm0, xmm1
		jbe  mf_angle_step_store
		addsd xmm1, qword ptr [zpatch_mf_two_pi]
	mf_angle_step_store:
		cvtpd2ps xmm0, xmm1
		movss dword ptr [esi + 148h], xmm0
		pop  esi
		mov  esp, ebp
		pop  ebp
		ret
		__emit 0CCh
	}
}
// @exact 0x10024560 MouseFix body A (installed at game 0xA81B10): saves xmm0-7
// through the plugin's scratch slots, advances the angles, restores them,
// replays the hooked function's 15-byte prologue and tail-jumps to the resume
// the handler published (match, game 0xA81B1F).
__declspec(naked) void abi_mouse_angle()
{
	__asm {
		pushad
		movss dword ptr [zpatch_mf_xmm_save], xmm0
		movss dword ptr [zpatch_mf_xmm_save + 4], xmm1
		movss dword ptr [zpatch_mf_xmm_save + 8], xmm2
		movss dword ptr [zpatch_mf_xmm_save + 12], xmm3
		movss dword ptr [zpatch_mf_xmm_save + 16], xmm4
		movss dword ptr [zpatch_mf_xmm_save + 20], xmm5
		movss dword ptr [zpatch_mf_xmm_save + 24], xmm6
		movss dword ptr [zpatch_mf_xmm_save + 28], xmm7
		call abi_mf_angle_step
		movss xmm0, dword ptr [zpatch_mf_xmm_save]
		movss xmm1, dword ptr [zpatch_mf_xmm_save + 4]
		movss xmm2, dword ptr [zpatch_mf_xmm_save + 8]
		movss xmm3, dword ptr [zpatch_mf_xmm_save + 12]
		movss xmm4, dword ptr [zpatch_mf_xmm_save + 16]
		movss xmm5, dword ptr [zpatch_mf_xmm_save + 20]
		movss xmm6, dword ptr [zpatch_mf_xmm_save + 24]
		movss xmm7, dword ptr [zpatch_mf_xmm_save + 28]
		popad
		push ebp
		mov  ebp, esp
		and  esp, 0FFFFFFF0h
		xorps xmm0, xmm0
		sub  esp, 94h
		jmp  dword ptr [zpatch_mf_resume_a]
		__emit 0CCh
	}
}
// @exact 0x100243A0 MouseFix body B (installed at game 0xA82265): same xmm
// save/restore around the advance helper (sub_100241E0), then replays the three
// replaced instructions and tail-jumps to the published resume (match+0x756,
// game 0xA82275).
__declspec(naked) void abi_mouse_advance()
{
	__asm {
		pushad
		movss dword ptr [zpatch_mf_xmm_save], xmm0
		movss dword ptr [zpatch_mf_xmm_save + 4], xmm1
		movss dword ptr [zpatch_mf_xmm_save + 8], xmm2
		movss dword ptr [zpatch_mf_xmm_save + 12], xmm3
		movss dword ptr [zpatch_mf_xmm_save + 16], xmm4
		movss dword ptr [zpatch_mf_xmm_save + 20], xmm5
		movss dword ptr [zpatch_mf_xmm_save + 24], xmm6
		movss dword ptr [zpatch_mf_xmm_save + 28], xmm7
		call zpatch_mf_advance_c
		movss xmm0, dword ptr [zpatch_mf_xmm_save]
		movss xmm1, dword ptr [zpatch_mf_xmm_save + 4]
		movss xmm2, dword ptr [zpatch_mf_xmm_save + 8]
		movss xmm3, dword ptr [zpatch_mf_xmm_save + 12]
		movss xmm4, dword ptr [zpatch_mf_xmm_save + 16]
		movss xmm5, dword ptr [zpatch_mf_xmm_save + 20]
		movss xmm6, dword ptr [zpatch_mf_xmm_save + 24]
		movss xmm7, dword ptr [zpatch_mf_xmm_save + 28]
		popad
		mulss xmm0, xmm2
		addss xmm0, xmm3
		movss xmm3, dword ptr [esi + 150h]
		jmp  dword ptr [zpatch_mf_resume_b]
		__emit 0CCh
	}
}

// OutOfCommissionFix (callback 0x1004C3F0). Scan 1 (A9 39 44 24 18 at game
// 0xA22E90) E9s match+7 to the branch-widening body and publishes match+0x11 as
// its resume. Scan 2 (E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56 at game 0xB70733)
// E8s the match to the wrapper and publishes dword_10159BF8 (the E8 at
// match-0x33's target) / dword_10159C04 (the match E8's original target).
// ---------------------------------------------------------------------------
namespace
{
// OutOfCommissionFix's real payload: the original embeds a compressed
// "finale2.sco" (163796 bytes at 0x100B6798); its init (sub_1004B750 ->
// sub_1004B570/sub_1004B150) decodes it into a 179536-byte table, registers it,
// then sub_1004ACE0 builds an in-memory RPF archive around it. The wrapper mounts
// that archive with the game's own mount function ("memory:$<ptr>,<size>,0:") and
// the E9 body widens the game's script-id check to accept its resource magic.
// The decode below mirrors sub_1004B150's opcode loop.
constexpr std::size_t kOocTableSize = 0x2BD50u;  // 179536
constexpr const unsigned char* kOocPakBlob = zpatch_ooc_blob;

inline void zpatch_ooc_store(std::uint32_t* destination,
                             std::uint32_t value) noexcept
{
	*reinterpret_cast<volatile std::uint32_t*>(destination) = value;
}

struct OocPakEntry final
{
	char name_buffer[16];
	std::uint32_t name_size;
	std::uint32_t name_capacity;
	std::uint32_t file_size;
	std::uint32_t stored_offset;
	std::uint32_t stored_size;
	std::uint32_t padding;
	std::uint32_t data;
};
static_assert(sizeof(OocPakEntry) == 44);

void zpatch_ooc_register_entry(unsigned char* table)
{
	auto* begin = reinterpret_cast<OocPakEntry*>(
	    static_cast<std::uintptr_t>(zpatch_ooc_vector_begin));
	auto* end = reinterpret_cast<OocPakEntry*>(
	    static_cast<std::uintptr_t>(zpatch_ooc_vector_end));
	auto* capacity = reinterpret_cast<OocPakEntry*>(
	    static_cast<std::uintptr_t>(zpatch_ooc_vector_capacity));
	const std::size_t size = begin == nullptr ? 0u
	                                           : static_cast<std::size_t>(end - begin);
	const std::size_t cap = begin == nullptr ? 0u
	                                          : static_cast<std::size_t>(capacity - begin);
	bool reallocated = false;
	if (size == cap)
	{
		const std::size_t new_capacity =
		    (cap + cap / 2u > size) ? cap + cap / 2u : size + 1u;
		auto* replacement = static_cast<OocPakEntry*>(
		    ::operator new(new_capacity * sizeof(OocPakEntry)));
		if (size != 0u) std::memcpy(replacement, begin, size * sizeof(OocPakEntry));
		if (begin != nullptr) ::operator delete(begin);
		begin = replacement;
		end = begin + size;
		capacity = begin + new_capacity;
		reallocated = true;
	}

	OocPakEntry entry{};
	std::memcpy(entry.name_buffer, "finale2.sco", sizeof("finale2.sco"));
	entry.name_size = 11u;
	entry.name_capacity = 15u;
	entry.file_size = static_cast<std::uint32_t>(kOocTableSize);
	entry.data = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table));
	std::memcpy(end, &entry, sizeof(entry));
	++end;
	if (reallocated)
	{
		zpatch_ooc_store(&zpatch_ooc_vector_begin,
		                 static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(begin)));
		zpatch_ooc_store(&zpatch_ooc_vector_end,
		                 static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(end)));
		zpatch_ooc_store(&zpatch_ooc_vector_capacity,
		                 static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(capacity)));
	}
	else
	{
		zpatch_ooc_store(&zpatch_ooc_vector_end,
		                 static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(end)));
	}
}

// The finale2 stream's opcode format. The names mirror the decoder branches;
// the code values are part of the resource format, not arbitrary numbers.
constexpr std::uint8_t kOocOpLiteral = 0x20;
constexpr std::uint8_t kOocOpLongBackref = 0x18;
constexpr std::uint8_t kOocOpWideBackref = 0x10;
constexpr std::uint8_t kOocOpMediumLiteral = 0x08;
constexpr std::uint8_t kOocOpLongLiteral = 0x07;
constexpr std::uint8_t kOocOpShortBackref = 0x06;
constexpr std::uint8_t kOocOpWideLiteralBackref = 0x04;
constexpr std::uint8_t kOocOpShortDistance = 0x40;
constexpr std::uint8_t kOocOpShortestDistance = 0x80;
constexpr std::uint8_t kOocOpEnd = 0x05;
constexpr std::uint32_t kOocByteBase = 256u;
constexpr std::uint32_t kOocWordBase = 65536u;
constexpr std::uint32_t kOocLongDistanceBias = 1572863u;
constexpr std::uint32_t kOocWideDistanceBias = 0xFFFFFu;
constexpr std::uint32_t kOocShortDistanceBias = 0x3FFFu;
constexpr std::uint8_t kOocLiteralBias = 31u;
constexpr std::uint8_t kOocLiteralAdvance = 30u;
constexpr std::uint8_t kOocShortestBias = 127u;
constexpr std::uint32_t kOocMediumLiteralBias = 2047u;
constexpr std::uint32_t kOocAdlerModulus = 0xFFF1u;
constexpr std::size_t kOocAdlerBlock = 1872u;

struct OocStream final
{
	std::size_t cursor = 0;  // the original's dword_101A8430
	unsigned char* dest = nullptr;

	void literal(std::size_t count, std::size_t from) noexcept
	{
		if (cursor + count <= kOocTableSize)
		{
			// The original's source bound (a1 + n >= dword_101887D4 = blob base - 16)
			// is constant-true: the stream starts past the base pointer.
			std::memcpy(dest + cursor, kOocPakBlob + from, count);
			cursor += count;
		}
		else
		{
			cursor = kOocTableSize + 1;
		}
	}
	void backref(std::size_t distance, std::size_t count) noexcept
	{
		if (cursor + count > kOocTableSize || distance > cursor)
		{
			cursor = kOocTableSize + 1;
			return;
		}
		unsigned char* out = dest + cursor;
		const unsigned char* in = out - distance;
		for (std::size_t i = 0; i < count; ++i) out[i] = in[i];  // overlap propagates
		cursor += count;
	}
	std::size_t step(std::size_t pc) noexcept
	{
		const unsigned char op = kOocPakBlob[pc];
		if (op < kOocOpLiteral)
		{
			if (op >= kOocOpLongBackref)
			{
				backref(kOocPakBlob[pc + 2] + kOocByteBase * kOocPakBlob[pc + 1] +
				            kOocWordBase * op - kOocLongDistanceBias,
				        kOocPakBlob[pc + 3] + 1u);
				return pc + 4;
			}
			if (op >= kOocOpWideBackref)
			{
				backref(kOocPakBlob[pc + 2] + kOocByteBase * kOocPakBlob[pc + 1] +
				            kOocWordBase * op - kOocWideDistanceBias,
				        (kOocPakBlob[pc + 3] << 8) + kOocPakBlob[pc + 4] + 1u);
				return pc + 5;
			}
			if (op >= kOocOpMediumLiteral)
			{
				const std::size_t count = kOocByteBase * op + kOocPakBlob[pc + 1] - kOocMediumLiteralBias;
				literal(count, pc + 2);
				return pc + 2 + count;
			}
			if (op == kOocOpLongLiteral)
			{
				const std::size_t count = kOocByteBase * kOocPakBlob[pc + 1] + kOocPakBlob[pc + 2] + 1u;
				literal(count, pc + 3);
				return pc + 3 + count;
			}
			if (op == kOocOpShortBackref)
			{
				backref(kOocPakBlob[pc + 3] + kOocByteBase * kOocPakBlob[pc + 2] +
				            kOocWordBase * kOocPakBlob[pc + 1] + 1u,
				        kOocPakBlob[pc + 4] + 1u);
				return pc + 5;
			}
			if (op == kOocOpWideLiteralBackref)
			{
				backref(kOocPakBlob[pc + 3] + kOocByteBase * kOocPakBlob[pc + 2] +
				            kOocWordBase * kOocPakBlob[pc + 1] + 1u,
				        (kOocPakBlob[pc + 4] << 8) + kOocPakBlob[pc + 5] + 1u);
				return pc + 6;
			}
			return pc;  // no-progress opcode terminates the stream
		}
		if (op < kOocOpShortestDistance)
		{
			if (op >= kOocOpShortDistance)
			{
				backref(kOocPakBlob[pc + 1] + kOocByteBase * op -
				            kOocShortDistanceBias,
				        kOocPakBlob[pc + 2] + 1u);
				return pc + 3;
			}
			literal(op - kOocLiteralBias, pc + 1);
			return pc + (op - kOocLiteralAdvance);
		}
		backref(kOocPakBlob[pc + 1] + 1u, op - kOocShortestBias);
		return pc + 2;
	}
};

// The original's Adler-32 variant (a = 1, b = 0, 1872-byte partition, 32-bit
// wrapping); reproduced because the blob's trailer was built with it.
std::uint32_t zpatch_ooc_adler32(const unsigned char* data, std::size_t size) noexcept
{
	std::uint32_t a = 1u;
	std::uint32_t b = 0u;
	for (std::size_t block = 0; block < size; block += kOocAdlerBlock)
	{
		const std::size_t end = (block + kOocAdlerBlock < size) ? block + kOocAdlerBlock : size;
		for (std::size_t i = block; i < end; ++i)
		{
			a += data[i];
			b += a;
		}
		a %= kOocAdlerModulus;
		b %= kOocAdlerModulus;
	}
	return (b << 16) | a;
}

bool zpatch_ooc_decode(unsigned char* table) noexcept
{
	// sub_1004B570: decode until the no-progress opcode, then require the
	// 05 FA terminator, the exact destination size and the Adler trailer.
	const auto table_begin = reinterpret_cast<std::uintptr_t>(table);
	zpatch_ooc_store(&zpatch_ooc_input_bound,
	                 static_cast<std::uint32_t>(
	                     reinterpret_cast<std::uintptr_t>(kOocPakBlob) - 16u));
	zpatch_ooc_store(&zpatch_ooc_output_begin,
	                 static_cast<std::uint32_t>(table_begin));
	zpatch_ooc_store(&zpatch_ooc_output_cursor,
	                 static_cast<std::uint32_t>(table_begin));
	zpatch_ooc_store(&zpatch_ooc_output_end,
	                 static_cast<std::uint32_t>(table_begin + kOocTableSize));
	OocStream stream{0, table};
	std::size_t prior_cursor = stream.cursor;
	std::size_t pc = stream.step(0);
	if (stream.cursor != prior_cursor)
	{
		zpatch_ooc_store(&zpatch_ooc_output_cursor,
		                 static_cast<std::uint32_t>(table_begin + stream.cursor));
	}
	bool terminated = pc == 0;
	while (!terminated && stream.cursor <= kOocTableSize)
	{
		const std::size_t previous = pc;
		prior_cursor = stream.cursor;
		pc = stream.step(pc);
		if (stream.cursor != prior_cursor)
		{
			zpatch_ooc_store(&zpatch_ooc_output_cursor,
			                 static_cast<std::uint32_t>(table_begin + stream.cursor));
		}
		if (pc == previous) terminated = true;
	}
	if (!terminated || stream.cursor != kOocTableSize) return false;
	if (kOocPakBlob[pc] != 5u || kOocPakBlob[pc + 1] != kOocPakMarker) return false;
	const std::uint32_t trailer = (static_cast<std::uint32_t>(kOocPakBlob[pc + 2]) << 24) |
	                              (static_cast<std::uint32_t>(kOocPakBlob[pc + 3]) << 16) |
	                              (static_cast<std::uint32_t>(kOocPakBlob[pc + 4]) << 8) |
	                              static_cast<std::uint32_t>(kOocPakBlob[pc + 5]);
	return zpatch_ooc_adler32(table, kOocTableSize) == trailer;
}

// sub_1004ACE0 for the single registered entry ("finale2.sco", table, 179536).
// 20-byte header, one 16-byte record, the name, then the payload at offset 2048
// followed by the zero padding to the next 2048 boundary. The table is freed like
// the original; `final_offset` is the original's dword_10159C78.
// Archive page size (the 0x800 granularity of the payload/records).
constexpr std::uint32_t kOocPageSize = 2048u;

unsigned char* zpatch_ooc_build_archive(unsigned char* table,
                                        std::uint32_t& final_offset) noexcept
{
	const std::uint32_t size = static_cast<std::uint32_t>(kOocTableSize);
	const std::uint32_t remainder = size & (kOocPageSize - 1u);
	const std::uint32_t pages = size / kOocPageSize + (remainder != 0u ? 1u : 0u);
	const std::uint32_t total = pages * kOocPageSize + kOocPageSize;
	// No null check, like the original's archive build: the memset below faults
	// on a failed allocation.
	auto* archive = static_cast<unsigned char*>(std::malloc(total));
	std::memset(archive, 0, total);
	std::uint32_t offset = 0;
	const auto append = [&](const void* source, std::uint32_t bytes) noexcept
	{
		std::memcpy(archive + offset, source, bytes);
		offset += bytes;
		zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
	};
	zpatch_ooc_store(&zpatch_ooc_archive_offset, 0);
	const char name[] = "finale2.sco";
	const std::uint32_t name_bytes = sizeof(name);  // includes the NUL
	struct OocHeader final
	{
		std::uint32_t fields[4];
		std::uint16_t words[2];
	};
	static_assert(sizeof(OocHeader) == 20);
	const OocHeader header{{kOocArchiveMagic, 3u, 1u, name_bytes + 16u},
	                       {kOocHeaderPage, kOocHeaderOp}};
	append(&header, sizeof(header));
	unsigned char record[16] = {};
	std::memcpy(record, &size, 4);                 // physical size (entry[0x20])
	const std::uint32_t index = 1u;                // v9 + 1
	std::memcpy(record + 8, &index, 4);
	const std::uint16_t record_pages = static_cast<std::uint16_t>(pages);
	std::memcpy(record + 12, &record_pages, 2);    // sub_1004ABB0
	if (remainder != 0u)
	{
		const std::uint16_t padding =
		    static_cast<std::uint16_t>(kOocPageSize - remainder);
		const std::uint16_t packed =
		    static_cast<std::uint16_t>((static_cast<std::uint16_t>(0u) & kOocPadMask) | padding);
		std::memcpy(record + 14, &packed, 2);        // sub_1004AB90
	}
	append(record, 16);
	append(name, name_bytes);
	offset = kOocPageSize;                         // the original resets dword_10159C78
	zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
	append(table, size);
	std::free(table);                              // j_j_free inside the payload loop
	if (remainder != 0u)
		offset += kOocPageSize - remainder;        // zero padding (already memset)
	if (remainder != 0u) zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
	final_offset = offset;
	return archive;
}

// sub_1004B750: the one-shot init the wrapper runs before mounting. Its guard
// dword_10159C00 has no writer in the original, so it re-runs (and leaks the
// archive) on every call - kept 1:1. Both mallocs are unchecked like the
// original.
std::uint32_t zpatch_ooc_lookup() noexcept
{
	auto* table = static_cast<unsigned char*>(std::malloc(kOocTableSize));
	static_cast<void>(zpatch_ooc_decode(table));  // the original discards the result
	zpatch_ooc_store(&zpatch_ooc_table_size,
	                 static_cast<std::uint32_t>(kOocTableSize));
	zpatch_ooc_register_entry(table);
	std::uint32_t final_offset = 0;
	unsigned char* archive = zpatch_ooc_build_archive(table, final_offset);
	zpatch_ooc_store(&zpatch_ooc_vector_end,
	                 zpatch_ooc_vector_begin);
	zpatch_ooc_store(&zpatch_ooc_state, final_offset);
	return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(archive));
}

std::uint32_t zpatch_ooc_fnv1a(const unsigned char* data, std::size_t size) noexcept
{
	std::uint32_t hash = 2166136261u;
	for (std::size_t i = 0; i < size; ++i)
	{
		hash ^= data[i];
		hash *= 16777619u;
	}
	return hash;
}
}  // namespace

// Offline self-test for OutOfCommissionFix: the decode must reproduce the blob's
// own Adler trailer and the archive must match the golden layout/hash.
extern "C" bool zpatch_ooc_selftest() noexcept
{
	auto* table = static_cast<unsigned char*>(std::malloc(kOocTableSize));
	if (table == nullptr) return false;
	const bool decoded = zpatch_ooc_decode(table);
	const std::uint32_t table_adler = zpatch_ooc_adler32(table, kOocTableSize);
	std::uint32_t final_offset = 0;
	unsigned char* archive = zpatch_ooc_build_archive(table, final_offset);
	if (archive == nullptr)
	{
		std::free(table);
		return false;
	}
	const std::uint32_t header[4] = {kOocArchiveMagic, 3u, 1u, 28u};
	bool header_ok = true;
	for (int i = 0; i < 4; ++i)
		header_ok = header_ok && std::memcmp(archive + 4 * i, &header[i], 4) == 0;
	header_ok = header_ok && archive[16] == kOocHeaderPage && archive[17] == 0u &&
	            archive[18] == kOocHeaderOp && archive[19] == 0u;
	const std::uint32_t record_size = static_cast<std::uint32_t>(kOocTableSize);
	const std::uint32_t record_index = 1u;
	bool record_ok = std::memcmp(archive + 20, &record_size, 4) == 0;
	const std::uint32_t zero = 0u;
	record_ok = record_ok && std::memcmp(archive + 24, &zero, 4) == 0 &&
	            std::memcmp(archive + 28, &record_index, 4) == 0;
	record_ok = record_ok && archive[32] == 88u && archive[33] == 0u &&
	            archive[34] == kOocRecordTag && archive[35] == kOocRecordTag2;
	const bool name_ok = std::memcmp(archive + 36, "finale2.sco", 12) == 0;
	const bool ok = decoded && final_offset == kOocDecodedLength && table_adler == kOocTableAdler &&
	                header_ok && record_ok && name_ok &&
	                zpatch_ooc_fnv1a(archive, final_offset) == kOocArchiveFnv;
	std::free(archive);
	return ok;
}

// @exact 0x1004C3C0 OutOfCommissionFix branch body (E9 target of game
// 0xA22E97): replay `cmp [esp+0x30], eax` + `movq xmm0, [esp+0x30]`; when the
// comparison was unequal, re-compare against the original's 0x86723DFF magic so
// the game's following `jz` also accepts the injected value; resume at match+0x11.
__declspec(naked) void abi_ooc_branch()
{
	__asm {
		cmp  dword ptr [esp + 30h], eax
		movq xmm0, qword ptr [esp + 30h]
		jne  ooc_branch_check
		jmp  dword ptr [zpatch_ooc_resume]
	ooc_branch_check:
		mov  eax, 86723DFFh
		cmp  dword ptr [esp + 30h], eax
		jmp  dword ptr [zpatch_ooc_resume]
		__emit 0CCh
	}
}

// 0x1004C350 OutOfCommissionFix wrapper (E8 target of game 0xB70733): run the
// one-shot fake-pak registration, print the fixed model name through the
// published logger, then call the game function the E8 replaced with the
// original arguments. Its register use is cdecl-clean, so it is ordinary C++.
extern "C" std::uint32_t abi_ooc_wrap(std::uint32_t arg0, std::uint32_t arg1) noexcept
{
	const std::uint32_t lookup = zpatch_ooc_lookup();
	if (lookup != 0u)
	{
		char buffer[kOocPathCapacity];
		zpatch_spawns_sprintf(buffer, kSpawnsNameCapacity, zpatch_ooc_fmt_b, zpatch_ooc_fmt_a,
		                      ':', '$', static_cast<int>(lookup),
		                      static_cast<int>(zpatch_ooc_state), '0', ':', 0);
		const auto logger = reinterpret_cast<void(__cdecl*)(char*, int, int)>(
		    static_cast<std::uintptr_t>(zpatch_ooc_logger));
		// sub_1004C350 calls the logger and the original target unguarded.
		logger(buffer, 1, -1);
	}
	const auto original = reinterpret_cast<std::uint32_t(__cdecl*)(std::uint32_t, std::uint32_t)>(
	    static_cast<std::uintptr_t>(zpatch_ooc_orig));
	return original(arg0, arg1);
}

// HighQualityReflections (callback 0x1001F390): scans for
// `8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8` (unique on every classic build
// and EFLC 1110/1120/1130; absent on 12043), E9s the match to the detour below,
// and publishes dword_10159CFC = match+0x13 (resume) and
// dword_10159D04 = match + *(match+0xF) + 0x13 (the draw-command builder called).
// @exact 0x1001F030 detour body: dumps the registers (the original's crash aid),
// calls the game's draw-command builder 12 times with (index 0..11, flags
// 0x11D, mode 0) and resumes at match+0x13 (tail-jumped).
__declspec(naked) void abi_hqr_body()
{
	__asm {
			mov       dword ptr [zpatch_hqr_dump], eax
			mov       dword ptr [zpatch_hqr_dump+4], ebx
			mov       dword ptr [zpatch_hqr_dump+8], ecx
			mov       dword ptr [zpatch_hqr_dump+12], edx
			mov       dword ptr [zpatch_hqr_dump+16], esi
			mov       dword ptr [zpatch_hqr_dump+20], edi
			mov       dword ptr [zpatch_hqr_dump+24], ebp
			mov       dword ptr [zpatch_hqr_dump+28], esp
			mov       eax, 0
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 1
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 2
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 3
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 4
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 5
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 6
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 7
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 8
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 9
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 0xa
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			mov       eax, dword ptr [zpatch_hqr_dump]
			mov       ebx, dword ptr [zpatch_hqr_dump+4]
			mov       ecx, dword ptr [zpatch_hqr_dump+8]
			mov       edx, dword ptr [zpatch_hqr_dump+12]
			mov       esi, dword ptr [zpatch_hqr_dump+16]
			mov       edi, dword ptr [zpatch_hqr_dump+20]
			mov       ebp, dword ptr [zpatch_hqr_dump+24]
			mov       esp, dword ptr [zpatch_hqr_dump+28]
			mov       eax, 0xb
			push      dword ptr [zpatch_hqr_mode]
			push      dword ptr [zpatch_hqr_flags]
			push      eax
			call      dword ptr [zpatch_hqr_builder]
			jmp       dword ptr [zpatch_hqr_resume]
			__emit 0CCh
	}
}

// The worker's sub_10003980: the resolver call with the salt in ECX and ESI.
extern "C" __declspec(naked) std::uint32_t zpatch_cutscene_scene()
{
	__asm {
		push esi
		mov esi, ecx
		mov eax, dword ptr [zpatch_driveby_resolver]
		call eax
		pop esi
		ret
	}
}
// The worker's sub_1005FE7B cookie check (cookie in ECX), same body as the
// spawns probe's: it must preserve EAX/AL because the cutscene body returns
// its result in AL across this call.
__declspec(naked) void zpatch_cutscene_cookie_check()
{
	__asm {
		jmp zpatch_spawns_cookie_check_ecx
	}
}
// 0x100388C0 CutsceneFixes chain B (original sub_100388C0..0x10038B3F, the dest
// of the E8 installs the worker 0x10038B40 writes at the two camera call sites).
// thiscall char(float). Reads a scene float through the loader-published resolver
// (salt dword_1015803C, ecx/esi), pokes the camera function's `mov byte
// [esp+0x1C],imm` at target+0x4E, calls it and, once the accumulated frame time
// passes 0.3333, repeats with the immediate 0 and restores the pre-call object
// when the position floats differ beyond (0.05,0.05,0.05,0.5,0.5,0.5).
// @exact 0x100388C0
__declspec(naked) void abi_cutscene_chain_b()
{
	__asm {
		push ebp
		mov ebp, esp
		sub esp, 0A18h
		mov eax, dword ptr [__security_cookie]
		xor eax, ebp
		mov dword ptr [ebp - 4], eax
		push esi
		mov esi, ecx
		mov ecx, dword ptr [zpatch_salt_3c]
		push edi
		mov dword ptr [ebp - 0A10h], esi
		call zpatch_cutscene_scene
		lea edi, [ebp - 504h]
		mov ecx, dword ptr [eax + 0Eh]
		mov eax, dword ptr [ecx + eax + 18h]
		mov ecx, 140h
		rep movsd
		movss xmm0, dword ptr [eax + 8]
		lea eax, [ebp - 0A08h]
		mov esi, dword ptr [zpatch_cutscene_poke]
		mov edi, dword ptr [zpatch_cutscene_virtualprotect]
		dec esi
		cvtps2pd xmm0, xmm0
		push eax
		push 40h
		push 1
		push esi
		addsd xmm0, qword ptr [zpatch_cutscene_accum]
		movsd qword ptr [zpatch_cutscene_accum], xmm0
		call edi
		lea eax, [ebp - 0A08h]
		mov byte ptr [esi], 1
		push eax
		push dword ptr [ebp - 0A08h]
		push 1
		push esi
		call edi
		mov eax, dword ptr [zpatch_cutscene_target]
		movss xmm0, dword ptr [ebp + 8]
		push ecx
		mov ecx, dword ptr [ebp - 0A10h]
		mov dword ptr [ebp - 0A14h], eax
		movss dword ptr [esp], xmm0
		call dword ptr [ebp - 0A14h]
		fstp qword ptr [ebp - 0A18h]
		movsd xmm0, qword ptr [ebp - 0A18h]
		ucomisd xmm0, qword ptr [zpatch_cutscene_zero]
		lahf
		test ah, 44h
		jnp ccb_al_zero
		mov al, 1
		jmp ccb_al_done
	ccb_al_zero:
		xor al, al
	ccb_al_done:
		mov edx, dword ptr [ebp - 0A10h]
		lea edi, [ebp - 0A04h]
		movsd xmm0, qword ptr [zpatch_cutscene_limit]
		mov ecx, 140h
		comisd xmm0, qword ptr [zpatch_cutscene_accum]
		mov esi, edx
		mov byte ptr [ebp - 0A09h], al
		rep movsd
		ja ccb_return_al
		mov edi, edx
		lea eax, [ebp - 0A08h]
		push eax
		mov ecx, 140h
		lea esi, [ebp - 504h]
		rep movsd
		mov esi, dword ptr [zpatch_cutscene_poke]
		mov edi, dword ptr [zpatch_cutscene_virtualprotect]
		dec esi
		push 40h
		push 1
		push esi
		call edi
		lea eax, [ebp - 0A08h]
		mov byte ptr [esi], 0
		push eax
		push dword ptr [ebp - 0A08h]
		push 1
		push esi
		call edi
		mov eax, dword ptr [zpatch_cutscene_target]
		movss xmm0, dword ptr [ebp + 8]
		push ecx
		mov ecx, dword ptr [ebp - 0A10h]
		mov dword ptr [ebp - 0A14h], eax
		movss dword ptr [esp], xmm0
		call dword ptr [ebp - 0A14h]
		fstp qword ptr [ebp - 0A18h]
		movsd xmm0, qword ptr [ebp - 0A18h]
		xorps xmm3, xmm3
		ucomisd xmm0, xmm3
		lahf
		test ah, 44h
		jnp ccb_dl_zero
		mov dl, 1
		jmp ccb_dl_done
	ccb_dl_zero:
		xor dl, dl
	ccb_dl_done:
		mov eax, dword ptr [ebp - 0A10h]
		lea edi, [ebp - 504h]
		movss xmm1, dword ptr [zpatch_cutscene_abs_mask]
		mov ecx, 140h
		movss xmm2, dword ptr [zpatch_cutscene_eps_a]
		mov esi, eax
		rep movsd
		movss xmm0, dword ptr [ebp - 4E4h]
		subss xmm0, dword ptr [ebp - 9E4h]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		movss xmm0, dword ptr [ebp - 4E0h]
		subss xmm0, dword ptr [ebp - 9E0h]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		movss xmm0, dword ptr [ebp - 4DCh]
		subss xmm0, dword ptr [ebp - 9DCh]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		movss xmm0, dword ptr [ebp - 4C4h]
		subss xmm0, dword ptr [ebp - 9C4h]
		movss xmm2, dword ptr [zpatch_cutscene_eps_b]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		movss xmm0, dword ptr [ebp - 4C0h]
		subss xmm0, dword ptr [ebp - 9C0h]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		movss xmm0, dword ptr [ebp - 4BCh]
		subss xmm0, dword ptr [ebp - 9BCh]
		andps xmm0, xmm1
		comiss xmm0, xmm2
		ja ccb_restore
		pop edi
		mov al, dl
		pop esi
		mov ecx, dword ptr [ebp - 4]
		xor ecx, ebp
		call zpatch_cutscene_cookie_check
		mov esp, ebp
		pop ebp
		ret 4
	ccb_restore:
		mov edi, eax
		movsd qword ptr [zpatch_cutscene_accum], xmm3
		mov al, byte ptr [ebp - 0A09h]
		lea esi, [ebp - 0A04h]
		mov ecx, 140h
		rep movsd
	ccb_return_al:
		mov ecx, dword ptr [ebp - 4]
		pop edi
		xor ecx, ebp
		pop esi
		call zpatch_cutscene_cookie_check
		mov esp, ebp
		pop ebp
		ret 4
		__emit 0CCh
	}
}

// NewMemorySystem: the reimplemented CRageAllocator whose vtable (original
// 0x100B5038) the E8 body writes into the game's allocator singleton. The
// original swaps the arena allocator for a HeapAlloc-backed one: allocate() uses
// unit = max(0x10, align), want = max(size, unit), HeapAllocs want + 2*unit,
// rounds up to ((base-1)/unit + 2)*unit, stores the base offset at [p-4], fills
// want bytes with 0xCD and registers the pointer under the critical section at
// 0x101A0808; free() recovers the base from [p-4], HeapFrees and unregisters.
// Only vtable[18] observes the registry, so a plain set suffices.
// The allocator vtable's small methods. The original's calling conventions
// are preserved; only the constructor and thunk still need their exact
// prologue/tail-jump.
extern "C" void __cdecl abi_nms_noop_ret0() {}

// 0x10038640: constructor (thiscall; one stack argument, callee pops it).
extern "C" __declspec(naked) void* abi_nms_ctor()
{
	__asm {
		push ebp
		mov ebp, esp
		mov eax, dword ptr [ebp + 8]
		push esi
		mov esi, ecx
		and eax, 5
		mov dword ptr [esi], offset zpatch_nms_vtable
		cmp al, 5
		jne nms_ctor_done
		push 4
		push esi
		call abi_nms_noop_ret0
		add esp, 8
	nms_ctor_done:
		mov eax, esi
		pop esi
		pop ebp
		ret 4
		__emit 0CCh
	}
}

// 0x10038350
extern "C" bool __stdcall abi_nms_reject_ptr(void*) { return false; }

// 0x10038540
extern "C" void* __fastcall abi_nms_self(void* self, void* /*edx*/, void*)
{
	return self;
}

// 0x10001950
extern "C" void* __fastcall abi_nms_zero_ptr(void* /*self*/, void* /*edx*/, void*)
{
	return nullptr;
}

// 0x10038580
extern "C" void __stdcall abi_nms_noop_pair(void*, void*) {}

// 0x10038590
extern "C" bool __cdecl abi_nms_reject() { return false; }

// 0x10038620: forward through vtable[6].
// Tail-jumps, so the caller's return address and EBP stay untouched.
extern "C" __declspec(naked) void abi_nms_thunk18()
{
	__asm {
		push ebp
		mov ebp, esp
		mov eax, dword ptr [ecx]
		pop ebp
		jmp dword ptr [eax + 18h]
		__emit 0CCh
	}
}

// 0x10038630
extern "C" void __stdcall abi_nms_noop_arg(void*) {}

// 0x10038670: install the vtable in the game's allocator singleton.
extern "C" void __cdecl abi_nms_apply()
{
	auto* object = reinterpret_cast<void**>(zpatch_nms_object);
	if (object) *object = zpatch_nms_vtable;
}

namespace
{
// NewMemorySystem's allocation registry: the original keeps the pointers it
// handed out in its own hash map (FNV-1a over the pointer's four bytes) under
// the recursive critical section at 0x101A0808. Only membership is observable,
// so the keys go in a plain set behind an equivalent lock.
CRITICAL_SECTION g_nms_lock;
std::unordered_set<std::uint32_t> g_nms_allocations;
struct NmsRegistryInit final
{
	NmsRegistryInit() noexcept { InitializeCriticalSection(&g_nms_lock); }
};
NmsRegistryInit g_nms_registry_init;
}  // namespace

// vtable[2] (0x10038360): HeapAlloc-backed allocate with the original's
// alignment scheme. ECX is the object (unused by the original's body too).
extern "C" void* __stdcall zpatch_nms_alloc(std::uint32_t size, std::uint32_t align,
                                            std::uint32_t flags) noexcept
{
	(void)flags;
	const std::uint32_t unit = align < 0x10u ? 0x10u : align;
	const std::uint32_t want = size < unit ? unit : size;
	void* base = HeapAlloc(GetProcessHeap(), 0, want + unit * 2u);
	if (!base) return nullptr;
	const auto raw = reinterpret_cast<std::uintptr_t>(base);
	const std::uintptr_t aligned = ((raw - 1u) / unit + 2u) * unit;
	*reinterpret_cast<std::uint32_t*>(aligned - 4u) =
	    static_cast<std::uint32_t>(aligned - raw);
	auto* result = reinterpret_cast<void*>(aligned);
	std::memset(result, kUninitialisedHeapPoison, want);
	EnterCriticalSection(&g_nms_lock);
	g_nms_allocations.insert(static_cast<std::uint32_t>(aligned));
	LeaveCriticalSection(&g_nms_lock);
	return result;
}

// vtable[3] (0x10038430): recover the HeapAlloc base from [ptr-4], free it and
// drop the pointer from the registry.
extern "C" void __stdcall zpatch_nms_free(void* ptr) noexcept
{
	if (!ptr) return;
	const auto raw = reinterpret_cast<std::uintptr_t>(ptr);
	const std::uintptr_t base = raw - *reinterpret_cast<const std::uint32_t*>(raw - 4u);
	HeapFree(GetProcessHeap(), 0, reinterpret_cast<void*>(base));
	EnterCriticalSection(&g_nms_lock);
	g_nms_allocations.erase(static_cast<std::uint32_t>(raw));
	LeaveCriticalSection(&g_nms_lock);
}

// vtable[18] (0x100385A0): the game's "is this pointer mine" query.
extern "C" std::uint8_t __stdcall zpatch_nms_contains(std::uint32_t key) noexcept
{
	EnterCriticalSection(&g_nms_lock);
	const bool found = g_nms_allocations.find(key) != g_nms_allocations.end();
	LeaveCriticalSection(&g_nms_lock);
	return found ? std::uint8_t{1} : std::uint8_t{0};
}

// The CRageAllocator vtable, entry order and per-entry conventions as the
// original's 0x100B5038 (see the individual methods above).
extern "C"
{
std::uintptr_t zpatch_nms_vtable[24] =
{
		reinterpret_cast<std::uintptr_t>(&abi_nms_ctor),         // [0] 0x10038640
		reinterpret_cast<std::uintptr_t>(&abi_nms_reject_ptr),   // [1] 0x10038350
		reinterpret_cast<std::uintptr_t>(&zpatch_nms_alloc),     // [2] 0x10038360
		reinterpret_cast<std::uintptr_t>(&zpatch_nms_free),      // [3] 0x10038430
		reinterpret_cast<std::uintptr_t>(&abi_nms_self),         // [4] 0x10038540
		reinterpret_cast<std::uintptr_t>(&abi_nms_self),         // [5] 0x10038540
		reinterpret_cast<std::uintptr_t>(&abi_nms_zero_ptr),     // [6] 0x10001950
		reinterpret_cast<std::uintptr_t>(&abi_nms_zero_ptr),     // [7] 0x10001950
		reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [8] 0x100057F0
		reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [9] 0x100057F0
		reinterpret_cast<std::uintptr_t>(&abi_rgsc_login_removed),  // [10] 0x10038550
		reinterpret_cast<std::uintptr_t>(&abi_nms_noop_ret0),    // [11] 0x10038560
		reinterpret_cast<std::uintptr_t>(&abi_savegame_fix_null),   // [12] 0x10038570
		reinterpret_cast<std::uintptr_t>(&abi_nms_noop_pair),    // [13] 0x10038580
		reinterpret_cast<std::uintptr_t>(&abi_nms_noop_ret0),    // [14] 0x10038560
		reinterpret_cast<std::uintptr_t>(&abi_nms_reject),       // [15] 0x10038590
		reinterpret_cast<std::uintptr_t>(&abi_nms_reject),       // [16] 0x10038590
		reinterpret_cast<std::uintptr_t>(&abi_nms_noop_ret0),    // [17] 0x10038560
		reinterpret_cast<std::uintptr_t>(&zpatch_nms_contains),  // [18] 0x100385A0
		reinterpret_cast<std::uintptr_t>(&abi_nms_thunk18),      // [19] 0x10038620
		reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [20] 0x100057F0
		reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [21] 0x100057F0
		reinterpret_cast<std::uintptr_t>(&abi_nms_noop_arg),     // [22] 0x10038630
		reinterpret_cast<std::uintptr_t>(&abi_nms_reject),       // [23] 0x10038590
};
}  // extern "C"

#endif  // !ZP_DUMP_TOOL — assembly bodies are ASI-only; the dump uses stubs

#if defined(ZP_DUMP_TOOL)
// The dump links tests/stub_bodies.cpp in place of the assembly region above:
// the installer arms name each body, and the table needs a declaration for
// every stand-in address it hands out.
#include "../tests/stub_bodies.h"
#endif

// The original ASI's handler/body addresses, named. The resolver keys on the
// original VA, so each value is an identity label, not a patch address: the name
// is what the table is for, and the comment names the original function (and
// what the body does). `@exact` on a body says the body is a byte-exact
// transcription of that original function; tools/verify_bodies.py proves it.
enum class BodyId : std::uint32_t
{
    aim_projectile_crash_fix = 0x10005590u,  // sub_10005590: AimProjectileCrashFix (__thiscall, ret 12)
    better_mp_sync = 0x100057f0u,  // sub_100057f0: BetterMPSync (xor eax,eax / ret)
    combined_radio_freeze_fix = 0x10005ed0u,  // sub_10005ed0: CombinedRadioFreezeFix (mov eax,13h / ret)
    carparts_shadow = 0x10006880u,  // sub_10006880: CarPartsShadowFix shim (NOPs are original bytes)
    popup_headlights = 0x10044530u,  // sub_10044530: PopupHeadlightsFlag, first site
    popup_headlights_edx = 0x10044560u,  // sub_10044560: PopupHeadlightsFlag, EDX twin
    fchs_shim = 0x1001adb0u,  // sub_1001adb0: ForceCarHeadlightShadows thunk
    dual_headlights_new = 0x100064f0u,  // sub_100064f0: DualVehicleHeadlights, new-game body
    dual_headlights_old = 0x10006630u,  // sub_10006630: DualVehicleHeadlights, old-game body
    car_decals_fix = 0x1001b2e0u,  // sub_1001b2e0: CarDecalsFix (scan-based replacement)
    bad_player_model = 0x10037ba0u,  // sub_10037ba0: BadPlayerModelCrashFix trampoline
    death_music_install = 0x10050ad0u,  // sub_10050ad0: RestoreDeathMusic installer body
    death_music_enabled = 0x10050a50u,  // sub_10050a50: RestoreDeathMusic gate body
    new_saves_compat = 0x100521e0u,  // sub_100521e0: NewSavesCompatibility savegame-load tramp
    gfwl_stub = 0x100054e0u,  // sub_100054e0: Patch4RemoveModChecks / RemoveGFWLUpdatePopup stub
    nolivery_e = 0x1003d3e0u,  // sub_1003d3e0: NoLiveryLimit body E (ESI object, name = &edi[0x13c])
    carparts_shadow_2 = 0x100068a0u,  // sub_100068a0: CarPartsShadowFix other slot
    carparts_shadow_2b = 0x100068d0u,  // sub_100068d0: CarDynamicShadowFix body B (pattern-B)
    garage_body_a = 0x10046c80u,  // sub_10046c80: GarageModelFix body A (installed at game 0xC68BB0)
    garage_body_b = 0x10046ca0u,  // sub_10046ca0: GarageModelFix body B (installed at game 0x9659C3)
    steer_detour_eax = 0x100051b0u,  // sub_100051b0: MovingSteeringWheelSupport detour (EAX entry)
    steer_detour_edx = 0x100051e0u,  // sub_100051e0: MovingSteeringWheelSupport fallback detour (EDX entry)
    local_settings_detour = 0x100512f0u,  // sub_100512f0: LocalSettingsPath detour
    local_settings_fallback_detour = 0x10051300u,  // sub_10051300: LocalSettingsPath fallback detour
    spawns_probe = 0x10037510u,  // sub_10037510: FreezeCarFix/DebugPrintSpawns probe (394 insns)
    spawns_guard = 0x100372f0u,  // sub_100372f0: FreezeCarFix / DebugPrintSpawns guard
    spawns_case_0x24 = 0x10037330u,  // sub_10037330: spawns dispatch case 0x24
    spawns_case_0x3D = 0x100373f0u,  // sub_100373f0: spawns dispatch case 0x3D
    spawns_case_0x56 = 0x10037420u,  // sub_10037420: spawns dispatch case 0x56
    spawns_case_0x6F = 0x10037390u,  // sub_10037390: spawns dispatch case 0x6F
    spawns_case_0x88 = 0x10037450u,  // sub_10037450: spawns dispatch case 0x88
    spawns_case_0x9E = 0x10037480u,  // sub_10037480: spawns dispatch case 0x9E
    spawns_case_0xB4 = 0x100374e0u,  // sub_100374e0: spawns dispatch case 0xB4
    spawns_case_0xCA = 0x10037360u,  // sub_10037360: spawns dispatch case 0xCA
    spawns_case_0xE0 = 0x100373c0u,  // sub_100373c0: spawns dispatch case 0xE0
    spawns_case_0xF6 = 0x100374b0u,  // sub_100374b0: spawns dispatch case 0xF6
    game_hash = 0x10003e50u,  // sub_10003e50: AllowHashesForIDE model-name hash replacement
    ped_models_in_cutscenes_check = 0x10050870u,  // sub_10050870: PedModelsInCutscenesFix player-model check
    improved_shader_streaming_find_hpe = 0x10045a30u,  // sub_10045a30: ImprovedShaderStreaming HPE filter
    improved_shader_streaming_find_v = 0x10045a80u,  // sub_10045a80: ImprovedShaderStreaming V filter
    vram_fix_shim = 0x1004aab0u,  // sub_1004aab0: VRAMFix argument/verdict shim
    nolivery_a = 0x1003d310u,  // sub_1003d310: NoLiveryLimit body A (name = &ebx[0x13c], index = eax)
    nolivery_b = 0x1003d340u,  // sub_1003d340: NoLiveryLimit body B (name = &ebx[0x13c], index = ecx)
    nolivery_c = 0x1003d370u,  // sub_1003d370: NoLiveryLimit body C (drops 8 bytes of args)
    nolivery_d = 0x1003d3a0u,  // sub_1003d3a0: NoLiveryLimit body D (16-byte name copy, counts _sign_N)
    crash_msg_shim = 0x10036f00u,  // sub_10036f00: CrashMsgFix message-getter shim
    newfiles_register_body = 0x10051600u,  // sub_10051600: NewFilesCompatibility register body
    newfiles_update_a = 0x10051890u,  // sub_10051890: NewFilesCompatibility update A
    newfiles_update_b = 0x100518d0u,  // sub_100518d0: NewFilesCompatibility update B
    benchmark_a = 0x10005650u,  // sub_10005650: BenchmarkFix shim A (E9 at match[0])
    benchmark_b = 0x10005670u,  // sub_10005670: BenchmarkFix shim B (E9 at match[1]+8)
    building_dynamic_shadows = 0x10005af0u,  // sub_10005af0: BuildingDynamicShadows shim
    copsirens_a = 0x10006310u,  // sub_10006310: CopSirensFlag shim A
    copsirens_b = 0x10006330u,  // sub_10006330: CopSirensFlag shim B
    copsirens_ide = 0x100040e0u,  // sub_100040e0: CopSirensFlag IDE-hook body
    borderless_w = 0x10005890u,  // sub_10005890: BorderlessWindowed Unicode window-proc shim
    borderless_a = 0x10005900u,  // sub_10005900: BorderlessWindowed ANSI window-proc shim
    speedup_body = 0x10050540u,  // sub_10050540: SpeedupBugFix timing-helper replacement
    episode_switch_sum = 0x1001b080u,  // sub_1001b080: EpisodeSwitchFreezeFix sum body (__thiscall)
    load_dlcs_dummy = 0x10023480u,  // sub_10023480: LoadDLCs dummy (mov al,1 / ret 8)
    load_dlcs_struct_fixup = 0x10023490u,  // sub_10023490: LoadDLCs struct fixup (ESI object, cleans 0x14)
    skip_intro_body = 0x10045c30u,  // sub_10045c30: SkipIntro body (E9 from match-6)
    skip_menu_body = 0x10046610u,  // sub_10046610: SkipMenu body (E8 from the patched site)
    rgsc_login_removed = 0x10038550u,  // sub_10038550: RemoveRGSCLoginRequirement (mov al,1 / ret)
    savegame_fix_null = 0x10038570u,  // sub_10038570: SavegameFix null path (xor eax,eax / ret 8)
    nms_ctor = 0x10038640u,  // sub_10038640: NewMemorySystem CRageAllocator constructor
    nms_reject_ptr = 0x10038350u,  // sub_10038350: NewMemorySystem allocator vtable reject-pointer slot
    nms_self = 0x10038540u,  // sub_10038540: NewMemorySystem allocator vtable self slot
    nms_zero_ptr = 0x10001950u,  // sub_10001950: NewMemorySystem allocator vtable zero-pointer slot
    nms_noop_ret0 = 0x10038560u,  // sub_10038560: NewMemorySystem allocator vtable no-op returning 0
    nms_noop_pair = 0x10038580u,  // sub_10038580: NewMemorySystem allocator vtable paired no-op
    nms_reject = 0x10038590u,  // sub_10038590: NewMemorySystem allocator vtable reject
    nms_thunk18 = 0x10038620u,  // sub_10038620: NewMemorySystem allocator vtable forwarder (slot 18)
    nms_noop_arg = 0x10038630u,  // sub_10038630: NewMemorySystem allocator vtable no-op with argument
    nms_apply = 0x10038670u,  // sub_10038670: NewMemorySystem vtable install
    savegame_fix_close = 0x10045580u,  // sub_10045580: SavegameFix CloseHandle path (ret 4)
    savegame_fix_copy = 0x100455a0u,  // sub_100455a0: SavegameFix read/copy path
    savegame_fix_mutex = 0x100455e0u,  // sub_100455e0: SavegameFix CreateMutex path
    blackscreen_shim = 0x10036ee0u,  // sub_10036ee0: BlackscreenFix shim
    path_node_shim = 0x1003d7b0u,  // sub_1003d7b0: PathNodeCrashFix shim (sentinel 0x1FFFDF)
    plane_crash_shim = 0x1003d870u,  // sub_1003d870: PlaneCrashFix shim
    wanted_level_shim = 0x10050490u,  // sub_10050490: WantedLevelCrashFix shim (null returns 0)
    group_hack_shim = 0x10036e90u,  // sub_10036e90: GroupHackFix shim
    smpa60_shim = 0x10046750u,  // sub_10046750: SMPA60Fix shim (sentinel 0xDDDDDDDD)
    smpa60_tail_shim = 0x10046780u,  // sub_10046780: SMPA60Fix tail shim
    freeze_gun_shim = 0x10036eb0u,  // sub_10036eb0: FreezeGunFix shim
    player_ped_components_shim = 0x1003d9a0u,  // sub_1003d9a0: PlayerAsPedComponentsFix shim
    emissive_lerp_shim = 0x1001af60u,  // sub_1001af60: EmissiveLerpFix shim
    super_lod_fix_shim = 0x100469e0u,  // sub_100469e0: SuperLODFix shim (calls a helper)
    episode_switch_store_shim = 0x1001b040u,  // sub_1001b040: EpisodeSwitchFreezeFix store shim
    episode_switch_shim_b = 0x1001b060u,  // sub_1001b060: EpisodeSwitchFreezeFix shim B
    episode_switch_vcall = 0x1001b0a0u,  // sub_1001b0a0: EpisodeSwitchFreezeFix virtual call shim
    episode_switch_shim_c = 0x1001b0c0u,  // sub_1001b0c0: EpisodeSwitchFreezeFix shim C
    bike_physics_detour = 0x1001ef60u,  // sub_1001ef60: HighFPSBikePhysicsFix detour
    collision_deadlock_walk = 0x10005d80u,  // sub_10005d80: CollisionDeadlockFix replacement
    mouse_axis_float = 0x10024060u,  // sub_10024060: MouseFix axis-float helper
    mouse_axis_neg = 0x100240c0u,  // sub_100240c0: MouseFix negated-axis helper
    mouse_axis_int = 0x10024120u,  // sub_10024120: MouseFix int-axis helper
    mouse_axis_flag = 0x10024170u,  // sub_10024170: MouseFix flagged-axis helper
    mf_angle_step = 0x10024440u,  // sub_10024440: MouseFix angle-step helper
    mouse_angle = 0x10024560u,  // sub_10024560: MouseFix body A (installed at game 0xA81B10)
    mouse_advance = 0x100243a0u,  // sub_100243a0: MouseFix body B (installed at game 0xA82265)
    ooc_branch = 0x1004c3c0u,  // sub_1004c3c0: OutOfCommissionFix branch body
    ooc_wrap = 0x1004c350u,  // sub_1004c350: OutOfCommissionFix wrapper
    hqr_body = 0x1001f030u,  // sub_1001f030: HighQualityReflections body
    cutscene_chain_b = 0x100388c0u,  // sub_100388c0: CutsceneFixes chain B
    driveby_fov_shim = 0x100064b0u,  // sub_100064b0: DrivebyFOVFix shim
};

constexpr std::uint32_t OriginalHandlerVa(BodyId id) noexcept
{
	return static_cast<std::uint32_t>(id);
}

struct AbiBody final
{
	BodyId id;
	void (*address)();
};

// Only destinations whose conventions are verified appear here; everything else
// resolves to 0 and the installer reports it as skipped.
const AbiBody kAbiBodies[] =
{
		{BodyId::aim_projectile_crash_fix, reinterpret_cast<void(*)()>(&abi_aim_projectile_crash_fix)},
		{BodyId::better_mp_sync, reinterpret_cast<void(*)()>(&abi_better_mp_sync)},
		{BodyId::combined_radio_freeze_fix, reinterpret_cast<void(*)()>(&abi_combined_radio_freeze_fix)},
		{BodyId::carparts_shadow, reinterpret_cast<void(*)()>(&abi_carparts_shadow)},
		{BodyId::popup_headlights, reinterpret_cast<void(*)()>(&abi_popup_headlights)},
		{BodyId::popup_headlights_edx, reinterpret_cast<void(*)()>(&abi_popup_headlights_edx)},
		{BodyId::fchs_shim, reinterpret_cast<void(*)()>(&abi_fchs_shim)},
		{BodyId::dual_headlights_new, reinterpret_cast<void(*)()>(&abi_dual_headlights_new)},
		{BodyId::dual_headlights_old, reinterpret_cast<void(*)()>(&abi_dual_headlights_old)},
		{BodyId::car_decals_fix, reinterpret_cast<void(*)()>(&abi_car_decals_fix)},
		{BodyId::bad_player_model, reinterpret_cast<void(*)()>(&abi_bad_player_model)},
		{BodyId::death_music_install, reinterpret_cast<void(*)()>(&abi_death_music_install)},
		{BodyId::death_music_enabled, reinterpret_cast<void(*)()>(&abi_death_music_enabled)},
		{BodyId::new_saves_compat, reinterpret_cast<void(*)()>(&abi_new_saves_compat)},
		{BodyId::gfwl_stub, reinterpret_cast<void(*)()>(&abi_gfwl_stub)},
		{BodyId::nolivery_e, reinterpret_cast<void(*)()>(&abi_nolivery_e)},
		{BodyId::carparts_shadow_2, reinterpret_cast<void(*)()>(&abi_carparts_shadow_2)},
		{BodyId::carparts_shadow_2b, reinterpret_cast<void(*)()>(&abi_carparts_shadow_2b)},
		{BodyId::garage_body_a, reinterpret_cast<void(*)()>(&abi_garage_body_a)},
		{BodyId::garage_body_b, reinterpret_cast<void(*)()>(&abi_garage_body_b)},
		{BodyId::steer_detour_eax, reinterpret_cast<void(*)()>(&abi_steer_detour_eax)},
		{BodyId::steer_detour_edx, reinterpret_cast<void(*)()>(&abi_steer_detour_edx)},
		{BodyId::local_settings_detour, reinterpret_cast<void(*)()>(&abi_local_settings_detour)},
		{BodyId::local_settings_fallback_detour, reinterpret_cast<void(*)()>(&abi_local_settings_fallback_detour)},
		{BodyId::spawns_probe, reinterpret_cast<void(*)()>(&abi_spawns_probe)},
		{BodyId::spawns_guard, reinterpret_cast<void(*)()>(&abi_spawns_guard)},
		{BodyId::spawns_case_0x24, reinterpret_cast<void(*)()>(&abi_spawns_case_0x24)},
		{BodyId::spawns_case_0x3D, reinterpret_cast<void(*)()>(&abi_spawns_case_0x3D)},
		{BodyId::spawns_case_0x56, reinterpret_cast<void(*)()>(&abi_spawns_case_0x56)},
		{BodyId::spawns_case_0x6F, reinterpret_cast<void(*)()>(&abi_spawns_case_0x6F)},
		{BodyId::spawns_case_0x88, reinterpret_cast<void(*)()>(&abi_spawns_case_0x88)},
		{BodyId::spawns_case_0x9E, reinterpret_cast<void(*)()>(&abi_spawns_case_0x9E)},
		{BodyId::spawns_case_0xB4, reinterpret_cast<void(*)()>(&abi_spawns_case_0xB4)},
		{BodyId::spawns_case_0xCA, reinterpret_cast<void(*)()>(&abi_spawns_case_0xCA)},
		{BodyId::spawns_case_0xE0, reinterpret_cast<void(*)()>(&abi_spawns_case_0xE0)},
		{BodyId::spawns_case_0xF6, reinterpret_cast<void(*)()>(&abi_spawns_case_0xF6)},
		{BodyId::game_hash, reinterpret_cast<void(*)()>(&abi_game_hash)},
		{BodyId::ped_models_in_cutscenes_check, reinterpret_cast<void(*)()>(&abi_ped_models_in_cutscenes_check)},
		{BodyId::improved_shader_streaming_find_hpe,
		 reinterpret_cast<void(*)()>(&abi_improved_shader_streaming_find_hpe)},
		{BodyId::improved_shader_streaming_find_v,
		 reinterpret_cast<void(*)()>(&abi_improved_shader_streaming_find_v)},
		{BodyId::vram_fix_shim, reinterpret_cast<void(*)()>(&abi_vram_fix_shim)},
		{BodyId::nolivery_a, reinterpret_cast<void(*)()>(&abi_nolivery_a)},
		{BodyId::nolivery_b, reinterpret_cast<void(*)()>(&abi_nolivery_b)},
		{BodyId::nolivery_c, reinterpret_cast<void(*)()>(&abi_nolivery_c)},
		{BodyId::nolivery_d, reinterpret_cast<void(*)()>(&abi_nolivery_d)},
		{BodyId::crash_msg_shim, reinterpret_cast<void(*)()>(&abi_crash_msg_shim)},
		{BodyId::newfiles_register_body, reinterpret_cast<void(*)()>(&abi_newfiles_register_body)},
		{BodyId::newfiles_update_a, reinterpret_cast<void(*)()>(&abi_newfiles_update_a)},
		{BodyId::newfiles_update_b, reinterpret_cast<void(*)()>(&abi_newfiles_update_b)},
		{BodyId::benchmark_a, reinterpret_cast<void(*)()>(&abi_benchmark_a)},
		{BodyId::benchmark_b, reinterpret_cast<void(*)()>(&abi_benchmark_b)},
		{BodyId::building_dynamic_shadows, reinterpret_cast<void(*)()>(&abi_building_dynamic_shadows)},
		{BodyId::copsirens_a, reinterpret_cast<void(*)()>(&abi_copsirens_a)},
		{BodyId::copsirens_b, reinterpret_cast<void(*)()>(&abi_copsirens_b)},
		{BodyId::copsirens_ide, reinterpret_cast<void(*)()>(&abi_copsirens_ide)},
		{BodyId::borderless_w, reinterpret_cast<void(*)()>(&abi_borderless_w)},
		{BodyId::borderless_a, reinterpret_cast<void(*)()>(&abi_borderless_a)},
		{BodyId::speedup_body, reinterpret_cast<void(*)()>(&abi_speedup_body)},
		{BodyId::episode_switch_sum, reinterpret_cast<void(*)()>(&abi_episode_switch_sum)},
		{BodyId::load_dlcs_dummy, reinterpret_cast<void(*)()>(&abi_load_dlcs_dummy)},
		{BodyId::load_dlcs_struct_fixup, reinterpret_cast<void(*)()>(&abi_load_dlcs_struct_fixup)},
		{BodyId::skip_intro_body, reinterpret_cast<void(*)()>(&abi_skip_intro_body)},
		{BodyId::skip_menu_body, reinterpret_cast<void(*)()>(&abi_skip_menu_body)},
		{BodyId::rgsc_login_removed, reinterpret_cast<void(*)()>(&abi_rgsc_login_removed)},
		{BodyId::savegame_fix_null, reinterpret_cast<void(*)()>(&abi_savegame_fix_null)},
		{BodyId::nms_ctor, reinterpret_cast<void(*)()>(&abi_nms_ctor)},
		{BodyId::nms_reject_ptr, reinterpret_cast<void(*)()>(&abi_nms_reject_ptr)},
		{BodyId::nms_self, reinterpret_cast<void(*)()>(&abi_nms_self)},
		{BodyId::nms_zero_ptr, reinterpret_cast<void(*)()>(&abi_nms_zero_ptr)},
		{BodyId::nms_noop_ret0, reinterpret_cast<void(*)()>(&abi_nms_noop_ret0)},
		{BodyId::nms_noop_pair, reinterpret_cast<void(*)()>(&abi_nms_noop_pair)},
		{BodyId::nms_reject, reinterpret_cast<void(*)()>(&abi_nms_reject)},
		{BodyId::nms_thunk18, reinterpret_cast<void(*)()>(&abi_nms_thunk18)},
		{BodyId::nms_noop_arg, reinterpret_cast<void(*)()>(&abi_nms_noop_arg)},
		{BodyId::nms_apply, reinterpret_cast<void(*)()>(&abi_nms_apply)},
		{BodyId::savegame_fix_close, reinterpret_cast<void(*)()>(&abi_savegame_fix_close)},
		{BodyId::savegame_fix_copy, reinterpret_cast<void(*)()>(&abi_savegame_fix_copy)},
		{BodyId::savegame_fix_mutex, reinterpret_cast<void(*)()>(&abi_savegame_fix_mutex)},
		{BodyId::blackscreen_shim, reinterpret_cast<void(*)()>(&abi_blackscreen_shim)},
		{BodyId::path_node_shim, reinterpret_cast<void(*)()>(&abi_path_node_shim)},
		{BodyId::plane_crash_shim, reinterpret_cast<void(*)()>(&abi_plane_crash_shim)},
		{BodyId::wanted_level_shim, reinterpret_cast<void(*)()>(&abi_wanted_level_shim)},
		{BodyId::group_hack_shim, reinterpret_cast<void(*)()>(&abi_group_hack_shim)},
		{BodyId::smpa60_shim, reinterpret_cast<void(*)()>(&abi_smpa60_shim)},
		{BodyId::smpa60_tail_shim, reinterpret_cast<void(*)()>(&abi_smpa60_tail_shim)},
		{BodyId::freeze_gun_shim, reinterpret_cast<void(*)()>(&abi_freeze_gun_shim)},
		{BodyId::player_ped_components_shim, reinterpret_cast<void(*)()>(&abi_player_ped_components_shim)},
		{BodyId::emissive_lerp_shim, reinterpret_cast<void(*)()>(&abi_emissive_lerp_shim)},
		{BodyId::super_lod_fix_shim, reinterpret_cast<void(*)()>(&abi_super_lod_fix_shim)},
		{BodyId::episode_switch_store_shim, reinterpret_cast<void(*)()>(&abi_episode_switch_store_shim)},
		{BodyId::episode_switch_shim_b, reinterpret_cast<void(*)()>(&abi_episode_switch_shim_b)},
		{BodyId::episode_switch_vcall, reinterpret_cast<void(*)()>(&abi_episode_switch_vcall)},
		{BodyId::episode_switch_shim_c, reinterpret_cast<void(*)()>(&abi_episode_switch_shim_c)},
		{BodyId::bike_physics_detour, reinterpret_cast<void(*)()>(&abi_bike_physics_detour)},
		{BodyId::collision_deadlock_walk, reinterpret_cast<void(*)()>(&abi_collision_deadlock_walk)},
		{BodyId::mouse_axis_float, reinterpret_cast<void(*)()>(&abi_mouse_axis_float)},
		{BodyId::mouse_axis_neg, reinterpret_cast<void(*)()>(&abi_mouse_axis_neg)},
		{BodyId::mouse_axis_int, reinterpret_cast<void(*)()>(&abi_mouse_axis_int)},
		{BodyId::mouse_axis_flag, reinterpret_cast<void(*)()>(&abi_mouse_axis_flag)},
		{BodyId::mf_angle_step, reinterpret_cast<void(*)()>(&abi_mf_angle_step)},
		{BodyId::mouse_angle, reinterpret_cast<void(*)()>(&abi_mouse_angle)},
		{BodyId::mouse_advance, reinterpret_cast<void(*)()>(&abi_mouse_advance)},
		{BodyId::ooc_branch, reinterpret_cast<void(*)()>(&abi_ooc_branch)},
		{BodyId::ooc_wrap, reinterpret_cast<void(*)()>(&abi_ooc_wrap)},
		{BodyId::hqr_body, reinterpret_cast<void(*)()>(&abi_hqr_body)},
		{BodyId::cutscene_chain_b, reinterpret_cast<void(*)()>(&abi_cutscene_chain_b)},
		{BodyId::driveby_fov_shim, reinterpret_cast<void(*)()>(&abi_driveby_fov_shim)},
};

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


std::uintptr_t ResolveHookBody(std::uint32_t reference_va, void*) noexcept
{
	for (const AbiBody& body : kAbiBodies)
	{
		if (OriginalHandlerVa(body.id) == reference_va)
			return reinterpret_cast<std::uintptr_t>(body.address);
	}
	return 0;
}

std::size_t HookBodyCount() noexcept
{
	return sizeof(kAbiBodies) / sizeof(kAbiBodies[0]);
}

#if defined(ZP_DUMP_TOOL)
// Write-set dump support. A branch the installer writes to a hook body encodes
// the body's address *in the tool*, which is not part of the installer logic:
// the dump canonicalises those displacements to the body's reference VA so its
// output does not depend on where the tool happens to load. (The stub bodies
// still exist because the arms name them directly.)
std::size_t DumpHookBodyCount() noexcept
{
	return HookBodyCount();
}

std::uint32_t DumpHookBodyReference(std::size_t index) noexcept
{
	return OriginalHandlerVa(kAbiBodies[index].id);
}

std::uintptr_t DumpHookBodyAddress(std::size_t index) noexcept
{
	return reinterpret_cast<std::uintptr_t>(kAbiBodies[index].address);
}
#endif  // ZP_DUMP_TOOL

#endif  // _WIN32

// ---------------------------------------------------------------------------
// Option pass: one block per INI option, in the original loader's order.
// ---------------------------------------------------------------------------

namespace
{
	constexpr std::size_t kWordLength = 2;
	constexpr std::size_t kBranchLength = 5; // E8/E9 <rel32>
	constexpr std::uint8_t kCallOpcode = 0xE8;
	// The other instruction bytes the arms write. An opcode is not data: name it
	// so `0xEB` at a site reads as "jmp rel8" and not as a magic number.
	constexpr std::uint8_t kRetOpcode = 0xC3;
	constexpr std::uint8_t kMovEaxOpcode = 0xB8;   // mov eax, imm32
	constexpr std::uint8_t kPushadOpcode = 0x60;
	constexpr std::uint8_t kXorEaxOpcode = 0x33;      // xor
	constexpr std::uint8_t kXorEaxEaxModrm = 0xC0;    // eax, eax
	constexpr std::uint8_t kJeRel32First = 0x0F;      // jcc rel32, two-byte opcode
	constexpr std::uint8_t kJeRel32Second = 0x84;     // je
	// 66 0F 6E C0 / 66 0F 6E C8 = movd xmm0, eax / movd xmm1, eax.
	constexpr std::uint8_t kMovdXmm0Eax[] = {0x66, 0x0F, 0x6E, 0xC0};
	constexpr std::uint8_t kMovdXmm1Eax[] = {0x66, 0x0F, 0x6E, 0xC8};
	constexpr std::uint32_t kFusionFixProbeRva = 0x20D7u + 4u;

	bool bMenuIntegration = false;

	bool BuildIs(GameBuild build, std::initializer_list<GameBuild> builds)
	{
		for (const GameBuild candidate : builds)
		{
			if (candidate == build)
				return true;
		}
		return false;
	}

	// The original's hook::pattern, over the image the parity drivers hand in.
	hook::pattern Pattern(const GameImage& image, std::string_view signature)
	{
		const std::string normalized = NormalizeSignature(signature);
		if (normalized.empty())
			return hook::pattern{};
		const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(image.base);
		return hook::make_range_pattern(begin, begin + image.size, normalized);
	}

	// The address of a site inside the current match.
	std::uint32_t MatchAddress(hook::pattern& pattern, std::ptrdiff_t offset = 0)
	{
		return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
		    pattern.get(0).get<std::uint8_t>(offset)));
	}

	// A rel32 target inside the current match: the displacement at
	// `displacementOffset`, measured from the instruction at `nextOffset`.
	std::uint32_t MatchTarget(hook::pattern& pattern, std::ptrdiff_t displacementOffset,
	                          std::ptrdiff_t nextOffset)
	{
		return MatchAddress(pattern, nextOffset) +
		       *pattern.get(0).get<std::uint32_t>(displacementOffset);
	}

	// The dword stored at `address`.
	std::uint32_t ReadDword(const std::uint8_t* address)
	{
		std::uint32_t value = 0;
		std::memcpy(&value, address, sizeof(value));
		return value;
	}

	// A dword field inside a match: a call displacement, or a site pointer.
	std::uint32_t Field(const std::uint8_t* match, std::ptrdiff_t offset)
	{
		return ReadDword(match + offset);
	}

	// A pointer stored in a dword field of the current match.
	std::uint8_t* MatchPointer(hook::pattern& pattern, std::ptrdiff_t offset)
	{
		return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(
		    *pattern.get(0).get<std::uint32_t>(offset)));
	}

	// A pointer as the dword the game and the injector expect (the ASI is x86).
	std::uint32_t AddressOf(const void* pointer)
	{
		return static_cast<std::uint32_t>(
		    PublishedAddress(reinterpret_cast<std::uintptr_t>(pointer)));
	}

	bool InImage(const GameImage& image, std::uintptr_t address)
	{
		const auto base = reinterpret_cast<std::uintptr_t>(image.base);
		return address >= base && address + kDwordLength <= base + image.size;
	}

	// Where the branch at `siteOffset` goes: the original re-points these.
	std::uintptr_t BranchTarget(const std::uint8_t* match, std::ptrdiff_t siteOffset)
	{
		const auto displacement = static_cast<std::int32_t>(Field(match, siteOffset + 2));
		return reinterpret_cast<std::uintptr_t>(match) + siteOffset + 6 +
		       static_cast<std::uintptr_t>(displacement);
	}

	// A five-byte branch written the way the original's collector writes it
	// (opcode + rel32). injector::MakeCALL/MakeJMP first call
	// GetBranchDestination, which for a `FF 15 <disp32>` site dereferences the
	// operand; that read is discarded, and it is the only difference from the
	// original. Use this where the dereference would read memory the site does
	// not point into (e.g. a byte of the operand is overwritten by the same arm).
	// Only WriteMemoryRaw is guaranteed by every injector shim (the real one, the
	// dump shim and the trace shim), so the rel32 is built here.
	void WriteBranchNoRead(std::uint8_t* site, std::uint8_t opcode, void* destination)
	{
		std::uint8_t bytes[kBranchLength];
		bytes[0] = opcode;
		const auto displacement = static_cast<std::int32_t>(
		    reinterpret_cast<std::intptr_t>(destination) -
		    reinterpret_cast<std::intptr_t>(site + kBranchLength));
		std::memcpy(bytes + 1, &displacement, sizeof(displacement));
		injector::WriteMemoryRaw(site, bytes, sizeof(bytes), true); //write
	}

	void WriteCallNoRead(std::uint8_t* site, void* destination)
	{
		WriteBranchNoRead(site, kCallOpcode, destination);
	}

	// The FusionFix compatibility probe: a build of the fix we cannot share a
	// module with keeps its own bytes at 0x20D3/0x20D7.
	bool FusionFixGateClosed()
	{
		const HMODULE module = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi");
		if (module == nullptr)
			return false;
		const auto* image = reinterpret_cast<const std::uint8_t*>(module);
		return ModuleLacksSignature(image, kFusionFixProbeRva);
	}

	bool NewFilesConflictModulesPresent()
	{
		return GetModuleHandleA("GTAC.dll") != nullptr ||
		       GetModuleHandleA("JacksIVFixes.asi") != nullptr;
	}

} // namespace

// One pass over every arm, in the original loader's order.
#if defined(ZP_TRACE_WRITES)
namespace injector
{
const char* g_traceArm = "?";
}
#endif

#if defined(ZP_TRACE_INSTALLS)
// Debug builds only: name the arm that is about to run, so a boot fault can
// be narrowed down from the log.
void Trace(const char* name)
{
	static std::FILE* log = std::fopen("ZolikaPatch.trace.log", "a");
	if (log == nullptr)
		return;
	std::fprintf(log, "arm %s\n", name);
	std::fprintf(log, "heap %s arm:%s\n",
	             HeapValidate(GetProcessHeap(), 0, nullptr) ? "ok" : "BAD", name);
#if defined(ZP_TRACE_WRITES)
	injector::g_traceArm = name;
#endif
	std::fflush(log); //patch
}
#else
void Trace(const char*) {}
#endif

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
