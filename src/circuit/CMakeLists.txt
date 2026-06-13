add_library(
    volt_circuit
    circuit.cpp
    connectivity_model.cpp
    definitions.cpp
    design_intent.cpp
    electrical_model.cpp
    hierarchy.cpp
    hierarchy_model.cpp
    instances.cpp
    nets.cpp
    part_definition.cpp
    parts.cpp
    queries.cpp
    net_class_resolution.cpp
    net_classes.cpp
    validation.cpp
)
add_library(Volt::Circuit ALIAS volt_circuit)

set_target_properties(volt_circuit PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_circuit PUBLIC cxx_std_20)
target_link_libraries(volt_circuit PUBLIC Volt::Core)
target_include_directories(
    volt_circuit
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
volt_apply_project_warnings(volt_circuit PRIVATE)
volt_apply_clang_tidy(volt_circuit)
volt_apply_sanitizers(volt_circuit)
volt_apply_coverage(volt_circuit)
