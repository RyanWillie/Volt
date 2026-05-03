cmake_minimum_required(VERSION 3.25)

project(
    Volt
    VERSION 0.1.0
    DESCRIPTION "A modern electronics design kernel"
    LANGUAGES CXX
)

include(cmake/VoltCompilerOptions.cmake)

option(VOLT_BUILD_TESTS "Build Volt tests" ON)

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

if(VOLT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
