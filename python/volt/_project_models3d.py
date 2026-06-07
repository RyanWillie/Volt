"""Typed 3D part-model bundle materialization helpers for project results."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass
from math import cos, radians, sin
from pathlib import Path
from typing import Iterable

from .pcb import Board, _BoardPlacementRef


@dataclass(frozen=True)
class MissingPartModel3D:
    """Viewer-profile issue for a placed component lacking bundle-ready model data."""

    board: Board
    reference: str
    message: str


@dataclass(frozen=True)
class MaterializedPartModel3DAsset:
    """Deduplicated copied asset payload for the project bundle."""

    id: str
    format: str
    suffix: str
    sha256: str
    payload: bytes


@dataclass(frozen=True)
class MaterializedPartModel3D:
    """Reusable selected-part model record for bundle consumers."""

    id: str
    asset: str
    file_name: str
    translation_mm: tuple[float, float, float]
    rotation_deg: float


@dataclass(frozen=True)
class MaterializedPartModel3DPlacement:
    """Viewer-ready placed component model reference and transform."""

    placement: int
    component: int
    reference: str
    model: str
    transform_matrix: list[list[float]]


@dataclass(frozen=True)
class MaterializedPartModel3DBoard:
    """Bundle-ready placement records for one board projection."""

    board: Board
    output_name: str
    placements: tuple[MaterializedPartModel3DPlacement, ...]


@dataclass(frozen=True)
class MaterializedPartModel3DBundle:
    """All 3D model records needed to write one project bundle."""

    assets: tuple[MaterializedPartModel3DAsset, ...]
    models: tuple[MaterializedPartModel3D, ...]
    boards: tuple[MaterializedPartModel3DBoard, ...]
    missing: tuple[MissingPartModel3D, ...]


def collect_project_part_models_3d(
    boards: Iterable[tuple[Board, str]],
    *,
    profile: str,
) -> MaterializedPartModel3DBundle:
    """Collect typed 3D model bundle records from authored boards and selected parts."""
    assets_by_hash: dict[str, MaterializedPartModel3DAsset] = {}
    models_by_key: dict[tuple[object, ...], str] = {}
    assets: list[MaterializedPartModel3DAsset] = []
    models: list[MaterializedPartModel3D] = []
    board_outputs: list[MaterializedPartModel3DBoard] = []
    missing: list[MissingPartModel3D] = []

    for board, output_name in boards:
        placements: list[MaterializedPartModel3DPlacement] = []
        for placement in board._placements():
            model_3d = board._design._selected_part_model_3d(placement.component)
            if model_3d is None:
                if profile == "viewer":
                    missing.append(
                        MissingPartModel3D(
                            board=board,
                            reference=board._design._component_reference(placement.component),
                            message="Placed component has no selected-part 3D model declaration",
                        )
                    )
                continue

            source_path = board._design._component_model_3d_asset_source(placement.component)
            if source_path is None or not source_path.is_file():
                if profile == "viewer":
                    missing.append(
                        MissingPartModel3D(
                            board=board,
                            reference=board._design._component_reference(placement.component),
                            message=(
                                "Placed component 3D model asset is missing from local project "
                                "materialization"
                            ),
                        )
                    )
                continue

            asset_bytes = source_path.read_bytes()
            asset_hash = hashlib.sha256(asset_bytes).hexdigest()
            asset = assets_by_hash.get(asset_hash)
            if asset is None:
                asset = MaterializedPartModel3DAsset(
                    id=f"part_model_asset:{len(assets_by_hash)}",
                    format=model_3d.format,
                    suffix=source_path.suffix.lower(),
                    sha256=asset_hash,
                    payload=asset_bytes,
                )
                assets_by_hash[asset_hash] = asset
                assets.append(asset)

            model_key = (
                asset.id,
                model_3d.file_name,
                model_3d.translation_mm,
                model_3d.rotation_deg,
            )
            model_id = models_by_key.get(model_key)
            if model_id is None:
                model_id = f"part_model:{len(models_by_key)}"
                models_by_key[model_key] = model_id
                models.append(
                    MaterializedPartModel3D(
                        id=model_id,
                        asset=asset.id,
                        file_name=model_3d.file_name,
                        translation_mm=model_3d.translation_mm,
                        rotation_deg=model_3d.rotation_deg,
                    )
                )

            placements.append(
                MaterializedPartModel3DPlacement(
                    placement=placement.index,
                    component=placement.component,
                    reference=board._design._component_reference(placement.component),
                    model=model_id,
                    transform_matrix=_part_model_3d_transform_matrix(
                        placement=placement,
                        translation_mm=model_3d.translation_mm,
                        rotation_deg=model_3d.rotation_deg,
                        surface_z=board._surface_z(placement.side),
                    ),
                )
            )

        if placements:
            board_outputs.append(
                MaterializedPartModel3DBoard(
                    board=board,
                    output_name=output_name,
                    placements=tuple(placements),
                )
            )

    return MaterializedPartModel3DBundle(
        assets=tuple(assets),
        models=tuple(models),
        boards=tuple(board_outputs),
        missing=tuple(missing),
    )


def _part_model_3d_transform_matrix(
    *,
    placement: _BoardPlacementRef,
    translation_mm: tuple[float, float, float],
    rotation_deg: float,
    surface_z: float,
) -> list[list[float]]:
    px, py = placement.position
    tx, ty, tz = translation_mm
    transform = _matrix_translate(px, py, surface_z)
    transform = _matrix_multiply(transform, _matrix_rotate_z(placement.rotation_deg))
    if placement.side == "bottom":
        transform = _matrix_multiply(transform, _matrix_scale(-1.0, 1.0, -1.0))
    transform = _matrix_multiply(transform, _matrix_translate(tx, ty, tz))
    transform = _matrix_multiply(transform, _matrix_rotate_z(rotation_deg))
    return [[_normalized_matrix_value(value) for value in row] for row in transform]


def _matrix_translate(x: float, y: float, z: float) -> list[list[float]]:
    return [
        [1.0, 0.0, 0.0, x],
        [0.0, 1.0, 0.0, y],
        [0.0, 0.0, 1.0, z],
        [0.0, 0.0, 0.0, 1.0],
    ]


def _matrix_rotate_z(angle_deg: float) -> list[list[float]]:
    angle = radians(angle_deg)
    return [
        [cos(angle), -sin(angle), 0.0, 0.0],
        [sin(angle), cos(angle), 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]


def _matrix_scale(x: float, y: float, z: float) -> list[list[float]]:
    return [
        [x, 0.0, 0.0, 0.0],
        [0.0, y, 0.0, 0.0],
        [0.0, 0.0, z, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ]


def _matrix_multiply(left: list[list[float]], right: list[list[float]]) -> list[list[float]]:
    return [
        [
            sum(left[row][index] * right[index][column] for index in range(4))
            for column in range(4)
        ]
        for row in range(4)
    ]


def _normalized_matrix_value(value: float) -> float:
    if abs(value) < 1e-12:
        return 0.0
    half_step = round(value * 2.0) / 2.0
    if abs(value - half_step) < 1e-12:
        return half_step
    return round(value, 16)
