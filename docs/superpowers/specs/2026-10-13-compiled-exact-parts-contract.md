# CompiledBoard exact-part retention (#400)

Baseline: main 6e7f3de06c92b7c3800a2dd4fd9c90d8513eed0e. This finite
prerequisite does not deliver #389 manufacturing readiness.

## Contract

- `CompiledBoard::part_definitions() const &` returns an artifact-owned
  `const PartDefinitionResolver&`; its rvalue overload is deleted. Its private
  implementation retains one `(LibraryPartRef, PartDefinition)` per consumed
  exact reference, not per component. Lookup compares every reference field,
  including both digests. An absent/foreign reference raises native
  `KernelLogicError(UnknownEntity)`; no key/name/digest fallback is permitted.
- Use PartDefinitionResolver, not ExactPartResolver: the consumed closure may
  contain multiple library revisions and has no honest single-library identity.
  Selection APIs remain unchanged. No new generic resolver framework.
- `selected_component_definition(ComponentId) const &` returns a pointer to
  the original verified complete ComponentDefinition for a selected component,
  or null for a valid component with no selection. Invalid component IDs raise
  the existing native Circuit lookup error. The rvalue overload is deleted.
  Complete component definitions are retained once per content identity.
- The decoder's existing component-payload/logical-projection equality and
  part-to-original-component checks remain mandatory before publication.
  Full component identity differs from the symbol-stripped logical projection:
  retained parts must not be fed to strict electrical consumers with that
  projection as though it were the original complete component. This slice
  provides full contract inspection and native BOM consumption, not an
  electrical Circuit reconstruction or an identity-check exception.
- Broaden existing BOM-readiness validation's parameter from ExactPartResolver
  to PartDefinitionResolver: it only resolves definitions and never uses library
  admission/selection identity. Preserve its checks, codes, entities and behavior.
  Existing callers using ExactPartResolver remain source-compatible.
- Returned references/pointers are immutable borrows until owning storage is
  destroyed. Moving the owner preserves borrows; move assignment destroys the
  destination's previous storage and preserves borrows to transferred storage.
  Moved-from owners support destruction/assignment only. No shared ownership or
  lifetime extension. Base resolver reference must itself remain a live borrow.
- Transfer decoded definitions through the existing private Codec/materialization
  boundary. Do not change V1/V2 payload grammars, canonical bytes, provenance,
  IDs, continuity, or hierarchy. No new friend declarations.
- Preserve absent optional records and all asset references. Only existing
  footprint and capability-selected models are closed assets; retaining full
  definitions does not reconstruct a complete PartLibraryBundle or load symbol,
  simulation, evidence, licence or provenance assets.

## Finite verification plan

1. Source/fresh/reopened canonical full PartDefinition equality, full original
   component contract equality (including stripped symbols), rich electrical
   records, ratings, approved alternates, mappings, assembly and asset references.
2. Destroy all source owners; run existing native BOM projection and BOM-readiness
   validation with artifact Circuit and resolver. Compare passing and bad-design
   diagnostic codes/entities; prove quantities, DNP and alternates.
3. Repeated selections share the definition address; digest-distinct references
   with identical namespace/version/key remain separate and foreign/missing full
   references fail with typed errors. Unplaced and DNP selected parts remain;
   valid no-selection returns null and invalid IDs reject.
4. Compile-time rvalue rejection; borrow stability across move construction and
   assignment; independent artifacts do not share owner storage.
5. Rehashed malformed part records and mismatched component closure reject through
   native errors. Keep existing integrity/subject-binding checks and add focused
   negative coverage for the retained path.
6. V1/V2 reopen/write exact bytes and identities remain unchanged; V1 continuity
   stays unavailable with the existing warning. Native ProjectBundle reopen
   resolves full selected parts and runs BOM without source owners.
7. Full development build/CTest (native, Python, IO/architecture/docs scripts),
   package consumer check, independent implementation review, and fresh final-head
   platform, sanitizer, static-analysis and coverage CI. No new Python API/parser
   is required: the existing binding does not expose native resolver consumers.

Contract review is required before implementation and before adding ready to #400.
