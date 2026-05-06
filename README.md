<p align="center">
  <img src="docs/assets/volt-logo.png" alt="Volt logo" width="220">
</p>

# Volt

Volt is a modern C++ electronics design kernel for representing circuit intent as
structured, validated data.

Most electronics tools start with drawings. Volt starts with the logical model:
component definitions, component instances, pins, nets, selected parts, and deterministic
serialization are the source of truth. Schematic and PCB layers can then become projections
over that model instead of owning connectivity themselves.

The guiding rule is:

```text
Invalid kernel state should be impossible.
Bad circuit design should be diagnosable.
```

That means structural mistakes are rejected at mutation boundaries, while design-quality
issues such as unconnected pins, single-pin nets, and future electrical-rule findings are
reported through diagnostics.

## Why Volt Exists

Volt is being built for tools that need circuit data they can trust: programmatic
authoring, validation, import/export pipelines, future Python bindings, and eventually
schematic and PCB views. The project is intentionally starting below UI, routing, and
manufacturing export so the kernel can establish stable invariants first.

## What Works Today

- C++20 core library with typed IDs, deterministic entity storage, diagnostics, and
  metadata properties
- Logical circuit model for component definitions, instances, concrete pins, nets, and
  selected physical parts
- Kernel-enforced connectivity invariants, including prevention of dangling references
  and pins connected to multiple nets
- Logical validation diagnostics for design-quality issues
- Deterministic logical circuit JSON writer and structural reader
- Low-level authoring helpers for library component specs, reference allocation, and net
  connection
- Golden fixture round-trip tests
- CMake/Ninja presets, CTest-based unit tests, strict project warnings, and Doxygen docs
- Layered CMake targets: `Volt::Core`, `Volt::Circuit`, `Volt::Authoring`, `Volt::IO`,
  and `Volt::Volt`

## Build

Volt uses CMake presets. From the repository root:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Examples are built by default. The first end-to-end logical example is an LED circuit:

```sh
./build/dev/examples/volt_led_circuit_example
```

## Project Layout

```text
include/volt/core      typed IDs, entity storage, diagnostics, properties, version API
include/volt/circuit   logical circuit model, parts, nets, instances, validation
include/volt/authoring component-library specs, reference allocation, connection helpers
include/volt/io        deterministic logical circuit read/write support
examples               small executable examples
tests                  unit tests and golden logical-circuit fixtures
docs                   architecture, format, authoring, and development notes
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

For contributor workflow and milestones, see [CONTRIBUTING.md](CONTRIBUTING.md) and
[ROADMAP.md](ROADMAP.md).

## Status

Volt is pre-release kernel work. The logical model, validation foundation, selected-part
data, deterministic JSON persistence, and first authoring helpers are in place; Python
bindings, schematic projection, and PCB modeling are planned layers.
