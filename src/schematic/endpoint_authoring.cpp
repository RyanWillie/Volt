#include <volt/schematic/endpoint_authoring.hpp>

#include <volt/circuit/connectivity/queries.hpp>
#include <volt/core/errors.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace volt {

[[nodiscard]] std::optional<NetId>
SchematicEndpointAuthoring::infer_endpoint_net(const SchematicEndpoint &endpoint) const {
    if (endpoint.pin().has_value()) {
        static_cast<void>(circuit_.get(endpoint.pin().value()));
        const auto pin_net = queries::net_of(circuit_, endpoint.pin().value());
        if (!pin_net.has_value()) {
            throw KernelArgumentError{ErrorCode::InvalidState,
                                      "Schematic endpoint " + pin_label(endpoint.pin().value()) +
                                          " is not connected to any logical net",
                                      EntityRef::pin(endpoint.pin().value())};
        }
        return pin_net.value();
    }
    if (endpoint.port_net().has_value()) {
        static_cast<void>(circuit_.get(endpoint.port_net().value()));
        return endpoint.port_net().value();
    }
    return std::nullopt;
}

void SchematicEndpointAuthoring::require_endpoint_matches_net(const SchematicEndpoint &endpoint,
                                                              NetId net) const {
    static_cast<void>(circuit_.get(net));
    const auto endpoint_net = infer_endpoint_net(endpoint);
    if (endpoint_net.has_value() && endpoint_net.value() != net) {
        if (endpoint.pin().has_value()) {
            throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                      "Schematic endpoint " + pin_label(endpoint.pin().value()) +
                                          ": the pin belongs to " +
                                          net_label(endpoint_net.value()) + " instead of " +
                                          net_label(net),
                                      EntityRef::pin(endpoint.pin().value())};
        }
        throw KernelArgumentError{ErrorCode::CrossReferenceViolation,
                                  "Schematic endpoint port belongs to " +
                                      net_label(endpoint_net.value()) + " instead of " +
                                      net_label(net),
                                  EntityRef::net(endpoint_net.value())};
    }
}

[[nodiscard]] NetId SchematicEndpointAuthoring::resolve_endpoint_net(
    std::optional<NetId> net, const SchematicEndpoint &endpoint, std::string_view action) const {
    if (net.has_value()) {
        require_endpoint_matches_net(endpoint, net.value());
        return net.value();
    }

    const auto inferred = infer_endpoint_net(endpoint);
    if (!inferred.has_value()) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  std::string{"Cannot infer logical net for "} +
                                      std::string{action} +
                                      " from a non-pin anchor; pass explicit net"};
    }
    return inferred.value();
}

[[nodiscard]] NetId SchematicEndpointAuthoring::resolve_wire_endpoint_net(
    std::optional<NetId> net, const std::vector<SchematicEndpoint> &endpoints) const {
    if (net.has_value()) {
        for (const auto &endpoint : endpoints) {
            require_endpoint_matches_net(endpoint, net.value());
        }
        return net.value();
    }
    if (endpoints.size() < 2U) {
        throw KernelArgumentError{ErrorCode::InvalidArgument,
                                  "Cannot infer schematic wire net without at least two endpoints"};
    }
    if (endpoints.size() != 2U || !endpoints.front().pin().has_value() ||
        !endpoints.back().pin().has_value()) {
        throw KernelArgumentError{
            ErrorCode::InvalidArgument,
            "Cannot infer schematic wire net unless both endpoints are placed pin anchors; "
            "pass explicit net"};
    }

    auto resolved = std::optional<NetId>{};
    auto resolved_endpoint = std::optional<std::size_t>{};
    for (std::size_t endpoint_index = 0; endpoint_index < endpoints.size(); ++endpoint_index) {
        const auto &endpoint = endpoints[endpoint_index];
        const auto endpoint_net = infer_endpoint_net(endpoint);
        if (!endpoint_net.has_value()) {
            throw KernelArgumentError{
                ErrorCode::InvalidArgument,
                "Cannot infer schematic wire net from a plain schematic point"};
        }
        if (resolved.has_value() && resolved.value() != endpoint_net.value()) {
            const auto &first = endpoints[resolved_endpoint.value()];
            throw KernelArgumentError{
                ErrorCode::CrossReferenceViolation,
                "Cannot infer schematic wire net because endpoints belong to different logical "
                "nets: " +
                    endpoint_label(first) + " is on " + net_label(resolved.value()) + ", but " +
                    endpoint_label(endpoint) + " is on " + net_label(endpoint_net.value()),
                EntityRef::pin(endpoint.pin().value())};
        }
        resolved = endpoint_net.value();
        resolved_endpoint = endpoint_index;
    }
    return resolved.value();
}

