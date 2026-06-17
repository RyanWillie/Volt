# Volt Kernel Architecture Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the Volt kernel architecture decomposition described in `docs/superpowers/specs/2026-06-02-volt-kernel-architecture-design.md`.

**Architecture:** Keep model roots as thin aggregate coordinators that compose concern-owned subsystems. Derived reads move to `volt::queries` free functions, validation moves through minimal read-only `RuleSet<Model>` registries, and no implementation may introduce `CircuitView`, subsystem inheritance, or broad facade friendship.

**Tech Stack:** C++20, CMake/Ninja, Catch2, LCOV, Python boundary scripts, GitHub PR workflow.

---

## Stage 1: Boundary And Snapshot Gate

**Files:**
- Create: `scripts/check-circuit-architecture-boundary.py`
- Modify: `tests/CMakeLists.txt`
- Create: `docs/superpowers/plans/2026-06-02-volt-kernel-architecture-refactor.md`

- [ ] Write a failing boundary test command by adding `check-circuit-architecture-boundary.py` to CTest.
- [ ] Implement the script so it rejects `CircuitView`, broad `Circuit*` facade classes, broad `friend class` declarations on `Circuit`, subsystem back-references to `Circuit`, and empty/delegation-only subsystem source files.
- [ ] Run `python3 scripts/check-circuit-architecture-boundary.py` and `ctest --preset dev -R circuit_architecture_boundary --output-on-failure`.
- [ ] Run the stage gates: `cmake --preset dev`, `cmake --build --preset dev`, `ctest --preset dev`, `cmake --build --preset dev --clean-first`, `python3 scripts/check-kicad-boundary.py`, fixture diff check, and coverage via CI.

## Stage 2: Queries And Const Read Surface

**Files:**
- Create: `include/volt/circuit/connectivity/queries.hpp`
- Create: `src/circuit/connectivity/queries.cpp`
- Modify: `include/volt/circuit/circuit.hpp`
- Modify: `src/circuit/circuit.cpp`
- Modify callers in `src`, `include`, `tests`, and `src/python` as needed
- Test: `tests/circuit/connectivity/queries_test.cpp`

- [ ] Write failing tests for `volt::queries::component_by_reference`, `net_by_name`, `pins_for`, `pin_by_name`, `pin_by_number`, `pin_by_definition`, module lookup queries, port binding queries, and `net_of`.
- [ ] Move derived reads out of `Circuit` where they can be written against fundamental public accessors.
- [ ] Preserve source compatibility only for root methods that are still fundamental primitives or necessary mutation-boundary helpers.
- [ ] Run targeted circuit query tests, then full stage gates.

## Stage 3: ConnectivityModel

**Files:**
- Create: `include/volt/circuit/connectivity/connectivity_model.hpp`
- Create: `src/circuit/connectivity/connectivity_model.cpp`
- Modify: `include/volt/circuit/circuit.hpp`
- Modify: `src/circuit/circuit.cpp`
- Test: `tests/circuit/connectivity/connectivity_model_test.cpp`

- [ ] Write failing subsystem tests for pin-definition storage, component-definition pin membership, unique component references, unique net names, dangling pin rejection, and one-net-per-pin enforcement.
- [ ] Move component, pin, and net storage plus connect/disconnect logic into `ConnectivityModel`.
- [ ] Keep root-level cross-subsystem operations on `Circuit`, delegating local work to `connectivity_`.
- [ ] Run targeted connectivity tests, circuit tests, and full stage gates.

## Stage 4: HierarchyModel

**Files:**
- Create: `include/volt/circuit/hierarchy/hierarchy_model.hpp`
- Create: `src/circuit/hierarchy/hierarchy_model.cpp`
- Modify: `include/volt/circuit/circuit.hpp`
- Modify: `src/circuit/circuit.cpp`
- Test: `tests/circuit/hierarchy/hierarchy_model_test.cpp`

- [ ] Write failing subsystem tests for module-name uniqueness, template-net uniqueness within a module, port membership, module component membership, pin-template connections, instance origin metadata, and duplicate binding rejection.
- [ ] Move hierarchy-owned state and local invariants into `HierarchyModel`.
- [ ] Keep cross-subsystem orchestration such as root module instantiation on `Circuit` or authoring free functions over public primitives.
- [ ] Run targeted hierarchy tests, circuit tests, and full stage gates.

## Stage 5: ElectricalModel And DesignIntent

