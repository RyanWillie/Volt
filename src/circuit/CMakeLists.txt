add_library(volt_circuit INTERFACE)
add_library(Volt::Circuit ALIAS volt_circuit)

target_compile_features(volt_circuit INTERFACE cxx_std_20)
target_link_libraries(volt_circuit INTERFACE Volt::Core)
target_include_directories(
    volt_circuit
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
