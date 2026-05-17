include(FetchContent)

find_package(Python3 COMPONENTS Interpreter Development.Module REQUIRED)

FetchContent_Declare(
    pybind11
    URL https://github.com/pybind/pybind11/archive/refs/tags/v3.0.3.tar.gz
    URL_HASH SHA256=787459e1e186ee82001759508fefa408373eae8a076ffe0078b126c6f8f0ec5e
)
FetchContent_MakeAvailable(pybind11)

file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/python/volt)
file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/python/volt/libraries)
configure_file(
    ${PROJECT_SOURCE_DIR}/python/volt/__init__.py
    ${PROJECT_BINARY_DIR}/python/volt/__init__.py
    COPYONLY
)
configure_file(
    ${PROJECT_SOURCE_DIR}/python/volt/libraries/__init__.py
    ${PROJECT_BINARY_DIR}/python/volt/libraries/__init__.py
    COPYONLY
)
configure_file(
    ${PROJECT_SOURCE_DIR}/python/volt/libraries/stm32_usb_buck.py
    ${PROJECT_BINARY_DIR}/python/volt/libraries/stm32_usb_buck.py
    COPYONLY
)

pybind11_add_module(_volt MODULE bindings.cpp)
target_link_libraries(_volt PRIVATE Volt::Authoring Volt::IO)
if(MINGW)
    target_compile_options(_volt PRIVATE -Wa,-mbig-obj)
endif()
volt_apply_coverage(_volt)
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
    file(
        GLOB_RECURSE VOLT_PYTHON_TEST_FILES CONFIGURE_DEPENDS
        ${PROJECT_SOURCE_DIR}/python/tests/test_*.py
    )
    list(REMOVE_ITEM VOLT_PYTHON_TEST_FILES ${PROJECT_SOURCE_DIR}/python/tests/test_runner.py)

    execute_process(
        COMMAND
            ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/python/tests/test_runner.py
            discover-cmake ${VOLT_PYTHON_TEST_FILES}
        OUTPUT_VARIABLE VOLT_PYTHON_TEST_ENTRIES
        COMMAND_ERROR_IS_FATAL ANY
    )
    string(REPLACE "\n" ";" VOLT_PYTHON_TEST_ENTRIES "${VOLT_PYTHON_TEST_ENTRIES}")

    foreach(entry IN LISTS VOLT_PYTHON_TEST_ENTRIES)
        if(entry STREQUAL "")
            continue()
        endif()

        string(REPLACE "|" ";" fields "${entry}")
        list(GET fields 0 test_name)
        list(GET fields 1 test_path)
        list(GET fields 2 test_function)
        list(GET fields 3 test_labels)
        string(REPLACE "," ";" test_labels "${test_labels}")

        add_test(
            NAME ${test_name}
            COMMAND
                ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/python/tests/test_runner.py
                run ${test_path} ${test_function}
        )
        set_tests_properties(
            ${test_name}
            PROPERTIES
                ENVIRONMENT "PYTHONPATH=${PROJECT_BINARY_DIR}/python"
                LABELS "${test_labels}"
        )
    endforeach()
endif()
