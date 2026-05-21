"""Component label helpers for schematic presentation."""

from __future__ import annotations

import json

from .logical import Component


def _component_json(component: Component) -> dict:
    logical = json.loads(component._design.to_json())
    return next(
        item for item in logical["components"] if item["id"] == f"component:{component.index}"
    )


def _component_value_label(component: Component) -> str | None:
    target = _component_json(component)
    for name in ("value", "Value"):
        value = target["properties"].get(name)
        if value is not None:
            return str(value["value"])

    attributes = target.get("electrical_attributes", {})
    for name in ("resistance", "capacitance", "inductance", "voltage", "current", "power"):
        if name in attributes:
            formatted = _format_electrical_attribute(name, attributes[name])
            if formatted is not None:
                return formatted
    for name in sorted(attributes):
        formatted = _format_electrical_attribute(name, attributes[name])
        if formatted is not None:
            return formatted
    return None


def _format_electrical_attribute(name: str, attribute: dict) -> str | None:
    if attribute.get("type") != "quantity":
        return None
    value = attribute.get("value")
    # JSON booleans are ints in Python; quantities must render only real numbers.
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        return None
    unit = {
        "resistance": "ohm",
        "capacitance": "F",
        "inductance": "H",
        "voltage": "V",
        "current": "A",
        "power": "W",
    }.get(name, attribute.get("dimension", ""))
    number = f"{float(value):g}"
    return f"{number} {unit}" if unit else number
