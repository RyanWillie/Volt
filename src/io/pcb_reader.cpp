#include <volt/io/pcb_reader.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <iterator>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "pcb_feature_io.hpp"

#include <volt/io/detail/typed_id.hpp>
#include <volt/io/pcb_schema.hpp>
#include <volt/pcb/board.hpp>
#include <volt/pcb/footprints.hpp>

namespace volt::io::detail {

/** Internal implementation for loading the v1 PCB projection JSON format. */
class PcbBoardReader {
  public:
    /** Construct a reader over a parsed JSON document and its logical circuit context. */
    PcbBoardReader(const Circuit &circuit, const nlohmann::json &document)
        : circuit_{circuit}, document_{document} {}

    /** Load and structurally validate the document into a board projection. */
    [[nodiscard]] Board read();

  private:
    static void require(bool condition, const std::string &message);

    static const nlohmann::json &field(const nlohmann::json &object, const char *name);

    static const nlohmann::json *optional_field(const nlohmann::json &object, const char *name);

    static const nlohmann::json &object_field(const nlohmann::json &object, const char *name);

    static const nlohmann::json &array_field(const nlohmann::json &object, const char *name);

    static std::string string_field(const nlohmann::json &object, const char *name);

    static std::optional<std::string> nullable_string_field(const nlohmann::json &object,
                                                            const char *name);

    static bool bool_field(const nlohmann::json &object, const char *name);

    static double number_field(const nlohmann::json &object, const char *name);

    static int int_field(const nlohmann::json &object, const char *name);

    template <typename Id> static Id typed_id(const nlohmann::json &object, const char *name) {
        return decode_local_id<Id>(string_field(object, name));
    }

    template <typename Id> static std::optional<Id> decode_if_prefixed(std::string_view id) {
        if (id.rfind(local_id_prefix<Id>(), 0) != 0) {
            return std::nullopt;
        }
        return decode_local_id<Id>(id);
    }

    template <typename Id>
    static void require_sequential_id(const nlohmann::json &object, const char *name, Id expected,
                                      const std::string &message) {
        require(typed_id<Id>(object, name) == expected, message);
    }

    static void require_format(const nlohmann::json &object);

    static void require_version(const nlohmann::json &object);

    [[nodiscard]] static BoardPoint board_point(const nlohmann::json &value);

    [[nodiscard]] static std::vector<BoardPoint> board_points(const nlohmann::json &value);

    [[nodiscard]] static FootprintPoint footprint_point(const nlohmann::json &value);

    [[nodiscard]] static FootprintSize footprint_size(const nlohmann::json &value);

    [[nodiscard]] static FootprintRef footprint_ref(const nlohmann::json &object);

    [[nodiscard]] static FootprintLayerSet footprint_layers(const nlohmann::json &value);

    [[nodiscard]] static std::optional<FootprintDrill> drill_value(const nlohmann::json &value);

    [[nodiscard]] static std::optional<FootprintDrill> drill(const nlohmann::json &object);

    [[nodiscard]] static std::optional<FootprintPadMechanicalRole>
    mechanical_role_value(const nlohmann::json &value);

    [[nodiscard]] static std::optional<FootprintPadMechanicalRole>
    mechanical_role(const nlohmann::json &object);

    [[nodiscard]] static FootprintPad footprint_pad(const nlohmann::json &object,
                                                    FootprintPadId expected_id);

    void read_layers(Board &board, const nlohmann::json &board_json) const;

    void read_layer_stack(Board &board, const nlohmann::json &board_json) const;

    void read_outline(Board &board, const nlohmann::json &board_json) const;

    void read_rules(Board &board, const nlohmann::json &board_json) const;

    void read_footprint_definitions(Board &board, const nlohmann::json &board_json) const;

    void read_placements(Board &board, const nlohmann::json &board_json) const;

    void read_tracks(Board &board, const nlohmann::json &board_json) const;

    void read_vias(Board &board, const nlohmann::json &board_json) const;

    [[nodiscard]] std::vector<BoardLayerId>
    read_board_layers(const Board &board, const nlohmann::json &object, const char *name,
                      const std::string &missing_message) const;

    void read_zones(Board &board, const nlohmann::json &board_json) const;

    void read_keepouts(Board &board, const nlohmann::json &board_json) const;

    void read_texts(Board &board, const nlohmann::json &board_json) const;

    void validate_placement_footprint(const Board &board, ComponentId component,
                                      const nlohmann::json &placement_json) const;

    [[nodiscard]] FootprintLibrary cached_footprint_library(const Board &board) const;

    void validate_viewer_cache(const Board &board) const;

    void validate_viewer_pad_resolution(const Board &board, const PadResolution &expected,
                                        const nlohmann::json &pad_resolution) const;

    static void validate_viewer_pad_geometry(const FootprintPad &pad,
                                             const nlohmann::json &geometry);

