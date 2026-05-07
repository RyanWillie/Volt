include(FetchContent)

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

FetchContent_Declare(
    pybind11
    URL https://github.com/pybind/pybind11/archive/refs/tags/v3.0.3.tar.gz
    URL_HASH SHA256=787459e1e186ee82001759508fefa408373eae8a076ffe0078b126c6f8f0ec5e
)
FetchContent_MakeAvailable(pybind11)

file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/python/volt)
configure_file(
    ${PROJECT_SOURCE_DIR}/python/volt/__init__.py
    ${PROJECT_BINARY_DIR}/python/volt/__init__.py
    COPYONLY
)

pybind11_add_module(_volt MODULE bindings.cpp)
target_link_libraries(_volt PRIVATE Volt::Authoring Volt::IO)
set_target_properties(_volt PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/python/volt)

if(WIN32)
    get_filename_component(VOLT_PYTHON_RUNTIME_DIR "${Python3_EXECUTABLE}" DIRECTORY)
    get_filename_component(VOLT_CXX_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(VOLT_PYTHON_RUNTIME_SEARCH_DIRS "${VOLT_PYTHON_RUNTIME_DIR};${VOLT_CXX_RUNTIME_DIR}")

    add_custom_command(
        TARGET _volt
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} "-DVOLT_RUNTIME_TARGET_FILE=$<TARGET_FILE:_volt>"
            "-DVOLT_RUNTIME_TARGET_DIR=$<TARGET_FILE_DIR:_volt>"
            "-DVOLT_RUNTIME_SEARCH_DIRS=${VOLT_PYTHON_RUNTIME_SEARCH_DIRS}" -P
            "${PROJECT_SOURCE_DIR}/cmake/VoltCopyRuntimeDependencies.cmake"
        VERBATIM
    )
endif()

if(VOLT_BUILD_TESTS)
    add_test(
        NAME volt_python_led_mvp
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/python/tests/test_led_mvp.py
    )
    set_tests_properties(
        volt_python_led_mvp
        PROPERTIES ENVIRONMENT "PYTHONPATH=${PROJECT_BINARY_DIR}/python"
    )
endif()
