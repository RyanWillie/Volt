#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <volt/io/capabilities/board_capability_profile.hpp>
#include <volt/io/pcb/manufacturing_target.hpp>

namespace volt::io {
namespace {
using Json = nlohmann::json;

[[noreturn]] void malformed(std::string message) {
    throw ManufacturingTargetReadError{ManufacturingTargetReadErrorCode::Malformed,
                                       std::move(message)};
}

void require(bool condition, std::string_view message) {
    if (!condition)
        malformed(std::string{message});
}

void keys(const Json &value, std::initializer_list<std::string_view> required,
          std::initializer_list<std::string_view> optional = {}) {
    require(value.is_object(), "Expected manufacturing target object");
    for (const auto key : required)
        require(value.contains(key), "Missing manufacturing target field: " + std::string{key});
    for (const auto &entry : value.items())
        require(std::ranges::find(required, entry.key()) != required.end() ||
                    std::ranges::find(optional, entry.key()) != optional.end(),
                "Unknown manufacturing target field: " + entry.key());
}

std::string string(const Json &value) {
    require(value.is_string(), "Expected string");
    return value.get<std::string>();
}

double number(const Json &value) {
    require(value.is_number(), "Expected finite manufacturing number");
    const auto result = value.get<double>();
    require(std::isfinite(result), "Expected finite manufacturing number");
    return result;
}

unsigned int integer(const Json &value) {
    require(value.is_number_integer() && value >= 0 &&
                value <= std::numeric_limits<unsigned int>::max(),
            "Expected unsigned manufacturing integer");
    return value.get<unsigned int>();
}

void version(const Json &value) {
    require(value.is_number_integer(), "Manufacturing version must be an integer");
    if (value != 1)
        throw ManufacturingTargetReadError{ManufacturingTargetReadErrorCode::UnsupportedVersion,
                                           "Unsupported manufacturing document or policy version"};
}

template <typename T> std::span<const std::string_view> enum_names();

template <> std::span<const std::string_view> enum_names<ManufacturingGoal>() {
    static constexpr auto values =
        std::array{std::string_view{"Fabrication"}, std::string_view{"FabricationAndAssembly"}};
    return values;
}

template <> std::span<const std::string_view> enum_names<ManufacturingFamily>() {
    static constexpr auto values =
        std::array{std::string_view{"RigidFr4"}, std::string_view{"Flex"}};
    return values;
}

template <> std::span<const std::string_view> enum_names<ManufacturingMaterial>() {
    static constexpr auto values =
        std::array{std::string_view{"Fr4Tg135"}, std::string_view{"OtherFr4"}};
    return values;
}

template <> std::span<const std::string_view> enum_names<ManufacturingFinish>() {
    static constexpr auto values =
        std::array{std::string_view{"LeadFreeHasl"}, std::string_view{"Enig"}};
    return values;
}

template <> std::span<const std::string_view> enum_names<ManufacturingMaskSides>() {
    static constexpr auto values = std::array{
        std::string_view{"Both"}, std::string_view{"SingleSide"}, std::string_view{"NoMask"}};
    return values;
}

template <> std::span<const std::string_view> enum_names<ManufacturingMaskColour>() {
    static constexpr auto values =
        std::array{std::string_view{"Green"}, std::string_view{"OtherColour"}};
    return values;
}

template <> std::span<const std::string_view> enum_names<ManufacturingViaTreatment>() {
    static constexpr auto values = std::array{
        std::string_view{"Exposed"}, std::string_view{"Tented"}, std::string_view{"Filled"}};
    return values;
}

template <> std::span<const std::string_view> enum_names<ManufacturingDrilling>() {
    static constexpr auto values =
        std::array{std::string_view{"MechanicalThrough"}, std::string_view{"Laser"}};
    return values;
}

template <> std::span<const std::string_view> enum_names<ManufacturingHoleTreatment>() {
    static constexpr auto values =
        std::array{std::string_view{"RoundOnly"}, std::string_view{"BlindBuried"}};
    return values;
}

template <> std::span<const std::string_view> enum_names<ManufacturingSlotTreatment>() {
    static constexpr auto values = std::array{
        std::string_view{"NoSlots"}, std::string_view{"Routed"}, std::string_view{"Plated"}};
    return values;
}

template <> std::span<const std::string_view> enum_names<ManufacturingAssembly>() {
    static constexpr auto values =
        std::array{std::string_view{"NoAssembly"}, std::string_view{"SmtAndThroughHole"},
                   std::string_view{"OtherAssembly"}};
    return values;
}

template <typename T> T enumeration(const Json &value) {
    const auto name = string(value);
    const auto names = enum_names<T>();
    const auto found = std::ranges::find(names, name);
    require(found != names.end(), "Unknown manufacturing enum value");
    return static_cast<T>(std::ranges::distance(names.begin(), found));
}

template <typename T> Json enum_json(T value) {
    return enum_names<T>()[static_cast<std::size_t>(value)];
}

template <typename T> Json optional_enum(const std::optional<T> &value) {
    return value ? enum_json(*value) : Json{};
}

Json optional_number(std::optional<double> value) { return value ? Json(*value) : Json{}; }

Json range_json(std::optional<BoardCapabilityRange> value) {
    return value ? Json{{"minimum_mm", value->minimum_mm}, {"maximum_mm", value->maximum_mm}}
                 : Json{};
}

std::optional<BoardCapabilityRange> range(const Json &value) {
    if (value.is_null())
        return std::nullopt;
    keys(value, {"minimum_mm", "maximum_mm"});
    return BoardCapabilityRange{number(value.at("minimum_mm")), number(value.at("maximum_mm"))};
}

Json process_json(const ManufacturingProcess &p) {
    return {
        {"family", optional_enum(p.family)},
        {"material", optional_enum(p.material)},
        {"copper_layers", (p.copper_layers ? Json(*p.copper_layers) : Json{})},
        {"top_copper_weight_oz", optional_number(p.top_copper_weight_oz)},
        {"bottom_copper_weight_oz", optional_number(p.bottom_copper_weight_oz)},
        {"thickness_mm", optional_number(p.thickness_mm)},
        {"finish", optional_enum(p.finish)},
        {"mask_sides", optional_enum(p.mask_sides)},
        {"mask_colour", optional_enum(p.mask_colour)},
        {"via_treatment", optional_enum(p.via_treatment)},
        {"drilling", optional_enum(p.drilling)},
        {"holes", optional_enum(p.holes)},
        {"slots", optional_enum(p.slots)},
        {"assembly", optional_enum(p.assembly)},
        {"dimension_x_mm", optional_number(p.dimension_x_mm)},
        {"dimension_y_mm", optional_number(p.dimension_y_mm)},
    };
}

ManufacturingProcess process(const Json &value) {
    keys(value,
         {"family", "material", "copper_layers", "top_copper_weight_oz", "bottom_copper_weight_oz",
          "thickness_mm", "finish", "mask_sides", "mask_colour", "via_treatment", "drilling",
          "holes", "slots", "assembly", "dimension_x_mm", "dimension_y_mm"});
    auto p = ManufacturingProcess{};
    if (!value.at("family").is_null())
        p.family = enumeration<ManufacturingFamily>(value.at("family"));
    if (!value.at("material").is_null())
        p.material = enumeration<ManufacturingMaterial>(value.at("material"));
    if (!value.at("copper_layers").is_null())
        p.copper_layers = integer(value.at("copper_layers"));
    if (!value.at("top_copper_weight_oz").is_null())
        p.top_copper_weight_oz = number(value.at("top_copper_weight_oz"));
    if (!value.at("bottom_copper_weight_oz").is_null())
        p.bottom_copper_weight_oz = number(value.at("bottom_copper_weight_oz"));
    if (!value.at("thickness_mm").is_null())
        p.thickness_mm = number(value.at("thickness_mm"));
    if (!value.at("finish").is_null())
        p.finish = enumeration<ManufacturingFinish>(value.at("finish"));
    if (!value.at("mask_sides").is_null())
        p.mask_sides = enumeration<ManufacturingMaskSides>(value.at("mask_sides"));
    if (!value.at("mask_colour").is_null())
        p.mask_colour = enumeration<ManufacturingMaskColour>(value.at("mask_colour"));
    if (!value.at("via_treatment").is_null())
        p.via_treatment = enumeration<ManufacturingViaTreatment>(value.at("via_treatment"));
    if (!value.at("drilling").is_null())
        p.drilling = enumeration<ManufacturingDrilling>(value.at("drilling"));
    if (!value.at("holes").is_null())
        p.holes = enumeration<ManufacturingHoleTreatment>(value.at("holes"));
    if (!value.at("slots").is_null())
        p.slots = enumeration<ManufacturingSlotTreatment>(value.at("slots"));
    if (!value.at("assembly").is_null())
        p.assembly = enumeration<ManufacturingAssembly>(value.at("assembly"));
    if (!value.at("dimension_x_mm").is_null())
        p.dimension_x_mm = number(value.at("dimension_x_mm"));
    if (!value.at("dimension_y_mm").is_null())
        p.dimension_y_mm = number(value.at("dimension_y_mm"));
    return p;
}

BoardCapabilityProfile capability(const Json &doc) {
    keys(doc, {"format", "version", "profile"});
    version(doc.at("version"));
    const auto &p = doc.at("profile");
    keys(p,
         {"name", "provenance", "minimum_track_width_mm", "minimum_via_drill_mm",
          "minimum_via_annular_mm", "minimum_clearances"},
         {"copper_weight_refinements", "supported_copper_layer_counts", "board_thickness_range_mm",
          "available_copper_weights_oz", "drill_diameter_range_mm"});
    keys(p.at("provenance"), {"source", "as_of"});
    require(p.at("minimum_clearances").is_array(), "Expected clearance array");
    for (const auto &item : p.at("minimum_clearances"))
        keys(item, {"first", "second", "clearance_mm"});
    if (p.contains("copper_weight_refinements")) {
        require(p.at("copper_weight_refinements").is_array(), "Expected refinement array");
        for (const auto &item : p.at("copper_weight_refinements"))
            keys(item, {"copper_weight_oz", "minimum_track_width_mm", "minimum_clearance_mm"});
    }
    for (const auto *name : {"board_thickness_range_mm", "drill_diameter_range_mm"})
        if (p.contains(name)) {
            require(!p.at(name).is_null(), "Native capability range cannot be null");
            static_cast<void>(range(p.at(name)));
        }
    if (p.contains("supported_copper_layer_counts")) {
        require(p.at("supported_copper_layer_counts").is_array(),
                "Expected supported copper layer counts array");
        for (const auto &count : p.at("supported_copper_layer_counts"))
            require(count.is_number_integer() && count > 0 &&
                        count <= std::numeric_limits<int>::max(),
                    "Supported copper layer count is outside the native integer range");
    }
    return read_capability_profile_text(doc.dump());
}

Json cpl_json(const std::optional<CplProjectionOptions> &options) {
    if (!options)
        return {};
    auto entries = Json::array();
    for (const auto &offset : options->rotation_offsets)
        entries.push_back(
            {{"library", offset.footprint().library()},
             {"name", offset.footprint().name()},
             {"rotation_deg", offset.rotation_deg() == 0.0 ? 0.0 : offset.rotation_deg()}});
    return {{"rotation_offsets", entries}};
}

std::optional<CplProjectionOptions> cpl(const Json &value) {
    if (value.is_null())
        return {};
    keys(value, {"rotation_offsets"});
    require(value.at("rotation_offsets").is_array(), "Expected CPL offsets array");
    auto result = CplProjectionOptions{};
    for (const auto &entry : value.at("rotation_offsets")) {
        keys(entry, {"library", "name", "rotation_deg"});
        result.rotation_offsets.emplace_back(
            FootprintRef{string(entry.at("library")), string(entry.at("name"))},
            number(entry.at("rotation_deg")));
    }
    return result;
}
} // namespace

std::string write_manufacturing_target(const ManufacturingTarget &target) {
    const auto &profile = target.profile();
    const auto doc =
        Json{{"format", "volt.manufacturing-target"},
             {"schema_version", 1},
             {"policy_version", 1},
             {"source",
              {{"board", target.source().board().value()},
               {"provenance_digest", target.source().provenance_digest().value()},
               {"content_digest", target.content_digest().value()}}},
             {"goal", enum_json(target.goal())},
             {"process", process_json(target.process())},
             {"profile",
              {{"capability", Json::parse(write_capability_profile(profile.capability()))},
               {"source_content", profile.source_content().value()},
               {"applicability", process_json(profile.applicability())},
               {"dimension_x_mm", range_json(profile.dimension_x_mm())},
               {"dimension_y_mm", range_json(profile.dimension_y_mm())},
               {"minimum_via_radial_annulus_mm",
                optional_number(profile.minimum_via_radial_annulus_mm())}}},
             {"cpl_options", cpl_json(target.cpl_options())}};
    return doc.dump(2) + "\n";
}

ManufacturingTarget read_manufacturing_target(std::string_view text) {
    try {
        auto stack = std::vector<std::set<std::string>>{};
        const auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
            if (event == Json::parse_event_t::object_start)
                stack.emplace_back();
            else if (event == Json::parse_event_t::key)
                require(stack.back().insert(parsed.get<std::string>()).second,
                        "Duplicate manufacturing target JSON key");
            else if (event == Json::parse_event_t::object_end)
                stack.pop_back();
            return true;
        };
        const auto doc = Json::parse(text.begin(), text.end(), callback);
        keys(doc, {"format", "schema_version", "policy_version", "source", "goal", "process",
                   "profile", "cpl_options"});
        require(string(doc.at("format")) == "volt.manufacturing-target",
                "Unsupported manufacturing target format");
        version(doc.at("schema_version"));
        version(doc.at("policy_version"));
        const auto &source = doc.at("source");
        keys(source, {"board", "provenance_digest", "content_digest"});
        const auto &p = doc.at("profile");
        keys(p, {"capability", "source_content", "applicability", "dimension_x_mm",
                 "dimension_y_mm", "minimum_via_radial_annulus_mm"});
        return ManufacturingTarget{
            CompiledBoardIdentity{BoardName{string(source.at("board"))},
                                  ContentHash{string(source.at("provenance_digest"))}},
            ContentHash{string(source.at("content_digest"))},
            enumeration<ManufacturingGoal>(doc.at("goal")),
            process(doc.at("process")),
            ManufacturingProfile{
                capability(p.at("capability")), ContentHash{string(p.at("source_content"))},
                process(p.at("applicability")), range(p.at("dimension_x_mm")),
                range(p.at("dimension_y_mm")),
                p.at("minimum_via_radial_annulus_mm").is_null()
                    ? std::nullopt
                    : std::optional{number(p.at("minimum_via_radial_annulus_mm"))}},
            cpl(doc.at("cpl_options"))};
    } catch (const ManufacturingTargetReadError &) {
        throw;
    } catch (const KernelError &error) {
        malformed(error.what());
    } catch (const Json::exception &error) {
        malformed(error.what());
    }
}
} // namespace volt::io
