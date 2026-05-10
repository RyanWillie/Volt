include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

add_library(volt_io INTERFACE)
add_library(Volt::IO ALIAS volt_io)

target_compile_features(volt_io INTERFACE cxx_std_20)
target_link_libraries(volt_io INTERFACE Volt::Circuit Volt::Schematic nlohmann_json::nlohmann_json)
target_include_directories(
    volt_io
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
