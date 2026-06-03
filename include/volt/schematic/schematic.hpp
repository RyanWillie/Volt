#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <volt/circuit/circuit.hpp>
#include <volt/schematic/geometry.hpp>
#include <volt/schematic/schematic_items.hpp>
#include <volt/schematic/schematic_items_model.hpp>
#include <volt/schematic/schematic_library_model.hpp>
#include <volt/schematic/schematic_sheet.hpp>
#include <volt/schematic/schematic_sheet_metadata.hpp>
#include <volt/schematic/schematic_sheet_model.hpp>
#include <volt/schematic/symbols.hpp>
#include <volt/schematic/wire_topology.hpp>

namespace volt {

/**
 * Kernel-owned schematic projection over a logical circuit, and aggregate root of the
 * schematic model.
 *
 * Responsibility: composes the library, sheet, and items subsystems into a schematic view over
 *   existing logical entities.
 * Invariants: references existing logical nets/pins/components and never owns connectivity;
 *   consistency issues are reported as diagnostics.
 * Collaborators: read-only consumer of Circuit; composes the Schematic*Model subsystems;
 *   schematic checks run as a RuleSet<Schematic>. See
 *   docs/superpowers/specs/2026-06-02-volt-kernel-architecture-design.md.
 */
class Schematic {
  public:
    /** Construct a schematic projection for one logical circuit context. */
    explicit Schematic(const Circuit &circuit);

    /** Replace projection contents with another schematic over the same logical circuit. */
    void replace_with(Schematic replacement);

    /** Store a reusable symbol definition and return its stable schematic ID. */
    [[nodiscard]] SymbolDefId add_symbol_definition(SymbolDefinition definition);

    /** Store a schematic sheet and return its stable schematic ID. */
    [[nodiscard]] SheetId add_sheet(Sheet sheet);

    /** Add a named presentation region to a schematic sheet. */
    [[nodiscard]] std::size_t add_sheet_region(SheetId sheet, SheetRegion region);

    /** Place a symbol on a sheet for an existing logical component instance. */
    [[nodiscard]] SymbolInstanceId place_symbol(SheetId sheet, SymbolInstance instance);

    /** Add an explicit junction over an existing logical net. */
    [[nodiscard]] JunctionId add_junction(SheetId sheet, Junction junction);

    /** Add a one-terminal marker over an existing logical net. */
    [[nodiscard]] PowerPortId add_power_port(SheetId sheet, PowerPort port);

    /** Add a one-terminal marker whose net is inferred or checked from an authoring endpoint. */
    [[nodiscard]] PowerPortId
    add_power_port_for_endpoint(SheetId sheet, std::optional<NetId> net,
                                const SchematicEndpoint &endpoint, PowerPortKind kind,
                                SchematicOrientation orientation = SchematicOrientation::Up,
                                std::optional<std::size_t> authored_region = std::nullopt,
                                std::optional<std::string> label = std::nullopt);

    /** Add a generic terminal marker over an existing logical net. */
    [[nodiscard]] PowerPortId add_terminal_marker(SheetId sheet, PowerPort marker);

    /** Add a generic terminal marker whose net is inferred or checked from an endpoint. */
    [[nodiscard]] PowerPortId
    add_terminal_marker_for_endpoint(SheetId sheet, std::optional<NetId> net,
                                     const SchematicEndpoint &endpoint, PowerPortKind kind,
                                     SchematicOrientation orientation = SchematicOrientation::Up,
                                     std::optional<std::size_t> authored_region = std::nullopt,
                                     std::optional<std::string> label = std::nullopt);

    /** Add a no-connect marker for an existing concrete pin. */
    [[nodiscard]] NoConnectMarkerId add_no_connect_marker(SheetId sheet, NoConnectMarker marker);

    /** Add a sheet/off-page port over an existing logical net. */
    [[nodiscard]] SheetPortId add_sheet_port(SheetId sheet, SheetPort port);

