#pragma once
#include <cmath>

namespace volt::detail {
/** Shared native CPL normalization, including the established near-zero convention. */
inline double normalize_cpl_rotation(double degrees) {
    auto normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0)
        normalized += 360.0;
    if (std::abs(normalized - 360.0) < 1.0e-9 || std::abs(normalized) < 1.0e-9)
        return 0.0;
    return normalized;
}
} // namespace volt::detail
