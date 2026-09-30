#include "support/dc_request_fixture.hpp"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <volt/electrical/compiled_electrical_model.hpp>
#include <volt/io/electrical/ac_request_io.hpp>

TEST_CASE(
    "AC request codecs preserve canonical samples phases exact identity and keyed measurements") {
    using namespace volt;
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_dc_input(*fixture.circuit, fixture.library);
    const auto dc = test::dc_request::complete_request(input);
    const auto nets = DcNetPair{input.net(NetId{0}), input.net(NetId{1})};
    const auto request = AcRequest{
        DcRequestKey{"sweep"},
        input,
        dc.reference(),
        AcFrequencySweep::logarithmic(Quantity{UnitDimension::Frequency, 1.0},
                                      Quantity{UnitDimension::Frequency, 1000.0}, 4),
        {AcCurrentSource{DcSourceKey{"drive"}, nets, Quantity{UnitDimension::Current, 0.1}, 1.0}},
        {DcVoltageProbe{DcProbeKey{"v"}, nets}},
        dc.exclusions(),
        {AcGainProbe{DcProbeKey{"g"}, DcProbeKey{"v"}, DcProbeKey{"v"}}},
        {AcImpedanceProbe{DcProbeKey{"z"}, nets, DcSourceKey{"drive"}}}};
    const auto bytes = io::write_ac_request(request);
    const auto restored = io::read_ac_request(bytes, input);
    CHECK(io::write_ac_request(restored) == bytes);
    const auto restored_model = compile_electrical(restored);
    const auto original_model = compile_electrical(request);
    REQUIRE(restored_model.complete());
    REQUIRE(original_model.complete());
    CHECK(restored_model.model()->identity() == original_model.model()->identity());
    CHECK_THROWS(io::read_dc_request(bytes, input));
    auto document = nlohmann::ordered_json::parse(bytes);
    SECTION("wrong frequency dimension") { document["frequencies"][0]["dimension"] = "voltage"; }
    SECTION("duplicate frequency") { document["frequencies"][1] = document["frequencies"][0]; }
    SECTION("stale exact input") {
        document["input"]["logical"] = "sha256:" + std::string(64, '0');
    }
    SECTION("wrong source dimension") {
        document["sources"][0]["amplitude"]["dimension"] = "voltage";
    }
    SECTION("non-numeric phase") { document["sources"][0]["phase_radians"] = "1"; }
    SECTION("unknown field") { document["unused"] = 1; }
    SECTION("unknown gain target") { document["gains"][0]["numerator"] = "missing"; }
    SECTION("unknown impedance source") { document["impedances"][0]["source"] = "missing"; }
    CHECK_THROWS(io::read_ac_request(document.dump(), input));
}

TEST_CASE("AC request codec rejects duplicate object keys before interpretation") {
    using namespace volt;
    const auto fixture = test::dc_request::make_fixture();
    const auto input = io::prepare_dc_input(*fixture.circuit, fixture.library);
    CHECK_THROWS(
        io::read_ac_request(R"({"format":"volt.ac-request","format":"volt.ac-request"})", input));
}
