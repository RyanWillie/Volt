# Routed PCB visual regression

Issue #246 gates a reviewed routed build using existing native diagnostics. C++
remains the owner of geometry and conflicts. The Python regression runner compares
kernel output; it does not infer overlap or clearance.

## Public regression

Run the same checks used by the existing cross-platform CI CTest suite:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev -R 'Real-board.*regression|python.test_pcb_visual_regression'
```

`tests/pcb/validation/erc_drc_real_board_regression_test.cpp` extends the existing
routed status-controller fixture. It pins ordered diagnostic codes, severity,
category, counts, entities and relevant obstruction rules for placement overlap,
off-board text, label conflicts, text over holes, and off-board features. The last
case is a `pcb.board` error (`PCB_BOARD_FEATURE_OUTSIDE_OUTLINE`), not a visual warning.

`python/tests/test_pcb_visual_regression.py` builds a shareable routed two-part
fixture through `Project`. Its board-stage test requires the exact reviewed native
report. Broken placement, labels, holes and off-board features fail the project.
The advisory fixture permits exactly one off-board review annotation, text entity
1. A second annotation with the same diagnostic code fails; accepting a code alone
cannot authorize it. This synthetic advisory is a policy test, not a badge exception.

Goldens live in `tests/fixtures/pcb_visual_regression/`: canonical PCB JSON, clean
SVG, and the exact advisory report/SVG. Two independent clean builds must match
all review artifacts and every bundle byte. Tests compare project/native reports,
persisted bundle diagnostics, canonical entity references and whole-board SVG
labels/overlays, including element type, severity, ordered entities, layer scope,
and geometry. Tampered SVG code, entity, geometry, element type and missing overlay
variants must fail. The runner refuses optimized Python, which would disable its
assertions. Layer-filtered previews intentionally omit unrelated overlays; the
whole-board preview is the agreement check.

Goldens are reviewed inputs, never regenerated during tests. To intentionally
update them, build the fixture via `build_project()` in the test module, export
`regression.review_artifacts(result, "Regression")`, inspect rendered SVGs and
review the exact diagnostic changes before replacing fixture files.

## Private Pulse Badge proof

The consumer is `Boards/boards/volt_pulse_badge/`. On 2026-10-07, `Boards` was
verified to have no Git repository, so it has no consumer commit SHA or PR workflow.
Its nine authoring/bootstrap/profile inputs are pinned by SHA-256 in the private
`review/pulse-badge-visual-policy.json`. The canonical sorted input-hash map has
SHA-256 `756fe731f9a22c38965b4c3b2a590eeb36b0d4bf49a20ce0599b368aaa726ae7`.
No private board source or preview is included in public Volt.

The exact merged Volt production dependency is
`f24c692215bab7c709b45d40de15d0982f6b8ebc`. A fresh `dev` build in the isolated
issue worktree used matching `src`, `include` and `python/volt` sources. The runner
records the loaded package/extension path and extension hash as build provenance;
a binary hash alone does not establish its source revision. Rebuild before running.

The selected `Pulse Badge` build contains 20 placements, 38 tracks, 13 vias, four
mounting holes, five board texts, one zone and four layers. Both project and native
board diagnostics are empty, and all product-intent tests pass. The reviewed policy
requires exactly zero native board findings; there are no accepted warning codes
or intentional native advisories. Two clean builds produced 52 identical artifacts,
including the canonical bundle, logical/PCB JSON, diagnostics and all layer previews.

Selected SHA-256 review identities:

| Artifact | SHA-256 |
| --- | --- |
| PCB JSON | `7004255183fd8a04ce0be3606bb82dba8725a3ca84e69290896b96fe28d3c340` |
| Combined PCB SVG | `e734069290d9e5fa0960d4735618ad53dd97a450bb47504f1397666d58163830` |
| Front silk SVG | `158a43c9205918752871998bb71bd0e9c494c30db46fc29685f6b1dc1d8fe3b2` |
| Project diagnostics JSON | `37517e5f3dc66819f61f5a7bb8ace1921282415f10551d2defa5c3eb0985b570` |

Use the private policy and a fresh output directory:

```sh
python3 -B scripts/pcb_visual_regression.py \
  --consumer-root /path/to/Boards \
  --volt-root "$PWD" \
  --policy /path/to/Boards/review/pulse-badge-visual-policy.json \
  --output build/pulse-badge-review-new
```

The local consumer also has `tests/test_pulse_badge_visual.py`, integrated with its
existing unittest workflow. Set `VOLT_ROOT` and `VOLT_BUILD_PYTHON` to this fresh
build, then run from Boards:

```sh
python3 -B -m unittest discover -s tests -p test_pulse_badge_visual.py -v
```

The consumer policy/test are local changes in a non-Git workspace. Public CI uses
the shareable fixtures above; it cannot execute or publish the private consumer.

## Rendered review and limits

The clean public fixture, its advisory overlay, and the badge's combined, front/back
copper and front/back silkscreen SVGs were rasterized and visually inspected. The
badge's five LEDs form a symmetric V; the lower timer/driver block and rear battery
holder remain separated by board side. All four holes sit within the outline.
Title, revision and rear battery/polarity labels are visible in their layer views.
The combined preview overlays the rear battery body over front content, so individual
layer previews are necessary for human review. Default labels such as R1/R3 touch
their own package boundaries; native hidden-reference checks compare other packages,
not self-placement. These are preview observations, not suppressed diagnostics.

This proof covers implemented native checks and deterministic review evidence.
Font legibility, label placement on its own package, aesthetic balance, assembly
access, full route readability and manufacturing suitability are not universally
machine-enforced here. Several exported visual code names are reserved without
checks. Long diagnostic legends can clip to the current SVG viewport; structured
diagnostics and overlay geometry remain the regression evidence. No new visual engine, routing redesign, readiness policy or fabrication
order is introduced.