    /** Add a sheet/off-page port whose net is inferred or checked from an endpoint. */
    [[nodiscard]] SheetPortId
    add_sheet_port_for_endpoint(SheetId sheet, std::optional<NetId> net,
                                const SchematicEndpoint &endpoint, std::string name,
                                SheetPortKind kind,
                                SchematicOrientation orientation = SchematicOrientation::Right,
                                std::optional<std::size_t> authored_region = std::nullopt);

    /** Add a placed field for a symbol instance on the same sheet. */
    [[nodiscard]] SymbolFieldId add_symbol_field(SheetId sheet, SymbolField field);

    /** Add a wire run on a sheet for an existing logical net. */
    [[nodiscard]] WireRunId add_wire_run(SheetId sheet, WireRun wire);

    /** Add a wire run whose logical net is inferred or checked from authoring endpoints. */
    [[nodiscard]] WireRunId
    add_wire_run_for_endpoints(SheetId sheet, std::optional<NetId> net, std::vector<Point> points,
                               const std::vector<SchematicEndpoint> &endpoints,
                               RouteIntent route_intent = RouteIntent::Direct,
                               std::optional<std::size_t> authored_region = std::nullopt);

    /** Add a net label on a sheet for an existing logical net. */
    [[nodiscard]] NetLabelId add_net_label(SheetId sheet, NetLabel label);

    /** Add a net label whose net is inferred or checked from an authoring endpoint. */
    [[nodiscard]] NetLabelId add_net_label_for_endpoint(
        SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint,
        SchematicOrientation orientation = SchematicOrientation::Right,
        std::optional<std::size_t> authored_region = std::nullopt,
        std::optional<std::string> label = std::nullopt,
        SchematicTextStyle style = SchematicTextStyle{TextHorizontalAlignment::Start},
        std::optional<Point> text_position = std::nullopt);

    /** Add a junction whose net is inferred or checked from an authoring endpoint. */
    [[nodiscard]] JunctionId
    add_junction_for_endpoint(SheetId sheet, std::optional<NetId> net,
                              const SchematicEndpoint &endpoint,
                              std::optional<std::size_t> authored_region = std::nullopt);

    /** Return the symbol definition with this name, if it exists. */
    [[nodiscard]] std::optional<SymbolDefId>
    symbol_definition_by_name(const std::string &name) const;

    /** Return the sheet with this name, if it exists. */
    [[nodiscard]] std::optional<SheetId> sheet_by_name(const std::string &name) const;

    /** Return a region with this name on the given sheet, if it exists. */
    [[nodiscard]] std::optional<std::size_t> sheet_region_by_name(SheetId sheet,
                                                                  const std::string &name) const;

    /** Return a symbol definition by ID. */
    [[nodiscard]] const SymbolDefinition &symbol_definition(SymbolDefId id) const;

    /** Return a schematic sheet by ID. */
    [[nodiscard]] const Sheet &sheet(SheetId id) const { return sheets_.sheet(id); }

    /** Return a named presentation region by sheet and sheet-local region index. */
    [[nodiscard]] const SheetRegion &sheet_region(SheetId sheet, std::size_t region) const;

    /** Return a placed symbol instance by ID. */
    [[nodiscard]] const SymbolInstance &symbol_instance(SymbolInstanceId id) const;

    /** Return a wire run by ID. */
    [[nodiscard]] const WireRun &wire_run(WireRunId id) const { return items_.wire_run(id); }

    /** Return a net label by ID. */
    [[nodiscard]] const NetLabel &net_label(NetLabelId id) const { return items_.net_label(id); }

    /** Move net label text without changing its presentation anchor. */
    void move_net_label_text(NetLabelId id, Point position);

    /** Return an explicit junction by ID. */
    [[nodiscard]] const Junction &junction(JunctionId id) const { return items_.junction(id); }

    /** Return a power or ground port by ID. */
    [[nodiscard]] const PowerPort &power_port(PowerPortId id) const {
        return items_.power_port(id);
    }

    /** Move power or ground marker label text without changing the marker anchor. */
    void move_power_port_label(PowerPortId id, Point position);

