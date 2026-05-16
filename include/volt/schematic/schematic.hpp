#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/core/entity_table.hpp>
#include <volt/core/ids.hpp>
#include <volt/schematic/geometry.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt {

/** Physical drawing size for a schematic sheet. */
class SheetSize {
  public:
    /** Construct a sheet size from positive finite dimensions. */
    SheetSize(double width = 297.0, double height = 210.0) : width_{width}, height_{height} {
        if (!std::isfinite(width_) || !std::isfinite(height_) || width_ <= 0.0 || height_ <= 0.0) {
            throw std::invalid_argument{"Sheet size dimensions must be finite and positive"};
        }
    }

    /** Return the sheet width. */
    [[nodiscard]] double width() const noexcept { return width_; }

    /** Return the sheet height. */
    [[nodiscard]] double height() const noexcept { return height_; }

  private:
    double width_;
    double height_;
};

/** One key/value entry in a schematic sheet title block. */
class TitleBlockField {
  public:
    /** Construct a title-block field. */
    TitleBlockField(std::string key, std::string value)
        : key_{std::move(key)}, value_{std::move(value)} {
        if (key_.empty()) {
            throw std::invalid_argument{"Title block field key must not be empty"};
        }
        if (value_.empty()) {
            throw std::invalid_argument{"Title block field value must not be empty"};
        }
    }

    /** Return the field key. */
    [[nodiscard]] const std::string &key() const noexcept { return key_; }

    /** Return the field value. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

  private:
    std::string key_;
    std::string value_;
};

/** Sheet-level metadata used by renderers and authoring tools. */
class SheetMetadata {
  public:
    /** Construct sheet metadata from a title, size, and optional title-block fields. */
    explicit SheetMetadata(std::string title, SheetSize size = {},
                           std::vector<TitleBlockField> title_block = {})
        : title_{std::move(title)}, size_{size}, title_block_{std::move(title_block)} {
        if (title_.empty()) {
            throw std::invalid_argument{"Sheet title must not be empty"};
        }
    }

    /** Return the displayed sheet title. */
    [[nodiscard]] const std::string &title() const noexcept { return title_; }

    /** Return the sheet drawing size. */
    [[nodiscard]] SheetSize size() const noexcept { return size_; }

    /** Return title-block fields in insertion order. */
    [[nodiscard]] const std::vector<TitleBlockField> &title_block() const noexcept {
        return title_block_;
    }

  private:
    std::string title_;
    SheetSize size_;
    std::vector<TitleBlockField> title_block_;
};

/** A schematic sheet that owns presentation objects for one drawing page. */
class Sheet {
  public:
    /** Construct a named schematic sheet. */
    explicit Sheet(std::string name) : Sheet{name, SheetMetadata{name}} {}

    /** Construct a named schematic sheet with explicit metadata. */
    Sheet(std::string name, SheetMetadata metadata)
        : name_{std::move(name)}, metadata_{std::move(metadata)} {
        if (name_.empty()) {
            throw std::invalid_argument{"Sheet name must not be empty"};
        }
    }

    /** Return the sheet name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return sheet metadata. */
    [[nodiscard]] const SheetMetadata &metadata() const noexcept { return metadata_; }

    /** Return placed symbol instances in insertion order. */
    [[nodiscard]] const std::vector<SymbolInstanceId> &symbol_instances() const noexcept {
        return symbol_instances_;
    }

    /** Return wire runs in insertion order. */
    [[nodiscard]] const std::vector<WireRunId> &wire_runs() const noexcept { return wire_runs_; }

    /** Return net labels in insertion order. */
    [[nodiscard]] const std::vector<NetLabelId> &net_labels() const noexcept { return net_labels_; }

    /** Return explicit junctions in insertion order. */
    [[nodiscard]] const std::vector<JunctionId> &junctions() const noexcept { return junctions_; }

    /** Return power and ground ports in insertion order. */
    [[nodiscard]] const std::vector<PowerPortId> &power_ports() const noexcept {
        return power_ports_;
    }

    /** Return no-connect markers in insertion order. */
    [[nodiscard]] const std::vector<NoConnectMarkerId> &no_connect_markers() const noexcept {
        return no_connect_markers_;
    }

    /** Return sheet/off-page ports in insertion order. */
    [[nodiscard]] const std::vector<SheetPortId> &sheet_ports() const noexcept {
        return sheet_ports_;
    }

