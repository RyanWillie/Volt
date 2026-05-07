# Development

## Prerequisites

- CMake 3.25 or newer
- A C++20 compiler
- Ninja

## Common Commands

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The `dev` preset also enables the optional Python binding target so the first Python
authoring surface is exercised with the normal local test run. The binding target fetches
`pybind11` through CMake `FetchContent` and writes the importable package into
`build/dev/python`.

Run only the Python authoring MVP test with:

```sh
ctest --preset dev -R volt_python_led_mvp --output-on-failure
```

Generate API documentation with:

```sh
cmake --build --preset dev --target docs
```

Documentation is generated with Doxygen from public headers and Markdown docs. The build
keeps Doxygen optional for normal compilation, but public APIs should be documented as
they are added.

If the Apple system compiler fails inside standard library headers, use the Homebrew LLVM
preset available on this workstation:

```sh
cmake --preset macos-llvm
cmake --build --preset macos-llvm
ctest --preset macos-llvm
```

Run the LED logical-circuit example with:

```sh
./build/macos-llvm/examples/volt_led_circuit_example
```

## Build Targets

The build is split by architecture layer:

- `Volt::Core` contains typed IDs, storage primitives, diagnostics, properties, and the
  version API.
- `Volt::Circuit` contains the logical circuit model and depends on `Volt::Core`.
- `Volt::Authoring` contains component-library presets, reference allocation helpers, and
  connection helpers over `Volt::Circuit`.
- `Volt::IO` contains logical circuit read/write support and owns the JSON dependency.
- `_volt` is the optional private Python extension module used by the public Python
  authoring facade.
- `Volt::Volt` is an umbrella interface target for applications that want the full public
  API.

Format changed C++ files with:

```sh
clang-format -i include/**/*.hpp src/**/*.cpp tests/**/*.cpp
```

## Project Rules

- Keep the kernel independent from UI frameworks.
- Prefer value-oriented data structures and typed IDs over pointer graphs.
- Add tests with each behavioral change.
- Keep derived indexes out of serialized source data.
- Keep public APIs narrow enough to bind cleanly to Python later.
