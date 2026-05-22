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
set(
    VOLT_PYTHON_PACKAGE_FILES
    __init__.py
    _presentation.py
    _schematic_labels.py
    _schematic_metadata.py
    _schematic_routing.py
    _utils.py
    design.py
    diagnostics.py
    library.py
    logical.py
    schematic.py
)
foreach(package_file IN LISTS VOLT_PYTHON_PACKAGE_FILES)
    configure_file(
        ${PROJECT_SOURCE_DIR}/python/volt/${package_file}
        ${PROJECT_BINARY_DIR}/python/volt/${package_file}
        COPYONLY
    )
endforeach()

set(
    VOLT_PYTHON_LIBRARY_FILES
    __init__.py
    stm32_usb_buck.py
)
foreach(library_file IN LISTS VOLT_PYTHON_LIBRARY_FILES)
    configure_file(
        ${PROJECT_SOURCE_DIR}/python/volt/libraries/${library_file}
        ${PROJECT_BINARY_DIR}/python/volt/libraries/${library_file}
        COPYONLY
    )
endforeach()

pybind11_add_module(
    _volt MODULE
    bindings.cpp
    circuit_bindings.cpp
)
target_link_libraries(_volt PRIVATE Volt::Authoring Volt::IO)
if(MINGW)
    target_compile_options(_volt PRIVATE -Wa,-mbig-obj)
endif()
volt_apply_coverage(_volt)
set_target_properties(_volt PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/python/volt)
install(TARGETS _volt LIBRARY DESTINATION volt RUNTIME DESTINATION volt)

if(WIN32)
    get_filename_component(VOLT_PYTHON_RUNTIME_DIR "${Python3_EXECUTABLE}" DIRECTORY)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import sys; print(sys.base_prefix)"
        OUTPUT_VARIABLE VOLT_PYTHON_BASE_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    get_filename_component(VOLT_CXX_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(
        VOLT_PYTHON_RUNTIME_SEARCH_DIRS
        "${VOLT_PYTHON_RUNTIME_DIR};${VOLT_PYTHON_BASE_PREFIX};${VOLT_CXX_RUNTIME_DIR}"
    )

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

    execute_process(
        COMMAND ${Python3_EXECUTABLE} -c "import pytest"
        RESULT_VARIABLE VOLT_PYTEST_IMPORT_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(NOT VOLT_PYTEST_IMPORT_RESULT EQUAL 0)
        message(
            FATAL_ERROR
                "pytest is required for Volt Python tests. Install dev dependencies with "
                "'python ${PROJECT_SOURCE_DIR}/scripts/install-python-dev-deps.py'."
        )
    endif()

    foreach(test_file IN LISTS VOLT_PYTHON_TEST_FILES)
        file(RELATIVE_PATH test_relative ${PROJECT_SOURCE_DIR}/python/tests ${test_file})
        string(REPLACE "/" "." test_name ${test_relative})
        string(REPLACE "\\" "." test_name ${test_name})
        string(REGEX REPLACE "\\.py$" "" test_name ${test_name})

        set(test_labels python)
        if(test_relative MATCHES "^examples/")
            list(APPEND test_labels example integration)
        endif()
        # ENVIRONMENT_MODIFICATION applies path_list_prepend in reverse order.
        # Effective PYTHONPATH: build tree, source root, test helpers.
        # This ensures the built _volt extension is imported before source files.
        set(
            test_environment
            "PYTHONPATH=path_list_prepend:${PROJECT_SOURCE_DIR}/python/tests"
            "PYTHONPATH=path_list_prepend:${PROJECT_SOURCE_DIR}"
            "PYTHONPATH=path_list_prepend:${PROJECT_BINARY_DIR}/python"
        )

        add_test(
            NAME python.${test_name}
            COMMAND ${Python3_EXECUTABLE} -m pytest -q -p no:cacheprovider ${test_file}
        )
        set_tests_properties(
            python.${test_name}
            PROPERTIES
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                ENVIRONMENT_MODIFICATION "${test_environment}"
                LABELS "${test_labels}"
        )
    endforeach()
endif()
