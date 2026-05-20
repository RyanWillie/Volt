#pragma once

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <volt/io/schematic_writer.hpp>

namespace volt::io {

namespace detail {

/** Internal implementation for loading the v1 schematic projection JSON format. */
class SchematicReader {
  public:
    /** Construct a reader over a parsed JSON document and its logical circuit context. */
    SchematicReader(const nlohmann::json &document, const Circuit &circuit)
        : document_{document}, circuit_{circuit}, schematic_{circuit} {}

    /** Load and structurally validate the document into a schematic projection. */
    [[nodiscard]] Schematic read() {
        require(document_.is_object(), "Schematic document must be an object");
        require_format(document_);
        require_version(document_);

        read_symbol_definitions();
        read_sheets();
        read_symbol_instances();
        read_wire_runs();
        read_net_labels();
        read_junctions();
        read_power_ports();
        read_no_connect_markers();
        read_sheet_ports();
        read_symbol_fields();
        require_sheet_instance_lists_match();
        require_sheet_wire_run_lists_match();
        require_sheet_net_label_lists_match();
        require_sheet_junction_lists_match();
        require_sheet_power_port_lists_match();
        require_sheet_no_connect_marker_lists_match();
        require_sheet_port_lists_match();
        require_sheet_symbol_field_lists_match();

        return std::move(schematic_);
    }

  private:
    static void require(bool condition, const std::string &message) {
        if (!condition) {
            throw std::logic_error{message};
        }
    }

    static const nlohmann::json &field(const nlohmann::json &object, const char *name) {
        require(object.is_object(), "Expected object while reading schematic");
        const auto it = object.find(name);
        require(it != object.end(), std::string{"Missing required field: "} + name);
        return *it;
    }

    static std::string string_field(const nlohmann::json &object, const char *name) {
        const auto &value = field(object, name);
        require(value.is_string(), std::string{"Expected string field: "} + name);
        return value.get<std::string>();
    }

    static std::string optional_string_field(const nlohmann::json &object, const char *name) {
        require(object.is_object(), "Expected object while reading schematic");
        const auto it = object.find(name);
        if (it == object.end()) {
            return {};
        }
        require(it->is_string(), std::string{"Expected string field: "} + name);
        return it->get<std::string>();
    }

    static std::optional<std::string> optional_non_empty_string_field(const nlohmann::json &object,
                                                                      const char *name) {
        require(object.is_object(), "Expected object while reading schematic");
        const auto it = object.find(name);
        if (it == object.end()) {
            return std::nullopt;
        }
        require(it->is_string(), std::string{"Expected string field: "} + name);
        auto value = it->get<std::string>();
        require(!value.empty(), std::string{"Expected non-empty string field: "} + name);
        return value;
    }

    static bool optional_bool_field(const nlohmann::json &object, const char *name, bool fallback) {
        require(object.is_object(), "Expected object while reading schematic");
        const auto it = object.find(name);
        if (it == object.end()) {
            return fallback;
        }
        require(it->is_boolean(), std::string{"Expected boolean field: "} + name);
        return it->get<bool>();
    }

    static double number_field(const nlohmann::json &object, const char *name) {
        const auto &value = field(object, name);
        require(value.is_number(), std::string{"Expected number field: "} + name);
        const auto number = value.get<double>();
        require(std::isfinite(number),
                std::string{"Schematic numeric field must be finite: "} + name);
        return number;
    }

    static const nlohmann::json &array_field(const nlohmann::json &object, const char *name) {
        const auto &value = field(object, name);
        require(value.is_array(), std::string{"Expected array field: "} + name);
        return value;
    }

    static const nlohmann::json &optional_array_field(const nlohmann::json &object,
                                                      const char *name) {
        require(object.is_object(), "Expected object while reading schematic");
        const auto it = object.find(name);
        if (it == object.end()) {
            static const auto empty = nlohmann::json::array();
            return empty;
        }
        require(it->is_array(), std::string{"Expected array field: "} + name);
        return *it;
    }

