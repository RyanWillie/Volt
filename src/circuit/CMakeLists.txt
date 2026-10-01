include(FetchContent)

FetchContent_Declare(
    eigen
    URL https://gitlab.com/libeigen/eigen/-/archive/5.0.0/eigen-5.0.0.tar.gz
    URL_HASH SHA256=315c881e19e17542a7d428c5aa37d113c89b9500d350c433797b730cd449c056
    # Eigen's project installs its headers unconditionally. Populate only, then expose the
    # exact archive through a private build-tree target so it cannot enter the Volt SDK.
    SOURCE_SUBDIR volt-header-only
)
FetchContent_MakeAvailable(eigen)

add_library(volt_eigen INTERFACE)
add_library(Volt::Eigen ALIAS volt_eigen)
target_include_directories(
    volt_eigen
    SYSTEM INTERFACE $<BUILD_INTERFACE:${eigen_SOURCE_DIR}>
)

add_library(
    volt_circuit
    bom/bom.cpp
    circuit.cpp
    connectivity/component_contract.cpp
    connectivity/connectivity_model.cpp
    connectivity/definitions.cpp
    connectivity/instances.cpp
    connectivity/nets.cpp
    connectivity/queries.cpp
    constraints/net_class_resolution.cpp
    constraints/net_classes.cpp
    electrical/electrical_model.cpp
    electrical/electrical_records.cpp
    electrical/electrical_queries.cpp
    electrical/dc_request.cpp
    electrical/ac_request.cpp
    electrical/dc_solve.cpp
    electrical/ac_solve.cpp
    electrical/transient_request.cpp
    electrical/transient_solve.cpp
    electrical/ngspice_dc.cpp
    electrical/compiled_electrical_model.cpp
    electrical/electrical_input.cpp
    electrical/electrical_request.cpp
    electrical/passive_model.cpp
    hierarchy/hierarchy.cpp
    hierarchy/hierarchy_model.cpp
    parts/part_definition.cpp
    parts/parts.cpp
    parts/selected_part.cpp
    validation/validation.cpp
)
add_library(Volt::Circuit ALIAS volt_circuit)

set_target_properties(volt_circuit PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(volt_circuit PUBLIC cxx_std_23)
target_link_libraries(
    volt_circuit
    PUBLIC Volt::Core
    PRIVATE $<BUILD_INTERFACE:Volt::Eigen>
)
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
volt_install_library(volt_circuit Circuit)
