"""Build results and validation diagnostics for Volt Python libraries."""

from __future__ import annotations

import json
from collections.abc import Mapping
from dataclasses import dataclass
from typing import Iterable, Iterator

from . import _volt
from ._footprint import Footprint, footprint_ref
from .diagnostics import (
    DiagnosticEntity,
    DiagnosticMeasurement,
    DiagnosticOverlay,
    _diagnostic_from_dict,
)
from .part import Part, _pad_labels


@dataclass(frozen=True)
class LibraryDiagnostic:
    """Diagnostic emitted while validating one library build."""

    source: str
    report: str
    severity: str
    code: str
    message: str
    entities: tuple[object, ...] = ()
    category: str = "general"
    overlays: tuple[DiagnosticOverlay, ...] = ()
    measurement: DiagnosticMeasurement | None = None
    rule: str | None = None


class LibraryDiagnostics:
    """Ordered library diagnostics collected from part validation."""

    def __init__(self, diagnostics: Iterable[LibraryDiagnostic]):
        self._diagnostics = tuple(diagnostics)

    def __iter__(self) -> Iterator[LibraryDiagnostic]:
        """Iterate over diagnostics in deterministic report order."""
        return iter(self._diagnostics)

    def __len__(self) -> int:
        """Return the number of diagnostics."""
        return len(self._diagnostics)

    def __getitem__(self, index: int) -> LibraryDiagnostic:
        """Return one diagnostic by positional index."""
        return self._diagnostics[index]

    def errors(self, *, source: str | None = None) -> tuple[LibraryDiagnostic, ...]:
        """Return error diagnostics, optionally filtered by source."""
        return tuple(
            diagnostic
            for diagnostic in self._diagnostics
            if diagnostic.severity == "error"
            and (source is None or diagnostic.source == source)
        )

    @property
    def has_errors(self) -> bool:
        """Return whether any collected diagnostic has error severity."""
        return bool(self.errors())


@dataclass(frozen=True)
class LibraryPartArtifact:
    """Canonical kernel-owned part artifact emitted by a library build."""

    bytes: bytes
    sha256: str


@dataclass(frozen=True)
class LibraryPartResult:
    """Validation status for one public library part."""

    name: str
    schematic_ready: bool
    board_ready: bool
    serializable: bool
    has_footprint: bool
    pad_mapping_complete: bool
    artifact: LibraryPartArtifact | None
    diagnostics: tuple[LibraryDiagnostic, ...]
    component_sha256: str
    exact_reference: dict[str, str]


