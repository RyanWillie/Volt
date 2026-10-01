#include "support/dc_request_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <volt/electrical/dc_solve.hpp>
#include <volt/io/electrical/compiled_electrical_model_io.hpp>
#include <volt/io/electrical/transient_request_io.hpp>

namespace {
using namespace volt;
using Json = nlohmann::ordered_json;

Quantity seconds(double value) { return Quantity{UnitDimension::Time, value}; }

Quantity volts(double value) { return Quantity{UnitDimension::Voltage, value}; }

TransientRequest request(const ElectricalInput &input) {
    const auto dc = test::dc_request::complete_request(input);
    const auto occurrence = input.occurrence(ComponentId{0});
    const auto part = *input.circuit().get(ComponentId{0}).selected_library_part_ref();
    return TransientRequest{
        ElectricalRequestKey{"io"},
        input,
        dc.reference(),
        TransientTimeGrid::uniform(seconds(1.0), 3),
        {TransientVoltageSource{
            ElectricalSourceKey{"drive"},
            ElectricalNetPair{input.net(NetId{0}), input.net(NetId{1})},
            TransientWaveform{std::vector<TransientWaveformKnot>{{seconds(0.0), volts(0.0)},
                                                                 {seconds(0.4), volts(1.0)},
                                                                 {seconds(0.8), volts(-1.0)}}}}},
        dc.probes(),
        dc.exclusions(),
        {{occurrence, part, ModelElementKey{"storage"}, TransientStorageKind::CapacitorVoltage,
          volts(0.0)},
         {occurrence, part, ModelElementKey{"esl"}, TransientStorageKind::InductorCurrent,
          Quantity{UnitDimension::Current, 0.0}}}};
}
} // namespace

TEST_CASE("Transient native codec deterministically roundtrips waveforms exact storage and graph "
          "identity") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto authored = request(input);
    const auto bytes = io::write_transient_request(authored);
    const auto loaded = io::read_transient_request(bytes, input);
    CHECK(io::write_transient_request(loaded) == bytes);
    const auto before = compile_electrical(authored);
    const auto after = compile_electrical(loaded);
    REQUIRE(before.complete());
    REQUIRE(after.complete());
    CHECK(after.model()->identity() == before.model()->identity());
    CHECK(io::write_compiled_electrical_model(*before.model()).find("transient_voltage_source") !=
          std::string::npos);
    auto canonical = Json::parse(bytes);
    std::reverse(canonical["initial_state"].begin(), canonical["initial_state"].end());
    std::reverse(canonical["probes"].begin(), canonical["probes"].end());
    CHECK(io::write_transient_request(io::read_transient_request(canonical.dump(), input)) ==
          bytes);
}

TEST_CASE(
    "Transient codec rejects duplicate fields stale identities invalid dimensions and targets") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto document = Json::parse(io::write_transient_request(request(input)));
    const auto reject = [&](const auto &mutate) {
        auto value = document;
        mutate(value);
        CHECK_THROWS(io::read_transient_request(value.dump(), input));
    };
    reject([](auto &value) { value["version"] = 2; });
    reject([](auto &value) { value["extra"] = true; });
    reject([](auto &value) { value["times"][1]["dimension"] = "voltage"; });
    reject([](auto &value) { value["times"][1]["si"] = 0.0; });
    reject([](auto &value) { value["sources"][0]["waveform"]["kind"] = "callback"; });
    reject([](auto &value) { value["sources"][0]["waveform"]["knots"][1]["time"]["si"] = 2.0; });
    reject([](auto &value) { value["sources"].push_back(value["sources"][0]); });
    reject([](auto &value) {
        value["initial_state"][0]["part"]["part_digest"] = std::string(64, '0');
    });
    reject([](auto &value) { value["initial_state"][0]["kind"] = "capacitor_voltage"; });
    reject([](auto &value) { value["initial_state"].push_back(value["initial_state"][0]); });
    reject([](auto &value) { value["initial_state"][0]["occurrence"] = "component:1"; });
    reject([](auto &value) { value["probes"][0]["source"] = "unknown"; });
    auto repeated = document.dump();
    repeated.insert(1, "\"format\":\"volt.transient-request\",");
    CHECK_THROWS(io::read_transient_request(repeated, input));
    auto other = test::dc_request::make_fixture();
    static_cast<void>(other.circuit->add_net(NetSpec{.name = NetName{"extra"}}));
    const auto changed = io::prepare_electrical_input(*other.circuit, other.library);
    CHECK_THROWS(io::read_transient_request(document.dump(), changed));
}

TEST_CASE("DC copied collection provenance survives native codec and refuses dropped mappings") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto dc_request = test::dc_request::complete_request(input);
    const auto compile = compile_electrical(dc_request);
    REQUIRE(compile.complete());
    const auto dc = solve_dc(*compile.model());
    REQUIRE(dc.success());
    const auto target = TransientRequest{
        ElectricalRequestKey{"dc-copy"}, input,
        dc_request.reference(),          TransientTimeGrid::uniform(seconds(1.0), 2),
        request(input).sources(),        {},
        dc_request.exclusions(),         initial_state_from(dc)};
    const auto bytes = io::write_transient_request(target);
    const auto loaded = io::read_transient_request(bytes, input);
    REQUIRE(loaded.initial_conditions().dc_provenance());
    CHECK(loaded.initial_conditions().dc_provenance()->analysis_identity == dc.analysis_identity());
    CHECK(io::write_transient_request(loaded) == bytes);
    auto missing = Json::parse(bytes);
    missing["initial_state"] = Json::array();
    CHECK_THROWS(io::read_transient_request(missing.dump(), input));
}

TEST_CASE("Transient codec retains signed constant current laws and independent request identity") {
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_electrical_input(*fixture.circuit, fixture.library);
    const auto base = request(input);
    const auto constant = TransientRequest{
        base.key(),
        input,
        base.reference(),
        base.grid(),
        {TransientCurrentSource{ElectricalSourceKey{"drive"},
                                ElectricalNetPair{input.net(NetId{0}), input.net(NetId{1})},
                                TransientWaveform{Quantity{UnitDimension::Current, -0.25}}}},
        {},
        base.exclusions(),
        base.initial_conditions()};
    const auto bytes = io::write_transient_request(constant);
    const auto loaded = io::read_transient_request(bytes, input);
    CHECK(io::write_transient_request(loaded) == bytes);
    CHECK(std::get<TransientCurrentSource>(loaded.sources().front())
              .waveform()
              .value_at(seconds(1.0)) == Quantity{UnitDimension::Current, -0.25});
    const auto original = compile_electrical(constant);
    REQUIRE(original.complete());
    const auto grid_changed =
        compile_electrical(TransientRequest{base.key(),
                                            input,
                                            base.reference(),
                                            TransientTimeGrid::uniform(seconds(1.0), 5),
                                            constant.sources(),
                                            {},
                                            base.exclusions(),
                                            base.initial_conditions()});
    REQUIRE(grid_changed.complete());
    CHECK(grid_changed.model()->request_identity() != original.model()->request_identity());
}
