#pragma once
#include <stdexcept>
#include <string>
#include <string_view>
#include <volt/pcb/compiled/manufacturing_target.hpp>

namespace volt::io {
/** Closed failure family at the versioned target document boundary. */
enum class ManufacturingTargetReadErrorCode { Malformed, UnsupportedVersion };

/** Typed rejection; no partially decoded target is published. */
class ManufacturingTargetReadError final : public std::runtime_error {
  public:
    /** Preserve exact error family and explanation. */
    ManufacturingTargetReadError(ManufacturingTargetReadErrorCode code, std::string message)
        : std::runtime_error{message}, code_{code} {}

    /** Return the stable malformed/unsupported-version distinction. */
    [[nodiscard]] ManufacturingTargetReadErrorCode code() const noexcept { return code_; }

  private:
    ManufacturingTargetReadErrorCode code_;
};

/** Write the deterministic standalone V1 target document. */
[[nodiscard]] std::string write_manufacturing_target(const ManufacturingTarget &target);
/** Decode strict V1 typed target data without filesystem/source access. */
[[nodiscard]] ManufacturingTarget read_manufacturing_target(std::string_view text);
/** Write the complete deterministic native coverage payload; reports are never evaluator inputs. */
[[nodiscard]] std::string write_manufacturing_coverage(const ManufacturingCoverage &result);
} // namespace volt::io
