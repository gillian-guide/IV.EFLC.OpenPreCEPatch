// zp.hook_bodies - NewSavesCompatibility: the save-file reader and the
// converter (original sub_10051B30) that walks the 18 chunks and patches the
// three game sites. Ordinary C++ and file IO, so it lives outside the
// assembly unit (P3); the savegame-load tramp itself stays naked in
// source/hook_bodies.cpp.

#include "common.hxx"
#include "common.h"
#include "rebased.h"
#include "hooks.h"
#include "hook_bodies.h"
#include "body_helpers.h"

#if defined(_WIN32)

namespace
{
// NewSaves: header layout and the "SAVE" magic the reader checks.
constexpr std::size_t kSaveHeaderSize = 0x110u;
constexpr std::size_t kSaveMagicOffset = 0x0C;
// The save-name buffers sub_10051FD0/1FE0 use: the game directory buffer and the
// "<dir>\\<save name>" path built before converting.
constexpr std::size_t kSaveDirectoryCapacity = 312;
constexpr std::size_t kSaveFilePathCapacity = 560;

// NewSaves' flag16 restore branch: 75 26 = jne +0x26 (the two bytes the original
// writes back over the forced short jump).
constexpr std::uint16_t kJneRel8Plus38 = 0x2675u;
constexpr std::uint32_t kSaveMagic = static_cast<std::uint32_t>('S') |
                                     (static_cast<std::uint32_t>('A') << 8) |
                                     (static_cast<std::uint32_t>('V') << 16) |
                                     (static_cast<std::uint32_t>('E') << 24);
static_assert(kSaveMagic == 0x45564153u);

// NewSavesCompatibility's save-image layout (original converter sub_10051B30
// and the tramp bodies): an 0x110-byte header, then kSaveChunkCount chunks of a
// 9-byte header whose dword at +5 is the chunk's total size, followed by that
// payload. The three chunk sizes identify the newer save format; P2 writes one
// of the two read sizes into the game's read call for it.
constexpr std::size_t kSaveChunkCount = 18;
constexpr std::uint32_t kSaveChunkHeaderSize = 9;
constexpr std::size_t kSaveChunkSizeOffset = 5;
constexpr std::uint32_t kSaveChunk9SizeNew = 19525u;
constexpr std::uint32_t kSaveChunk13SizeNew = 5953u;
constexpr std::uint32_t kSaveChunk16SizeNew = 3703u;
constexpr std::uint32_t kSaveReadSizeNew = 3694u;  // overwrites the 0x0CBE push
constexpr std::uint32_t kSaveReadSizeOld = 3262u;
// The read hook reads into its own scratch, not the caller's buffer (the
// original body does the same); 12 is the extra block P3 probes.
constexpr std::uint32_t kNewSaveScratchSize = 3704u;
constexpr std::uint32_t kNewSaveProbeSize = 12u;
// Where the converter's hook sites sit inside their signature match: the E8 at
// +6 (operand +7), the later LoadData call P3 repoints (+30), and the jne that
// selects the read path (-5).
constexpr std::ptrdiff_t kNewSaveCallSite = 6;
constexpr std::ptrdiff_t kNewSaveCallOperand = 7;
constexpr std::ptrdiff_t kNewSaveLoadOperand = 30;
constexpr std::ptrdiff_t kNewSaveBranchSite = -5;
// The read size is the immediate of the `68 BE 0C 00 00` push at P2 (+1).
constexpr std::ptrdiff_t kNewSaveReadSizeOperand = 1;

}  // namespace

namespace
{
// The one VA-to-function conversion the bodies use: a handler stores its
// destination as a plain module VA (dword), so this is the only place that
// turns it back into a callable.
template <typename Fn>
Fn GameFunction(std::uintptr_t va) noexcept
{
	return reinterpret_cast<Fn>(va);
}

// The save file is this code's own file: an owned stream in the original's
// binary read-only mode. A short read leaves the rest of the buffer untouched
// and is ignored, exactly like the original's unchecked std::fread calls.
class SaveFile final
{
public:
	explicit SaveFile(const char* path) : stream_(path, std::ios::binary) {}

