// zp.config - ZolikaPatch.ini: the parsed document, its lookup index and the
// loader's per-arm read.
//
// The reader is the shipped reader's: last value for a key wins, an absent key
// yields the caller's default, an option runs only when the value parses to exactly
// 1, and a malformed line rejects the file. `ConfigEntry`, `ConfigDocument` and
// `INI<>` stay declared in `src/INI.h`; this unit owns their implementations.

module;

#include "common.hxx"
#include "common.h"

export module zp.config;

import zp.common;

export
{
// The section every option key lives in; the loader selects it once.
inline constexpr const char* kOptionsSection = "Options";

// Reads the key with the default its loader arm substitutes.
bool Option(INI<>& ini, const char* key, const char* defaultValue);

bool ParseConfig(std::string_view text, ConfigDocument& output) noexcept;

bool LoadConfig(std::string_view path, ConfigDocument& output) noexcept;

const char* ReadIniString(const ConfigDocument& config, const char* section,
                          const char* key) noexcept;

const char* ReadIniString(const ConfigDocument& config, const char* key) noexcept;
}  // export

namespace
{
// The parser index cannot address more than this many entries (slot = entry+1).
constexpr std::size_t kMaxIndexEntries = 0xFFFFFFF0u;
// FNV-1a over section+key needs it, so ("a","bc") and ("ab","c") differ.
constexpr std::uint64_t kSectionKeySeparator = 0xFFu;
}  // namespace

namespace
{
std::uint64_t ConfigKeyHash(std::string_view section, std::string_view key) noexcept
{
	// FNV-1a 64: the standard offset basis and prime.
	constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ull;
	constexpr std::uint64_t kFnvPrime = 0x100000001b3ull;
	std::uint64_t hash = kFnvOffsetBasis;
	for (const char c : section)
	{
		hash ^= static_cast<unsigned char>(c);
		hash *= kFnvPrime;
	}
	hash ^= kSectionKeySeparator;
	hash *= kFnvPrime;
	for (const char c : key)
	{
		hash ^= static_cast<unsigned char>(c);
		hash *= kFnvPrime;
	}
	return hash;
}
} // namespace

void ConfigDocument::BuildIndex() noexcept
{
	index.clear();
	indexed = 0;
	try
	{
		if (entries.empty() || entries.size() > kMaxIndexEntries)
			return;
		std::size_t cells = 16;
		while (cells < entries.size() * 2)
			cells <<= 1;
		index.assign(cells, 0);
		const std::size_t mask = cells - 1;
		for (std::size_t i = 0; i < entries.size(); ++i)
		{
			const ConfigEntry& entry = entries[i];
			std::size_t slot = static_cast<std::size_t>(ConfigKeyHash(entry.section, entry.key)) & mask;
			for (;;)
			{
				const std::uint32_t stored = index[slot];
				if (stored == 0)
				{
					index[slot] = static_cast<std::uint32_t>(i + 1);
					break;
				}
				const ConfigEntry& existing = entries[stored - 1];
				if (existing.section == entry.section && existing.key == entry.key)
				{
					// The reader is last-value-wins: repoint the slot at the newer one.
					index[slot] = static_cast<std::uint32_t>(i + 1);
					break;
				}
				slot = (slot + 1) & mask;
			}
		}
		indexed = entries.size();
	}
	catch (...)
	{
		// No index: find() falls back to the reverse scan (slower, same result).
		index.clear();
		indexed = 0;
	}
}

const ConfigEntry* ConfigDocument::find(std::string_view section, std::string_view key) const noexcept
{
	// Entries appended after the parse shadow the file (the Patch7 compatibility
	// append and the write-set dump's per-option isolation) and are not indexed.
	for (std::size_t i = entries.size(); i > indexed; --i)
	{
		const ConfigEntry& entry = entries[i - 1];
		if (entry.section == section && entry.key == key)
			return &entry;
	}

	if (!index.empty())
	{
		const std::size_t mask = index.size() - 1;
		std::size_t slot = static_cast<std::size_t>(ConfigKeyHash(section, key)) & mask;
		for (std::size_t probes = 0; probes < index.size(); ++probes)
		{
			const std::uint32_t stored = index[slot];
			if (stored == 0)
				break; // an empty slot ends the probe chain
			const ConfigEntry& entry = entries[stored - 1];
			if (entry.section == section && entry.key == key)
				return &entry;
			slot = (slot + 1) & mask;
		}
		return nullptr; // the index covers every parsed entry
	}

	// Hand-built documents and a failed index build: the original's reverse scan.
	for (std::size_t i = indexed; i > 0; --i)
	{
		const ConfigEntry& entry = entries[i - 1];
		if (entry.section == section && entry.key == key)
			return &entry;
	}
	return nullptr;
}

bool ParseConfig(std::string_view text, ConfigDocument& output) noexcept
{
	try
	{
		ConfigDocument parsed;
		std::string section;
		bool firstLine = true;
		std::size_t offset = 0;

		while (offset <= text.size())
		{
			const std::size_t end = text.find('\n', offset);
			std::string_view line = text.substr(
				offset, end == std::string_view::npos ? text.size() - offset : end - offset);
			offset = end == std::string_view::npos ? text.size() + 1 : end + 1;

			if (firstLine)
			{
				firstLine = false;
				constexpr std::string_view bom{"\xEF\xBB\xBF", 3};
				if (line.substr(0, bom.size()) == bom)
					line.remove_prefix(bom.size());
			}

			line = Trim(line);
			if (line.empty() || line.front() == ';' || line.front() == '#')
				continue;

			if (line.front() == '[')
			{
				const std::size_t close = line.find(']');
				if (close == std::string_view::npos)
					return false;

				const std::string_view name = Trim(line.substr(1, close - 1));
				const std::string_view suffix = Trim(line.substr(close + 1));
				if (name.empty() ||
				    (!suffix.empty() && suffix.front() != ';' && suffix.front() != '#'))
					return false;

				section.assign(name);
				continue;
			}

			const std::size_t equals = line.find('=');
			if (equals == std::string_view::npos)
				return false;

			const std::string_view key = Trim(line.substr(0, equals));
			const std::string_view value = Trim(line.substr(equals + 1));
			if (key.empty() || key.front() == '[')
				return false;

			parsed.entries.push_back({section, std::string(key), std::string(value)});
		}

		parsed.BuildIndex();
		output = std::move(parsed);
		return true;
	}
	catch (...)
	{
		return false;
	}
}

bool LoadConfig(std::string_view path, ConfigDocument& output) noexcept
{
	if (path.empty())
		return false;

	try
	{
		std::ifstream input(std::string(path), std::ios::binary);
		if (!input)
			return false;

		std::string contents((std::istreambuf_iterator<char>(input)),
		                     std::istreambuf_iterator<char>());
		if (input.bad())
			return false;

		return ParseConfig(contents, output);
	}
	catch (...)
	{
		return false;
	}
}

const char* ReadIniString(const ConfigDocument& config, const char* section, const char* key) noexcept
{
	const ConfigEntry* entry = config.find(section, key);
	return entry ? entry->value.c_str() : nullptr;
}

const char* ReadIniString(const ConfigDocument& config, const char* key) noexcept
{
	return ReadIniString(config, kOptionsSection, key);
}

bool Option(INI<>& ini, const char* key, const char* defaultValue)
{
	// The original uses std::stoi here, so a non-numeric value throws out of the
	// loader instead of reading as "off"; that fault is preserved, not guarded.
	return std::stoi(ini.get(key, defaultValue), nullptr, 10) == 1;
}
