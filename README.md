# Volt

Volt is a modern C++ electronics design kernel. The first milestone is a logical circuit
kernel where components, pins, and nets are canonical source data, and schematics are
views over that data.

The project is intentionally starting below UI, PCB routing, and manufacturing export.
The initial goal is to make circuit intent explicit, validated, serializable, and suitable
for future Python bindings.

## Current Scope

- C++20 core library
- CMake/Ninja build presets
- CTest-based unit tests
- Strict compiler warnings for project code
- Documentation for architecture and development workflow
- Layered CMake targets: `Volt::Core`, `Volt::Circuit`, `Volt::IO`, and `Volt::Volt`

## Build

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Examples are built by default. The first end-to-end logical example is an LED circuit:

```sh
./build/dev/examples/volt_led_circuit_example
```

## Documentation

Volt uses Doxygen for public API documentation:

```sh
cmake --build --preset dev --target docs
```

The generated HTML lives under the selected build directory.

For sanitizer builds:

```sh
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

On this workstation, the Apple system compiler currently fails to compile standard C++
library headers because its SDK/compiler pairing is inconsistent. Use the Homebrew LLVM
preset instead:

```sh
cmake --preset macos-llvm
cmake --build --preset macos-llvm
ctest --preset macos-llvm
```

## Design Direction

Volt uses stable typed IDs for kernel entities. Human names such as `R1`, `GND`, and
`U1.PA0` are labels, not internal identity. The first kernel layers will be:

1. Core primitive types and diagnostics
2. Entity storage
3. Component and pin definitions
4. Component instances
5. Nets and pin connectivity
6. Electrical validation
7. Schematic views over canonical nets
8. Deterministic serialization
9. Python bindings

See [docs/architecture.md](docs/architecture.md) for the architectural outline,
[docs/authoring-api.md](docs/authoring-api.md) for the planned SKiDL-style authoring
facade boundary, [docs/logical-circuit-format.md](docs/logical-circuit-format.md) for
the canonical logical circuit file format, and
[docs/schema-versioning.md](docs/schema-versioning.md) for file compatibility policy.