    static std::size_t positive_size_field(const nlohmann::json &object, const char *name) {
        const auto &value = field(object, name);
        require(value.is_number_unsigned() || value.is_number_integer(),
                std::string{"Expected integer field: "} + name);
        const auto signed_value = value.get<std::int64_t>();
        require(signed_value > 0, std::string{"Expected positive integer field: "} + name);
        return static_cast<std::size_t>(signed_value);
    }

    static void require_format(const nlohmann::json &object) {
        const auto actual = string_field(object, "format");
        require(actual == schematic_format_name(), "Unsupported schematic format: " + actual);
    }

    static void require_version(const nlohmann::json &object) {
        const auto &value = field(object, "version");
        require(value.is_number_integer(), "Expected integer field: version");
        const auto actual = value.get<int>();
        require(actual == schematic_format_version(),
                "Unsupported schematic format version: " + std::to_string(actual));
    }

    static std::size_t local_index(std::string_view id, std::string_view prefix) {
        require(id.rfind(prefix, 0) == 0, "Local ID has the wrong typed prefix");
        const auto suffix = id.substr(prefix.size());
        require(!suffix.empty(), "Local ID must contain an index");
        auto index = std::size_t{0};
        for (const auto character : suffix) {
            require(std::isdigit(static_cast<unsigned char>(character)) != 0,
                    "Local ID index must be numeric");
            const auto digit = static_cast<std::size_t>(character - '0');
            require(index <= (std::numeric_limits<std::size_t>::max() - digit) / std::size_t{10},
                    "Local ID index is too large");
            index = (index * std::size_t{10}) + digit;
        }
        return index;
    }

    static std::string local_id(const nlohmann::json &object, const std::string &prefix,
                                std::set<std::string> &seen) {
        const auto id = string_field(object, "id");
        static_cast<void>(local_index(id, prefix));
        require(seen.insert(id).second, "Duplicate local ID");
        return id;
    }

    template <typename Id>
    [[nodiscard]] static Id resolve(const std::map<std::string, Id> &ids, const std::string &id) {
        const auto it = ids.find(id);
        require(it != ids.end(), "Reference points to a missing local ID");
        return it->second;
    }

    [[nodiscard]] static Point point(const nlohmann::json &object) {
        require(object.is_object(), "Schematic point must be an object");
        return Point{number_field(object, "x"), number_field(object, "y")};
    }

    [[nodiscard]] static SchematicOrientation orientation(const std::string &value) {
        if (value == "Right")
            return SchematicOrientation::Right;
        if (value == "Down")
            return SchematicOrientation::Down;
        if (value == "Left")
            return SchematicOrientation::Left;
        if (value == "Up")
            return SchematicOrientation::Up;
        throw std::logic_error{"Invalid schematic orientation value"};
    }

    [[nodiscard]] static SymbolLineRole symbol_line_role(const std::string &value) {
        if (value.empty() || value == "Normal")
            return SymbolLineRole::Normal;
        if (value == "TerminalLeadStart")
            return SymbolLineRole::TerminalLeadStart;
        if (value == "TerminalLeadEnd")
            return SymbolLineRole::TerminalLeadEnd;
        throw std::logic_error{"Invalid symbol line role"};
    }

    [[nodiscard]] static SheetOrientation sheet_orientation(const std::string &value) {
        if (value == "Portrait")
            return SheetOrientation::Portrait;
        if (value == "Landscape")
            return SheetOrientation::Landscape;
        throw std::logic_error{"Invalid schematic sheet orientation value"};
    }

    [[nodiscard]] static RouteIntent route_intent(const std::string &value) {
        if (value == "Direct")
            return RouteIntent::Direct;
        if (value == "Orthogonal")
            return RouteIntent::Orthogonal;
        throw std::logic_error{"Invalid schematic route intent value"};
    }

    [[nodiscard]] static PowerPortKind power_port_kind(const std::string &value) {
        if (value == "Power")
            return PowerPortKind::Power;
        if (value == "Ground")
            return PowerPortKind::Ground;
        throw std::logic_error{"Invalid schematic power port kind"};
    }

