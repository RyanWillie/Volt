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

    /** Return whether two sheet sizes have the same dimensions. */
    [[nodiscard]] friend bool operator==(SheetSize lhs, SheetSize rhs) noexcept = default;

  private:
    double width_;
    double height_;
};

/** Page orientation for a schematic drawing sheet. */
enum class SheetOrientation {
    Portrait,
    Landscape,
};

/** Margins between the physical sheet frame and the drawing area. */
class SheetMargins {
  public:
    /** Construct sheet margins from finite non-negative distances. */
    SheetMargins(double left = 10.0, double top = 10.0, double right = 10.0, double bottom = 10.0)
        : left_{left}, top_{top}, right_{right}, bottom_{bottom} {
        if (!std::isfinite(left_) || !std::isfinite(top_) || !std::isfinite(right_) ||
            !std::isfinite(bottom_) || left_ < 0.0 || top_ < 0.0 || right_ < 0.0 || bottom_ < 0.0) {
            throw std::invalid_argument{"Sheet margins must be finite and non-negative"};
        }
    }

    /** Return the left margin. */
    [[nodiscard]] double left() const noexcept { return left_; }

    /** Return the top margin. */
    [[nodiscard]] double top() const noexcept { return top_; }

    /** Return the right margin. */
    [[nodiscard]] double right() const noexcept { return right_; }

    /** Return the bottom margin. */
    [[nodiscard]] double bottom() const noexcept { return bottom_; }

    /** Return whether two margin sets are identical. */
    [[nodiscard]] friend bool operator==(SheetMargins lhs, SheetMargins rhs) noexcept = default;

  private:
    double left_;
    double top_;
    double right_;
    double bottom_;
};

/** Outer frame configuration for a schematic drawing sheet. */
class SheetFrame {
  public:
    /** Construct a sheet frame with optional visibility and margins. */
    explicit SheetFrame(bool visible = true, SheetMargins margins = {})
        : visible_{visible}, margins_{margins} {}

    /** Return whether the outer frame should be visible. */
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    /** Return the inner drawing-area margins. */
    [[nodiscard]] SheetMargins margins() const noexcept { return margins_; }

    /** Return whether two frame configurations are identical. */
    [[nodiscard]] friend bool operator==(SheetFrame lhs, SheetFrame rhs) noexcept = default;

  private:
    bool visible_;
    SheetMargins margins_;
};

/** Optional coordinate zone labels shown along a schematic sheet border. */
class SheetCoordinateZones {
  public:
    /** Construct border coordinate zones from positive row and column counts. */
    SheetCoordinateZones(std::size_t columns, std::size_t rows, bool visible = true)
        : columns_{columns}, rows_{rows}, visible_{visible} {
        if (columns_ == 0U || rows_ == 0U) {
            throw std::invalid_argument{"Sheet coordinate zones must have positive counts"};
        }
    }

    /** Return the number of horizontal zones. */
    [[nodiscard]] std::size_t columns() const noexcept { return columns_; }

    /** Return the number of vertical zones. */
    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }

    /** Return whether coordinate zone labels should be visible. */
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    /** Return whether two coordinate-zone configurations are identical. */
    [[nodiscard]] friend bool operator==(SheetCoordinateZones lhs,
                                         SheetCoordinateZones rhs) noexcept = default;

  private:
    std::size_t columns_;
    std::size_t rows_;
    bool visible_;
};

/** Optional visible grid metadata for a schematic sheet. */
class SheetGrid {
  public:
    /** Construct grid metadata from a positive finite spacing. */
    explicit SheetGrid(double spacing, bool visible = true) : spacing_{spacing}, visible_{visible} {
        if (!std::isfinite(spacing_) || spacing_ <= 0.0) {
            throw std::invalid_argument{"Sheet grid spacing must be finite and positive"};
        }
    }

    /** Return the grid spacing in sheet units. */
    [[nodiscard]] double spacing() const noexcept { return spacing_; }