    void validate_viewer_diagnostics(const Board &board, const nlohmann::json &diagnostics) const;

    [[nodiscard]] static bool footprint_pad_exists(const Board &board,
                                                   const std::vector<FootprintDefId> &definitions,
                                                   FootprintPadId pad);

    static void validate_diagnostic_severity(const std::string &severity);

    void validate_viewer_diagnostic_ref(const Board &board, std::string_view ref) const;

    [[nodiscard]] static std::optional<PinId> optional_pin(const std::optional<std::string> &id);

    [[nodiscard]] static std::optional<NetId> optional_net(const std::optional<std::string> &id);

    const Circuit &circuit_;
    const nlohmann::json &document_;
};

[[nodiscard]] Board PcbBoardReader::read() {
    require(document_.is_object(), "PCB document must be an object");
    require_format(document_);
    require_version(document_);
    const auto &board_json = object_field(document_, "board");
    // v1 stores one board per document; this stable ID anchors viewer references.
    require(string_field(board_json, "id") == "board:0", "PCB board id must be board:0");

    static_cast<void>(board_units_from_name(string_field(board_json, "units")));
    auto board = Board{circuit_, BoardName{string_field(board_json, "name")}};
    read_rules(board, board_json);
    read_layers(board, board_json);
    read_layer_stack(board, board_json);
    read_outline(board, board_json);
    read_features(board, board_json);
    read_footprint_definitions(board, board_json);
    read_placements(board, board_json);
    read_tracks(board, board_json);
    read_vias(board, board_json);
    read_zones(board, board_json);
    read_keepouts(board, board_json);
    read_texts(board, board_json);
    validate_viewer_cache(board);
    return board;
}

void PcbBoardReader::require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::logic_error{message};
    }
}

const nlohmann::json &PcbBoardReader::field(const nlohmann::json &object, const char *name) {
    require(object.is_object(), "Expected object while reading PCB projection");
    const auto it = object.find(name);
    require(it != object.end(), std::string{"Missing required field: "} + name);
    return *it;
}

const nlohmann::json *PcbBoardReader::optional_field(const nlohmann::json &object,
                                                     const char *name) {
    require(object.is_object(), "Expected object while reading PCB projection");
    const auto it = object.find(name);
    if (it == object.end()) {
        return nullptr;
    }
    return &*it;
}

const nlohmann::json &PcbBoardReader::object_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_object(), std::string{"Expected object field: "} + name);
    return value;
}

const nlohmann::json &PcbBoardReader::array_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_array(), std::string{"Expected array field: "} + name);
    return value;
}

std::string PcbBoardReader::string_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_string(), std::string{"Expected string field: "} + name);
    return value.get<std::string>();
}

std::optional<std::string> PcbBoardReader::nullable_string_field(const nlohmann::json &object,
                                                                 const char *name) {
    const auto &value = field(object, name);
    if (value.is_null()) {
        return std::nullopt;
    }
    require(value.is_string(), std::string{"Expected string field: "} + name);
    return value.get<std::string>();
}

bool PcbBoardReader::bool_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_boolean(), std::string{"Expected boolean field: "} + name);
    return value.get<bool>();
}

double PcbBoardReader::number_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_number(), std::string{"Expected number field: "} + name);
    const auto result = value.get<double>();
    require(std::isfinite(result), std::string{"Expected finite number field: "} + name);
    return result;
}

int PcbBoardReader::int_field(const nlohmann::json &object, const char *name) {
    const auto &value = field(object, name);
    require(value.is_number_integer(), std::string{"Expected integer field: "} + name);
    return value.get<int>();
}

void PcbBoardReader::require_format(const nlohmann::json &object) {
    const auto actual = string_field(object, "format");
    require(actual == pcb_format_name(), "Unsupported PCB projection format: " + actual);
}

void PcbBoardReader::require_version(const nlohmann::json &object) {
    const auto &value = field(object, "version");
    require(value.is_number_integer(), "Expected integer field: version");
    const auto actual = value.get<std::int64_t>();
    require(actual == static_cast<std::int64_t>(pcb_format_version()),
            "Unsupported PCB projection format version: " + std::to_string(actual));
}

[[nodiscard]] BoardPoint PcbBoardReader::board_point(const nlohmann::json &value) {
    require(value.is_array(), "PCB point must be an array");
    require(value.size() == 2U, "PCB point must contain two numbers");
    require(value[0].is_number() && value[1].is_number(), "PCB point values must be numbers");
    const auto x = value[0].get<double>();
    const auto y = value[1].get<double>();
    require(std::isfinite(x) && std::isfinite(y), "PCB point values must be finite");
    return BoardPoint{x, y};
}

