include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

add_library(
    volt_io
    detail_schematic_svg_common.cpp
    detail_schematic_svg_elements.cpp
    detail_schematic_svg_layers.cpp
    detail_schematic_svg_page_chrome.cpp
    logical_circuit_reader.cpp
    logical_circuit_writer.cpp
    pcb_reader.cpp
    pcb_schema.cpp
    pcb_svg_writer.cpp
    pcb_writer.cpp
    schematic_reader.cpp
    schematic_schema.cpp
    schematic_svg_writer.cpp
    schematic_writer.cpp
)
add_library(Volt::IO ALIAS volt_io)

set_target_properties(volt_io PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_io PUBLIC cxx_std_20)
target_link_libraries(
    volt_io
    PUBLIC
        Volt::Circuit
        Volt::Schematic
        Volt::PCB
    PRIVATE
        nlohmann_json::nlohmann_json
)
target_include_directories(
    volt_io
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
volt_apply_project_warnings(volt_io PRIVATE)
volt_apply_sanitizers(volt_io)
volt_apply_coverage(volt_io)
