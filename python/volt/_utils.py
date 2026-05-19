"""Shared scalar validation helpers for the Volt Python facade."""

from __future__ import annotations

from math import isfinite


def _number(value: float) -> float:
    if isinstance(value, bool):
        raise TypeError("Electrical attribute values must be numbers")
    if not isinstance(value, (int, float)):
        raise TypeError("Electrical attribute values must be numbers")
    result = float(value)
    if not isfinite(result):
        raise ValueError("Electrical attribute values must be finite")
    return result


def _coordinate(value: float) -> float:
    if isinstance(value, bool):
        raise TypeError("Schematic coordinates must be numbers")
    if not isinstance(value, (int, float)):
        raise TypeError("Schematic coordinates must be numbers")
    result = float(value)
    if not isfinite(result):
        raise ValueError("Schematic coordinates must be finite")
    return result


def _positive_coordinate(value: float, label: str) -> float:
    result = _coordinate(value)
    if result <= 0:
        raise ValueError(f"{label} must be positive")
    return result


def _nonnegative_coordinate(value: float, label: str) -> float:
    result = _coordinate(value)
    if result < 0:
        raise ValueError(f"{label} must not be negative")
    return result


def _string_dict(values: dict, context: str) -> dict[str, str]:
    if not isinstance(values, dict):
        raise TypeError(f"{context} must be a dict")
    result: dict[str, str] = {}
    for key, value in values.items():
        if not isinstance(key, str):
            raise TypeError(f"{context} keys must be strings")
        if not key:
            raise ValueError(f"{context} keys must not be empty")
        if not isinstance(value, str):
            raise TypeError(f"{context} values must be strings")
        if not value:
            raise ValueError(f"{context} values must not be empty")
        result[key] = value
    return result


def _orientation(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic orientation must be a string")
    normalized = {
        "right": "Right",
        "down": "Down",
        "left": "Left",
        "up": "Up",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError("Schematic orientation must be Right, Down, Left, or Up")
    return normalized
