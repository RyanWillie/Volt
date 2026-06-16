#include <catch2/catch_test_macros.hpp>

#include <volt/pcb/board_copper.hpp>
#include <volt/pcb/board_copper_model.hpp>
#include <volt/pcb/board_features.hpp>
#include <volt/pcb/board_footprint_model.hpp>
#include <volt/pcb/board_geometry.hpp>
#include <volt/pcb/board_geometry_projection.hpp>
#include <volt/pcb/board_layers.hpp>
#include <volt/pcb/board_outline.hpp>
#include <volt/pcb/board_placement_model.hpp>
#include <volt/pcb/board_router.hpp>
#include <volt/pcb/board_spatial_index.hpp>
#include <volt/pcb/board_structure_model.hpp>
#include <volt/pcb/footprints.hpp>

TEST_CASE("Flat PCB public headers forward to ownership folders") { CHECK(true); }