[[nodiscard]] std::vector<BoardPoint> PcbBoardReader::board_points(const nlohmann::json &value) {
    require(value.is_array(), "PCB point list must be an array");
    auto points = std::vector<BoardPoint>{};
    points.reserve(value.size());
    for (const auto &point : value) {
        points.push_back(board_point(point));
    }
    return points;
}

[[nodiscard]] FootprintPoint PcbBoardReader::footprint_point(const nlohmann::json &value) {
    const auto point = board_point(value);
    return FootprintPoint{point.x_mm(), point.y_mm()};
}

[[nodiscard]] FootprintSize PcbBoardReader::footprint_size(const nlohmann::json &value) {
    require(value.is_array(), "PCB footprint size must be an array");
    require(value.size() == 2U, "PCB footprint size must contain two numbers");
    require(value[0].is_number() && value[1].is_number(),
            "PCB footprint size values must be numbers");
    const auto width = value[0].get<double>();
    const auto height = value[1].get<double>();
    require(std::isfinite(width) && std::isfinite(height),
            "PCB footprint size values must be finite");
    return FootprintSize{width, height};
}

[[nodiscard]] FootprintRef PcbBoardReader::footprint_ref(const nlohmann::json &object) {
    require(object.is_object(), "PCB footprint ref must be an object");
    return FootprintRef{string_field(object, "library"), string_field(object, "name")};
}

[[nodiscard]] FootprintLayerSet PcbBoardReader::footprint_layers(const nlohmann::json &value) {
    require(value.is_array(), "PCB footprint layers must be an array");
    auto layers = std::vector<FootprintLayer>{};
    layers.reserve(value.size());
    for (const auto &layer : value) {
        require(layer.is_string(), "PCB footprint layer must be a string");
        layers.push_back(footprint_layer_from_name(layer.get<std::string>()));
    }
    return FootprintLayerSet{std::move(layers)};
}

[[nodiscard]] std::optional<FootprintDrill>
PcbBoardReader::drill_value(const nlohmann::json &value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    require(value.is_object(), "PCB footprint drill must be an object");
    return FootprintDrill{number_field(value, "diameter_mm"),
                          footprint_pad_plating_from_name(string_field(value, "plating"))};
}

[[nodiscard]] std::optional<FootprintDrill> PcbBoardReader::drill(const nlohmann::json &object) {
    const auto *value = optional_field(object, "drill");
    return value == nullptr ? std::nullopt : drill_value(*value);
}

[[nodiscard]] std::optional<FootprintPadMechanicalRole>
PcbBoardReader::mechanical_role_value(const nlohmann::json &value) {
    if (value.is_null()) {
        return std::nullopt;
    }
    require(value.is_string(), "PCB footprint mechanical role must be a string");
    return footprint_pad_mechanical_role_from_name(value.get<std::string>());
}

[[nodiscard]] std::optional<FootprintPadMechanicalRole>
PcbBoardReader::mechanical_role(const nlohmann::json &object) {
    const auto *value = optional_field(object, "mechanical_role");
    return value == nullptr ? std::nullopt : mechanical_role_value(*value);
}

[[nodiscard]] FootprintPad PcbBoardReader::footprint_pad(const nlohmann::json &object,
                                                         FootprintPadId expected_id) {
    require_sequential_id(object, "id", expected_id, "PCB footprint pad IDs must be sequential");
    const auto kind = footprint_pad_kind_from_name(string_field(object, "kind"));
    const auto label = string_field(object, "label");
    const auto shape = footprint_pad_shape_from_name(string_field(object, "shape"));
    const auto position = footprint_point(field(object, "position"));
    const auto size = footprint_size(field(object, "size"));
    const auto layers = footprint_layers(field(object, "layers"));
    const auto parsed_drill = drill(object);
    const auto parsed_mechanical_role = mechanical_role(object);

    if (kind == FootprintPadKind::SurfaceMount) {
        return FootprintPad::surface_mount(label, shape, position, size, layers,
                                           parsed_mechanical_role);
    }

    require(parsed_drill.has_value(), "PCB through-hole footprint pad requires drill data");
    return FootprintPad::through_hole(label, shape, position, size, layers, parsed_drill.value(),
                                      parsed_mechanical_role);
}

void PcbBoardReader::read_layers(Board &board, const nlohmann::json &board_json) const {
    const auto &layers = array_field(board_json, "layers");
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const auto &layer = layers[index];
        require(layer.is_object(), "PCB board layer must be an object");
        const auto expected = BoardLayerId{index};
        require_sequential_id(layer, "id", expected, "PCB board layer IDs must be sequential");
        const auto id = board.add_layer(BoardLayer{
            string_field(layer, "name"),
            board_layer_role_from_name(string_field(layer, "role")),
            board_layer_side_from_name(string_field(layer, "side")),
            number_field(layer, "thickness_mm"),
            bool_field(layer, "enabled"),
        });
        require(id == expected, "PCB board layer IDs must be sequential");
    }
}

