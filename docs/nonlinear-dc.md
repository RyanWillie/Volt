# Explicit idealized diode DC

Volt's native `ShockleyDiodeElement` supports a bounded, single DC operating point.
Its law is `i = Is * expm1(v/a)`, with `v = V(from) - V(to)`, positive current from
anode to cathode, and `a = n*k*T0/q` using exact SI constants. This is an idealized
mathematical model. Evidence absence means uncalibrated; the examples do not establish
manufacturer fidelity or measured physical performance.

`DiodeParameters(saturation_current, ideality_factor, fixed_temperature,
voltage_domain, evidence=())` contains native `ModelParameter` values for Is (Current)
and n (Ratio), a positive finite Temperature quantity in kelvin, and a required bounded
Voltage `QuantityRange`. Positive nominal values and tolerance bounds are required.
The inclusive domain contains zero and permits only mild reverse voltage:
`vmin >= -3*a`. This excludes ordinary rectifier reverse operation and breakdown.
Each element retains its own prescribed fixed temperature. Volt never rescales Is,
looks up ambient temperature, or applies an analysis temperature override.
Parameter and condition/domain evidence are immutable, exact-identity Part data whose
assets must be present in the library and ProjectBundle closure. Optional tolerance
metadata does not request statistical or corner solving; execution uses nominal values.

Use the existing builder's `add(volt.ShockleyDiodeElement, key, anode, cathode,
parameters)` with builder-owned endpoints. Terminal names and physical pin numbers
never infer polarity. No Circuit device methods or Python law callbacks are added.

```python
options = volt.NonlinearDcSolveOptions(
    volt.DcSolveOptions(), max_iterations=80, max_backtracks=24,
    max_residual_evaluations=2048, max_jacobian_evaluations=81)
compiled = volt.compile_electrical(request)
if compiled.complete:
    report = volt.solve_dc(compiled.model, options)
    if report.success:
        solution = report.solution
```

The default `solve_dc(model)` and default CLI method remain linear and refuse diode
laws explicitly. Native Newton starts all node potentials and branch currents at zero,
uses analytic Jacobians and bounded backtracking, and checks original laws, all-node KCL,
reference, the undamped correction, and final Jacobian rank/condition. No continuation,
clipping, gmin, warm start or backend fallback is used. A successful point is locally
regular under the numerical gates; there is no global convergence or uniqueness guarantee.
A singular iterate does not establish global nonuniqueness; a failed solve does not prove
that no operating point exists. `DomainLimited` means no admissible progressing trial was
found, not proof that the physical point lies outside the declared domain.

Acceptance defaults are voltage floor 1e-9 V, current floor 1e-12 A, relative tolerance
1e-9, rank threshold 1e-12, and minimum equilibrated reciprocal condition 1e-12.
Picoamp leakage tests tighten the current floor to 1e-16 A. Native analysis identity hashes
all effective algorithm, budget and acceptance settings, even on failure. Reports retain
available metrics, counts, provenance and termination reason; missing metrics remain absent.
Only a complete accepted point receives a solution. Reports and solutions use the current
native version-2 DC contract.

Run the executable [example](../samples/nonlinear_dc/main.py):

```sh
python samples/nonlinear_dc/run_dc.py /tmp/diode-example
volt simulate --project samples/nonlinear_dc --design diode-bias \
  --request /tmp/diode-example/accepted/request.json \
  --method diode-newton --output /tmp/diode-source --json
volt simulate --bundle /tmp/diode-example/diode.volt --design diode-bias \
  --request /tmp/diode-example/accepted/request.json \
  --method diode-newton --output /tmp/diode-reopened --json
```

The example writes accepted and domain-limited native reports with exact identities.
Its reopened logical-only bundle requires no authoring source or source library. To force a
bounded failure, add `--max-iterations 1`. CLI budget flags are `--max-iterations`,
`--max-backtracks`, `--max-residual-evaluations` and `--max-jacobian-evaluations`.
Acceptance flags are `--relative-tolerance`, `--absolute-voltage-tolerance`,
`--absolute-current-tolerance`, `--relative-rank-threshold` and
`--minimum-reciprocal-condition`; native constructors validate every supplied value.
Diode flags require `--method diode-newton`; that method requires DC and the native backend.
Transient step flags remain restricted to transient analysis.

Successful execution exits 0, incomplete compilation or a failed numerical analysis exits 1,
and invalid options/input/publication exits 2. Output publication uses the existing atomic
no-replace directory contract. Failure never publishes `solution.json`, and a pre-existing
or racing output destination is preserved. Requests, compile reports and solve reports are
native canonical bytes, including from the source-free bundle route. Separate native
executables may differ in floating-point roundoff: their parity gate keeps identities,
contracts, settings and work counts exact and compares physical observations using the
native acceptance tolerances.

AC and transient compile reports explicitly reject diode models before numerical execution.
The public ngspice adapter returns an incomplete native capability report with branch
provenance; the CLI exits 1 and publishes no deck, process evidence or solution. Sweeps,
transistors, biased AC, nonlinear transient, dynamic temperature, thermal solving and
manufacturer calibration remain outside this slice. Independent high-precision scalar
roots and equation checks establish the canonical fixture accuracy; narrower pinned
ngspice comparisons are external model comparisons, not physical calibration. Cross-platform
numerical comparisons use tolerances; deterministic input identities compare exactly.
