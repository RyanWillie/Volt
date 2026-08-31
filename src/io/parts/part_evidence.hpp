#pragma once

#include <algorithm>
#include <vector>

#include <volt/library/part_library.hpp>

namespace volt::io::detail {

// Model evidence comes from Part asset references; canonical V/I records add their own evidence.
[[nodiscard]] inline std::vector<ContentHash> part_evidence_digests(const PartDefinition &part) {
    auto result = std::vector<ContentHash>{};
    for (const auto &asset : part_asset_references(part)) {
        if (asset.kind() == PartAssetKind::Evidence) {
            result.push_back(asset.digest());
        }
    }
    for (const auto &record : part.electrical_records().records()) {
        result.insert(result.end(), record.evidence().begin(), record.evidence().end());
    }
    std::ranges::sort(result, {}, &ContentHash::value);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

} // namespace volt::io::detail
