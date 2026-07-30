#pragma once

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/updates.hpp>
#include <volt/core/errors.hpp>
#include <volt/library/part_library.hpp>
#include <volt/pcb/resolution/board_resolution.hpp>

namespace volt::test {
namespace detail {

class ResolvedBoardTestPartResolver final : public ExactPartResolver {
  public:
    explicit ResolvedBoardTestPartResolver(PartDefinition part)
        : identity_{"test.resolved", "1", PartLibrarySchemaVersion::V1},
          reference_digest_{sha256_content_hash("test.resolved.library")}, part_{std::move(part)},
          reference_{identity_.namespace_name(), identity_.version(),
                     PartKey{part_.identity().name()}, reference_digest_,
                     part_.content_identity()} {}

    [[nodiscard]] const PartLibraryIdentity &identity() const & noexcept override {
        return identity_;
    }

    [[nodiscard]] const ContentHash &reference_digest() const & noexcept override {
        return reference_digest_;
    }

    [[nodiscard]] const PartDefinition &resolve(const LibraryPartRef &reference) const & override {
        if (reference != reference_) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Resolved Board test part reference is foreign"};
        }
        return part_;
    }

    [[nodiscard]] const PartDefinition &part() const noexcept { return part_; }

    [[nodiscard]] const LibraryPartRef &reference() const noexcept { return reference_; }

  private:
    PartLibraryIdentity identity_;
    ContentHash reference_digest_;
    PartDefinition part_;
    LibraryPartRef reference_;
};

[[nodiscard]] inline PartDefinition test_part_definition(const Circuit &circuit,
                                                         ComponentId component,
                                                         const PhysicalPart &physical_part) {
    const auto &instance = circuit.get(component);
    const auto &definition = circuit.get(instance.definition());
    const auto &pin_keys = definition.contract().pin_keys();
    auto pin_terminal_mappings = std::vector<PinPackageTerminalMapping>{};
    auto terminal_pad_mappings = std::vector<PackageTerminalPadMapping>{};
    auto footprint_pads = std::vector<PartFootprintPad>{};

    for (std::size_t pin_index = 0; pin_index < definition.pins().size(); ++pin_index) {
        const auto pin = definition.pins()[pin_index];
        auto pad_labels = std::vector<std::string>{};
        for (const auto &mapping : physical_part.pin_pad_mappings()) {
            if (mapping.pin() == pin) {
                pad_labels.push_back(mapping.pad());
            }
        }
        if (pad_labels.empty()) {
            pad_labels.push_back("__unmapped-" + std::to_string(pin_index));
        }

        auto terminals = std::vector<PackageTerminalKey>{};
        for (std::size_t pad_index = 0; pad_index < pad_labels.size(); ++pad_index) {
            auto terminal = PackageTerminalKey{"terminal-" + std::to_string(pin_index) + "-" +
                                               std::to_string(pad_index)};
            terminal_pad_mappings.emplace_back(terminal,
                                               std::vector{FootprintPadKey{pad_labels[pad_index]}});
            terminals.push_back(std::move(terminal));
            footprint_pads.emplace_back(pad_labels[pad_index],
                                        static_cast<double>(footprint_pads.size()), 0.0, 1.0, 1.0);
        }
        pin_terminal_mappings.emplace_back(pin_keys[pin_index], std::move(terminals));
    }

    return PartDefinition{
        definition,
        PartIdentity{"test.resolved", "part-" + std::to_string(component.index()), "1"},
        ElectricalRecordSet{definition.pins().size()},
        std::move(pin_terminal_mappings),
        {},
        PartProvenance{},
        {},
        OrderablePart{
            physical_part.manufacturer_part(),
            physical_part.package(),
            HashedFootprintReference{physical_part.footprint(),
                                     sha256_content_hash("test.resolved.footprint." +
                                                         physical_part.footprint().library() + "." +
                                                         physical_part.footprint().name())},
            std::move(footprint_pads),
            std::move(terminal_pad_mappings),
            physical_part.approved_alternate_mpns(),
        },
    };
}

} // namespace detail

/** Explicit test owner for exact physical parts consumed through ResolvedBoardView. */
class ResolvedBoardTestParts {
  public:
    void set(Circuit &circuit, ComponentId component, PhysicalPart part) {
        auto resolver = detail::ResolvedBoardTestPartResolver{
            detail::test_part_definition(circuit, component, part)};
        auto reference = resolver.reference();
        circuit.update(component, SelectLibraryPart{resolver, reference});

        const auto existing = std::ranges::find(parts_, component, &ResolvedBoardPart::component);
        auto resolved = ResolvedBoardPart{component, std::move(reference), std::move(part)};
        if (existing == parts_.end()) {
            parts_.push_back(std::move(resolved));
            std::ranges::sort(parts_, [](const auto &lhs, const auto &rhs) {
                return lhs.component().index() < rhs.component().index();
            });
        } else {
            *existing = std::move(resolved);
        }
    }

    [[nodiscard]] ResolvedBoardView view(const Board &board,
                                         const FootprintLibrary &footprints) const {
        return ResolvedBoardView::test_only(board, footprints, parts_);
    }

    [[nodiscard]] ResolvedBoardView view_builtin(const Board &board) const {
        return ResolvedBoardView::test_only(board, builtin_footprints_, parts_);
    }

    [[nodiscard]] std::span<const ResolvedBoardPart> parts() const noexcept { return parts_; }

  private:
    FootprintLibrary builtin_footprints_ = builtin_footprint_library();
    std::vector<ResolvedBoardPart> parts_;
};

} // namespace volt::test
