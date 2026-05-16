# Contributing To Volt

Thanks for helping improve Volt. The project is currently focused on a small, reliable
logical circuit kernel with deterministic serialization and validation.

## Core Principle

Volt's kernel follows this rule:

```text
Invalid kernel state should be impossible.
Bad circuit design should be diagnosable.
```

Structural integrity belongs at mutation boundaries. Examples include missing IDs,
dangling references, concrete pins connected to multiple nets, and selected-part mappings
that do not match a component definition.

Design-quality issues belong in diagnostics and validation. Examples include unconnected
required pins, single-pin nets, incompatible electrical roles, and future power-domain
findings.

## Development Workflow

Use normal CMake presets:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --build --preset dev --target docs
```

The `dev` preset uses Ninja, a debug build, exported compile commands, and tests enabled.
On macOS it also asks CMake to use the active macOS SDK via `CMAKE_OSX_SYSROOT=macosx`.

Useful checks before sending changes:

```sh
python3 scripts/check-format.py
cmake --build --preset dev
ctest --preset dev
cmake --build --preset dev --target docs
```

CI runs formatting, build, tests, and docs on Ubuntu, macOS, and Windows.

## Formatting

C++ code is formatted with `clang-format` using `.clang-format` in the repository root.
Run:

```sh
python3 scripts/check-format.py
```

To apply formatting locally:

```sh
find include src tests examples -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 \
  | xargs -0 clang-format -i
```

To install the repo-owned pre-push hook that blocks pushes when formatting is invalid:

```sh
python3 scripts/install-git-hooks.py
```

## Testing Expectations

Use test-driven development for non-trivial behavior changes:

1. Add or update a failing test that captures the expected behavior.
2. Implement the smallest change that makes it pass.
3. Run the full local verification commands.

Public API additions should be Doxygen documented. The docs target treats warnings as
errors.

## Issue Workflow

Volt uses Pebble (`pb`) for local issue tracking. Before starting non-trivial work:

```sh
pb ready
pb show <id>
pb claim <id>
pb update <id> --branch codex/<short-branch-name>
git switch -c codex/<short-branch-name>
```

When finished:

```sh
pb update <id> --verification-status passed \
  --verification-summary "Build passed; tests passed; Doxygen completed." \
  --verification-command "cmake --build --preset dev && ctest --preset dev && cmake --build --preset dev --target docs"
pb close <id> -r "Implemented and verified."
```

Keep changes surgical. Avoid unrelated refactors or cleanup in feature branches.

## CMake Targets

Volt is split into layered targets:

- `Volt::Core` — typed IDs, entity storage, diagnostics, properties, version API
- `Volt::Circuit` — logical circuit model and validation; depends on `Volt::Core`
- `Volt::IO` — logical circuit JSON read/write support; depends on `Volt::Circuit`
- `Volt::Volt` — umbrella target for applications that want the full public surface

Downstream projects should use:

```cmake
find_package(Volt CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE Volt::Volt)
```

or link only the layer they need.
