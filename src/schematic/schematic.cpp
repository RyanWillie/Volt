#include <volt/schematic/schematic.hpp>

#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <volt/circuit/queries.hpp>

namespace volt {

SheetSize::SheetSize(double width, double height) : width_{width}, height_{height} {
    if (!std::isfinite(width_) || !std::isfinite(height_) || width_ <= 0.0 || height_ <= 0.0) {
        throw std::invalid_argument{"Sheet size dimensions must be finite and positive"};
    }
}
SheetMargins::SheetMargins(double left, double top, double right, double bottom)
    : left_{left}, top_{top}, right_{right}, bottom_{bottom} {
    if (!std::isfinite(left_) || !std::isfinite(top_) || !std::isfinite(right_) ||
        !std::isfinite(bottom_) || left_ < 0.0 || top_ < 0.0 || right_ < 0.0 || bottom_ < 0.0) {
        throw std::invalid_argument{"Sheet margins must be finite and non-negative"};
    }
}
SheetFrame::SheetFrame(bool visible, SheetMargins margins) : visible_{visible}, margins_{margins} {}
SheetCoordinateZones::SheetCoordinateZones(std::size_t columns, std::size_t rows, bool visible)
    : columns_{columns}, rows_{rows}, visible_{visible} {
    if (columns_ == 0U || rows_ == 0U) {
        throw std::invalid_argument{"Sheet coordinate zones must have positive counts"};
    }
}
SheetGrid::SheetGrid(double spacing, bool visible) : spacing_{spacing}, visible_{visible} {
    if (!std::isfinite(spacing_) || spacing_ <= 0.0) {
        throw std::invalid_argument{"Sheet grid spacing must be finite and positive"};
    }
}
TitleBlockField::TitleBlockField(std::string key, std::string value)
    : key_{std::move(key)}, value_{std::move(value)} {
    if (key_.empty()) {
        throw std::invalid_argument{"Title block field key must not be empty"};
    }
    if (value_.empty()) {
        throw std::invalid_argument{"Title block field value must not be empty"};
    }
}
SheetRegionStyleField::SheetRegionStyleField(std::string key, std::string value)
    : key_{std::move(key)}, value_{std::move(value)} {
    if (key_.empty()) {
        throw std::invalid_argument{"Region style field key must not be empty"};
    }
    if (value_.empty()) {
        throw std::invalid_argument{"Region style field value must not be empty"};
    }
}
SheetRegionBounds::SheetRegionBounds(double x, double y, double width, double height)
    : x_{x}, y_{y}, width_{width}, height_{height} {
    if (!std::isfinite(x_) || !std::isfinite(y_) || !std::isfinite(width_) ||
        !std::isfinite(height_) || width_ <= 0.0 || height_ <= 0.0) {
        throw std::invalid_argument{
            "Sheet region bounds must be finite with positive width and height"};
    }
}
SheetRegion::SheetRegion(std::string name, std::string title, SheetRegionBounds bounds,
                         std::vector<SheetRegionStyleField> style)
    : name_{std::move(name)}, title_{std::move(title)}, bounds_{bounds}, style_{std::move(style)} {
    if (name_.empty()) {
        throw std::invalid_argument{"Sheet region name must not be empty"};
    }
    if (title_.empty()) {
        throw std::invalid_argument{"Sheet region title must not be empty"};
    }
}
[[nodiscard]] const std::vector<SheetRegionStyleField> &SheetRegion::style() const noexcept {
    return style_;
}
SheetMetadata::SheetMetadata(std::string title, SheetSize size,
                             std::vector<TitleBlockField> title_block, SheetOrientation orientation,
                             SheetFrame frame, std::optional<SheetCoordinateZones> coordinate_zones,
                             std::optional<SheetGrid> grid)
    : title_{std::move(title)}, size_{size}, orientation_{orientation},
      title_block_{std::move(title_block)}, frame_{frame}, coordinate_zones_{coordinate_zones},
      grid_{grid} {
    if (title_.empty()) {
        throw std::invalid_argument{"Sheet title must not be empty"};
    }
}
[[nodiscard]] const std::vector<TitleBlockField> &SheetMetadata::title_block() const noexcept {
    return title_block_;
}
[[nodiscard]] const std::optional<SheetCoordinateZones> &
SheetMetadata::coordinate_zones() const noexcept {
    return coordinate_zones_;
}
Sheet::Sheet(std::string name) : Sheet{name, SheetMetadata{name}} {}

Sheet::Sheet(std::string name, SheetMetadata metadata)
    : name_{std::move(name)}, metadata_{std::move(metadata)} {
    if (name_.empty()) {
        throw std::invalid_argument{"Sheet name must not be empty"};
    }
}
[[nodiscard]] const std::vector<SymbolInstanceId> &Sheet::symbol_instances() const noexcept {
    return symbol_instances_;
}
[[nodiscard]] const std::vector<PowerPortId> &Sheet::power_ports() const noexcept {
    return power_ports_;
}
[[nodiscard]] const std::vector<NoConnectMarkerId> &Sheet::no_connect_markers() const noexcept {
    return no_connect_markers_;
}
[[nodiscard]] const std::vector<SheetPortId> &Sheet::sheet_ports() const noexcept {
    return sheet_ports_;
}
[[nodiscard]] const std::vector<SymbolFieldId> &Sheet::symbol_fields() const noexcept {
    return symbol_fields_;
}
[[nodiscard]] std::optional<std::size_t> Sheet::region_by_name(const std::string &name) const {
    for (std::size_t index = 0; index < regions_.size(); ++index) {
        if (regions_[index].name() == name) {
            return index;
        }
    }
    return std::nullopt;
}
[[nodiscard]] const SheetRegion &Sheet::region(std::size_t index) const {
    if (index >= regions_.size()) {
        throw std::out_of_range{"Sheet region index does not belong to this sheet"};
    }
    return regions_[index];
}
std::size_t Sheet::add_region(SheetRegion region) {
    regions_.push_back(std::move(region));
    return regions_.size() - 1U;
}
void Sheet::add_symbol_instance(SymbolInstanceId instance) {
    symbol_instances_.push_back(instance);
}
void Sheet::add_wire_run(WireRunId wire) { wire_runs_.push_back(wire); }
void Sheet::add_net_label(NetLabelId label) { net_labels_.push_back(label); }
void Sheet::add_junction(JunctionId junction) { junctions_.push_back(junction); }
void Sheet::add_power_port(PowerPortId port) { power_ports_.push_back(port); }
void Sheet::add_no_connect_marker(NoConnectMarkerId marker) {
    no_connect_markers_.push_back(marker);
}
void Sheet::add_sheet_port(SheetPortId port) { sheet_ports_.push_back(port); }
void Sheet::add_symbol_field(SymbolFieldId field) { symbol_fields_.push_back(field); }
[[nodiscard]] const std::optional<std::size_t> &SymbolInstance::authored_region() const noexcept {
    return authored_region_;
}
SchematicEndpoint::SchematicEndpoint(Point position) : position_{position} {}
SchematicEndpoint::SchematicEndpoint(Point position, PinId pin) : position_{position}, pin_{pin} {}
WireRun::WireRun(NetId net, std::vector<Point> points, RouteIntent route_intent)
    : WireRun{net, std::move(points), route_intent, std::nullopt} {}

WireRun::WireRun(NetId net, std::vector<Point> points, RouteIntent route_intent,
                 std::optional<std::size_t> authored_region)
    : net_{net}, points_{std::move(points)}, route_intent_{route_intent},
      authored_region_{authored_region} {
    if (points_.size() < 2U) {
        throw std::invalid_argument{"Schematic wire run must contain at least two points"};
    }
}
[[nodiscard]] const std::optional<std::size_t> &WireRun::authored_region() const noexcept {
    return authored_region_;
}
SchematicEndpoint SchematicEndpoint::port(Point position, NetId net) {
    auto endpoint = SchematicEndpoint{position};
    endpoint.port_net_ = net;
    return endpoint;
}
NetLabel::NetLabel(NetId net, Point position, SchematicOrientation orientation,
                   std::optional<std::size_t> authored_region, std::optional<std::string> label,
                   SchematicTextStyle style, std::optional<Point> text_position)
    : net_{net}, position_{position}, orientation_{orientation}, authored_region_{authored_region},
      label_{std::move(label)}, style_{style}, text_position_{text_position} {
    if (label_ && label_->empty()) {
        throw std::invalid_argument{"Net label display text must not be empty"};
    }
}
[[nodiscard]] Point NetLabel::text_position() const noexcept {
    return text_position_.value_or(position_);
}
[[nodiscard]] const std::optional<Point> &NetLabel::explicit_text_position() const noexcept {
    return text_position_;
}
void NetLabel::move_text_to(Point position) noexcept { text_position_ = position; }
[[nodiscard]] const std::optional<std::size_t> &NetLabel::authored_region() const noexcept {
    return authored_region_;
}
[[nodiscard]] const std::optional<std::size_t> &Junction::authored_region() const noexcept {
    return authored_region_;
}
PowerPort::PowerPort(NetId net, PowerPortKind kind, Point position,
                     SchematicOrientation orientation, std::optional<std::size_t> authored_region,
                     std::optional<std::string> label, std::optional<Point> label_position)
    : net_{net}, kind_{kind}, position_{position}, orientation_{orientation},
      authored_region_{authored_region}, label_{std::move(label)}, label_position_{label_position} {
    if (label_.has_value() && label_->empty()) {
        throw std::invalid_argument{"Schematic power port label must not be empty"};
    }
}
[[nodiscard]] const std::optional<std::size_t> &PowerPort::authored_region() const noexcept {
    return authored_region_;
}
[[nodiscard]] const std::optional<Point> &PowerPort::explicit_label_position() const noexcept {
    return label_position_;
}
void PowerPort::move_label_to(Point position) noexcept { label_position_ = position; }
NoConnectMarker::NoConnectMarker(PinId pin, Point position, SchematicOrientation orientation,
                                 std::string reason, std::optional<std::size_t> authored_region)
    : pin_{pin}, position_{position}, orientation_{orientation}, reason_{std::move(reason)},
      authored_region_{authored_region} {}
[[nodiscard]] const std::optional<std::size_t> &NoConnectMarker::authored_region() const noexcept {
    return authored_region_;
}
SheetPort::SheetPort(NetId net, std::string name, SheetPortKind kind, Point position,
                     SchematicOrientation orientation, std::optional<std::size_t> authored_region)
    : net_{net}, name_{std::move(name)}, kind_{kind}, position_{position},
      orientation_{orientation}, authored_region_{authored_region} {
    if (name_.empty()) {
        throw std::invalid_argument{"Sheet port name must not be empty"};
    }
}
[[nodiscard]] const std::optional<std::size_t> &SheetPort::authored_region() const noexcept {
    return authored_region_;
}
SymbolField::SymbolField(SymbolInstanceId symbol_instance, std::string name, std::string value,
                         Point position, SchematicOrientation orientation,
                         std::optional<std::size_t> authored_region, SchematicTextStyle style)
    : symbol_instance_{symbol_instance}, name_{std::move(name)}, value_{std::move(value)},
      position_{position}, orientation_{orientation}, authored_region_{authored_region},
      style_{style} {
    if (name_.empty()) {
        throw std::invalid_argument{"Symbol field name must not be empty"};
    }
    if (value_.empty()) {
        throw std::invalid_argument{"Symbol field value must not be empty"};
    }
}
void SymbolField::move_to(Point position) noexcept { position_ = position; }
[[nodiscard]] const std::optional<std::size_t> &SymbolField::authored_region() const noexcept {
    return authored_region_;
}
Schematic::Schematic(const Circuit &circuit) : circuit_{circuit} {}
void Schematic::replace_with(Schematic replacement) {
    if (&replacement.circuit() != &circuit_) {
        throw std::logic_error{"Schematic replacement must reference the same logical circuit"};
    }

    library_ = std::move(replacement.library_);
    sheets_ = std::move(replacement.sheets_);
    items_ = std::move(replacement.items_);
}
[[nodiscard]] SymbolDefId Schematic::add_symbol_definition(SymbolDefinition definition) {
    return library_.add_symbol_definition(std::move(definition));
}
[[nodiscard]] SheetId Schematic::add_sheet(Sheet sheet) {
    return sheets_.add_sheet(std::move(sheet));
}
[[nodiscard]] std::size_t Schematic::add_sheet_region(SheetId sheet, SheetRegion region) {
    return sheets_.add_sheet_region(sheet, std::move(region));
}
[[nodiscard]] SymbolInstanceId Schematic::place_symbol(SheetId sheet, SymbolInstance instance) {
    require_sheet(sheet);
    require_symbol_definition(instance.symbol_definition());
    static_cast<void>(circuit_.component(instance.component()));
    require_symbol_matches_component(instance.symbol_definition(), instance.component());
    require_authored_region(sheet, instance.authored_region());

    const auto id = items_.add_symbol_instance(instance);
    sheets_.add_symbol_instance(sheet, id);
    return id;
}
[[nodiscard]] JunctionId Schematic::add_junction(SheetId sheet, Junction junction) {
    require_sheet(sheet);
    static_cast<void>(circuit_.net(junction.net()));
    require_authored_region(sheet, junction.authored_region());
    require_junction_does_not_touch_different_net(sheet, junction);

    const auto id = items_.add_junction(junction);
    sheets_.add_junction(sheet, id);
    return id;
}
[[nodiscard]] PowerPortId Schematic::add_power_port(SheetId sheet, PowerPort port) {
    require_sheet(sheet);
    static_cast<void>(circuit_.net(port.net()));
    require_authored_region(sheet, port.authored_region());

    const auto id = items_.add_power_port(std::move(port));
    sheets_.add_power_port(sheet, id);
    return id;
}
[[nodiscard]] PowerPortId Schematic::add_power_port_for_endpoint(
    SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint, PowerPortKind kind,
    SchematicOrientation orientation, std::optional<std::size_t> authored_region,
    std::optional<std::string> label) {
    const auto resolved_net = resolve_endpoint_net(net, endpoint, "schematic power port");
    if (label.has_value() && label.value() == circuit_.net(resolved_net).name().value()) {
        label = std::nullopt;
    }
    return add_power_port(sheet, PowerPort{resolved_net, kind, endpoint.position(), orientation,
                                           authored_region, std::move(label)});
}
[[nodiscard]] PowerPortId Schematic::add_terminal_marker(SheetId sheet, PowerPort marker) {
    return add_power_port(sheet, std::move(marker));
}
[[nodiscard]] PowerPortId Schematic::add_terminal_marker_for_endpoint(
    SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint, PowerPortKind kind,
    SchematicOrientation orientation, std::optional<std::size_t> authored_region,
    std::optional<std::string> label) {
    return add_power_port_for_endpoint(sheet, net, endpoint, kind, orientation, authored_region,
                                       std::move(label));
}
[[nodiscard]] NoConnectMarkerId Schematic::add_no_connect_marker(SheetId sheet,
                                                                 NoConnectMarker marker) {
    require_sheet(sheet);
    static_cast<void>(circuit_.pin(marker.pin()));
    require_authored_region(sheet, marker.authored_region());

    const auto id = items_.add_no_connect_marker(std::move(marker));
    sheets_.add_no_connect_marker(sheet, id);
    return id;
}
[[nodiscard]] SheetPortId Schematic::add_sheet_port(SheetId sheet, SheetPort port) {
    require_sheet(sheet);
    static_cast<void>(circuit_.net(port.net()));
    require_authored_region(sheet, port.authored_region());

    const auto id = items_.add_sheet_port(std::move(port));
    sheets_.add_sheet_port(sheet, id);
    return id;
}
[[nodiscard]] SheetPortId
Schematic::add_sheet_port_for_endpoint(SheetId sheet, std::optional<NetId> net,
                                       const SchematicEndpoint &endpoint, std::string name,
                                       SheetPortKind kind, SchematicOrientation orientation,
                                       std::optional<std::size_t> authored_region) {
    const auto resolved_net = resolve_endpoint_net(net, endpoint, "schematic sheet port");
    return add_sheet_port(sheet, SheetPort{resolved_net, std::move(name), kind, endpoint.position(),
                                           orientation, authored_region});
}
[[nodiscard]] SymbolFieldId Schematic::add_symbol_field(SheetId sheet, SymbolField field) {
    require_sheet(sheet);
    require_symbol_instance(field.symbol_instance());
    require_authored_region(sheet, field.authored_region());
    if (!sheet_contains_symbol_instance(sheet, field.symbol_instance())) {
        throw std::logic_error{"Symbol field must be placed on the symbol instance sheet"};
    }

    const auto id = items_.add_symbol_field(std::move(field));
    sheets_.add_symbol_field(sheet, id);
    return id;
}
[[nodiscard]] WireRunId Schematic::add_wire_run(SheetId sheet, WireRun wire) {
    require_sheet(sheet);
    static_cast<void>(circuit_.net(wire.net()));
    require_authored_region(sheet, wire.authored_region());
    require_wire_run_does_not_collide_with_different_net(sheet, wire);

    const auto id = items_.add_wire_run(std::move(wire));
    sheets_.add_wire_run(sheet, id);
    return id;
}
[[nodiscard]] WireRunId Schematic::add_wire_run_for_endpoints(
    SheetId sheet, std::optional<NetId> net, std::vector<Point> points,
    const std::vector<SchematicEndpoint> &endpoints, RouteIntent route_intent,
    std::optional<std::size_t> authored_region) {
    const auto resolved_net = resolve_wire_endpoint_net(net, endpoints);
    return add_wire_run(sheet,
                        WireRun{resolved_net, std::move(points), route_intent, authored_region});
}
[[nodiscard]] NetLabelId Schematic::add_net_label(SheetId sheet, NetLabel label) {
    require_sheet(sheet);
    static_cast<void>(circuit_.net(label.net()));
    require_authored_region(sheet, label.authored_region());

    const auto id = items_.add_net_label(std::move(label));
    sheets_.add_net_label(sheet, id);
    return id;
}
[[nodiscard]] NetLabelId Schematic::add_net_label_for_endpoint(
    SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint,
    SchematicOrientation orientation, std::optional<std::size_t> authored_region,
    std::optional<std::string> label, SchematicTextStyle style,
    std::optional<Point> text_position) {
    const auto resolved_net = resolve_endpoint_net(net, endpoint, "schematic net label");
    return add_net_label(sheet, NetLabel{resolved_net, endpoint.position(), orientation,
                                         authored_region, std::move(label), style, text_position});
}
[[nodiscard]] JunctionId
Schematic::add_junction_for_endpoint(SheetId sheet, std::optional<NetId> net,
                                     const SchematicEndpoint &endpoint,
                                     std::optional<std::size_t> authored_region) {
    const auto resolved_net = resolve_endpoint_net(net, endpoint, "schematic junction");
    return add_junction(sheet, Junction{resolved_net, endpoint.position(), authored_region});
}
[[nodiscard]] std::optional<SymbolDefId>
Schematic::symbol_definition_by_name(const std::string &name) const {
    return library_.symbol_definition_by_name(name);
}
[[nodiscard]] std::optional<SheetId> Schematic::sheet_by_name(const std::string &name) const {
    return sheets_.sheet_by_name(name);
}
[[nodiscard]] std::optional<std::size_t>
Schematic::sheet_region_by_name(SheetId sheet, const std::string &name) const {
    return sheets_.sheet_region_by_name(sheet, name);
}
[[nodiscard]] const SymbolDefinition &Schematic::symbol_definition(SymbolDefId id) const {
    return library_.symbol_definition(id);
}
[[nodiscard]] const SheetRegion &Schematic::sheet_region(SheetId sheet, std::size_t region) const {
    return sheets_.sheet_region(sheet, region);
}
[[nodiscard]] const SymbolInstance &Schematic::symbol_instance(SymbolInstanceId id) const {
    return items_.symbol_instance(id);
}
void Schematic::move_net_label_text(NetLabelId id, Point position) {
    items_.move_net_label_text(id, position);
}
void Schematic::move_power_port_label(PowerPortId id, Point position) {
    items_.move_power_port_label(id, position);
}
[[nodiscard]] const NoConnectMarker &Schematic::no_connect_marker(NoConnectMarkerId id) const {
    return items_.no_connect_marker(id);
}
[[nodiscard]] const SymbolField &Schematic::symbol_field(SymbolFieldId id) const {
    return items_.symbol_field(id);
}
void Schematic::move_symbol_field(SymbolFieldId id, Point position) {
    items_.move_symbol_field(id, position);
}
[[nodiscard]] std::size_t Schematic::symbol_definition_count() const noexcept {
    return library_.symbol_definition_count();
}
[[nodiscard]] std::size_t Schematic::symbol_instance_count() const noexcept {
    return items_.symbol_instance_count();
}
[[nodiscard]] std::size_t Schematic::no_connect_marker_count() const noexcept {
    return items_.no_connect_marker_count();
}
void Schematic::require_sheet(SheetId sheet) const { sheets_.require_sheet(sheet); }
void Schematic::require_symbol_definition(SymbolDefId symbol_definition) const {
    library_.require_symbol_definition(symbol_definition);
}
void Schematic::require_symbol_instance(SymbolInstanceId instance) const {
    items_.require_symbol_instance(instance);
}
void Schematic::require_authored_region(SheetId sheet,
                                        const std::optional<std::size_t> &region) const {
    if (region.has_value() && region.value() >= sheets_.sheet(sheet).regions().size()) {
        throw std::out_of_range{"Authored schematic region does not belong to this sheet"};
    }
}
void Schematic::require_symbol_matches_component(SymbolDefId symbol_definition,
                                                 ComponentId component) const {
    const auto &symbol = library_.symbol_definition(symbol_definition);
    for (const auto &pin : symbol.pins()) {
        if (!queries::pin_by_number(circuit_, component, pin.number()).has_value()) {
            throw std::logic_error{"Schematic symbol pin does not match component pin"};
        }
    }
}
[[nodiscard]] bool Schematic::sheet_contains_symbol_instance(SheetId sheet,
                                                             SymbolInstanceId instance) const {
    for (const auto candidate : sheets_.sheet(sheet).symbol_instances()) {
        if (candidate == instance) {
            return true;
        }
    }
    return false;
}
[[nodiscard]] bool Schematic::wire_contains_point(const WireRun &wire, Point point) {
    for (std::size_t index = 1; index < wire.points().size(); ++index) {
        if (point_on_schematic_segment(
                point, SchematicSegment{wire.points()[index - 1U], wire.points()[index]})) {
            return true;
        }
    }
    return false;
}
[[nodiscard]] bool Schematic::has_junction_on_segments(SheetId sheet, SchematicSegment first,
                                                       SchematicSegment second) const {
    for (const auto junction_id : sheets_.sheet(sheet).junctions()) {
        const auto &junction = items_.junction(junction_id);
        if (point_on_schematic_segment(junction.position(), first) &&
            point_on_schematic_segment(junction.position(), second)) {
            return true;
        }
    }
    return false;
}
void Schematic::require_junction_does_not_touch_different_net(SheetId sheet,
                                                              const Junction &junction) const {
    for (const auto wire_id : sheets_.sheet(sheet).wire_runs()) {
        const auto &wire = items_.wire_run(wire_id);
        if (wire.net() != junction.net() && wire_contains_point(wire, junction.position())) {
            throw std::logic_error{"Schematic junction collides with a different logical net"};
        }
    }
}
void Schematic::require_wire_run_does_not_collide_with_different_net(SheetId sheet,
                                                                     const WireRun &wire) const {
    for (const auto existing_id : sheets_.sheet(sheet).wire_runs()) {
        const auto &existing = items_.wire_run(existing_id);
        if (existing.net() == wire.net()) {
            continue;
        }
        const auto topology = classify_wire_pair_topology(
            wire.points(), existing.points(), SchematicWireNetRelationship::DifferentNet,
            [this, sheet](SchematicSegment first, SchematicSegment second) {
                return has_junction_on_segments(sheet, first, second) ? SchematicJunction::Present
                                                                      : SchematicJunction::Absent;
            });
        if (topology.has_visual_contact()) {
            throw std::logic_error{"Schematic wire run collides with a different logical net"};
        }
    }
}
[[nodiscard]] std::string Schematic::net_label(NetId net) const {
    const auto &logical_net = circuit_.net(net);
    return logical_net.name().value() + " (net:" + std::to_string(net.index()) + ")";
}
[[nodiscard]] std::string Schematic::pin_label(PinId pin) const {
    const auto &pin_ref = circuit_.pin(pin);
    const auto &component = circuit_.component(pin_ref.component());
    const auto &definition = circuit_.pin_definition(pin_ref.definition());
    return component.reference().value() + " pin " + definition.number() + " (" +
           definition.name() + ")";
}
[[nodiscard]] std::string Schematic::endpoint_label(const SchematicEndpoint &endpoint) const {
    if (endpoint.pin().has_value()) {
        return pin_label(endpoint.pin().value());
    }
    if (endpoint.port_net().has_value()) {
        return "schematic port for " + net_label(endpoint.port_net().value());
    }
    return "plain schematic point";
}
[[nodiscard]] std::optional<NetId>
Schematic::infer_endpoint_net(const SchematicEndpoint &endpoint) const {
    if (endpoint.pin().has_value()) {
        static_cast<void>(circuit_.pin(endpoint.pin().value()));
        const auto pin_net = queries::net_of(circuit_, endpoint.pin().value());
        if (!pin_net.has_value()) {
            throw std::invalid_argument{"Schematic endpoint " + pin_label(endpoint.pin().value()) +
                                        " is not connected to any logical net"};
        }
        return pin_net.value();
    }
    if (endpoint.port_net().has_value()) {
        static_cast<void>(circuit_.net(endpoint.port_net().value()));
        return endpoint.port_net().value();
    }
    return std::nullopt;
}
void Schematic::require_endpoint_matches_net(const SchematicEndpoint &endpoint, NetId net) const {
    static_cast<void>(circuit_.net(net));
    const auto endpoint_net = infer_endpoint_net(endpoint);
    if (endpoint_net.has_value() && endpoint_net.value() != net) {
        if (endpoint.pin().has_value()) {
            throw std::invalid_argument{"Schematic endpoint " + pin_label(endpoint.pin().value()) +
                                        ": the pin belongs to " + net_label(endpoint_net.value()) +
                                        " instead of " + net_label(net)};
        }
        throw std::invalid_argument{"Schematic endpoint port belongs to " +
                                    net_label(endpoint_net.value()) + " instead of " +
                                    net_label(net)};
    }
}
[[nodiscard]] NetId Schematic::resolve_endpoint_net(std::optional<NetId> net,
                                                    const SchematicEndpoint &endpoint,
                                                    std::string_view action) const {
    if (net.has_value()) {
        require_endpoint_matches_net(endpoint, net.value());
        return net.value();
    }

    const auto inferred = infer_endpoint_net(endpoint);
    if (!inferred.has_value()) {
        throw std::invalid_argument{std::string{"Cannot infer logical net for "} +
                                    std::string{action} +
                                    " from a non-pin anchor; pass explicit net"};
    }
    return inferred.value();
}
[[nodiscard]] NetId
Schematic::resolve_wire_endpoint_net(std::optional<NetId> net,
                                     const std::vector<SchematicEndpoint> &endpoints) const {
    if (net.has_value()) {
        for (const auto &endpoint : endpoints) {
            require_endpoint_matches_net(endpoint, net.value());
        }
        return net.value();
    }
    if (endpoints.size() < 2U) {
        throw std::invalid_argument{
            "Cannot infer schematic wire net without at least two endpoints"};
    }
    if (endpoints.size() != 2U || !endpoints.front().pin().has_value() ||
        !endpoints.back().pin().has_value()) {
        throw std::invalid_argument{
            "Cannot infer schematic wire net unless both endpoints are placed pin anchors; "
            "pass explicit net"};
    }

    auto resolved = std::optional<NetId>{};
    auto resolved_endpoint = std::optional<std::size_t>{};
    for (std::size_t endpoint_index = 0; endpoint_index < endpoints.size(); ++endpoint_index) {
        const auto &endpoint = endpoints[endpoint_index];
        const auto endpoint_net = infer_endpoint_net(endpoint);
        if (!endpoint_net.has_value()) {
            throw std::invalid_argument{
                "Cannot infer schematic wire net from a plain schematic point"};
        }
        if (resolved.has_value() && resolved.value() != endpoint_net.value()) {
            const auto &first = endpoints[resolved_endpoint.value()];
            throw std::invalid_argument{
                "Cannot infer schematic wire net because endpoints belong to different "
                "logical nets: " +
                endpoint_label(first) + " is on " + net_label(resolved.value()) + ", but " +
                endpoint_label(endpoint) + " is on " + net_label(endpoint_net.value())};
        }
        resolved = endpoint_net.value();
        resolved_endpoint = endpoint_index;
    }
    return resolved.value();
}

} // namespace volt
