add_library(
    volt_circuit
    bom/bom.cpp
    circuit.cpp
    connectivity/connectivity_model.cpp
    connectivity/definitions.cpp
    connectivity/instances.cpp
    connectivity/nets.cpp
    connectivity/queries.cpp
    constraints/net_class_resolution.cpp
    constraints/net_classes.cpp
    electrical/electrical_model.cpp
    hierarchy/hierarchy.cpp
    hierarchy/hierarchy_model.cpp
    intent/design_intent.cpp
    parts/part_definition.cpp
    parts/parts.cpp
    validation/validation.cpp
)
add_library(Volt::Circuit ALIAS volt_circuit)

set_target_properties(volt_circuit PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_circuit PUBLIC cxx_std_23)
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
