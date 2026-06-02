# Volt Kernel Architecture Design

**Status:** Proposed — supersedes the `CircuitView` direction taken in PR #135.
**Date:** 2026-06-02
**Scope:** C++ kernel architecture (logical `Circuit`, schematic and PCB projections, and the
ERC/DRC validation layer). Python is out of scope and stays "syntax over kernel-owned meaning."

---

## 1. Motivation

`Circuit` is a god class: a single type that owns entity storage **and** the behavior for
connectivity, hierarchy/modules, electrical attributes, design intent, and queries. The same
shape is emerging in `Board` (board.hpp was ~2552 lines) and the Python `Schematic` class.

The cost is not correctness — it's maintainability: there is no obvious answer to "where does
this piece of logic belong?", every feature edits the same large file, and the invariant
surface that must be kept correct is enormous.

### What PR #135 did and did not achieve

PR #135 introduced `CircuitView` plus `CircuitHierarchy` / `CircuitElectrical` /
`CircuitDesignIntent` facades and made the corresponding methods `private` with the facades as
`friend`s. This **enforced a segmented public API** (a real improvement) but did **not**
decompose anything:

- `circuit.cpp` is unchanged in size (~852 lines); all logic still lives in `Circuit`.
- The facades are pure pass-through (`return circuit_->same_method(...)`); they hold no logic.
- Four `friend` classes each get access to **all** of `Circuit`'s privates — wider coupling
  than before.

This document defines the architecture that actually completes the decomposition, and corrects
two design mistakes in #135: the `CircuitView` read wrapper and the broad friendship.

---

## 2. Design principles

These extend the ROADMAP's existing "Design Rules For Roadmap Work":

1. **Invalid kernel state is impossible** — structural invariants are enforced at mutation
   boundaries and violations throw.
2. **Bad design intent is diagnosable** — design-quality issues are reported as diagnostics,
   never thrown (the Notification pattern).
3. **Const-correctness is the read/write boundary** — readers take `const T&`. Do not invent a
   wrapper type to re-express what `const` already guarantees.
4. **Prefer non-member non-friend functions** — behavior that can be expressed over a type's
   public interface lives *outside* the type (this increases encapsulation; Meyers, *Effective
   C++* Item 23).
5. **Models own data; checkers consume data** — validation is read-only behavior over a model,
   never a part of the model.
6. **Composition over inheritance** — a model *has-a* subsystem; it never *is-a* subsystem.
7. **Dependencies are acyclic** — a model's subsystems never reference the model back.
8. **The model root's public surface is a defended budget, not a convenience layer** — new
   public root methods must either be structural primitives, cross-subsystem mutation
   boundaries, or deliberately reviewed additions to the root API budget.
9. **Avoid speculative abstraction** — every abstraction below is justified by 2+ concrete
   consumers that exist today or are on the near-term ROADMAP (PCB, then simulation).

---

## 3. Core architecture

The kernel is built from three kinds of unit, each with one job:

### 3.1 Models — aggregate roots composing subsystems

A **model** (`Circuit`, `Schematic`, `Board`) is a DDD-style **aggregate root**: a cluster of
data treated as one consistency boundary, with the root enforcing the invariants that span the
cluster. The root is *thin*: it composes subsystems, owns only cross-subsystem invariants, and
**delegates** concern-local work down.

- Each **subsystem** owns its own data and its own *local* invariants.
- Cross-subsystem invariants (e.g. "an electrical attribute's owner must exist") live on the
  **root**, which validates the cross-reference *before* delegating.
- **Acyclic rule (critical):** dependencies point one way — `root → subsystem`. A subsystem
  never holds a `Circuit&` and never calls back up. Data flows down with validated inputs. This
  is the rule that prevents regressing to #135's friend/back-reference coupling.

> **Why the connectivity core cannot be split further.** Components, pins, and nets are bound by
> a single spanning invariant ("a pin is on at most one net"; "a net references valid pins").
> They form one `ConnectivityModel`. Three entity types in one unit here is *legitimate
> cohesion*, not a god class — the god class had six *unrelated* concerns.

