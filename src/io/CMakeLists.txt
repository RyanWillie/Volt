include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(volt_io INTERFACE)
add_library(Volt::IO ALIAS volt_io)

target_compile_features(volt_io INTERFACE cxx_std_20)
target_link_libraries(volt_io INTERFACE Volt::Circuit nlohmann_json::nlohmann_json)
target_include_directories(
    volt_io
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
