# Schema Versioning And Compatibility Policy

Volt file formats declare both a format name and an integer schema version. The logical
circuit format currently uses:

```json
{
  "format": "volt.logical_circuit",
  "version": 1
}
```

The `format` field identifies the document family. The `version` field identifies the
schema for that document family.

## Current Logical Circuit Version

The current logical circuit schema is version `1`, exposed by:

```cpp
volt::io::logical_circuit_format_name()
volt::io::logical_circuit_format_version()
```

Writers emit exactly this format name and version. Readers accept exactly this format name
and version until a compatibility path for another version is implemented.

## Reader Behavior

A reader must reject unsupported documents deterministically before constructing partial
circuit state:

- missing `format` or non-string `format` fails
- `format` other than `volt.logical_circuit` fails
- missing `version` or non-integer `version` fails
- integer `version` other than `1` fails

Unsupported version failures are structural load errors. They are reported as exceptions,
not diagnostics, because the reader cannot safely interpret the file as a valid `Circuit`.

## Compatibility Stance For v1

The v1 reader is strict about interpreted structure:

- all required core fields must be present with the documented types
- references must use valid typed local IDs
- enum and property type spellings must match the documented values
- structurally invalid input is rejected instead of normalized

One existing v1 compatibility exception is explicitly retained: a module instance may omit
`component_origins` only when the reader can infer the complete one-to-one mapping
deterministically from instance names, component references, and component definitions. The
reader still validates restored template connectivity, and the canonical writer always emits
the inferred field. This exception is covered by logical reader and round-trip tests; it does
not relax any other required structure.

The v1 reader may ignore unknown fields until an extension mechanism is defined. Unknown
fields are not preserved when rewriting canonical output, so producers must not rely on
unknown fields for data that should round-trip.

Future versions may add explicit compatibility logic, such as reading v1 and writing the
current version, but that migration must be deliberate and tested. Until then, unsupported
versions fail closed.

## Future Versioning Rules

When changing the logical circuit schema:

1. Keep deterministic output for each writer version.
2. Do not silently reinterpret fields with changed meaning.
3. Add migration tests before accepting older versions.
4. Preserve structural invariants at the load boundary.
5. Keep design-quality findings in validation diagnostics, not load errors, unless the
   schema itself is structurally invalid.

A future compatible reader may accept older versions, but it should still write the
current canonical version unless an API explicitly requests legacy output.
