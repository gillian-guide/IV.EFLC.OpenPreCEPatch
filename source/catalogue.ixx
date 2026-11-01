// zp.catalogue — the recovered model catalogue.
//
// The generated table is `src/catalogue_data.cpp` (original order); this unit owns
// the lookups the crash fixes use.

module;

#include "common.hxx"
#include "common.h"

// Defined in catalogue_data.cpp (the plain generated data TU); the header is
// included in the global fragment so the symbols keep global-module linkage.
#include "catalogue_data.h"

export module zp.catalogue;

export namespace zp
{
// The catalogue's entry count (38,455 on every build; the data is build-neutral).
[[nodiscard]] std::size_t ModelCatalogSize() noexcept;

// The name at `index`, or "**UNKNOWN**" when the index is out of range.
[[nodiscard]] std::string_view ModelCatalogName(std::size_t index) noexcept;

// The name whose GameHash equals `hash`, or "**UNKNOWN**".
[[nodiscard]] std::string_view ModelNameFromHash(std::uint32_t hash) noexcept;
}  // namespace zp

namespace zp
{
std::size_t ModelCatalogSize() noexcept
{
	return kModelNameCount;
}

std::string_view ModelCatalogName(std::size_t index) noexcept
{
	return index < kModelNameCount ? kModelNames[index] : "**UNKNOWN**";
}

std::string_view ModelNameFromHash(std::uint32_t hash) noexcept
{
	for (std::size_t i = 0; i < kModelNameCount; ++i)
	{
		if (GameHash(kModelNames[i]) == hash)
			return kModelNames[i];
	}
	return "**UNKNOWN**";
}
}  // namespace zp