void PcbBoardReader::read_layer_stack(Board &board, const nlohmann::json &board_json) const {
    const auto &stack = field(board_json, "layer_stack");
    if (stack.is_null()) {
        return;
    }
    require(stack.is_object(), "PCB layer stack must be an object");
    const auto &layers_json = array_field(stack, "layers");
    auto layers = std::vector<BoardLayerId>{};
    layers.reserve(layers_json.size());
    for (const auto &layer_json : layers_json) {
        require(layer_json.is_string(), "PCB layer stack layer must be a string");
        const auto layer = decode_local_id<BoardLayerId>(layer_json.get<std::string>());
        if (layer.index() >= board.layer_count()) {
            throw std::logic_error{"PCB layer stack references missing board layer"};
        }
        layers.push_back(layer);
    }
    board.set_layer_stack(LayerStack{std::move(layers), number_field(stack, "board_thickness_mm")});
}

void PcbBoardReader::read_outline(Board &board, const nlohmann::json &board_json) const {
    const auto &outline = field(board_json, "outline");
    if (outline.is_null()) {
        return;
    }
    require(outline.is_object(), "PCB outline must be an object");
    require(string_field(outline, "kind") == "polygon", "PCB outline kind must be polygon");
    const auto &vertices_json = array_field(outline, "vertices");
    auto vertices = std::vector<BoardPoint>{};
    vertices.reserve(vertices_json.size());
    for (const auto &vertex : vertices_json) {
        vertices.push_back(board_point(vertex));
    }
    board.set_outline(BoardOutline{std::move(vertices)});
}

void PcbBoardReader::read_rules(Board &board, const nlohmann::json &board_json) const {
    const auto *rules = optional_field(board_json, "rules");
    if (rules == nullptr) {
        return;
    }
    require(rules->is_object(), "PCB board rules must be an object");
    board.set_design_rules(BoardDesignRules{
        number_field(*rules, "copper_clearance_mm"),
        number_field(*rules, "minimum_track_width_mm"),
        number_field(*rules, "minimum_via_drill_diameter_mm"),
        number_field(*rules, "minimum_via_annular_diameter_mm"),
        number_field(*rules, "board_outline_clearance_mm"),
    });
}

void PcbBoardReader::read_footprint_definitions(Board &board,
                                                const nlohmann::json &board_json) const {
    const auto &definitions = array_field(board_json, "footprint_definitions");
    for (std::size_t index = 0; index < definitions.size(); ++index) {
        const auto &definition = definitions[index];
        require(definition.is_object(), "PCB footprint definition must be an object");
        const auto expected = FootprintDefId{index};
        require_sequential_id(definition, "id", expected,
                              "PCB footprint definition IDs must be sequential");

        const auto &pads_json = array_field(definition, "pads");
        auto pads = std::vector<FootprintPad>{};
        pads.reserve(pads_json.size());
        for (std::size_t pad_index = 0; pad_index < pads_json.size(); ++pad_index) {
            require(pads_json[pad_index].is_object(), "PCB footprint pad must be an object");
            pads.push_back(footprint_pad(pads_json[pad_index], FootprintPadId{pad_index}));
        }

        const auto id = board.cache_footprint_definition(
            FootprintDefinition{footprint_ref(object_field(definition, "ref")), std::move(pads)});
        require(id == expected, "PCB footprint definition IDs must be sequential");
    }
}

void PcbBoardReader::read_placements(Board &board, const nlohmann::json &board_json) const {
    const auto &placements = array_field(board_json, "placements");
    for (std::size_t index = 0; index < placements.size(); ++index) {
        const auto &placement_json = placements[index];
        require(placement_json.is_object(), "PCB component placement must be an object");
        const auto expected = ComponentPlacementId{index};
        require_sequential_id(placement_json, "id", expected,
                              "PCB component placement IDs must be sequential");
        const auto component = typed_id<ComponentId>(placement_json, "component");
        if (component.index() >= circuit_.component_count()) {
            throw std::logic_error{"PCB placement references missing component"};
        }
        validate_placement_footprint(board, component, placement_json);
        const auto id = board.place_component(ComponentPlacement{
            component,
            board_point(field(placement_json, "position")),
            BoardRotation::degrees(number_field(placement_json, "rotation_deg")),
            board_side_from_name(string_field(placement_json, "side")),
            bool_field(placement_json, "locked"),
        });
        require(id == expected, "PCB component placement IDs must be sequential");
    }
}