    [[nodiscard]] static SheetPortKind sheet_port_kind(const std::string &value) {
        if (value == "Input")
            return SheetPortKind::Input;
        if (value == "Output")
            return SheetPortKind::Output;
        if (value == "Bidirectional")
            return SheetPortKind::Bidirectional;
        if (value == "OffPage")
            return SheetPortKind::OffPage;
        throw std::logic_error{"Invalid schematic sheet port kind"};
    }

    [[nodiscard]] ComponentId component_id(const std::string &id) const {
        const auto component = ComponentId{local_index(id, "component:")};
        require(component.index() < circuit_.component_count(),
                "Component reference points to a missing logical component: " + id);
        return component;
    }

    [[nodiscard]] NetId net_id(const std::string &id) const {
        const auto net = NetId{local_index(id, "net:")};
        require(net.index() < circuit_.net_count(),
                "Net reference points to a missing logical net: " + id);
        return net;
    }

    [[nodiscard]] PinId pin_id(const std::string &id) const {
        const auto pin = PinId{local_index(id, "pin:")};
        require(pin.index() < circuit_.pin_count(),
                "Pin reference points to a missing logical pin: " + id);
        return pin;
    }

    [[nodiscard]] static std::vector<Point> point_list(const nlohmann::json &array) {
        require(array.is_array(), "Schematic wire points must be an array");
        auto points = std::vector<Point>{};
        points.reserve(array.size());
        for (const auto &point_object : array) {
            points.push_back(point(point_object));
        }
        return points;
    }

    [[nodiscard]] static SymbolPrimitive primitive(const nlohmann::json &object) {
        require(object.is_object(), "Symbol primitive must be an object");
        const auto type = string_field(object, "type");
        if (type == "line") {
            return SymbolLine{point(field(object, "start")), point(field(object, "end")),
                              symbol_line_role(optional_string_field(object, "role"))};
        }
        if (type == "rectangle") {
            return SymbolRectangle{point(field(object, "first_corner")),
                                   point(field(object, "second_corner"))};
        }
        if (type == "circle") {
            return SymbolCircle{point(field(object, "center")), number_field(object, "radius")};
        }
        if (type == "arc") {
            return SymbolArc{point(field(object, "center")), number_field(object, "radius"),
                             number_field(object, "start_degrees"),
                             number_field(object, "sweep_degrees")};
        }
        if (type == "text") {
            return SymbolText{string_field(object, "text"), point(field(object, "anchor")),
                              orientation(string_field(object, "orientation"))};
        }
        throw std::logic_error{"Invalid symbol primitive type"};
    }

    [[nodiscard]] static SheetMargins sheet_margins(const nlohmann::json &object) {
        require(object.is_object(), "Sheet margins must be an object");
        return SheetMargins{number_field(object, "left"), number_field(object, "top"),
                            number_field(object, "right"), number_field(object, "bottom")};
    }

    [[nodiscard]] static SheetFrame sheet_frame(const nlohmann::json &metadata_object) {
        const auto frame_it = metadata_object.find("frame");
        if (frame_it == metadata_object.end()) {
            return SheetFrame{};
        }
        const auto &frame_object = *frame_it;
        require(frame_object.is_object(), "Sheet frame must be an object");
        auto margins = SheetMargins{};
        const auto margins_it = frame_object.find("margins");
        if (margins_it != frame_object.end()) {
            margins = sheet_margins(*margins_it);
        }
        return SheetFrame{optional_bool_field(frame_object, "visible", true), margins};
    }

    [[nodiscard]] static std::optional<SheetCoordinateZones>
    sheet_coordinate_zones(const nlohmann::json &metadata_object) {
        const auto zones_it = metadata_object.find("coordinate_zones");
        if (zones_it == metadata_object.end() || zones_it->is_null()) {
            return std::nullopt;
        }
        const auto &zones_object = *zones_it;
        require(zones_object.is_object(), "Sheet coordinate zones must be an object");
        return SheetCoordinateZones{positive_size_field(zones_object, "columns"),
                                    positive_size_field(zones_object, "rows"),
                                    optional_bool_field(zones_object, "visible", true)};
    }

