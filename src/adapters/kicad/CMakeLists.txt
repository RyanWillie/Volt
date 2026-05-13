add_library(volt_kicad_adapter INTERFACE)
add_library(Volt::KiCadAdapter ALIAS volt_kicad_adapter)

target_compile_features(volt_kicad_adapter INTERFACE cxx_std_20)
target_link_libraries(volt_kicad_adapter INTERFACE Volt::Circuit Volt::Schematic)
target_include_directories(
    volt_kicad_adapter
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

