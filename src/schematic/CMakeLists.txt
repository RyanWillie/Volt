add_library(
    volt_schematic
    default_symbols.cpp
    geometry.cpp
    layout.cpp
    presentation_geometry.cpp
    readability_bounds_validation.cpp
    readability_collision_validation.cpp
    readability_density_validation.cpp
    readability_geometry.cpp
    readability_label_validation.cpp
    readability_validation.cpp
    readability_validation_common.cpp
    readability_wire_validation.cpp
    readiness_validation.cpp
    schematic.cpp
    schematic_document.cpp
    symbols.cpp
    wire_topology.cpp
)
add_library(Volt::Schematic ALIAS volt_schematic)

set_target_properties(volt_schematic PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_schematic PUBLIC cxx_std_20)
target_link_libraries(volt_schematic PUBLIC Volt::Circuit)
target_include_directories(
    volt_schematic
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
volt_apply_project_warnings(volt_schematic PRIVATE)
volt_apply_clang_tidy(volt_schematic)
volt_apply_sanitizers(volt_schematic)
volt_apply_coverage(volt_schematic)
