#include <catch2/catch_test_macros.hpp>

#include <volt/pcb/copper/board_copper.hpp>
#include <volt/pcb/features/board_features.hpp>
#include <volt/pcb/footprints/footprints.hpp>
#include <volt/pcb/geometry/board_geometry.hpp>
#include <volt/pcb/geometry/board_outline.hpp>
#include <volt/pcb/layers/board_layers.hpp>
#include <volt/pcb/placement/board_placement_updates.hpp>
#include <volt/pcb/projection/board_geometry_projection.hpp>
#include <volt/pcb/queries/board_queries.hpp>
#include <volt/pcb/routing/board_router.hpp>
#include <volt/pcb/routing/board_spatial_index.hpp>

TEST_CASE("Canonical PCB public headers live in ownership folders") { CHECK(true); }