class LibraryResult:
    """Output of one deterministic library validation run."""

    def __init__(self, library):
        self.library = library
        validation = [(part, *_validate_part(part)) for part in library.parts]
        native_parts = [
            part
            for part, facts, _diagnostics in validation
            if _part_artifact_prerequisites(part, facts)
        ]
        self._snapshot = library._native_snapshot(native_parts)
        part_results: list[LibraryPartResult] = []
        diagnostics: list[LibraryDiagnostic] = []
        native_names = {part.name for part in native_parts}
        for part, facts, preflight_diagnostics in validation:
            if part.name not in native_names:
                diagnostics.extend(preflight_diagnostics)
                part_results.append(
                    LibraryPartResult(
                        name=part.name,
                        schematic_ready=bool(part.pins and part.schematic_symbols),
                        board_ready=False,
                        serializable=facts.serializable,
                        has_footprint=facts.has_object_footprint,
                        pad_mapping_complete=facts.pad_mapping_complete,
                        artifact=None,
                        diagnostics=preflight_diagnostics,
                        component_sha256="",
                        exact_reference={},
                    )
                )
                continue
            native = self._snapshot.part_result(part.source_name)
            part_diagnostics = _kernel_part_diagnostics(f"part:{part.name}", native["diagnostics"])
            diagnostics.extend(part_diagnostics)
            part_results.append(
                LibraryPartResult(
                    name=part.name,
                    schematic_ready=bool(part.pins and part.schematic_symbols),
                    board_ready=True,
                    serializable=True,
                    has_footprint=True,
                    pad_mapping_complete=True,
                    artifact=LibraryPartArtifact(
                        bytes=bytes(native["bytes"]), sha256=str(native["sha256"])
                    ),
                    diagnostics=part_diagnostics,
                    component_sha256=str(native["component_sha256"]),
                    exact_reference=dict(native["exact_reference"]),
                )
            )
        self._parts = tuple(part_results)
        self._diagnostics = LibraryDiagnostics(diagnostics)

    @property
    def ok(self) -> bool:
        """Return whether the library build has no error diagnostics."""
        return not self._diagnostics.has_errors

    @property
    def diagnostics(self) -> LibraryDiagnostics:
        """Return diagnostics collected during library validation."""
        return self._diagnostics

    @property
    def digest(self) -> str:
        """Return the deterministic native P4 library digest."""
        return str(self._snapshot.digest)

    @property
    def parts(self) -> tuple[LibraryPartResult, ...]:
        """Return per-part validation summaries in deterministic name order."""
        return self._parts

    def part(self, name: str) -> LibraryPartResult:
        """Return a per-part validation summary by part name."""
        for part in self._parts:
            if part.name == name:
                return part
        raise LookupError(f"Library result has no part named {name}")

    def to_dict(self) -> dict:
        """Return a deterministic dictionary payload for this library result."""
        return {
            "format": "volt.library_result",
            "schema_version": 1,
            "library": {
                "namespace": self.library.namespace,
                "version": self.library.version,
            },
            "ok": self.ok,
            "parts": [_part_result_payload(part) for part in self._parts],
            "diagnostics": _library_diagnostics_payload(self._diagnostics),
        }


@dataclass(frozen=True)
class _PartValidationFacts:
    has_logical_pins: bool
    has_object_footprint: bool
    pad_mapping_complete: bool
    serializable: bool


def _validate_part(part: Part) -> tuple[_PartValidationFacts, tuple[LibraryDiagnostic, ...]]:
    diagnostics: list[LibraryDiagnostic] = []
    source = f"part:{part.name}"

    has_logical_pins = bool(part.pins)
    if not has_logical_pins:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_MISSING_PINS",
                f"Part {part.name} has no logical pins",
            )
        )

    has_object_footprint = isinstance(part.footprint, Footprint)
    if part.footprint is None:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_MISSING_FOOTPRINT",
                f"Part {part.name} has no selected footprint",
            )
        )
    elif not has_object_footprint:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_MISSING_FOOTPRINT_GEOMETRY",
                f"Part {part.name} uses a footprint reference without reusable geometry",
            )
        )

    serializable = True
    try:
        json.dumps(part._to_dict(), sort_keys=True)
    except (TypeError, ValueError) as error:
        serializable = False
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_NON_SERIALIZABLE",
                f"Part {part.name} contains non-serializable data: {error}",
            )
        )

    return (
        _PartValidationFacts(
            has_logical_pins=has_logical_pins,
            has_object_footprint=has_object_footprint,
            pad_mapping_complete=False,
            serializable=serializable,
        ),
        tuple(diagnostics),
    )


def _part_artifact_prerequisites(part: Part, facts: _PartValidationFacts) -> bool:
    if part.library is None:
        return False
    if not (
        facts.has_logical_pins and facts.has_object_footprint and facts.serializable
    ):
        return False
    physical = part._physical_part_spec()
    return bool(physical and physical.manufacturer and physical.part_number and physical.package)


