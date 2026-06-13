import hashlib
import json

import pytest

import volt
from project_framework_helpers import _header_1x02, _passive_0603


def _mark_populated(design, *references):
    for reference in references:
        design.component(reference).dnp(False)


def test_part_model_3d_requires_absolute_source_path():
    with pytest.raises(ValueError, match="absolute"):
        volt.PartModel3D("models/r_0603_body.glb")


def test_project_result_writes_part_model_assets_and_placement_transforms(tmp_path):
    asset_path = tmp_path / "resistor-body.glb"
    asset_bytes = b"placeholder-glb"
    asset_path.write_bytes(asset_bytes)
    asset_hash = hashlib.sha256(asset_bytes).hexdigest()

    project = volt.Project("model-bundle")

    @project.design
    def design():
        design = volt.Design("model-bundle")
        vcc = design.net("VCC", kind="power")
        mid = design.net("MID")
        gnd = design.net("GND", kind="ground")
        j1 = design.connector_1x02(ref="J1")
        r1 = design.R("330", ref="R1")
        r2 = design.R("1k", ref="R2")
        vcc += j1[1], r1[1]
        mid += r1[2], r2[1]
        gnd += r2[2], j1[2]

        design.component("J1").select_part(
            manufacturer="Generic",
            part_number="HDR-1x02",
            package="2.54mm-1x02",
            footprint=_header_1x02(),
            pin_pads={1: "1", 2: "2"},
        )
        for ref, part_number in (
            ("R1", "RC0603FR-07330RL"),
            ("R2", "RC0603FR-071KL"),
        ):
            design.component(ref).select_part(
                manufacturer="Yageo",
                part_number=part_number,
                package="0603",
                footprint=_passive_0603(("passives", "R_0603_1608Metric")),
                pin_pads={1: "1", 2: "2"},
                model_3d=volt.PartModel3D(
                    asset_path,
                    offset=(1.0, 2.0, 0.3),
                    rotation=30,
                ),
            )
        _mark_populated(design, "J1", "R1", "R2")
        return design

    @project.board
    def board(context):
        design = context.design()
        pcb = design.board("Main")
        front = pcb.add_layer("F.Cu", role="copper", side="top")
        back = pcb.add_layer("B.Cu", role="copper", side="bottom")
        pcb.set_layer_stack((front, back), thickness=1.6)
        pcb.set_rectangular_outline(origin=(0, 0), size=(24, 12))
        pcb.place(design.component("J1"), at=(4, 6), locked=True)
        pcb.place(design.component("R1"), at=(10, 5), rotation=90)
        pcb.place(design.component("R2"), at=(20, 5), rotation=180, side="bottom")
        return pcb

    output = tmp_path / "model-bundle.volt"
    project.run().write(output)

    manifest = json.loads((output / "manifest.volt.json").read_text(encoding="utf-8"))
    assert [artifact["path"] for artifact in manifest["artifacts"]] == [
        "logical/model-bundle.volt.json",
        "bom/bom.json",
        "bom/bom.csv",
        "pcb/Main.volt.pcb.json",
        "pcb/Main.svg",
        "pcb/Main.kicad_pcb",
        "assets/part_models_3d.json",
        f"assets/models/{asset_hash}.glb",
        "pcb/Main.volt.models3d.json",
        "diagnostics/diagnostics.json",
        "diagnostics/tests.json",
    ]

    registry = json.loads((output / "assets" / "part_models_3d.json").read_text(encoding="utf-8"))
    assert registry["assets"] == [
        {
            "id": "part_model_asset:0",
            "format": "glb",
            "path": f"assets/models/{asset_hash}.glb",
            "sha256": asset_hash,
        }
    ]
    assert registry["models"] == [
        {
            "id": "part_model:0",
            "asset": "part_model_asset:0",
            "file_name": "resistor-body.glb",
            "rotation_deg": 30.0,
            "translation_mm": [1.0, 2.0, 0.3],
        }
    ]
    assert (output / "assets" / "models" / f"{asset_hash}.glb").read_bytes() == asset_bytes

    placements = json.loads((output / "pcb" / "Main.volt.models3d.json").read_text(encoding="utf-8"))
    assert [placement["model"] for placement in placements["placements"]] == [
        "part_model:0",
        "part_model:0",
    ]
    assert placements["placements"] == [
        {
            "placement": "component_placement:1",
            "component": "component:1",
            "reference": "R1",
            "model": "part_model:0",
            "transform_matrix": [
                [-0.5, -0.8660254037844387, 0.0, 8.0],
                [0.8660254037844387, -0.5, 0.0, 6.0],
                [0.0, 0.0, 1.0, 1.1],
                [0.0, 0.0, 0.0, 1.0],
            ],
        },
        {
            "placement": "component_placement:2",
            "component": "component:2",
            "reference": "R2",
            "model": "part_model:0",
            "transform_matrix": [
                [0.8660254037844386, -0.5, 0.0, 21.0],
                [-0.5, -0.8660254037844386, 0.0, 3.0],
                [0.0, 0.0, -1.0, -1.1],
                [0.0, 0.0, 0.0, 1.0],
            ],
        },
    ]