    /** Return whether the grid should be visible. */
    [[nodiscard]] bool visible() const noexcept { return visible_; }

    /** Return whether two grid configurations are identical. */
    [[nodiscard]] friend bool operator==(SheetGrid lhs, SheetGrid rhs) noexcept = default;

  private:
    double spacing_;
    bool visible_;
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

    /** Return whether two title-block fields are identical. */
    [[nodiscard]] friend bool operator==(const TitleBlockField &lhs,
                                         const TitleBlockField &rhs) noexcept = default;

  private:
    std::string key_;
    std::string value_;
};

/** One key/value style metadata entry for a schematic region. */
class SheetRegionStyleField {
  public:
    /** Construct a region style field. */
    SheetRegionStyleField(std::string key, std::string value)
        : key_{std::move(key)}, value_{std::move(value)} {
        if (key_.empty()) {
            throw std::invalid_argument{"Region style field key must not be empty"};
        }
        if (value_.empty()) {
            throw std::invalid_argument{"Region style field value must not be empty"};
        }
    }

    /** Return the style field key. */
    [[nodiscard]] const std::string &key() const noexcept { return key_; }

    /** Return the style field value. */
    [[nodiscard]] const std::string &value() const noexcept { return value_; }

    /** Return whether two style fields are identical. */
    [[nodiscard]] friend bool operator==(const SheetRegionStyleField &lhs,
                                         const SheetRegionStyleField &rhs) noexcept = default;

  private:
    std::string key_;
    std::string value_;
};

/** Sheet-local rectangular bounds for a named schematic region. */
class SheetRegionBounds {
  public:
    /** Construct region bounds from an origin and positive size. */
    SheetRegionBounds(double x, double y, double width, double height)
        : x_{x}, y_{y}, width_{width}, height_{height} {
        if (!std::isfinite(x_) || !std::isfinite(y_) || !std::isfinite(width_) ||
            !std::isfinite(height_) || width_ <= 0.0 || height_ <= 0.0) {
            throw std::invalid_argument{
                "Sheet region bounds must be finite with positive width and height"};
        }
    }

    /** Return the region's sheet-local x origin. */
    [[nodiscard]] double x() const noexcept { return x_; }

    /** Return the region's sheet-local y origin. */
    [[nodiscard]] double y() const noexcept { return y_; }

    /** Return the region width. */
    [[nodiscard]] double width() const noexcept { return width_; }

    /** Return the region height. */
    [[nodiscard]] double height() const noexcept { return height_; }

    /** Return whether two region bounds are identical. */
    [[nodiscard]] friend bool operator==(SheetRegionBounds lhs,
                                         SheetRegionBounds rhs) noexcept = default;

  private:
    double x_;
    double y_;
    double width_;
    double height_;
};

/** A named functional drawing region on a physical schematic sheet. */
class SheetRegion {
  public:
    /** Construct a named rectangular sheet region. */
    SheetRegion(std::string name, std::string title, SheetRegionBounds bounds,
                std::vector<SheetRegionStyleField> style = {})
        : name_{std::move(name)}, title_{std::move(title)}, bounds_{bounds},
          style_{std::move(style)} {
        if (name_.empty()) {
            throw std::invalid_argument{"Sheet region name must not be empty"};
        }
        if (title_.empty()) {
            throw std::invalid_argument{"Sheet region title must not be empty"};
        }
    }

    /** Return the stable region name. */
    [[nodiscard]] const std::string &name() const noexcept { return name_; }

    /** Return the displayed region title. */
    [[nodiscard]] const std::string &title() const noexcept { return title_; }

    /** Return the sheet-local region bounds. */
    [[nodiscard]] SheetRegionBounds bounds() const noexcept { return bounds_; }

    /** Return region style metadata in insertion order. */
    [[nodiscard]] const std::vector<SheetRegionStyleField> &style() const noexcept {
        return style_;
    }

