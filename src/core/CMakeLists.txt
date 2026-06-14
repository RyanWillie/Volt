configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/volt_version_config.hpp.in
    ${PROJECT_BINARY_DIR}/generated/volt/core/version_config.hpp
    @ONLY
)

add_library(volt_core content_hash.cpp version.cpp)
add_library(Volt::Core ALIAS volt_core)

set_target_properties(volt_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_core PUBLIC cxx_std_23)

target_include_directories(
    volt_core
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${PROJECT_BINARY_DIR}/generated
)

volt_apply_project_warnings(volt_core PRIVATE)
volt_apply_clang_tidy(volt_core)
volt_apply_sanitizers(volt_core)
volt_apply_coverage(volt_core)
