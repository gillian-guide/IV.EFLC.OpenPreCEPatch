// SPDX-License-Identifier: GPL-3.0-or-later
//
// The install schedule, declared instead of numbered.
//
// The original loader runs a fixed linear sequence of arms. This tree's shipping
// sequence is the one frozen in tests/order_golden.txt; the table below is its
// executable copy, grouped by phase. A feature registers itself with a phase and
// its name (feature_registry.cpp), and BuildOrder turns the two into the run
// order: phase first, then this table's position, then the name. `After` edges
// are load-bearing dependencies (an arm must observe what another published or
// must run before another destroys the bytes it scans); they can only delay an
// arm relative to the table and are validated for phase inversions and cycles.
//
// Changing this table is a behaviour change: update tests/order_golden.txt in the
// same commit and attach the opcep_matrix write-set comparison to it.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common.hxx"

namespace opp
{
    // The pipeline stages. The registry currently holds `publish` and `install`
    // arms; `probe`, `config` and `post` are owned by dllmain's SetStuff, where
    // the original loader had them, and exist so a future arm can declare itself
    // instead of being called out of band.
    enum class Phase : uint8_t
    {
        probe = 0,
        config = 1,
        publish = 2,
        install = 3,
        post = 4,
    };

    const char* PhaseName(Phase phase) noexcept;

    // One arm as registered by a feature module. The dependency lists are copied
    // into the registry, so a caller may build the row from a temporary.
    struct FeatureRegistration
    {
        Phase phase{};
        std::string_view name{};
        void (*install)(PatchContext&){};
        std::vector<std::string_view> after{};
        std::vector<std::string_view> needs{};  // hard edges: absent target => skipped
    };

    // One arm in the computed order.
    struct Feature
    {
        Phase phase{};
        const char* name{};
        void (*install)(PatchContext&){};
    };

    struct OrderResult
    {
        std::vector<Feature> order;
        std::vector<std::string> diagnostics;  // stable "order: " prefixes
        bool valid{};
    };

    // The shipping order, one name per line in the effective run order. Used as
    // the intra-phase tie-break and by the golden test.
    std::span<const std::string_view> ShippingOrder() noexcept;

    // Deterministic: phase, then schedule position, then name. A cycle, a
    // duplicate name, an unknown name or a phase inversion makes the result
    // invalid and clears `order` (the caller installs nothing).
    OrderResult BuildOrder(std::span<const FeatureRegistration> features);
} // namespace opp
