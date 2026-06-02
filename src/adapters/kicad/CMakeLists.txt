add_library(
    volt_kicad_adapter
    format.cpp
    loss_report.cpp
    pcb_writer.cpp
    schematic_writer.cpp
)
add_library(Volt::KiCadAdapter ALIAS volt_kicad_adapter)

set_target_properties(volt_kicad_adapter PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_kicad_adapter PUBLIC cxx_std_20)
target_link_libraries(volt_kicad_adapter PUBLIC Volt::Circuit Volt::PCB Volt::Schematic)
target_include_directories(
    volt_kicad_adapter
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
volt_apply_project_warnings(volt_kicad_adapter PRIVATE)
volt_apply_clang_tidy(volt_kicad_adapter)
volt_apply_sanitizers(volt_kicad_adapter)
volt_apply_coverage(volt_kicad_adapter)