def test_project_result_keeps_distinct_model_assets_with_same_hash(tmp_path):
    glb_asset = tmp_path / "shared-body.glb"
    step_asset = tmp_path / "shared-body.step"
    asset_bytes = b"same-model-payload"
    glb_asset.write_bytes(asset_bytes)
    step_asset.write_bytes(asset_bytes)
    asset_hash = hashlib.sha256(asset_bytes).hexdigest()

    project = volt.Project("model-metadata-collision")

    @project.design
    def design():
        design = volt.Design("model-metadata-collision")
        vcc = design.net("VCC", kind="power")
        gnd = design.net("GND", kind="ground")
        r1 = design.R("330", ref="R1")
        r2 = design.R("1k", ref="R2")
        vcc += r1[1], r2[1]
        gnd += r1[2], r2[2]
        design.component("R1").select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            pin_pads={1: "1", 2: "2"},
            model_3d=volt.PartModel3D(glb_asset),
        )
        design.component("R2").select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-071KL",
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            pin_pads={1: "1", 2: "2"},
            model_3d=volt.PartModel3D(step_asset),
        )
        _mark_populated(design, "R1", "R2")
        return design

    @project.board
    def board(context):
        design = context.design()
        pcb = design.board("Main")
        pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        pcb.place(design.component("R1"), at=(6, 5))
        pcb.place(design.component("R2"), at=(14, 5))
        return pcb

    output = tmp_path / "model-metadata-collision.volt"
    project.run().write(output)

    registry = json.loads((output / "assets" / "part_models_3d.json").read_text(encoding="utf-8"))
    assert registry["assets"] == [
        {
            "id": "part_model_asset:0",
            "format": "glb",
            "path": f"assets/models/{asset_hash}.glb",
            "sha256": asset_hash,
        },
        {
            "id": "part_model_asset:1",
            "format": "step",
            "path": f"assets/models/{asset_hash}.step",
            "sha256": asset_hash,
        },
    ]
    assert [model["asset"] for model in registry["models"]] == [
        "part_model_asset:0",
        "part_model_asset:1",
    ]


def test_project_result_viewer_profile_reports_missing_part_model_assets(tmp_path):
    header_asset = tmp_path / "header-body.glb"
    header_asset.write_bytes(b"header-glb")
    project = volt.Project("viewer-profile")

    @project.design
    def design():
        design = volt.Design("viewer-profile")
        vcc = design.net("VCC", kind="power")
        gnd = design.net("GND", kind="ground")
        j1 = design.connector_1x02(ref="J1")
        r1 = design.R("330", ref="R1")
        vcc += j1[1], r1[1]
        gnd += r1[2], j1[2]

        design.component("J1").select_part(
            manufacturer="Generic",
            part_number="HDR-1x02",
            package="2.54mm-1x02",
            footprint=_header_1x02(),
            pin_pads={1: "1", 2: "2"},
            model_3d=volt.PartModel3D(header_asset),
        )
        design.component("R1").select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            pin_pads={1: "1", 2: "2"},
            model_3d=volt.PartModel3D(
                tmp_path / "missing-body.glb",
                offset=(0.0, 0.0, 0.3),
            ),
        )
        _mark_populated(design, "J1", "R1")
        return design

    @project.board
    def board(context):
        design = context.design()
        pcb = design.board("Main")
        pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        pcb.place(design.component("J1"), at=(4, 5), locked=True)
        pcb.place(design.component("R1"), at=(10, 5))
        return pcb

    output = tmp_path / "viewer-profile.volt"
    project.run().write(output, profile="viewer")

    diagnostics = json.loads((output / "diagnostics" / "diagnostics.json").read_text(encoding="utf-8"))
    assert diagnostics["summary"]["errors"] == 1
    assert [diagnostic["code"] for diagnostic in diagnostics["diagnostics"]] == [
        "PROJECT_PART_MODEL_3D_MISSING",
    ]


