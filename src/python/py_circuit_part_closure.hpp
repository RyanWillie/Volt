#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <volt/core/errors.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/schematic/schematic.hpp>

namespace volt::python {

/** Exact immutable library closure plus authoritative authored symbol inputs. */
class PyCircuitPartClosure {
  public:
    explicit PyCircuitPartClosure(std::shared_ptr<const volt::io::PartLibraryBundle> bundle)
        : bundle_{std::move(bundle)} {}

    [[nodiscard]] const volt::io::PartLibraryBundle &bundle() const noexcept { return *bundle_; }

    void replace_bundle(std::shared_ptr<const volt::io::PartLibraryBundle> bundle) {
        bundle_ = std::move(bundle);
    }

    void retain_symbol(const volt::SchematicSymbolReference &reference, const std::string &bytes) {
        const auto key = reference.name() + "@" + reference.variant();
        const auto [existing, inserted] = symbols_.emplace(key, bytes);
        if (!inserted && existing->second != bytes) {
            throw volt::KernelLogicError{
                volt::ErrorCode::CrossReferenceViolation,
                "Schematic symbol identity resolves to conflicting authoritative bytes"};
        }
    }

    [[nodiscard]] std::optional<std::string>
    symbol(const volt::SchematicSymbolReference &reference) const {
        const auto key = reference.name() + "@" + reference.variant();
        const auto match = symbols_.find(key);
        return match == symbols_.end() ? std::nullopt : std::optional<std::string>{match->second};
    }

  private:
    std::shared_ptr<const volt::io::PartLibraryBundle> bundle_;
    std::map<std::string, std::string> symbols_;
};

} // namespace volt::python
