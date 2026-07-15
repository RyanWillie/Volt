#include <volt/schematic/schematic.hpp>

#include <volt/core/errors.hpp>
#include <volt/schematic/endpoint_authoring.hpp>

#include "schematic_storage.hpp"

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/connectivity/queries.hpp>

namespace volt {

SheetSize::SheetSize(double width, double height) : width_{width}, height_{height} {
    if (!std::isfinite(width_) || !std::isfinite(height_) || width_ <= 0.0 || height_ <= 0.0) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Sheet size dimensions must be finite and positive"};
    }
}

SheetMargins::SheetMargins(double left, double top, double right, double bottom)
    : left_{left}, top_{top}, right_{right}, bottom_{bottom} {
    if (!std::isfinite(left_) || !std::isfinite(top_) || !std::isfinite(right_) ||
        !std::isfinite(bottom_) || left_ < 0.0 || top_ < 0.0 || right_ < 0.0 || bottom_ < 0.0) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Sheet margins must be finite and non-negative"};
    }
}

SheetFrame::SheetFrame(bool visible, SheetMargins margins) : visible_{visible}, margins_{margins} {}

SheetCoordinateZones::SheetCoordinateZones(std::size_t columns, std::size_t rows, bool visible)
    : columns_{columns}, rows_{rows}, visible_{visible} {
    if (columns_ == 0U || rows_ == 0U) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Sheet coordinate zones must have positive counts"};
    }
}

SheetGrid::SheetGrid(double spacing, bool visible) : spacing_{spacing}, visible_{visible} {
    if (!std::isfinite(spacing_) || spacing_ <= 0.0) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Sheet grid spacing must be finite and positive"};
    }
}

TitleBlockField::TitleBlockField(std::string key, std::string value)
    : key_{std::move(key)}, value_{std::move(value)} {
    if (key_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Title block field key must not be empty"};
    }
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Title block field value must not be empty"};
    }
}

SheetRegionStyleField::SheetRegionStyleField(std::string key, std::string value)
    : key_{std::move(key)}, value_{std::move(value)} {
    if (key_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Region style field key must not be empty"};
    }
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Region style field value must not be empty"};
    }
}

SheetRegionBounds::SheetRegionBounds(double x, double y, double width, double height)
    : x_{x}, y_{y}, width_{width}, height_{height} {
    if (!std::isfinite(x_) || !std::isfinite(y_) || !std::isfinite(width_) ||
        !std::isfinite(height_) || width_ <= 0.0 || height_ <= 0.0) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Sheet region bounds must be finite with positive width and height"};
    }
}

