add_library(
    volt_pcb
    board.cpp
    board_copper.cpp
    board_features.cpp
    board_geometry.cpp
    board_layers.cpp
    board_outline.cpp
    footprints.cpp
)
add_library(Volt::PCB ALIAS volt_pcb)

set_target_properties(volt_pcb PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_pcb PUBLIC cxx_std_20)
target_link_libraries(volt_pcb PUBLIC Volt::Circuit)
target_include_directories(
    volt_pcb
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
volt_apply_project_warnings(volt_pcb PRIVATE)
volt_apply_sanitizers(volt_pcb)
volt_apply_coverage(volt_pcb)
