#pragma once
// ZolikaPatch.ini reader.
//
// The loader walks the file through an INI<> object and asks it for one key at a
// time, the way the older source did:
//
//   INI<> ini("ZolikaPatch.ini", true);
//   ini.select("Options");
//   if (std::stoi(ini.get("SkipLauncher", "1")) == 1) ...

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <cstdlib>

struct ConfigEntry
{
	std::string section;
	std::string key;
	std::string value;
};

struct ConfigDocument
{
	std::vector<ConfigEntry> entries;

	// Open-addressed (section, key) index over `entries`: a slot holds the entry's index
	// + 1, 0 is empty. Built once after the parse, so a lookup is O(1) instead of a reverse
	// scan of ~2,100 entries (the table loader alone asks ~2,000 times at start-up). Entries
	// appended after the parse are not indexed; find() scans those first, so an append still
	// shadows the file.
	std::vector<std::uint32_t> index;
	std::size_t indexed = 0;

	// Indexes the entries current at parse time. Never throws (on allocation
	// failure the index stays empty and find() falls back to the reverse scan).
	void BuildIndex() noexcept;

	// Duplicate keys keep their file order; lookup is last-value-wins.
	const ConfigEntry* find(std::string_view section, std::string_view key) const noexcept;
};

bool LoadConfig(std::string_view path, ConfigDocument& output) noexcept;
bool ParseConfig(std::string_view text, ConfigDocument& output) noexcept;

// Raw value, or nullptr. The caller decides what an absent key means.
const char* ReadIniString(const ConfigDocument& config, const char* section, const char* key) noexcept;
const char* ReadIniString(const ConfigDocument& config, const char* key) noexcept;

// The empty template argument list is the original source's spelling
// (`INI<> ini(...)`); the dummy parameter exists only so that spelling works.
template <typename = void>
class INI
{
public:
	INI(const char* path, bool /*save*/ = false)
		: loaded_(LoadConfig(path, document_))
	{
	}

	void select(const char* section) { section_ = section ? section : ""; }

	const char* get(const char* key, const char* defaultValue) const
	{
		const ConfigEntry* entry = document_.find(section_, key);
		return entry ? entry->value.c_str() : defaultValue;
	}

	const char* get(const char* key) const { return get(key, nullptr); }

	bool loaded() const { return loaded_; }
	ConfigDocument& document() { return document_; }
	const ConfigDocument& document() const { return document_; }

private:
	ConfigDocument document_;
	bool loaded_{};
	const char* section_{""};
};
