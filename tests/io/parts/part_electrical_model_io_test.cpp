#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/circuit/circuit.hpp>
#include <volt/electrical/passive_model.hpp>
#include <volt/io/parts/part_definition_reader.hpp>
#include <volt/io/parts/part_definition_writer.hpp>

namespace {

using Json = nlohmann::json;

volt::ComponentDefinition passive_component() {
    auto circuit = volt::Circuit{};
    const auto definition = circuit.define_component(volt::ComponentSpec{
        .name = "Passive",
        .pins = {volt::PinSpec{.name = "A", .number = "1"},
                 volt::PinSpec{.name = "B", .number = "2"}},
        .contract =
            volt::ComponentContractSpec{
                .key = volt::ComponentKey{"test.component/passive@1"},
                .pin_keys = {volt::PinKey{"A"}, volt::PinKey{"B"}},
            },
    });
    return circuit.get(definition);
}

volt::PartDefinition exact_part(const volt::ComponentDefinition &component,
                                std::optional<volt::PartElectricalModel> model = std::nullopt) {
    return volt::PartDefinition{
        component,
        volt::PartIdentity{"test.passives", "demonstration", "1"},
        volt::ElectricalRecordSet{2},
        {volt::PinPackageTerminalMapping{volt::PinKey{"A"}, {volt::PackageTerminalKey{"1"}}},
         volt::PinPackageTerminalMapping{volt::PinKey{"B"}, {volt::PackageTerminalKey{"2"}}}},
        {},
        volt::PartProvenance{},
        {},
        volt::OrderablePart{
            volt::ManufacturerPart{"Test", "demonstration"},
            volt::PackageRef{"0603"},
            volt::HashedFootprintReference{volt::FootprintRef{"Test", "0603"},
                                           volt::sha256_content_hash("test footprint")},
            {volt::PartFootprintPad{"1", -0.5, 0.0, 0.5, 0.5},
             volt::PartFootprintPad{"2", 0.5, 0.0, 0.5, 0.5}},
            {volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"1"},
                                             {volt::FootprintPadKey{"1"}}},
             volt::PackageTerminalPadMapping{volt::PackageTerminalKey{"2"},
                                             {volt::FootprintPadKey{"2"}}}},
        },
        std::move(model),
    };
}

template <typename Element>
volt::PartElectricalModel single_element(const volt::ComponentDefinition &component,
                                         volt::ModelParameter parameter) {
    auto builder = volt::PartElectricalModelBuilder{component};
    const auto a = builder.terminal(volt::ModelTerminalKey{"a"}, volt::PinKey{"A"});
    const auto b = builder.terminal(volt::ModelTerminalKey{"b"}, volt::PinKey{"B"});
    builder.add<Element>(volt::ModelElementKey{"body"}, a, b, std::move(parameter));
    return builder.build();
}

volt::PartElectricalModel composite_model(const volt::ComponentDefinition &component) {
    auto builder = volt::PartElectricalModelBuilder{component};
    const auto a = builder.terminal(volt::ModelTerminalKey{"a"}, volt::PinKey{"A"});
    const auto b = builder.terminal(volt::ModelTerminalKey{"b"}, volt::PinKey{"B"});
    // The two typed key namespaces may intentionally use the same spelling.
    const auto x = builder.internal_node(volt::ModelInternalNodeKey{"a"});
    const auto y = builder.internal_node(volt::ModelInternalNodeKey{"y"});
    builder.add<volt::ResistanceElement>(
        volt::ModelElementKey{"esr"}, a, x,
        volt::ModelParameter{
            volt::Quantity{volt::UnitDimension::Resistance, 10.0},
            volt::Tolerance::percent(0.1, 0.2),
            {volt::sha256_content_hash("evidence-a"), volt::sha256_content_hash("evidence-b")}});
    builder.add<volt::InductanceElement>(
        volt::ModelElementKey{"esl"}, x, y,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Inductance, 1.0e-9}});
    builder.add<volt::CapacitanceElement>(
        volt::ModelElementKey{"storage"}, y, b,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Capacitance, 10.0e-6},
                             volt::Tolerance::percent(0.0)});
    return builder.build();
}

Json composite_document(const volt::ComponentDefinition &component) {
    return Json::parse(
        volt::io::write_part_definition(exact_part(component, composite_model(component))));
}

