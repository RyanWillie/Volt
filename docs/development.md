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

The test presets run CTest with parallel jobs. Use `ctest --preset dev -j 1` only when
debugging order-sensitive output.

The common local workflow can also be run through CMake workflow presets:

```sh
cmake --workflow --preset dev
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

Build performance-sensitive benchmark executables with:

```sh
cmake --workflow --preset benchmarks
./build/benchmarks/benchmarks/volt_kernel_benchmarks
```

Benchmarks print CSV to standard output. They are intentionally opt-in so normal
developer builds stay focused on library, test, Python, and example targets.

Enable coverage instrumentation explicitly with:

```sh
cmake --workflow --preset coverage
```

`VOLT_ENABLE_COVERAGE` is supported for Clang and GNU-like compilers. It is not enabled by
default because coverage instrumentation slows the binaries it builds. The `coverage-report`
target prints project totals filtered to `include/volt/*` and `src/*` so tests,
dependencies, and system SDK headers do not dilute the signal. CI runs this workflow on
Linux and fails if filtered line coverage drops below 80%.

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
- `volt_kernel_benchmarks` is the opt-in benchmark executable built when
  `VOLT_BUILD_BENCHMARKS` is enabled.
- `Volt::Volt` is an umbrella interface target for applications that want the full public
  API.

Format changed C++ files with:

```sh
clang-format -i include/**/*.hpp src/**/*.cpp tests/**/*.cpp benchmarks/**/*.cpp
```

## Project Rules

- Keep the kernel independent from UI frameworks.
- Prefer value-oriented data structures and typed IDs over pointer graphs.
- Add tests with each behavioral change.
- Keep derived indexes out of serialized source data.
- Keep public APIs narrow enough to bind cleanly to Python later.