    /** Return whether two regions are identical. */
    [[nodiscard]] friend bool operator==(const SheetRegion &lhs,
                                         const SheetRegion &rhs) noexcept = default;

  private:
    std::string name_;
    std::string title_;
    SheetRegionBounds bounds_;
    std::vector<SheetRegionStyleField> style_;
};

/** Sheet-level metadata used by renderers and authoring tools. */
class SheetMetadata {
  public:
    /** Construct sheet metadata from a title, size, and optional title-block fields. */
    explicit SheetMetadata(std::string title, SheetSize size = {},
                           std::vector<TitleBlockField> title_block = {},
                           SheetOrientation orientation = SheetOrientation::Landscape,
                           SheetFrame frame = SheetFrame{},
                           std::optional<SheetCoordinateZones> coordinate_zones = std::nullopt,
                           std::optional<SheetGrid> grid = std::nullopt)
        : title_{std::move(title)}, size_{size}, orientation_{orientation},
          title_block_{std::move(title_block)}, frame_{frame}, coordinate_zones_{coordinate_zones},
          grid_{grid} {
        if (title_.empty()) {
            throw std::invalid_argument{"Sheet title must not be empty"};
        }
    }

    /** Return the displayed sheet title. */
    [[nodiscard]] const std::string &title() const noexcept { return title_; }

    /** Return the sheet drawing size. */
    [[nodiscard]] SheetSize size() const noexcept { return size_; }

    /** Return the sheet orientation. */
    [[nodiscard]] SheetOrientation orientation() const noexcept { return orientation_; }

    /** Return title-block fields in insertion order. */
    [[nodiscard]] const std::vector<TitleBlockField> &title_block() const noexcept {
        return title_block_;
    }

    /** Return the sheet frame and margins. */
    [[nodiscard]] SheetFrame frame() const noexcept { return frame_; }

    /** Return optional coordinate zone metadata. */
    [[nodiscard]] const std::optional<SheetCoordinateZones> &coordinate_zones() const noexcept {
        return coordinate_zones_;
    }

    /** Return optional visible grid metadata. */
    [[nodiscard]] const std::optional<SheetGrid> &grid() const noexcept { return grid_; }

    /** Return whether two metadata objects are identical. */
    [[nodiscard]] friend bool operator==(const SheetMetadata &lhs,
                                         const SheetMetadata &rhs) noexcept = default;

  private:
    std::string title_;
    SheetSize size_;
    SheetOrientation orientation_;
    std::vector<TitleBlockField> title_block_;
    SheetFrame frame_;
    std::optional<SheetCoordinateZones> coordinate_zones_;
    std::optional<SheetGrid> grid_;
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

    /** Return named functional regions in insertion order. */
    [[nodiscard]] const std::vector<SheetRegion> &regions() const noexcept { return regions_; }

    /** Return the region with this name, if it exists on this sheet. */
    [[nodiscard]] std::optional<std::size_t> region_by_name(const std::string &name) const {
        for (std::size_t index = 0; index < regions_.size(); ++index) {
            if (regions_[index].name() == name) {
                return index;
            }
        }
        return std::nullopt;
    }

    /** Return a region by sheet-local region index. */
    [[nodiscard]] const SheetRegion &region(std::size_t index) const {
        if (index >= regions_.size()) {
            throw std::out_of_range{"Sheet region index does not belong to this sheet"};
        }
        return regions_[index];
    }

  private:
    friend class Schematic;

    std::size_t add_region(SheetRegion region) {
        regions_.push_back(std::move(region));
        return regions_.size() - 1U;
    }

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
    std::vector<SheetRegion> regions_;
};

