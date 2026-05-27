"""Private schematic symbol builder implementation for :mod:`volt.library`."""

from __future__ import annotations

from dataclasses import dataclass
from typing import TYPE_CHECKING, Iterable

from ._utils import _coordinate, _nonnegative_coordinate, _positive_coordinate

if TYPE_CHECKING:
    from .library import (
        SchematicBlockPinSpec,
        SchematicBlockSideLayoutSpec,
        SchematicSymbolPinSpec,
        SchematicSymbolSpec,
    )


@dataclass(frozen=True)
class _SchematicBlockSideLayout:
    pad: float
    lead_length: float
    pin_pitch: float
    pin_label_offset: float
    pin_number_offset: float


def _symbol_point(value: tuple[float, float]) -> dict:
    if not isinstance(value, (tuple, list)) or len(value) != 2:
        raise TypeError("Schematic symbol points must be (x, y) pairs")
    return {"x": _coordinate(value[0]), "y": _coordinate(value[1])}


def _symbol_line_role(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic symbol line roles must be strings")
    normalized = {
        "normal": "Normal",
        "terminalleadstart": "TerminalLeadStart",
        "terminal_lead_start": "TerminalLeadStart",
        "terminal-lead-start": "TerminalLeadStart",
        "start": "TerminalLeadStart",
        "terminalleadend": "TerminalLeadEnd",
        "terminal_lead_end": "TerminalLeadEnd",
        "terminal-lead-end": "TerminalLeadEnd",
        "end": "TerminalLeadEnd",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError(
            "Schematic symbol line roles must be Normal, TerminalLeadStart, or TerminalLeadEnd"
        )
    return normalized


def _terminal_lead_line_role(terminal: str) -> str:
    if not isinstance(terminal, str):
        raise TypeError("Schematic terminal lead names must be strings")
    normalized = {
        "start": "TerminalLeadStart",
        "1": "TerminalLeadStart",
        "left": "TerminalLeadStart",
        "end": "TerminalLeadEnd",
        "2": "TerminalLeadEnd",
        "right": "TerminalLeadEnd",
    }.get(terminal.casefold())
    if normalized is None:
        raise ValueError("Schematic terminal leads must target start or end")
    return normalized


def _schematic_block_pin_side(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic block pin sides must be strings")
    normalized = {
        "l": "Left",
        "left": "Left",
        "r": "Right",
        "right": "Right",
        "t": "Up",
        "top": "Up",
        "up": "Up",
        "u": "Up",
        "b": "Down",
        "bottom": "Down",
        "down": "Down",
        "d": "Down",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError("Schematic block pin side must be left, right, top, or bottom")
    return normalized


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


def _text_horizontal_alignment(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic text horizontal alignment must be a string")
    normalized = {
        "start": "Start",
        "left": "Start",
        "middle": "Middle",
        "center": "Middle",
        "centre": "Middle",
        "end": "End",
        "right": "End",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError("Schematic text horizontal alignment must be start, middle, or end")
    return normalized


def _text_vertical_alignment(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic text vertical alignment must be a string")
    normalized = {
        "top": "Top",
        "hanging": "Top",
        "middle": "Middle",
        "center": "Middle",
        "centre": "Middle",
        "bottom": "Bottom",
        "baseline": "Baseline",
        "alphabetic": "Baseline",
    }.get(value.casefold())
    if normalized is None:
        raise ValueError(
            "Schematic text vertical alignment must be top, middle, bottom, or baseline"
        )
    return normalized


def _schematic_block_symbol_spec(
    name: str,
    *,
    pins: Iterable[SchematicBlockPinSpec],
    width: float | None,
    height: float | None,
    lead_length: float,
    pin_pitch: float,
    pin_label_offset: float,
    side_layouts: Iterable[SchematicBlockSideLayoutSpec],
    center_label: str | None,
    bottom_label: str | None,
    pin_labels: bool,
    pin_numbers: bool,
    pin_number_offset: float,
    variant: str,
) -> SchematicSymbolSpec:
    from .library import SchematicSymbolSpec

    if not isinstance(name, str):
        raise TypeError("Schematic block symbol names must be strings")
    if not name:
        raise ValueError("Schematic block symbol names must not be empty")
    block_pins = tuple(_schematic_block_pin_entry(pin) for pin in pins)
    if not block_pins:
        raise ValueError("Schematic block symbols need at least one pin")
    if not isinstance(pin_labels, bool):
        raise TypeError("Schematic block symbol pin_labels must be a boolean")
    if not isinstance(pin_numbers, bool):
        raise TypeError("Schematic block symbol pin_numbers must be a boolean")

    pitch = _positive_coordinate(pin_pitch, "Schematic block symbol pin pitches")
    lead = _nonnegative_coordinate(lead_length, "Schematic block symbol lead lengths")
    label_offset = _nonnegative_coordinate(
        pin_label_offset,
        "Schematic block symbol pin label offsets",
    )
    number_offset = _nonnegative_coordinate(
        pin_number_offset,
        "Schematic block symbol pin number offsets",
    )
    layouts = _schematic_block_side_layouts(
        side_layouts,
        pad=pitch,
        lead_length=lead,
        pin_pitch=pitch,
        pin_label_offset=label_offset,
        pin_number_offset=number_offset,
    )
    slots = _schematic_block_pin_slots(block_pins)
    horizontal_extent = max(
        (
            _schematic_block_slot_offset(layouts[pin.side], slot)
            + layouts[pin.side].pin_pitch
            for pin, slot in slots
            if pin.side in ("Up", "Down")
        ),
        default=0,
    )
    vertical_extent = max(
        (
            _schematic_block_slot_offset(layouts[pin.side], slot)
            + layouts[pin.side].pin_pitch
            for pin, slot in slots
            if pin.side in ("Left", "Right")
        ),
        default=0,
    )
    body_width = (
        _positive_coordinate(width, "Schematic block symbol widths")
        if width is not None
        else max(horizontal_extent, 4 * pitch)
    )
    body_height = (
        _positive_coordinate(height, "Schematic block symbol heights")
        if height is not None
        else max(vertical_extent, 4 * pitch)
    )
    horizontal_max = max(
        (
            _schematic_block_slot_offset(layouts[pin.side], slot)
            for pin, slot in slots
            if pin.side in ("Up", "Down")
        ),
        default=0,
    )
    vertical_max = max(
        (
            _schematic_block_slot_offset(layouts[pin.side], slot)
            for pin, slot in slots
            if pin.side in ("Left", "Right")
        ),
        default=0,
    )
    if horizontal_max > body_width:
        raise ValueError("Schematic block symbol width is too small for top or bottom pin slots")
    if vertical_max > body_height:
        raise ValueError("Schematic block symbol height is too small for left or right pin slots")

    center_label = _optional_symbol_text(center_label, "center label")
    bottom_label = _optional_symbol_text(bottom_label, "bottom label")

    body_left = layouts["Left"].lead_length
    body_top = 0.0
    body_right = body_left + body_width
    body_bottom = body_height
    symbol_pins = []
    primitives = [
        SchematicSymbolSpec.rectangle((body_left, body_top), (body_right, body_bottom))
    ]
    if center_label is not None:
        primitives.append(
            SchematicSymbolSpec.text(
                center_label,
                (body_left + body_width / 2, body_top + body_height / 2),
                baseline="middle",
            )
        )
    if bottom_label is not None:
        primitives.append(
            SchematicSymbolSpec.text(
                bottom_label,
                (
                    body_left + body_width / 2,
                    body_bottom
                    + layouts["Down"].lead_length
                    + layouts["Down"].pin_label_offset,
                ),
                baseline="top",
            )
        )

    seen_numbers: set[str] = set()
    for pin, slot in slots:
        if pin.number in seen_numbers:
            raise ValueError(f"Schematic block symbol pin number {pin.number!r} is duplicated")
        seen_numbers.add(pin.number)
        anchor, body = _schematic_block_pin_points(
            pin.side,
            slot=slot,
            layout=layouts[pin.side],
            body_left=body_left,
            body_right=body_right,
            body_top=body_top,
            body_bottom=body_bottom,
        )
        symbol_pins.append(SchematicSymbolSpec.pin(pin.name, pin.number, anchor, pin.side))
        primitives.append(SchematicSymbolSpec.line(anchor, body))
        if pin_labels:
            primitives.append(
                SchematicSymbolSpec.text(
                    pin.label or pin.name,
                    _schematic_block_pin_label_point(
                        pin.side,
                        body=body,
                        offset=layouts[pin.side].pin_label_offset,
                    ),
                    **_schematic_block_pin_label_text_style(pin.side),
                )
            )
        if pin_numbers:
            primitives.append(
                SchematicSymbolSpec.text(
                    pin.number,
                    _schematic_block_pin_number_point(
                        pin.side,
                        body=body,
                        offset=layouts[pin.side].pin_number_offset,
                    ),
                    **_schematic_block_pin_number_text_style(pin.side),
                )
            )

    return SchematicSymbolSpec(
        name,
        pins=tuple(symbol_pins),
        primitives=tuple(primitives),
        variant=variant,
    )


def _schematic_block_pin_entry(value) -> SchematicBlockPinSpec:
    from .library import SchematicBlockPinSpec

    if not isinstance(value, SchematicBlockPinSpec):
        raise TypeError("Schematic block symbol pins must be SchematicBlockPinSpec entries")
    return value


def _schematic_block_pin_slots(
    pins: tuple[SchematicBlockPinSpec, ...],
) -> tuple[tuple[SchematicBlockPinSpec, int], ...]:
    used: dict[str, set[int]] = {"Left": set(), "Right": set(), "Up": set(), "Down": set()}
    next_slot = {side: 1 for side in used}
    result = []
    for pin in pins:
        slot = pin.slot
        if slot is None:
            while next_slot[pin.side] in used[pin.side]:
                next_slot[pin.side] += 1
            slot = next_slot[pin.side]
        if slot in used[pin.side]:
            raise ValueError(
                f"Schematic block symbol {pin.side.lower()} pin slot {slot} is duplicated"
            )
        used[pin.side].add(slot)
        result.append((pin, slot))
    return tuple(result)


def _schematic_block_side_layouts(
    entries: Iterable[SchematicBlockSideLayoutSpec],
    *,
    pad: float,
    lead_length: float,
    pin_pitch: float,
    pin_label_offset: float,
    pin_number_offset: float,
) -> dict[str, _SchematicBlockSideLayout]:
    result = {
        side: _SchematicBlockSideLayout(
            pad=pad,
            lead_length=lead_length,
            pin_pitch=pin_pitch,
            pin_label_offset=pin_label_offset,
            pin_number_offset=pin_number_offset,
        )
        for side in ("Left", "Right", "Up", "Down")
    }
    seen: set[str] = set()
    for entry in entries:
        layout = _schematic_block_side_layout_entry(entry)
        if layout.side in seen:
            raise ValueError(
                f"Schematic block symbol side layout for {layout.side.lower()} is duplicated"
            )
        seen.add(layout.side)
        result[layout.side] = _SchematicBlockSideLayout(
            pad=(
                _nonnegative_coordinate(layout.pad, "Schematic block symbol side layout pads")
                if layout.pad is not None
                else pad
            ),
            lead_length=(
                _nonnegative_coordinate(
                    layout.lead_length,
                    "Schematic block symbol side layout lead lengths",
                )
                if layout.lead_length is not None
                else lead_length
            ),
            pin_pitch=(
                _positive_coordinate(
                    layout.pin_pitch,
                    "Schematic block symbol side layout pin pitches",
                )
                if layout.pin_pitch is not None
                else pin_pitch
            ),
            pin_label_offset=(
                _nonnegative_coordinate(
                    layout.pin_label_offset,
                    "Schematic block symbol side layout pin label offsets",
                )
                if layout.pin_label_offset is not None
                else pin_label_offset
            ),
            pin_number_offset=(
                _nonnegative_coordinate(
                    layout.pin_number_offset,
                    "Schematic block symbol side layout pin number offsets",
                )
                if layout.pin_number_offset is not None
                else pin_number_offset
            ),
        )
    return result


def _schematic_block_side_layout_entry(value) -> SchematicBlockSideLayoutSpec:
    from .library import SchematicBlockSideLayoutSpec

    if not isinstance(value, SchematicBlockSideLayoutSpec):
        raise TypeError(
            "Schematic block symbol side_layouts must be SchematicBlockSideLayoutSpec entries"
        )
    return value


def _schematic_block_slot_offset(layout: _SchematicBlockSideLayout, slot: int) -> float:
    return layout.pad + (slot - 1) * layout.pin_pitch


def _schematic_block_pin_points(
    side: str,
    *,
    slot: int,
    layout: _SchematicBlockSideLayout,
    body_left: float,
    body_right: float,
    body_top: float,
    body_bottom: float,
) -> tuple[tuple[float, float], tuple[float, float]]:
    offset = _schematic_block_slot_offset(layout, slot)
    if side == "Left":
        return (
            (body_left - layout.lead_length, body_top + offset),
            (body_left, body_top + offset),
        )
    if side == "Right":
        return (
            (body_right + layout.lead_length, body_top + offset),
            (body_right, body_top + offset),
        )
    if side == "Up":
        return (
            (body_left + offset, body_top - layout.lead_length),
            (body_left + offset, body_top),
        )
    return (
        (body_left + offset, body_bottom + layout.lead_length),
        (body_left + offset, body_bottom),
    )


def _schematic_block_pin_label_point(
    side: str,
    *,
    body: tuple[float, float],
    offset: float,
) -> tuple[float, float]:
    x, y = body
    if side == "Left":
        return (x + offset, y)
    if side == "Right":
        return (x - offset, y)
    if side == "Up":
        return (x, y + offset)
    return (x, y - offset)


def _schematic_block_pin_number_point(
    side: str,
    *,
    body: tuple[float, float],
    offset: float,
) -> tuple[float, float]:
    x, y = body
    if side == "Left":
        return (x - offset, y + offset)
    if side == "Right":
        return (x + offset, y + offset)
    if side == "Up":
        return (x + offset, y - offset)
    return (x + offset, y + offset)


def _schematic_block_pin_label_text_style(side: str) -> dict[str, str]:
    if side == "Left":
        return {"align": "start", "baseline": "middle"}
    if side == "Right":
        return {"align": "end", "baseline": "middle"}
    if side == "Up":
        return {"baseline": "top"}
    return {"baseline": "bottom"}


def _schematic_block_pin_number_text_style(side: str) -> dict[str, str]:
    if side == "Left":
        return {"align": "end", "baseline": "bottom"}
    if side == "Right":
        return {"align": "start", "baseline": "bottom"}
    if side == "Up":
        return {"align": "start", "baseline": "bottom"}
    return {"align": "start", "baseline": "top"}


def _optional_symbol_text(value: str | None, label: str) -> str | None:
    if value is None:
        return None
    if not isinstance(value, str):
        raise TypeError(f"Schematic block symbol {label} must be a string")
    if not value:
        raise ValueError(f"Schematic block symbol {label} must not be empty")
    return value


def _default_two_terminal_symbol_spec(name: str) -> SchematicSymbolSpec | None:
    # Production defaults keep identity in fields or explicitly authored text, not
    # internal debug glyphs embedded in generic element geometry.
    if name in ("resistor", "volt.passives:resistor"):
        return _resistor_symbol_spec(name)
    if name in ("capacitor", "volt.passives:capacitor"):
        return _capacitor_symbol_spec(name)
    if name in ("inductor", "volt.passives:inductor"):
        return _inductor_symbol_spec(name)
    if name in ("diode", "volt.discretes:diode"):
        return _diode_symbol_spec(name)
    if name in ("led", "volt.optos:led"):
        return _led_symbol_spec(name)
    return None


def _two_terminal_pins(
    left_name: str,
    left_number: int | str,
    right_name: str,
    right_number: int | str,
) -> tuple[SchematicSymbolPinSpec, SchematicSymbolPinSpec]:
    from .library import SchematicSymbolSpec

    return (
        SchematicSymbolSpec.pin(left_name, left_number, (0, 0), "Left"),
        SchematicSymbolSpec.pin(right_name, right_number, (20, 0), "Right"),
    )


def _resistor_symbol_spec(name: str) -> SchematicSymbolSpec:
    from .library import SchematicSymbolSpec

    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("1", 1, "2", 2),
        primitives=(
            SchematicSymbolSpec.terminal_lead((0, 0), (5, 0), terminal="start"),
            SchematicSymbolSpec.line((5, 0), (6.5, -3)),
            SchematicSymbolSpec.line((6.5, -3), (8, 3)),
            SchematicSymbolSpec.line((8, 3), (9.5, -3)),
            SchematicSymbolSpec.line((9.5, -3), (11, 3)),
            SchematicSymbolSpec.line((11, 3), (12.5, -3)),
            SchematicSymbolSpec.line((12.5, -3), (14, 3)),
            SchematicSymbolSpec.line((14, 3), (15, 0)),
            SchematicSymbolSpec.terminal_lead((15, 0), (20, 0), terminal="end"),
        ),
    )


def _capacitor_symbol_spec(name: str) -> SchematicSymbolSpec:
    from .library import SchematicSymbolSpec

    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("1", 1, "2", 2),
        primitives=(
            SchematicSymbolSpec.terminal_lead((0, 0), (8, 0), terminal="start"),
            SchematicSymbolSpec.line((8, -5), (8, 5)),
            SchematicSymbolSpec.line((12, -5), (12, 5)),
            SchematicSymbolSpec.terminal_lead((12, 0), (20, 0), terminal="end"),
        ),
    )


def _inductor_symbol_spec(name: str) -> SchematicSymbolSpec:
    from .library import SchematicSymbolSpec

    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("1", 1, "2", 2),
        primitives=(
            SchematicSymbolSpec.terminal_lead((0, 0), (4, 0), terminal="start"),
            SchematicSymbolSpec.arc((6, 0), 2, 180, -180),
            SchematicSymbolSpec.arc((10, 0), 2, 180, -180),
            SchematicSymbolSpec.arc((14, 0), 2, 180, -180),
            SchematicSymbolSpec.terminal_lead((16, 0), (20, 0), terminal="end"),
        ),
    )


def _diode_symbol_spec(name: str) -> SchematicSymbolSpec:
    from .library import SchematicSymbolSpec

    return SchematicSymbolSpec(
        name,
        pins=_two_terminal_pins("K", 1, "A", 2),
        primitives=(
            SchematicSymbolSpec.terminal_lead((0, 0), (7, 0), terminal="start"),
            SchematicSymbolSpec.line((7, -5), (7, 5)),
            SchematicSymbolSpec.line((7, -5), (13, 0)),
            SchematicSymbolSpec.line((7, 5), (13, 0)),
            SchematicSymbolSpec.terminal_lead((13, 0), (20, 0), terminal="end"),
        ),
    )


def _led_symbol_spec(name: str) -> SchematicSymbolSpec:
    from .library import SchematicSymbolSpec

    diode = _diode_symbol_spec(name)
    return SchematicSymbolSpec(
        name,
        pins=diode.pins,
        primitives=(
            *diode.primitives,
            SchematicSymbolSpec.line((13, -6), (17, -10)),
            SchematicSymbolSpec.line((15, -4), (19, -8)),
        ),
    )
