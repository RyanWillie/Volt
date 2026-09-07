#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/circuit/updates.hpp>
#include <volt/electrical/dc_request.hpp>
#include <volt/electrical/passive_model.hpp>
#include <volt/io/electrical/dc_request_io.hpp>
#include <volt/io/parts/footprint_asset.hpp>
#include <volt/io/parts/part_library_bundle.hpp>
#include <volt/io/project_bundle_writer.hpp>

namespace volt::test::electrical_compilation {

class Assets final : public PartAssetResolver {
  public:
    void add(const PartAssetReference &reference, std::string bytes) {
        assets_.emplace(std::pair{reference.kind(), reference.key()}, std::move(bytes));
    }

    [[nodiscard]] std::optional<std::string>
    resolve(const PartAssetReference &reference) const override {
        const auto found = assets_.find(std::pair{reference.kind(), reference.key()});
        return found == assets_.end() ? std::nullopt : std::optional{found->second};
    }

  private:
    std::map<std::pair<PartAssetKind, std::string>, std::string> assets_;
};

struct PartVariant {
    ComponentDefId component;
    PartKey part;
};

struct Fixture {
    std::unique_ptr<Circuit> circuit;
    io::PartLibraryBundle library;
    PartVariant resistor;
    PartVariant zero_resistor;
    PartVariant composite;
    PartVariant capacitor;
    PartVariant inductor;
    PartVariant absent;
    PartVariant optional_resistor;
    PartVariant must_not_connect_resistor;
    NetId supply;
    NetId midpoint;
    NetId reference;
    ComponentId upper_resistor;
    ComponentId lower_resistor;
};

[[nodiscard]] inline Fixture make_fixture() {
    auto circuit = std::make_unique<Circuit>();
    const auto required_spec = ComponentSpec{
        .name = "S2 required two-terminal component",
        .pins = {PinSpec{.name = "A", .number = "1"}, PinSpec{.name = "B", .number = "2"}},
        .contract = ComponentContractSpec{.key = ComponentKey{"test.compile/required@1"},
                                          .pin_keys = {PinKey{"A"}, PinKey{"B"}}}};
    const auto optional_spec = ComponentSpec{
        .name = "S2 optional terminal component",
        .pins = {PinSpec{.name = "A", .number = "1"},
                 PinSpec{
                     .name = "B", .number = "2", .requirement = ConnectionRequirement::Optional}},
        .contract = ComponentContractSpec{.key = ComponentKey{"test.compile/optional@1"},
                                          .pin_keys = {PinKey{"A"}, PinKey{"B"}}}};
    const auto must_not_connect_spec = ComponentSpec{
        .name = "S2 must-not-connect terminal component",
        .pins = {PinSpec{.name = "A", .number = "1"},
                 PinSpec{.name = "B",
                         .number = "2",
                         .requirement = ConnectionRequirement::MustNotConnect}},
        .contract = ComponentContractSpec{.key = ComponentKey{"test.compile/no-connect@1"},
                                          .pin_keys = {PinKey{"A"}, PinKey{"B"}}}};
    const auto required_definition = circuit->define_component(required_spec);
    const auto optional_definition = circuit->define_component(optional_spec);
    const auto must_not_connect_definition = circuit->define_component(must_not_connect_spec);
    const auto &required_component = circuit->get(required_definition);
    const auto &optional_component = circuit->get(optional_definition);
    const auto &must_not_connect_component = circuit->get(must_not_connect_definition);

    const auto resistor_model = [](const ComponentDefinition &component, double resistance) {
        auto builder = PartElectricalModelBuilder{component};
        const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
        const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
        builder.add<ResistanceElement>(
            ModelElementKey{"body"}, a, b,
            ModelParameter{Quantity{UnitDimension::Resistance, resistance},
                           resistance == 0.0 ? std::optional{Tolerance::percent(0.0)}
                                             : std::nullopt});
        return builder.build();
    };
    const auto ideal_model = [](const ComponentDefinition &component, bool capacitor) {
        auto builder = PartElectricalModelBuilder{component};
        const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
        const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
        if (capacitor) {
            builder.add<CapacitanceElement>(
                ModelElementKey{"storage"}, a, b,
                ModelParameter{Quantity{UnitDimension::Capacitance, 1.0e-6}});
        } else {
            builder.add<InductanceElement>(
                ModelElementKey{"storage"}, a, b,
                ModelParameter{Quantity{UnitDimension::Inductance, 1.0e-3}});
        }
        return builder.build();
    };
    const auto composite_model = [&] {
        auto builder = PartElectricalModelBuilder{required_component};
        const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
        const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
        const auto after_esr = builder.internal_node(ModelInternalNodeKey{"after_esr"});
        const auto after_esl = builder.internal_node(ModelInternalNodeKey{"after_esl"});
        builder.add<ResistanceElement>(ModelElementKey{"esr"}, a, after_esr,
                                       ModelParameter{Quantity{UnitDimension::Resistance, 0.08}});
        builder.add<InductanceElement>(ModelElementKey{"esl"}, after_esr, after_esl,
                                       ModelParameter{Quantity{UnitDimension::Inductance, 1.0e-9}});
        builder.add<CapacitanceElement>(
            ModelElementKey{"storage"}, after_esl, b,
            ModelParameter{Quantity{UnitDimension::Capacitance, 10.0e-6}});
        return builder.build();
    }();

    const auto footprint = io::write_footprint_asset(FootprintDefinition{
        FootprintRef{"test.compile", "two-terminal"},
        {FootprintPad::surface_mount("1", FootprintPadShape::Rectangle, {-0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd()),
         FootprintPad::surface_mount("2", FootprintPadShape::Rectangle, {0.5, 0.0}, {0.5, 0.5},
                                     FootprintLayerSet::front_smd())}});
    const auto make_part = [&](const ComponentDefinition &component, std::string key,
                               std::optional<PartElectricalModel> model) {
        return PartDefinition{
            component,
            PartIdentity{"test.compile", key, "1"},
            ElectricalRecordSet{2},
            {PinPackageTerminalMapping{PinKey{"A"}, {PackageTerminalKey{"1"}}},
             PinPackageTerminalMapping{PinKey{"B"}, {PackageTerminalKey{"2"}}}},
            {},
            PartProvenance{"", "volt.tests", "S2 electrical compilation fixture"},
            {},
            OrderablePart{
                ManufacturerPart{"Test", key},
                PackageRef{"TEST-2"},
                HashedFootprintReference{FootprintRef{"test.compile", "two-terminal"},
                                         sha256_content_hash(footprint)},
                {PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5},
                 PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
                {PackageTerminalPadMapping{PackageTerminalKey{"1"}, {FootprintPadKey{"1"}}},
                 PackageTerminalPadMapping{PackageTerminalKey{"2"}, {FootprintPadKey{"2"}}}}},
            std::move(model)};
    };
    const auto resistor =
        make_part(required_component, "resistor", resistor_model(required_component, 1000.0));
    const auto zero_resistor =
        make_part(required_component, "zero-resistor", resistor_model(required_component, 0.0));
    const auto composite = make_part(required_component, "composite-capacitor", composite_model);
    const auto capacitor =
        make_part(required_component, "ideal-capacitor", ideal_model(required_component, true));
    const auto inductor =
        make_part(required_component, "ideal-inductor", ideal_model(required_component, false));
    const auto absent = make_part(required_component, "model-absent", std::nullopt);
    const auto optional_resistor = make_part(optional_component, "optional-resistor",
                                             resistor_model(optional_component, 1000.0));
    const auto must_not_connect_resistor =
        make_part(must_not_connect_component, "must-not-connect-resistor",
                  resistor_model(must_not_connect_component, 1000.0));

    auto assets = Assets{};
    for (const auto &part : {std::cref(resistor), std::cref(zero_resistor), std::cref(composite),
                             std::cref(capacitor), std::cref(inductor), std::cref(absent),
                             std::cref(optional_resistor), std::cref(must_not_connect_resistor)}) {
        for (const auto &reference : part_asset_references(part.get())) {
            assets.add(reference, footprint);
        }
    }
    auto library_builder = PartLibraryBuilder{
        PartLibraryIdentity{"test.compile", "2026", PartLibrarySchemaVersion::V1}};
    library_builder.add_component(required_spec)
        .add_component(optional_spec)
        .add_component(must_not_connect_spec);
    for (const auto &part : {std::cref(resistor), std::cref(zero_resistor), std::cref(composite),
                             std::cref(capacitor), std::cref(inductor), std::cref(absent),
                             std::cref(optional_resistor), std::cref(must_not_connect_resistor)}) {
        library_builder.add_part(part.get());
    }
    const auto part_keys = std::vector{PartKey{"resistor"},
                                       PartKey{"zero-resistor"},
                                       PartKey{"composite-capacitor"},
                                       PartKey{"ideal-capacitor"},
                                       PartKey{"ideal-inductor"},
                                       PartKey{"model-absent"},
                                       PartKey{"optional-resistor"},
                                       PartKey{"must-not-connect-resistor"}};
    auto library = io::PartLibraryBundle::build(library_builder, part_keys, assets);

    const auto supply = circuit->add_net(NetSpec{.name = NetName{"supply"}});
    const auto midpoint = circuit->add_net(NetSpec{.name = NetName{"midpoint"}});
    const auto reference = circuit->add_net(NetSpec{.name = NetName{"reference"}});
    const auto add_resistor = [&](std::string reference_designator, NetId from, NetId to) {
        const auto occurrence = circuit->instantiate_component(
            required_definition,
            ComponentInstanceSpec{.reference = ReferenceDesignator{reference_designator}});
        circuit->update(occurrence,
                        SelectLibraryPart{library, library.require(PartKey{"resistor"})});
        circuit->connect(from, queries::pin_by_number(*circuit, occurrence, "1").value());
        circuit->connect(to, queries::pin_by_number(*circuit, occurrence, "2").value());
        return occurrence;
    };
    const auto upper_resistor = add_resistor("R1", supply, midpoint);
    const auto lower_resistor = add_resistor("R2", midpoint, reference);

    return {std::move(circuit),
            std::move(library),
            {required_definition, PartKey{"resistor"}},
            {required_definition, PartKey{"zero-resistor"}},
            {required_definition, PartKey{"composite-capacitor"}},
            {required_definition, PartKey{"ideal-capacitor"}},
            {required_definition, PartKey{"ideal-inductor"}},
            {required_definition, PartKey{"model-absent"}},
            {optional_definition, PartKey{"optional-resistor"}},
            {must_not_connect_definition, PartKey{"must-not-connect-resistor"}},
            supply,
            midpoint,
            reference,
            upper_resistor,
            lower_resistor};
}

[[nodiscard]] inline DcInput input(const Fixture &fixture) {
    return io::prepare_dc_input(*fixture.circuit, fixture.library);
}

[[nodiscard]] inline DcRequest divider_request(const DcInput &input, NetId supply, NetId midpoint,
                                               NetId reference) {
    return DcRequest{DcRequestKey{"divider-operating-point"},
                     input,
                     input.net(reference),
                     {DcVoltageSource{DcSourceKey{"supply-5v"},
                                      DcNetPair{input.net(supply), input.net(reference)},
                                      Quantity{UnitDimension::Voltage, 5.0}}},
                     {DcVoltageProbe{DcProbeKey{"midpoint-voltage"},
                                     DcNetPair{input.net(midpoint), input.net(reference)}}}};
}

[[nodiscard]] inline DcRequest divider_request(const Fixture &fixture, const DcInput &input) {
    return divider_request(input, fixture.supply, fixture.midpoint, fixture.reference);
}

[[nodiscard]] inline io::ProjectBundlePublication source_free_project(const Fixture &fixture) {
    auto builder = io::ProjectBundleBuilder{
        io::ProjectIdentity{"S2 electrical compilation fixture", std::nullopt, std::nullopt},
        io::ProjectRunSummary{true, io::ProjectStatus::Clean, "default", {"design"}},
        io::LogicalInputName{"project.py"},
        {io::AuthoringInput{io::AuthoringInputKind::ProjectSource,
                            io::LogicalInputName{"project.py"}, "native fixture"}},
        io::ProjectReport{
            R"({"status":"clean","summary":{"errors":0,"warnings":0,"infos":0},"diagnostics":[],"expected":[],"unexpected":[],"missing_expected":[]})"},
        io::ProjectReport{R"({"summary":{"passed":0,"failed":0},"tests":[]})"}};
    builder.add_logical(io::DesignKey{"main"}, *fixture.circuit, fixture.library);
    return builder.build();
}

} // namespace volt::test::electrical_compilation