    [[nodiscard]] static std::optional<SheetGrid>
    sheet_grid(const nlohmann::json &metadata_object) {
        const auto grid_it = metadata_object.find("grid");
        if (grid_it == metadata_object.end() || grid_it->is_null()) {
            return std::nullopt;
        }
        const auto &grid_object = *grid_it;
        require(grid_object.is_object(), "Sheet grid must be an object");
        return SheetGrid{number_field(grid_object, "spacing"),
                         optional_bool_field(grid_object, "visible", true)};
    }

    [[nodiscard]] static SheetMetadata sheet_metadata(const nlohmann::json &sheet_object,
                                                      const std::string &fallback_title) {
        const auto metadata_it = sheet_object.find("metadata");
        if (metadata_it == sheet_object.end()) {
            return SheetMetadata{fallback_title};
        }
        const auto &metadata_object = *metadata_it;
        require(metadata_object.is_object(), "Sheet metadata must be an object");
        const auto &size_object = field(metadata_object, "size");
        require(size_object.is_object(), "Sheet size must be an object");
        auto title_block = std::vector<TitleBlockField>{};
        for (const auto &field_object : optional_array_field(metadata_object, "title_block")) {
            title_block.emplace_back(string_field(field_object, "key"),
                                     string_field(field_object, "value"));
        }
        auto orientation = SheetOrientation::Landscape;
        const auto orientation_value = optional_string_field(metadata_object, "orientation");
        if (!orientation_value.empty()) {
            orientation = sheet_orientation(orientation_value);
        }
        return SheetMetadata{
            string_field(metadata_object, "title"),
            SheetSize{number_field(size_object, "width"), number_field(size_object, "height")},
            std::move(title_block),
            orientation,
            sheet_frame(metadata_object),
            sheet_coordinate_zones(metadata_object),
            sheet_grid(metadata_object)};
    }

    [[nodiscard]] static std::vector<SheetRegionStyleField>
    sheet_region_style(const nlohmann::json &region_object) {
        const auto style_it = region_object.find("style");
        if (style_it == region_object.end() || style_it->is_null()) {
            return {};
        }
        const auto &style_object = *style_it;
        require(style_object.is_object(), "Sheet region style must be an object");
        auto style = std::vector<SheetRegionStyleField>{};
        style.reserve(style_object.size());
        for (auto it = style_object.begin(); it != style_object.end(); ++it) {
            require(it.value().is_string(), "Sheet region style values must be strings");
            style.emplace_back(it.key(), it.value().get<std::string>());
        }
        return style;
    }

    [[nodiscard]] static SheetRegion sheet_region(const nlohmann::json &region_object) {
        require(region_object.is_object(), "Sheet region must be an object");
        const auto &bounds_object = field(region_object, "bounds");
        require(bounds_object.is_object(), "Sheet region bounds must be an object");
        return SheetRegion{
            string_field(region_object, "name"), string_field(region_object, "title"),
            SheetRegionBounds{number_field(bounds_object, "x"), number_field(bounds_object, "y"),
                              number_field(bounds_object, "width"),
                              number_field(bounds_object, "height")},
            sheet_region_style(region_object)};
    }

    [[nodiscard]] std::optional<std::size_t> authored_region(SheetId sheet,
                                                             const nlohmann::json &object) const {
        const auto name = optional_string_field(object, "authored_region");
        if (name.empty()) {
            return std::nullopt;
        }
        const auto region = schematic_.sheet_region_by_name(sheet, name);
        require(region.has_value(), "Authored region reference points to a missing sheet region");
        return region.value();
    }

    static void append_sheet_references(const nlohmann::json &sheet_object, const char *field_name,
                                        std::vector<std::string> &references,
                                        const std::string &error_message) {
        for (const auto &reference : optional_array_field(sheet_object, field_name)) {
            require(reference.is_string(), error_message);
            references.push_back(reference.get<std::string>());
        }
    }

