include(FetchContent)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)

set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Catch2)

add_executable(
    volt_core_tests
    authoring/connection_helpers_test.cpp
    authoring/component_library_test.cpp
    authoring/reference_designators_test.cpp
    circuit/circuit_test.cpp
    circuit/circuit_updates_test.cpp
    circuit/circuit_reads_test.cpp
    circuit/authoring_test.cpp
    circuit/construction_specs_test.cpp
    circuit/bom/bom_test.cpp
    circuit/connectivity/connectivity_model_test.cpp
    circuit/connectivity/definitions_test.cpp
    circuit/connectivity/instances_test.cpp
    circuit/connectivity/nets_test.cpp
    circuit/connectivity/queries_test.cpp
    circuit/constraints/net_classes_test.cpp
    circuit/electrical/electrical_model_test.cpp
    circuit/hierarchy/hierarchy_model_test.cpp
    circuit/hierarchy/hierarchy_test.cpp
    circuit/intent/design_intent_test.cpp
    circuit/led_circuit_test.cpp
    circuit/parts/part_definition_test.cpp
    circuit/parts/parts_test.cpp
    circuit/validation/validation_power_intent_test.cpp
    circuit/validation/validation_signal_domain_test.cpp
    circuit/validation/validation_test.cpp
    core/diagnostics_test.cpp
    core/content_hash_test.cpp
    core/electrical_attributes_test.cpp
    core/entity_table_test.cpp
    core/errors_test.cpp
    core/ids_test.cpp
    core/properties_test.cpp
    core/quantities_test.cpp
    core/rule_set_test.cpp
    core/umbrella_header_test.cpp
    core/version_test.cpp
    io/capabilities/board_capability_profile_io_test.cpp
    io/logical/logical_circuit_reader_test.cpp
    io/logical/logical_circuit_round_trip_test.cpp
    io/logical/logical_circuit_writer_test.cpp
    io/parts/part_definition_io_test.cpp
    io/pcb/pcb_fabrication_writer_test.cpp
    io/pcb/pcb_projection_io_test.cpp
    io/pcb/pcb_svg_writer_test.cpp
    io/schematic/schematic_reader_test.cpp
    io/schematic/schematic_round_trip_test.cpp
    io/schematic/schematic_schema_test.cpp
    io/schematic/schematic_svg_writer_test.cpp
    io/schematic/schematic_writer_test.cpp
    pcb/geometry/board_geometry_projection_test.cpp
    pcb/assembly/cpl_test.cpp
    pcb/routing/board_escape_router_test.cpp
    pcb/routing/board_router_test.cpp
    pcb/routing/board_spatial_index_test.cpp
    pcb/board_test.cpp
    pcb/canonical_public_include_paths_test.cpp
    pcb/models/board_models_test.cpp
    pcb/validation/erc_drc_real_board_coverage_matrix_test.cpp
    pcb/validation/erc_drc_real_board_edge_cases_test.cpp
    pcb/validation/erc_drc_real_board_regression_test.cpp
    pcb/validation/board_validation_test.cpp
    pcb/footprints/footprints_test.cpp
    adapters/kicad_boundary_test.cpp
    adapters/kicad_loss_report_test.cpp
    adapters/kicad_pcb_writer_test.cpp
    adapters/kicad_schematic_writer_test.cpp
    schematic/schematic_geometry_test.cpp
    schematic/schematic_readability_labeling_test.cpp
    schematic/schematic_readability_routing_test.cpp
    schematic/schematic_readability_test.cpp
    schematic/schematic_readiness_test.cpp
    schematic/schematic_models_test.cpp
    schematic/schematic_test.cpp
)
target_link_libraries(
    volt_core_tests
    PRIVATE
        Volt::Authoring
        Volt::IO
        Volt::NlohmannJson
        Volt::PCB
        Volt::KiCadAdapter
        Catch2::Catch2WithMain
)
target_compile_features(volt_core_tests PRIVATE cxx_std_23)
target_include_directories(
    volt_core_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/examples
        ${PROJECT_SOURCE_DIR}/tests
)
target_compile_definitions(
    volt_core_tests PRIVATE VOLT_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)
volt_apply_project_warnings(volt_core_tests PRIVATE)
volt_apply_sanitizers(volt_core_tests)
volt_apply_coverage(volt_core_tests)

include(Catch)
catch_discover_tests(volt_core_tests PROPERTIES LABELS cpp)

add_test(
    NAME kicad_adapter_boundary
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/check-kicad-boundary.py
)
set_tests_properties(kicad_adapter_boundary PROPERTIES LABELS cpp)

add_test(
    NAME circuit_architecture_boundary
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/check-circuit-architecture-boundary.py
)
set_tests_properties(circuit_architecture_boundary PROPERTIES LABELS cpp)

add_test(
    NAME circuit_architecture_boundary_self_test
    COMMAND
        ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/check-circuit-architecture-boundary.py
        --self-test
)
set_tests_properties(circuit_architecture_boundary_self_test PROPERTIES LABELS cpp)

add_test(
    NAME refactor_inventory_baselines
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/check-refactor-inventory-baselines.py
)
set_tests_properties(refactor_inventory_baselines PROPERTIES LABELS cpp)

add_test(
    NAME refactor_inventory_baselines_self_test
    COMMAND
        ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/check-refactor-inventory-baselines.py
        --self-test
)
set_tests_properties(refactor_inventory_baselines_self_test PROPERTIES LABELS cpp)

add_test(
    NAME docs_site
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/check-docs-site.py
)
set_tests_properties(docs_site PROPERTIES LABELS docs)

add_test(
    NAME python_api_docs
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/generate-python-api-docs.py --check
)
set_tests_properties(python_api_docs PROPERTIES LABELS docs)