    /** Return placed symbol fields in insertion order. */
    [[nodiscard]] const std::vector<SymbolFieldId> &symbol_fields() const noexcept {
        return symbol_fields_;
    }

  private:
    friend class Schematic;

    void add_symbol_instance(SymbolInstanceId instance) { symbol_instances_.push_back(instance); }

    void add_wire_run(WireRunId wire) { wire_runs_.push_back(wire); }

    void add_net_label(NetLabelId label) { net_labels_.push_back(label); }

    void add_junction(JunctionId junction) { junctions_.push_back(junction); }

    void add_power_port(PowerPortId port) { power_ports_.push_back(port); }

    void add_no_connect_marker(NoConnectMarkerId marker) { no_connect_markers_.push_back(marker); }

    void add_sheet_port(SheetPortId port) { sheet_ports_.push_back(port); }

    void add_symbol_field(SymbolFieldId field) { symbol_fields_.push_back(field); }

    std::string name_;
    SheetMetadata metadata_;
    std::vector<SymbolInstanceId> symbol_instances_;
    std::vector<WireRunId> wire_runs_;
    std::vector<NetLabelId> net_labels_;
    std::vector<JunctionId> junctions_;
    std::vector<PowerPortId> power_ports_;
    std::vector<NoConnectMarkerId> no_connect_markers_;
    std::vector<SheetPortId> sheet_ports_;
    std::vector<SymbolFieldId> symbol_fields_;
};

/** A placed schematic symbol that presents an existing logical component instance. */
class SymbolInstance {
  public:
    /** Construct a symbol instance over an existing logical component. */
    SymbolInstance(SymbolDefId symbol_definition, ComponentId component, Point position,
                   SchematicOrientation orientation = SchematicOrientation::Right)
        : symbol_definition_{symbol_definition}, component_{component}, position_{position},
          orientation_{orientation} {}

    /** Return the reusable symbol definition used by this placement. */
    [[nodiscard]] SymbolDefId symbol_definition() const noexcept { return symbol_definition_; }

    /** Return the logical component instance presented by this placement. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the sheet-local symbol origin. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the symbol orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

  private:
    SymbolDefId symbol_definition_;
    ComponentId component_;
    Point position_;
    SchematicOrientation orientation_;
};

/** Authoring route intent retained on a drawn wire run. */
enum class RouteIntent {
    Direct,
    Orthogonal,
};

/** A drawn schematic wire segment sequence that presents one canonical logical net. */
class WireRun {
  public:
    /** Construct a wire run over an existing logical net. */
    WireRun(NetId net, std::vector<Point> points, RouteIntent route_intent = RouteIntent::Direct)
        : net_{net}, points_{std::move(points)}, route_intent_{route_intent} {
        if (points_.size() < 2U) {
            throw std::invalid_argument{"Schematic wire run must contain at least two points"};
        }
    }

    /** Return the canonical logical net presented by this wire run. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the wire polyline points in drawing order. */
    [[nodiscard]] const std::vector<Point> &points() const noexcept { return points_; }

    /** Return the authoring route intent for this wire run. */
    [[nodiscard]] RouteIntent route_intent() const noexcept { return route_intent_; }

  private:
    NetId net_;
    std::vector<Point> points_;
    RouteIntent route_intent_;
};

/** A schematic label whose visible text is derived from a canonical logical net name. */
class NetLabel {
  public:
    /** Construct a net label over an existing logical net. */
    NetLabel(NetId net, Point position,
             SchematicOrientation orientation = SchematicOrientation::Right)
        : net_{net}, position_{position}, orientation_{orientation} {}

    /** Return the canonical logical net named by this label. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the label anchor position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the label orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

  private:
    NetId net_;
    Point position_;
    SchematicOrientation orientation_;
};

/** An explicit junction dot over an existing logical net. */
class Junction {
  public:
    /** Construct a junction over an existing logical net. */
    Junction(NetId net, Point position) : net_{net}, position_{position} {}