/** A placed schematic symbol that presents an existing logical component instance. */
class SymbolInstance {
  public:
    /** Construct a symbol instance over an existing logical component. */
    SymbolInstance(SymbolDefId symbol_definition, ComponentId component, Point position,
                   SchematicOrientation orientation = SchematicOrientation::Right,
                   std::optional<std::size_t> authored_region = std::nullopt)
        : symbol_definition_{symbol_definition}, component_{component}, position_{position},
          orientation_{orientation}, authored_region_{authored_region} {}

    /** Return the reusable symbol definition used by this placement. */
    [[nodiscard]] SymbolDefId symbol_definition() const noexcept { return symbol_definition_; }

    /** Return the logical component instance presented by this placement. */
    [[nodiscard]] ComponentId component() const noexcept { return component_; }

    /** Return the sheet-local symbol origin. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the symbol orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept {
        return authored_region_;
    }

  private:
    SymbolDefId symbol_definition_;
    ComponentId component_;
    Point position_;
    SchematicOrientation orientation_;
    std::optional<std::size_t> authored_region_;
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
        : WireRun{net, std::move(points), route_intent, std::nullopt} {}

    /** Construct a wire run over an existing logical net with authored-region metadata. */
    WireRun(NetId net, std::vector<Point> points, RouteIntent route_intent,
            std::optional<std::size_t> authored_region)
        : net_{net}, points_{std::move(points)}, route_intent_{route_intent},
          authored_region_{authored_region} {
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

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept {
        return authored_region_;
    }

  private:
    NetId net_;
    std::vector<Point> points_;
    RouteIntent route_intent_;
    std::optional<std::size_t> authored_region_;
};

/** A schematic label whose visible text is derived from a canonical logical net name. */
class NetLabel {
  public:
    /** Construct a net label over an existing logical net. */
    NetLabel(NetId net, Point position,
             SchematicOrientation orientation = SchematicOrientation::Right,
             std::optional<std::size_t> authored_region = std::nullopt,
             std::optional<std::string> label = std::nullopt)
        : net_{net}, position_{position}, orientation_{orientation},
          authored_region_{authored_region}, label_{std::move(label)} {
        if (label_ && label_->empty()) {
            throw std::invalid_argument{"Net label display text must not be empty"};
        }
    }

    /** Return the canonical logical net named by this label. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the label anchor position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the label orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept {
        return authored_region_;
    }

    /** Return optional display text for the label. Falls back to the logical net name. */
    [[nodiscard]] const std::optional<std::string> &label() const noexcept { return label_; }

  private:
    NetId net_;
    Point position_;
    SchematicOrientation orientation_;
    std::optional<std::size_t> authored_region_;
    std::optional<std::string> label_;
};

/** An explicit junction dot over an existing logical net. */
class Junction {
  public:
    /** Construct a junction over an existing logical net. */
    Junction(NetId net, Point position, std::optional<std::size_t> authored_region = std::nullopt)
        : net_{net}, position_{position}, authored_region_{authored_region} {}

    /** Return the canonical logical net joined by this junction. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the junction position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept {
        return authored_region_;
    }

  private:
    NetId net_;
    Point position_;
    std::optional<std::size_t> authored_region_;
};

/** One-terminal marker visual style. */
enum class PowerPortKind {
    Power,
    Ground,
};

/** A schematic one-terminal marker over an existing logical net. */
class PowerPort {
  public:
    /** Construct a one-terminal marker over an existing logical net. */
    PowerPort(NetId net, PowerPortKind kind, Point position,
              SchematicOrientation orientation = SchematicOrientation::Up,
              std::optional<std::size_t> authored_region = std::nullopt,
              std::optional<std::string> label = std::nullopt)
        : net_{net}, kind_{kind}, position_{position}, orientation_{orientation},
          authored_region_{authored_region}, label_{std::move(label)} {
        if (label_.has_value() && label_->empty()) {
            throw std::invalid_argument{"Schematic power port label must not be empty"};
        }
    }