	bool IsOpen() const noexcept { return stream_.is_open(); }

	void Read(std::span<std::byte> out) noexcept
	{
		ReadRaw(out.data(), static_cast<std::uint32_t>(out.size()));
	}

	// The unchecked-payload path: the original hands std::malloc's pointer
	// straight to fread, so a null block faults inside the read. Keep that.
	void ReadRaw(std::byte* out, std::uint32_t bytes) noexcept
	{
		stream_.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(bytes));
	}

private:
	std::ifstream stream_;
};

std::array<std::byte, kSaveHeaderSize> ReadSaveHeader(SaveFile& file) noexcept
{
	std::array<std::byte, kSaveHeaderSize> header;
	file.Read(header);
	return header;
}

// One chunk header: the dword at +5 is the chunk's total size (payload follows).
struct SaveChunk final
{
	std::uint32_t size;
	std::uint32_t payload;
};

SaveChunk ReadChunkHeader(SaveFile& file, std::span<std::byte> chunk) noexcept
{
	file.Read(chunk);
	std::uint32_t size = 0;
	std::memcpy(&size, chunk.data() + kSaveChunkSizeOffset, sizeof(size));
	return {size, size - kSaveChunkHeaderSize};
}

// The original mallocs the payload unchecked and passes it to fread. The
// unique_ptr adds ownership only (same malloc/free, no null guard), so the
// fault on a failed allocation is unchanged.
void ReadPayload(SaveFile& file, std::uint32_t bytes) noexcept
{
	const std::unique_ptr<void, FreeDeleter> payload(std::malloc(bytes));
	file.ReadRaw(static_cast<std::byte*>(payload.get()), bytes);
}
}  // namespace