void PcbBoardReader::read_tracks(Board &board, const nlohmann::json &board_json) const {
    const auto *tracks = optional_field(board_json, "tracks");
    if (tracks == nullptr) {
        return;
    }
    require(tracks->is_array(), "Expected array field: tracks");
    for (std::size_t index = 0; index < tracks->size(); ++index) {
        const auto &track_json = (*tracks)[index];
        require(track_json.is_object(), "PCB track must be an object");
        const auto expected = BoardTrackId{index};
        require_sequential_id(track_json, "id", expected, "PCB track IDs must be sequential");

        const auto net = typed_id<NetId>(track_json, "net");
        if (net.index() >= circuit_.net_count()) {
            throw std::logic_error{"PCB track references missing net"};
        }
        const auto layer = typed_id<BoardLayerId>(track_json, "layer");
        if (layer.index() >= board.layer_count()) {
            throw std::logic_error{"PCB track references missing board layer"};
        }

        const auto &points_json = array_field(track_json, "points");
        auto points = std::vector<BoardPoint>{};
        points.reserve(points_json.size());
        for (const auto &point_json : points_json) {
            points.push_back(board_point(point_json));
        }

        const auto id = board.add_track(
            BoardTrack{net, layer, std::move(points), number_field(track_json, "width_mm")});
        require(id == expected, "PCB track IDs must be sequential");
    }
}

void PcbBoardReader::read_vias(Board &board, const nlohmann::json &board_json) const {
    const auto *vias = optional_field(board_json, "vias");
    if (vias == nullptr) {
        return;
    }
    require(vias->is_array(), "Expected array field: vias");
    for (std::size_t index = 0; index < vias->size(); ++index) {
        const auto &via_json = (*vias)[index];
        require(via_json.is_object(), "PCB via must be an object");
        const auto expected = BoardViaId{index};
        require_sequential_id(via_json, "id", expected, "PCB via IDs must be sequential");

        const auto net = typed_id<NetId>(via_json, "net");
        if (net.index() >= circuit_.net_count()) {
            throw std::logic_error{"PCB via references missing net"};
        }
        const auto start_layer = typed_id<BoardLayerId>(via_json, "start_layer");
        const auto end_layer = typed_id<BoardLayerId>(via_json, "end_layer");
        if (start_layer.index() >= board.layer_count() ||
            end_layer.index() >= board.layer_count()) {
            throw std::logic_error{"PCB via references missing board layer"};
        }

        const auto id = board.add_via(BoardVia{
            net,
            board_point(field(via_json, "position")),
            start_layer,
            end_layer,
            number_field(via_json, "drill_diameter_mm"),
            number_field(via_json, "annular_diameter_mm"),
        });
        require(id == expected, "PCB via IDs must be sequential");
    }
}

[[nodiscard]] std::vector<BoardLayerId>
PcbBoardReader::read_board_layers(const Board &board, const nlohmann::json &object,
                                  const char *name, const std::string &missing_message) const {
    const auto &layers_json = array_field(object, name);
    auto layers = std::vector<BoardLayerId>{};
    layers.reserve(layers_json.size());
    for (const auto &layer_json : layers_json) {
        require(layer_json.is_string(), "PCB board primitive layer must be a string");
        const auto layer = decode_local_id<BoardLayerId>(layer_json.get<std::string>());
        if (layer.index() >= board.layer_count()) {
            throw std::logic_error{missing_message};
        }
        layers.push_back(layer);
    }
    return layers;
}

void PcbBoardReader::read_zones(Board &board, const nlohmann::json &board_json) const {
    const auto *zones = optional_field(board_json, "zones");
    if (zones == nullptr) {
        return;
    }
    require(zones->is_array(), "Expected array field: zones");
    for (std::size_t index = 0; index < zones->size(); ++index) {
        const auto &zone_json = (*zones)[index];
        require(zone_json.is_object(), "PCB zone must be an object");
        const auto expected = BoardZoneId{index};
        require_sequential_id(zone_json, "id", expected, "PCB zone IDs must be sequential");

        const auto net_value = nullable_string_field(zone_json, "net");
        auto net = std::optional<NetId>{};
        if (net_value.has_value()) {
            net = decode_local_id<NetId>(net_value.value());
            if (net->index() >= circuit_.net_count()) {
                throw std::logic_error{"PCB zone references missing net"};
            }
        }

        const auto id = board.add_zone(BoardZone{
            board_points(field(zone_json, "outline")),
            read_board_layers(board, zone_json, "layers",
                              "PCB zone references missing board layer"),
            net,
            board_zone_fill_from_name(string_field(zone_json, "fill")),
            int_field(zone_json, "priority"),
        });
        require(id == expected, "PCB zone IDs must be sequential");
    }
}

