#pragma once
// ZolikaPatch.ini reader.
//
// The loader walks the file through an INI<> object and asks it for one key at a
// time, the way the older source did:
//
//   INI<> ini("ZolikaPatch.ini", true);
//   ini.select("Options");
//   if (std::stoi(ini.get("SkipLauncher", "1")) == 1) ...
//
// What is read here are the shipped reader's rules, not a general-purpose INI
// library's: the last entry for a key wins, an absent key yields the caller's
// default, and an option runs only when its value parses to exactly 1.

#include <cstddef>
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

	// Duplicate keys keep their file order; lookup is last-value-wins.
	const ConfigEntry* find(std::string_view section, std::string_view key) const noexcept;
};

bool LoadConfig(std::string_view path, ConfigDocument& output) noexcept;
bool ParseConfig(std::string_view text, ConfigDocument& output) noexcept;

// Raw value, or nullptr. The caller decides what an absent key means.
const char* ReadIniString(const ConfigDocument& config, const char* section, const char* key) noexcept;
const char* ReadIniString(const ConfigDocument& config, const char* key) noexcept;

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