    void read_symbol_definitions() {
        auto seen = std::set<std::string>{};
        for (const auto &symbol_object : array_field(document_, "symbol_definitions")) {
            const auto id = local_id(symbol_object, "symbol_def:", seen);
            auto symbol = SymbolDefinition{string_field(symbol_object, "name")};
            for (const auto &pin_object : array_field(symbol_object, "pins")) {
                symbol.add_pin(SymbolPin{string_field(pin_object, "name"),
                                         string_field(pin_object, "number"),
                                         point(field(pin_object, "anchor")),
                                         orientation(string_field(pin_object, "orientation"))});
            }
            for (const auto &primitive_object : array_field(symbol_object, "primitives")) {
                symbol.add_primitive(primitive(primitive_object));
            }
            symbol_def_ids_.emplace(id, schematic_.add_symbol_definition(std::move(symbol)));
        }
    }

    void read_sheets() {
        auto seen = std::set<std::string>{};
        for (const auto &sheet_object : array_field(document_, "sheets")) {
            const auto id = local_id(sheet_object, "sheet:", seen);
            const auto name = string_field(sheet_object, "name");
            const auto sheet =
                schematic_.add_sheet(Sheet{name, sheet_metadata(sheet_object, name)});
            sheet_ids_.emplace(id, sheet);
            for (const auto &region_object : optional_array_field(sheet_object, "regions")) {
                static_cast<void>(schematic_.add_sheet_region(sheet, sheet_region(region_object)));
            }
            auto instances = std::vector<std::string>{};
            for (const auto &instance : array_field(sheet_object, "symbol_instances")) {
                require(instance.is_string(), "Sheet symbol instance reference must be a string");
                instances.push_back(instance.get<std::string>());
            }
            expected_sheet_instances_.emplace_back(sheet, std::move(instances));
            auto wire_runs = std::vector<std::string>{};
            append_sheet_references(sheet_object, "wire_runs", wire_runs,
                                    "Sheet wire run reference must be a string");
            expected_sheet_wire_runs_.emplace_back(sheet, std::move(wire_runs));
            auto net_labels = std::vector<std::string>{};
            append_sheet_references(sheet_object, "net_labels", net_labels,
                                    "Sheet net label reference must be a string");
            expected_sheet_net_labels_.emplace_back(sheet, std::move(net_labels));
            auto junctions = std::vector<std::string>{};
            append_sheet_references(sheet_object, "junctions", junctions,
                                    "Sheet junction reference must be a string");
            expected_sheet_junctions_.emplace_back(sheet, std::move(junctions));
            auto power_ports = std::vector<std::string>{};
            append_sheet_references(sheet_object, "power_ports", power_ports,
                                    "Sheet power port reference must be a string");
            expected_sheet_power_ports_.emplace_back(sheet, std::move(power_ports));
            auto no_connect_markers = std::vector<std::string>{};
            append_sheet_references(sheet_object, "no_connect_markers", no_connect_markers,
                                    "Sheet no-connect marker reference must be a string");
            expected_sheet_no_connect_markers_.emplace_back(sheet, std::move(no_connect_markers));
            auto sheet_ports = std::vector<std::string>{};
            append_sheet_references(sheet_object, "sheet_ports", sheet_ports,
                                    "Sheet port reference must be a string");
            expected_sheet_ports_.emplace_back(sheet, std::move(sheet_ports));
            auto symbol_fields = std::vector<std::string>{};
            append_sheet_references(sheet_object, "symbol_fields", symbol_fields,
                                    "Sheet symbol field reference must be a string");
            expected_sheet_symbol_fields_.emplace_back(sheet, std::move(symbol_fields));
        }
    }

