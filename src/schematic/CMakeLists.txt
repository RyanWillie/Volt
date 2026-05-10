add_library(volt_schematic INTERFACE)
add_library(Volt::Schematic ALIAS volt_schematic)

target_compile_features(volt_schematic INTERFACE cxx_std_20)
target_link_libraries(volt_schematic INTERFACE Volt::Circuit)
target_include_directories(
    volt_schematic
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