def _part_artifact_payload(part: Part) -> dict[str, object]:
    if part.library is None:
        raise ValueError("part must be bound to a library before artifact emission")
    physical = part._physical_part_spec()
    if physical is None:
        raise ValueError("part artifact requires an orderable physical part")
    pads = _part_physical_pin_pads(part, physical)
    footprint_library, footprint_name = footprint_ref(physical.footprint)
    payload = {
        "identity": {
            "namespace": part.library.namespace,
            "name": part.source_name,
            "version": part.source_version or part.library.version,
        },
        "component_name": part.name,
        "component_properties": dict(part.properties),
        "pins": [pin._to_dict() for pin in part.pins],
        "contract": None if part.contract is None else part.contract._to_dict(),
        "electrical_records": [
            record._to_dict() for record in part.electrical_records
        ],
        "provenance": _part_provenance_payload(part),
        "symbols": _part_symbol_refs(part),
        "orderable_part": {
            "manufacturer": physical.manufacturer,
            "mpn": physical.part_number,
            "package": physical.package,
            "footprint_library": footprint_library,
            "footprint_name": footprint_name,
            "footprint_hash": _content_hash(_footprint_payload(physical.footprint)),
            "footprint_pads": _part_footprint_pads(physical.footprint),
            "footprint_courtyard": _part_footprint_polygon(
                physical.footprint, "courtyard"
            ),
            "footprint_body": _part_footprint_polygon(physical.footprint, "body"),
            "footprint_fabrication_outline": _part_footprint_polygon(
                physical.footprint, "fabrication_outline"
            ),
            "footprint_assembly_outline": _part_footprint_polygon(
                physical.footprint, "assembly_outline"
            ),
            "footprint_markings": _part_footprint_markings(physical.footprint),
            "pin_pad_mappings": _part_pin_pad_mappings(part, pads),
            "approved_alternate_mpns": list(physical.approved_alternate_mpns),
            "model_3d": _model_3d_reference_payload(physical.model_3d),
        },
    }
    payload["assets"] = _part_asset_payloads(part, payload)
    return payload


def _part_asset_payloads(part: Part, payload: dict[str, object]) -> list[dict[str, object]]:
    assets: list[dict[str, object]] = []
    for symbol, symbol_ref in zip(part.schematic_symbols, payload["symbols"], strict=True):
        assets.append(
            {
                "kind": "schematic",
                "key": f"symbol:{symbol_ref['name']}@{symbol_ref['variant']}",
                "bytes": _canonical_bytes(_symbol_payload(symbol)),
            }
        )
    physical = part._physical_part_spec()
    if physical is None or not isinstance(physical.footprint, Footprint):
        return assets
    footprint_library, footprint_name = footprint_ref(physical.footprint)
    assets.append(
        {
            "kind": "footprint",
            "key": f"footprint:{footprint_library}/{footprint_name}",
            "bytes": _canonical_bytes(_footprint_payload(physical.footprint)),
        }
    )
    if physical.model_3d is not None:
        assets.append(
            {
                "kind": "model_3d",
                "key": (
                    f"model:{physical.model_3d.format}/"
                    f"{physical.model_3d.file_name}"
                ),
                "bytes": physical.model_3d.source_path.read_bytes(),
            }
        )
    return assets


def _part_provenance_payload(part: Part) -> dict[str, object]:
    provenance = part.extensions.get("provenance", {})
    if not isinstance(provenance, Mapping):
        return {}
    return {
        "datasheet": provenance.get("datasheet", ""),
        "authored_by": provenance.get("authored_by", ""),
        "derived_from": provenance.get("derived_from", ""),
    }


def _part_symbol_refs(part: Part) -> list[dict[str, object]]:
    return [
        {
            "name": symbol.name,
            "variant": symbol.variant,
            "hash": _content_hash(_symbol_payload(symbol)),
            "pins": [
                {"name": pin.name, "number": str(pin.number)}
                for pin in symbol.pins
            ],
        }
        for symbol in part.schematic_symbols
    ]


def _symbol_payload(symbol) -> dict[str, object]:
    payload = symbol._to_dict()
    payload["variant"] = symbol.variant
    return payload


def _footprint_payload(footprint) -> dict[str, object]:
    if isinstance(footprint, Footprint):
        return footprint._to_dict()
    library, name = footprint_ref(footprint)
    return {"ref": (library, name)}


def _part_physical_pin_pads(part: Part, physical=None) -> dict[int | str, object]:
    if physical is None:
        physical = part._physical_part_spec()
    if physical is None:
        return {}
    if physical.same_numbered_pads or physical.pin_pads is not None:
        return physical.pin_pads_for(part)
    return {}


