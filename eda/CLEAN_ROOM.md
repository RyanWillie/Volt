# Volt EDA Clean-Room Implementation Policy

This document defines how `eda/` work is specified and implemented so the MIT-licensed Volt EDA codebase does not derive from GPL source code.

## Why this exists

Volt `eda/` is MIT-licensed.

Other EDA tools we have evaluated for behavior and feature coverage include:

- LibrePCB — GPL-3.0-or-later
- KiCad — GPL-3.0 / GPL-2.0+ in parts

We may study those projects during a **research/specification** phase, but implementation for Volt must be authored from Volt-owned specifications and public standards, not copied or translated from GPL source.

## Scope

This policy applies to all work in:

- `eda/crates/**`
- `eda/tests/**`
- `eda/docs/specs/**`
- any helper tooling used to generate EDA outputs for Volt

## Rules

### 1. Separate research, specification, and implementation

Every non-trivial EDA feature must go through three phases:

1. **Research**
   - Allowed inputs:
     - public standards and vendor documentation
     - academic papers
     - permissively licensed libraries and docs
     - GPL tools used as **black-box executables**
     - GPL source code only for high-level behavior analysis
   - Output:
     - notes written in our own words
     - no copied code, no copied comments, no copied class/field names

2. **Specification**
   - Create `eda/docs/specs/<feature>.md`
   - Describe:
     - input/output contracts
     - invariants
     - algorithms in prose or original pseudocode
     - test vectors and oracle data sources
     - chosen dependencies/licenses
   - The spec is the only design document used during implementation.

3. **Implementation**
   - Work only from the Volt spec and permitted public references.
   - Do **not** reopen GPL source while implementing the feature.
   - If more research is needed, stop implementation, update the spec, then resume.

### 2. Use GPL tools as black-box oracles, not code sources

Allowed:

- opening a design in KiCad or LibrePCB
- exporting Gerbers, drills, netlists, SVG, STEP, etc.
- comparing Volt output against those exported artifacts
- writing tests from those artifacts

Not allowed during implementation:

- copying code structure
- porting functions line-by-line
- mirroring internal type layouts, names, or control flow
- translating comments, diagnostics, or rule text verbatim

## 3. Prefer public standards and permissive dependencies

Implementation should be derived from public standards or permissive libraries whenever possible.

Examples:

- Gerber / Gerber X2: Ucamco specification
- Excellon drill: public NC drill documentation
- Specctra DSN/SES: public format documentation
- IPC-D-356: public test netlist references
- Polygon clipping / offsets: a permissive Rust crate or Boost-licensed library
- SPICE simulation: ngspice docs and subprocess integration

Each feature spec must record the exact dependency or standard used.

### 4. Record provenance per feature

Each feature spec must contain a provenance section listing:

- consulted sources
- license of each source when relevant
- whether the source was used for:
  - behavior research
  - test oracle generation
  - implementation dependency

## Existing researched sources

As of 2026-04-17, the following sources have been consulted for behavior and gap analysis:

- LibrePCB source tree at `~/Development/Tools/LibrePCB`
- KiCad source tree at `~/Development/Tools/Kicad`
- Volt source tree at `eda/`
- Ucamco Gerber documentation (referenced conceptually during planning)
- ngspice documentation / FAQ (licensing and integration planning)
- ODB++ and IPC-2581 availability/licensing references (planning only)

These were consulted for comparison and planning. New implementation work must proceed from Volt-authored specs, not from reopening those GPL source trees mid-task.

## Solo-developer clean-room workflow

Volt may be developed by one person. In that case, clean-room separation is achieved by **time and artifacts**, not by separate people.

Required workflow:

1. Research the problem.
2. Write or update `eda/docs/specs/<feature>.md` in your own words.
3. Commit the spec.
4. Implement from the spec.
5. In implementation notes / commit message, attest that no GPL source was consulted during implementation.

## Required attestation

For each non-trivial implementation, include this attestation in the commit message body, PR description, or issue notes:

> Clean-room attestation: this change was implemented from `eda/docs/specs/<feature>.md` and public standards/docs noted there. No GPL source was consulted during implementation of this change.

## Spec requirements

A feature spec in `eda/docs/specs/` must include at least:

- Title
- Status
- Linked Pebble issue
- Motivation
- Goals / non-goals
- Data model impact
- CLI / API impact
- Algorithm / behavior
- Test vectors
- Acceptance criteria
- Provenance / sources
- Implementation notes

See:

- `eda/docs/specs/README.md`
- `eda/docs/specs/_TEMPLATE.md`

## Feature provenance log

Use this table as the high-level register. Detailed provenance belongs in each feature spec.

| Feature | Spec | Primary standards / dependencies | Research-only sources | Status |
|---|---|---|---|---|
| Net-aware DRC | `docs/specs/drc-net-aware.md` | TBD | LibrePCB, KiCad | planned |
| DRC completion | `docs/specs/drc-coverage.md` | TBD | Volt schema + public fab rules | planned |
| Per-netclass DRC | `docs/specs/drc-netclasses.md` | TBD | KiCad, LibrePCB | planned |
| Courtyard overlap DRC | `docs/specs/drc-courtyard.md` | polygon library TBD | KiCad | planned |
| Zone refill engine | `docs/specs/zone-refill.md` | polygon library TBD | KiCad, LibrePCB | planned |
| Device assignment workflow | `docs/specs/device-assignment.md` | Volt schema | Volt only | planned |
| Multi-gate schematic placement | `docs/specs/schematic-multigate.md` | Volt schema | LibrePCB, KiCad | planned |
| Paste Gerber export | `docs/specs/gerber-paste.md` | Ucamco | KiCad | planned |
| Net-segment splitter | `docs/specs/net-segment-splitter.md` | graph algorithm | KiCad, LibrePCB | planned |

## Dependency selection notes

When a feature depends on a third-party crate or library, the spec must name the exact package and license.

Examples of acceptable entries:

- `clipper2` crate — license: <fill in after selection>
- `i_overlay` crate — license: <fill in after selection>
- `spade` crate — MIT/Apache-2.0
- `quick-xml` crate — MIT
- `printpdf` crate — MIT/Apache-2.0

Do not write "use clipper" or "use a Gerber library" without naming the exact package and license.

## Enforcement

Before starting a Pebble issue for non-trivial EDA work:

1. `pb claim <id>`
2. create/update a matching spec in `eda/docs/specs/`
3. only then begin implementation

Before closing the issue:

1. tests exist for the acceptance criteria
2. provenance section is filled in
3. clean-room attestation is included in issue notes / commit message

## Practical guidance

When in doubt:

- derive behavior from standards, math, and test vectors
- use other EDA tools as executable oracles
- write your own names, structures, and control flow
- stop and write a spec before coding further
