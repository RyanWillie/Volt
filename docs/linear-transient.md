# Native linear transient execution

Transient analysis is explicit and native. Ordinary check, build, export and manufacturing
commands do not solve a circuit. C++ owns the request, waveform, exact input, storage mapping,
compiled graph, initialization, numerical acceptance and persisted results. Python provides
construction syntax. DC remains the default analysis; existing DC and AC behavior is retained.

Run the exact-part RC startup example with an output directory that does not already exist:

```sh
PYTHONPATH="$PWD/build/dev/python" python samples/linear_transient/run_transient.py /tmp/volt-rc-startup
```

The example authors a 1 kOhm resistor and 1 uF capacitor (tau = 1 ms), retains an immutable
request with an explicit 0 V capacitor state, writes a verified bundle and then invokes the
native CLI. The constant 1 V source means the post-start value at t=0+. There is no t=0- sample
or automatic equilibrium calculation. Its voltage at tau should be near 0.632120559 V.

## Request and initial storage

`prepare_electrical_input(design)` captures the exact logical circuit and selected-Part closure.
The same neutral types serve DC, AC and transient: `ElectricalInput`, `ElectricalInputIdentity`,
`ElectricalNetRef`, `ElectricalOccurrenceRef`, request/source/probe keys and `ElectricalNetPair`.
Verified bundle circuits expose `electrical_input()`. The former DC/AC shared-type names and
preparation aliases are removed; DC-specific requests, sources and solvers retain their names.

```python
input = volt.prepare_electrical_input(design)
supply, output, ground = input.nets
voltage = lambda value: volt.Quantity(volt.UnitDimension.VOLTAGE, value)
request = volt.TransientRequest(
    "startup", input,
    volt.TransientTimeGrid([volt.seconds(t) for t in (0, 0.001, 0.003)]),
    reference=ground,
    sources=[volt.TransientVoltageSource("drive", supply, ground,
                                        volt.TransientWaveform(voltage(1)))],
    probes=[volt.DcVoltageProbe("out", output, ground)],
    initial_state=[volt.TransientInitialState(
        input, input.occurrences[1], "body",
        volt.TransientStorageKind.CAPACITOR_VOLTAGE, voltage(0))],
)
```

The occurrence and model-element key must target the actual selected capacitor. Every participating
capacitor or inductor, including private/composite model elements, needs exactly one oriented
voltage or current. Missing state is incomplete analysis; no state is silently filled with zero.
Wrong kind/dimension, foreign/stale input, duplicate targets and excluded/extra storage are rejected.
Source-free response and an explicitly supplied all-zero state are valid. Resistor-only requests
have no storage and solve the requested times directly.

`initial_state_from(successful_dc_solution_or_report)` copies complete oriented storage values,
exact input/selected-Part identity and DC-result provenance. Pass its native result directly as
`initial_state`. It performs no solve, rejects unsuccessful reports and remains bound to the
participating storage set. A target request must match that set and its exact input. Its t=0+
source constraints are independently validated even when state came from DC.

Times are finite SI Time quantities, with at least two strictly increasing samples beginning at
zero and ending at a positive horizon. `TransientTimeGrid.uniform(horizon, count)` materializes
inclusive endpoints. Sources have signed Voltage or Current values, explicit orientation and
unique keys. A waveform is either a constant quantity or a list of `TransientWaveformKnot(time,
value)` values. Continuous PWL knots begin at zero, increase strictly, stay within the horizon
and hold the final value. The native solver lands exactly on every knot and requested sample;
knots do not add published samples. Callbacks, jumps, repetition and inferred stimulus are excluded.

Voltage is V(from)-V(to); current is positive from from-net to to-net. Model-element observations
use their selected model's stored orientation. Signed branch power is absorbed voltage times
current. Reversing an authored pair reverses the observation; storage polarity is never guessed.

## Initialization support

The initializer fixes all capacitor voltages and inductor currents and solves the instantaneous
KCL, resistor and source constraints. It validates unique finite coordinates, conditioning,
frozen-state residuals and the complete continuous electrical residual with recovered derivatives.
It never approximates t=0+ using a tiny integration step.

This bounded projector deliberately rejects some valid designs. An ideal capacitor directly
across an ideal voltage source, compatible ideal parallel capacitors and other dependent storage
constraints can produce `unsupported_initialization_topology`. This reports rank/origin evidence
and does not establish physical nonuniqueness. Incompatible fixed states instead report
`inconsistent_initial_state`. There is no automatic ESR/ESL substitution, shunt, least-squares
answer or invented current. Explicitly authored composite ESR/ESL models remain available.

## Adaptive backward Euler and damping

Each macro trial of duration h compares one full backward-Euler solve with two h/2 solves from
the same accepted state. Successful trials retain the two-half trajectory. Each actual half step
has its own algebra evidence and duration. The temporal error is the maximum storage-coordinate
full-versus-two-half difference divided by `absolute + relative * max(previous, full, two_half)`.
It must be finite and at most one; algebra gates are independent. Rejected temporal trials shrink
and retry; numerical/rank/conditioning/residual failures terminate with timed evidence.

