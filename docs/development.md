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