SheetRegion::SheetRegion(std::string name, std::string title, SheetRegionBounds bounds,
                         std::vector<SheetRegionStyleField> style)
    : name_{std::move(name)}, title_{std::move(title)}, bounds_{bounds}, style_{std::move(style)} {
    if (name_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Sheet region name must not be empty"};
    }
    if (title_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Sheet region title must not be empty"};
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
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Sheet title must not be empty"};
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
    : Sheet{std::make_shared<detail::SheetState>(std::move(name), std::move(metadata))} {
    if (state().name.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Sheet name must not be empty"};
    }
}

Sheet::Sheet(std::shared_ptr<const detail::SheetState> state) : state_{std::move(state)} {}

Sheet::Sheet(const Sheet &other) : Sheet{std::make_shared<detail::SheetState>(other.state())} {}

Sheet::Sheet(Sheet &&other) noexcept = default;

Sheet &Sheet::operator=(const Sheet &other) {
    if (this != &other) {
        state_ = std::make_shared<detail::SheetState>(other.state());
    }
    return *this;
}

Sheet &Sheet::operator=(Sheet &&other) noexcept = default;

Sheet::~Sheet() = default;

[[nodiscard]] const std::string &Sheet::name() const noexcept { return state().name; }

[[nodiscard]] const SheetMetadata &Sheet::metadata() const noexcept { return state().metadata; }

[[nodiscard]] const std::vector<SymbolInstanceId> &Sheet::symbol_instances() const noexcept {
    return state().symbol_instances;
}

[[nodiscard]] const std::vector<WireRunId> &Sheet::wire_runs() const noexcept {
    return state().wire_runs;
}

[[nodiscard]] const std::vector<NetLabelId> &Sheet::net_labels() const noexcept {
    return state().net_labels;
}

[[nodiscard]] const std::vector<JunctionId> &Sheet::junctions() const noexcept {
    return state().junctions;
}

[[nodiscard]] const std::vector<PowerPortId> &Sheet::power_ports() const noexcept {
    return state().power_ports;
}

[[nodiscard]] const std::vector<NoConnectMarkerId> &Sheet::no_connect_markers() const noexcept {
    return state().no_connect_markers;
}

[[nodiscard]] const std::vector<SheetPortId> &Sheet::sheet_ports() const noexcept {
    return state().sheet_ports;
}

[[nodiscard]] const std::vector<SymbolFieldId> &Sheet::symbol_fields() const noexcept {
    return state().symbol_fields;
}

[[nodiscard]] const std::vector<SheetRegion> &Sheet::regions() const noexcept {
    return state().regions;
}

[[nodiscard]] std::optional<std::size_t> Sheet::region_by_name(const std::string &name) const {
    for (std::size_t index = 0; index < state().regions.size(); ++index) {
        if (state().regions[index].name() == name) {
            return index;
        }
    }
    return std::nullopt;
}

[[nodiscard]] const SheetRegion &Sheet::region(std::size_t index) const {
    if (index >= state().regions.size()) {
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Sheet region index does not belong to this sheet"};
    }
    return state().regions[index];
}

[[nodiscard]] const detail::SheetState &Sheet::state() const noexcept { return *state_; }

namespace detail {

SheetStorage::SheetStorage(std::string name) : SheetStorage{name, SheetMetadata{name}} {}

SheetStorage::SheetStorage(std::string name, SheetMetadata metadata)
    : SheetStorage{std::make_shared<SheetState>(std::move(name), std::move(metadata))} {
    if (state_->name.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Sheet name must not be empty"};
    }
}

SheetStorage::SheetStorage(std::shared_ptr<SheetState> state)
    : Sheet{state}, state_{std::move(state)} {}

SheetStorage::SheetStorage(const SheetStorage &other)
    : SheetStorage{std::make_shared<SheetState>(other.state())} {}

SheetStorage &SheetStorage::operator=(const SheetStorage &other) {
    if (this != &other) {
        auto replacement = SheetStorage{std::make_shared<SheetState>(other.state())};
        *this = std::move(replacement);
    }
    return *this;
}

SheetStorage::SheetStorage(Sheet sheet)
    : SheetStorage{std::make_shared<SheetState>(sheet.name(), sheet.metadata())} {
    state_->symbol_instances = sheet.symbol_instances();
    state_->wire_runs = sheet.wire_runs();
    state_->net_labels = sheet.net_labels();
    state_->junctions = sheet.junctions();
    state_->power_ports = sheet.power_ports();
    state_->no_connect_markers = sheet.no_connect_markers();
    state_->sheet_ports = sheet.sheet_ports();
    state_->symbol_fields = sheet.symbol_fields();
    state_->regions = sheet.regions();
}

} // namespace detail

[[nodiscard]] const std::optional<std::size_t> &SymbolInstance::authored_region() const noexcept {
    return authored_region_;
}

SchematicEndpoint::SchematicEndpoint(Point position) : position_{position} {}

SchematicEndpoint::SchematicEndpoint(Point position, PinId pin) : position_{position}, pin_{pin} {}

[[nodiscard]] std::string SchematicEndpointAuthoring::net_label(NetId net) const {
    const auto &logical_net = circuit_.get(net);
    return logical_net.name().value() + " (net:" + std::to_string(net.index()) + ")";
}

[[nodiscard]] std::string SchematicEndpointAuthoring::pin_label(PinId pin) const {
    const auto &pin_ref = circuit_.get(pin);
    const auto &component = circuit_.get(pin_ref.component());
    const auto &definition = circuit_.get(pin_ref.definition());
    return component.reference().value() + " pin " + definition.number() + " (" +
           definition.name() + ")";
}

[[nodiscard]] std::string
SchematicEndpointAuthoring::endpoint_label(const SchematicEndpoint &endpoint) const {
    if (endpoint.pin().has_value()) {
        return pin_label(endpoint.pin().value());
    }
    if (endpoint.port_net().has_value()) {
        return "schematic port for " + net_label(endpoint.port_net().value());
    }
    return "plain schematic point";
}

[[nodiscard]] bool SchematicEndpointAuthoring::net_has_name(NetId net,
                                                            const std::string &name) const {
    return circuit_.get(net).name().value() == name;
}

WireRun::WireRun(NetId net, std::vector<Point> points, RouteIntent route_intent)
    : WireRun{net, std::move(points), route_intent, std::nullopt} {}

WireRun::WireRun(NetId net, std::vector<Point> points, RouteIntent route_intent,
                 std::optional<std::size_t> authored_region)
    : net_{net}, points_{std::move(points)}, route_intent_{route_intent},
      authored_region_{authored_region} {
    if (points_.size() < 2U) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Schematic wire run must contain at least two points"};
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
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Net label display text must not be empty"};
    }
}

[[nodiscard]] Point NetLabel::text_position() const noexcept {
    return text_position_.value_or(position_);
}

[[nodiscard]] const std::optional<Point> &NetLabel::explicit_text_position() const noexcept {
    return text_position_;
}

[[nodiscard]] const std::optional<std::size_t> &NetLabel::authored_region() const noexcept {
    return authored_region_;
}

[[nodiscard]] NetLabel NetLabel::with_text_position(Point position) const {
    return NetLabel{net_, position_, orientation_, authored_region_, label_, style_, position};
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
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Schematic power port label must not be empty"};
    }
}

