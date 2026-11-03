// zp.xliveless implementation: the recovered tables, the launcher/DLC installers
// and the reimplemented hook bodies (the Win32 half is the shipped mod's
// CreateFile/ROM-window path).

module;

#include "common.hxx"
#include "common.h"
#include "patches.h"
#include "xlive/xliveless.h"

#if defined(_WIN32)
#include <atomic>
#endif

module zp.xliveless;

import zp.common;
import zp.patch;

namespace
{
constexpr std::uint8_t kJumpOpcode = 0xE9;  // E9 <rel32> (the DFA/ROM install rows)

// ---- the byte runs the launcher rows write --------------------------------
// Named by the instruction they form; the recovered table repeats the same
// wipes, so the rows share these runs and a write row's length is its run's
// own size.
constexpr std::uint8_t kJmpRel8[] = {0xEB};  // jmp rel8
constexpr std::uint8_t kNop2[] = {0x90, 0x90};
constexpr std::uint8_t kNop3[] = {0x90, 0x90, 0x90};
constexpr std::uint8_t kNop4[] = {0x90, 0x90, 0x90, 0x90};
constexpr std::uint8_t kNop6[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
constexpr std::uint8_t kNop8[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
constexpr std::uint8_t kNop11[] = {
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
};
constexpr std::uint8_t kNop14[] = {
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
};
constexpr std::uint8_t kXorEax[] = {0x33, 0xC0};            // xor eax,eax
constexpr std::uint8_t kNopMovMoffsEax[] = {0x90, 0xA3};    // nop; mov [imm32],eax
constexpr std::uint8_t kJmpRel32[] = {kJumpOpcode};         // jmp rel32 (the rel32 follows)
constexpr std::uint8_t kRet[] = {0xC3};                     // ret
constexpr std::uint8_t kXorEaxRetNop[] = {0x33, 0xC0, 0xC3, 0x90};  // xor; ret; nop

// ---- the main table's inline patches (0x10045CE0) -------------------------
//
// In-place instruction-window patches, not hook bodies: the recovered handler
// overwrites the launcher check at the signature match (NOPs, an xor, a
// one-byte branch or a short jump), so nothing here is callable. The rows,
// their order and the bytes are 1:1 with the handler; a byte row's length is
// its run's size and a fill row states its count, so no length is duplicated.
// The offsets are match-relative and land on the instruction named in the row.

// 0: `test bl,bl; jne +0x143; lea ecx,[esp+28]; mov eax,ecx`; both rows force
// the single EB of a short branch in the same function (the condition byte is
// outside the matched window).
constexpr SkipLauncherWrite kEntry0Writes[] = {
    WriteBytes(0x22, kJmpRel8),  // +34: jz/jnz -> jmp
    WriteBytes(0xDF, kJmpRel8),  // +223
};
// 2: `74 EC` (jz), `cmp [esi+6],bl`, `74 E7` (jz), `push imm32`.
constexpr SkipLauncherWrite kEntry2Writes[] = {
    WriteBytes(0, kNop2),
    WriteBytes(5, kNop2),
};
// 3: `74 C4` (jz), `mov eax,[edi]`, `cmp eax,[esi+18]`, `75 BD`.
constexpr SkipLauncherWrite kEntry3Writes[] = {
    WriteBytes(0, kNop2),
};
// 4: `cmp edx,[esi+18]; jne rel32` - the 6 bytes are the whole jcc.
constexpr SkipLauncherWrite kEntry4Writes[] = {
    WriteBytes(3, kNop6),
};
// 5: `cmp ecx,[esi+18]; jne rel32`.
constexpr SkipLauncherWrite kEntry5Writes[] = {
    WriteBytes(3, kNop6),
};
// 6: `jne rel32; mov ecx,[esp+..]; pop edi/esi/ebx; xor ecx,esp`.
constexpr SkipLauncherWrite kEntry6Writes[] = {
    WriteBytes(0, kNop6),
};
// 7: `cmp eax,[esi+18]; jne -0x43`; +3 replaces the `75 BD` with `33 C0`
// (xor eax,eax); +10 is 5 bytes past the window: `90 A3`.
constexpr SkipLauncherWrite kEntry7Writes[] = {
    WriteBytes(3, kXorEax),
    WriteBytes(10, kNopMovMoffsEax),
};
// 8: `push edi; push ecx` then three `C7 44 24` immediate stores; the
// recovered handler memsets 0x1BF (447) bytes starting at +1.
constexpr SkipLauncherWrite kEntry8Writes[] = {
    NopFill(1, 447),
};
// 9: `mov edx,[esi+1C]; cmp edx,[esi+20]` becomes `33 C0 E9 rel32`: xor
// eax,eax, then a jump to entry 1's match (the launcher entry the original
// keeps for this row).
constexpr SkipLauncherWrite kEntry9Writes[] = {
    WriteBytes(0, kXorEax),
    WriteBytes(2, kJmpRel32),
    Jump(3, kSkipLauncherJumpTargetEntry),
};
// 10: `mov [imm32],esi; call; add esp,4; test al,al`; `90 A3` keeps the store
// but takes EAX instead of ESI (the `89` becomes `90`).
constexpr SkipLauncherWrite kEntry10Writes[] = {
    WriteBytes(0, kNopMovMoffsEax),
};
// 11: `mov edx,[eax+2C]; cmp edx,[eax+30]` -> `33 C0` + NOP4, so the caller
// receives eax = 0.
constexpr SkipLauncherWrite kEntry11Writes[] = {
    WriteBytes(0, kXorEax),
    WriteBytes(2, kNop4),
};
// 12: `mov ecx,[eax+2C]; sub esp,0x78`; NOP3 removes the load and the 11 bytes
// after the 6-byte window are wiped.
constexpr SkipLauncherWrite kEntry12Writes[] = {
    WriteBytes(0, kNop3),
    NopFill(6, 11),
};
// 13: the same with `sub esp,0x74`.
constexpr SkipLauncherWrite kEntry13Writes[] = {
    WriteBytes(0, kNop3),
    NopFill(6, 11),
};
// 14: `mov ecx,[eax+2C]; cmp ecx,[eax+30]; jz +3`.
constexpr SkipLauncherWrite kEntry14Writes[] = {
    WriteBytes(0, kNop11),
};
// 15: the same compare followed by `push ebx`.
constexpr SkipLauncherWrite kEntry15Writes[] = {
    NopFill(0, 32),
};
// 16: the same compare followed by `jz +0x11`.
constexpr SkipLauncherWrite kEntry16Writes[] = {
    NopFill(0, 25),
};
// 17/18: `mov ecx,[eax+2C]; cmp ecx,[eax+30]; jne -0x24; lea edx,[esp+18/1C]`;
// the 8-byte compare-and-branch window is wiped.
constexpr SkipLauncherWrite kEntry17Writes[] = {
    WriteBytes(0, kNop8),
};
constexpr SkipLauncherWrite kEntry18Writes[] = {
    WriteBytes(0, kNop8),
};
// 19: `mov ecx,[imm32]; mov edx,[ecx+2C]`.
constexpr SkipLauncherWrite kEntry19Writes[] = {
    WriteBytes(0, kNop14),
};
// 20: prologue pushes, `mov edi,[imm32]; mov eax,[edi+2C]`; the whole function
// becomes `ret`.
constexpr SkipLauncherWrite kEntry20Writes[] = {
    WriteBytes(0, kRet),
};
// 21: `cmp eax,edi; jne rel32; test eax,eax`.
constexpr SkipLauncherWrite kEntry21Writes[] = {
    NopFill(0, 16),
};
// 22: `jne rel32; test eax,eax; je rel32; call rel32; test edx,edx`.
constexpr SkipLauncherWrite kEntry22Writes[] = {
    WriteBytes(0, kNop14),
};
// 23: `sub esp,0x24; mov eax,[imm32]; mov ecx,[eax+14]; push ...`; the function
// becomes `xor eax,eax; ret; nop`.
constexpr SkipLauncherWrite kEntry23Writes[] = {
    WriteBytes(0, kXorEaxRetNop),
};
// 24: `sub esp,8; mov ecx,[imm32]; mov edx,[ecx+14]; lea eax,[esp]`.
constexpr SkipLauncherWrite kEntry24Writes[] = {
    WriteBytes(0, kXorEaxRetNop),
};

// In the original's order. Entry 1 is the only row with no edits: entry 9's E9
// jumps to its match, so the handler keeps its address and writes nothing.
constexpr SkipLauncherEntry kSkipLauncherEntries[] = {
    // 0: `test bl,bl; jne` launcher check; two EB hook sites
    {"84 DB 0F 85 43 01 00 00 8D 4C 24 28 8B C1", kEntry0Writes},
    // 1: target of entry 9's jump; no edits of its own
    {"68 ? ? ? ? 89 35 ? ? ? ? E8 ? ? ? ? 83 C4 04 84 C0 0F 84", {}},
    // 2: `74 EC` / `74 E7` conditional launcher jumps
    {"74 EC 38 5E 06 74 E7 68", kEntry2Writes},
    // 3: `74 C4` leading conditional jump
    {"74 C4 8B 07 3B 46 18 75 BD 68", kEntry3Writes},
    // 4: `cmp edx,[esi+18]; jne rel32`
    {"3B 56 18 0F 85", kEntry4Writes},
    // 5: `cmp ecx,[esi+18]; jne rel32`
    {"3B 4E 18 0F 85", kEntry5Writes},
    // 6: leading `jne rel32` of an epilogue sequence
    {"0F 85 ? ? ? ? 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC C6", kEntry6Writes},
    // 7: `cmp eax,[esi+18]; jne -0x43` and a following store
    {"3B 46 18 75 BD", kEntry7Writes},
    // 8: `push edi; push ecx` + three `C7 44 24` immediate stores
    {"57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24", kEntry8Writes},
    // 9: `mov edx,[esi+1C]; cmp edx,[esi+20]` -> xor eax,eax; jmp entry 1
    {"8B 56 1C 3B 56 20 ? ? 6A 00 6A 00", kEntry9Writes},
    // 10: `mov [imm32],esi; call; add esp,4; test al,al`
    {"89 35 ? ? ? ? E8 ? ? ? ? 83 C4 04 84 C0", kEntry10Writes},
    // 11: `mov edx,[eax+2C]; cmp edx,[eax+30]` -> eax = 0
    {"8B 50 2C 3B 50 30", kEntry11Writes},
    // 12: `mov ecx,[eax+2C]; sub esp,0x78` + body tail
    {"8B 48 2C 83 EC 78", kEntry12Writes},
    // 13: the same with `sub esp,0x74`
    {"8B 48 2C 83 EC 74", kEntry13Writes},
    // 14: `mov ecx,[eax+2C]; cmp ecx,[eax+30]; jz +3`
    {"8B 48 2C 3B 48 30 74 03", kEntry14Writes},
    // 15: the same compare followed by `push ebx`
    {"8B 48 2C 3B 48 30 53", kEntry15Writes},
    // 16: the same compare followed by `jz +0x11`
    {"8B 48 2C 3B 48 30 74 11", kEntry16Writes},
    // 17: `... jne -0x24; lea edx,[esp+18]`
    {"8B 48 2C 3B 48 30 75 DC 8D 54 24 18", kEntry17Writes},
    // 18: `... jne -0x24; lea edx,[esp+1C]`
    {"8B 48 2C 3B 48 30 75 DC 8D 54 24 1C", kEntry18Writes},
    // 19: `mov ecx,[imm32]; mov edx,[ecx+2C]`
    {"8B 0D ? ? ? ? 8B 51 2C", kEntry19Writes},
    // 20: prologue pushes + `mov edi,[imm32]; mov eax,[edi+2C]`
    {"51 53 55 56 57 8B 3D ? ? ? ? 8B 47 2C", kEntry20Writes},
    // 21: `cmp eax,edi; jne rel32; test eax,eax`
    {"3B C7 0F 85 ? ? ? ? 85 C0", kEntry21Writes},
    // 22: `jne rel32; test eax,eax; je rel32; call rel32; test edx,edx`
    {"0F 85 ? ? ? ? 85 C0 0F 84 ? ? ? ? E8 ? ? ? ? 85 D2", kEntry22Writes},
    // 23: `sub esp,0x24; mov eax,[imm32]; mov ecx,[eax+14]; push ...`
    {"83 EC 24 A1 ? ? ? ? 8B 48 14 53 55 56 57", kEntry23Writes},
    // 24: `sub esp,8; mov ecx,[imm32]; mov edx,[ecx+14]; lea eax,[esp]`
    {"83 EC 08 8B 0D ? ? ? ? 8B 51 14 8D 04 24 50 6A 00 52", kEntry24Writes},
};
static_assert(std::size(kSkipLauncherEntries) == 25);

// The two collector rules: the first two matches of one pattern and the last
// two of the other, each widened by the same "mov al,1; ret; nop" stub.
constexpr std::uint8_t kCollectorStub[] = {0xB0, 0x01, 0xC3, 0x90};

constexpr SkipLauncherCollector kSkipLauncherCollectors[] = {
    {"A1 ? ? ? ? 85 C0 8B 0D ? ? ? ? 75", SkipLauncherCollector::Target::first_two,
     kCollectorStub},
    {"33 C0 83 3D ? ? ? ? 01 0F 94 C0 C3", SkipLauncherCollector::Target::last_two,
     kCollectorStub},
};

// `FF 15` is a call through an IAT slot; the slot VA is the operand at +2.
constexpr std::string_view kDfaCapturePattern = "FF 15 ? ? ? ? 8B F0 83 FE FF 74 24";

// DFA file-API hooks: the original installs these E8/E9 jumps from 0x10004850.
constexpr SkipLauncherInstall kSkipLauncherDfaInstalls[] = {
	{"E8 ? ? ? ? 83 C4 0C 83 F8 01 74 11 6A 00 6A 00", 0, kCallOpcode,
		SkipHook::dfa_init},
	{"55 8B EC 6A 00 E8", 0, kJumpOpcode, SkipHook::create_file},
	{"6A 00 E8 ? ? ? ? 83 F8 01 59 75 17 FF 15 ? ? ? ? 50 B9 ? ? ? ? E8 ? ? ? ? 8B 40 08 "
	 "FF 60 0C 33 C0 C3", 0, kJumpOpcode, SkipHook::close_handle},
	{"6A 00 E8 ? ? ? ? 83 F8 01 59 75 17 FF 15 ? ? ? ? 50 B9 ? ? ? ? E8 ? ? ? ? 8B 40 08 "
	 "FF 60 ? 83 C8 FF C3", 0, kJumpOpcode, SkipHook::set_file_pointer},
	{"56 8B 74 24 0C 8D 46 04 50 FF 74 24 0C E8 ? ? ? ? 59 59 33 C9 83 F8 FF 0F 95 C1 89 "
	 "06 5E 8B C1 C3", 0, kJumpOpcode, SkipHook::get_file_size_ex},
	{"FF 50 60 83 C4 14 39 75 F8 89 45 FC", -165, kJumpOpcode, SkipHook::read_file},
};

// ROM message-window hooks: the original installs these from 0x10004620.
constexpr SkipLauncherInstall kSkipLauncherRomInstalls[] = {
	{"89 46 3C 89 4E 40 E8", 6, kCallOpcode, SkipHook::rom_reset},
	{"0F 85 ? ? ? ? 68 03 01 00 00 68 ? ? ? ? 68 04 01 00 00", -67, kJumpOpcode,
		SkipHook::rom_set},
};

// The collector's two jump targets: index 0 of the first pattern, index 1 of the
// second.
constexpr SkipLauncherCollectedTarget kDfaCollectedTargets_0[] = {
	{"80 BC 24 ? ? ? ? ? 6A 00", 0},
	{"6A 00 68 80 00 00 00 6A 03 6A 00 6A 01 68 00 00 00 80 57", 1},
};

constexpr SkipLauncherCollectedInstall kSkipLauncherDfaCollected[] = {
	{"A1 ? ? ? ? B9 A7 FA DC 5C F7 E1 8B 0D ? ? ? ? 33 F6", kDfaCollectedTargets_0,
		std::size(kDfaCollectedTargets_0)},
};

// A signature scan over the caller-owned image, in the original's hook::pattern
// style; a miss is the one failure a pass tolerates.
hook::pattern ScanPattern(const GameImage& image, std::string_view signature)
{
	const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(image.base);
	return hook::make_range_pattern(begin, begin + image.size,
	                                NormalizeSignature(signature));
}

// A store outside the mapped image is the pass's one hard failure.
bool InBounds(GameImage image, const std::uint8_t* site, std::size_t length) noexcept
{
	return site >= image.base && site + length <= image.base + image.size;
}

} // namespace

std::span<const SkipLauncherEntry> SkipLauncherEntries() noexcept
{
	return kSkipLauncherEntries;
}

std::span<const SkipLauncherCollector> SkipLauncherCollectors() noexcept
{
	return kSkipLauncherCollectors;
}

std::span<const SkipLauncherInstall> SkipLauncherDfaInstalls() noexcept
{
	return kSkipLauncherDfaInstalls;
}

std::span<const SkipLauncherInstall> SkipLauncherRomInstalls() noexcept
{
	return kSkipLauncherRomInstalls;
}

std::span<const SkipLauncherCollectedInstall> SkipLauncherDfaCollected() noexcept
{
	return kSkipLauncherDfaCollected;
}

// The original's main pass (0x10045CE0) plus the DFA (0x10004850) and ROM
// (0x10004620) installers; each stage runs only when its bit is selected.
PatchResult ApplySkipLauncher(GameImage image, SkipHookResolver resolve, void* resolver_user,
                              SkipLauncherReport& report, SkipLauncherStage stages) noexcept
{
	if (!image.base || image.size == 0)
		return {PatchResultCode::invalid_input, "invalid skip launcher input"};

	const std::span<const SkipLauncherEntry> entries = SkipLauncherEntries();
	std::vector<const std::uint8_t*> matches(entries.size(), nullptr);

	if (HasStage(stages, SkipLauncherStage::main))
	{
		for (std::size_t i = 0; i < entries.size(); ++i)
		{
			const SkipLauncherEntry& entry = entries[i];
			hook::pattern pattern = ScanPattern(image, entry.signature);
			if (pattern.empty())
			{
				++report.entriesMissing;
				continue;
			}
			auto* match = pattern.get(0).get<std::uint8_t>();
			matches[i] = match;
			++report.entriesFound;
			// The jump-installing entry needs its target entry's match (from the
			// 0x10045CE0 scan); when it is absent the whole block is skipped, not
			// just the jump - 33 C0 + E9 + rel32 share one [ebp-0x1c] guard.
			bool targetAbsent = false;
			for (const SkipLauncherWrite& write : entry.writes)
			{
				if (write.kind != SkipLauncherWrite::Kind::jump)
					continue;
				const std::uint32_t targetEntry = write.jumpToEntry;
				if (targetEntry >= matches.size() || matches[targetEntry] == nullptr)
				{
					targetAbsent = true;
					break;
				}
			}
			if (targetAbsent)
				continue;
			for (const SkipLauncherWrite& write : entry.writes)
			{
				auto* site = match + write.offset;
				if (write.kind == SkipLauncherWrite::Kind::jump)
				{
					const std::uint8_t* target =
					    write.jumpToEntry < matches.size() ? matches[write.jumpToEntry] : nullptr;
					if (!target)
						continue;  // the original skips when the target entry was absent
					if (!InBounds(image, site, kDwordLength))
						return {PatchResultCode::out_of_bounds, "site out of bounds"};
					// The recorded site is the rel32 itself; the E9 opcode sits one
					// byte before it, so the displacement is measured from the end
					// of the dword.
					injector::WriteMemory<std::uint32_t>(  //jmp rel32
					    site,
					    static_cast<std::uint32_t>(
					        reinterpret_cast<std::uintptr_t>(target) -
					        (reinterpret_cast<std::uintptr_t>(site) + kDwordLength)),
					    true);
					report.jumpInstalled = true;
				}
				else if (write.kind == SkipLauncherWrite::Kind::fill)
				{
					if (!InBounds(image, site, write.count))
						return {PatchResultCode::out_of_bounds, "site out of bounds"};
					// Every fill row in the tables is kLauncherNopByte.
					injector::MakeNOP(site, write.count, true);  //nop
				}
				else
				{
					if (!InBounds(image, site, write.bytes.size()))
						return {PatchResultCode::out_of_bounds, "site out of bounds"};
					injector::WriteMemoryRaw(site,
					                         const_cast<std::uint8_t*>(write.bytes.data()),
					                         write.bytes.size(),
					                         true);  //the entry's bytes
				}
				++report.writes;
			}
		}

		const std::span<const SkipLauncherCollector> collectors = SkipLauncherCollectors();
		for (const SkipLauncherCollector& collector : collectors)
		{
			hook::pattern pattern = ScanPattern(image, collector.pattern);
			const std::size_t hits = pattern.size();
			if (hits == 0)
				continue;
			++report.collectorsFound;
			for (std::size_t hit = 0; hit < hits; ++hit)
			{
				// first_two keeps matches 0 and 1; last_two keeps the final two
				// (a lone match is one site for either rule).
				if (collector.target == SkipLauncherCollector::Target::first_two)
				{
					if (hit >= 2)
						break;
				}
				else if (hit + 2 < hits)
				{
					continue;
				}
				auto* site = pattern.get(hit).get<std::uint8_t>();
				if (!InBounds(image, site, collector.stub.size()))
					return {PatchResultCode::out_of_bounds, "site out of bounds"};
				injector::WriteMemoryRaw(site, const_cast<std::uint8_t*>(collector.stub.data()),
				                         collector.stub.size(), true);  //mov al,1; ret; nop
				++report.collectorWrites;
			}
		}
	}

	if (HasStage(stages, SkipLauncherStage::dfa))
	{
		// The create_file hook forwards through this IAT slot, so the capture must
		// happen before the installer writes the hook.
		hook::pattern capture = ScanPattern(image, kDfaCapturePattern);
		if (!capture.empty())
			report.createFileSlot = injector::ReadMemory<std::uint32_t>(
			    capture.get(0).get<std::uint8_t>() + 2);
	}

	if (resolve)
	{
		// Installs an opcode + rel32 to the reimplemented body. A body the resolver
		// does not know, or a missing signature, counts as missing.
		const auto InstallHooks = [&](std::span<const SkipLauncherInstall> installs,
		                              std::size_t& applied,
		                              std::size_t& missing) -> PatchResult
		{
			for (const SkipLauncherInstall& install : installs)
			{
				const std::uintptr_t destination = resolve(install.hook, resolver_user);
				if (!destination)
				{
					++missing;
					continue;
				}
				hook::pattern hookSite = ScanPattern(image, install.signature);
				if (hookSite.empty())
				{
					++missing;
					continue;
				}
				auto* site = hookSite.get(0).get<std::uint8_t>() + install.offset;
				if (!InBounds(image, site, kBranchLength))
					return {PatchResultCode::out_of_bounds, "site out of bounds"};
				// The table's opcode is only ever E8/E9 (SkipLauncherInstall), so
				// this is the original's call/jump choice written the injector way.
				if (install.opcode == kCallOpcode)
					injector::MakeCALL(injector::raw_ptr(site),
					                   injector::raw_ptr(reinterpret_cast<void*>(destination)),
					                   true); //call
				else
					injector::MakeJMP(injector::raw_ptr(site),
					                  injector::raw_ptr(reinterpret_cast<void*>(destination)),
					                  true); //jmp
				++applied;
			}
			return {};
		};

		if (HasStage(stages, SkipLauncherStage::dfa))
		{
			const auto dfaStatus =
			    InstallHooks(SkipLauncherDfaInstalls(), report.dfaInstalls, report.dfaMissing);
			if (!dfaStatus)
				return dfaStatus;

			// The collector tail of 0x10004850: each collected site jumps to another
			// pattern's match.
			const std::span<const SkipLauncherCollectedInstall> collected =
			    SkipLauncherDfaCollected();
			for (const SkipLauncherCollectedInstall& install : collected)
			{
				hook::pattern hits = ScanPattern(image, install.collector);
				for (std::size_t t = 0; t < install.targetCount; ++t)
				{
					const SkipLauncherCollectedTarget& target = install.targets[t];
					if (target.index >= hits.size())
						continue;
					hook::pattern targetPattern = ScanPattern(image, target.targetSignature);
					if (targetPattern.empty())
						continue;
					auto* site = hits.get(target.index).get<std::uint8_t>();
					if (!InBounds(image, site, kBranchLength))
						return {PatchResultCode::out_of_bounds, "site out of bounds"};
					injector::MakeJMP(
					    injector::raw_ptr(site),
					    injector::raw_ptr(reinterpret_cast<void*>(
					        targetPattern.get(0).get<std::uint8_t>())),
					    true); //jmp
					++report.collectedInstalls;
				}
			}
		}

		if (HasStage(stages, SkipLauncherStage::rom))
		{
			const auto romStatus =
			    InstallHooks(SkipLauncherRomInstalls(), report.romInstalls, report.romMissing);
			if (!romStatus)
				return romStatus;
		}
	}

	return {};
}

// Windows hook bodies (the shipped mod's CreateFile/ROM-window path).

#if defined(_WIN32)

namespace
{
// The originals live at 0x10004720..0x1000484e (the file API and DFA bodies)
// and 0x100041A0..0x10004570 (the WM_ROM message-window service, ANSI variant).
constexpr UINT kRomMessage = 43858;  // WM_ROM in the original
constexpr char kRomClass[] = "MessageWindow";
constexpr char kRomClassFallback[] = "MessageWindow2";

// xmmword_10104050..0x10104080: the table the ROM service hands to the game.
constexpr std::uint32_t kRomTable[16] = {
	8, 11, 241, 1, 240, 2, 7, 12, 3, 243, 9, 6, 10, 5, 4, 242,
};

// WM_ROM carries a word array: [4] is the action, [5] and [6] are its operands.
constexpr std::size_t kRomPayloadAction = 4;
constexpr std::size_t kRomPayloadArg0 = 5;
constexpr std::size_t kRomPayloadArg1 = 6;

// The WM_ROM actions the service implements; the values are the original's.
enum class RomAction : std::uint32_t
{
	copyTable = 18,
	add = 51,
	set = 100,
};

// SkipRomReset's object layout: [1] is the window handle, [4] the WM_ROM id;
// [0], [2], [3] and [5..18] are cleared first.
constexpr std::size_t kRomObjectWindow = 1;
constexpr std::size_t kRomObjectMessage = 4;
constexpr std::size_t kRomObjectClearFirst = 5;
constexpr std::size_t kRomObjectClearLast = 18;

constexpr int kRomReadyPollLimit = 200;
constexpr DWORD kRomReadyPollMs = 1;

std::atomic<std::uint32_t> gCreateFileSlot{0};
std::atomic<HWND> gRomWindow{nullptr};
std::atomic<bool> gRomReady{false};
std::atomic<bool> gRomStarted{false};

using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD,
                                      HANDLE);

LRESULT CALLBACK RomWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
	if (message == WM_NCCREATE)
	{
		gRomWindow.store(window);
		gRomReady.store(true);
	}
	else if (message == WM_DESTROY)
	{
		gRomWindow.store(nullptr);
		PostQuitMessage(0);
		return 0;
	}
	else if (message == kRomMessage)
	{
		auto* payload = reinterpret_cast<std::uint32_t*>(lparam);
		switch (static_cast<RomAction>(payload[kRomPayloadAction]))
		{
		case RomAction::copyTable:
			std::memcpy(reinterpret_cast<void*>(
			                static_cast<std::uintptr_t>(payload[kRomPayloadArg1])),
			            kRomTable, sizeof(kRomTable));
			return 0;
		case RomAction::add:
			return static_cast<LRESULT>(payload[kRomPayloadArg0] + payload[kRomPayloadArg1]);
		case RomAction::set:
			*reinterpret_cast<std::uint32_t*>(
			    static_cast<std::uintptr_t>(payload[kRomPayloadArg0])) = 1;
			return 0;
		default:
			return 0;
		}
	}
	return DefWindowProcA(window, message, wparam, lparam);
}

DWORD WINAPI RomThread(LPVOID)
{
	WNDCLASSEXA wc{};
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = &RomWindowProc;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = kRomClass;
	ATOM atom = RegisterClassExA(&wc);
	if (!atom)
	{
		wc.lpszClassName = kRomClassFallback;
		atom = RegisterClassExA(&wc);
		if (!atom)
			return 1;
	}
	CreateWindowExA(0, reinterpret_cast<LPCSTR>(MAKEINTATOM(atom)), "", 0, 0, 0, 0, 0,
	                HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
	MSG msg;
	while (GetMessageA(&msg, nullptr, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
	UnregisterClassA(reinterpret_cast<LPCSTR>(MAKEINTATOM(atom)), wc.hInstance);
	return 0;
}

// 0x100043D0: start the ROM service once and wait until the window exists.
void StartRomService()
{
	if (gRomStarted.exchange(true))
		return;
	const HANDLE thread = CreateThread(nullptr, 0, &RomThread, nullptr, 0, nullptr);
	if (thread)
		CloseHandle(thread);
	for (int i = 0; i < kRomReadyPollLimit && !gRomReady.load(); ++i)
		Sleep(kRomReadyPollMs);
}

// 0x10004780: replaces the DFA init call so it reports success.
int __cdecl SkipDfaInit()
{
	return 1;
}

// 0x10004820: CreateFileA through the captured IAT slot. The original trampoline
// is __cdecl (plain ret; the caller pops the seven arguments), so the ABI must
// match - a stdcall body would corrupt the stack.
HANDLE __cdecl SkipCreateFile(LPCSTR fileName, DWORD desiredAccess, DWORD shareMode,
                              LPSECURITY_ATTRIBUTES security, DWORD creationDisposition,
                              DWORD flags, HANDLE templateFile)
{
	const std::uint32_t slot = gCreateFileSlot.load();
	if (slot)
	{
		const auto fn = *reinterpret_cast<CreateFileAFn*>(static_cast<std::uintptr_t>(slot));
		if (fn)
			return fn(fileName, desiredAccess, shareMode, security, creationDisposition, flags,
			          templateFile);
	}
	return CreateFileA(fileName, desiredAccess, shareMode, security, creationDisposition, flags,
	                   templateFile);
}

// 0x10004790: the original wraps CloseHandle in SEH and returns -1 (its `or
// eax,-1` filter at 0x100047ec) when it faults. __cdecl like the original
// trampoline; the SEH keywords are MSVC-only.
BOOL __cdecl SkipCloseHandle(HANDLE handle)
{
	__try
	{
		return CloseHandle(handle);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return static_cast<BOOL>(-1);
	}
}

// 0x10004760
DWORD __cdecl SkipSetFilePointer(HANDLE handle, LONG distance, PLONG distanceHigh,
                                 DWORD moveMethod)
{
	return SetFilePointer(handle, distance, distanceHigh, moveMethod);
}

// 0x10004720
BOOL __cdecl SkipGetFileSizeEx(HANDLE handle, PLARGE_INTEGER size)
{
	return GetFileSizeEx(handle, size);
}

// 0x10004740
BOOL __cdecl SkipReadFile(HANDLE handle, LPVOID buffer, DWORD toRead, LPDWORD read,
                          LPOVERLAPPED overlapped)
{
	return ReadFile(handle, buffer, toRead, read, overlapped);
}

// 0x10004570: start the ROM service, reset the object and publish the window
// handle plus the WM_ROM id. __cdecl with one stack argument, returning the
// window handle in eax (plain ret).
int __cdecl SkipRomReset(std::uint32_t* object)
{
	StartRomService();
	if (!object)
		return 0;
	object[0] = 0;
	object[2] = 0;
	object[3] = 0;
	for (std::size_t i = kRomObjectClearFirst; i <= kRomObjectClearLast; ++i)
		object[i] = 0;
	object[kRomObjectWindow] =
	    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(gRomWindow.load()));
	object[kRomObjectMessage] = kRomMessage;
	return static_cast<int>(object[kRomObjectWindow]);
}

// 0x10004610: the object arrives in ESI (the patched site keeps it there) and
// the site pushed two arguments the callee must pop - the original is
// `mov ecx,[esi+1c]; mov [esi+20],ecx; mov al,1; ret 8`. A C function cannot
// express that, so it is reproduced instruction-for-instruction.
__declspec(naked) void SkipRomSet()
{
	__asm {
		mov ecx, [esi + 0x1c]
		mov [esi + 0x20], ecx
		mov al, 1
		ret 8
	}
}

} // namespace

void SetCreateFileSlot(std::uint32_t slot_va) noexcept
{
	gCreateFileSlot.store(slot_va);
}

std::uintptr_t ResolveSkipHook(SkipHook hook, void*) noexcept
{
	switch (hook)
	{
	case SkipHook::dfa_init:
		return reinterpret_cast<std::uintptr_t>(&SkipDfaInit);
	case SkipHook::create_file:
		return reinterpret_cast<std::uintptr_t>(&SkipCreateFile);
	case SkipHook::close_handle:
		return reinterpret_cast<std::uintptr_t>(&SkipCloseHandle);
	case SkipHook::set_file_pointer:
		return reinterpret_cast<std::uintptr_t>(&SkipSetFilePointer);
	case SkipHook::get_file_size_ex:
		return reinterpret_cast<std::uintptr_t>(&SkipGetFileSizeEx);
	case SkipHook::read_file:
		return reinterpret_cast<std::uintptr_t>(&SkipReadFile);
	case SkipHook::rom_reset:
		return reinterpret_cast<std::uintptr_t>(&SkipRomReset);
	case SkipHook::rom_set:
		return reinterpret_cast<std::uintptr_t>(&SkipRomSet);
	}
	return 0;
}

#endif  // _WIN32