def _part_footprint_pads(footprint: Footprint) -> list[dict[str, object]]:
    if not isinstance(footprint, Footprint):
        raise ValueError("part artifact requires footprint geometry")
    return [_part_footprint_pad(pad) for pad in footprint.pads]


def _part_footprint_polygon(
    footprint: Footprint,
    name: str,
) -> list[dict[str, float]] | None:
    if not isinstance(footprint, Footprint):
        raise ValueError("part artifact requires footprint geometry")
    polygon = getattr(footprint, name)
    if polygon is None:
        return None
    return [{"x_mm": float(x), "y_mm": float(y)} for x, y in polygon]


def _part_footprint_markings(footprint: Footprint) -> list[dict[str, object]]:
    if not isinstance(footprint, Footprint):
        raise ValueError("part artifact requires footprint geometry")
    return [
        {
            "kind": marking.kind,
            "polygon": [
                {"x_mm": float(x), "y_mm": float(y)}
                for x, y in marking.polygon
            ],
        }
        for marking in footprint.markings
    ]


def _part_footprint_pad(pad) -> dict[str, object]:
    payload: dict[str, object] = {
        "label": str(pad.label),
        "x_mm": float(pad.position[0]),
        "y_mm": float(pad.position[1]),
        "width_mm": float(pad.size[0]),
        "height_mm": float(pad.size[1]),
    }
    role = _part_footprint_pad_role(pad)
    if role is not None:
        payload["role"] = role
    return payload


def _part_footprint_pad_role(pad) -> str | None:
    role = getattr(pad, "mechanical_role", None)
    if role is None:
        return None
    role_text = str(role)
    if role_text.casefold() == "thermal":
        return "thermal"
    if role_text in {
        "mechanical",
        "mounting",
        "Mounting",
        "fiducial",
        "Fiducial",
        "mechanical_support",
        "mechanical-support",
        "MechanicalSupport",
    }:
        return "mechanical"
    raise ValueError(f"Unknown footprint pad mechanical role: {role_text}")


def _part_pin_pad_mappings(
    part: Part,
    pads: dict[int | str, object],
) -> list[dict[str, str]]:
    pads_by_pin_number, pads_by_pin_name = _pads_by_logical_pin(part, pads)
    mappings: list[dict[str, str]] = []
    consumed_keys: set[str] = set()
    for pin in part.pins:
        labels = pads_by_pin_number.get(str(pin.number))
        consumed_key = str(pin.number)
        if labels is None:
            labels = pads_by_pin_name.get(pin.name)
            consumed_key = pin.name
        if labels is None:
            continue
        consumed_keys.add(consumed_key)
        for label in labels:
            mappings.append({"pin_number": str(pin.number), "pad": label})
    for key, value in sorted(pads.items(), key=lambda item: str(item[0])):
        key_text = str(key)
        if key_text in consumed_keys:
            continue
        for label in _pad_labels(value):
            mappings.append({"pin_number": key_text, "pad": label})
    return mappings


def _pads_by_logical_pin(
    part: Part,
    pads: dict[int | str, object],
) -> tuple[dict[str, tuple[str, ...]], dict[str, tuple[str, ...]]]:
    pin_numbers = {str(pin.number) for pin in part.pins}
    unique_pin_names = _unique_pin_names(part)
    by_number: dict[str, tuple[str, ...]] = {}
    by_name: dict[str, tuple[str, ...]] = {}
    for key, value in pads.items():
        labels = tuple(_pad_labels(value))
        key_text = str(key)
        if key_text in pin_numbers:
            by_number[key_text] = labels
        elif key_text in unique_pin_names:
            by_name[key_text] = labels
    return by_number, by_name


def _unique_pin_names(part: Part) -> set[str]:
    names: dict[str, int] = {}
    for pin in part.pins:
        names[pin.name] = names.get(pin.name, 0) + 1
    return {name for name, count in names.items() if count == 1}


def _model_3d_reference_payload(model) -> dict[str, object] | None:
    if model is None:
        return None
    payload = model._selected_part_payload()
    return {
        "format": payload["format"],
        "file_name": payload["file_name"],
        "hash": str(_volt.content_hash(model.source_path.read_bytes())),
        "translation_mm": payload["translation_mm"],
        "rotation_deg": payload["rotation_deg"],
    }