[[nodiscard]] const std::optional<std::size_t> &PowerPort::authored_region() const noexcept {
    return authored_region_;
}

[[nodiscard]] const std::optional<Point> &PowerPort::explicit_label_position() const noexcept {
    return label_position_;
}

[[nodiscard]] PowerPort PowerPort::with_label_position(Point position) const {
    return PowerPort{net_, kind_, position_, orientation_, authored_region_, label_, position};
}

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
        throw KernelArgumentError{ErrorCode::InvalidArgument, "Sheet port name must not be empty"};
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
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Symbol field name must not be empty"};
    }
    if (value_.empty()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Symbol field value must not be empty"};
    }
}

[[nodiscard]] const std::optional<std::size_t> &SymbolField::authored_region() const noexcept {
    return authored_region_;
}

[[nodiscard]] SymbolField SymbolField::with_position(Point position) const {
    return SymbolField{symbol_instance_, name_, value_, position, orientation_,
                       authored_region_, style_};
}

Schematic::Schematic(const Circuit &circuit) : circuit_{circuit} {}

void Schematic::replace_with(Schematic replacement) {
    if (&replacement.circuit() != &circuit_) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Schematic replacement must reference the same logical circuit"};
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
    static_cast<void>(circuit_.get(instance.component()));
    require_symbol_matches_component(instance.symbol_definition(), instance.component());
    require_authored_region(sheet, instance.authored_region());

    const auto id = items_.add_symbol_instance(instance);
    sheets_.add_symbol_instance(sheet, id);
    return id;
}

[[nodiscard]] JunctionId Schematic::add_junction(SheetId sheet, Junction junction) {
    require_sheet(sheet);
    static_cast<void>(circuit_.get(junction.net()));
    require_authored_region(sheet, junction.authored_region());
    require_junction_does_not_touch_different_net(sheet, junction);

    const auto id = items_.add_junction(junction);
    sheets_.add_junction(sheet, id);
    return id;
}

