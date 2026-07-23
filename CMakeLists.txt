cmake_minimum_required(VERSION 3.25)

project(
    Volt
    VERSION 0.1.0
    DESCRIPTION "A modern electronics design kernel"
    LANGUAGES CXX
)

include(cmake/VoltCompilerOptions.cmake)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Do not update downloaded dependencies")

option(VOLT_BUILD_TESTS "Build Volt tests" ON)
option(VOLT_BUILD_DOCS "Add the Doxygen documentation target" ON)
option(VOLT_BUILD_EXAMPLES "Build Volt examples" ON)
option(VOLT_BUILD_PYTHON "Build Volt Python bindings" OFF)
option(VOLT_BUILD_BENCHMARKS "Build Volt benchmark executables" OFF)

if(VOLT_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(src/core)
add_subdirectory(src/circuit)
add_subdirectory(src/library)
add_subdirectory(src/authoring)
add_subdirectory(src/pcb)
add_subdirectory(src/schematic)
add_subdirectory(src/io)
add_subdirectory(src/adapters/kicad)

if(VOLT_BUILD_PYTHON)
    add_subdirectory(src/python)
endif()

add_library(volt INTERFACE)
add_library(Volt::Volt ALIAS volt)
target_link_libraries(
    volt
    INTERFACE
        Volt::Core
        Volt::Circuit
        Volt::Library
        Volt::Authoring
        Volt::PCB
        Volt::Schematic
        Volt::IO
)

if(VOLT_BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(VOLT_DOXYGEN_INPUT_DIRS
            "${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/CONTRIBUTING.md ${PROJECT_SOURCE_DIR}/ROADMAP.md ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/docs"
        )
        set(VOLT_DOXYGEN_OUTPUT_DIR "${PROJECT_BINARY_DIR}/docs/doxygen")
        configure_file(docs/Doxyfile.in ${PROJECT_BINARY_DIR}/Doxyfile @ONLY)
        add_custom_target(
            docs
            COMMAND ${CMAKE_COMMAND} -E make_directory ${VOLT_DOXYGEN_OUTPUT_DIR}
            COMMAND ${DOXYGEN_EXECUTABLE} ${PROJECT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
            COMMENT "Generating Volt API documentation with Doxygen"
            VERBATIM
        )
    else()
        message(STATUS "Doxygen not found; documentation target will not be available")
    endif()
endif()

if(VOLT_BUILD_TESTS)
    add_subdirectory(tests)
endif()

if(VOLT_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(VOLT_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

if(VOLT_ENABLE_COVERAGE)
    find_program(LCOV_EXECUTABLE lcov REQUIRED)

    set(VOLT_COVERAGE_DIR "${PROJECT_BINARY_DIR}/coverage")
    set(VOLT_COVERAGE_RAW_INFO "${VOLT_COVERAGE_DIR}/volt-coverage.raw.info")
    set(VOLT_COVERAGE_INFO "${VOLT_COVERAGE_DIR}/volt-coverage.info")
    set(VOLT_COVERAGE_DEPENDS)

    foreach(
        target
        volt_core_tests
        volt_pcb_resolution_link_contract
        volt_io_resolution_link_contract
        volt_pcb_compiled_board_link_contract
        volt_io_compiled_board_link_contract
        volt_led_circuit_example
        volt_kernel_benchmarks
    )
        if(TARGET ${target})
            list(APPEND VOLT_COVERAGE_DEPENDS ${target})
        endif()
    endforeach()

    add_custom_target(
        coverage-report
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VOLT_COVERAGE_DIR}"
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${PROJECT_BINARY_DIR}" --output-on-failure -j 8
        COMMAND
            ${LCOV_EXECUTABLE} --capture --directory "${PROJECT_BINARY_DIR}" --output-file
            "${VOLT_COVERAGE_RAW_INFO}" --ignore-errors
            mismatch,gcov,source,inconsistent,inconsistent,unsupported,format
        COMMAND
            ${LCOV_EXECUTABLE} --extract "${VOLT_COVERAGE_RAW_INFO}"
            "${PROJECT_SOURCE_DIR}/include/volt/*" "${PROJECT_SOURCE_DIR}/src/*" --output-file
            "${VOLT_COVERAGE_INFO}" --ignore-errors unused,inconsistent,inconsistent,unsupported,format
        COMMAND
            ${LCOV_EXECUTABLE} --list "${VOLT_COVERAGE_INFO}" --ignore-errors
            inconsistent,inconsistent,unsupported
        DEPENDS ${VOLT_COVERAGE_DEPENDS}
        COMMENT "Running Volt tests and generating filtered LCOV coverage"
        VERBATIM
    )
endif()
