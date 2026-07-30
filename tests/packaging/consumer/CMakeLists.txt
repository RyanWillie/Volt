cmake_minimum_required(VERSION 3.25)

project(VoltPackagingConsumer LANGUAGES CXX)

# Consume Volt exactly as a downstream project does: through the installed package
# configuration, never through the Volt source tree.
find_package(Volt 0.1.0 EXACT CONFIG REQUIRED)

# The umbrella target plus the aggregate public header.
add_executable(volt_consumer_umbrella umbrella.cpp)
target_link_libraries(volt_consumer_umbrella PRIVATE Volt::Volt)
target_compile_features(volt_consumer_umbrella PRIVATE cxx_std_23)

# Every layer target linked by name. Volt::KiCadAdapter is deliberately included because it
# is not part of the Volt::Volt umbrella, so nothing else would catch it dropping out of the
# export set.
add_executable(volt_consumer_layers layers.cpp)
target_link_libraries(
    volt_consumer_layers
    PRIVATE
        Volt::Core
        Volt::Circuit
        Volt::Library
        Volt::Authoring
        Volt::PCB
        Volt::Schematic
        Volt::IO
        Volt::KiCadAdapter
)
target_compile_features(volt_consumer_layers PRIVATE cxx_std_23)

enable_testing()
add_test(NAME consumer_umbrella COMMAND volt_consumer_umbrella)
add_test(NAME consumer_layers COMMAND volt_consumer_layers)
