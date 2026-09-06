# ADR: Minimal Part Electrical Model and Read-Only Compilation

Status: accepted via [PR #369](https://github.com/RyanWillie/Volt/pull/369), merged
2026-08-30. Amended 2026-09-06 following the maintainer-approved roadmap review.

Program: [#250](https://github.com/RyanWillie/Volt/issues/250). The
[single-page companion](part-electrical-model-contract.html) explains this normative contract.
E1 native models and E2 persistence are implemented; E3 authoring is delivered through
[PR #372](https://github.com/RyanWillie/Volt/pull/372). S1–S3 and later execution capabilities
remain planned. Their illustrative signatures below are not current callable APIs.

### Dated amendment — 2026-09-06

- E3 uses one reusable native component authoring value and one generic typed element
  insertion operation. Component fields are authored once and shared by the model builder
  and implementing Part; a provisional Part or public dictionary-taking model constructor
  is not the authoring boundary.
- S1 owns request-local structural validation, exact-input binding and participation/coverage.
  S2 owns model expansion, hierarchical continuity, resolved topology and compilation findings.
  S3 owns numerical formulation, rank/solvability, conditioning, residuals and truthful outcomes.
  Shared native preparation must not become competing net-resolution or readiness engines.
- S2 preserves a small typed resolved graph with laws, parameters and origin mappings.
  Backends may eliminate numerical coordinates only while preserving or reconstructing
  observations, signs and provenance; canonical branches do not prescribe matrix unknowns.
- S3 includes an early analytical and pinned-SPICE reference corpus. S5 reuses this proof
  for the full public adapter; no separate setup issue or current adapter is implied.
- Linear AC and linear transient are independently refinable after the shared S4 execution
  contracts. AC before transient is a scheduling preference, not a technical dependency.
  Nonlinear extensions remain later explicit refinement under #366.

This amendment changes authoring guidance and future service responsibilities, not the
implemented native model, value domains, identity, Circuit boundary or artifact formats.

## Decision and boundary

An exact immutable `PartDefinition` may own one immutable `PartElectricalModel`, composed from
typed resistance, capacitance and inductance elements. A `ComponentDefinition` supplies the
logical `PinKey` contract; a `ComponentInstance` supplies occurrence identity, exact selected
`LibraryPartRef`, and Circuit-owned connectivity. No specialised product hierarchy or new
`Circuit` method is needed.

Five concepts remain distinct:

| Concept | Owner and meaning |
| --- | --- |
| Quantity | Existing `UnitDimension`, `Quantity`, units, `Tolerance`, `QuantityRange` |
| Element | An oriented ideal R, C or L law with a nominal parameter |
| Exact Part | Model, physical identity, mappings, canonical V/I records and evidence |
| Circuit instance | Selection and logical topology; no intrinsic parameter override |
| Testbench | Explicit excitation, reference, participation and probes for one analysis |

C++ owns construction validation, canonicalization, identity, persistence, readiness and
compilation. Python only lowers ergonomic authoring to these native objects. Numerical
execution is a later explicit consumer. Normal check/build/manufacturing never invokes a
solver, and neither compilation nor solving modifies Part/Circuit truth.

The first implementation milestone ends at E3: define, select, save, reopen without source,
and inspect correct Part models. It requires no solver, Board, Schematic, manufacturing
readiness, catalogue, or changes to other examples.

## 1. Minimal native model

### Quantities and parameters

Reuse `Quantity` with its existing dimensions and SI values: resistance in ohms,
capacitance in farads, inductance in henries, frequency in hertz, time in seconds.
Dimension checks occur in typed element/analysis constructors. Small native functions such
as `ohms(value)`, `farads(value)`, `henries(value)`, `hertz(value)` and `seconds(value)` may
return existing SI `Quantity` values; no second units library, numeric parser, or dimension hierarchy
is introduced. Python bare numbers are not accepted as element parameters.

Each element owns exactly one parameter: a known nominal `Quantity`, optional existing
`Tolerance`, and zero or more immutable evidence content references. The element variant
fixes the required dimension. No generic parameter registry, cross-element expression,
reference to a V/I record, or unknown numeric sentinel is accepted. Unknown behaviour is
represented by absence of the optional model.

Absolute and percent tolerances may be asymmetric. Normalize either to absolute minus/plus
deviations in the nominal dimension, retaining the nominal; use `QuantityRange` to derive
the inclusive bounds. Percent input uses a ratio (`0.01` means 1%). No tolerance means
**unspecified uncertainty**, not zero uncertainty; explicit zero tolerance remains distinct.
The nominal, deviations and derived bounds must be finite. Deviations must be nonnegative;
only the nominal and derived bounds must lie in the element's permitted domain. Zero or
one-sided zero deviations are therefore valid for positive C/L. Derived bounds are not a
second serialized value. Tolerance/evidence describe the
parameter; nominal compilation does not claim corner, worst-case, or statistical analysis.

| Element | Law for `v = V(from) - V(to)`, `i` from `from` to `to` | Allowed parameter and tolerance bounds |
| --- | --- | --- |
| `ResistanceElement` | `v - R i = 0` | `R >= 0`; zero is the explicit ideal voltage constraint `v = 0` |
| `CapacitanceElement` | `i - C dv/dt = 0` | `C > 0`; zero rejects, omit an unwanted capacitance |
| `InductanceElement` | `v - L di/dt = 0` | `L > 0`; zero rejects, omit an unwanted inductance |

Negative values, NaN, infinity, overflow during unit/tolerance normalization, wrong
dimensions and bounds outside this table reject. Normalize negative zero to positive zero.
There is no epsilon clamp or substitution of a very large/small value. Ideal C/L do not
require ESR, leakage or winding resistance. Omitting a series parasitic means authoring the
remaining element at the intended endpoints, not retaining a dangling intermediate node.
Frequency and Time are quantity/analysis coordinates, never two-terminal elements. Future
frequency/time-dependent parameters and initial conditions are outside this contract.

### Terminals, private nodes and composition

The native vocabulary is closed:

```text
ModelTerminalKey, ModelInternalNodeKey, ModelElementKey    distinct stable key types
ModelTerminal { key, PinKey }
ModelInternalNode { key }
ModelEndpoint = ModelTerminalKey | ModelInternalNodeKey
Element = ResistanceElement | CapacitanceElement | InductanceElement
Element { key, from, to, nominal_parameter }
PartElectricalModel { implemented_component_digest, terminals, internal_nodes, elements }
```

An authoring builder is created against one immutable `ComponentDefinition`. It returns
typed, builder-owned terminal/internal-node handles; a handle from another builder rejects
even when its local spelling matches. Finalization resolves them into the closed portable
key types above and produces immutable value data. Copying a finalized model is safe;
mutating a builder later cannot mutate an already finalized model or Part.

Construction/finalization and native readers enforce:

- non-empty unique keys within each typed collection; no duplicate elements are merged;
- exactly one model terminal for each contract `PinKey`, no foreign or missing pins and no
  duplicate pin binding; model terminal names do not imply pin identity;
- every endpoint resolves to this model and the two endpoints of an element are distinct;
- at least one element, and each declared terminal/internal node is used by an element;
- the finalized model's component digest equals the Part's implemented component digest.

Full terminal coverage prevents a partially described IC from posing as a complete passive
model. This first vocabulary cannot express an intentionally unused logical contract pin;
such a Part remains model-absent until a separately accepted vocabulary can represent it.
Physical NC/non-electrical package terminals remain in the existing physical disposition
contract and are not model terminals. Disconnected but used passive subnetworks are valid
data; analysis can diagnose disconnected or floating islands.

Composition means adding elements over these shared local handles, including several
elements incident on one terminal. There are no nested submodel calls, external global
nodes, built-in ground nodes, implicit pin-name joins, arbitrary callbacks, or SPICE strings.
Internal nodes are inaccessible to Circuit topology and ordinary authoring connectivity.
An analysis may inspect their derived identities, but cannot connect a source to them in S1.

Reversing an element's endpoints reverses the reported voltage/current convention. It is a
semantic change even for a reciprocal passive law: hashes preserve orientation and element
keys for stable observations. A multi-terminal pin current is the signed sum of the
incident element currents, not an invented two-terminal current for the whole Part.

## 2. Laws, claims and existing authoring values

The existing canonical `ElectricalRecordSet` remains Voltage/Current-only. Its subjects,
conditions, requirements, characteristics, accepted/provided ranges, absolute limits,
capabilities, merge/conflict rules and ERC guarantees remain unchanged. R/C/L parameters
are not new observables in those records and do not participate in their merge rules.

A model parameter defines an ideal law. A voltage absolute limit remains an existing V/I
record over the physical contract terminals. Neither a limit nor a ProvidedRange supplies
an equation or ideal source. A capacitor voltage rating does not clamp its model, and a
regulator's provided voltage does not supply its missing active behaviour. Evidence may
support both claims through shared immutable references; values are not copied between them.
Internal-element physical ratings, canonical Power/Temperature, derating, validity regions,
operating-mode selection and nonlinear laws are not introduced here.

Existing instance `Resistance`, `Capacitance`, `Inductance`, `NominalValue` and `Tolerance`
attributes retain their current instance-owned meaning; this ADR does not relabel every
old attribute as a formal requirement. They never seed or override an
exact Part model. Existing string `value`, MPN, category and generic properties remain
display/metadata, not equations. No reader infers a model from these fields, and no new
exact-Part `resistance`/`capacitance`/`inductance` scalar is stored beside the model parameter.

The E1/E3 source migration must classify each overlapping caller explicitly: move a value
intended as intrinsic simulated truth into the one model parameter; retain independent
requirements as intent; retain text as text. Two authoring inputs both claiming the same
intrinsic parameter reject even if equal; no precedence or silent promotion. Arbitrary
property text is not parsed to discover a conflict. Comparing instance design intent with
model values is a separate diagnostic capability, not an implicit parameter substitution
or a promised new ERC rule in this gate. Existing supported Power/rating data must continue
to be preserved or explicitly rejected by its current owning boundary, never dropped.

Missing evidence does not invalidate a nominal model. Supplied references must be well
formed and resolved when the operation promises closure. A tolerance-free or illustrative
model remains explicitly limited evidence, not a verified manufacturer guarantee.

## 3. Exact identity and current-only persistence

Component identity is unchanged by attaching a model to an implementing Part. Part semantic
identity includes a new semantic-model version and the explicit optional-model state.
When present it covers the implemented component digest, all typed keys and terminal
bindings, internal nodes, element discriminants, ordered endpoints, normalized nominal
parameters/tolerances, and sorted/deduplicated immutable evidence references. Changing any
of those changes Part identity. Absent model and present model are never equivalent.

Terminal, internal-node and element collections canonicalize by stable typed key after
duplicate rejection. Declaration order is nonsemantic; local key spelling and endpoint
order are semantic. Unit aliases producing the same normalized finite SI value are equal;
this is exact canonical-value equality, not approximate physical equality. C++ and Python
must use the same native unit normalization and float serialization. No unit spelling,
builder token, insertion order, host path, timestamp, solver result or runtime ID enters
the digest. Percent/absolute tolerances with exactly equal normalized deviations canonicalize
identically; mathematically equal spellings that round to different finite doubles are not
silently approximately merged. Absent and
explicit zero tolerance do not. Evidence content is immutable and hash-addressed; changing
evidence changes the Part identity that makes that claim.

`LibraryPartRef.part_digest` pins this semantic Part identity. The serialized Part-byte
digest and library/bundle digest remain distinct enclosing integrity checks, and all
affected library identities/selected references are rebuilt. No per-instance copy of a
model is serialized; several selected instances resolve the same immutable Part.

The E2 contract is:

1. Serialize the complete optional model inline in the exact Part artifact. In the new
   current Part schema, `electrical_model` is a mandatory field: `null` means absent and an
   object means present; a missing field rejects. Persist its
   normalized quantities, tolerance state and evidence references; do not serialize derived
   bounds, builder tokens, compiled equations or backend text.
2. PartLibraryBundle dependencies include the implemented component and every immutable
   evidence asset referenced by the model, alongside existing dependencies. Verify bytes,
   hashes and exact edges on build and open. Shared evidence is stored once per existing
   origin-bearing artifact identity.
3. ProjectBundle's selected-Part closure must likewise include the inline model and every
   referenced model evidence asset, even in a logical-only project with no Board. Apply the
   same rule to existing canonical V/I evidence referenced by that selected Part; carrying
   half its claims is not a self-contained Part. This is the precise extension to the
   artifact-graph ADR's currently narrower component/footprint/symbol/consumed-GLB closure.
   It does not pull unrelated catalogue documents, STEP or unused GLB into the graph.
4. Native reopening resolves the exact selected Part and evidence solely from the verified
   vendored closure. No Python source/import, source library, ambient installed catalogue,
   network, generated default model or cache is required or permitted.
5. Reject unknown variants, missing mandatory fields, duplicate/dangling keys, wrong units,
   invalid values, mismatched hashes/relationships and missing required closure members
   before publishing partial state. A recognized Part with an absent model stays absent.

At the original E0 baseline `volt.part` was v5, and PartLibraryBundle/ProjectBundle were v2.
E2 now implements Part v6, ProjectBundle v3 and unchanged PartLibraryBundle v2.
E1 updates semantic identity immediately and explicitly rejects every write path unable to
preserve a model-bearing Part until E2 supplies transport. E2 advances affected wire/schema
and semantic versions, writers, readers, bindings, fixture producers and goldens atomically.
The implementation must name the changed format versions; it must not bump unrelated
formats mechanically. Only the new current contract remains readable/writable; regenerate
old artifacts from current source. No converters, compatibility readers or silent unknown
field dropping. E0 itself changes no format number or current support claim.

## 4. Independent native testbench and readiness

S1 introduces one immutable typed `DcRequest`, separate from Circuit and Part, containing:

- the exact logical input identity and explicit reference `NetId`;
- uniquely keyed independent DC voltage/current sources over ordered pairs of logical nets,
  with finite Voltage/Current quantities;
- voltage probes over ordered net pairs and current probes over an instance/model element
  or source identity with its stored orientation;
- explicit per-occurrence exclusions with typed reason: `NonElectrical`,
  `OutsideAnalysis`, or `ReplacedByStimulus` (identifying its replacement source keys).

Persist document-local references plus exact logical/selected-model identity, never raw
addresses or display labels. Runtime handles are resolved against that exact input on load.
A foreign/dangling reference, wrong source dimension, duplicate key, incompatible probe,
invalid replacement-source reference or stale exact identity rejects native request binding.
No reference supplied is an incomplete request diagnostic; a supplied foreign reference is
a structural error. Sources use two distinct authored NetIds; nets may subsequently be
electrically equivalent through existing hierarchy, which is a readiness concern.

All Circuit occurrences participate by default, including unplaced and DNP occurrences:
assembly intent is not an analysis inclusion policy. An explicit exclusion applies only to
that request, records a reason, and never edits Circuit selection/topology. It removes the
occurrence's whole electrical contribution, never silently ties or removes nets. Part of a
multi-terminal model cannot be selectively discarded in S1. `NonElectrical` is an explicit
author assertion, not a category/absence inference; it cannot certify missing behaviour as
supported. Completeness is only relative to the explicitly scoped request, never an assertion
that every device in the physical Circuit was simulated. Results must report the modeled
boundary and every exclusion. A replaced supply
must be explicitly excluded and driven at named logical nets; merely adding a source across
a selected physical supply leaves that supply required and its absent model blocking.

Voltage source orientation is `V(from)-V(to)=value`; its reported positive current runs
from `from` to `to`, so delivering power normally gives negative current. Current source
orientation prescribes positive current from `from` to `to`. Zero and negative source
values are finite valid excitation. No source is inferred from Ground/Power net kind, a
net name, accepted/provided range, current capability or an ordinary project build.

Readiness is analysis-specific and reports every occurrence as supported, excluded,
unselected, unresolved, model-absent or unsupported for the requested consumer. It must not
use absence as a zero/open circuit or declare an incomplete coverage solve complete.
Malformed recognized artifacts reject load; a valid current model that a particular
consumer cannot lower is `unsupported`, not an excuse to skip its elements. Relevant
existing V/I diagnostics remain available without turning all ERC/manufacturing errors into
a universal simulation gate.

## 5. Read-only compilation preserving equation meaning

S2 provides one free native operation, schematically:

```text
compile_electrical(const Circuit&, const PartDefinitionResolver&, const DcRequest&)
    -> CompileReport { coverage, diagnostics, optional immutable CompiledElectricalModel }
```

The resolver is the existing explicit exact-Part resolution boundary, including the
verified offline closure. This operation is not a new `Circuit` method or hidden storage
facade. The output is available only for complete required coverage; incomplete/failed
reports retain useful provenance/diagnostics but never expose a partial model as runnable.
Complete compilation means faithful equation construction, not proof of numerical solvability.

Terminal binding follows model `PinKey` to the exact instance's corresponding contract pin,
then Circuit pin-to-net membership. Electrical continuity also includes existing hierarchical
`PortBinding` relationships between internal and parent nets. Derive one compiled node per
continuity group, retain all member document NetIds and binding origins, and do not mutate
or merge the Circuit nets. Labels, package terminals, pads, placement and schematic wires
play no role. An unconnected pin gets a unique occurrence/pin node and an explicit readiness
diagnostic; unrelated unconnected pins never alias. Required unresolved connections block
readiness; an intentionally open optional pin can compile and still expose floating algebra.

Each internal node is keyed by `(logical input identity, occurrence id, model node key)`.
Each element is keyed by `(occurrence id, exact Part digest, model element key)` under that
input identity. Reusing one Part twice must yield disjoint internal nodes and branches while
sharing only Circuit-connected external nets. An element's distinct model endpoints may
map to the same electrical node; that is valid Circuit design, not corrupt model data.

The minimal immutable compiled representation is a typed resolved graph with closed laws
and provenance. Conservation is derived from its connectivity; authors do not maintain a
second equation copy. It is not a matrix or a prescribed numerical formulation:

| Record | Required content |
| --- | --- |
| Node | Stable compiled identity, logical-net members or occurrence-local origin |
| Branch | Stable element/source origin, ordered node endpoints, oriented current observation |
| R/C/L law | Exact variant and nominal SI parameter; references to resolved branches/nodes |
| Source law | Independent Voltage/Current constraint and testbench source origin |
| Conservation | Signed branch incidence and KCL at each node; reference-potential constraint |
| Storage | Capacitor voltage/charge `q=Cv`; inductor current/flux `phi=Li`, with derivative law |
| Provenance | Input identities, exact selection, model/element keys, uncertainty/evidence and coverage |

Node potentials and branch currents define observable quantities, not mandatory independent
matrix coordinates. S3 or another backend may derive an efficient formulation and eliminate
coordinates only with a deterministic reconstruction to requested and mandatory observations,
including their orientation and origin. For example, a resistor current may be reconstructed
from its law; a DC capacitor current remains reportable as zero. Elimination must not erase
an ideal branch current or arbitrarily choose a nonunique current. Charge/flux may be derived
storage coordinates; implementations need not introduce redundant independent unknowns. For every
branch, KCL receives `+i` at `from`, `-i` at `to`; coincident endpoints cancel in incidence
but the branch and its law/provenance remain. Reference potential fixes the gauge at the
explicit reference continuity group. Redundant KCL equations may be identified for
execution, but their conservation meaning must remain inspectable. No arbitrary expression
AST, user matrix DSL, callback, discretization, backend option or numerical state belongs
in this canonical output.

In DC steady state, consumers set storage derivatives to zero: ideal C imposes `i=0`, ideal
L imposes `v=0`. They must retain the original positive C/L parameters, branches and storage
laws in the compiled model. A capacitor is not globally deleted, an inductor is not globally
replaced by a merged Circuit net, and `R=0` is not evaluated as `1/R`. No conductance shunts
or epsilon resistors are inserted to make a singular design solvable. Native and future
SPICE consumers derive from these same records, with origin-preserving lowering or explicit
unsupported status.

Floating islands, inconsistent ideal constraints, nonunique currents and relevant open or
shorted connectivity are design/analysis findings, not mutation errors. S1 validates request-local references and coverage without a second topology engine. S2
owns the stated concrete resolved-topology/source findings without promising a complete rank
test. S3 owns matrix solvability, conditioning, residuals and truthful singular/inconsistent
outcomes:

- an isolated resistor pair has an unfixed voltage gauge;
- a capacitor-only DC path cannot provide a resistive reference path;
- two parallel zero-ohm/ideal-inductor branches may have nonunique branch currents;
- unequal ideal voltage sources on the same node pair are contradictory;
- a nonzero voltage source whose nets map to one continuity node is contradictory;
- zero voltage across a short is consistent but may leave source current nonunique.

Compilation identity covers the canonical logical input, exact selected closure, request,
compiler-contract version and deterministic output ordering, not host paths/timestamps or
solver state. Inspection/serialization snapshots retain source identity and storage; any
consumer's numerical results are separate objects keyed to that identity. Board, Schematic,
Circuit and Part hashes/bytes remain unchanged by compile/solve.

## 6. Native and Python authoring

The [complete C++ and Python examples](https://github.com/RyanWillie/Volt/tree/main/samples/electrical_part_models)
exercise authoring, exact selection, connectivity, save, source-free reopen and inspection.
Values are illustrative, not manufacturer guarantees. C++ uses the native typed builder:

```cpp
PartElectricalModelBuilder builder{component};
const auto a = builder.terminal(ModelTerminalKey{"a"}, PinKey{"A"});
const auto b = builder.terminal(ModelTerminalKey{"b"}, PinKey{"B"});
builder.add<ResistanceElement>(ModelElementKey{"body"}, a, b,
    ModelParameter{Quantity{UnitDimension::Resistance, 330.0},
                   Tolerance::percent(0.01), {}});
const PartElectricalModel model = builder.build();
```

Python declares component fields once. `PartComponentDefinition` owns the complete native input
and normalized definition; `PartElectricalModelBuilder` accepts that typed value directly:

```python
component = library.component(
    "R330", pins=(volt.PinSpec("A", 1), volt.PinSpec("B", 2)),
    contract=volt.ComponentContract("example/R330@1", ("A", "B")),
)
builder = volt.PartElectricalModelBuilder(component)
a = builder.terminal("a", "A")
b = builder.terminal("b", "B")
builder.add(volt.ResistanceElement, "body", a, b,
    volt.ModelParameter(volt.ohms(330), volt.Tolerance.percent(0.01)))
part = library.part("R330", component=component,
    electrical_model=builder.build(), **physical_fields)
```

`physical_fields` denotes the footprint, mappings and exact manufacturer identity shown in
the [runnable API example](../python-api.md#part-electrical-models). It contains no repeated
component declaration. Component properties, including display value, are declared on the
component. One component may be reused for multiple exact Parts and model builders.
The single `add` accepts exactly the native `ResistanceElement`, `CapacitanceElement` or
`InductanceElement` class and builder-owned endpoint handles. Portable element values are
inspection/transport values; they do not bypass handle ownership during insertion.

For a composite capacitor the same operation adds R from `a` to private `after_esr`, L from
`after_esr` to private `after_esl`, then C from `after_esl` to `b`. This is one physical Part,
not three Circuit components. Native Part construction checks the implemented component
digest, even when foreign components have the same pin display names.

String-to-typed-key conversion is syntax only. Quantities, laws, normalization, validation,
identity and persistence remain native. Existing `Design.instantiate` and `Net.connect`
select exact Parts and connect their logical pins. The complete examples prove this sequence:

```text
author exact Parts -> select and connect -> write library and logical-only project
remove authoring-source and source-library access -> native reopen verified closure
inspect the same model keys, normalized values, tolerances and evidence
```

### Multi-terminal proof and deliberate failures

A three-pin network with terminals `a->A`, `b->B`, `common->COM` and two resistance
elements `(ra,a,common,1 kohm)` and `(rb,b,common,2 kohm)` uses the identical builder API.
It has no special network class. With current positive into each branch's `from`, terminal
currents are `I_A=i_ra`, `I_B=i_rb`, `I_COM=-i_ra-i_rb`; their sum is zero. Two instances
share no private nodes unless their external pins are connected by the Circuit.

| Input | Owning boundary and outcome |
| --- | --- |
| Resistance supplied in volts; NaN; negative R; zero C/L; overflowing tolerance | Element/model construction rejects |
| R = 0 ohm with explicit zero tolerance | Valid exact voltage constraint, possible analysis degeneracy |
| C = 1 uF with minus tolerance 100% | Reject: lower bound reaches forbidden zero |
| Foreign builder node, dangling endpoint, duplicate element key, same model endpoint twice | Construction/finalization rejects |
| Missing contract terminal or duplicate PinKey binding | Model finalization rejects |
| Distinct model terminals connected to the same Circuit net | Structurally valid; preserve law and diagnose relevant short/degeneracy |
| Missing optional model on a valid exact Part | Part stays loadable; required analysis coverage is incomplete |
| Model/evidence missing from a promised self-contained bundle | Bundle build/load rejects |
| Resistor plus an unmodeled LED/IC | Passive model remains valid; full analysis cannot be certified |
| Physical supply with ProvidedRange but no model | No source inferred; explicit replacement/exclusion required |

## 7. Finite acceptance matrix and sequencing

The issue bodies and native GitHub blockers own work state. This matrix assigns proof to
existing slices; it does not change scopes, dispatch them, or mark any later issue ready.

| ID | Accepted requirement and finite proof | Owning issue |
| --- | --- | --- |
| E0-1 | Accepted normative ADR, authoring examples and companion agree; explicit amendments preserve V/I and Circuit boundaries | [#252](https://github.com/RyanWillie/Volt/issues/252) |
| M-1 | Native R/C/L dimensions, finite/domain/tolerance tests including R=0, C/L=0, wrong units and overflow | [#361](https://github.com/RyanWillie/Volt/issues/361) |
| M-2 | Resistor, ideal C/L, C+ESR+ESL and three-terminal network; foreign handles, missing pins and duplicate keys reject; immutable lifetimes | [#361](https://github.com/RyanWillie/Volt/issues/361) |
| M-3 | Optional model affects semantic identity; absent remains absent; transport incapable of preserving it rejects | [#361](https://github.com/RyanWillie/Volt/issues/361) |
| P-1 | Reordered declarations/equivalent units/tolerances produce canonical parity; parameter, orientation, local-key and evidence changes alter identity | [#362](https://github.com/RyanWillie/Volt/issues/362) |
| P-2 | Exact Part, library and logical-only project round trips include all selected model/V/I evidence; source-free native inspection; corruption and incomplete closure reject | [#362](https://github.com/RyanWillie/Volt/issues/362) |
| P-3 | Current-only writer/reader/schema/fixture migration, no old reader or silently lost supported field | [#362](https://github.com/RyanWillie/Volt/issues/362) |
| A-1 | Runnable concise C++/Python resistor, ideal C/L and composite example; select/connect/save/reopen/inspect, native error/lifetime/hash/byte parity | [#363](https://github.com/RyanWillie/Volt/issues/363) |
| T-1 | Typed DC sources/probes/reference/exclusions round-trip against exact inputs; wrong dimensions, foreign/stale refs reject | [#253](https://github.com/RyanWillie/Volt/issues/253) |
| T-2 | Missing model/reference, request-local invalid stimulus, incomplete coverage and ordinary non-simulation build have truthful separate outcomes | [#253](https://github.com/RyanWillie/Volt/issues/253) |
| C-1 | Shared/hierarchical nets, repeated Parts/private nodes, open/shorted terminals, orientation and KCL map deterministically with provenance | [#254](https://github.com/RyanWillie/Volt/issues/254) |
| C-2 | R0 constraints and ideal C/L storage retained; unsupported/incomplete never runnable; source-free inputs and immutable input hashes/bytes proven | [#254](https://github.com/RyanWillie/Volt/issues/254) |

### S1–S3 shared capability proof

Use one resistor-divider case across the stages: author exact Parts, apply 5 V, request the
midpoint voltage, obtain its analytical value, save/reopen the supported inputs and trace
the answer to authored entities. S1 proves request binding and coverage; S2 proves expansion,
continuity, laws and provenance; S3 proves the numerical answer and conservation residuals.
Owner-local request/compiled/result codecs do not automatically add artifact kinds to
ProjectBundle v3; bundle admission requires a focused schema decision.

S3 [#364](https://github.com/RyanWillie/Volt/issues/364) must establish a finite versioned
corpus while the native DC solver is developed: circuit/model/request fixtures, expected S1
coverage, deterministic S2 law/origin snapshots, analytical answers or ill-posed
classifications, and independently authored pinned-SPICE decks/results for supported cases.
Record backend version, tolerances, polarity and conservation checks. Include the divider,
R0 and DC C/L behavior, floating/nonunique/inconsistent ideal constraints, and incomplete
coverage. Unsupported oracle cases remain explicit; agreement between two solvers alone is
not proof. S5 [#255](https://github.com/RyanWillie/Volt/issues/255) reuses this corpus for its
full generated adapter and public backend workflow. This is future acceptance evidence,
not a claim that a solver, corpus or SPICE adapter exists in E3.

### Remaining delivery sequence

E1 -> E2 -> E3 completes correct Part authoring/persistence. S1 follows the amended E3
handoff; S2 and S3 build the shared proof above, followed by S4 public execution #365 and
S5's full adapter. Native issue blockers remain the execution authority.

Linear AC [#368](https://github.com/RyanWillie/Volt/issues/368) and bounded linear transient
[#367](https://github.com/RyanWillie/Volt/issues/367) each depend on #365 and can be refined
independently of each other and nonlinear #366. Prefer AC before transient in serial
scheduling. Direct linear R/C/L AC needs no nonlinear operating-point linearization;
linear transient still needs an explicit initialization, integration and error-control
contract. Biased nonlinear AC and nonlinear transient remain later explicit refinement
under [#366](https://github.com/RyanWillie/Volt/issues/366).

Nonlinear devices, arbitrary IC/digital behaviour, behavioural callbacks, arbitrary
waveforms, transient/AC execution, noise, Monte Carlo and temperature/power laws remain
unsupported by the E1–E3 implementation.

Acceptance settles the semantic choices in this ADR. Implementation may choose internal
layout and idiomatic spellings while preserving these tests; changing coverage, value
domains, identity, ownership or equation meaning requires an explicit ADR amendment.