    /** Return the canonical logical net joined by this junction. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the junction position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

  private:
    NetId net_;
    Point position_;
};

/** Power-port visual style. */
enum class PowerPortKind {
    Power,
    Ground,
};

/** A schematic power or ground symbol over an existing logical net. */
class PowerPort {
  public:
    /** Construct a power or ground port over an existing logical net. */
    PowerPort(NetId net, PowerPortKind kind, Point position,
              SchematicOrientation orientation = SchematicOrientation::Up)
        : net_{net}, kind_{kind}, position_{position}, orientation_{orientation} {}

    /** Return the canonical logical net presented by this port. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return whether this is rendered as a power or ground port. */
    [[nodiscard]] PowerPortKind kind() const noexcept { return kind_; }

    /** Return the port anchor position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the port orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

  private:
    NetId net_;
    PowerPortKind kind_;
    Point position_;
    SchematicOrientation orientation_;
};

/** A schematic no-connect marker tied to an existing concrete pin. */
class NoConnectMarker {
  public:
    /** Construct a no-connect marker for an existing concrete pin. */
    NoConnectMarker(PinId pin, Point position,
                    SchematicOrientation orientation = SchematicOrientation::Right,
                    std::string reason = {})
        : pin_{pin}, position_{position}, orientation_{orientation}, reason_{std::move(reason)} {}

    /** Return the concrete pin marked as intentionally open. */
    [[nodiscard]] PinId pin() const noexcept { return pin_; }

    /** Return the marker position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the marker orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

    /** Return the optional author-supplied reason for the no-connect marker. */
    [[nodiscard]] const std::string &reason() const noexcept { return reason_; }

  private:
    PinId pin_;
    Point position_;
    SchematicOrientation orientation_;
    std::string reason_;
};

/** Sheet-port visual and authoring direction. */
enum class SheetPortKind {
    Input,
    Output,
    Bidirectional,
    OffPage,
};

/** A sheet/off-page connector over an existing logical net. */
class SheetPort {
  public:
    /** Construct a sheet/off-page port over an existing logical net. */
    SheetPort(NetId net, std::string name, SheetPortKind kind, Point position,
              SchematicOrientation orientation = SchematicOrientation::Right)
        : net_{net}, name_{std::move(name)}, kind_{kind}, position_{position},
          orientation_{orientation} {
        if (name_.empty()) {
            throw std::invalid_argument{"Sheet port name must not be empty"};
        }
    }

    /** Return the canonical logical net presented by this port. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the visible port name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the port kind. */
    [[nodiscard]] SheetPortKind kind() const noexcept { return kind_; }

    /** Return the port anchor position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the port orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

  private:
    NetId net_;
    std::string name_;
    SheetPortKind kind_;
    Point position_;
    SchematicOrientation orientation_;
};

/** A placed symbol field owned by an existing symbol instance. */
class SymbolField {
  public:
    /** Construct a symbol field for an existing symbol instance. */
    SymbolField(SymbolInstanceId symbol_instance, std::string name, std::string value,
                Point position, SchematicOrientation orientation = SchematicOrientation::Right)
        : symbol_instance_{symbol_instance}, name_{std::move(name)}, value_{std::move(value)},
          position_{position}, orientation_{orientation} {
        if (name_.empty()) {
            throw std::invalid_argument{"Symbol field name must not be empty"};
        }
        if (value_.empty()) {
            throw std::invalid_argument{"Symbol field value must not be empty"};
        }
    }

    /** Return the symbol instance that owns this field. */
    [[nodiscard]] SymbolInstanceId symbol_instance() const noexcept { return symbol_instance_; }

    /** Return the field name, such as reference or value. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the field value to render. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return the field anchor position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the field orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

  private:
    SymbolInstanceId symbol_instance_;
    std::string name_;
    std::string value_;
    Point position_;
    SchematicOrientation orientation_;
};

/** Kernel-owned schematic projection over a logical circuit. */
class Schematic {
  public:
    /** Construct a schematic projection for one logical circuit context. */
    explicit Schematic(const Circuit &circuit) : circuit_{circuit} {}

    /** Replace projection contents with another schematic over the same logical circuit. */
    void replace_with(Schematic replacement) {
        if (&replacement.circuit() != &circuit_) {
            throw std::logic_error{"Schematic replacement must reference the same logical circuit"};
        }

        symbol_definitions_ = std::move(replacement.symbol_definitions_);
        sheets_ = std::move(replacement.sheets_);
        symbol_instances_ = std::move(replacement.symbol_instances_);
        wire_runs_ = std::move(replacement.wire_runs_);
        net_labels_ = std::move(replacement.net_labels_);
        junctions_ = std::move(replacement.junctions_);
        power_ports_ = std::move(replacement.power_ports_);
        no_connect_markers_ = std::move(replacement.no_connect_markers_);
        sheet_ports_ = std::move(replacement.sheet_ports_);
        symbol_fields_ = std::move(replacement.symbol_fields_);
    }

