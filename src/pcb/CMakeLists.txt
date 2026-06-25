add_library(
    volt_pcb
    board.cpp
    copper/board_capability.cpp
    copper/board_copper.cpp
    copper/board_copper_geometry.cpp
    copper/board_copper_model.cpp
    copper/board_copper_validation.cpp
    copper/board_room_rules.cpp
    features/board_features.cpp
    footprints/board_footprint_model.cpp
    footprints/footprints.cpp
    geometry/board_geometry.cpp
    geometry/board_outline.cpp
    layers/board_layers.cpp
    placement/board_placement_model.cpp
    assembly/cpl.cpp
    projection/board_geometry_projection.cpp
    projection/footprint_visual_projection.cpp
    routing/board_router.cpp
    routing/board_spatial_index.cpp
    structure/board_structure_model.cpp
    validation/board_capability_validation.cpp
    validation/board_footprint_drc.cpp
    validation/board_visual_validation.cpp
)
add_library(Volt::PCB ALIAS volt_pcb)

set_target_properties(volt_pcb PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_pcb PUBLIC cxx_std_23)
target_link_libraries(volt_pcb PUBLIC Volt::Circuit)
target_include_directories(
    volt_pcb
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
volt_apply_project_warnings(volt_pcb PRIVATE)
volt_apply_clang_tidy(volt_pcb)
volt_apply_sanitizers(volt_pcb)
volt_apply_coverage(volt_pcb)
