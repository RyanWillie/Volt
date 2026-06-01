add_library(
    volt_authoring
    component_library.cpp
    connection_helpers.cpp
    reference_designators.cpp
)
add_library(Volt::Authoring ALIAS volt_authoring)

set_target_properties(volt_authoring PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_authoring PUBLIC cxx_std_20)
target_link_libraries(volt_authoring PUBLIC Volt::Circuit)
target_include_directories(
    volt_authoring
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
volt_apply_project_warnings(volt_authoring PRIVATE)
volt_apply_sanitizers(volt_authoring)
volt_apply_coverage(volt_authoring)