def test_project_result_viewer_profile_honors_expected_model_diagnostics(tmp_path):
    project = volt.Project("expected-viewer-profile")
    project.expect_diagnostic(code="PROJECT_PART_MODEL_3D_MISSING", stage="bundle")

    @project.design
    def design():
        design = volt.Design("expected-viewer-profile")
        vcc = design.net("VCC", kind="power")
        gnd = design.net("GND", kind="ground")
        j1 = design.connector_1x02(ref="J1")
        r1 = design.R("330", ref="R1")
        vcc += j1[1], r1[1]
        gnd += r1[2], j1[2]
        design.component("J1").select_part(
            manufacturer="Generic",
            part_number="HDR-1x02",
            package="2.54mm-1x02",
            footprint=_header_1x02(),
            pin_pads={1: "1", 2: "2"},
        )
        design.component("R1").select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            pin_pads={1: "1", 2: "2"},
            model_3d=volt.PartModel3D(tmp_path / "missing-body.glb"),
        )
        _mark_populated(design, "J1", "R1")
        return design

    @project.board
    def board(context):
        design = context.design()
        pcb = design.board("Main")
        pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        pcb.place(design.component("J1"), at=(4, 5), locked=True)
        pcb.place(design.component("R1"), at=(10, 5))
        return pcb

    result = project.run()
    output = tmp_path / "expected-viewer-profile.volt"
    result.write(output, profile="viewer")

    manifest = json.loads((output / "manifest.volt.json").read_text(encoding="utf-8"))
    diagnostics = json.loads((output / "diagnostics" / "diagnostics.json").read_text(encoding="utf-8"))

    assert manifest["ok"] is True
    assert manifest["status"] == "expected-diagnostics"
    assert [item["code"] for item in diagnostics["expected"]] == ["PROJECT_PART_MODEL_3D_MISSING"]
    assert diagnostics["unexpected"] == []
    assert diagnostics["missing_expected"] == []


def test_project_result_default_profile_keeps_part_models_optional(tmp_path):
    header_asset = tmp_path / "header-body.glb"
    header_asset.write_bytes(b"header-glb")
    project = volt.Project("default-profile")

    @project.design
    def design():
        design = volt.Design("default-profile")
        vcc = design.net("VCC", kind="power")
        gnd = design.net("GND", kind="ground")
        j1 = design.connector_1x02(ref="J1")
        r1 = design.R("330", ref="R1")
        vcc += j1[1], r1[1]
        gnd += r1[2], j1[2]

        design.component("J1").select_part(
            manufacturer="Generic",
            part_number="HDR-1x02",
            package="2.54mm-1x02",
            footprint=_header_1x02(),
            pin_pads={1: "1", 2: "2"},
            model_3d=volt.PartModel3D(header_asset),
        )
        design.component("R1").select_part(
            manufacturer="Yageo",
            part_number="RC0603FR-07330RL",
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            pin_pads={1: "1", 2: "2"},
            model_3d=volt.PartModel3D(
                tmp_path / "missing-body.glb",
                offset=(0.0, 0.0, 0.3),
            ),
        )
        _mark_populated(design, "J1", "R1")
        return design

    @project.board
    def board(context):
        design = context.design()
        pcb = design.board("Main")
        pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        pcb.place(design.component("J1"), at=(4, 5), locked=True)
        pcb.place(design.component("R1"), at=(10, 5))
        return pcb

    output = tmp_path / "default-profile.volt"
    project.run().write(output)

    diagnostics = json.loads((output / "diagnostics" / "diagnostics.json").read_text(encoding="utf-8"))
    assert diagnostics["summary"]["errors"] == 0
    assert [
        diagnostic["code"]
        for diagnostic in diagnostics["diagnostics"]
        if diagnostic["code"] == "PROJECT_PART_MODEL_3D_MISSING"
    ] == []
