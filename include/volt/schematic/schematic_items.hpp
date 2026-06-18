#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <volt/core/ids.hpp>
#include <volt/schematic/geometry.hpp>
#include <volt/schematic/symbols.hpp>

namespace volt {

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
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept;

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
    WireRun(NetId net, std::vector<Point> points, RouteIntent route_intent = RouteIntent::Direct);

    /** Construct a wire run over an existing logical net with authored-region metadata. */
    WireRun(NetId net, std::vector<Point> points, RouteIntent route_intent,
            std::optional<std::size_t> authored_region);

    /** Return the canonical logical net presented by this wire run. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the wire polyline points in drawing order. */
    [[nodiscard]] const std::vector<Point> &points() const noexcept { return points_; }

    /** Return the authoring route intent for this wire run. */
    [[nodiscard]] RouteIntent route_intent() const noexcept { return route_intent_; }

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept;

  private:
    NetId net_;
    std::vector<Point> points_;
    RouteIntent route_intent_;
    std::optional<std::size_t> authored_region_;
};

/** Authoring endpoint used by kernel-owned schematic net inference. */
class SchematicEndpoint {
  public:
    /** Construct a plain sheet point with no logical endpoint reference. */
    explicit SchematicEndpoint(Point position);

    /** Construct a sheet point tied to an existing concrete logical pin. */
    SchematicEndpoint(Point position, PinId pin);

    /** Construct a sheet point tied to an existing schematic port net. */
    static SchematicEndpoint port(Point position, NetId net);

    /** Return the sheet-local endpoint position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the optional concrete pin reference for this endpoint. */
    [[nodiscard]] const std::optional<PinId> &pin() const noexcept { return pin_; }

    /** Return the optional schematic port net reference for this endpoint. */
    [[nodiscard]] const std::optional<NetId> &port_net() const noexcept { return port_net_; }

  private:
    Point position_;
    std::optional<PinId> pin_;
    std::optional<NetId> port_net_;
};

/** A schematic label whose visible text is derived from a canonical logical net name. */
class NetLabel {
  public:
    /** Construct a net label over an existing logical net. */
    NetLabel(NetId net, Point position,
             SchematicOrientation orientation = SchematicOrientation::Right,
             std::optional<std::size_t> authored_region = std::nullopt,
             std::optional<std::string> label = std::nullopt,
             SchematicTextStyle style = SchematicTextStyle{TextHorizontalAlignment::Start},
             std::optional<Point> text_position = std::nullopt);

    /** Return the canonical logical net named by this label. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return the label anchor position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the rendered text position, falling back to the anchor position. */
    [[nodiscard]] Point text_position() const noexcept;

    /** Return the optional explicit rendered text position. */
    [[nodiscard]] const std::optional<Point> &explicit_text_position() const noexcept;

    /** Return the label orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept;

    /** Return optional display text for the label. Falls back to the logical net name. */
    [[nodiscard]] const std::optional<std::string> &label() const noexcept { return label_; }

    /** Return generic text presentation metadata. */
    [[nodiscard]] SchematicTextStyle style() const noexcept { return style_; }

    /** Return a copy with explicit rendered text position changed. */
    [[nodiscard]] NetLabel with_text_position(Point position) const;

  private:
    NetId net_;
    Point position_;
    SchematicOrientation orientation_;
    std::optional<std::size_t> authored_region_;
    std::optional<std::string> label_;
    SchematicTextStyle style_;
    std::optional<Point> text_position_;
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
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept;

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
              std::optional<std::string> label = std::nullopt,
              std::optional<Point> label_position = std::nullopt);

    /** Return the canonical logical net presented by this port. */
    [[nodiscard]] NetId net() const noexcept { return net_; }

    /** Return whether this is rendered as a power or ground port. */
    [[nodiscard]] PowerPortKind kind() const noexcept { return kind_; }

    /** Return the port anchor position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the port orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept;

    /** Return the optional presentation label used instead of the logical net name. */
    [[nodiscard]] const std::optional<std::string> &label() const noexcept { return label_; }

    /** Return optional explicit rendered label position. */
    [[nodiscard]] const std::optional<Point> &explicit_label_position() const noexcept;

    /** Return a copy with explicit rendered label position changed. */
    [[nodiscard]] PowerPort with_label_position(Point position) const;

  private:
    NetId net_;
    PowerPortKind kind_;
    Point position_;
    SchematicOrientation orientation_;
    std::optional<std::size_t> authored_region_;
    std::optional<std::string> label_;
    std::optional<Point> label_position_;
};

/** A schematic no-connect marker tied to an existing concrete pin. */
class NoConnectMarker {
  public:
    /** Construct a no-connect marker for an existing concrete pin. */
    NoConnectMarker(PinId pin, Point position,
                    SchematicOrientation orientation = SchematicOrientation::Right,
                    std::string reason = {},
                    std::optional<std::size_t> authored_region = std::nullopt);

    /** Return the concrete pin marked as intentionally open. */
    [[nodiscard]] PinId pin() const noexcept { return pin_; }

    /** Return the marker position. */
    [[nodiscard]] Point position() const noexcept { return position_; }

    /** Return the marker orientation. */
    [[nodiscard]] SchematicOrientation orientation() const noexcept { return orientation_; }

    /** Return the optional author-supplied reason for the no-connect marker. */
    [[nodiscard]] const std::string &reason() const noexcept { return reason_; }

    /** Return the sheet-local region this object was authored through, if any. */
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept;

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
              std::optional<std::size_t> authored_region = std::nullopt);

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
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept;

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
                std::optional<std::size_t> authored_region = std::nullopt,
                SchematicTextStyle style = SchematicTextStyle{});

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
    [[nodiscard]] const std::optional<std::size_t> &authored_region() const noexcept;

    /** Return generic text presentation metadata. */
    [[nodiscard]] SchematicTextStyle style() const noexcept { return style_; }

    /** Return a copy with the field anchor position changed. */
    [[nodiscard]] SymbolField with_position(Point position) const;

  private:
    SymbolInstanceId symbol_instance_;
    std::string name_;
    std::string value_;
    Point position_;
    SchematicOrientation orientation_;
    std::optional<std::size_t> authored_region_;
    SchematicTextStyle style_;
};

} // namespace volt
