#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/io/electrical/dc_request_io.hpp>

namespace {

using namespace volt;
using Json = nlohmann::ordered_json;

class NoParts final : public PartDefinitionResolver {
  public:
    const PartDefinition &resolve(const LibraryPartRef &) const & override {
        throw KernelRangeError{ErrorCode::UnknownEntity, "No selected Parts"};
    }
};

DcInput input() {
    auto circuit = Circuit{};
    static_cast<void>(circuit.add_net(NetSpec{.name = NetName{"positive"}}));
    static_cast<void>(circuit.add_net(NetSpec{.name = NetName{"reference"}}));
    return io::prepare_dc_input(circuit, NoParts{});
}

DcRequest request(const DcInput &owner) {
    const auto pair = DcNetPair{owner.net(NetId{0}), owner.net(NetId{1})};
    return DcRequest{
        DcRequestKey{"dc"},
        owner,
        owner.net(NetId{1}),
        {DcVoltageSource{DcSourceKey{"supply"}, pair, Quantity{UnitDimension::Voltage, -1.25}}},
        {DcVoltageProbe{DcProbeKey{"voltage"}, pair},
         DcSourceCurrentProbe{DcProbeKey{"current"}, DcSourceKey{"supply"}}}};
}

} // namespace

TEST_CASE("DC request codec publishes deterministic current-only bound values") {
    const auto owner = input();
    const auto value = request(owner);
    const auto bytes = io::write_dc_request(value);
    const auto document = Json::parse(bytes);
    CHECK(document.at("format") == "volt.dc-request");
    CHECK(document.at("version") == 1);
    CHECK(document.at("reference") == "net:1");
    CHECK(document.at("sources")[0]["nets"]["from"] == "net:0");
    CHECK(document.at("sources")[0]["value"]["si"] == -1.25);
    const auto reopened = io::read_dc_request(bytes, owner);
    CHECK(io::write_dc_request(reopened) == bytes);
    CHECK(assess_dc_request(reopened).complete());

    const auto incomplete = DcRequest{DcRequestKey{"missing"}, owner, std::nullopt};
    const auto incomplete_bytes = io::write_dc_request(incomplete);
    CHECK(Json::parse(incomplete_bytes).at("reference").is_null());
    CHECK_FALSE(assess_dc_request(io::read_dc_request(incomplete_bytes, owner)).complete());
}

TEST_CASE("DC request codec rejects malformed variants fields quantities and references") {
    const auto owner = input();
    const auto original = Json::parse(io::write_dc_request(request(owner)));
    const auto mutations = std::vector<std::pair<std::string, std::function<void(Json &)>>>{
        {"unknown format", [](Json &d) { d["format"] = "other"; }},
        {"old version", [](Json &d) { d["version"] = 0; }},
        {"future version", [](Json &d) { d["version"] = 2; }},
        {"noninteger version", [](Json &d) { d["version"] = 1.0; }},
        {"missing reference", [](Json &d) { d.erase("reference"); }},
        {"unknown root field", [](Json &d) { d["solver"] = "anything"; }},
        {"unknown identity field", [](Json &d) { d["input"]["cache"] = "hidden"; }},
        {"foreign logical identity",
         [](Json &d) { d["input"]["logical"] = sha256_content_hash("other input").value(); }},
        {"stale selected identity",
         [](Json &d) {
             d["input"]["selected_parts"] = sha256_content_hash("other parts").value();
         }},
        {"dangling reference", [](Json &d) { d["reference"] = "net:9"; }},
        {"wrong reference kind", [](Json &d) { d["reference"] = "component:1"; }},
        {"noncanonical reference", [](Json &d) { d["reference"] = "net:01"; }},
        {"numeric reference", [](Json &d) { d["reference"] = 1; }},
        {"source variant", [](Json &d) { d["sources"][0]["kind"] = "waveform"; }},
        {"source field", [](Json &d) { d["sources"][0]["result"] = 12; }},
        {"missing source value", [](Json &d) { d["sources"][0].erase("value"); }},
        {"source dimension", [](Json &d) { d["sources"][0]["value"]["dimension"] = "current"; }},
        {"unknown dimension", [](Json &d) { d["sources"][0]["value"]["dimension"] = "power"; }},
        {"source null", [](Json &d) { d["sources"][0]["value"]["si"] = nullptr; }},
        {"source string", [](Json &d) { d["sources"][0]["value"]["si"] = "5 V"; }},
        {"source boolean", [](Json &d) { d["sources"][0]["value"]["si"] = true; }},
        {"dangling source net", [](Json &d) { d["sources"][0]["nets"]["to"] = "net:9"; }},
        {"equal source nets", [](Json &d) { d["sources"][0]["nets"]["to"] = "net:0"; }},
        {"duplicate source", [](Json &d) { d["sources"].push_back(d["sources"][0]); }},
        {"probe variant", [](Json &d) { d["probes"][0]["kind"] = "power"; }},
        {"duplicate probe", [](Json &d) { d["probes"].push_back(d["probes"][0]); }},
        {"missing current branch", [](Json &d) { d["probes"][0]["source"] = "missing"; }},
        {"non-array exclusions", [](Json &d) { d["exclusions"] = Json::object(); }},
        {"dangling exclusion",
         [](Json &d) {
             d["exclusions"] = Json::array(
                 {{{"occurrence", "component:0"}, {"reason", {{"kind", "outside_analysis"}}}}});
         }},
    };
    for (const auto &[name, mutate] : mutations) {
        DYNAMIC_SECTION(name) {
            auto malformed = original;
            mutate(malformed);
            CHECK_THROWS(io::read_dc_request(malformed.dump(), owner));
        }
    }
}

TEST_CASE("DC request JSON rejects duplicate keys and overflowing numeric values") {
    const auto owner = input();
    const auto original = io::write_dc_request(request(owner));
    auto duplicated = original;
    duplicated.insert(duplicated.find("\"key\""), "\"key\": \"hidden\", ");
    CHECK_THROWS(io::read_dc_request(duplicated, owner));
    for (const auto spelling : {"1e999", "NaN", "Infinity", "-Infinity"}) {
        auto malformed = original;
        const auto at = malformed.find("-1.25");
        REQUIRE(at != std::string::npos);
        malformed.replace(at, 5U, spelling);
        CHECK_THROWS(io::read_dc_request(malformed, owner));
    }
}