    void read_symbol_instances() {
        auto seen = std::set<std::string>{};
        for (const auto &instance_object : array_field(document_, "symbol_instances")) {
            const auto id = local_id(instance_object, "symbol_instance:", seen);
            const auto sheet = resolve(sheet_ids_, string_field(instance_object, "sheet"));
            const auto symbol =
                resolve(symbol_def_ids_, string_field(instance_object, "symbol_definition"));
            const auto component = component_id(string_field(instance_object, "component"));
            const auto instance = schematic_.place_symbol(
                sheet, SymbolInstance{
                           symbol, component, point(field(instance_object, "position")),
                           orientation(string_field(instance_object, "orientation")),
                           authored_region(sheet, instance_object),
                           optional_non_empty_string_field(instance_object, "reference_label")});
            symbol_instance_ids_.emplace(id, instance);
        }
    }

    void read_wire_runs() {
        auto seen = std::set<std::string>{};
        for (const auto &wire_object : optional_array_field(document_, "wire_runs")) {
            const auto id = local_id(wire_object, "wire_run:", seen);
            const auto sheet = resolve(sheet_ids_, string_field(wire_object, "sheet"));
            const auto net = net_id(string_field(wire_object, "net"));
            auto intent = RouteIntent::Direct;
            const auto intent_it = wire_object.find("route_intent");
            if (intent_it != wire_object.end()) {
                require(intent_it->is_string(), "Expected string field: route_intent");
                intent = route_intent(intent_it->get<std::string>());
            }
            const auto wire = schematic_.add_wire_run(
                sheet, WireRun{net, point_list(field(wire_object, "points")), intent,
                               authored_region(sheet, wire_object)});
            wire_run_ids_.emplace(id, wire);
        }
    }

    void read_net_labels() {
        auto seen = std::set<std::string>{};
        for (const auto &label_object : optional_array_field(document_, "net_labels")) {
            const auto id = local_id(label_object, "net_label:", seen);
            const auto sheet = resolve(sheet_ids_, string_field(label_object, "sheet"));
            const auto net = net_id(string_field(label_object, "net"));
            const auto label = schematic_.add_net_label(
                sheet, NetLabel{net, point(field(label_object, "position")),
                                orientation(string_field(label_object, "orientation")),
                                authored_region(sheet, label_object),
                                optional_non_empty_string_field(label_object, "label")});
            net_label_ids_.emplace(id, label);
        }
    }

    void read_junctions() {
        auto seen = std::set<std::string>{};
        for (const auto &junction_object : optional_array_field(document_, "junctions")) {
            const auto id = local_id(junction_object, "junction:", seen);
            const auto sheet = resolve(sheet_ids_, string_field(junction_object, "sheet"));
            const auto net = net_id(string_field(junction_object, "net"));
            const auto junction = schematic_.add_junction(
                sheet, Junction{net, point(field(junction_object, "position")),
                                authored_region(sheet, junction_object)});
            junction_ids_.emplace(id, junction);
        }
    }

    void read_power_ports() {
        auto seen = std::set<std::string>{};
        for (const auto &port_object : optional_array_field(document_, "power_ports")) {
            const auto id = local_id(port_object, "power_port:", seen);
            const auto sheet = resolve(sheet_ids_, string_field(port_object, "sheet"));
            const auto net = net_id(string_field(port_object, "net"));
            const auto port = schematic_.add_power_port(
                sheet, PowerPort{net, power_port_kind(string_field(port_object, "kind")),
                                 point(field(port_object, "position")),
                                 orientation(string_field(port_object, "orientation")),
                                 authored_region(sheet, port_object),
                                 optional_non_empty_string_field(port_object, "label")});
            power_port_ids_.emplace(id, port);
        }
    }

    void read_no_connect_markers() {
        auto seen = std::set<std::string>{};
        for (const auto &marker_object : optional_array_field(document_, "no_connect_markers")) {
            const auto id = local_id(marker_object, "no_connect_marker:", seen);
            const auto sheet = resolve(sheet_ids_, string_field(marker_object, "sheet"));
            const auto pin = pin_id(string_field(marker_object, "pin"));
            const auto marker = schematic_.add_no_connect_marker(
                sheet, NoConnectMarker{pin, point(field(marker_object, "position")),
                                       orientation(string_field(marker_object, "orientation")),
                                       optional_string_field(marker_object, "reason"),
                                       authored_region(sheet, marker_object)});
            no_connect_marker_ids_.emplace(id, marker);
        }
    }

