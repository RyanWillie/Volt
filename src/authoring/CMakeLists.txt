add_library(volt_authoring INTERFACE)
add_library(Volt::Authoring ALIAS volt_authoring)

target_compile_features(volt_authoring INTERFACE cxx_std_20)
target_link_libraries(volt_authoring INTERFACE Volt::Circuit)
target_include_directories(
    volt_authoring
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
