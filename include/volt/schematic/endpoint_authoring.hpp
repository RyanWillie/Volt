#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <volt/schematic/schematic.hpp>

namespace volt {

/**
 * Author schematic presentation entities whose logical net is resolved from typed endpoints.
 *
 * This service borrows one Schematic owner. It reads the owner's Circuit dependency and commits
 * presentation data only through the public Schematic mutation boundary.
 */
class SchematicEndpointAuthoring {
  public:
    /** Borrow a mutable Schematic owner for endpoint-based presentation authoring. */
    explicit SchematicEndpointAuthoring(Schematic &schematic) noexcept
        : schematic_{schematic}, circuit_{schematic.circuit()} {}

    /** Add a power or ground marker whose net is inferred or checked from an endpoint. */
    [[nodiscard]] PowerPortId
    add_power_port(SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint,
                   PowerPortKind kind, SchematicOrientation orientation = SchematicOrientation::Up,
                   std::optional<std::size_t> authored_region = std::nullopt,
                   std::optional<std::string> label = std::nullopt);

    /** Add a generic terminal marker whose net is inferred or checked from an endpoint. */
    [[nodiscard]] PowerPortId
    add_terminal_marker(SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint,
                        PowerPortKind kind,
                        SchematicOrientation orientation = SchematicOrientation::Up,
                        std::optional<std::size_t> authored_region = std::nullopt,
                        std::optional<std::string> label = std::nullopt);

    /** Add a sheet or off-page port whose net is inferred or checked from an endpoint. */
    [[nodiscard]] SheetPortId
    add_sheet_port(SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint,
                   std::string name, SheetPortKind kind,
                   SchematicOrientation orientation = SchematicOrientation::Right,
                   std::optional<std::size_t> authored_region = std::nullopt);

    /** Add a wire run whose net is inferred or checked from its endpoints. */
    [[nodiscard]] WireRunId add_wire_run(SheetId sheet, std::optional<NetId> net,
                                         std::vector<Point> points,
                                         const std::vector<SchematicEndpoint> &endpoints,
                                         RouteIntent route_intent = RouteIntent::Direct,
                                         std::optional<std::size_t> authored_region = std::nullopt);

    /** Add a net label whose net is inferred or checked from an endpoint. */
    [[nodiscard]] NetLabelId
    add_net_label(SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint,
                  SchematicOrientation orientation = SchematicOrientation::Right,
                  std::optional<std::size_t> authored_region = std::nullopt,
                  std::optional<std::string> label = std::nullopt,
                  SchematicTextStyle style = SchematicTextStyle{TextHorizontalAlignment::Start},
                  std::optional<Point> text_position = std::nullopt);

    /** Add a junction whose net is inferred or checked from an endpoint. */
    [[nodiscard]] JunctionId
    add_junction(SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint,
                 std::optional<std::size_t> authored_region = std::nullopt);

  private:
    [[nodiscard]] std::string net_label(NetId net) const;
    [[nodiscard]] std::string pin_label(PinId pin) const;
    [[nodiscard]] std::string endpoint_label(const SchematicEndpoint &endpoint) const;
    [[nodiscard]] bool net_has_name(NetId net, const std::string &name) const;

    [[nodiscard]] std::optional<NetId> infer_endpoint_net(const SchematicEndpoint &endpoint) const;
    void require_endpoint_matches_net(const SchematicEndpoint &endpoint, NetId net) const;
    [[nodiscard]] NetId resolve_endpoint_net(std::optional<NetId> net,
                                             const SchematicEndpoint &endpoint,
                                             std::string_view action) const;
    [[nodiscard]] NetId
    resolve_wire_endpoint_net(std::optional<NetId> net,
                              const std::vector<SchematicEndpoint> &endpoints) const;

    Schematic &schematic_;
    const Circuit &circuit_;
};

} // namespace volt