void check_structural_rejection(const Json &document, const volt::ComponentDefinition &component) {
    try {
        static_cast<void>(volt::io::read_part_definition_text(document.dump(), component));
        FAIL("Malformed model was accepted");
    } catch (const std::logic_error &error) {
        // A changed digest alone must not make malformed-model tests pass.
        CHECK(std::string{error.what()} != "Part definition content identity mismatch");
    }
}

} // namespace

TEST_CASE("Part electrical model transport preserves ideal R C L and exact finite SI values") {
    const auto component = passive_component();
    auto models = std::vector<volt::PartElectricalModel>{};
    for (const auto value :
         {0.0, std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::max()}) {
        models.push_back(single_element<volt::ResistanceElement>(
            component,
            volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, value}}));
    }
    models.push_back(single_element<volt::CapacitanceElement>(
        component,
        volt::ModelParameter{volt::Quantity{volt::UnitDimension::Capacitance, 10.0e-6}}));
    models.push_back(single_element<volt::InductanceElement>(
        component, volt::ModelParameter{volt::Quantity{volt::UnitDimension::Inductance, 1.0e-9}}));
    for (const auto &model : models) {
        const auto part = exact_part(component, model);
        const auto bytes = volt::io::write_part_definition(part);
        const auto loaded = volt::io::read_part_definition_text(bytes, component);
        REQUIRE(loaded.electrical_model());
        CHECK(loaded.content_identity() == part.content_identity());
        CHECK(volt::io::write_part_definition(loaded) == bytes);
        CHECK(volt::io::part_definition_content_hash(loaded) == volt::sha256_content_hash(bytes));
        std::visit(
            [&](const auto &element) {
                using Element = std::decay_t<decltype(element)>;
                const auto &restored = std::get<Element>(loaded.electrical_model()->elements()[0]);
                CHECK(restored.parameter().nominal() == element.parameter().nominal());
            },
            model.elements()[0]);
    }
}

TEST_CASE("Part composite model transport preserves graph orientation tolerance and evidence") {
    const auto component = passive_component();
    const auto part = exact_part(component, composite_model(component));
    const auto bytes = volt::io::write_part_definition(part);
    const auto loaded = volt::io::read_part_definition_text(bytes, component);
    REQUIRE(loaded.electrical_model());
    const auto &model = *loaded.electrical_model();
    CHECK(model.implemented_component() == component.content_identity());
    REQUIRE(model.terminals().size() == 2U);
    REQUIRE(model.internal_nodes().size() == 2U);
    REQUIRE(model.elements().size() == 3U);
    const auto &esl = std::get<volt::InductanceElement>(model.elements()[0]);
    const auto &esr = std::get<volt::ResistanceElement>(model.elements()[1]);
    const auto &storage = std::get<volt::CapacitanceElement>(model.elements()[2]);
    CHECK(esr.from() == volt::ModelEndpoint{volt::ModelTerminalKey{"a"}});
    CHECK(esr.to() == volt::ModelEndpoint{volt::ModelInternalNodeKey{"a"}});
    CHECK(esl.from() == esr.to());
    CHECK(esl.to() == storage.from());
    CHECK(storage.to() == volt::ModelEndpoint{volt::ModelTerminalKey{"b"}});
    REQUIRE(esr.parameter().tolerance());
    CHECK(esr.parameter().tolerance()->mode() == volt::ToleranceMode::Absolute);
    CHECK(esr.parameter().tolerance()->minus().value() == 1.0);
    CHECK(esr.parameter().tolerance()->plus().value() == 2.0);
    CHECK(esr.parameter().evidence().size() == 2U);
    CHECK_FALSE(esl.parameter().tolerance());
    REQUIRE(storage.parameter().tolerance());
    CHECK(storage.parameter().tolerance()->minus().value() == 0.0);
    CHECK(storage.parameter().tolerance()->plus().value() == 0.0);
    CHECK(loaded.content_identity() == part.content_identity());
    CHECK(volt::io::write_part_definition(loaded) == bytes);
}