### 3.2 Queries — free functions over a model

Derived reads (lookup by reference/name, adjacency, traversal) are **non-member non-friend free
functions** taking `const Model&`, grouped by concern (`volt::queries`). They use only the
model's public accessors. If a query cannot be written over the public interface, that reveals
either a genuinely core member or a *missing public primitive*. Public read primitives must be
fundamental and representation-hiding: expose concepts such as `net_of(pin)` and `pins_on(net)`,
not storage structures such as entity tables. A query must prove it cannot be composed from
existing primitives before a new read accessor is added. Public write primitives must be
invariant-safe mutation boundaries.

There is **no `CircuitView` wrapper.** `const Circuit&` is the read surface. (Keep
`NetContinuityView` — it *computes* an adjacency model, which is a view that adds something.)

### 3.3 Checkers — read-only rule modules over models

Validation (ERC, DRC, schematic checks, future sim-readiness) is a set of **read-only rules**
that consume a model and emit into a shared `DiagnosticReport`. Checkers are never composed into
the model — they are consumers. The mechanism is one minimal, generic registry:

```cpp
template <class Model>
class RuleSet {
    std::vector<std::function<void(const Model&, DiagnosticReport&)>> rules_;
  public:
    RuleSet& add(auto rule) { rules_.emplace_back(std::move(rule)); return *this; }
    void run(const Model& model, DiagnosticReport& report) const {
        for (const auto& rule : rules_) rule(model, report);
    }
};
```

`ERC = RuleSet<Circuit>`, `DRC = RuleSet<Board>`, `SchematicChecks = RuleSet<Schematic>`. Adding
a check is "write a function, register it" — no core changes. Keep `RuleSet` minimal; it is a
registry, not a plugin DSL.

---

## 4. Applied to `Circuit` (the flagship)

```cpp
class Circuit {                       // aggregate root / coordinator — thin
    ConnectivityModel connectivity_;  // components + pins + nets, connect/disconnect (spanning invariant)
    HierarchyModel    hierarchy_;     // module defs/instances, ports, bindings
    ElectricalModel   electrical_;    // typed attributes keyed by entity id + local dimension invariants
    DesignIntent      intent_;        // intentional stub / no-connect flags
    // RuleClasses    rule_classes_;  // (future) netclass intent — see §6
  public:
    // Core structural primitives stay here (public):
    [[nodiscard]] ComponentId add_component(ComponentInstance c);
    [[nodiscard]] NetId       add_net(Net n);
    bool connect(NetId net, PinId pin);
    bool disconnect(PinId pin);
    // ... minimal fundamental accessors ...

    // Cross-subsystem ops: guard the cross-reference at the root, delegate the local work down:
    void set_component_electrical_attribute(ComponentId c, ElectricalAttribute a) {
        connectivity_.require_component(c);          // cross-ref invariant: root's job
        electrical_.set_attribute(c, std::move(a));  // concern-local: delegated
    }
};
```

**Where each current `Circuit` responsibility goes:**

| Current concern | New home |
|---|---|
| components/pins/nets + connect/disconnect | `ConnectivityModel` (composed; the irreducible core) |
| `add_module_*`, `instantiate_root_module`, `bind_port`, `restore_*` | `HierarchyModel` (composed) + orchestration as free functions over public primitives |
| `set_*_electrical_attribute`, `select_physical_part` | `ElectricalModel` (composed) |
| `mark_intentional_*` | `DesignIntent` (composed) |
| `*_by_reference`, `*_by_name`, `*_for`, `net_of` (derived) | `volt::queries` free functions over `const Circuit&` |
| ERC `validate_*` | `RuleSet<Circuit>` (checker module, §5) |

