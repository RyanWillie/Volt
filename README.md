<p align="center">
  <img src="docs/assets/volt-logo.png" alt="Volt logo" width="160">
</p>

<h1 align="center">Volt</h1>

<p align="center">
  <strong>A modern C++ electronics design kernel for structured, validated circuit intent.</strong>
</p>

<p align="center">
  <a href="https://github.com/RyanWillie/Volt/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/RyanWillie/Volt/actions/workflows/ci.yml/badge.svg"></a>
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C">
  <img alt="CMake" src="https://img.shields.io/badge/build-CMake-064F8C">
  <a href="LICENSE"><img alt="License: Apache-2.0" src="https://img.shields.io/badge/license-Apache--2.0-blue"></a>
  <img alt="Status: pre-release" src="https://img.shields.io/badge/status-pre--release-f5a623">
</p>

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
authoring, validation, import/export pipelines, Python bindings, and eventually
schematic and PCB views. The project is intentionally starting below UI, routing, and
manufacturing export so the kernel can establish stable invariants first.

## What Works Today

- C++20 core library with typed IDs, deterministic entity storage, diagnostics, and
  metadata properties
- Logical circuit model for component definitions, instances, concrete pins, nets, and
  selected physical parts
- Typed electrical semantics: quantities/units, electrical attributes, pin voltage
  ranges, net voltages, and selected-part voltage-rating diagnostics
- Logical hierarchy primitives: module definitions, module instances, ports, and
  template nets, plus rule classes for reusable net design intent
- Kernel-enforced connectivity invariants, including prevention of dangling references
  and pins connected to multiple nets
- Logical validation diagnostics for design-quality issues, with named entry points for
  general, connectivity, ERC, and PCB-readiness checks
- Schematic projection layer: kernel-owned sheets, symbols, wires, and labels over
  canonical nets, schematic readability/consistency validation, and deterministic SVG
  rendering
- PCB projection layer: board outline, layers, footprint placement, copper geometry and
  routing, board 3D geometry projection, and deterministic PCB SVG output
- KiCad export adapters for schematic and PCB projections, with a structured loss report
- Deterministic JSON writers and structural readers for the logical circuit, schematic,
  and PCB projections
- Low-level authoring helpers for library component specs, reference allocation, and net
  connection
- Python authoring bindings providing ergonomic syntax over kernel-owned logical,
  schematic, and PCB state
- Golden fixture round-trip tests
- CMake/Ninja presets, CTest-based unit tests, strict project warnings, and Doxygen docs
- Layered CMake targets: `Volt::Core`, `Volt::Circuit`, `Volt::Authoring`,
  `Volt::Schematic`, `Volt::PCB`, `Volt::IO`, and `Volt::Volt`

## Build And Test

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
include/volt/circuit   logical circuit model, parts, nets, instances, hierarchy, validation
include/volt/authoring component-library specs, reference allocation, connection helpers
include/volt/schematic schematic projection model, symbols, layout, readability validation
include/volt/pcb       board outline, layers, footprints, copper, geometry projection
include/volt/io        deterministic read/write for logical, schematic, and PCB data
include/volt/adapters  KiCad schematic and PCB export adapters
python/volt            Python authoring bindings over kernel-owned state
examples               small executable examples
tests                  unit tests and golden fixtures
docs                   architecture, format, authoring, and development notes
docs/design            standalone design and planning artifacts (HTML exports)
```

## Documentation

Volt has two documentation surfaces:

- Public user docs live in `docs-site/` as a Mintlify site.
- C++ API documentation is generated with Doxygen from public headers and project docs.

Preview the public docs with the Mintlify CLI:

```sh
cd docs-site
mint dev
```

Check that the public docs stay aligned with the Python API and example workflows:

```sh
python3 scripts/generate-python-api-docs.py --check
python3 scripts/check-docs-site.py
```

Generate C++ API documentation with:

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

A Homebrew LLVM preset is also available for macOS environments that prefer a toolchain
separate from the Apple system compiler:

```sh
cmake --preset macos-llvm
cmake --build --preset macos-llvm
ctest --preset macos-llvm
```

## Design Direction

Volt uses stable typed IDs for kernel entities. Human names such as `R1`, `GND`, and
`U1.PA0` are labels, not internal identity. The logical circuit remains the source of
truth for connectivity, with deterministic serialization, validation, and a Python
authoring surface over kernel-owned state. Schematic and PCB layers are implemented as
projections over the logical circuit, not alternate owners of connectivity.

See [docs/architecture.md](docs/architecture.md) for the architectural outline,
[docs/authoring-api.md](docs/authoring-api.md) for the programmatic authoring facade
boundary, [docs/logical-circuit-format.md](docs/logical-circuit-format.md) for the
canonical logical circuit file format, and
[docs/schema-versioning.md](docs/schema-versioning.md) for file compatibility policy.
[docs/python-api.md](docs/python-api.md) documents the Python authoring boundary.

For contributor workflow and milestones, see [CONTRIBUTING.md](CONTRIBUTING.md) and
[ROADMAP.md](ROADMAP.md).

## License

Volt is licensed under the [Apache License 2.0](LICENSE). You may use Volt in open
source or proprietary products, subject to the license terms.

## Status

Volt is pre-release kernel work. The logical model, typed electrical semantics, hierarchy
primitives, validation foundation, selected-part data, deterministic JSON persistence,
authoring helpers, Python bindings, and the schematic and PCB projection layers (including
KiCad export) are in place. APIs are still evolving ahead of a stable release, and deeper
ERC, constraints, and a simulation foundation remain planned.
