"""Build results and validation diagnostics for Volt Python libraries."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Iterable, Iterator

from ._footprint import Footprint
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
class LibraryPartResult:
    """Validation status for one public library part."""

    name: str
    schematic_ready: bool
    board_ready: bool
    serializable: bool
    has_footprint: bool
    pad_mapping_complete: bool
    diagnostics: tuple[LibraryDiagnostic, ...]


class LibraryResult:
    """Output of one deterministic library validation run."""

    def __init__(self, library):
        self.library = library
        part_results: list[LibraryPartResult] = []
        diagnostics: list[LibraryDiagnostic] = []
        for part in library.parts:
            facts, part_diagnostics = _validate_part(part)
            diagnostics.extend(part_diagnostics)
            part_results.append(_part_result(part, facts, part_diagnostics))
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

    pad_mapping_complete = True
    if has_logical_pins and part.footprint is not None:
        pad_mapping_complete, pad_diagnostics = _validate_part_pad_mapping(part, source)
        diagnostics.extend(pad_diagnostics)

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
            pad_mapping_complete=pad_mapping_complete,
            serializable=serializable,
        ),
        tuple(diagnostics),
    )


def _validate_part_pad_mapping(
    part: Part,
    source: str,
) -> tuple[bool, tuple[LibraryDiagnostic, ...]]:
    diagnostics: list[LibraryDiagnostic] = []
    pads = part.pads or {}
    mapped_pin_numbers = _mapped_pin_numbers(part.pins, pads)
    missing_pin_numbers = [
        str(pin.number) for pin in part.pins if str(pin.number) not in mapped_pin_numbers
    ]
    if missing_pin_numbers:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_MISSING_PIN_MAPPING",
                "Part "
                f"{part.name} has logical pins without pad mappings: "
                + ", ".join(missing_pin_numbers),
            )
        )

    if isinstance(part.footprint, Footprint):
        diagnostics.extend(_validate_footprint_pad_mapping(part, source, pads))

    return (not diagnostics, tuple(diagnostics))


def _validate_footprint_pad_mapping(
    part: Part,
    source: str,
    pads: dict[int | str, object],
) -> tuple[LibraryDiagnostic, ...]:
    diagnostics: list[LibraryDiagnostic] = []
    footprint_labels = {str(pad.label) for pad in part.footprint.pads}
    mapped_labels = _mapped_pad_labels(pads)
    unknown_labels = sorted(label for label in mapped_labels if label not in footprint_labels)
    if unknown_labels:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_UNKNOWN_PAD",
                f"Part {part.name} maps to unknown footprint pads: "
                + ", ".join(unknown_labels),
            )
        )

    missing_electrical_labels = [
        str(pad.label)
        for pad in part.footprint.pads
        if _pad_requires_mapping(pad) and str(pad.label) not in mapped_labels
    ]
    if missing_electrical_labels:
        diagnostics.append(
            _library_diagnostic(
                source,
                "LIBRARY_PART_INCOMPLETE_PAD_MAPPING",
                f"Part {part.name} has electrical footprint pads without logical "
                "pin mappings: "
                + ", ".join(sorted(missing_electrical_labels)),
            )
        )

    return tuple(diagnostics)


def _part_result(
    part: Part,
    facts: _PartValidationFacts,
    diagnostics: tuple[LibraryDiagnostic, ...],
) -> LibraryPartResult:
    return LibraryPartResult(
        name=part.name,
        schematic_ready=bool(part.pins and part.schematic_symbols),
        board_ready=(
            facts.has_logical_pins
            and facts.has_object_footprint
            and facts.pad_mapping_complete
        ),
        serializable=facts.serializable,
        has_footprint=facts.has_object_footprint,
        pad_mapping_complete=facts.pad_mapping_complete,
        diagnostics=diagnostics,
    )


def _mapped_pin_numbers(
    pins: tuple[object, ...],
    pads: dict[int | str, object],
) -> set[str]:
    pin_numbers = {str(pin.number) for pin in pins}
    names: dict[str, str | None] = {}
    for pin in pins:
        existing = names.get(pin.name)
        if existing is None and pin.name in names:
            continue
        if existing is not None:
            names[pin.name] = None
        else:
            names[pin.name] = str(pin.number)

    mapped: set[str] = set()
    for key in pads:
        key_text = str(key)
        if key_text in pin_numbers:
            mapped.add(key_text)
            continue
        named_number = names.get(key_text)
        if named_number is not None:
            mapped.add(named_number)
    return mapped


def _mapped_pad_labels(pads: dict[int | str, object]) -> set[str]:
    labels: set[str] = set()
    for value in pads.values():
        for label in _pad_labels(value):
            labels.add(label)
    return labels


def _pad_requires_mapping(pad) -> bool:
    return getattr(pad, "mechanical_role", None) is None


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
        "diagnostics": [
            {
                "source": diagnostic.source,
                "report": diagnostic.report,
                "severity": diagnostic.severity,
                "code": diagnostic.code,
                "message": diagnostic.message,
                "entities": list(diagnostic.entities),
            }
            for diagnostic in part.diagnostics
        ],
    }


def _library_diagnostics_payload(diagnostics: LibraryDiagnostics) -> dict:
    return {
        "summary": _library_diagnostic_summary(diagnostics),
        "diagnostics": [
            {
                "source": diagnostic.source,
                "report": diagnostic.report,
                "severity": diagnostic.severity,
                "code": diagnostic.code,
                "message": diagnostic.message,
                "entities": list(diagnostic.entities),
            }
            for diagnostic in diagnostics
        ],
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