    void read_sheet_ports() {
        auto seen = std::set<std::string>{};
        for (const auto &port_object : optional_array_field(document_, "sheet_ports")) {
            const auto id = local_id(port_object, "sheet_port:", seen);
            const auto sheet = resolve(sheet_ids_, string_field(port_object, "sheet"));
            const auto net = net_id(string_field(port_object, "net"));
            const auto port = schematic_.add_sheet_port(
                sheet, SheetPort{net, string_field(port_object, "name"),
                                 sheet_port_kind(string_field(port_object, "kind")),
                                 point(field(port_object, "position")),
                                 orientation(string_field(port_object, "orientation")),
                                 authored_region(sheet, port_object)});
            sheet_port_ids_.emplace(id, port);
        }
    }

    void read_symbol_fields() {
        auto seen = std::set<std::string>{};
        for (const auto &field_object : optional_array_field(document_, "symbol_fields")) {
            const auto id = local_id(field_object, "symbol_field:", seen);
            const auto sheet = resolve(sheet_ids_, string_field(field_object, "sheet"));
            const auto instance =
                resolve(symbol_instance_ids_, string_field(field_object, "symbol_instance"));
            const auto field_id = schematic_.add_symbol_field(
                sheet, SymbolField{instance, string_field(field_object, "name"),
                                   string_field(field_object, "value"),
                                   point(field(field_object, "position")),
                                   orientation(string_field(field_object, "orientation")),
                                   authored_region(sheet, field_object)});
            symbol_field_ids_.emplace(id, field_id);
        }
    }

    void require_sheet_instance_lists_match() const {
        for (const auto &[sheet, expected_ids] : expected_sheet_instances_) {
            auto expected = std::vector<SymbolInstanceId>{};
            expected.reserve(expected_ids.size());
            for (const auto &id : expected_ids) {
                expected.push_back(resolve(symbol_instance_ids_, id));
            }
            require(schematic_.sheet(sheet).symbol_instances() == expected,
                    "Sheet symbol instance list does not match placed instances");
        }
    }

    void require_sheet_wire_run_lists_match() const {
        for (const auto &[sheet, expected_ids] : expected_sheet_wire_runs_) {
            auto expected = std::vector<WireRunId>{};
            expected.reserve(expected_ids.size());
            for (const auto &id : expected_ids) {
                expected.push_back(resolve(wire_run_ids_, id));
            }
            require(schematic_.sheet(sheet).wire_runs() == expected,
                    "Sheet wire run list does not match placed wires");
        }
    }

    void require_sheet_net_label_lists_match() const {
        for (const auto &[sheet, expected_ids] : expected_sheet_net_labels_) {
            auto expected = std::vector<NetLabelId>{};
            expected.reserve(expected_ids.size());
            for (const auto &id : expected_ids) {
                expected.push_back(resolve(net_label_ids_, id));
            }
            require(schematic_.sheet(sheet).net_labels() == expected,
                    "Sheet net label list does not match placed labels");
        }
    }

    void require_sheet_junction_lists_match() const {
        for (const auto &[sheet, expected_ids] : expected_sheet_junctions_) {
            auto expected = std::vector<JunctionId>{};
            expected.reserve(expected_ids.size());
            for (const auto &id : expected_ids) {
                expected.push_back(resolve(junction_ids_, id));
            }
            require(schematic_.sheet(sheet).junctions() == expected,
                    "Sheet junction list does not match placed junctions");
        }
    }

    void require_sheet_power_port_lists_match() const {
        for (const auto &[sheet, expected_ids] : expected_sheet_power_ports_) {
            auto expected = std::vector<PowerPortId>{};
            expected.reserve(expected_ids.size());
            for (const auto &id : expected_ids) {
                expected.push_back(resolve(power_port_ids_, id));
            }
            require(schematic_.sheet(sheet).power_ports() == expected,
                    "Sheet power port list does not match placed ports");
        }
    }