    /** Return a no-connect marker by ID. */
    [[nodiscard]] const NoConnectMarker &no_connect_marker(NoConnectMarkerId id) const;

    /** Return a sheet/off-page port by ID. */
    [[nodiscard]] const SheetPort &sheet_port(SheetPortId id) const {
        return items_.sheet_port(id);
    }

    /** Return a placed symbol field by ID. */
    [[nodiscard]] const SymbolField &symbol_field(SymbolFieldId id) const;

    /** Move a symbol field presentation anchor. */
    void move_symbol_field(SymbolFieldId id, Point position);

    /** Return the logical circuit this schematic projection references. */
    [[nodiscard]] const Circuit &circuit() const noexcept { return circuit_; }

    /** Return the number of stored symbol definitions. */
    [[nodiscard]] std::size_t symbol_definition_count() const noexcept;

    /** Return the number of stored sheets. */
    [[nodiscard]] std::size_t sheet_count() const noexcept { return sheets_.sheet_count(); }

    /** Return the number of stored symbol instances. */
    [[nodiscard]] std::size_t symbol_instance_count() const noexcept;

    /** Return the number of stored wire runs. */
    [[nodiscard]] std::size_t wire_run_count() const noexcept { return items_.wire_run_count(); }

    /** Return the number of stored net labels. */
    [[nodiscard]] std::size_t net_label_count() const noexcept { return items_.net_label_count(); }

    /** Return the number of stored explicit junctions. */
    [[nodiscard]] std::size_t junction_count() const noexcept { return items_.junction_count(); }

    /** Return the number of stored power and ground ports. */
    [[nodiscard]] std::size_t power_port_count() const noexcept {
        return items_.power_port_count();
    }

    /** Return the number of stored no-connect markers. */
    [[nodiscard]] std::size_t no_connect_marker_count() const noexcept;

    /** Return the number of stored sheet/off-page ports. */
    [[nodiscard]] std::size_t sheet_port_count() const noexcept {
        return items_.sheet_port_count();
    }

    /** Return the number of stored symbol fields. */
    [[nodiscard]] std::size_t symbol_field_count() const noexcept {
        return items_.symbol_field_count();
    }

  private:
    void require_sheet(SheetId sheet) const;

    void require_symbol_definition(SymbolDefId symbol_definition) const;

    void require_symbol_instance(SymbolInstanceId instance) const;

    void require_authored_region(SheetId sheet, const std::optional<std::size_t> &region) const;

    void require_symbol_matches_component(SymbolDefId symbol_definition,
                                          ComponentId component) const;

    [[nodiscard]] bool sheet_contains_symbol_instance(SheetId sheet,
                                                      SymbolInstanceId instance) const;

    [[nodiscard]] static bool wire_contains_point(const WireRun &wire, Point point);

    [[nodiscard]] bool has_junction_on_segments(SheetId sheet, SchematicSegment first,
                                                SchematicSegment second) const;

    void require_junction_does_not_touch_different_net(SheetId sheet,
                                                       const Junction &junction) const;

    void require_wire_run_does_not_collide_with_different_net(SheetId sheet,
                                                              const WireRun &wire) const;

    [[nodiscard]] std::string net_label(NetId net) const;

    [[nodiscard]] std::string pin_label(PinId pin) const;

    [[nodiscard]] std::string endpoint_label(const SchematicEndpoint &endpoint) const;

    [[nodiscard]] std::optional<NetId> infer_endpoint_net(const SchematicEndpoint &endpoint) const;

    void require_endpoint_matches_net(const SchematicEndpoint &endpoint, NetId net) const;

    [[nodiscard]] NetId resolve_endpoint_net(std::optional<NetId> net,
                                             const SchematicEndpoint &endpoint,
                                             std::string_view action) const;

    [[nodiscard]] NetId
    resolve_wire_endpoint_net(std::optional<NetId> net,
                              const std::vector<SchematicEndpoint> &endpoints) const;

    const Circuit &circuit_;
    SchematicLibraryModel library_;
    SchematicSheetModel sheets_;
    SchematicItemsModel items_;
};

} // namespace volt
