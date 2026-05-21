"""Sheet metadata helpers for schematic authoring."""

from __future__ import annotations

from ._utils import _coordinate, _positive_coordinate


def _sheet_orientation(value: str) -> str:
    if not isinstance(value, str):
        raise TypeError("Schematic sheet orientation must be a string")
    normalized = value.casefold()
    if normalized == "portrait":
        return "Portrait"
    if normalized == "landscape":
        return "Landscape"
    raise ValueError("Schematic sheet orientation must be portrait or landscape")


def _schematic_sheet_size(value, orientation: str) -> dict[str, float]:
    if isinstance(value, str):
        if value.casefold() != "a4":
            raise ValueError("Schematic sheet size names must be A4")
        if orientation == "Portrait":
            return {"width": 210.0, "height": 297.0}
        return {"width": 297.0, "height": 210.0}
    if isinstance(value, dict):
        return {
            "width": _positive_coordinate(value["width"], "Schematic sheet widths"),
            "height": _positive_coordinate(value["height"], "Schematic sheet heights"),
        }
    if isinstance(value, (tuple, list)) and len(value) == 2:
        return {
            "width": _positive_coordinate(value[0], "Schematic sheet widths"),
            "height": _positive_coordinate(value[1], "Schematic sheet heights"),
        }
    raise TypeError("Schematic sheet size must be A4, a (width, height) pair, or a dict")


def _title_block_value(value, label: str) -> str:
    if not isinstance(value, (str, int)):
        raise TypeError(f"Schematic title-block {label} must be a string or integer")
    result = str(value)
    if not result:
        raise ValueError(f"Schematic title-block {label} must not be empty")
    return result


def _optional_text_font_size(value: float | None) -> float | None:
    if value is None:
        return None
    return _positive_coordinate(value, "Schematic text font sizes")


def _title_block_items(values) -> list[dict[str, str]]:
    if values is None:
        return []
    if isinstance(values, dict):
        iterable = values.items()
    else:
        iterable = values
    result: list[dict[str, str]] = []
    for item in iterable:
        if isinstance(item, dict):
            key = item["key"]
            value = item["value"]
        else:
            key, value = item
        if not isinstance(key, str):
            raise TypeError("Schematic title-block keys must be strings")
        if not key:
            raise ValueError("Schematic title-block keys must not be empty")
        result.append({"key": key, "value": _title_block_value(value, key)})
    return result


def _sheet_margins(value) -> dict[str, float]:
    if isinstance(value, dict):
        return {
            "left": _coordinate(value["left"]),
            "top": _coordinate(value["top"]),
            "right": _coordinate(value["right"]),
            "bottom": _coordinate(value["bottom"]),
        }
    if isinstance(value, (int, float)) and not isinstance(value, bool):
        margin = _coordinate(value)
        return {"left": margin, "top": margin, "right": margin, "bottom": margin}
    if isinstance(value, (tuple, list)) and len(value) == 4:
        return {
            "left": _coordinate(value[0]),
            "top": _coordinate(value[1]),
            "right": _coordinate(value[2]),
            "bottom": _coordinate(value[3]),
        }
    raise TypeError("Schematic sheet margins must be a number, four-value tuple, or dict")


def _positive_count(value, label: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError(f"{label} must be an integer")
    if value <= 0:
        raise ValueError(f"{label} must be positive")
    return value


def _visibility_flag(value, label: str) -> bool:
    if not isinstance(value, bool):
        raise TypeError(f"{label} must be a boolean")
    return value


def _coordinate_zones(value) -> dict[str, int | bool]:
    if isinstance(value, dict):
        return {
            "columns": _positive_count(value["columns"], "Coordinate zone columns"),
            "rows": _positive_count(value["rows"], "Coordinate zone rows"),
            "visible": _visibility_flag(
                value.get("visible", True), "Coordinate zone visibility"
            ),
        }
    if isinstance(value, (tuple, list)) and len(value) == 2:
        return {
            "columns": _positive_count(value[0], "Coordinate zone columns"),
            "rows": _positive_count(value[1], "Coordinate zone rows"),
            "visible": True,
        }
    raise TypeError("Coordinate zones must be a (columns, rows) pair or dict")


def _sheet_grid(value) -> dict[str, float | bool]:
    if isinstance(value, dict):
        return {
            "spacing": _positive_coordinate(value["spacing"], "Schematic grid spacing"),
            "visible": _visibility_flag(
                value.get("visible", True), "Schematic grid visibility"
            ),
        }
    return {"spacing": _positive_coordinate(value, "Schematic grid spacing"), "visible": True}


def _schematic_sheet_metadata(
    name: str,
    *,
    size,
    orientation,
    title,
    number,
    page_count,
    revision,
    date,
    project,
    file,
    title_block,
    margins,
    coordinate_zones,
    grid,
) -> dict:
    metadata: dict = {}
    orientation_value: str | None = None
    if orientation is not None:
        orientation_value = _sheet_orientation(orientation)
        metadata["orientation"] = orientation_value
    if size is not None:
        orientation_value = orientation_value or "Landscape"
        metadata["orientation"] = orientation_value
        metadata["size"] = _schematic_sheet_size(size, orientation_value)
    if title is not None:
        if not isinstance(title, str):
            raise TypeError("Schematic sheet titles must be strings")
        if not title:
            raise ValueError("Schematic sheet titles must not be empty")
        metadata["title"] = title

    fields = []
    for key, value in (
        ("Number", number),
        ("Page Count", page_count),
        ("Revision", revision),
        ("Date", date),
        ("Project", project),
        ("File", file),
    ):
        if value is not None:
            fields.append({"key": key, "value": _title_block_value(value, key)})
    fields.extend(_title_block_items(title_block))
    if fields:
        metadata["title_block"] = fields
    if margins is not None:
        metadata["frame"] = {"visible": True, "margins": _sheet_margins(margins)}
    if coordinate_zones is not None:
        metadata["coordinate_zones"] = _coordinate_zones(coordinate_zones)
    if grid is not None:
        metadata["grid"] = _sheet_grid(grid)
    return metadata


def _schematic_svg_page_filename(name: str) -> str:
    safe = "".join(
        character
        if character.isascii() and (character.isalnum() or character in "._-")
        else "_"
        for character in name
    ).strip("._")
    return safe or "sheet"
