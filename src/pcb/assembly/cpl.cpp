#include <volt/pcb/assembly/cpl.hpp>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>
#include <volt/pcb/queries/board_queries.hpp>

#include <algorithm>
#include <cmath>
#include <string_view>
#include <utility>

namespace volt {
namespace {

struct ResolvedRotationOffset {
    FootprintRef footprint;
    double rotation_deg;
    bool ambiguous = false;
};

[[nodiscard]] bool same_rotation(double lhs, double rhs) noexcept {
    return std::abs(lhs - rhs) < 1.0e-9;
}

[[nodiscard]] double normalize_rotation(double degrees) {
    auto normalized = std::fmod(degrees, 360.0);
    if (normalized < 0.0) {
        normalized += 360.0;
    }
    if (same_rotation(normalized, 360.0) || same_rotation(normalized, 0.0)) {
        return 0.0;
    }
    return normalized;
}

[[nodiscard]] std::vector<ResolvedRotationOffset>
resolve_rotation_offsets(const CplProjectionOptions &options) {
    auto offsets = std::vector<ResolvedRotationOffset>{};
    for (const auto &offset : options.rotation_offsets) {
        const auto existing =
            std::find_if(offsets.begin(), offsets.end(), [&](const auto &candidate) {
                return candidate.footprint == offset.footprint();
            });
        if (existing == offsets.end()) {
            offsets.push_back(ResolvedRotationOffset{offset.footprint(), offset.rotation_deg()});
            continue;
        }
        if (!same_rotation(existing->rotation_deg, offset.rotation_deg())) {
            existing->ambiguous = true;
        }
    }
    return offsets;
}

[[nodiscard]] const ResolvedRotationOffset *
find_offset(const std::vector<ResolvedRotationOffset> &offsets, const FootprintRef &footprint) {
    const auto existing = std::find_if(offsets.begin(), offsets.end(), [&](const auto &candidate) {
        return candidate.footprint == footprint;
    });
    if (existing == offsets.end()) {
        return nullptr;
    }
    return &*existing;
}

[[nodiscard]] Diagnostic assembly_diagnostic(std::string_view code, std::string message,
                                             std::vector<EntityRef> entities) {
    return Diagnostic{Severity::Error, DiagnosticCode{std::string{code}},
                      DiagnosticCategory{diagnostic_categories::Assembly}, std::move(message),
                      std::move(entities)};
}

[[nodiscard]] std::vector<ComponentId> sorted_components(const Circuit &circuit) {
    auto components = std::vector<ComponentId>{};
    components.reserve(circuit.all<volt::ComponentId>().size());
    for (std::size_t index = 0; index < circuit.all<volt::ComponentId>().size(); ++index) {
        components.push_back(ComponentId{index});
    }
    std::sort(components.begin(), components.end(), [&](ComponentId lhs, ComponentId rhs) {
        return circuit.get(lhs).reference().value() < circuit.get(rhs).reference().value();
    });
    return components;
}

[[nodiscard]] bool is_populated(const Circuit &circuit, ComponentId component) {
    return !volt::queries::component_dnp(circuit, component).value_or(false);
}

void append_component_diagnostics(const Board &board, DiagnosticReport &report) {
    for (const auto component : sorted_components(board.circuit())) {
        if (!is_populated(board.circuit(), component)) {
            continue;
        }

        const auto &instance = board.circuit().get(component);
        const auto entities = std::vector{EntityRef::component(component),
                                          EntityRef::component_def(instance.definition())};
        const auto &selected_part =
            volt::queries::selected_physical_part(board.circuit(), component);
        if (!selected_part.has_value()) {
            report.add(assembly_diagnostic(
                assembly_diagnostic_codes::ComponentMissingSelectedPart,
                "Populated component requires a selected physical part for assembly handoff",
                entities));
            report.add(assembly_diagnostic(
                assembly_diagnostic_codes::PartIdentityMissing,
                "Populated component has no manufacturer part identity for assembly handoff",
                entities));
        }
        if (!queries::placement_for_component(board, component).has_value()) {
            report.add(assembly_diagnostic(
                assembly_diagnostic_codes::ComponentUnplaced,
                "Populated BOM component has no board placement for assembly handoff", entities));
        }
    }
}

void append_orientation_diagnostic(const ComponentPlacement &placement,
                                   ComponentPlacementId placement_id, DiagnosticReport &report) {
    report.add(assembly_diagnostic(assembly_diagnostic_codes::OrientationAmbiguous,
                                   "Footprint has conflicting assembly rotation-offset data",
                                   std::vector{EntityRef::component(placement.component()),
                                               EntityRef::component_placement(placement_id)}));
}

[[nodiscard]] std::optional<CplPartIdentity> cpl_part_identity(const PhysicalPart &part) {
    return CplPartIdentity{part.manufacturer_part().manufacturer(),
                           part.manufacturer_part().part_number(), part.package().value()};
}

} // namespace

CplRotationOffset::CplRotationOffset(FootprintRef footprint, double rotation_deg)
    : footprint_{std::move(footprint)}, rotation_deg_{rotation_deg} {
    if (!std::isfinite(rotation_deg_)) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "CPL rotation offset must be finite"};
    }
}