    /** Store a reusable symbol definition and return its stable schematic ID. */
    [[nodiscard]] SymbolDefId add_symbol_definition(SymbolDefinition definition) {
        if (symbol_definition_by_name(definition.name()).has_value()) {
            throw std::logic_error{"Symbol definition name already exists"};
        }

        return symbol_definitions_.insert(std::move(definition));
    }

    /** Store a schematic sheet and return its stable schematic ID. */
    [[nodiscard]] SheetId add_sheet(Sheet sheet) {
        if (sheet_by_name(sheet.name()).has_value()) {
            throw std::logic_error{"Sheet name already exists"};
        }

        return sheets_.insert(std::move(sheet));
    }

    /** Place a symbol on a sheet for an existing logical component instance. */
    [[nodiscard]] SymbolInstanceId place_symbol(SheetId sheet, SymbolInstance instance) {
        require_sheet(sheet);
        require_symbol_definition(instance.symbol_definition());
        static_cast<void>(circuit_.component(instance.component()));
        require_symbol_matches_component(instance.symbol_definition(), instance.component());

        const auto id = symbol_instances_.insert(std::move(instance));
        sheets_.get(sheet).add_symbol_instance(id);
        return id;
    }

    /** Add an explicit junction over an existing logical net. */
    [[nodiscard]] JunctionId add_junction(SheetId sheet, Junction junction) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(junction.net()));
        require_junction_does_not_touch_different_net(sheet, junction);

        const auto id = junctions_.insert(std::move(junction));
        sheets_.get(sheet).add_junction(id);
        return id;
    }

    /** Add a power or ground port over an existing logical net. */
    [[nodiscard]] PowerPortId add_power_port(SheetId sheet, PowerPort port) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(port.net()));

        const auto id = power_ports_.insert(std::move(port));
        sheets_.get(sheet).add_power_port(id);
        return id;
    }

    /** Add a no-connect marker for an existing concrete pin. */
    [[nodiscard]] NoConnectMarkerId add_no_connect_marker(SheetId sheet, NoConnectMarker marker) {
        require_sheet(sheet);
        static_cast<void>(circuit_.pin(marker.pin()));

        const auto id = no_connect_markers_.insert(std::move(marker));
        sheets_.get(sheet).add_no_connect_marker(id);
        return id;
    }

    /** Add a sheet/off-page port over an existing logical net. */
    [[nodiscard]] SheetPortId add_sheet_port(SheetId sheet, SheetPort port) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(port.net()));

        const auto id = sheet_ports_.insert(std::move(port));
        sheets_.get(sheet).add_sheet_port(id);
        return id;
    }

    /** Add a placed field for a symbol instance on the same sheet. */
    [[nodiscard]] SymbolFieldId add_symbol_field(SheetId sheet, SymbolField field) {
        require_sheet(sheet);
        require_symbol_instance(field.symbol_instance());
        if (!sheet_contains_symbol_instance(sheet, field.symbol_instance())) {
            throw std::logic_error{"Symbol field must be placed on the symbol instance sheet"};
        }

        const auto id = symbol_fields_.insert(std::move(field));
        sheets_.get(sheet).add_symbol_field(id);
        return id;
    }

    /** Add a wire run on a sheet for an existing logical net. */
    [[nodiscard]] WireRunId add_wire_run(SheetId sheet, WireRun wire) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(wire.net()));
        require_wire_run_does_not_collide_with_different_net(sheet, wire);