[[nodiscard]] PowerPortId Schematic::add_power_port(SheetId sheet, PowerPort port) {
    require_sheet(sheet);
    static_cast<void>(circuit_.get(port.net()));
    require_authored_region(sheet, port.authored_region());

    const auto id = items_.add_power_port(std::move(port));
    sheets_.add_power_port(sheet, id);
    return id;
}

[[nodiscard]] PowerPortId Schematic::add_terminal_marker(SheetId sheet, PowerPort marker) {
    return add_power_port(sheet, std::move(marker));
}

[[nodiscard]] NoConnectMarkerId Schematic::add_no_connect_marker(SheetId sheet,
                                                                 NoConnectMarker marker) {
    require_sheet(sheet);
    static_cast<void>(circuit_.get(marker.pin()));
    require_authored_region(sheet, marker.authored_region());

    const auto id = items_.add_no_connect_marker(std::move(marker));
    sheets_.add_no_connect_marker(sheet, id);
    return id;
}

[[nodiscard]] SheetPortId Schematic::add_sheet_port(SheetId sheet, SheetPort port) {
    require_sheet(sheet);
    static_cast<void>(circuit_.get(port.net()));
    require_authored_region(sheet, port.authored_region());

    const auto id = items_.add_sheet_port(std::move(port));
    sheets_.add_sheet_port(sheet, id);
    return id;
}

[[nodiscard]] SymbolFieldId Schematic::add_symbol_field(SheetId sheet, SymbolField field) {
    require_sheet(sheet);
    require_symbol_instance(field.symbol_instance());
    require_authored_region(sheet, field.authored_region());
    if (!sheet_contains_symbol_instance(sheet, field.symbol_instance())) {
        throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                               "Symbol field must be placed on the symbol instance sheet",
                               EntityRef::symbol_instance(field.symbol_instance())};
    }

    const auto id = items_.add_symbol_field(std::move(field));
    sheets_.add_symbol_field(sheet, id);
    return id;
}

[[nodiscard]] WireRunId Schematic::add_wire_run(SheetId sheet, WireRun wire) {
    require_sheet(sheet);
    static_cast<void>(circuit_.get(wire.net()));
    require_authored_region(sheet, wire.authored_region());
    require_wire_run_does_not_collide_with_different_net(sheet, wire);

    const auto id = items_.add_wire_run(std::move(wire));
    sheets_.add_wire_run(sheet, id);
    return id;
}

[[nodiscard]] NetLabelId Schematic::add_net_label(SheetId sheet, NetLabel label) {
    require_sheet(sheet);
    static_cast<void>(circuit_.get(label.net()));
    require_authored_region(sheet, label.authored_region());

    const auto id = items_.add_net_label(std::move(label));
    sheets_.add_net_label(sheet, id);
    return id;
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
        throw KernelRangeError{ErrorCode::UnknownEntity,
                               "Authored schematic region does not belong to this sheet"};
    }
}

void Schematic::require_symbol_matches_component(SymbolDefId symbol_definition,
                                                 ComponentId component) const {
    const auto &symbol = library_.symbol_definition(symbol_definition);
    for (const auto &pin : symbol.pins()) {
        if (!queries::pin_by_number(circuit_, component, pin.number()).has_value()) {
            throw KernelLogicError{ErrorCode::CrossReferenceViolation,
                                   "Schematic symbol pin does not match component pin",
                                   EntityRef::symbol_def(symbol_definition)};
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
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Schematic junction collides with a different logical net",
                                   EntityRef::wire_run(wire_id)};
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
            throw KernelLogicError{ErrorCode::InvalidState,
                                   "Schematic wire run collides with a different logical net",
                                   EntityRef::wire_run(existing_id)};
        }
    }
}

} // namespace volt
