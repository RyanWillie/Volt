add_library(volt_library part_library.cpp)
add_library(Volt::Library ALIAS volt_library)

set_target_properties(volt_library PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_library PUBLIC cxx_std_23)
target_link_libraries(volt_library PUBLIC Volt::Circuit)
target_include_directories(
    volt_library
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
volt_apply_project_warnings(volt_library PRIVATE)
volt_apply_clang_tidy(volt_library)
volt_apply_sanitizers(volt_library)
volt_apply_coverage(volt_library)
