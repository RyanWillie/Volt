// Verifies that each installed layer target can be linked by name from an install tree,
// including Volt::KiCadAdapter, which is not reachable through the Volt::Volt umbrella.
//
// This is a packaging test, not a behavior test. Each layer only needs one real symbol so
// that a target missing from the export set, or an archive missing from the install, fails
// the build rather than passing silently.

#include <iostream>
#include <string>
#include <utility>

#include <volt/adapters/kicad/loss_report.hpp>
#include <volt/authoring/component_library.hpp>
#include <volt/authoring/reference_designators.hpp>
#include <volt/circuit/circuit.hpp>
#include <volt/circuit/connectivity/nets.hpp>
#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/version.hpp>
#include <volt/io/logical/logical_circuit_writer.hpp>
#include <volt/io/pcb/compiled_board.hpp>
#include <volt/io/pcb/manufacturing_target.hpp>
#include <volt/library/part_library.hpp>
#include <volt/pcb/board.hpp>
#include <volt/schematic/schematic.hpp>

namespace {
class EmptyAssetResolver final : public volt::PartAssetResolver {
    std::optional<std::string> resolve(const volt::PartAssetReference &) const override {
        return std::nullopt;
    }
};
} // namespace

int main() {
    // Volt::Core
    const auto version = volt::version_string();

    // Volt::Circuit + Volt::Authoring
    auto circuit = volt::Circuit{};
    const auto resistor = volt::authoring::define_component(circuit, volt::authoring::resistor());
    const auto r1 = volt::authoring::instantiate(circuit, resistor, "R");
    const auto net =
        circuit.add_net(volt::NetSpec{.name = volt::NetName{"N1"}, .kind = volt::NetKind::Signal});
    const auto pin = volt::queries::pin_by_number(circuit, r1, "1");
    if (!pin.has_value() || !circuit.connect(net, pin.value())) {
        std::cerr << "expected resistor pin 1 to connect to N1\n";
        return 1;
    }

    // Volt::Library
    const auto library_digest =
        volt::PartLibraryBuilder{
            volt::PartLibraryIdentity{"packaging", "0.1.0", volt::PartLibrarySchemaVersion::V1}}
            .reference_digest();

    // Volt::PCB
    const auto board = volt::Board{circuit};

    // Volt::Schematic
    const auto schematic = volt::Schematic{circuit};

    // Volt::IO
    const auto serialized = volt::io::write_logical_circuit(circuit);
    const auto hash = volt::sha256_content_hash("synthetic installed consumer");
    const auto process = volt::ManufacturingProcess{};
    const auto target = volt::ManufacturingTarget{
        volt::CompiledBoardIdentity{board.name(), hash}, hash, volt::ManufacturingGoal::Fabrication,
        process,
        volt::ManufacturingProfile{volt::BoardCapabilityProfile::conservative_default(), hash,
                                   process}};
    if (volt::io::read_manufacturing_target(volt::io::write_manufacturing_target(target))
            .digest() != target.digest())
        return 1;

    const auto empty_circuit = volt::Circuit{};
    auto empty_board = volt::Board{empty_circuit};
    const auto capability = volt::BoardCapabilityProfile::conservative_default();
    empty_board.set_capability_profile(capability);
    const auto closure = volt::io::PartLibraryBundle::build(
        volt::PartLibraryBuilder{volt::PartLibraryIdentity{"packaging-empty", "0.1.0",
                                                           volt::PartLibrarySchemaVersion::V1}},
        {}, EmptyAssetResolver{});
    auto compilation = volt::io::compile_board(empty_circuit, empty_board, closure,
                                               volt::CompiledBoardCapabilities{capability});
    if (!compilation.has_artifact())
        return 1;
    const auto compiled = std::move(compilation).take_artifact();
    const auto supported =
        volt::ManufacturingProcess{.family = volt::ManufacturingFamily::RigidFr4,
                                   .material = volt::ManufacturingMaterial::Fr4Tg135,
                                   .copper_layers = 2U,
                                   .top_copper_weight_oz = 1.0,
                                   .bottom_copper_weight_oz = 1.0,
                                   .thickness_mm = 1.6,
                                   .finish = volt::ManufacturingFinish::LeadFreeHasl,
                                   .mask_sides = volt::ManufacturingMaskSides::Both,
                                   .mask_colour = volt::ManufacturingMaskColour::Green,
                                   .via_treatment = volt::ManufacturingViaTreatment::Exposed,
                                   .drilling = volt::ManufacturingDrilling::MechanicalThrough,
                                   .holes = volt::ManufacturingHoleTreatment::RoundOnly,
                                   .slots = volt::ManufacturingSlotTreatment::NoSlots,
                                   .assembly = volt::ManufacturingAssembly::SmtAndThroughHole,
                                   .dimension_x_mm = 30,
                                   .dimension_y_mm = 20};
    const auto compiled_target =
        volt::ManufacturingTarget{compiled.identity(),
                                  compiled.content_digest(),
                                  volt::ManufacturingGoal::FabricationAndAssembly,
                                  supported,
                                  volt::ManufacturingProfile{capability, hash, supported},
                                  volt::CplProjectionOptions{}};
    const auto coverage = volt::evaluate_manufacturing_coverage(compiled, compiled_target);
    if (coverage.ready() || volt::io::write_manufacturing_coverage(coverage).empty())
        return 1;

    if (!coverage.bom() || !coverage.cpl() || !coverage.ratings() ||
        !coverage.ratings()->subjects.empty() ||
        coverage.coverage()[static_cast<std::size_t>(volt::ManufacturingCheck::SelectedPartRatings)]
                .state != volt::ManufacturingState::NotEvaluated)
        return 1;

    // Volt::KiCadAdapter
    auto loss_report = volt::adapters::kicad::LossReport{};
    loss_report.add_warning(volt::adapters::kicad::LossKind::UnsupportedConstruct, "probe",
                            "packaging consumer");

    if (version.empty() || serialized.empty() || library_digest.value().empty() ||
        !loss_report.has_warnings()) {
        std::cerr << "expected non-empty version, digest, serialization, and loss report\n";
        return 1;
    }

    std::cout << "layers consumer: volt " << version << ", board '" << board.name().value() << "', "
              << schematic.all<volt::SheetId>().size() << " sheets, library digest "
              << library_digest.value() << ", " << serialized.size() << " serialized bytes\n";
    return 0;
}