    /** Return the canonical logical net presented by this port. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return whether this is rendered as a power or ground port. */
    [[nodiscard]] PowerPortKind kind() const noexcept { return kind_; }

    /** Return the port anchor position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the port orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept {
        return authored_region_;
    }

    /** Return the optional presentation label used instead of the logical net name. */
    [[nodiscard]] const std::optional<std::string> &label() const noexcept { return label_; }

  private:
    NetId net_;
    PowerPortKind kind_;
    Point position_;
    SchematicOrientation orientation_;
    std::optional<std::size_t> authored_region_;
    std::optional<std::string> label_;
};

/** A schematic no-connect marker tied to an existing concrete pin. */
class NoConnectMarker {
  public:
    /** Construct a no-connect marker for an existing concrete pin. */
    NoConnectMarker(PinId pin, Point position,
                    SchematicOrientation orientation = SchematicOrientation::Right,
                    std::string reason = {},
                    std::optional<std::size_t> authored_region = std::nullopt)
        : pin_{pin}, position_{position}, orientation_{orientation}, reason_{std::move(reason)},
          authored_region_{authored_region} {}

    /** Return the concrete pin marked as intentionally open. */
    [[nodiscard]] PinId pin() const noexcept { return pin_; }

    /** Return the marker position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the marker orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

    /** Return the optional author-supplied reason for the no-connect marker. */
    [[nodiscard]] const std::string &reason() const noexcept { return reason_; }

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept {
        return authored_region_;
    }

  private:
    PinId pin_;
    Point position_;
    SchematicOrientation orientation_;
    std::string reason_;
    std::optional<std::size_t> authored_region_;
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
              SchematicOrientation orientation = SchematicOrientation::Right,
              std::optional<std::size_t> authored_region = std::nullopt)
        : net_{net}, name_{std::move(name)}, kind_{kind}, position_{position},
          orientation_{orientation}, authored_region_{authored_region} {
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

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept {
        return authored_region_;
    }

  private:
    NetId net_;
    std::string name_;
    SheetPortKind kind_;
    Point position_;
    SchematicOrientation orientation_;
    std::optional<std::size_t> authored_region_;
};

/** A placed symbol field owned by an existing symbol instance. */
class SymbolField {
  public:
    /** Construct a symbol field for an existing symbol instance. */
    SymbolField(SymbolInstanceId symbol_instance, std::string name, std::string value,
                Point position, SchematicOrientation orientation = SchematicOrientation::Right,
                std::optional<std::size_t> authored_region = std::nullopt)
        : symbol_instance_{symbol_instance}, name_{std::move(name)}, value_{std::move(value)},
          position_{position}, orientation_{orientation}, authored_region_{authored_region} {
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

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept {
        return authored_region_;
    }

  private:
    SymbolInstanceId symbol_instance_;
    std::string name_;
    std::string value_;
    Point position_;
    SchematicOrientation orientation_;
    std::optional<std::size_t> authored_region_;
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

    /** Add a named presentation region to a schematic sheet. */
    [[nodiscard]] std::size_t add_sheet_region(SheetId sheet, SheetRegion region) {
        require_sheet(sheet);
        auto &sheet_ref = sheets_.get(sheet);
        if (sheet_ref.region_by_name(region.name()).has_value()) {
            throw std::logic_error{"Sheet region name already exists"};
        }

        return sheet_ref.add_region(std::move(region));
    }

    /** Place a symbol on a sheet for an existing logical component instance. */
    [[nodiscard]] SymbolInstanceId place_symbol(SheetId sheet, SymbolInstance instance) {
        require_sheet(sheet);
        require_symbol_definition(instance.symbol_definition());
        static_cast<void>(circuit_.component(instance.component()));
        require_symbol_matches_component(instance.symbol_definition(), instance.component());
        require_authored_region(sheet, instance.authored_region());

        const auto id = symbol_instances_.insert(std::move(instance));
        sheets_.get(sheet).add_symbol_instance(id);
        return id;
    }

    /** Add an explicit junction over an existing logical net. */
    [[nodiscard]] JunctionId add_junction(SheetId sheet, Junction junction) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(junction.net()));
        require_authored_region(sheet, junction.authored_region());
        require_junction_does_not_touch_different_net(sheet, junction);

