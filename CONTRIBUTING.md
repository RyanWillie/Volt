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
cmake --workflow --preset clang-tidy
cmake --build --preset dev --target docs
```

The `dev` preset uses Ninja, a debug build, exported compile commands, and tests enabled.
On macOS it also asks CMake to use the active macOS SDK via `CMAKE_OSX_SYSROOT=macosx`.
Compiler warnings are treated as errors for Volt targets by default. The `clang-tidy`
workflow runs first-party static analysis on Linux CI.

Useful checks before sending changes:

```sh
python3 scripts/check-format.py
cmake --build --preset dev
ctest --preset dev
cmake --workflow --preset clang-tidy
cmake --build --preset dev --target docs
```

CI runs formatting, build, and tests on Ubuntu, macOS, and Windows. Documentation
builds are verified on Ubuntu.

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

## Testing Expectations

Use test-driven development for non-trivial behavior changes:

1. Add or update a failing test that captures the expected behavior.
2. Implement the smallest change that makes it pass.
3. Run the full local verification commands.

Public API additions should be Doxygen documented. The docs target treats warnings as
errors.

## Issue Workflow

Anyone may submit a GitHub issue, but submission does not schedule work. Volt's planned
delivery queue is exactly the set of open issues carrying the maintainer-controlled
`roadmap` label.

Before starting non-trivial planned work:

- Confirm that the GitHub issue has the `roadmap` label and read its acceptance criteria.
- Check its native parent/sub-issue and blocked-by/blocking relationships.
- Create a focused branch that includes the GitHub issue number, such as
  `feat/234-short-description` or `fix/234-short-description`.
- Keep the issue and pull request state current while work is in progress.
- Include verification commands and results in the pull request or issue update.
- Use `Closes #234` in the pull request when merging it should complete the issue.

Contributors may propose work without having it automatically admitted to the roadmap.
Only the maintainer, or an agent explicitly acting on the maintainer's behalf, applies
`roadmap`. Unlabelled issues are public intake and must not be selected as planned work.

When finished, record what changed, what was verified, and any follow-up work on the
pull request or GitHub issue.

Do not use Linear or Pebble (`pb`) for active planning.

Keep changes surgical. Avoid unrelated refactors or cleanup in feature branches.

## Licensing And Third-Party Code

Volt is licensed under the Apache License 2.0. Unless explicitly stated otherwise,
contributions intentionally submitted to Volt are accepted under the same license.

Apache-2.0 is permissive: it allows open source and proprietary products to use,
modify, and distribute Volt, subject to the license terms. It also includes an explicit
patent grant from contributors.

For third-party code and assets:

- Do not copy code, generated assets, examples, or documentation from third-party
  projects into Volt unless the source, license, and attribution requirements are
  documented in the change.
- It is fine to reference prior art, APIs, workflows, or visual inspiration in commit
  messages and docs, but inspiration must not become copied implementation.
- If a change intentionally depends on a third-party package, add it through the normal
  build or dependency manifest and document why its license is compatible with
  Apache-2.0.
- If code is adapted from another project, preserve required notices and call that out in
  the pull request.

Some Volt documentation and examples describe SchemDraw-style authoring. SchemDraw is an
inspiration for ergonomic drawing syntax, not a vendored dependency or owner of Volt's
EDA semantics. Keep that boundary explicit in future changes.

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