**Outcomes:** `Circuit` becomes a thin coordinator; `circuit.cpp` shrinks materially; the
`friend` declarations from #135 are deleted (`CircuitView` is removed entirely; mutation facades
are removed in favor of subsystems + free functions). The acceptance test is literally: *the
broad `friend` declarations are gone.*

---

## 5. Generalization to Schematic and PCB

The same pattern applies to every model layer — this uniformity is what makes "where does logic
go?" answerable across the codebase.

| Model (aggregate root + subsystems) | Checker |
|---|---|
| `Circuit` — Connectivity + Hierarchy + Electrical + Intent | **ERC** = `RuleSet<Circuit>` |
| `Schematic` (projection over `Circuit`) — Sheets + Symbols + Wiring + Labels | schematic checks = `RuleSet<Schematic>` (+`Circuit`) |
| `Board` (projection over `Circuit`) — Placement + Copper + Zones + Layers + DesignRules | **DRC** = `RuleSet<Board>` (+`Circuit`) |

**Projections are read-only consumers of the logical core** and own their own presentation /
physical data; they never own connectivity. `Board` already holds a `const Circuit&`.

`Schematic` and `Board` are themselves god-ish today and get the *same* aggregate decomposition
(this also finishes the file-split begun in the compiled-libraries refactor —
`board_geometry/layers/outline/features/copper` headers already exist; the `Board` *class* is
the remaining monolith).

### 5.1 ERC and DRC are already the right shape

The current code already implements both as read-only free functions emitting into the shared
report; this design only *organizes* them into rule sets:

- DRC today: `validate_track_widths(const Board&, DiagnosticReport&)`,
  `validate_via_rules(const Board&, …)`, `validate_netless_zone_outline_clearance(const Board&, …)`.
- ERC today: `validate_pin_connection_requirements(…, DiagnosticReport&)`,
  `validate_power_and_ground_semantics`, `validate_selected_part_voltage_ratings`.
- Shared diagnostics: `Severity`, `DiagnosticCode`, `EntityRef`, `Diagnostic`, `DiagnosticReport`.

### 5.2 Multi-model (net-aware) rules

A rule that needs more than one model is a free function over multiple const refs, living in the
checker module for its domain:

```cpp
// "copper on different nets must keep >= clearance" — needs Board geometry + logical net identity
void check_copper_clearance(const Board& board, const Circuit& circuit, DiagnosticReport& report);
```

### 5.3 The linchpin: shared `NetId`

Because `Schematic` and `Board` both *project* from the one logical `Circuit`, a net is the same
`NetId` everywhere. This is why ERC and DRC speak the same language: a clearance rule can ask
"different nets?", and an ERC finding and a DRC finding on the same net correlate automatically.
This correlation is the dividend of "the logical model is the source of truth," and it only
works because projections never own their own connectivity.

---

## 6. Netclasses / rule-classes (ROADMAP item)

A netclass ("high-voltage: max 400 V, min clearance 0.5 mm") is **stored design intent — state**
— so it is a **composed subsystem on `Circuit`** (`RuleClasses`), not a checker. Checkers *read*
it:

- ERC reads the voltage limit → logical voltage rule.
- DRC reads the *same* netclass's clearance → physical clearance rule.

One intent definition, enforced in two domains, because both checkers read the same logical
model. Split: **constraint *parameters* live on the model (data); constraint *checking* lives in
the rule module (behavior).** Begin with only constraints Volt can validate soon (voltage,
current limits, source requirement, selected-part rating), per the ROADMAP.

---

## 7. Future: simulation

Simulation (next-but-one on the ROADMAP) is **another read-only projection/consumer** of
`Circuit` + the electrical subsystem; "sim-readiness" is another `RuleSet`. Adding it changes
nothing structural — it is another reader of the source of truth, exactly like `Board` and
`Schematic`. No simulation engine/solver/SPICE work is implied here; only that the architecture
admits sim as a consumer without modification.

---

## 8. The decision tree — "where does this logic go?"

For any new piece of logic:

1. **Does it enforce a structural invariant spanning subsystems, needing intimate state access?**
   → a member of the relevant **model root** (keep this set minimal).
   If an operation can coordinate multiple subsystems or leave invalid kernel state unless
   preflighted atomically, it belongs on the aggregate root, even if most of the work delegates to
   subsystems.
2. **Does it own data with local invariants for one concern?** → a **composed subsystem** of the
   model (acyclic; no back-reference).
3. **Is it a read derived from the model's public interface?** → a **free function** in
   `volt::queries`.
4. **Is it a read-only check producing diagnostics?** → a **rule** in the relevant `RuleSet`
   (ERC/DRC/schematic/sim).
5. **Is it a stored constraint/intent?** → a **composed intent subsystem** (e.g. `RuleClasses`),
   *read* by checkers in step 4.
6. **Is it authoring convenience composing primitives?** → a **free function** in
   `volt::authoring` (or the Python layer), over the model's public surface.

---

## 9. Explicitly rejected

- **`CircuitView` read wrapper** — re-implements `const` with ceremony and a duplicated surface.
  Use `const Circuit&`. (Keep `NetContinuityView`, which computes an adjacency model.)
- **Inheritance of subsystems** (`Circuit : ElectricalSystem`) — not an is-a relationship; leaks
  interfaces and welds concerns together. Use composition.
- **Broad `friend` grants** — the #135 shortcut. Subsystems and free functions use the public
  interface; missing access means a missing primitive, not a missing friend.
- **Composing validation into models** — validation is behavior over data; it stays a consumer.
- **Observer/reactivity, ECS, Pimpl-everywhere** — speculative; no current consumer.

---

## 10. Relationship to PR #135 and sequencing

#135 may merge as the enforced-API-boundary milestone. This design then *completes* the
decomposition. Suggested staging (each stage: full suite green, 0 warnings):

1. Replace `CircuitView` usage with `const Circuit&`; remove the `CircuitView` type (retain
   `NetContinuityView`). Move derived queries to `volt::queries` free functions.
2. Extract `ConnectivityModel` as the core; make `Circuit` compose it; keep structural primitives
   public. Delete its friend.
3. Extract `HierarchyModel` (+ move orchestration to free functions over public primitives);
   delete its friend.
4. Extract `ElectricalModel` and `DesignIntent`; delete their friends. `circuit.cpp` should now
   be materially smaller.
5. Introduce `RuleSet<Model>`; convert ERC `validate_*` into registered rules over `const
   Circuit&`. Then DRC over `const Board&`.
6. Apply the aggregate decomposition to `Board`, then `Schematic`.
7. Add `RuleClasses` intent subsystem + first netclass-driven ERC and DRC rules.

Each extraction adds unit tests for the subsystem's now-real logic.

---

## 11. Verification gates

- `cmake --preset dev && cmake --build --preset dev && ctest --preset dev` — all tests pass.
- Clean build (`--clean-first`) — **0 warnings** under the strict warning set.
- Coverage ≥ 80%; golden fixtures unchanged; `check-circuit-architecture-boundary.py` and the
  KiCad boundary check still pass.
- Public API snapshot checks for model roots pass. Each root's public method list is compared
  against a checked-in allowlist; adding a root method requires an explicit allowlist diff and
  review justification.
- **Acceptance test for "decomposition actually happened":** the broad `friend` declarations on
  `Circuit` are gone, and the subsystem `.cpp` files contain real logic (not delegation).

---

## 12. Open decisions (resolve during planning)

- Do the **write-side subsystems** also expose *capability handles* to the authoring layer (e.g.
  hand authoring a `HierarchyModel&` rather than full `Circuit&`), or is that YAGNI for an
  internal kernel? (Read-side wrapper is rejected; the write-side handle question is separate.)
- Exact minimal **public primitive set** on each model root (the accessors free functions and
  subsystems compose over).
- Whether `RuleSet` runs rules in registration order only, or supports grouping/severity
  filtering from the start (prefer minimal until a consumer needs more).