extern "C"
{
using NewSaveLoadByteFn = std::uint8_t(__cdecl*)(void*, std::uint32_t);
using NewSaveLoadIntFn = std::int32_t(__cdecl*)(void*, std::uint32_t);
using NewSaveOriginalFn = std::int32_t(__cdecl*)();

// The existing read call, routed through the plugin: the caller's buffer is
// intentionally ignored - the original body reads into its own
// kNewSaveScratchSize scratch and only the return value is observed.
extern "C" std::uint8_t abi_new_saves_read_hook(void* /*caller_buffer*/,
                                                std::uint32_t size) noexcept
{
	std::array<std::uint8_t, kNewSaveScratchSize> scratch;
	if (zpatch_new_saves_loaddata == 0) return 0;
	return GameFunction<NewSaveLoadByteFn>(zpatch_new_saves_loaddata)(scratch.data(), size);
}

// P3's replacement: with the newer format on, probe the 12-byte block before
// and after the original call. The original issues both loads - the first
// result is discarded and the second returned - so the duplicate is kept.
extern "C" std::int32_t abi_new_saves_p3_hook() noexcept
{
	std::array<std::uint8_t, kNewSaveProbeSize> probe;
	const auto load = GameFunction<NewSaveLoadIntFn>(zpatch_new_saves_loaddata);
	const bool extra_block = zpatch_new_saves_extra_block != 0;
	if (extra_block && load)
		static_cast<void>(load(probe.data(), kNewSaveProbeSize));
	const std::int32_t result = GameFunction<NewSaveOriginalFn>(
	    zpatch_new_saves_p3_original)();
	if (extra_block && load) return load(probe.data(), kNewSaveProbeSize);
	return result;
}

extern "C" void abi_new_saves_convert(const char* path) noexcept
{
	SaveFile file(path);
	if (!file.IsOpen()) return;
	const std::array<std::byte, kSaveHeaderSize> header = ReadSaveHeader(file);
	std::uint32_t magic = 0;
	std::memcpy(&magic, header.data() + kSaveMagicOffset, sizeof(magic));
	if (magic != kSaveMagic) return;  // "SAVE"
	for (std::size_t i = 0; i < kSaveChunkCount; ++i)
	{
		std::array<std::byte, kSaveChunkHeaderSize> chunk;
		const SaveChunk info = ReadChunkHeader(file, chunk);
		zpatch_new_saves_chunk_len = info.size;           // dword_1015AD90
		zpatch_new_saves_element_count = info.payload;    // dword_1015ADA0
		ReadPayload(file, info.payload);
		if (i == 9)
			zpatch_new_saves_force_new_read = info.size == kSaveChunk9SizeNew ? 1u : 0u;
		if (i == 16)
			zpatch_new_saves_new_chunk16 = info.size == kSaveChunk16SizeNew ? 1u : 0u;
		if (i == 13)
			zpatch_new_saves_extra_block = info.size == kSaveChunk13SizeNew ? 1u : 0u;
	}

	// P1 (game 0x872185): force the 0x3C-byte read path for new saves.
	constexpr std::uint8_t kJeRel8Opcode = 0x74u;   // je rel8
	constexpr std::uint8_t kJmpRel8Opcode = 0xEBu;  // jmp rel8
	static const std::uint8_t* p1 = nullptr;
	if (p1 == nullptr) p1 = scan_game("74 37 80 3D ? ? ? ? 00 75 2E 8D 84 ? ? ? ? 00 6A 1E");
	if (p1 != nullptr)
		wp_byte(const_cast<std::uint8_t*>(p1),
		        zpatch_new_saves_force_new_read != 0 ? kJmpRel8Opcode : kJeRel8Opcode);

	// P2 (game 0x7D8C50): route the oversized read through the plugin's scratch
	// buffer, resize the read, and force/NOP the branch per the save format.
	static const std::uint8_t* p2 = nullptr;
	if (p2 == nullptr) p2 = scan_game("68 BE 0C 00 00 50 E8");
	if (p2 != nullptr)
	{
		auto* site = const_cast<std::uint8_t*>(p2);
		const auto original_target = static_cast<std::uint32_t>(
		    injector::ReadRelativeOffset(injector::raw_ptr(site + kNewSaveCallOperand), 4, true)
		        .as_int());
		injector::MakeCALL(injector::raw_ptr(site + kNewSaveCallSite),
		                   injector::raw_ptr(reinterpret_cast<void*>(&abi_new_saves_read_hook)),
		                   true); //call
		if (zpatch_new_saves_loaddata == 0)
			zpatch_new_saves_loaddata = original_target;
		wp_dword(site + kNewSaveReadSizeOperand,
		         zpatch_new_saves_new_chunk16 != 0 ? kSaveReadSizeNew : kSaveReadSizeOld);
		if (zpatch_new_saves_new_chunk16 != 0)
			injector::MakeNOP(injector::raw_ptr(site + kNewSaveBranchSite), 2, true); //nop
		else
			wp_word(site + kNewSaveBranchSite, kJneRel8Plus38);  // 75 26 = jne +0x26
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
		    injector::ReadRelativeOffset(injector::raw_ptr(site + kNewSaveCallOperand), 4, true)
		        .as_int());
		injector::MakeCALL(injector::raw_ptr(site + kNewSaveCallSite),
		                   injector::raw_ptr(reinterpret_cast<void*>(&abi_new_saves_p3_hook)),
		                   true); //call
		if (zpatch_new_saves_p3_original == 0)
			zpatch_new_saves_p3_original = original_target;
		zpatch_new_saves_loaddata = static_cast<std::uint32_t>(
		    injector::ReadRelativeOffset(injector::raw_ptr(site + kNewSaveLoadOperand), 4, true)
		        .as_int());
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
	std::array<char, kSaveDirectoryCapacity> directory{};
	char suffix = 0;
	if (reinterpret_cast<std::uint8_t(__cdecl*)(int, char*, char*)>(
	        const_cast<std::uint8_t*>(get_dir))(0, directory.data(), &suffix) == 0)
		return;
	std::array<char, kSaveFilePathCapacity> path;
	std::snprintf(path.data(), path.size(), "%s\\%s", directory.data(), name);
	abi_new_saves_convert(path.data());
}
}  // extern "C"

#endif  // _WIN32
