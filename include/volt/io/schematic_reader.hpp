#pragma once

#include <cctype>
#include <cmath>
#include <cstddef>
#include <istream>
#include <limits>
#include <map>
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
        require_sheet_instance_lists_match();
        require_sheet_wire_run_lists_match();
        require_sheet_net_label_lists_match();

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
            return SymbolLine{point(field(object, "start")), point(field(object, "end"))};
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
            const auto sheet = schematic_.add_sheet(Sheet{string_field(sheet_object, "name")});
            sheet_ids_.emplace(id, sheet);
            auto instances = std::vector<std::string>{};
            for (const auto &instance : array_field(sheet_object, "symbol_instances")) {
                require(instance.is_string(), "Sheet symbol instance reference must be a string");
                instances.push_back(instance.get<std::string>());
            }
            expected_sheet_instances_.emplace_back(sheet, std::move(instances));
            auto wire_runs = std::vector<std::string>{};
            for (const auto &wire : optional_array_field(sheet_object, "wire_runs")) {
                require(wire.is_string(), "Sheet wire run reference must be a string");
                wire_runs.push_back(wire.get<std::string>());
            }
            expected_sheet_wire_runs_.emplace_back(sheet, std::move(wire_runs));
            auto net_labels = std::vector<std::string>{};
            for (const auto &label : optional_array_field(sheet_object, "net_labels")) {
                require(label.is_string(), "Sheet net label reference must be a string");
                net_labels.push_back(label.get<std::string>());
            }
            expected_sheet_net_labels_.emplace_back(sheet, std::move(net_labels));
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
                sheet, SymbolInstance{symbol, component, point(field(instance_object, "position")),
                                      orientation(string_field(instance_object, "orientation"))});
            symbol_instance_ids_.emplace(id, instance);
        }
    }

    void read_wire_runs() {
        auto seen = std::set<std::string>{};
        for (const auto &wire_object : optional_array_field(document_, "wire_runs")) {
            const auto id = local_id(wire_object, "wire_run:", seen);
            const auto sheet = resolve(sheet_ids_, string_field(wire_object, "sheet"));
            const auto net = net_id(string_field(wire_object, "net"));
            const auto wire = schematic_.add_wire_run(
                sheet, WireRun{net, point_list(field(wire_object, "points"))});
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
                                orientation(string_field(label_object, "orientation"))});
            net_label_ids_.emplace(id, label);
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

    const nlohmann::json &document_;
    const Circuit &circuit_;
    Schematic schematic_;
    std::map<std::string, SymbolDefId> symbol_def_ids_;
    std::map<std::string, SheetId> sheet_ids_;
    std::map<std::string, SymbolInstanceId> symbol_instance_ids_;
    std::map<std::string, WireRunId> wire_run_ids_;
    std::map<std::string, NetLabelId> net_label_ids_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_instances_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_wire_runs_;
    std::vector<std::pair<SheetId, std::vector<std::string>>> expected_sheet_net_labels_;
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

} // namespace volt::io