void PcbBoardReader::read_keepouts(Board &board, const nlohmann::json &board_json) const {
    const auto *keepouts = optional_field(board_json, "keepouts");
    if (keepouts == nullptr) {
        return;
    }
    require(keepouts->is_array(), "Expected array field: keepouts");
    for (std::size_t index = 0; index < keepouts->size(); ++index) {
        const auto &keepout_json = (*keepouts)[index];
        require(keepout_json.is_object(), "PCB keepout must be an object");
        const auto expected = BoardKeepoutId{index};
        require_sequential_id(keepout_json, "id", expected, "PCB keepout IDs must be sequential");

        const auto &restrictions_json = array_field(keepout_json, "restrictions");
        auto restrictions = std::vector<BoardKeepoutRestriction>{};
        restrictions.reserve(restrictions_json.size());
        for (const auto &restriction_json : restrictions_json) {
            require(restriction_json.is_string(), "PCB keepout restriction must be a string");
            restrictions.push_back(
                board_keepout_restriction_from_name(restriction_json.get<std::string>()));
        }

        const auto id = board.add_keepout(BoardKeepout{
            board_points(field(keepout_json, "outline")),
            read_board_layers(board, keepout_json, "layers",
                              "PCB keepout references missing board layer"),
            std::move(restrictions),
        });
        require(id == expected, "PCB keepout IDs must be sequential");
    }
}

void PcbBoardReader::read_texts(Board &board, const nlohmann::json &board_json) const {
    const auto *texts = optional_field(board_json, "texts");
    if (texts == nullptr) {
        return;
    }
    require(texts->is_array(), "Expected array field: texts");
    for (std::size_t index = 0; index < texts->size(); ++index) {
        const auto &text_json = (*texts)[index];
        require(text_json.is_object(), "PCB text must be an object");
        const auto expected = BoardTextId{index};
        require_sequential_id(text_json, "id", expected, "PCB text IDs must be sequential");
        const auto layer = typed_id<BoardLayerId>(text_json, "layer");
        if (layer.index() >= board.layer_count()) {
            throw std::logic_error{"PCB text references missing board layer"};
        }

        const auto id = board.add_text(BoardText{
            string_field(text_json, "text"),
            board_point(field(text_json, "position")),
            BoardRotation::degrees(number_field(text_json, "rotation_deg")),
            layer,
            number_field(text_json, "size_mm"),
            bool_field(text_json, "locked"),
        });
        require(id == expected, "PCB text IDs must be sequential");
    }
}

void PcbBoardReader::validate_placement_footprint(const Board &board, ComponentId component,
                                                  const nlohmann::json &placement_json) const {
    const auto footprint = nullable_string_field(placement_json, "footprint");
    if (!footprint.has_value()) {
        return;
    }
    const auto footprint_id = decode_local_id<FootprintDefId>(footprint.value());
    if (footprint_id.index() >= board.footprint_definition_count()) {
        throw std::logic_error{"PCB placement references missing footprint definition"};
    }
    const auto &selected_part = circuit_.selected_physical_part(component);
    require(selected_part.has_value(), "PCB placement footprint requires selected physical part");
    require(board.footprint_definition(footprint_id).ref() == selected_part->footprint(),
            "PCB placement footprint does not match selected physical part");
}

[[nodiscard]] FootprintLibrary PcbBoardReader::cached_footprint_library(const Board &board) const {
    auto library = FootprintLibrary{};
    for (std::size_t index = 0; index < board.footprint_definition_count(); ++index) {
        library.add(board.footprint_definition(FootprintDefId{index}));
    }
    return library;
}

void PcbBoardReader::validate_viewer_cache(const Board &board) const {
    const auto *viewer = optional_field(document_, "viewer");
    if (viewer == nullptr) {
        return;
    }
    require(viewer->is_object(), "PCB viewer cache must be an object");
    const auto &pad_resolutions = array_field(*viewer, "pad_resolutions");
    const auto footprint_library = cached_footprint_library(board);
    const auto expected_resolutions = board.resolve_pads(footprint_library);
    require(pad_resolutions.size() == expected_resolutions.size(),
            "PCB viewer pad resolutions must match resolved pads");

    for (std::size_t index = 0; index < pad_resolutions.size(); ++index) {
        const auto &pad_resolution = pad_resolutions[index];
        require(pad_resolution.is_object(), "PCB viewer pad resolution must be an object");
        validate_viewer_pad_resolution(board, expected_resolutions[index], pad_resolution);
    }

    const auto *diagnostics = optional_field(*viewer, "diagnostics");
    if (diagnostics != nullptr) {
        validate_viewer_diagnostics(board, *diagnostics);
    }
}