def _content_hash(payload: object) -> str:
    return str(_volt.content_hash(_canonical_bytes(payload)))


def _canonical_bytes(payload: object) -> bytes:
    return json.dumps(
        payload,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")


def _kernel_part_diagnostics(source: str, diagnostics) -> tuple[LibraryDiagnostic, ...]:
    result: list[LibraryDiagnostic] = []
    for item in diagnostics:
        diagnostic = _diagnostic_from_dict(item)
        result.append(
            LibraryDiagnostic(
                source=source,
                report=diagnostic.category,
                severity=diagnostic.severity,
                code=diagnostic.code,
                message=diagnostic.message,
                entities=diagnostic.entities,
                category=diagnostic.category,
                overlays=diagnostic.overlays,
                measurement=diagnostic.measurement,
                rule=diagnostic.rule,
            )
        )
    return tuple(result)


def _library_diagnostic(source: str, code: str, message: str) -> LibraryDiagnostic:
    return LibraryDiagnostic(
        source=source,
        report="library.part",
        severity="error",
        code=code,
        message=message,
    )


def _part_result_payload(part: LibraryPartResult) -> dict:
    return {
        "name": part.name,
        "schematic_ready": part.schematic_ready,
        "board_ready": part.board_ready,
        "serializable": part.serializable,
        "has_footprint": part.has_footprint,
        "pad_mapping_complete": part.pad_mapping_complete,
        "artifact": (
            None
            if part.artifact is None
            else {
                "sha256": part.artifact.sha256,
                "byte_size": len(part.artifact.bytes),
            }
        ),
        "component_sha256": part.component_sha256,
        "exact_reference": dict(part.exact_reference),
        "diagnostics": [_library_diagnostic_payload(diagnostic) for diagnostic in part.diagnostics],
    }


def _library_diagnostics_payload(diagnostics: LibraryDiagnostics) -> dict:
    return {
        "summary": _library_diagnostic_summary(diagnostics),
        "diagnostics": [_library_diagnostic_payload(diagnostic) for diagnostic in diagnostics],
    }


def _library_diagnostic_payload(diagnostic: LibraryDiagnostic) -> dict[str, object]:
    return {
        "source": diagnostic.source,
        "report": diagnostic.report,
        "severity": diagnostic.severity,
        "category": diagnostic.category,
        "code": diagnostic.code,
        "message": diagnostic.message,
        "entities": [_diagnostic_entity_payload(entity) for entity in diagnostic.entities],
        "overlays": [_diagnostic_overlay_payload(overlay) for overlay in diagnostic.overlays],
        "measurement": _diagnostic_measurement_payload(diagnostic.measurement),
        "rule": diagnostic.rule,
    }


def _diagnostic_entity_payload(entity: object) -> dict[str, object]:
    if isinstance(entity, DiagnosticEntity):
        return {"kind": entity.kind, "index": entity.index}
    return dict(entity)


def _diagnostic_overlay_payload(overlay: DiagnosticOverlay) -> dict[str, object]:
    return {
        "kind": overlay.kind,
        "points": [list(point) for point in overlay.points],
        "entities": [_diagnostic_entity_payload(entity) for entity in overlay.entities],
        "layers": [_diagnostic_entity_payload(layer) for layer in overlay.layers],
    }


def _diagnostic_measurement_payload(
    measurement: DiagnosticMeasurement | None,
) -> dict[str, float] | None:
    if measurement is None:
        return None
    return {
        "actual_mm": measurement.actual_mm,
        "required_mm": measurement.required_mm,
    }


def _library_diagnostic_summary(diagnostics: LibraryDiagnostics) -> dict[str, int]:
    summary = {"errors": 0, "infos": 0, "warnings": 0}
    for diagnostic in diagnostics:
        if diagnostic.severity == "error":
            summary["errors"] += 1
        elif diagnostic.severity == "warning":
            summary["warnings"] += 1
        else:
            summary["infos"] += 1
    return summary