**Files:**
- Create: `include/volt/circuit/electrical/electrical_model.hpp`
- Create: `src/circuit/electrical/electrical_model.cpp`
- Create: `include/volt/circuit/intent/design_intent.hpp`
- Create: `src/circuit/intent/design_intent.cpp`
- Modify: `include/volt/circuit/circuit.hpp`
- Modify: `src/circuit/circuit.cpp`
- Test: `tests/circuit/electrical/electrical_model_test.cpp`
- Test: `tests/circuit/intent/design_intent_test.cpp`

- [ ] Write failing subsystem tests for electrical owner-kind validation, selected-part attribute storage, idempotent stub/no-connect marking, and deterministic insertion order.
- [ ] Move typed electrical attributes and selected-part state into `ElectricalModel`.
- [ ] Move intentional stub/no-connect assertions into `DesignIntent`.
- [ ] Keep owner-existence checks at the `Circuit` root before delegating to subsystems.
- [ ] Run targeted subsystem tests, circuit validation tests, and full stage gates.

## Stage 6: RuleSet And ERC/DRC

**Files:**
- Create: `include/volt/core/rule_set.hpp`
- Modify: `include/volt/circuit/validation/validation.hpp`
- Modify: `src/circuit/validation/validation.cpp`
- Modify: `include/volt/pcb/board.hpp`
- Modify: `src/pcb/copper/board_copper.cpp`
- Test: `tests/core/rule_set_test.cpp`
- Test: existing `tests/circuit/validation/validation_test.cpp` and `tests/pcb/board_test.cpp`

- [ ] Write failing `RuleSet` tests for registration order and report mutation.
- [ ] Convert circuit validation entry points to construct rule sets of existing read-only rules.
- [ ] Convert board DRC orchestration to a `RuleSet<Board>`-backed shape without changing public diagnostics.
- [ ] Run targeted validation tests and full stage gates.

## Stage 7: Board Aggregate Decomposition

**Files:**
- Create focused board subsystem headers/sources under `include/volt/pcb/` and `src/pcb/`
- Modify: `include/volt/pcb/board.hpp`
- Modify: `src/pcb/board.cpp`
- Test: `tests/pcb/board_*_model_test.cpp`

- [ ] Write failing subsystem tests for layer ownership, outline/rule storage, footprint cache conflicts, placement uniqueness, copper-layer restrictions, zone/keepout/text layer validation, and pad resolution behavior.
- [ ] Move board-owned presentation/physical state into composed subsystems.
- [ ] Keep `Board` as a projection over `const Circuit&`; do not let Board own logical connectivity.
- [ ] Run targeted board tests and full stage gates.

## Stage 8: Schematic Aggregate Decomposition

**Files:**
- Create focused schematic subsystem headers/sources under `include/volt/schematic/` and `src/schematic/`
- Modify: `include/volt/schematic/schematic.hpp`
- Modify: `src/schematic/schematic.cpp`
- Test: `tests/schematic/schematic_*_model_test.cpp`

- [ ] Write failing subsystem tests for sheet storage, symbol storage, symbol placement membership, wiring/label net checks, endpoint inference, and presentation mutation.
- [ ] Move schematic projection state into composed subsystems.
- [ ] Keep logical connectivity source-of-truth in `Circuit`; schematic layers only visualize/arrange/annotate.
- [ ] Run targeted schematic tests and full stage gates.

## Stage 9: RuleClasses

**Files:**
- Create: `include/volt/circuit/rule_classes.hpp`
- Create: `src/circuit/rule_classes.cpp`
- Modify: `include/volt/circuit/circuit.hpp`
- Modify: `src/circuit/circuit.cpp`
- Modify: circuit and board validation files
- Test: `tests/circuit/rule_classes_test.cpp`

- [ ] Write failing tests for defining a rule class, assigning it to a net, rejecting missing nets/classes, and using one stored netclass constraint from ERC and DRC.
- [ ] Store rule-class/netclass design intent in a composed `Circuit` subsystem.
- [ ] Make ERC/DRC read the same logical rule-class data without composing validation into models.
- [ ] Run targeted tests and full stage gates.

## Finalization

- [ ] Confirm broad friend declarations on `Circuit` are gone.
- [ ] Confirm subsystem `.cpp` files contain real logic rather than root delegation.
- [ ] Run full verification and inspect fixture/API/boundary diffs.
- [ ] Commit, push `feat/kernel-architecture`, open a ready PR, create a fresh Codex thread requesting a thermonuclear review, record findings as a PR comment, address actionable findings, push fixes, and leave resolution evidence on the PR.
