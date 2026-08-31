# Current Schema Policy

Volt file formats declare a format name and an integer schema version. The logical circuit
format currently uses:

```json
{
  "format": "volt.logical_circuit",
  "version": 1
}
```

The `format` field identifies the document family. The `version` field identifies its
current canonical schema. The current logical circuit version is exposed by:

```cpp
volt::io::logical_circuit_format_name()
volt::io::logical_circuit_format_version()
```

Writers emit exactly the current format and version. Readers accept exactly the current
format and version.

## Reader Behavior

A reader must reject unsupported or non-canonical documents before constructing partial
kernel state:

- required fields must be present with their documented types
- removed fields and fields from a known non-current schema reject rather than being ignored
- references must use valid typed local IDs and form a complete structurally valid graph
- enum and property spellings must match the documented values
- unsupported format names or schema versions reject

Unsupported documents are structural load errors, not diagnostics, because the reader
cannot safely interpret them as valid kernel state.

## Pre-release Policy

Until Volt has an external release or user contract, each Volt-authored reader supports
only its current canonical schema. Old artifacts must be regenerated with the current Volt
source. Volt does not retain old readers, converters, compatibility overloads, or legacy
output modes during this pre-release period.

Current first-version schemas remain version `1`; being the first current version does not
make them migration paths. ProjectBundle is schema version `3`; the exact Part artifact is `volt.part` version `6`.
PartLibraryBundle remains version `2`, whose existing evidence role and dependency edges
already support the selected model closure. Part semantic identity remains version `2`;
a lossless wire-format change does not redefine E1 model meaning.

## Changing a Schema

When changing a Volt-authored schema:

1. Preserve deterministic output for the new current writer.
2. Do not silently reinterpret or ignore removed fields.
3. Reject non-current versions and removed fields before publishing partial state.
4. Preserve structural invariants at the load boundary.
5. Keep design-quality findings in validation diagnostics unless the document itself is
   structurally invalid.
6. Regenerate checked-in current fixtures and user artifacts.

An external release or user contract may justify a future, separately accepted support
policy. Until then, schema numbers identify current wire contracts; they do not authorize
coexisting readers or writers.
