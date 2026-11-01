#pragma once
// Declarations shared by the loader, the installers and the hook bodies.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "INI.h"

// Result code of a patch pass; PatchResult::detail is always a fixed literal,
// never an allocated message (the original reports through the log).
enum class PatchResultCode
{
	ok,
	out_of_bounds,
	invalid_input,
	io_error,
	parse_error,
	not_found,
	unsupported_platform,
	mismatch,
};

struct PatchResult final
{
	PatchResultCode code{PatchResultCode::ok};
	std::string_view detail{};
	constexpr explicit operator bool() const noexcept { return code == PatchResultCode::ok; }
};

// A bounded memory view to scan. FindPattern reads tokens the original way -
// strtoul(token, nullptr, 16), low byte compared, "?" a wildcard - while the
// scanner itself is the vendored CitizenFX one.
struct ModuleView
{
	const std::uint8_t* base{};
	std::size_t size{};
};

struct Match
{
	const std::uint8_t* address{};
};

bool FindPattern(ModuleView module, std::string_view signature, Match& output) noexcept;

// All matches, in ascending address order.
std::size_t FindAllPatterns(ModuleView module, std::string_view signature,
                            std::vector<const std::uint8_t*>& output) noexcept;

// The same signature in the strict "AA ?? BB" form hook::pattern takes; empty
// when the signature has no tokens.
std::string NormalizeSignature(std::string_view signature);

// 0x10003E50. "hash:NNNN" returns the parsed value (a 0 result falls back to
// hashing); anything else is hashed.
std::uint32_t GameHash(std::string_view text) noexcept;

// The loader's classification enum. The number is the build id every build mask
// indexes, so it must not change.
enum class GameBuild : std::uint8_t
{
	unknown = 0,
	v1000 = 1,
	v1040 = 5,
	v1004 = 6,
	v1060 = 7,
	v1070 = 8,
	v1080 = 9,
	v1100 = 10,
	v1110 = 11,
	v1120 = 12,
	v1130 = 13,
	v1061 = 15,
	v12043 = 18,
};

// 0x10003B80: concatenate the four decimal fields and parse the result as base
// 10 (1.0.7.0 -> 1070).
bool ComposeGameVersion(std::uint16_t major, std::uint16_t minor, std::uint16_t build,
                        std::uint16_t revision, std::uint32_t& output) noexcept;

// The switch at 0x10055C70. Anything else is unknown and installs nothing.
GameBuild ClassifyGameBuild(std::uint32_t compactVersion) noexcept;

// Reads the running executable's VS_FIXEDFILEINFO.
bool QueryGameVersion(std::uint32_t& compactVersion, GameBuild& build) noexcept;

const char* GameBuildName(GameBuild build) noexcept;

struct ProcessEntry
{
	std::string_view executable;
	std::uint32_t processId{};
};
std::uint32_t FindGameProcess() noexcept;
std::uint32_t SelectGameProcess(const ProcessEntry* entries, std::size_t count,
                                std::string_view name) noexcept;

// 0x10003E20: true when the given module lacks the FusionFix bytes at
// 0x20D3/0x20D7 (the original probes GTAIV.EFLC.FusionFix.asi).
bool ModuleLacksSignature(const std::uint8_t* image, std::size_t imageSize) noexcept;

// The game image as it looks when mapped. Writes go through the caller's image;
// this type performs no OS calls of its own.
struct GameImage final
{
	std::uint8_t* base{};
	std::size_t size{};
};

// Applies one write: signature ops scan and write at match + offset (the offset
// may be negative), RVA ops write at base + rva. Expected bytes are not
// verified, exactly like the original handlers.
PatchResult ApplyMappedOperation(GameImage image, std::string_view signature, std::int32_t offset,
                                 std::uint32_t moduleRva, std::string_view replacement,
                                 std::uint8_t*& writtenAt, std::size_t& written) noexcept;

// The multiplayer model-name catalogue (the shipped mod's 38,455-entry table)
// and its reverse hash lookup, used by the spawn and print hooks.
std::size_t ModelCatalogSize() noexcept;
std::string_view ModelCatalogName(std::size_t index) noexcept;

