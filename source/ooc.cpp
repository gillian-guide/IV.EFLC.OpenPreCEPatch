// zp.hook_bodies - OutOfCommissionFix's compressed payload: the opcode
// decoder, the fake-pak registration and the in-memory archive builder.
// Ordinary C++ with no register contract, so it lives outside the assembly
// unit (P3); the naked branch body stays in source/hook_bodies.cpp and the
// wrapper here mounts the archive.

#include "common.hxx"
#include "common.h"
#include "rebased.h"
#include "hooks.h"
#include "hook_bodies.h"
#include "body_helpers.h"

// The original ASI's compressed "finale2.sco" resource (OutOfCommissionFix's
// payload), extracted from the shipped mod.
#include "ooc_payload.h"

#if defined(_WIN32)

namespace
{
constexpr std::uint16_t kOocPadMask = 0x1Fu << 11;  // record[15:11] padding
constexpr std::size_t kOocPathCapacity = 0x44u;  // the mount-string buffer
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

// OutOfCommissionFix (callback 0x1004C3F0). Scan 1 (A9 39 44 24 18 at game
// 0xA22E90) E9s match+7 to the branch-widening body and publishes match+0x11 as
// its resume. Scan 2 (E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56 at game 0xB70733)
// E8s the match to the wrapper and publishes dword_10159BF8 (the E8 at
// match-0x33's target) / dword_10159C04 (the match E8's original target).
// ---------------------------------------------------------------------------
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

// The single registered entry's name (used by the entry, the archive header and
// the self-test) and the archive's fixed offsets.
constexpr char kOocName[] = "finale2.sco";
constexpr std::size_t kOocNameLength = sizeof(kOocName) - 1;
constexpr std::size_t kOocHeaderSize = 20;
constexpr std::size_t kOocRecordSize = 16;
constexpr std::size_t kOocRecordSizeOffset = 0;    // physical size dword
constexpr std::size_t kOocRecordIndexOffset = 8;   // entry index dword
constexpr std::size_t kOocRecordPagesOffset = 12;  // page count word
constexpr std::size_t kOocRecordPadOffset = 14;    // padding word
constexpr std::size_t kOocArchiveRecordOffset = kOocHeaderSize;
constexpr std::size_t kOocArchiveRecordTagOffset = kOocArchiveRecordOffset + 12;
constexpr std::size_t kOocArchiveNameOffset = kOocArchiveRecordOffset + kOocRecordSize;

// The fake-pak vector is the original's own global storage: begin/end/
// capacity are the handler's dwords, so its growth keeps ::operator new and
// ::operator delete and the raw pointers. That is recovered ABI state, not a
// local allocation.
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
	std::memcpy(entry.name_buffer, kOocName, sizeof(kOocName));
	entry.name_size = static_cast<std::uint32_t>(kOocNameLength);
	entry.name_capacity = static_cast<std::uint32_t>(sizeof(entry.name_buffer) - 1);
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
std::uint32_t zpatch_ooc_adler32(std::span<const unsigned char> data) noexcept
{
	std::uint32_t a = 1u;
	std::uint32_t b = 0u;
	for (std::size_t block = 0; block < data.size(); block += kOocAdlerBlock)
	{
		const std::size_t end =
		    (block + kOocAdlerBlock < data.size()) ? block + kOocAdlerBlock : data.size();
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
	if (kOocPakBlob[pc] != kOocOpEnd || kOocPakBlob[pc + 1] != kOocPakMarker) return false;
	const std::uint32_t trailer = (static_cast<std::uint32_t>(kOocPakBlob[pc + 2]) << 24) |
	                              (static_cast<std::uint32_t>(kOocPakBlob[pc + 3]) << 16) |
	                              (static_cast<std::uint32_t>(kOocPakBlob[pc + 4]) << 8) |
	                              static_cast<std::uint32_t>(kOocPakBlob[pc + 5]);
	return zpatch_ooc_adler32({table, kOocTableSize}) == trailer;
}

// sub_1004ACE0 for the single registered entry ("finale2.sco", table, 179536).
// 20-byte header, one 16-byte record, the name, then the payload at offset 2048
// followed by the zero padding to the next 2048 boundary. Ownership is explicit:
// the builder consumes the malloc'd table (the original frees it inside the
// payload loop) and returns the malloc'd archive, which the caller owns.
// `final_offset` is the original's dword_10159C78.
// Archive page size (the 0x800 granularity of the payload/records).
constexpr std::uint32_t kOocPageSize = 2048u;

[[nodiscard]] std::unique_ptr<unsigned char, FreeDeleter> zpatch_ooc_build_archive(
    std::unique_ptr<unsigned char, FreeDeleter> table, std::uint32_t& final_offset) noexcept
{
	const std::uint32_t size = static_cast<std::uint32_t>(kOocTableSize);
	const std::uint32_t remainder = size & (kOocPageSize - 1u);
	const std::uint32_t pages = size / kOocPageSize + (remainder != 0u ? 1u : 0u);
	const std::uint32_t total = pages * kOocPageSize + kOocPageSize;
	// No null check, like the original's archive build: the memset below faults
	// on a failed allocation.
	std::unique_ptr<unsigned char, FreeDeleter> archive(
	    static_cast<unsigned char*>(std::malloc(total)));
	std::memset(archive.get(), 0, total);
	std::uint32_t offset = 0;
	const auto append = [&](const void* source, std::uint32_t bytes) noexcept
	{
		std::memcpy(archive.get() + offset, source, bytes);
		offset += bytes;
		zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
	};
	zpatch_ooc_store(&zpatch_ooc_archive_offset, 0);
	const std::uint32_t name_bytes = sizeof(kOocName);  // includes the NUL
	struct OocHeader final
	{
		std::uint32_t fields[4];
		std::uint16_t words[2];
	};
	static_assert(sizeof(OocHeader) == kOocHeaderSize);
	const OocHeader header{{kOocArchiveMagic, 3u, 1u, name_bytes + 16u},
	                       {kOocHeaderPage, kOocHeaderOp}};
	append(&header, sizeof(header));
	std::array<unsigned char, kOocRecordSize> record{};
	std::memcpy(record.data() + kOocRecordSizeOffset, &size,
	            sizeof(size));  // physical size (entry[0x20])
	const std::uint32_t index = 1u;  // v9 + 1
	std::memcpy(record.data() + kOocRecordIndexOffset, &index, sizeof(index));
	const std::uint16_t record_pages = static_cast<std::uint16_t>(pages);
	std::memcpy(record.data() + kOocRecordPagesOffset, &record_pages,
	            sizeof(record_pages));  // sub_1004ABB0
	if (remainder != 0u)
	{
		const std::uint16_t padding =
		    static_cast<std::uint16_t>(kOocPageSize - remainder);
		const std::uint16_t packed =
		    static_cast<std::uint16_t>((static_cast<std::uint16_t>(0u) & kOocPadMask) | padding);
		std::memcpy(record.data() + kOocRecordPadOffset, &packed,
		            sizeof(packed));  // sub_1004AB90
	}
	append(record.data(), kOocRecordSize);
	append(kOocName, name_bytes);
	offset = kOocPageSize;                         // the original resets dword_10159C78
	zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
	append(table.get(), size);
	table.reset();                                 // j_j_free inside the payload loop
	if (remainder != 0u)
		offset += kOocPageSize - remainder;        // zero padding (already memset)
	if (remainder != 0u) zpatch_ooc_store(&zpatch_ooc_archive_offset, offset);
	final_offset = offset;
	return archive;
}

// sub_1004B750: the one-shot init the wrapper runs before mounting. Its guard
// dword_10159C00 has no writer in the original, so it re-runs (and leaks the
// archive) on every call - kept 1:1. Both mallocs are unchecked like the
// original: the table block is written through even when malloc failed, and the
// archive is handed to the game and intentionally not freed.
std::uint32_t zpatch_ooc_lookup() noexcept
{
	std::unique_ptr<unsigned char, FreeDeleter> table(
	    static_cast<unsigned char*>(std::malloc(kOocTableSize)));
	static_cast<void>(zpatch_ooc_decode(table.get()));  // the original discards the result
	zpatch_ooc_register_entry(table.get());
	std::uint32_t final_offset = 0;
	std::unique_ptr<unsigned char, FreeDeleter> archive =
	    zpatch_ooc_build_archive(std::move(table), final_offset);
	zpatch_ooc_store(&zpatch_ooc_vector_end,
	                 zpatch_ooc_vector_begin);
	zpatch_ooc_store(&zpatch_ooc_state, final_offset);
	// The mounted archive stays alive for the game; the original leaks it on
	// every re-run of sub_1004B750 (its guard dword has no writer).
	return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(archive.release()));
}

std::uint32_t zpatch_ooc_fnv1a(std::span<const unsigned char> data) noexcept
{
	std::uint32_t hash = 2166136261u;
	for (std::size_t i = 0; i < data.size(); ++i)
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
	std::unique_ptr<unsigned char, FreeDeleter> table(
	    static_cast<unsigned char*>(std::malloc(kOocTableSize)));
	if (!table) return false;
	const bool decoded = zpatch_ooc_decode(table.get());
	const std::uint32_t table_adler =
	    zpatch_ooc_adler32({table.get(), kOocTableSize});
	std::uint32_t final_offset = 0;
	std::unique_ptr<unsigned char, FreeDeleter> archive =
	    zpatch_ooc_build_archive(std::move(table), final_offset);
	if (!archive) return false;
	constexpr std::array<std::uint32_t, 4> header{kOocArchiveMagic, 3u, 1u, 28u};
	bool header_ok = true;
	for (int i = 0; i < 4; ++i)
		header_ok = header_ok && std::memcmp(archive.get() + 4 * i, &header[i], 4) == 0;
	header_ok = header_ok && archive.get()[16] == kOocHeaderPage && archive.get()[17] == 0u &&
	            archive.get()[18] == kOocHeaderOp && archive.get()[19] == 0u;
	const std::uint32_t record_size = static_cast<std::uint32_t>(kOocTableSize);
	const std::uint32_t record_index = 1u;
	bool record_ok =
	    std::memcmp(archive.get() + kOocArchiveRecordOffset, &record_size, 4) == 0;
	const std::uint32_t zero = 0u;
	record_ok =
	    record_ok &&
	    std::memcmp(archive.get() + kOocArchiveRecordOffset + 4, &zero, 4) == 0 &&
	    std::memcmp(archive.get() + kOocArchiveRecordOffset + kOocRecordIndexOffset,
	                &record_index, 4) == 0;
	record_ok = record_ok && archive.get()[kOocArchiveRecordTagOffset] == 88u &&
	            archive.get()[kOocArchiveRecordTagOffset + 1] == 0u &&
	            archive.get()[kOocArchiveRecordTagOffset + 2] == kOocRecordTag &&
	            archive.get()[kOocArchiveRecordTagOffset + 3] == kOocRecordTag2;
	const bool name_ok =
	    std::memcmp(archive.get() + kOocArchiveNameOffset, kOocName, sizeof(kOocName)) == 0;
	return decoded && final_offset == kOocDecodedLength && table_adler == kOocTableAdler &&
	       header_ok && record_ok && name_ok &&
	       zpatch_ooc_fnv1a({archive.get(), final_offset}) == kOocArchiveFnv;
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
		// sub_1004C350 calls the logger and the original target unguarded.
		zpatch_ooc_logger(buffer, 1, -1);
	}
	return zpatch_ooc_orig(arg0, arg1);
}

#endif  // _WIN32
