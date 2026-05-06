# AGENTS.md

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## Core Kernel Principle

**Invalid kernel state should be impossible; bad circuit design should be diagnosable.**

The kernel must reject structurally invalid operations at mutation boundaries. Examples:
missing IDs, dangling references, pin instances that do not belong to the circuit, or a
pin connected to more than one net.

Bad design intent is different. Examples such as unconnected pins, single-pin nets,
incompatible electrical roles, and power-domain issues should be reported through
diagnostics and validation layers.

When deciding where a check belongs:
- Structural integrity belongs in the core mutation API.
- Design correctness belongs in diagnostics and validation.
- Reporting references such as `EntityRef` must not become normal traversal or mutation
  handles.

## Kernel-Owned EDA Semantics

Python, UI, importers, schematic tools, and future PCB tools are authoring or projection
surfaces. They must not become alternate owners of EDA meaning.

Any operation that changes EDA meaning must be represented in the C++ kernel as explicit
model data, a kernel mutation API, or a kernel-owned constraint. This includes logical
connectivity, selected parts, schematic presentation data, PCB placement/routing data,
rules, and validation results.

Layer ownership is strict:
- The logical circuit owns components, pins, nets, and pin-to-net membership.
- Schematic layers may visualize, arrange, label, and annotate existing logical
  connectivity, but must not create, merge, split, or otherwise mutate nets.
- PCB layers may physically implement existing logical connectivity, but must not define
  the netlist.
- Python should provide ergonomic syntax over kernel-owned state. It should not contain
  Python-only EDA semantics that cannot be loaded, validated, serialized, or inspected by
  the kernel.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

## 5. Avoid overly defensive programming

Handle realistic failure modes, not imaginary ones.

- Do not add guards for impossible states created only by misuse of internal APIs.
- Do not compensate for every theoretical edge case unless the project explicitly needs it.
- Prefer clear invariants and fail-fast behavior over layered defensive checks.
- Validate inputs at boundaries; keep internal code simple once invariants are established.
- If an edge case is worth handling, it should usually have a test.

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

## Issue Tracking

This project uses [Pebble](https://github.com/pebble-tracker/pebble) (`pb`) for local issue tracking. Run `pb --help` for commands.
See `.agents/skills/pebble/SKILL.md` for detailed usage guidance.