CplPartIdentity::CplPartIdentity(std::string manufacturer, std::string mpn, std::string package)
    : manufacturer_{std::move(manufacturer)}, mpn_{std::move(mpn)}, package_{std::move(package)} {}

CplRow::CplRow(ComponentPlacementId placement, ComponentId component, std::string reference,
               std::optional<FootprintRef> footprint, BoardSide side, BoardPoint position,
               double authored_rotation_deg, double rotation_offset_deg, double rotation_deg,
               std::optional<CplPartIdentity> part_identity)
    : placement_{placement}, component_{component}, reference_{std::move(reference)},
      footprint_{std::move(footprint)}, side_{side}, position_{position},
      authored_rotation_deg_{authored_rotation_deg}, rotation_offset_deg_{rotation_offset_deg},
      rotation_deg_{rotation_deg}, part_identity_{std::move(part_identity)} {}

Cpl::Cpl(std::vector<CplRow> rows, DiagnosticReport diagnostics)
    : rows_{std::move(rows)}, diagnostics_{std::move(diagnostics)} {}

[[nodiscard]] Cpl project_cpl(const Board &board, const FootprintLibrary &footprints) {
    return project_cpl(board, footprints, CplProjectionOptions{});
}

[[nodiscard]] Cpl project_cpl(const Board &board, const FootprintLibrary &footprints,
                              const CplProjectionOptions &options) {
    static_cast<void>(footprints);
    auto diagnostics = DiagnosticReport{};
    append_component_diagnostics(board, diagnostics);

    const auto offsets = resolve_rotation_offsets(options);
    auto rows = std::vector<CplRow>{};
    rows.reserve(board.all<volt::ComponentPlacementId>().size());

    for (std::size_t index = 0; index < board.all<volt::ComponentPlacementId>().size(); ++index) {
        const auto placement_id = ComponentPlacementId{index};
        const auto &placement = board.get(placement_id);
        if (!is_populated(board.circuit(), placement.component())) {
            continue;
        }

        auto footprint = std::optional<FootprintRef>{};
        auto part_identity = std::optional<CplPartIdentity>{};
        auto rotation_offset_deg = 0.0;
        const auto &selected_part =
            volt::queries::selected_physical_part(board.circuit(), placement.component());
        if (selected_part.has_value()) {
            footprint = selected_part->footprint();
            part_identity = cpl_part_identity(selected_part.value());
            if (const auto *offset = find_offset(offsets, selected_part->footprint());
                offset != nullptr) {
                if (offset->ambiguous) {
                    append_orientation_diagnostic(placement, placement_id, diagnostics);
                } else {
                    rotation_offset_deg = offset->rotation_deg;
                }
            }
        }

        const auto authored_rotation_deg = normalize_rotation(placement.rotation().degrees());
        rows.emplace_back(placement_id, placement.component(),
                          board.circuit().get(placement.component()).reference().value(),
                          std::move(footprint), placement.side(), placement.position(),
                          authored_rotation_deg, rotation_offset_deg,
                          normalize_rotation(authored_rotation_deg + rotation_offset_deg),
                          std::move(part_identity));
    }

    std::sort(rows.begin(), rows.end(), [](const CplRow &lhs, const CplRow &rhs) {
        if (lhs.reference() != rhs.reference()) {
            return lhs.reference() < rhs.reference();
        }
        return lhs.placement().index() < rhs.placement().index();
    });

    return Cpl{std::move(rows), std::move(diagnostics)};
}

} // namespace volt
