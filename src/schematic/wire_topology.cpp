#include <volt/schematic/wire_topology.hpp>

namespace volt {

[[nodiscard]] bool SchematicWireSegmentTopology::same_net() const noexcept {
    return net_relationship == SchematicWireNetRelationship::SameNet;
}

[[nodiscard]] bool SchematicWireSegmentTopology::different_net() const noexcept {
    return net_relationship == SchematicWireNetRelationship::DifferentNet;
}

[[nodiscard]] bool SchematicWireSegmentTopology::crossing() const noexcept {
    return relationship == SchematicSegmentRelationship::Crossing;
}

[[nodiscard]] bool SchematicWireSegmentTopology::crossing_without_junction() const noexcept {
    return crossing() && junction == SchematicJunction::Absent;
}

[[nodiscard]] bool SchematicWireSegmentTopology::crossing_with_junction() const noexcept {
    return crossing() && junction == SchematicJunction::Present;
}

[[nodiscard]] bool SchematicWireSegmentTopology::endpoint_touch() const noexcept {
    return relationship == SchematicSegmentRelationship::EndpointTouch;
}

[[nodiscard]] bool SchematicWireSegmentTopology::overlap() const noexcept {
    return relationship == SchematicSegmentRelationship::Overlap;
}

[[nodiscard]] bool SchematicWireSegmentTopology::visual_contact() const noexcept {
    return endpoint_touch() || overlap() || crossing_with_junction();
}

void SchematicWirePairTopology::include(SchematicWireSegmentTopology segment) noexcept {
    crossing_without_junction = crossing_without_junction || segment.crossing_without_junction();
    crossing_with_junction = crossing_with_junction || segment.crossing_with_junction();
    endpoint_touch = endpoint_touch || segment.endpoint_touch();
    overlap = overlap || segment.overlap();
}

[[nodiscard]] bool SchematicWirePairTopology::same_net() const noexcept {
    return net_relationship == SchematicWireNetRelationship::SameNet;
}

[[nodiscard]] bool SchematicWirePairTopology::different_net() const noexcept {
    return net_relationship == SchematicWireNetRelationship::DifferentNet;
}

[[nodiscard]] bool SchematicWirePairTopology::has_crossing_without_junction() const noexcept {
    return crossing_without_junction;
}

[[nodiscard]] bool SchematicWirePairTopology::has_crossing_with_junction() const noexcept {
    return crossing_with_junction;
}

[[nodiscard]] bool SchematicWirePairTopology::has_visual_contact() const noexcept {
    return has_endpoint_touch() || has_overlap() || has_crossing_with_junction();
}

[[nodiscard]] SchematicWireNetRelationship schematic_wire_net_relationship(bool same_net) noexcept {
    return same_net ? SchematicWireNetRelationship::SameNet
                    : SchematicWireNetRelationship::DifferentNet;
}

[[nodiscard]] SchematicWireSegmentTopology
classify_wire_segment_topology(SchematicSegment first, SchematicSegment second,
                               SchematicWireNetRelationship net_relationship,
                               SchematicJunction junction) noexcept {
    return SchematicWireSegmentTopology{
        classify_segment_relationship(first, second),
        junction,
        net_relationship,
    };
}

} // namespace volt
