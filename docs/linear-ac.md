# Native linear AC execution

AC analysis is explicit. Ordinary check, build and manufacturing commands do not solve a circuit.
The request binds the exact logical circuit and selected parts, and can execute against source or
its verified offline project bundle. DC remains the default analysis.

```python
import volt

input = volt.prepare_ac_input(design)
supply = input.net(supply_net)
output = input.net(output_net)
reference = input.net(reference_net)
request = volt.AcRequest(
    "frequency-response", input,
    volt.AcFrequencySweep.logarithmic(volt.hertz(10), volt.hertz(10000), 61),
    reference=reference,
    sources=[volt.AcVoltageSource("drive", supply, reference,
                                 volt.Quantity(volt.UnitDimension.VOLTAGE, 1), phase=0)],
    probes=[volt.AcVoltageProbe("output", output, reference),
            volt.AcVoltageProbe("input", supply, reference)],
    gains=[volt.AcGainProbe("gain", "output", "input")],
)
compiled = volt.compile_electrical(request)
if compiled.complete:
    report = volt.solve_ac(compiled.model)
    if report.success:
        for point in report.solution.points:
            print(point.frequency.value, {probe.key.value: probe.value.value for probe in point.probes})
```

Use existing net handles when preparing a request; supply_net, output_net and reference_net must already exist in the
Design. `AcInput`, net references, probe keys and exclusions reuse the native exact input types.
Frequency samples must be finite, positive, unique and ascending. Sweeps preserve both endpoints.
Amplitudes are nonnegative peak values; phases are radians. The time convention is
`Re{phasor * exp(j * omega * t)}`. DC source values are never implicit AC excitations.

```sh
volt simulate --project ./project --design filter --analysis ac --backend native \
  --request ac-request.json --output ./ac-results --json
volt simulate --bundle ./project.volt --design filter --analysis ac --backend native \
  --request ac-request.json --output ./offline-ac-results --json
```

Public AC execution supports the native backend. ngspice AC decks are independent evidence,
not a public execution adapter. Output must be a new directory. Publication is atomic and retains
canonical request, compilation and solve reports; `solution.json` is published only when every
frequency succeeds. Exit codes are 0 for success, 1 for incomplete or unsuccessful analysis,
and 2 for command failures.

Branch voltage is `V(from)-V(to)` and current flows from `from` to `to`. Complex observations
expose real/imaginary SI values, magnitude and phase; phase is absent for zero magnitude.
`AcGainProbe(key, numerator_probe_key, denominator_probe_key)` adds a keyed same-dimension
complex ratio to the native request. Its denominator must be nonzero. Transfer interpretation
requires one nonzero stimulus. `AcImpedanceProbe(key, from_, to, test_source_key)` adds native
`V / (-I)` in ohms, requiring one nonzero current test source and other sources zero.
Primitive and derived results are ordered together by request key. Undefined measurements fail the sweep.

Run `python samples/linear_ac/run_ac.py OUTPUT_DIRECTORY` for a complete RC low-pass example.
At the middle frequency, the keyed gain is `0.5-0.5j`; this is the 1 kOhm / 1 uF cutoff.

The native solver uses relative rank and reciprocal-condition thresholds of `1e-12`,
relative residual tolerance `1e-9`, and absolute residual floors of `1e-9 V` and `1e-12 A`.
It scales rows and columns by maximum absolute coefficients. These gates establish a unique,
finite solution and verify original complex electrical laws at each frequency. A failed point
retains its metrics and diagnostics and prevents whole-sweep solution publication.

The recorded analytical and ngspice corpus in `tests/fixtures/linear_ac` uses complex comparison
`abs(actual-expected) <= abs_tol + 1e-7*abs(expected)`, with absolute tolerances `1e-9 V`,
`1e-11 A` and `1e-6 Ohm`. Nonzero gain magnitude uses relative tolerance `1e-7` and wrapped
phase uses absolute tolerance `1e-5 rad`. The pinned oracle decks, ngspice version and generated
observations accompany that corpus; they are independent validation evidence.
