---
name: volt-circuit-authoring
description: Author or review canonical Volt logical circuits. Use when creating components, nets, typed pins, electrical attributes, design intent, power intent, diagnostics, project tests, or deterministic logical circuit artifacts.
---

# Volt Circuit Authoring

Use this skill to create canonical Volt logical circuits that can be validated, serialized, inspected, and projected into schematics and PCBs. First read `../shared-volt-architecture.md`.

## Workflow

1. Start with the logical product contract.
   - Name the design and identify required rails, grounds, signals, clocks, connectors, protection, debug paths, and intentional no-connects.
   - Use `volt.Project` for product workflows so design, schematic, PCB, diagnostics, tests, and bundles run in a deterministic order.

2. Author the circuit as kernel-owned data.
   - Create components from built-ins or explicit component definitions.
   - Create nets with stable names, `kind`, and typed electrical attributes such as nominal voltage when they express design intent.
   - Connect pins through logical circuit APIs only. Do not let schematic wires or PCB copper define connectivity.

3. Encode electrical and power intent explicitly.
   - Use typed `PinSpec` data, net kinds, net attributes, component properties, selected-part ratings, and net classes where the current kernel model supports them.
   - Keep design-quality findings in diagnostics and project tests, not in structural workarounds.

4. Add project tests for the behavior that must not drift.
   - Test required net membership, forbidden shorts, placed major components, board outline/placement, expected diagnostics, and product-specific invariants.
   - Use expected diagnostics only when the finding is intentional and documented in the project.

5. Write deterministic artifacts.
   - Use project bundle output for logical JSON, diagnostics, tests, and later schematic/PCB artifacts.
   - Treat `docs/logical-circuit-format.md` as the canonical JSON shape, not the Python builder shape.

## Validation Checklist

- Run the project or focused example command and require `result.ok` unless expected diagnostics are part of the contract.
- Inspect `diagnostics/diagnostics.json` and `diagnostics/tests.json` for unexpected errors, missing expected diagnostics, and failed project tests.
- Inspect logical JSON for stable component definitions, concrete components, concrete pins, nets, selected parts, typed electrical attributes, and deterministic writer output.
- For board-ready circuits, run PCB and BOM readiness validation before starting layout.

## References

- `docs/architecture.md`.
- `docs/authoring-api.md`.
- `docs/python-api.md` sections "Project Framework", "Current Logical Authoring", "Custom Component Definitions", "Selected Physical Parts", and "Error And Diagnostic Mapping".
- `docs/logical-circuit-format.md`.
- `examples/timer_555_led_blinker/main.py` and `examples/timer_555_led_blinker/project_tests.py`.
- `examples/stm32_usb_buck/main.py`.