void PcbBoardReader::validate_viewer_pad_resolution(const Board &board,
                                                    const PadResolution &expected,
                                                    const nlohmann::json &pad_resolution) const {
    const auto placement = typed_id<ComponentPlacementId>(pad_resolution, "placement");
    if (placement.index() >= board.placement_count()) {
        throw std::logic_error{"PCB viewer pad resolution references missing placement"};
    }
    const auto component = typed_id<ComponentId>(pad_resolution, "component");
    if (component.index() >= circuit_.component_count()) {
        throw std::logic_error{"PCB viewer pad resolution references missing component"};
    }
    require(component == board.placement(placement).component(),
            "PCB viewer pad resolution component does not match placement");

    const auto footprint = nullable_string_field(pad_resolution, "footprint");
    require(footprint.has_value(), "PCB viewer pad resolution requires a footprint definition");
    const auto footprint_id = decode_local_id<FootprintDefId>(footprint.value());
    if (footprint_id.index() >= board.footprint_definition_count()) {
        throw std::logic_error{"PCB viewer pad resolution references missing footprint definition"};
    }
    const auto &selected_part = circuit_.selected_physical_part(component);
    require(selected_part.has_value(),
            "PCB viewer pad resolution footprint requires selected physical part");
    require(board.footprint_definition(footprint_id).ref() == selected_part->footprint(),
            "PCB viewer pad resolution footprint does not match selected physical part");

    const auto pad = typed_id<FootprintPadId>(pad_resolution, "pad");
    if (pad.index() >= board.footprint_definition(footprint_id).pad_count()) {
        throw std::logic_error{"PCB viewer pad resolution references missing footprint pad"};
    }
    require(placement == expected.placement() && pad == expected.pad(),
            "PCB viewer pad resolution order does not match resolved pads");
    require(string_field(pad_resolution, "id") == pcb_pad_projection_id(placement, pad),
            "PCB viewer pad resolution id does not match placement and pad");
    require(string_field(pad_resolution, "label") == expected.pad_label(),
            "PCB viewer pad resolution label does not match footprint pad");
    require(board_point(field(pad_resolution, "position")) == expected.position(),
            "PCB viewer pad resolution position does not match resolved pad");

    const auto pin = nullable_string_field(pad_resolution, "pin");
    if (pin.has_value()) {
        const auto pin_id = decode_local_id<PinId>(pin.value());
        if (pin_id.index() >= circuit_.pin_count()) {
            throw std::logic_error{"PCB viewer pad resolution references missing pin"};
        }
        require(circuit_.pin(pin_id).component() == component,
                "PCB viewer pad resolution pin does not belong to component");
    }

    const auto net = nullable_string_field(pad_resolution, "net");
    if (net.has_value()) {
        const auto net_id = decode_local_id<NetId>(net.value());
        if (net_id.index() >= circuit_.net_count()) {
            throw std::logic_error{"PCB viewer pad resolution references missing net"};
        }
    }

    require(expected.component() == component,
            "PCB viewer pad resolution component does not match selected-part data");
    require(optional_pin(pin) == expected.pin(),
            "PCB viewer pad resolution pin does not match selected-part data");
    require(optional_net(net) == expected.net(),
            "PCB viewer pad resolution net does not match logical circuit");
    require(pad_resolution_status_from_name(string_field(pad_resolution, "status")) ==
                expected.status(),
            "PCB viewer pad resolution status does not match selected-part data");
    validate_viewer_pad_geometry(board.footprint_definition(footprint_id).pad(pad),
                                 object_field(pad_resolution, "geometry"));
}

void PcbBoardReader::validate_viewer_pad_geometry(const FootprintPad &pad,
                                                  const nlohmann::json &geometry) {
    require(footprint_pad_kind_from_name(string_field(geometry, "kind")) == pad.kind(),
            "PCB viewer pad resolution geometry does not match footprint pad");
    require(footprint_pad_shape_from_name(string_field(geometry, "shape")) == pad.shape(),
            "PCB viewer pad resolution geometry does not match footprint pad");
    require(footprint_size(field(geometry, "size")) == pad.size(),
            "PCB viewer pad resolution geometry does not match footprint pad");
    require(footprint_layers(field(geometry, "layers")) == pad.layers(),
            "PCB viewer pad resolution geometry does not match footprint pad");
    require(drill_value(field(geometry, "drill")) == pad.drill(),
            "PCB viewer pad resolution geometry does not match footprint pad");
    require(mechanical_role_value(field(geometry, "mechanical_role")) == pad.mechanical_role(),
            "PCB viewer pad resolution geometry does not match footprint pad");
}

void PcbBoardReader::validate_viewer_diagnostics(const Board &board,
                                                 const nlohmann::json &diagnostics) const {
    require(diagnostics.is_array(), "PCB viewer diagnostics must be an array");
    for (const auto &diagnostic : diagnostics) {
        require(diagnostic.is_object(), "PCB viewer diagnostic must be an object");
        validate_diagnostic_severity(string_field(diagnostic, "severity"));
        static_cast<void>(DiagnosticCode{string_field(diagnostic, "code")});
        static_cast<void>(string_field(diagnostic, "message"));
        const auto &entities = array_field(diagnostic, "entities");
        auto footprint_definitions = std::vector<FootprintDefId>{};
        auto footprint_pads = std::vector<FootprintPadId>{};
        for (const auto &entity : entities) {
            require(entity.is_string(), "PCB viewer diagnostic entity must be a string");
            const auto ref = entity.get<std::string>();
            validate_viewer_diagnostic_ref(board, ref);
            if (const auto id = decode_if_prefixed<FootprintDefId>(ref)) {
                footprint_definitions.push_back(*id);
            }
            if (const auto id = decode_if_prefixed<FootprintPadId>(ref)) {
                footprint_pads.push_back(*id);
            }
        }
        for (const auto pad : footprint_pads) {
            if (!footprint_definitions.empty() &&
                !footprint_pad_exists(board, footprint_definitions, pad)) {
                throw std::logic_error{"PCB viewer diagnostic references missing footprint pad"};
            }
        }
    }
}