Backward Euler introduces artificial damping, including in lossless LC circuits. Defaults are
local tolerances: relative 1e-4, absolute 1e-6 V and 1e-9 A. They do not guarantee global waveform,
phase or energy accuracy. Tighter settings reduce the observed damping at additional trial/solve
cost. No long-horizon phase/energy or performance guarantee is made. The analytical accuracy
corpus starts with relative 1e-7, absolute 1e-9 V and 1e-12 A.

The native lossless-LC fixture uses L=10 mH, C=10 uF, vC(0)=1 V, iL(0)=0,
output samples [0, T] over one period T=2*pi*sqrt(LC), h_initial=h_max=T/20,
h_min=1e-12 s and trial/accepted-macro budgets 200000/100000:

| Temporal settings | Energy lost over one period | Phase error | Trials | Accepted macro trials | Solves | Factorizations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Defaults: 1e-4 / 1e-6 V / 1e-9 A | 5.409% | 0.000164367 rad | 362 | 356 | 1087 | 1087 |
| Tight: 1e-7 / 1e-9 V / 1e-12 A | 0.1787403603% | 1.6979686284e-7 rad | 11039 | 11034 | 33118 | 33118 |

Default local acceptance therefore admits about 5.4% artificial energy loss in this specific
lossless fixture. Tight settings reduce it at roughly 30 times the trial count. These are actual
native solver observations from `tests/io/electrical/linear_transient_test.cpp`; independent
scratch estimates in the issue are separate. They are fixture-specific observations, not
universal error bounds or runtime performance certification. Solve and factorization counts were
measured separately; this implementation performs a distinct factorization for every actual solve
in this fixture. The accepted-half-step counts are 712 and 22068 respectively.

All step bounds and work budgets are mandatory. h_min <= h_initial <= h_max, all finite positive
Time quantities, and positive integer trial/accepted-step budgets. h denotes a macro trial;
the accepted-step budget counts accepted macro trials, each contributing two actual half steps
to `accepted_half_steps`. Trial budget includes rejections. The output grid does not choose step
bounds. A boundary-clipped trial may be below h_min, but failure requiring further reduction stops the solve. Representable midpoint,
progress, finite coefficients and finite normalization checks prevent false progress.

The fixed algebra gates use rank threshold and minimum equilibrated reciprocal condition 1e-12,
relative residual tolerance 1e-9 and absolute voltage/current floors 1e-9 V/1e-12 A. They remain
separate from the tunable temporal tolerances. Reports preserve evaluations, accepted half steps,
last accepted time and separate solve/factorization counters. Three solves per macro trial do
not promise three distinct factorizations.

## Explicit CLI and artifacts

```sh
volt simulate --project samples/linear_transient --design startup \
  --analysis transient --backend native --request /tmp/volt-rc-startup/request.json --output source-transient \
  --h-min 1e-12 --h-initial 5e-5 --h-max 1e-4 \
  --max-trials 100000 --max-accepted-steps 100000 \
  --relative-tolerance 1e-7 --absolute-voltage-tolerance 1e-9 \
  --absolute-current-tolerance 1e-12 --json

volt simulate --bundle /tmp/volt-rc-startup/startup.volt --design startup \
  --analysis transient --backend native --request /tmp/volt-rc-startup/request.json \
  --output bundle-transient --h-min 1e-12 --h-initial 5e-5 --h-max 1e-4 \
  --max-trials 100000 --max-accepted-steps 100000 \
  --relative-tolerance 1e-7 --absolute-voltage-tolerance 1e-9 \
  --absolute-current-tolerance 1e-12 --json
```

CLI time/tolerance numbers use SI seconds, volts and amperes. Public ngspice transient is rejected;
ngspice 46 with method=trap and maxord=2 is an independent acceptance oracle only. Bundle mode
verifies the exact vendored model closure and never imports source or discovers a project, even
from an unrelated working directory with poisoned source files.

The output directory must not exist. Publication is atomic. A complete success (exit 0) publishes
`request.json`, `compile-report.json`, `solve-report.json` and `solution.json`. Incomplete compilation
(exit 1) publishes request/compile evidence. Unsuccessful initialization/integration (exit 1)
adds a timed solve report and has no solution. Malformed request, invalid options/backend or I/O
failures are exit 2. Work-budget exhaustion cannot publish a partial successful solution.

The new wire contracts are `volt.transient-request`, `volt.transient-solve-report` and
`volt.transient-solution`, each version 1. Shared input/key renaming changes public API spellings
without changing DC/AC wire formats. The compiled graph and compile-report snapshots remain
version 1; the graph adds the explicit transient request/source variant without altering existing
DC/AC snapshots. Analysis identity includes exact model/request/storage/provenance, grid/knots, backend/integrator,
error policy and effective bounds/budgets. Request/graph transport is deterministic; numerical
reproducibility across supported platforms is tolerance-based. Authored Part/Circuit/Schematic/
Board bytes and their identities do not change merely because a request is compiled or solved.
No legacy readers or compatibility aliases are introduced.
