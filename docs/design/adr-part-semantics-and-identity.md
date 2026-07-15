# ADR: Part Semantics and Component/Part Identity

Status: accepted

Issue: [#291](https://github.com/RyanWillie/Volt/issues/291)

## Decision

Volt v1 uses exactly three primary concepts for reusable component and exact-part
semantics:

- `ComponentDefinition` is one immutable reusable logical contract.
- `PartDefinition` is one immutable exact manufacturer part that implements exactly one
  immutable component-contract identity.
- `ComponentInstance` is one occurrence of that component in a `Circuit` and may select
  one exact part.

`PinDefinition` and feature bindings are nested component-contract data. Package terminals,
footprint pads, symbols, models, bundles, and sourcing records are supporting data,
attachments, packaging, or outputs; they are not additional meanings of "part".

The C++ kernel owns these identities, relationships, mappings, and canonical electrical
semantics. Python and other authoring surfaces may provide concise helpers, but every
meaningful value lowers to this kernel-owned model.

Canonical v1 electrical records support only `Voltage` and `Current`. They use explicit
subjects, direction, units, meanings, conditions, and deterministic merge rules. Feature
schemas may compose those records, but feature names and namespaced extensions cannot
create synonyms that satisfy core ERC.

## Ownership And Primary Vocabulary

### ComponentDefinition

A `ComponentDefinition` owns the complete logical contract:

- one readable stable key and one immutable content identity;
- ordered `PinDefinition` data, each with a contract-local stable `PinKey`;
- pin connection requirements and terminal, direction, signal, drive, polarity, and
  other kernel-owned logical semantics;
- named framed pins, directed relations, supply domains, and feature bindings;
- the canonical record shapes that every implementing exact part must supply;
- the separate typed shape of instance-authored requirements and configuration; and
- the default logical schematic symbol reference.

A `PinKey` is portable only within its component contract. It is distinct from a display
name, the current ambiguous `PinDefinition.number`, a build-local `PinDefId`, a package
terminal, and a footprint pad. Package numbering is not component-contract data.

The definition and its `PinDefinition` data are immutable after commit. Changing pin
identity, logical semantics, relations, domains, feature bindings, required record shapes,
or the default symbol creates a new component content identity.

### PartDefinition

A `PartDefinition` owns one exact manufacturer and manufacturer-part-number identity,
one package, and an immutable reference to exactly one component content identity. It also
owns:

- the exact implementation of the component's feature bindings;
- canonical Voltage and Current records and immutable provenance;
- logical-pin to package-terminal bindings;
- package-terminal to footprint-pad bindings; and
- exact footprint, optional symbol override, 3D, and optional simulation attachments.

An exact part cannot implement a second component contract in v1. Contract compatibility
is exact content-identity equality; names, similar pinouts, versions, or feature names do
not imply compatibility. A later adapter or compatibility mechanism requires a separate
ADR with explicit, testable semantics.

### ComponentInstance

A `ComponentInstance` owns its build-local occurrence identity, immutable occurrence-of
relationship, concrete pins, reference designator, independently authored requirements or
configuration, and assembly intent. It participates in `Circuit`-owned nets and pin-to-net
membership and may additionally own one exact `LibraryPartRef`.

Intrinsic manufacturer identity, package data, electrical records, mappings, and assets
remain on the referenced `PartDefinition`; they are not copied onto the instance. An
instance requirement retained after selection remains independent design intent and is
checked against the resolved part rather than becoming an override of it.

No synthetic "unspecified" or placeholder part is a valid `LibraryPartRef`. Absence of a
selection is valid for logical exploration and is diagnosed by Board, BOM, or manufacturing
readiness when the populated occurrence needs an orderable exact part. Selecting or
replacing the reference is a closed typed `Circuit` update; it never mutates the referenced
part or copies its intrinsic truth.

Exact selected-part intent remains logical and `Circuit`-owned. Board, BOM, CPL, exporters,
and viewers consume it but do not own or reinterpret it.

## Stable Identity And Exact Selection

Portable identity and runtime identity remain separate:

- component and part definitions have readable stable keys plus immutable content digests;
- a component instance has build-local and persisted document-local identity and is not
  content-addressed;
- schema versions describe how bytes are interpreted;
- human release versions name releases but do not prove byte identity; and
- immutable digests prove the exact component, part, library snapshot, and asset content.

`LibraryPartRef` is one integrity-bearing value containing:

- library namespace;
- human library version;
- part key;
- library digest; and
- part digest.

The referenced `PartDefinition` carries its component content identity; the instance does
not copy that identity beside the reference. Resolving a `LibraryPartRef` against a supplied
immutable library snapshot must produce exactly one part whose key and digests all match.
A missing entry, digest mismatch, changed key-to-part binding, unsupported schema, or
component-contract mismatch rejects that resolution or selection operation.

A syntactically valid reference may be persisted or inspected without an available library
snapshot. Lack of a supplied snapshot is not itself proof of a forged reference. An
operation that promises a self-contained resolved closure must resolve it or fail; other
readiness and inspection paths report the unresolved state without inventing part truth.

Queries may return candidates, but persisted design state pins one exact reference. This
decision defines reference integrity only; it does not define a registry, downloader,
runtime distribution service, or package manager.

## Typed Physical Mappings

Physical correspondence is two separate typed relationships:

```text
PinKey -> [PackageTerminalKey]
PackageTerminalKey -> [FootprintPadKey]
```

Label equality may be authoring shorthand but is never persisted identity. The direct
logical-pin to footprint-pad relationship is derived and is not a third stored mapping.

The v1 mapping invariants are:

- every logical pin in an exact implementation maps to one or more package terminals;
- every package terminal either belongs to exactly one logical pin or has an explicit typed
  disposition outside the logical interface, such as NC or non-electrical;
- every package terminal represented by the selected footprint maps to one or more pads;
- every footprint pad either belongs to exactly one package terminal or has an explicit
  typed non-terminal disposition;
- one logical pin may intentionally use several package terminals;
- one package terminal may intentionally use several tied footprint lands; and
- an electrical thermal pad is mapped normally, while an NC, unused, or purely mechanical
  terminal or pad remains explicit rather than inferred from omission.

Dangling or foreign keys, missing required mappings, duplicate ownership, conflicting
bindings, and a referenced footprint whose pad keys do not match reject structurally.
The mapping does not change logical connectivity: tied terminals and lands implement one
existing logical pin and never merge distinct logical pins or nets.

## Canonical Voltage And Current Semantics

Every canonical record persists:

```text
subject
observable: Voltage | Current
meaning
typed value
normalized condition set
optional evidence references
```

Values use dimensioned quantities, ranges, tolerances, or a meaning-specific envelope.
Unknown values remain unknown. Core validation does not infer electrical meaning from a
pin name, net name, component category, feature name, Python keyword, or generic property.

A tolerance always accompanies one nominal quantity and is normalized before key merge
and hashing. For `Characteristic`, nominal plus absolute or percent tolerance becomes one
min/typical/max envelope. For `AcceptedRange`, `ProvidedRange`, or `AbsoluteLimit`, it
becomes a closed range. `Requirement` and `Capability` use direct non-negative bounds and
do not accept tolerance values. A missing nominal, incompatible dimension, or tolerance
that cannot produce an ordered range rejects structurally.

### Subjects, Frames, And Direction

V1 admits three subject shapes:

1. A framed pin contains exactly one `pin` and one distinct `reference` `PinKey` from the
   same component contract. Voltage is `V(pin) - V(reference)`. Current uses the passive
   sign convention: positive Current enters the component at `pin` and leaves at
   `reference`.
2. A directed relation used by a v1 Voltage or Current record contains exactly one
   positive/from and one distinct negative/to `PinKey`. Voltage is positive-pin potential
   relative to negative-pin potential. Current is the total signed current from the
   positive/from pin to the negative/to pin.
3. A supply domain declares non-empty, disjoint positive-pin and return-pin sets. Voltage
   is the positive set relative to the return set. General signed Current uses the passive
   convention: positive Current enters the component through the positive set and leaves
   through the return set. A continuous Current `Requirement` is non-negative demand in
   that direction; a continuous Current `Capability` is a non-negative magnitude available
   in the opposite delivery direction through the positive set with its return through the
   return set.

Each role has explicit cardinality in its feature schema: framed pins and v1 electrical
relations use `exactly_one` roles, while supply-domain sides use `one_or_more`. A binding
must satisfy that cardinality. Repeated pins within a role, overlap between opposing roles,
bare pin voltage, unsigned relation current, and undeclared supply domains are structurally
invalid rather than inputs for ERC to guess about.

All three subjects admit both canonical observables with the sign conventions above.
`Characteristic`, `AcceptedRange`, `ProvidedRange`, and `AbsoluteLimit` may be used with
Voltage or Current on any valid subject when required by its feature schema.
`Requirement` and `Capability` are admitted in v1 only as continuous Current on a supply
domain, where their source-versus-load interpretation is explicit.

### Meanings

The canonical v1 meanings are:

- `Characteristic`: described behavior, such as an LED forward-voltage envelope under a
  stated current condition. It is not an accepted operating range or source guarantee.
- `AcceptedRange`: the operating range a consumer or terminal contract accepts.
- `ProvidedRange`: the operating range a source guarantees.
- `AbsoluteLimit`: a boundary that must not be exceeded. It never widens an accepted
  range or substitutes for a provided range.
- `Requirement`: a minimum service the design or consumer requires. In v1 the merge and
  aggregation rule below is defined for continuous Current source-capacity requirements.
- `Capability`: a guaranteed service the exact part can provide. In v1 the merge and
  aggregation rule below is defined for continuous Current source capability.

ERC compares active canonical records, not feature classes. A provided Voltage range must
fit within every applicable accepted Voltage range and remain within applicable absolute
limits. An applicable continuous Current capability must meet the applicable aggregated
continuous Current requirement.

## Conditions, Cardinality, Merge, And Conflict

A record's semantic key is:

```text
(subject, observable, meaning, normalized condition set)
```

Evidence is not part of that key. Conditions are an order-independent typed set of
Voltage or Current equality/range predicates over valid subjects. A value or predicate may
be a literal dimensioned quantity or one bounded, dimension-preserving
`referenced value * dimensionless scalar` expression. References must resolve, remain
acyclic, and use the same observable dimension. Offsets, arbitrary formulas, multi-step
expression graphs, callbacks, and solver-produced values are not valid canonical inputs.

An absent condition set means unconditional. Identical normalized conditions identify the
same semantic key. Distinct condition sets coexist; there is no "most specific wins" or
last-writer-wins rule. At a resolved operating point, every matching record participates.
If the operating condition is unknown, validation uses the conservative rules below or
reports the result as unknown rather than selecting a convenient record.

For each semantic key, compatible source records normalize to at most one effective value:

| Meaning | Permitted effective value | Compatible merge | Conflict |
| --- | --- | --- | --- |
| `Characteristic` | scalar or one min/typical/max envelope | exact equality only; combine evidence | differing values |
| `AcceptedRange` | Voltage or Current range | intersection | empty intersection |
| `ProvidedRange` | Voltage or Current range | intersection of guarantees | empty intersection |
| `AbsoluteLimit` | one- or two-sided Voltage or Current range | intersection to the strictest limit | empty intersection |
| `Requirement` | non-negative continuous Current lower bound | maximum requirement for one subject | incompatible units, qualifiers, or direction |
| `Capability` | non-negative continuous Current upper bound | minimum guaranteed capability for one subject | incompatible units, qualifiers, or direction |

`Unknown` is a typed value state, not a seventh meaning. It preserves a record's subject,
observable, meaning, conditions, and provenance but contributes no usable range, bound, or
guarantee. It satisfies structural presence of a schema-required record, while emitting an
incomplete-data diagnostic and blocking publication or readiness when a known value is
required. An active Unknown can never certify compatibility or count as zero capability or
zero demand; known records may still prove a definite failure.

Exact duplicate records canonicalize once with their evidence references combined.
Compatible ranges and bounds merge as shown. Multiple instance records for the same
occurrence and domain merge before circuit-level aggregation, so alternate authoring paths
cannot double-count the same requirement.

Condition keys control applicability, not precedence. After conditions are evaluated, all
active records with the same `(subject, observable, meaning)` merge again by the same table:
active ranges and limits intersect, active requirements take the maximum total requirement
for that subject, active capabilities take the minimum guarantee, and differing active
characteristics conflict. Unconditional records participate in every operating point.
When conditions cannot be fully resolved, validation may certify a result only from the
conservative envelope of all records that are not known false; otherwise it reports
unknown.

A well-formed but contradictory set of device claims remains loadable and inspectable. It
emits a conflict diagnostic, has no effective value for that key, and blocks publication
when the record is required by the component contract or official catalogue policy. It is
never resolved by declaration order. A malformed record, invalid unit, impossible range,
dangling or cyclic expression, duplicate singular feature binding, or missing
schema-required record is structurally invalid and rejects at build or load.

## Compatible Continuous-Current Aggregation

V1 aggregates only continuous `Current` `Requirement` records after instance subjects
have been projected through their `PinKey`s onto one resolved Circuit supply domain. A
domain resolves only when its positive pins project to one Circuit net, its return pins
project to one distinct Circuit net, and source and consumer domains identify the same
ordered net pair. Split, missing, or shorted domain projection is structurally valid
connectivity but produces a design diagnostic and an unknown budget.

The aggregation rule is:

1. Normalize compatible records for each component instance and supply domain.
2. At a known operating point, discard records whose conditions are false and merge every
   active continuous requirement for one occurrence/domain by maximum. At an unresolved
   operating point, use the maximum over every requirement not known false. A requirement
   record is the total source capacity required by that occurrence/domain under its
   conditions, not an additive sub-feature contribution.
3. Count that merged requirement once per instance/domain, not once per logical pin,
   package terminal, footprint pad, condition record, or feature helper.
4. Sum the resulting non-negative requirements across component instances on the same
   resolved ordered supply domain.
5. For one source instance at a known operating point, merge every active compatible
   continuous `Current` `Capability` by minimum. At an unresolved operating point, at
   least one unconditional capability is required to certify a guarantee; merge that
   baseline with every conditional capability not known false by minimum. A sole
   condition-dependent capability cannot certify an unresolved case.
6. Emit an insufficient-capability diagnostic when the guarantee is below the sum and an
   unknown-budget diagnostic when no resolved compatible guarantee can be established.

Requirements merge with requirements in the passive demand direction, and capabilities
merge with capabilities in the opposite delivery direction. Comparing the two requires the
same resolved ordered domain, Current dimension, and continuous qualifier, with those
complementary meaning-specific directions. Peak, pulse, duty-cycle, startup,
operating-mode, mutually exclusive, probabilistic, or unbounded load records do not enter
this sum until their semantics are separately accepted; their presence prevents a complete
current-budget claim. Requirements from different resolved domains are never combined.
Capabilities from different source instances are never summed without a separately
accepted current-sharing contract.

## Persistence, Hashing, And Versioning

Canonical persistence must round-trip definitions, exact relationships, records,
conditions, mappings, and references without executing authoring Python. Units and key
ordering are normalized so authoring spelling and map order do not change content identity.
Ordered `PinDefinition` sequence and ordered relation roles remain semantic. Unordered
domain members, mapping targets, condition predicates, and named record collections are
sorted by stable typed key after duplicate rejection. Evidence references are sorted and
deduplicated. Evidence included in a digest is itself an immutable content-addressed
reference.

The component content digest covers its semantic model version, stable key, ordered
nested pin contracts, named framed pins/relations/domains, feature bindings, required
canonical record shapes, instance-input schema, default symbol content reference, and
immutable provenance.

The part content digest covers its semantic model version, stable key, manufacturer,
manufacturer part number, package, implemented component digest, feature implementations,
canonical records and conditions, evidence references, typed physical mappings, exact
projection and asset content references, and immutable provenance and licence data.

The library digest carried by `LibraryPartRef` is opaque but integrity-bearing at this
gate. Its manifest/archive construction belongs to later library-packaging work. An exact
asset reference included by a part uses the asset bytes' digest, not a source path or
metadata-only surrogate.

Mutable sourcing offers, distributor stock and prices, timestamps, machine-local paths,
component occurrence state, instance requirements, assembly intent, analysis results, and
post-publication verification attestations are excluded from component and part semantic
hashes. Attestations key to immutable digests instead of changing the identity they attest.

Any change to intrinsic semantic or physical truth creates a new content digest. Human
semantic versions communicate release intent but never authorize compatibility inference;
v1 selection still requires the exact component identity implemented by the exact part.
Build-local and document-local IDs are not portable content identities.

Encoding-format and semantic-model versions are distinct. The semantic-model version is a
component/part digest input, so a change of meaning creates a new semantic identity. A
lossless re-encoding may preserve that semantic identity while the serialized-byte or
bundle digest changes. Readers reject unsupported versions before constructing partial
state and do not silently reinterpret changed meanings. Any old-format migration must be
explicit, deterministic, and tested before a writer emits the successor version.

This ADR does not choose `volt.part` v5 or another successor and does not migrate the
current v4 artifact. The implementation slice must make that version decision explicitly,
retain a tested v4 read/conversion path while required, and write only the selected current
canonical format.

## Error And Diagnostic Contract

The following are structural boundary failures:

- missing, duplicate, dangling, or foreign stable identities;
- malformed subject cardinality, frames, direction, units, values, or conditions;
- unsupported semantic schema versions or digest mismatches;
- a part with a missing or non-exact component contract;
- a selected part whose implemented contract differs from the instance definition;
- missing schema-required canonical records;
- invalid logical-pin/package-terminal/footprint-pad mappings; and
- a forged or mismatched non-empty `LibraryPartRef` when resolution is required.

These reject definition build, selection, resolution, or an artifact load that claims a
self-contained closure without leaving partial kernel state.

The following are design or publication diagnostics over structurally valid data:

- a source's provided Voltage is outside a consumer's accepted or absolute range;
- continuous Current capability is below a compatible requirement sum;
- an independently authored instance requirement exceeds selected-part capability;
- a populated occurrence lacks an exact part at manufacturing-readiness validation;
- optional evidence or catalogue-completeness data is absent;
- multiple well-formed claims conflict; or
- operating conditions, domain projection, external resolution, or source-sharing
  semantics are insufficient to reach a result.

An official catalogue may promote specified diagnostics to blocking publication gates.
That policy does not make a structurally valid part artifact unloadable.

## Examples

The values below demonstrate semantics and are not verified claims about named production
parts.

### Regulator Or Source To MCU

An MCU component contract declares a `main_supply` domain with positive pin `3V3` and
return pin `GND`. Its exact part records:

```text
Voltage / AcceptedRange / main_supply = 3.0 V .. 3.6 V
Voltage / AbsoluteLimit / main_supply = -0.3 V .. 3.6 V
Current / Requirement / main_supply = continuous 0.50 A
```

A regulator or other source declares an output domain projected onto the same Circuit
supply domain. Its exact part records:

```text
Voltage / ProvidedRange / output = 3.2 V .. 3.4 V
Current / Capability / output = continuous 0.60 A
```

The Voltage check passes because the provided range is contained by the accepted range.
If the MCU and another compatible continuous load require `0.50 A + 0.05 A`, the current
check passes. Adding another compatible `0.10 A` requirement produces `0.65 A` and an
insufficient-capability diagnostic; neither circuit is structurally rejected. A 3.7 V
provided range similarly produces a Voltage diagnostic rather than a load failure.

### LED

An LED `ComponentDefinition` contains `A` and `K` pins and a directed `junction` relation
from `A` to `K`. One exact LED part may record:

```text
Voltage / Characteristic / junction = min 1.6 V, typical 2.0 V, max 2.4 V
  when Current / junction = +20 mA
Current / AbsoluteLimit / junction = upper +25 mA
Voltage / AbsoluteLimit / junction = lower -5 V
```

Positive Voltage and Current use the `A -> K` orientation. The reverse Voltage limit is
therefore negative in that frame; reversing the circuit does not silently reinterpret a
forward record.

The exact physical bindings remain separate:

```text
A -> package terminal 2 -> footprint pad 2
K -> package terminal 1 -> footprint pad 1
```

A missing terminal or pad rejects the part definition. An operating point above `+25 mA`
or below `-5 V` is a diagnostic over an otherwise structurally valid design.

## Relationship To Issue #237

Issue [#237](https://github.com/RyanWillie/Volt/issues/237) remains separate implementation
work for truthful selected-part identity, supported-field preservation, readiness,
serialization, bindings, validation, and deterministic fixtures. This ADR does not absorb
that product work.

Its current acceptance wording asks representative Voltage, Current, and Power ratings to
round-trip and affect validation. D1 accepts canonical Voltage and Current only. Existing
typed Power values must not be silently discarded: an implementation must preserve a
supported value or reject it explicitly. This ADR defines no canonical Power subject,
meaning, merge, aggregation, or ERC policy.

After Gate 1, #237 must be narrowed or split before it becomes ready: its canonical
acceptance should target Voltage and Current, preservation or explicit rejection should
cover existing typed Power values, and canonical Power semantics should move to a
separately admitted follow-up with the required native blockers.

## Consequences

- Component contracts and exact parts are authored once and selected without duplicating
  intrinsic truth.
- Stable typed seams replace ambiguous pin/package/pad number reuse.
- Standard and custom features share one canonical Voltage/Current language.
- Conflicts remain inspectable and diagnosable without allowing malformed structure.
- Current budgeting is useful for the initial source-to-load case without guessing at
  unsupported load or source-sharing semantics.
- Content identity is deterministic and independent of mutable sourcing and occurrence
  state.
- Existing APIs and artifacts require a later deliberate migration; this ADR changes no
  product code or compatibility surface by itself.

## V1 Exclusions

- Product code, bindings, packaging, CLI work, or migration of existing parts.
- Registry, downloader, runtime distribution, or package-manager machinery.
- Arbitrary formulas, Python callbacks, generic facts bags, and inferred meaning from
  names or categories.
- More than one component contract per exact part, implicit contract compatibility, or
  contract adapters.
- Canonical Power, Resistance, Inductance, Capacitance, Time/Frequency, Temperature, or
  optical semantics.
- Peak, duty-cycle, operating-mode, mutually exclusive-load, or multi-source current
  aggregation.
- A simulation engine or simulation-backend API.
- Downstream P1-P5, Y3, or dependent packaging work.

## Revisit Trigger

Revisit this decision only when a concrete exact part cannot be represented by one
component contract, a required Voltage/Current case cannot be expressed by the accepted
subjects and conditions, or measured implementation evidence shows that exact identity
or compatible continuous-current aggregation is insufficient. A revisit must preserve
kernel ownership, deterministic persistence, structural rejection at mutation/load
boundaries, and diagnostic handling of bad design intent unless a new ADR explicitly
replaces those principles.
