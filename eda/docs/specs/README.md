# Volt EDA Specs

This directory contains implementation specs for non-trivial `eda/` work.

## Workflow

For any non-trivial Pebble issue:

1. Claim the issue.
2. Create or update a spec in this directory.
3. Commit the spec.
4. Implement from the spec.
5. Add tests matching the spec's acceptance criteria.
6. Close the issue only after the spec's provenance and acceptance sections are complete.

See `eda/CLEAN_ROOM.md` for the full clean-room policy.

## Naming

Use short, stable file names:

- `drc-net-aware.md`
- `zone-refill.md`
- `device-assignment.md`
- `schematic-multigate.md`

## Required sections

Each spec must include:

- Title
- Status
- Linked Pebble issue
- Summary
- Motivation
- Goals
- Non-goals
- Current state
- Proposed design
- Data model changes
- CLI / API changes
- Algorithm / behavior
- Test vectors
- Acceptance criteria
- Provenance / sources
- Open questions

A starter template lives in `_TEMPLATE.md`.

## Status values

Suggested values:

- `draft`
- `approved`
- `implemented`
- `superseded`

## Provenance guidance

Be explicit.

Good:

- `Ucamco Gerber Layer Format Specification, used for output semantics`
- `KiCad gerber output used only as a black-box oracle for test vectors`
- `spade crate (MIT/Apache-2.0) used as implementation dependency`

Not good:

- `looked at KiCad`
- `used clipper`
- `based on LibrePCB`

## Minimal checklist

Before implementation starts:

- [ ] Pebble issue is claimed
- [ ] spec exists
- [ ] provenance section is started
- [ ] acceptance criteria are concrete and testable

Before issue close:

- [ ] tests cover acceptance criteria
- [ ] provenance section is complete
- [ ] implementation notes updated
- [ ] clean-room attestation added to commit / notes
