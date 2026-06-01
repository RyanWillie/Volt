# Review instructions

These instructions are for automated pull request reviews. Review thoroughly and
independently from the authoring agent. Treat the pull request as a proposal to change
Volt's architecture, behavior, tests, documentation, and contributor workflow.

## Review mandate

Do not limit the review to code quality. Check whether the change is the right change for
Volt.

Review for:

- Correctness bugs, broken edge cases, regressions, and unsafe assumptions.
- Architectural fit with Volt's kernel-first EDA model.
- Whether the feature or approach aligns with the project's current goals.
- Whether EDA meaning is owned by the C++ kernel rather than Python, UI, importers,
  schematic, PCB, or documentation-only behavior.
- Whether the implementation is simpler than plausible alternatives.
- Whether the scope is surgical and does not smuggle unrelated cleanup or speculative
  abstractions into the PR.
- Whether tests, diagnostics, serialization, docs, and examples changed where the behavior
  changed.

Push back when the PR appears to implement the wrong feature, solve the right feature at
the wrong layer, overfit to a narrow case, or weaken a core invariant. A review comment is
allowed to say that an approach should be redesigned before merging.

## What Important means here

Mark a finding as Important when it should be fixed before merge. Important findings
include:

- A structural invalid state can enter the kernel through a public or internal mutation
  boundary.
- A design-quality issue is rejected as a structural error instead of being reported
  through diagnostics or validation.
- Logical connectivity, selected parts, schematic presentation data, PCB placement or
  routing data, rules, or validation results become Python-only, UI-only, importer-only,
  or documentation-only semantics that cannot be loaded, validated, serialized, and
  inspected by the kernel.
- A schematic or PCB layer creates, merges, splits, or otherwise owns the logical netlist.
- Reporting references such as `EntityRef` become normal traversal or mutation handles.
- A public API addition lacks a kernel-owned data model, mutation API, constraint, or
  validation story for the EDA meaning it introduces.
- Serialization or parsing becomes nondeterministic, lossy, silently accepts malformed
  structural data, or emits unstable output.
- Tests fail to cover a new invariant, bug fix, file-format behavior, diagnostic, or
  public API contract that the PR changes.
- Documentation, examples, or generated API docs become materially misleading about what
  Volt can do or where EDA meaning lives.
- Build, packaging, CMake target boundaries, install/export behavior, or platform support
  is likely broken.
- The PR expands scope beyond the stated issue in a way that increases risk or makes the
  change hard to review.

Do not reserve Important only for runtime crashes. Architectural violations are Important
when they make future Volt state ambiguous or move EDA meaning out of the kernel.

## What is Nit at most

Mark a finding as Nit when it is useful but should not block merge by itself:

- Naming, wording, or local style issues that do not obscure behavior.
- Small simplifications that reduce code without changing architecture.
- Missing comments where the code is genuinely hard to follow.
- Minor documentation clarity problems.
- Test organization improvements when the essential coverage exists.

Report at most five Nits per review. If there are more, summarize the pattern instead of
posting many inline comments. If every finding is a Nit, lead the summary with "No
blocking issues."

## Pre-existing issues

Use Pre-existing for bugs or architectural weaknesses that are visible during the review
but were not introduced by the PR. Mention them only when they affect whether the PR is
safe to merge or when the PR makes the existing problem harder to fix.

Do not ask the author to fix unrelated pre-existing problems in the same PR unless the new
change depends on that fix.

## Volt architectural checks

Always check the PR against these project rules.

### Kernel invariants

Invalid kernel state should be impossible; bad circuit design should be diagnosable.

Structural integrity belongs in mutation APIs and loading boundaries. Examples include
missing IDs, dangling references, pin instances that do not belong to the circuit,
selected-part mappings that do not match a component definition, and pins connected to
more than one net.

Design correctness belongs in diagnostics and validation. Examples include unconnected
pins, single-pin nets, incompatible electrical roles, unresolved footprints, and
power-domain issues.

Flag mismatches between these categories as Important.

### Kernel-owned EDA semantics

Python, UI, importers, schematic tools, docs, and future PCB tools are authoring or
projection surfaces. They must not become alternate owners of EDA meaning.

Any operation that changes EDA meaning must be represented in the C++ kernel as explicit
model data, a kernel mutation API, or a kernel-owned constraint. This includes logical
connectivity, selected parts, schematic presentation data, PCB placement/routing data,
rules, and validation results.

### Layer ownership

The logical circuit owns components, pins, nets, and pin-to-net membership.

Schematic layers may visualize, arrange, label, and annotate existing logical
connectivity, but must not define or mutate nets.

PCB layers may physically implement existing logical connectivity, but must not define
the netlist.

Python should provide ergonomic syntax over kernel-owned state. It should not contain
Python-only EDA semantics that cannot be loaded, validated, serialized, or inspected by
the kernel.

