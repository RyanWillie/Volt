"""Diagnostic value objects for the Volt Python facade."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Iterator


@dataclass(frozen=True)
class DiagnosticEntity:
    """Reference to a kernel entity involved in a diagnostic."""

    kind: str
    index: int


@dataclass(frozen=True)
class Diagnostic:
    """Kernel-produced diagnostic exposed through the Python facade."""

    severity: str
    code: str
    message: str
    entities: tuple[DiagnosticEntity, ...]


class DiagnosticReport:
    """Ordered collection of kernel diagnostics."""

    def __init__(self, diagnostics: Iterable[Diagnostic]):
        self._diagnostics = tuple(diagnostics)

    def __iter__(self) -> Iterator[Diagnostic]:
        """Iterate over diagnostics in report order."""
        return iter(self._diagnostics)

    def __len__(self) -> int:
        """Return the number of diagnostics in this report."""
        return len(self._diagnostics)

    def __getitem__(self, index: int) -> Diagnostic:
        """Return one diagnostic by positional index."""
        return self._diagnostics[index]

    @property
    def has_errors(self) -> bool:
        """Return whether this report contains at least one error diagnostic."""
        return any(diagnostic.severity == "error" for diagnostic in self._diagnostics)



def _diagnostic_from_dict(item) -> Diagnostic:
    return Diagnostic(
        severity=item["severity"],
        code=item["code"],
        message=item["message"],
        entities=tuple(
            DiagnosticEntity(entity["kind"], entity["index"]) for entity in item["entities"]
        ),
    )