        const auto id = junctions_.insert(std::move(junction));
        sheets_.get(sheet).add_junction(id);
        return id;
    }

    /** Add a one-terminal marker over an existing logical net. */
    [[nodiscard]] PowerPortId add_power_port(SheetId sheet, PowerPort port) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(port.net()));
        require_authored_region(sheet, port.authored_region());

        const auto id = power_ports_.insert(std::move(port));
        sheets_.get(sheet).add_power_port(id);
        return id;
    }

    /** Add a generic terminal marker over an existing logical net. */
    [[nodiscard]] PowerPortId add_terminal_marker(SheetId sheet, PowerPort marker) {
        return add_power_port(sheet, std::move(marker));
    }

    /** Add a no-connect marker for an existing concrete pin. */
    [[nodiscard]] NoConnectMarkerId add_no_connect_marker(SheetId sheet, NoConnectMarker marker) {
        require_sheet(sheet);
        static_cast<void>(circuit_.pin(marker.pin()));
        require_authored_region(sheet, marker.authored_region());

        const auto id = no_connect_markers_.insert(std::move(marker));
        sheets_.get(sheet).add_no_connect_marker(id);
        return id;
    }

    /** Add a sheet/off-page port over an existing logical net. */
    [[nodiscard]] SheetPortId add_sheet_port(SheetId sheet, SheetPort port) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(port.net()));
        require_authored_region(sheet, port.authored_region());

        const auto id = sheet_ports_.insert(std::move(port));
        sheets_.get(sheet).add_sheet_port(id);
        return id;
    }

    /** Add a placed field for a symbol instance on the same sheet. */
    [[nodiscard]] SymbolFieldId add_symbol_field(SheetId sheet, SymbolField field) {
        require_sheet(sheet);
        require_symbol_instance(field.symbol_instance());
        require_authored_region(sheet, field.authored_region());
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
        require_authored_region(sheet, wire.authored_region());
        require_wire_run_does_not_collide_with_different_net(sheet, wire);

        const auto id = wire_runs_.insert(std::move(wire));
        sheets_.get(sheet).add_wire_run(id);
        return id;
    }

    /** Add a net label on a sheet for an existing logical net. */
    [[nodiscard]] NetLabelId add_net_label(SheetId sheet, NetLabel label) {
        require_sheet(sheet);
        static_cast<void>(circuit_.net(label.net()));
        require_authored_region(sheet, label.authored_region());

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

    /** Return a region with this name on the given sheet, if it exists. */
    [[nodiscard]] std::optional<std::size_t> sheet_region_by_name(SheetId sheet,
                                                                  const std::string &name) const {
        require_sheet(sheet);
        return sheets_.get(sheet).region_by_name(name);
    }

    /** Return a symbol definition by ID. */
    [[nodiscard]] const SymbolDefinition &symbol_definition(SymbolDefId id) const {
        return symbol_definitions_.get(id);
    }

    /** Return a schematic sheet by ID. */
    [[nodiscard]] const Sheet &sheet(SheetId id) const { return sheets_.get(id); }

    /** Return a named presentation region by sheet and sheet-local region index. */
    [[nodiscard]] const SheetRegion &sheet_region(SheetId sheet, std::size_t region) const {
        require_sheet(sheet);
        return sheets_.get(sheet).region(region);
    }

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

    void require_authored_region(SheetId sheet, const std::optional<std::size_t> &region) const {
        if (region.has_value() && region.value() >= sheets_.get(sheet).regions().size()) {
            throw std::out_of_range{"Authored schematic region does not belong to this sheet"};
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