    void require_sheet_no_connect_marker_lists_match() const {
        for (const auto &[sheet, expected_ids] : expected_sheet_no_connect_markers_) {
            auto expected = std::vector<NoConnectMarkerId>{};
            expected.reserve(expected_ids.size());
            for (const auto &id : expected_ids) {
                expected.push_back(resolve(no_connect_marker_ids_, id));
            }
            require(schematic_.sheet(sheet).no_connect_markers() == expected,
                    "Sheet no-connect marker list does not match placed markers");
        }
    }

    void require_sheet_port_lists_match() const {
        for (const auto &[sheet, expected_ids] : expected_sheet_ports_) {
            auto expected = std::vector<SheetPortId>{};
            expected.reserve(expected_ids.size());
            for (const auto &id : expected_ids) {
                expected.push_back(resolve(sheet_port_ids_, id));
            }
            require(schematic_.sheet(sheet).sheet_ports() == expected,
                    "Sheet port list does not match placed ports");
        }
    }

    void require_sheet_symbol_field_lists_match() const {
        for (const auto &[sheet, expected_ids] : expected_sheet_symbol_fields_) {
            auto expected = std::vector<SymbolFieldId>{};
            expected.reserve(expected_ids.size());
            for (const auto &id : expected_ids) {
                expected.push_back(resolve(symbol_field_ids_, id));
            }
            require(schematic_.sheet(sheet).symbol_fields() == expected,
                    "Sheet symbol field list does not match placed fields");
        }
    }

    const nlohmann::json &document_;
    const Circuit &circuit_;
    Schematic schematic_;
    std::map<std::string, SymbolDefId> symbol_def_ids_;
    std::map<std::string, SheetId> sheet_ids_;
    std::map<std::string, SymbolInstanceId> symbol_instance_ids_;
    std::map<std::string, WireRunId> wire_run_ids_;
    std::map<std::string, NetLabelId> net_label_ids_;
    std::map<std::string, JunctionId> junction_ids_;
    std::map<std::string, PowerPortId> power_port_ids_;
    std::map<std::string, NoConnectMarkerId> no_connect_marker_ids_;
    std::map<std::string, SheetPortId> sheet_port_ids_;
    std::map<std::string, SymbolFieldId> symbol_field_ids_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_instances_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_wire_runs_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_net_labels_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_junctions_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_power_ports_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_no_connect_markers_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_ports_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_symbol_fields_;
};

} // namespace detail

/** Read a schematic projection from parsed JSON, rejecting structurally invalid input. */
[[nodiscard]] inline Schematic read_schematic(const nlohmann::json &document,
                                              const Circuit &circuit) {
    return detail::SchematicReader{document, circuit}.read();
}

/** Read a schematic projection from a JSON string, rejecting structurally invalid input. */
[[nodiscard]] inline Schematic read_schematic_text(std::string_view text, const Circuit &circuit) {
    return read_schematic(nlohmann::json::parse(text.begin(), text.end()), circuit);
}

/** Read a schematic projection from a JSON stream, rejecting structurally invalid input. */
[[nodiscard]] inline Schematic read_schematic(std::istream &input, const Circuit &circuit) {
    auto buffer = std::ostringstream{};
    buffer << input.rdbuf();
    return read_schematic_text(buffer.str(), circuit);
}

/** Read a schematic document from parsed JSON, rejecting structurally invalid input. */
[[nodiscard]] inline SchematicDocument read_schematic_document(const nlohmann::json &document,
                                                               const Circuit &circuit) {
    return SchematicDocument{read_schematic(document, circuit)};
}

/** Read a schematic document from a JSON string, rejecting structurally invalid input. */
[[nodiscard]] inline SchematicDocument read_schematic_document_text(std::string_view text,
                                                                    const Circuit &circuit) {
    return SchematicDocument{read_schematic_text(text, circuit)};
}

/** Read a schematic document from a JSON stream, rejecting structurally invalid input. */
[[nodiscard]] inline SchematicDocument read_schematic_document(std::istream &input,
                                                               const Circuit &circuit) {
    return SchematicDocument{read_schematic(input, circuit)};
}

} // namespace volt::io
