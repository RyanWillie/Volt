include(FetchContent)

find_package(nlohmann_json 3.11.3 CONFIG QUIET)

if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    )
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(volt_nlohmann_json INTERFACE)
add_library(Volt::NlohmannJson ALIAS volt_nlohmann_json)
target_link_libraries(volt_nlohmann_json INTERFACE nlohmann_json::nlohmann_json)

add_library(
    volt_io
    assembly/cpl_writer.cpp
    bom/bom_writer.cpp
    capabilities/board_capability_profile.cpp
    detail/entity_ref_format.cpp
    logical/logical_circuit_reader.cpp
    logical/logical_circuit_hierarchy_parser.cpp
    logical/logical_circuit_parser.cpp
    logical/logical_circuit_restoration.cpp
    logical/logical_circuit_writer.cpp
    logical/logical_net_class_format.cpp
    parts/part_definition_reader.cpp
    parts/part_definition_writer.cpp
    parts/footprint_asset.cpp
    parts/part_library_bundle.cpp
    parts/electrical_records_io.cpp
    pcb/pcb_fabrication_writer.cpp
    pcb/board_resolution.cpp
    pcb/board_resolution_detail.cpp
    pcb/compiled_board.cpp
    pcb/compiled_board_consumers.cpp
    pcb/pcb_feature_io.cpp
    pcb/pcb_feature_svg.cpp
    pcb/pcb_reader.cpp
    pcb/pcb_schema.cpp
    pcb/pcb_svg_footprints.cpp
    pcb/pcb_svg_layers.cpp
    pcb/pcb_svg_writer.cpp
    pcb/pcb_writer.cpp
    pcb/pcb_writer_footprints.cpp
    pcb/pcb_writer_rooms.cpp
    schematic/detail_schematic_svg_common.cpp
    schematic/detail_schematic_svg_elements.cpp
    schematic/detail_schematic_svg_layers.cpp
    schematic/detail_schematic_svg_page_chrome.cpp
    schematic/schematic_reader.cpp
    schematic/schematic_schema.cpp
    schematic/schematic_svg_writer.cpp
    schematic/schematic_writer.cpp
)
add_library(Volt::IO ALIAS volt_io)

set_target_properties(volt_io PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_io PUBLIC cxx_std_23)
target_link_libraries(
    volt_io
    PUBLIC
        Volt::Circuit
        Volt::Library
        Volt::Schematic
        Volt::PCB
    PRIVATE
        Volt::NlohmannJson
)
target_include_directories(
    volt_io
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
volt_apply_project_warnings(volt_io PRIVATE)
volt_apply_clang_tidy(volt_io)
volt_apply_sanitizers(volt_io)
volt_apply_coverage(volt_io)
