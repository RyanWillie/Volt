#pragma once

#include <cstddef>

#include <volt/schematic/geometry.hpp>

namespace volt {

/** Whether two schematic wire runs present the same logical net. */
enum class SchematicWireNetRelationship {
    SameNet,
    DifferentNet,
};

/**
 * Topology facts for one pair of schematic wire segments.
 *
 * Policy boundary:
 * - Different-net endpoint touches, overlaps, and crossings with an explicit junction are
 *   structurally invalid at schematic mutation boundaries because they visually join logical nets.
 * - Different-net crossings without a junction are structurally representable and diagnostic-only.
 * - Same-net crossings without a junction are structurally representable and diagnostic-only.
 * - Same-net endpoint touches, overlaps, and crossings with a junction are valid visual joins.
 *
 * These helpers only classify facts. Callers own mutation rejection and diagnostic codes/severity.
 */
struct SchematicWireSegmentTopology {
    /** Geometric relationship between the two segments. */
    SchematicSegmentRelationship relationship = SchematicSegmentRelationship::Disjoint;
    /** Whether a junction marker is present at the segment intersection. */
    SchematicJunction junction = SchematicJunction::Absent;
    /** Whether the two parent wire runs present the same net. */
    SchematicWireNetRelationship net_relationship = SchematicWireNetRelationship::SameNet;

    /** Return whether the parent wire runs present one logical net. */
    [[nodiscard]] bool same_net() const noexcept {
        return net_relationship == SchematicWireNetRelationship::SameNet;
    }

    /** Return whether the parent wire runs present different logical nets. */
    [[nodiscard]] bool different_net() const noexcept {
        return net_relationship == SchematicWireNetRelationship::DifferentNet;
    }

    /** Return whether the segments cross away from endpoints. */
    [[nodiscard]] bool crossing() const noexcept {
        return relationship == SchematicSegmentRelationship::Crossing;
    }

    /** Return whether the segments cross without an explicit junction marker. */
    [[nodiscard]] bool crossing_without_junction() const noexcept {
        return crossing() && junction == SchematicJunction::Absent;
    }

    /** Return whether the segments cross with an explicit junction marker. */
    [[nodiscard]] bool crossing_with_junction() const noexcept {
        return crossing() && junction == SchematicJunction::Present;
    }

    /** Return whether the segments touch at an endpoint. */
    [[nodiscard]] bool endpoint_touch() const noexcept {
        return relationship == SchematicSegmentRelationship::EndpointTouch;
    }

    /** Return whether the segments overlap collinearly. */
    [[nodiscard]] bool overlap() const noexcept {
        return relationship == SchematicSegmentRelationship::Overlap;
    }

    /** Return whether the drawn segments visually contact each other. */
    [[nodiscard]] bool visual_contact() const noexcept {
        return endpoint_touch() || overlap() || crossing_with_junction();
    }
};

/** Topology facts accumulated across every segment pair in two wire runs. */
struct SchematicWirePairTopology {
    /** Whether the parent wire runs present the same net. */
    SchematicWireNetRelationship net_relationship = SchematicWireNetRelationship::SameNet;
    /** Whether any segment pair crosses without a junction marker. */
    bool crossing_without_junction = false;
    /** Whether any segment pair crosses with a junction marker. */
    bool crossing_with_junction = false;
    /** Whether any segment pair touches at an endpoint. */
    bool endpoint_touch = false;
    /** Whether any segment pair overlaps collinearly. */
    bool overlap = false;

    /** Include facts from one segment pair. */
    void include(SchematicWireSegmentTopology segment) noexcept {
        crossing_without_junction =
            crossing_without_junction || segment.crossing_without_junction();
        crossing_with_junction = crossing_with_junction || segment.crossing_with_junction();
        endpoint_touch = endpoint_touch || segment.endpoint_touch();
        overlap = overlap || segment.overlap();
    }

    /** Return whether the parent wire runs present one logical net. */
    [[nodiscard]] bool same_net() const noexcept {
        return net_relationship == SchematicWireNetRelationship::SameNet;
    }

    /** Return whether the parent wire runs present different logical nets. */
    [[nodiscard]] bool different_net() const noexcept {
        return net_relationship == SchematicWireNetRelationship::DifferentNet;
    }

    /** Return whether any segment pair crosses without a junction marker. */
    [[nodiscard]] bool has_crossing_without_junction() const noexcept {
        return crossing_without_junction;
    }

    /** Return whether any segment pair crosses with a junction marker. */
    [[nodiscard]] bool has_crossing_with_junction() const noexcept {
        return crossing_with_junction;
    }

    /** Return whether any segment pair touches at an endpoint. */
    [[nodiscard]] bool has_endpoint_touch() const noexcept { return endpoint_touch; }

    /** Return whether any segment pair overlaps collinearly. */
    [[nodiscard]] bool has_overlap() const noexcept { return overlap; }

    /** Return whether any segment pair visually contacts another. */
    [[nodiscard]] bool has_visual_contact() const noexcept {
        return has_endpoint_touch() || has_overlap() || has_crossing_with_junction();
    }
};

/** Convert a same-net comparison into the shared topology relationship enum. */
[[nodiscard]] inline SchematicWireNetRelationship
schematic_wire_net_relationship(bool same_net) noexcept {
    return same_net ? SchematicWireNetRelationship::SameNet
                    : SchematicWireNetRelationship::DifferentNet;
}

/** Classify one schematic wire segment pair into topology facts. */
[[nodiscard]] inline SchematicWireSegmentTopology
classify_wire_segment_topology(SchematicSegment first, SchematicSegment second,
                               SchematicWireNetRelationship net_relationship,
                               SchematicJunction junction) noexcept {
    return SchematicWireSegmentTopology{
        classify_segment_relationship(first, second),
        junction,
        net_relationship,
    };
}

/** Classify every segment pair in two schematic wire point sequences. */
template <typename FirstPoints, typename SecondPoints, typename JunctionForSegments>
[[nodiscard]] inline SchematicWirePairTopology
classify_wire_pair_topology(const FirstPoints &first_points, const SecondPoints &second_points,
                            SchematicWireNetRelationship net_relationship,
                            JunctionForSegments junction_for_segments) {
    auto result = SchematicWirePairTopology{net_relationship};
    for (std::size_t first_index = 1; first_index < first_points.size(); ++first_index) {
        const auto first_segment =
            SchematicSegment{first_points[first_index - 1U], first_points[first_index]};
        for (std::size_t second_index = 1; second_index < second_points.size(); ++second_index) {
            const auto second_segment =
                SchematicSegment{second_points[second_index - 1U], second_points[second_index]};
            result.include(classify_wire_segment_topology(
                first_segment, second_segment, net_relationship,
                junction_for_segments(first_segment, second_segment)));
        }
    }
    return result;
}

} // namespace volt