TEST_CASE("Part model bytes canonicalize declaration order equivalent tolerances and evidence") {
    const auto component = passive_component();
    const auto model = composite_model(component);
    const auto original = exact_part(component, model);
    auto terminals = model.terminals();
    auto nodes = model.internal_nodes();
    auto elements = model.elements();
    const auto resistance = std::get<volt::ResistanceElement>(elements[1]);
    auto evidence = resistance.parameter().evidence();
    std::ranges::reverse(evidence);
    evidence.push_back(evidence.front());
    elements[1] = volt::ResistanceElement{
        resistance.key(), resistance.from(), resistance.to(),
        volt::ModelParameter{
            volt::Quantity{volt::UnitDimension::Resistance, 10.0},
            volt::Tolerance::absolute(volt::Quantity{volt::UnitDimension::Resistance, 1.0},
                                      volt::Quantity{volt::UnitDimension::Resistance, 2.0}),
            evidence}};
    std::ranges::reverse(terminals);
    std::ranges::reverse(nodes);
    std::ranges::reverse(elements);
    const auto equivalent =
        exact_part(component, volt::PartElectricalModel{component, terminals, nodes, elements});
    CHECK(equivalent.content_identity() == original.content_identity());
    CHECK(volt::io::write_part_definition(equivalent) == volt::io::write_part_definition(original));

    auto document = Json::parse(volt::io::write_part_definition(original));
    for (const auto *name : {"terminals", "internal_nodes", "elements"}) {
        auto &collection = document["electrical_model"][name];
        std::reverse(collection.begin(), collection.end());
    }
    auto &references = document["electrical_model"]["elements"][1]["parameter"]["evidence"];
    std::reverse(references.begin(), references.end());
    references.push_back(references.front());
    const auto loaded = volt::io::read_part_definition_text(document.dump(), component);
    CHECK(volt::io::write_part_definition(loaded) == volt::io::write_part_definition(original));
}

TEST_CASE("Part model transport distinguishes absent tolerance and normalizes signed zero") {
    const auto component = passive_component();
    const auto unspecified =
        exact_part(component, single_element<volt::ResistanceElement>(
                                  component, volt::ModelParameter{volt::Quantity{
                                                 volt::UnitDimension::Resistance, -0.0}}));
    const auto zero = exact_part(
        component,
        single_element<volt::ResistanceElement>(
            component, volt::ModelParameter{volt::Quantity{volt::UnitDimension::Resistance, 0.0},
                                            volt::Tolerance::percent(-0.0)}));
    CHECK(unspecified.content_identity() != zero.content_identity());
    CHECK(volt::io::write_part_definition(unspecified) != volt::io::write_part_definition(zero));
    for (const auto &part : {unspecified, zero}) {
        auto document = Json::parse(volt::io::write_part_definition(part));
        auto &wire_parameter = document["electrical_model"]["elements"][0]["parameter"];
        wire_parameter["nominal"]["value"] = -0.0;
        if (!wire_parameter["tolerance"].is_null()) {
            wire_parameter["tolerance"]["minus"]["value"] = -0.0;
            wire_parameter["tolerance"]["plus"]["value"] = -0.0;
        }
        const auto loaded = volt::io::read_part_definition_text(document.dump(), component);
        const auto &parameter =
            std::get<volt::ResistanceElement>(loaded.electrical_model()->elements()[0]).parameter();
        CHECK_FALSE(std::signbit(parameter.nominal().value()));
        if (parameter.tolerance()) {
            CHECK_FALSE(std::signbit(parameter.tolerance()->minus().value()));
            CHECK_FALSE(std::signbit(parameter.tolerance()->plus().value()));
        }
        CHECK(volt::io::write_part_definition(loaded) == volt::io::write_part_definition(part));
    }
}

TEST_CASE("Part model reader canonicalizes equivalent exact SI numeric spellings") {
    const auto component = passive_component();
    const auto part =
        exact_part(component, single_element<volt::ResistanceElement>(
                                  component, volt::ModelParameter{volt::Quantity{
                                                 volt::UnitDimension::Resistance, 10.0}}));
    const auto canonical = volt::io::write_part_definition(part);
    for (const auto *spelling : {"10", "10.0", "1e1", "100e-1"}) {
        auto bytes = canonical;
        const auto position = bytes.find("\"value\": 10");
        REQUIRE(position != std::string::npos);
        bytes.replace(position + std::string{"\"value\": "}.size(), 2U, spelling);
        const auto loaded = volt::io::read_part_definition_text(bytes, component);
        CHECK(loaded.content_identity() == part.content_identity());
        CHECK(volt::io::write_part_definition(loaded) == canonical);
    }
}

