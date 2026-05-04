include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)

FetchContent_MakeAvailable(Catch2)

add_executable(
    volt_core_tests
    circuit/circuit_test.cpp
    circuit/authoring_test.cpp
    circuit/definitions_test.cpp
    circuit/instances_test.cpp
    circuit/led_circuit_test.cpp
    circuit/nets_test.cpp
    circuit/parts_test.cpp
    circuit/validation_test.cpp
    core/diagnostics_test.cpp
    core/entity_table_test.cpp
    core/ids_test.cpp
    core/properties_test.cpp
    core/umbrella_header_test.cpp
    core/version_test.cpp
    io/logical_circuit_reader_test.cpp
    io/logical_circuit_round_trip_test.cpp
    io/logical_circuit_writer_test.cpp
)
target_link_libraries(volt_core_tests PRIVATE Volt::IO Catch2::Catch2WithMain)
target_compile_features(volt_core_tests PRIVATE cxx_std_20)
target_include_directories(volt_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/examples)
target_compile_definitions(
    volt_core_tests PRIVATE VOLT_TEST_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)
volt_apply_project_warnings(volt_core_tests PRIVATE)
volt_apply_sanitizers(volt_core_tests)

include(Catch)
catch_discover_tests(volt_core_tests)