        const auto id = wire_runs_.insert(std::move(wire));
        sheets_.get(sheet).add_wire_run(id);
        return id;
    }

    /** Add a net label on a sheet for an existing logical net. */
    [[nodiscard]] NetLabelId add_net_label(SheetId sheet, NetLabel label) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(label.net()));

        const auto id = net_labels_.insert(std::move(label));
        sheets_.get(sheet).add_net_label(id);
        return id;
    }

    /** Return the symbol definition with this name, if it exists. */
    [[nodiscard]] std::optional<SymbolDefId>
    symbol_definition_by_name(const std::string &name) const {
        for (std::size_t index = 0; index < symbol_definitions_.size(); ++index) {
            const auto id = SymbolDefId{index};
            if (symbol_definitions_.get(id).name() == name) {
                return id;
            }
        }

        return std::nullopt;
    }

    /** Return the sheet with this name, if it exists. */
    [[nodiscard]] std::optional<SheetId> sheet_by_name(const std::string &name) const {
        for (std::size_t index = 0; index < sheets_.size(); ++index) {
            const auto id = SheetId{index};
            if (sheets_.get(id).name() == name) {
                return id;
            }
        }

        return std::nullopt;
    }

    /** Return a symbol definition by ID. */
    [[nodiscard]] const SymbolDefinition &symbol_definition(SymbolDefId id) const {
        return symbol_definitions_.get(id);
    }

    /** Return a schematic sheet by ID. */
    [[nodiscard]] const Sheet &sheet(SheetId id) const { return sheets_.get(id); }

    /** Return a placed symbol instance by ID. */
    [[nodiscard]] const SymbolInstance &symbol_instance(SymbolInstanceId id) const {
        return symbol_instances_.get(id);
    }

    /** Return a wire run by ID. */
    [[nodiscard]] const WireRun &wire_run(WireRunId id) const { return wire_runs_.get(id); }

    /** Return a net label by ID. */
    [[nodiscard]] const NetLabel &net_label(NetLabelId id) const { return net_labels_.get(id); }

    /** Return an explicit junction by ID. */
    [[nodiscard]] const Junction &junction(JunctionId id) const { return junctions_.get(id); }

    /** Return a power or ground port by ID. */
    [[nodiscard]] const PowerPort &power_port(PowerPortId id) const { return power_ports_.get(id); }

    /** Return a no-connect marker by ID. */
    [[nodiscard]] const NoConnectMarker &no_connect_marker(NoConnectMarkerId id) const {
        return no_connect_markers_.get(id);
    }

    /** Return a sheet/off-page port by ID. */
    [[nodiscard]] const SheetPort &sheet_port(SheetPortId id) const { return sheet_ports_.get(id); }

    /** Return a placed symbol field by ID. */
    [[nodiscard]] const SymbolField &symbol_field(SymbolFieldId id) const {
        return symbol_fields_.get(id);
    }

    /** Return the logical circuit this schematic projection references. */
    [[nodiscard]] const Circuit &circuit() const noexcept { return circuit_; }

    /** Return the number of stored symbol definitions. */
    [[nodiscard]] std::size_t symbol_definition_count() const noexcept {
        return symbol_definitions_.size();
    }

    /** Return the number of stored sheets. */
    [[nodiscard]] std::size_t sheet_count() const noexcept { return sheets_.size(); }

    /** Return the number of stored symbol instances. */
    [[nodiscard]] std::size_t symbol_instance_count() const noexcept {
        return symbol_instances_.size();
    }

    /** Return the number of stored wire runs. */
    [[nodiscard]] std::size_t wire_run_count() const noexcept { return wire_runs_.size(); }

    /** Return the number of stored net labels. */
    [[nodiscard]] std::size_t net_label_count() const noexcept { return net_labels_.size(); }

    /** Return the number of stored explicit junctions. */
    [[nodiscard]] std::size_t junction_count() const noexcept { return junctions_.size(); }

    /** Return the number of stored power and ground ports. */
    [[nodiscard]] std::size_t power_port_count() const noexcept { return power_ports_.size(); }

    /** Return the number of stored no-connect markers. */
    [[nodiscard]] std::size_t no_connect_marker_count() const noexcept {
        return no_connect_markers_.size();
    }

    /** Return the number of stored sheet/off-page ports. */
    [[nodiscard]] std::size_t sheet_port_count() const noexcept { return sheet_ports_.size(); }

    /** Return the number of stored symbol fields. */
    [[nodiscard]] std::size_t symbol_field_count() const noexcept { return symbol_fields_.size(); }

  private:
    void require_sheet(SheetId sheet) const {
        if (!sheets_.contains(sheet)) {
            throw std::out_of_range{"Sheet ID does not belong to this schematic"};
        }
    }

    void require_symbol_definition(SymbolDefId symbol_definition) const {
        if (!symbol_definitions_.contains(symbol_definition)) {
            throw std::out_of_range{"Symbol definition ID does not belong to this schematic"};
        }
    }

    void require_symbol_instance(SymbolInstanceId instance) const {
        if (!symbol_instances_.contains(instance)) {
            throw std::out_of_range{"Symbol instance ID does not belong to this schematic"};
        }
    }

    void require_symbol_matches_component(SymbolDefId symbol_definition,
                                          ComponentId component) const {
        const auto &symbol = symbol_definitions_.get(symbol_definition);
        for (const auto &pin : symbol.pins()) {
            if (!circuit_.pin_by_number(component, pin.number()).has_value()) {
                throw std::logic_error{"Schematic symbol pin does not match component pin"};
            }
        }
    }

    [[nodiscard]] bool sheet_contains_symbol_instance(SheetId sheet,
                                                      SymbolInstanceId instance) const {
        for (const auto candidate : sheets_.get(sheet).symbol_instances()) {
            if (candidate == instance) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static bool wire_contains_point(const WireRun &wire, Point point) {
        for (std::size_t index = 1; index < wire.points().size(); ++index) {
            if (point_on_schematic_segment(
                    point, SchematicSegment{wire.points()[index - 1U], wire.points()[index]})) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool has_junction_on_segments(SheetId sheet, SchematicSegment first,
                                                SchematicSegment second) const {
        for (const auto junction_id : sheets_.get(sheet).junctions()) {
            const auto &junction = junctions_.get(junction_id);
            if (point_on_schematic_segment(junction.position(), first) &&
                point_on_schematic_segment(junction.position(), second)) {
                return true;
            }
        }
        return false;
    }

    void require_junction_does_not_touch_different_net(SheetId sheet,
                                                       const Junction &junction) const {
        for (const auto wire_id : sheets_.get(sheet).wire_runs()) {
            const auto &wire = wire_runs_.get(wire_id);
            if (wire.net() != junction.net() && wire_contains_point(wire, junction.position())) {
                throw std::logic_error{"Schematic junction collides with a different logical net"};
            }
        }
    }

    void require_wire_run_does_not_collide_with_different_net(SheetId sheet,
                                                              const WireRun &wire) const {
        for (const auto existing_id : sheets_.get(sheet).wire_runs()) {
            const auto &existing = wire_runs_.get(existing_id);
            if (existing.net() == wire.net()) {
                continue;
            }
            for (std::size_t wire_index = 1; wire_index < wire.points().size(); ++wire_index) {
                for (std::size_t existing_index = 1; existing_index < existing.points().size();
                     ++existing_index) {
                    const auto relationship = classify_segment_relationship(
                        SchematicSegment{wire.points()[wire_index - 1U], wire.points()[wire_index]},
                        SchematicSegment{existing.points()[existing_index - 1U],
                                         existing.points()[existing_index]});
                    const auto junction =
                        has_junction_on_segments(
                            sheet,
                            SchematicSegment{wire.points()[wire_index - 1U],
                                             wire.points()[wire_index]},
                            SchematicSegment{existing.points()[existing_index - 1U],
                                             existing.points()[existing_index]})
                            ? SchematicJunction::Present
                            : SchematicJunction::Absent;
                    if (different_net_segments_collide(relationship, junction)) {
                        throw std::logic_error{
                            "Schematic wire run collides with a different logical net"};
                    }
                }
            }
        }
    }

    const Circuit &circuit_;
    EntityTable<SymbolDefinition, SymbolDefId> symbol_definitions_;
    EntityTable<Sheet, SheetId> sheets_;
    EntityTable<SymbolInstance, SymbolInstanceId> symbol_instances_;
    EntityTable<WireRun, WireRunId> wire_runs_;
    EntityTable<NetLabel, NetLabelId> net_labels_;
    EntityTable<Junction, JunctionId> junctions_;
    EntityTable<PowerPort, PowerPortId> power_ports_;
    EntityTable<NoConnectMarker, NoConnectMarkerId> no_connect_markers_;
    EntityTable<SheetPort, SheetPortId> sheet_ports_;
    EntityTable<SymbolField, SymbolFieldId> symbol_fields_;
};

} // namespace volt