TEST_CASE("Part electrical model wire objects reject every missing and unknown field") {
    const auto component = passive_component();
    const auto original = composite_document(component);
    for (const auto *path : {"/electrical_model", "/electrical_model/terminals/0",
                             "/electrical_model/internal_nodes/0", "/electrical_model/elements/1",
                             "/electrical_model/elements/1/from", "/electrical_model/elements/1/to",
                             "/electrical_model/elements/1/parameter",
                             "/electrical_model/elements/1/parameter/nominal",
                             "/electrical_model/elements/1/parameter/tolerance",
                             "/electrical_model/elements/1/parameter/tolerance/minus",
                             "/electrical_model/elements/1/parameter/tolerance/plus"}) {
        const auto pointer = Json::json_pointer{path};
        auto unknown = original;
        unknown[pointer]["unsupported"] = true;
        CAPTURE(path);
        check_structural_rejection(unknown, component);
        for (const auto &[key, value] : original[pointer].items()) {
            static_cast<void>(value);
            CAPTURE(key);
            auto missing = original;
            missing[pointer].erase(key);
            check_structural_rejection(missing, component);
        }
    }
}

TEST_CASE("Part electrical model rejects invalid types variants and component relationships") {
    const auto component = passive_component();
    auto document = composite_document(component);
    auto &model = document["electrical_model"];
    SECTION("mandatory optional state") { document.erase("electrical_model"); }
    SECTION("invalid optional state") { model = false; }
    SECTION("wrong component digest") {
        model["implements"] = volt::sha256_content_hash("other").value();
    }
    SECTION("unknown element") { model["elements"][0]["kind"] = "frequency"; }
    SECTION("unknown endpoint") { model["elements"][0]["from"]["kind"] = "ground"; }
    SECTION("wrong nominal dimension") {
        model["elements"][0]["parameter"]["nominal"]["dimension"] = "resistance";
    }
    SECTION("unknown dimension") {
        model["elements"][0]["parameter"]["nominal"]["dimension"] = "henry";
    }
    SECTION("wrong tolerance dimension") {
        model["elements"][1]["parameter"]["tolerance"]["minus"]["dimension"] = "capacitance";
    }
    SECTION("numeric nominal text") {
        model["elements"][0]["parameter"]["nominal"]["value"] = "1e-9";
    }
    SECTION("null nominal") { model["elements"][0]["parameter"]["nominal"]["value"] = nullptr; }
    SECTION("null evidence collection") { model["elements"][1]["parameter"]["evidence"] = nullptr; }
    SECTION("evidence is numeric") { model["elements"][1]["parameter"]["evidence"][0] = 1; }
    SECTION("malformed evidence hash") {
        model["elements"][1]["parameter"]["evidence"][0] = "datasheet.pdf";
    }
    SECTION("terminal collection is object") { model["terminals"] = Json::object(); }
    check_structural_rejection(document, component);
}

TEST_CASE("Part electrical model rejects malformed graphs before checking Part identity") {
    const auto component = passive_component();
    auto document = composite_document(component);
    auto &model = document["electrical_model"];
    SECTION("empty key") { model["elements"][0]["key"] = ""; }
    SECTION("duplicate terminal key") { model["terminals"][1]["key"] = "a"; }
    SECTION("duplicate node key") { model["internal_nodes"][1]["key"] = "a"; }
    SECTION("duplicate element key") { model["elements"][1]["key"] = "esl"; }
    SECTION("duplicate pin binding") { model["terminals"][1]["pin_key"] = "A"; }
    SECTION("foreign pin binding") { model["terminals"][1]["pin_key"] = "foreign"; }
    SECTION("missing terminal") { model["terminals"].erase(1); }
    SECTION("dangling endpoint") { model["elements"][0]["to"]["key"] = "foreign"; }
    SECTION("wrong typed endpoint namespace") { model["elements"][0]["to"]["kind"] = "terminal"; }
    SECTION("same endpoints") { model["elements"][0]["from"] = model["elements"][0]["to"]; }
    SECTION("unused internal node") { model["internal_nodes"].push_back({{"key", "unused"}}); }
    SECTION("unused terminal") { model["elements"][2]["to"] = model["elements"][1]["from"]; }
    SECTION("empty model") { model["elements"] = Json::array(); }
    check_structural_rejection(document, component);
}