// 0x10003F00: first match in original table order; "**UNKNOWN**" on a miss.
std::string_view ModelNameFromHash(std::uint32_t hash) noexcept;

struct VehicleLightValue
{
	std::uint32_t modelHash{};
	float value{};
	bool populated{};
};

struct VehicleLightTables
{
	std::array<VehicleLightValue, 512> popupAngles{};      // radians
	std::array<VehicleLightValue, 512> headlightOffsets{};
};

// FUN_100523c0's two 512-slot INI loops. Missing model names leave holes; the
// value conversion is the original's std::stof, so a missing or unparseable
// value throws (invalid_argument / out_of_range) and is not skipped.
PatchResult LoadVehicleLightTables(const ConfigDocument&, VehicleLightTables&);

// FUN_10044310 / FUN_1001ad20 look a model up through the game's callback. The
// adapter keeps those lookups free of any pointer into GTA IV.
using ModelIndexResolver = int (*)(std::uint32_t, void*);

struct PopupLightSelection
{
	float angleRadians{1.5707963705062866f};
	bool supported{};
};

PopupLightSelection SelectPopupLight(const VehicleLightTables&,
                                     const std::array<std::uint32_t, 512>& supportedModels,
                                     int modelIndex, ModelIndexResolver, void* context = nullptr) noexcept;
float SelectHeadlightOffset(const VehicleLightTables&, int modelIndex, ModelIndexResolver,
                            void* context = nullptr) noexcept;

// IVMenuAPI integration: when AddOptionsInMenu is on, the shipped mod probes
// for the module (loading it once and caching the attempt) and registers the
// two toggles through AddIVMenuOption (see the MenuRegistration rows).
enum class MenuProbeResult
{
	disabled,
	already_loaded,
	loaded,
	unavailable,
};

MenuProbeResult ProbeMenuApi(bool optionEnabled, bool modulePresent, bool attemptAlreadyMade,
                             bool loadSucceeded) noexcept;

struct MenuIntegrationState final
{
	bool optionEnabled{true};  // AddOptionsInMenu defaults to 1
	bool modulePresent{};
	bool attemptMade{};
	std::size_t loadAttempts{};
};

MenuProbeResult MenuIntegrationProbe(MenuIntegrationState& state, bool loadSucceeded) noexcept;

// One row of the loader's AddIVMenuOption registration.
struct MenuRegistration final
{
	const char* family;
	const wchar_t* title;
	const char* display;
	const char* pref;
	int a;
	int b;
	std::uintptr_t getter;
	std::uintptr_t setter;
};

const MenuRegistration* MenuRegistrations(std::size_t& count) noexcept;

// Number of successful AddIVMenuOption calls (0 when the module or its export is
// absent).
std::size_t RegisterMenuOptions() noexcept;

// The ZMenuIV compatibility guard (sub_10052300, called unconditionally at
// 0x1005527B after the INI object is built). It asks GetZMenuVersion for the
// menu's version and, below the minimum, shows a box and exits the process.
constexpr std::uint32_t kZMenuMinVersion = 0x35DD1u;
constexpr const char* kZMenuModuleName = "ZMenuIV.asi";
constexpr const char* kZMenuProcName = "GetZMenuVersion";
constexpr const char* kZMenuRejectText =
	"Incompatible/outdated version of ZMenu detected! The game will now exit.";
constexpr const char* kZMenuRejectCaption = "";  // 0x10063CD6, an empty string
constexpr std::uint32_t kZMenuRejectFlags = 0x10u;  // MB_ICONHAND
constexpr std::uint32_t kZMenuTerminateExitCode = 0u;

enum class ZMenuGuardResult : std::uint8_t
{
	module_absent,     // ZMenuIV.asi is not loaded
	export_absent,     // no GetZMenuVersion export
	version_ok,        // the export reported >= kZMenuMinVersion
	version_rejected   // below the minimum: the original exits
};

ZMenuGuardResult ZMenuVersionGuard(std::uint32_t& version) noexcept;

constexpr bool ZMenuVersionAccepted(std::uint32_t version) noexcept
{
	return version >= kZMenuMinVersion;
}
