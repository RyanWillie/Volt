cmake_minimum_required(VERSION 3.25)

project(
    Volt
    VERSION 0.1.0
    DESCRIPTION "A modern electronics design kernel"
    LANGUAGES CXX
)

include(cmake/VoltCompilerOptions.cmake)

option(VOLT_BUILD_TESTS "Build Volt tests" ON)
option(VOLT_BUILD_DOCS "Add the Doxygen documentation target" ON)

configure_file(
    cmake/volt_version_config.hpp.in
    ${PROJECT_BINARY_DIR}/generated/volt/core/version_config.hpp
    @ONLY
)

add_library(volt_core src/core/version.cpp)
add_library(Volt::Core ALIAS volt_core)

target_compile_features(volt_core PUBLIC cxx_std_20)

target_include_directories(
    volt_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_BINARY_DIR}/generated
)

volt_apply_project_warnings(volt_core PRIVATE)
volt_apply_sanitizers(volt_core)

if(VOLT_BUILD_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(VOLT_DOXYGEN_INPUT_DIRS
            "${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/include ${PROJECT_SOURCE_DIR}/docs"
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
    enable_testing()
    add_subdirectory(tests)
endif()