TEST_CASE("Part electrical model reader enforces native nominal and tolerance domains") {
    const auto component = passive_component();
    auto document = composite_document(component);
    auto &elements = document["electrical_model"]["elements"];
    SECTION("negative resistance") { elements[1]["parameter"]["nominal"]["value"] = -1.0; }
    SECTION("zero capacitance") { elements[2]["parameter"]["nominal"]["value"] = 0.0; }
    SECTION("zero inductance") { elements[0]["parameter"]["nominal"]["value"] = 0.0; }
    SECTION("negative tolerance") {
        elements[1]["parameter"]["tolerance"]["minus"]["value"] = -1.0;
    }
    SECTION("resistance lower bound negative") {
        elements[1]["parameter"]["tolerance"]["minus"]["value"] = 11.0;
    }
    SECTION("capacitance lower bound zero") {
        elements[2]["parameter"]["tolerance"]["minus"]["value"] = 10.0e-6;
    }
    SECTION("derived bound overflow") {
        elements[1]["parameter"]["nominal"]["value"] = std::numeric_limits<double>::max();
        elements[1]["parameter"]["tolerance"]["plus"]["value"] = std::numeric_limits<double>::max();
    }
    check_structural_rejection(document, component);
}

TEST_CASE("Part model reader rejects nonfinite JSON numbers duplicate keys and old versions") {
    const auto component = passive_component();
    const auto original = composite_document(component);
    for (const auto version : {1, 2, 3, 4, 5, 7}) {
        auto document = original;
        document["version"] = version;
        check_structural_rejection(document, component);
    }
    for (const auto *number : {"NaN", "Infinity", "-Infinity", "1e999"}) {
        auto bytes = original.dump();
        const auto position = bytes.find("\"value\":");
        REQUIRE(position != std::string::npos);
        const auto begin = position + std::string{"\"value\":"}.size();
        bytes.replace(begin, bytes.find_first_of(",}", begin) - begin, number);
        CHECK_THROWS_AS(volt::io::read_part_definition_text(bytes, component), std::logic_error);
    }
    auto bytes = original.dump();
    const auto position = bytes.find("\"key\":\"esl\"");
    REQUIRE(position != std::string::npos);
    bytes.insert(position, "\"key\":\"forged\",");
    CHECK_THROWS_WITH(volt::io::read_part_definition_text(bytes, component),
                      "Part definition contains a duplicate JSON object key");
}

TEST_CASE("Part reader verifies semantic identity for every valid model mutation") {
    const auto component = passive_component();
    auto document = composite_document(component);
    auto &model = document["electrical_model"];
    SECTION("remove model") { model = nullptr; }
    SECTION("nominal") { model["elements"][1]["parameter"]["nominal"]["value"] = 11.0; }
    SECTION("tolerance absence") { model["elements"][1]["parameter"]["tolerance"] = nullptr; }
    SECTION("tolerance value") {
        model["elements"][1]["parameter"]["tolerance"]["plus"]["value"] = 3.0;
    }
    SECTION("evidence") {
        model["elements"][1]["parameter"]["evidence"][0] =
            volt::sha256_content_hash("changed").value();
    }
    SECTION("element key") { model["elements"][1]["key"] = "changed"; }
    SECTION("orientation") { std::swap(model["elements"][1]["from"], model["elements"][1]["to"]); }
    SECTION("terminal binding") {
        std::swap(model["terminals"][0]["pin_key"], model["terminals"][1]["pin_key"]);
    }
    SECTION("terminal key") {
        model["terminals"][0]["key"] = "changed";
        model["elements"][1]["from"]["key"] = "changed";
    }
    SECTION("internal node key") {
        model["internal_nodes"][0]["key"] = "changed";
        model["elements"][0]["from"]["key"] = "changed";
        model["elements"][1]["to"]["key"] = "changed";
    }
    SECTION("element variant") {
        model["elements"][0]["kind"] = "capacitance";
        model["elements"][0]["parameter"]["nominal"]["dimension"] = "capacitance";
    }
    CHECK_THROWS_WITH(volt::io::read_part_definition_text(document.dump(), component),
                      "Part definition content identity mismatch");
}