[[nodiscard]] bool PcbBoardReader::footprint_pad_exists(
    const Board &board, const std::vector<FootprintDefId> &definitions, FootprintPadId pad) {
    return std::any_of(definitions.begin(), definitions.end(), [&board, pad](auto definition) {
        return pad.index() < board.footprint_definition(definition).pad_count();
    });
}

void PcbBoardReader::validate_diagnostic_severity(const std::string &severity) {
    if (severity == "info" || severity == "warning" || severity == "error") {
        return;
    }
    throw std::logic_error{"Invalid PCB viewer diagnostic severity"};
}

void PcbBoardReader::validate_viewer_diagnostic_ref(const Board &board,
                                                    std::string_view ref) const {
    if (const auto id = decode_if_prefixed<ComponentDefId>(ref)) {
        if (id->index() >= circuit_.component_definition_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing component definition"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<ComponentId>(ref)) {
        if (id->index() >= circuit_.component_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing component"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<PinDefId>(ref)) {
        if (id->index() >= circuit_.pin_definition_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing pin definition"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<PinId>(ref)) {
        if (id->index() >= circuit_.pin_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing pin"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<NetId>(ref)) {
        if (id->index() >= circuit_.net_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing net"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<ModuleDefId>(ref)) {
        if (id->index() >= circuit_.module_definition_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing module definition"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<ModuleInstanceId>(ref)) {
        if (id->index() >= circuit_.module_instance_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing module instance"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<PortDefId>(ref)) {
        if (id->index() >= circuit_.port_definition_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing port definition"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<BoardLayerId>(ref)) {
        if (id->index() >= board.layer_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing board layer"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<BoardFeatureId>(ref)) {
        if (id->index() >= board.feature_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing board feature"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<BoardTrackId>(ref)) {
        if (id->index() >= board.track_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing track"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<BoardViaId>(ref)) {
        if (id->index() >= board.via_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing via"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<BoardZoneId>(ref)) {
        if (id->index() >= board.zone_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing zone"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<BoardKeepoutId>(ref)) {
        if (id->index() >= board.keepout_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing keepout"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<BoardTextId>(ref)) {
        if (id->index() >= board.text_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing text"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<FootprintDefId>(ref)) {
        if (id->index() >= board.footprint_definition_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing footprint definition"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<FootprintPadId>(ref)) {
        auto found = false;
        for (std::size_t index = 0; index < board.footprint_definition_count(); ++index) {
            if (id->index() < board.footprint_definition(FootprintDefId{index}).pad_count()) {
                found = true;
                break;
            }
        }
        if (!found) {
            throw std::logic_error{"PCB viewer diagnostic references missing footprint pad"};
        }
        return;
    }
    if (const auto id = decode_if_prefixed<ComponentPlacementId>(ref)) {
        if (id->index() >= board.placement_count()) {
            throw std::logic_error{"PCB viewer diagnostic references missing placement"};
        }
        return;
    }
    throw std::logic_error{"PCB viewer diagnostic has unsupported entity reference"};
}

[[nodiscard]] std::optional<PinId>
PcbBoardReader::optional_pin(const std::optional<std::string> &id) {
    if (!id.has_value()) {
        return std::nullopt;
    }
    return decode_local_id<PinId>(id.value());
}

[[nodiscard]] std::optional<NetId>
PcbBoardReader::optional_net(const std::optional<std::string> &id) {
    if (!id.has_value()) {
        return std::nullopt;
    }
    return decode_local_id<NetId>(id.value());
}

} // namespace volt::io::detail

namespace {

[[nodiscard]] volt::Board read_pcb_board_document(const volt::Circuit &circuit,
                                                  const nlohmann::json &document) {
    return volt::io::detail::PcbBoardReader{circuit, document}.read();
}

} // namespace

namespace volt::io {

[[nodiscard]] Board read_pcb_board_text(const Circuit &circuit, std::string_view text) {
    const auto document = nlohmann::json::parse(text.begin(), text.end());
    return read_pcb_board_document(circuit, document);
}

[[nodiscard]] Board read_pcb_board(const Circuit &circuit, std::istream &input) {
    const auto text =
        std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    return read_pcb_board_text(circuit, text);
}

} // namespace volt::io
