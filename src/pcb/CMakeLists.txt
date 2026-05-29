add_library(volt_pcb INTERFACE)
add_library(Volt::PCB ALIAS volt_pcb)

target_compile_features(volt_pcb INTERFACE cxx_std_20)
target_link_libraries(volt_pcb INTERFACE Volt::Circuit)
target_include_directories(
    volt_pcb
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