### Target boundaries

Check that CMake and library target boundaries remain intentional:

- Core concepts belong in `Volt::Core`.
- Logical circuit behavior belongs in `Volt::Circuit`.
- Authoring helpers belong in `Volt::Authoring`.
- Persistence belongs in `Volt::IO`.
- Schematic and PCB code must not introduce reverse dependencies into lower layers.

Flag dependency inversions, leaky umbrella-target usage, accidental public dependencies,
and install/export regressions when the PR touches build files.

## Implementation checks

Prefer simple, explicit code that enforces invariants at the right boundary. Be skeptical
of generic frameworks, broad abstractions, optional configurability, and defensive guards
that exist only to compensate for unclear ownership.

Check for:

- Mutations that update only one side of a bidirectional relationship.
- ID, reference, or handle lifetimes that can dangle or cross circuit ownership.
- Silent fallbacks when loading, resolving, or validating structured EDA data.
- Non-deterministic ordering in persisted JSON, generated docs, diagnostics, examples, or
  tests.
- New public APIs that are hard to document, hard to validate, or inconsistent with
  existing naming and ownership patterns.
- Changes that make later schematic or PCB work harder by blurring logical ownership.
- Error handling that hides invalid structure instead of failing at the boundary.

When suggesting an alternative, prefer the smallest change that preserves the architecture.

## Testing and verification checks

Behavior changes should have focused tests. Bug fixes should include a test that would
have failed before the fix. New diagnostics should test the code, severity, and referenced
entities where practical. File-format changes should test deterministic write output and
reader behavior for valid and invalid structure.

Look for missing updates to:

- C++ unit tests and golden fixtures.
- Python binding tests when Python behavior changes.
- Serialization round-trip tests when persisted data changes.
- Docs and examples when public behavior or workflow changes.
- CMake, install, packaging, or docs checks when build structure changes.

Do not request tests only for the sake of volume. Request tests when they protect a new or
changed contract, invariant, bug fix, or user-visible workflow.

## Documentation and planning checks

Docs are part of the product. Review documentation changes for architectural accuracy, not
just wording.

Flag as Important when docs:

- Describe capabilities that the kernel cannot actually load, validate, serialize, or
  inspect.
- Teach an ownership model that conflicts with the kernel-first architecture.
- Present illustrative APIs as implemented APIs without saying so.
- Omit required updates to diagnostic catalogs, file-format docs, or public API docs after
  behavior changes.

For planning documents, push back when the proposed sequence would force a later rewrite,
make invalid kernel state representable, or move EDA meaning into a projection layer.

## Scope and project-goal checks

Volt is pre-release kernel work. The current priority is reliable structured circuit
intent: deterministic serialization, validation, authoring surfaces, and stable projection
boundaries for schematic and PCB work.

Flag PRs that:

- Build product UI, routing, manufacturing export, or broad ergonomics before the required
  kernel model and validation path exist.
- Add speculative extension points without a current caller.
- Make a large refactor without a clear acceptance criterion and verification path.
- Solve a Linear issue in a way that conflicts with documented architecture or roadmap
  direction.
- Mix unrelated feature, refactor, formatting, and documentation changes in one PR.

It is valid to recommend splitting the PR.

## Do not report

Do not spend review comments on:

- Formatting that `scripts/check-format.py` already enforces.
- Pure compile failures, test failures, or coverage failures that CI already reports,
  unless the failure reveals a deeper architectural problem.
- Generated build directories, dependency caches, vendored code, or lockfiles unless the
  PR intentionally changes how they are produced.
- Personal preference about naming, layout, or prose when the current version is clear and
  consistent with nearby code.

## Evidence bar

Every Important finding must include a concrete file and line reference and explain the
failure mode. If the finding depends on a project rule, name the rule briefly.

Do not invent requirements that are not supported by repository docs, code, tests, or the
PR's stated goal. When uncertain, ask for clarification or phrase the finding as a risk,
not a fact.

Before posting, verify that the issue is introduced or made materially worse by this PR.

## Review output

Lead the summary with one of:

- `No blocking issues found.`
- `Blocking issues found: N.`

Then summarize the architectural shape of the change in one or two sentences. Call out
whether the approach aligns with Volt's kernel-first model.

For each finding:

- State the concrete problem first.
- Explain why it matters for Volt.
- Suggest the smallest reasonable fix or redesign direction.
- Avoid rewriting the author's code unless a tiny replacement makes the issue clearer.

Keep the tone direct, technical, and collaborative. The goal is to protect the project,
not to maximize comment count.

## Re-reviews

On re-review, focus on whether previous Important findings were fixed and whether the new
commits introduced new Important issues. Suppress new Nits unless they are directly caused
by the fix or the PR author explicitly asks for a fresh full review.