[[nodiscard]] PowerPortId SchematicEndpointAuthoring::add_power_port(
    SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint, PowerPortKind kind,
    SchematicOrientation orientation, std::optional<std::size_t> authored_region,
    std::optional<std::string> label) {
    const auto resolved_net = resolve_endpoint_net(net, endpoint, "schematic power port");
    if (label.has_value() && net_has_name(resolved_net, label.value())) {
        label = std::nullopt;
    }
    return schematic_.add_power_port(sheet,
                                     PowerPort{resolved_net, kind, endpoint.position(), orientation,
                                               authored_region, std::move(label)});
}

[[nodiscard]] PowerPortId SchematicEndpointAuthoring::add_terminal_marker(
    SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint, PowerPortKind kind,
    SchematicOrientation orientation, std::optional<std::size_t> authored_region,
    std::optional<std::string> label) {
    return add_power_port(sheet, net, endpoint, kind, orientation, authored_region,
                          std::move(label));
}

[[nodiscard]] SheetPortId
SchematicEndpointAuthoring::add_sheet_port(SheetId sheet, std::optional<NetId> net,
                                           const SchematicEndpoint &endpoint, std::string name,
                                           SheetPortKind kind, SchematicOrientation orientation,
                                           std::optional<std::size_t> authored_region) {
    const auto resolved_net = resolve_endpoint_net(net, endpoint, "schematic sheet port");
    return schematic_.add_sheet_port(sheet,
                                     SheetPort{resolved_net, std::move(name), kind,
                                               endpoint.position(), orientation, authored_region});
}

[[nodiscard]] WireRunId SchematicEndpointAuthoring::add_wire_run(
    SheetId sheet, std::optional<NetId> net, std::vector<Point> points,
    const std::vector<SchematicEndpoint> &endpoints, RouteIntent route_intent,
    std::optional<std::size_t> authored_region) {
    const auto resolved_net = resolve_wire_endpoint_net(net, endpoints);
    return schematic_.add_wire_run(
        sheet, WireRun{resolved_net, std::move(points), route_intent, authored_region});
}

[[nodiscard]] NetLabelId SchematicEndpointAuthoring::add_net_label(
    SheetId sheet, std::optional<NetId> net, const SchematicEndpoint &endpoint,
    SchematicOrientation orientation, std::optional<std::size_t> authored_region,
    std::optional<std::string> label, SchematicTextStyle style,
    std::optional<Point> text_position) {
    const auto resolved_net = resolve_endpoint_net(net, endpoint, "schematic net label");
    return schematic_.add_net_label(sheet, NetLabel{resolved_net, endpoint.position(), orientation,
                                                    authored_region, std::move(label), style,
                                                    text_position});
}

[[nodiscard]] JunctionId
SchematicEndpointAuthoring::add_junction(SheetId sheet, std::optional<NetId> net,
                                         const SchematicEndpoint &endpoint,
                                         std::optional<std::size_t> authored_region) {
    const auto resolved_net = resolve_endpoint_net(net, endpoint, "schematic junction");
    return schematic_.add_junction(sheet,
                                   Junction{resolved_net, endpoint.position(), authored_region});
}

} // namespace volt
