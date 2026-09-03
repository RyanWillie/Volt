# Exact-Part electrical models

This small logical-only example defines local exact Parts for a 330-ohm resistor, an ideal
100 nF capacitor, an ideal 10 µH inductor, and a 10 µF capacitor with 0.08-ohm ESR and 1 nH ESL.
A fifth Part deliberately has no model. All values, evidence text, footprints and orderable
identities are illustrative, not manufacturer data or guarantees.

Every model uses the same native builder and ordinary exact-Part authoring path. The composite
has three elements and two private nodes inside one Part; it is one Circuit occurrence.
Models and canonical V/I ratings are distinct. Missing tolerance means unspecified uncertainty;
the ideal-inductor example explicitly supplies zero tolerance to demonstrate that separate state.
An unmodeled LED or IC remains unsupported, and a resistor model does not make a design
simulation-complete. The examples run no solver, Board, schematic or manufacturing workflow.

Read the [single-page guide](../../docs/design/part-electrical-model-authoring.html) for the
ownership and fidelity explanation.

## Python

From a repository build with Python bindings available on `PYTHONPATH` (the development preset
places them under `build/dev/python`):

```sh
cmake --preset dev
cmake --build --preset dev
export PYTHONPATH="$PWD/build/dev/python"
example_dir=$(mktemp -d)
mkdir "$example_dir/source"
cp samples/electrical_part_models/main.py "$example_dir/source/main.py"
cp samples/electrical_part_models/reopen.py "$example_dir/reopen.py"
python "$example_dir/source/main.py" "$example_dir/output"
rm "$example_dir/source/main.py" "$example_dir/output/library.voltlib"
python "$example_dir/reopen.py" "$example_dir/output"
```

`main.py` uses `Library.electrical_model_builder` to snapshot each normal exact-Part definition,
then `Library.part(..., electrical_model=builder.build())` attaches the native immutable result.
It writes a current PartLibraryBundle and ProjectBundle plus `expected.json`, an artifact
expectation file. The independent `reopen.py` imports only `volt` and standard-library modules;
it does not import the author or read the original library. Native `ProjectBundle.open` verifies
the saved graph before the script compares unchanged Part bytes, values, tolerance and evidence.

Both model and canonical Voltage/Current evidence are vendored in this Python example.
The reader also checks model absence and that the logical design still has five occurrences,
two external nets and no Board or schematic.

## C++

The registered example target links the existing native owners and readers:

```sh
cmake --build --preset dev --target volt_electrical_part_models_example
example_dir=$(mktemp -d)
build/dev/tests/volt_electrical_part_models_example write "$example_dir"
rm "$example_dir/library.voltlib"
build/dev/tests/volt_electrical_part_models_example inspect "$example_dir"
```

The write command captures `main.cpp` as authoring provenance. The inspect command does not
read that source or execute the author function: it opens the ProjectBundle, follows each
selected `LibraryPartRef` to the verified Part artifact, uses the current native Part reader
with the loaded component contract, and compares semantic identity and canonical bytes. It also
resolves model evidence solely from the saved project graph. The two commands are separate
processes, so no builder, Part, library or Circuit from authoring survives into inspection.

The native and Python examples intentionally have their own local definitions. Automated parity
tests use separate native fixtures to compare Python-authored values, identities and bytes.
