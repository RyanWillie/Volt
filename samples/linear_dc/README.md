# Explicit native linear DC

This logical-only example defines one exact 1 kOhm resistor Part, uses it twice in a 5 V
divider, and exposes a configured entrypoint that returns a `volt.Project` with only a design
stage. Importing `main.py`, calling `main()`, and running an ordinary project build do not compile
or solve an electrical model.

`run_dc.py` is the separate, explicit Python recipe. It runs only the design stage, selects
`result.design("divider")`, prepares the immutable native DC input, authors a 5 V source and two
probes, writes `DcRequest.to_json()`, writes a source-free ProjectBundle, compiles, and calls
`solve_dc` only when the native compile report is complete. The example Part and footprint are
illustrative rather than manufacturer data.

From a repository build with the Python bindings available:

```sh
export PYTHONPATH="$PWD/build/dev/python"
example_dir=$(mktemp -d)/linear-dc
python samples/linear_dc/run_dc.py "$example_dir"
```

The Python recipe writes `canonicalrequest.json`, `divider.volt`, `compile-report.json`,
`solve-report.json`, and a success-only `solution.json`. The midpoint probe is 2.5 V for the two
equal resistors.

Run the same canonical request through the CLI from source and from the verified source-free
bundle. Each `--output` path must be new:

```sh
python -m volt.cli simulate --project samples/linear_dc --design divider \
  --request "$example_dir/canonicalrequest.json" \
  --output "$example_dir/source-dc" --json

python -m volt.cli simulate --bundle "$example_dir/divider.volt" --design divider \
  --request "$example_dir/canonicalrequest.json" \
  --output "$example_dir/bundle-dc" --json
```

Both modes bind the exact saved request bytes to the selected native input. If the logical design
or exact selected-Part closure changes, regenerate the request explicitly; Volt does not rebind
stale requests by names or indexes.

To run the same source-free input through the bounded external adapter, install ngspice 46
separately and pass its executable explicitly:

```sh
python -m volt.cli simulate --bundle "$example_dir/divider.volt" --design divider \
  --request "$example_dir/canonicalrequest.json" \
  --output "$example_dir/ngspice-dc" --json \
  --backend ngspice --ngspice /absolute/path/to/ngspice
```

Volt generates the deck; this example does not supply arbitrary SPICE text. The ngspice result is
accepted only after native finiteness, uniqueness, conditioning, and residual checks, and its deck
and mapping identities are retained in the published solve provenance.
