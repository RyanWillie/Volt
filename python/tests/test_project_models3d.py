import hashlib
import json

import pytest

import volt
from project_framework_helpers import _delivery_profile, _header_1x02, _passive_0603


def _model_part(
    library,
    name,
    *,
    manufacturer,
    mpn,
    package,
    footprint,
    model_3d=None,
    prefix="R",
    value=None,
):
    return library.part(
        name,
        pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
        manufacturer=manufacturer,
        mpn=mpn,
        package=package,
        footprint=footprint,
        pads={1: "1", 2: "2"},
        model_3d=model_3d,
        prefix=prefix,
        value=value,
    )


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
        library = volt.Library("volt.tests.models3d", version="1.0.0")
        header = _model_part(
            library,
            "Header-1x02",
            manufacturer="Generic",
            mpn="HDR-1x02",
            package="2.54mm-1x02",
            footprint=_header_1x02(),
            prefix="J",
        )
        model = volt.PartModel3D(
            asset_path,
            offset=(1.0, 2.0, 0.3),
            rotation=30,
        )
        r330 = _model_part(
            library,
            "Resistor-330R",
            manufacturer="Yageo",
            mpn="RC0603FR-07330RL",
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            model_3d=model,
            value="330",
        )
        r1k = _model_part(
            library,
            "Resistor-1K",
            manufacturer="Yageo",
            mpn="RC0603FR-071KL",
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            model_3d=model,
            value="1k",
        )
        vcc = design.net("VCC", kind="power")
        mid = design.net("MID")
        gnd = design.net("GND", kind="ground")
        j1 = design.instantiate(header, ref="J1")
        r1 = design.instantiate(r330, ref="R1")
        r2 = design.instantiate(r1k, ref="R2")
        vcc += j1[1], r1[1]
        mid += r1[2], r2[1]
        gnd += r2[2], j1[2]
        j1.dnp(False)
        r1.dnp(False)
        r2.dnp(False)
        return design

    @project.board
    def board(context):
        design = context.design()
        pcb = design.add_board("Main")
        pcb.set_capability_profile(_delivery_profile())
        front = pcb.add_layer("F.Cu", role="copper", side="top")
        back = pcb.add_layer("B.Cu", role="copper", side="bottom")
        pcb.set_layer_stack((front, back), thickness=1.6)
        pcb.set_rectangular_outline(origin=(0, 0), size=(24, 12))
        pcb.place(design.component("J1"), at=(4, 6), locked=True)
        pcb.place(design.component("R1"), at=(10, 5), rotation=90)
        pcb.place(design.component("R2"), at=(20, 5), rotation=180, side="bottom")
        return pcb

    output = tmp_path / "model-bundle.volt"
    project.run().write(output, profile="viewer")

    manifest = json.loads((output / "manifest.volt.json").read_text(encoding="utf-8"))
    glb = next(
        artifact for artifact in manifest["artifacts"] if artifact["kind"] == "glb_asset"
    )
    scene = next(
        artifact for artifact in manifest["artifacts"] if artifact["kind"] == "board_scene"
    )
    compiled = next(
        artifact
        for artifact in manifest["artifacts"]
        if artifact["kind"] == "compiled_board"
    )
    assert (output / glb["path"]).read_bytes() == asset_bytes
    assert glb["content_digest"] == f"sha256:{asset_hash}"
    assert glb["id"] in [
        dependency["artifact"] for dependency in compiled["depends_on"]
    ]
    assert {
        dependency["artifact"]["kind"] for dependency in scene["depends_on"]
    } == {"compiled_board", "glb_asset"}
    assert [
        dependency["artifact"]
        for dependency in scene["depends_on"]
        if dependency["artifact"]["kind"] == "glb_asset"
    ] == [glb["id"]]
    scene_payload = json.loads((output / scene["path"]).read_text(encoding="utf-8"))
    assert scene_payload["models"] == [{"digest": f"sha256:{asset_hash}"}]
    assert [placement["reference"] for placement in scene_payload["placements"]] == [
        "J1",
        "R1",
        "R2",
    ]
    assert [
        placement["model_digest"]
        for placement in scene_payload["placements"]
        if placement["model_digest"] is not None
    ] == [f"sha256:{asset_hash}", f"sha256:{asset_hash}"]


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
        library = volt.Library("volt.tests.models3d.collision", version="1.0.0")
        r330 = _model_part(
            library,
            "Resistor-330R",
            manufacturer="Yageo",
            mpn="RC0603FR-07330RL",
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            model_3d=volt.PartModel3D(glb_asset),
            value="330",
        )
        r1k = _model_part(
            library,
            "Resistor-1K",
            manufacturer="Yageo",
            mpn="RC0603FR-071KL",
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            model_3d=volt.PartModel3D(step_asset),
            value="1k",
        )
        vcc = design.net("VCC", kind="power")
        gnd = design.net("GND", kind="ground")
        r1 = design.instantiate(r330, ref="R1")
        r2 = design.instantiate(r1k, ref="R2")
        vcc += r1[1], r2[1]
        gnd += r1[2], r2[2]
        r1.dnp(False)
        r2.dnp(False)
        return design

    @project.board
    def board(context):
        design = context.design()
        pcb = design.add_board("Main")
        pcb.set_capability_profile(_delivery_profile())
        pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        pcb.place(design.component("R1"), at=(6, 5))
        pcb.place(design.component("R2"), at=(14, 5))
        return pcb

    output = tmp_path / "model-metadata-collision.volt"
    project.run().write(output, profile="viewer")

    manifest = json.loads((output / "manifest.volt.json").read_text(encoding="utf-8"))
    glbs = [
        artifact for artifact in manifest["artifacts"] if artifact["kind"] == "glb_asset"
    ]
    assert len(glbs) == 1
    assert glbs[0]["content_digest"] == f"sha256:{asset_hash}"
    assert "step_asset" not in {artifact["kind"] for artifact in manifest["artifacts"]}


def test_missing_part_model_asset_is_rejected_at_exact_part_admission(tmp_path):
    design = volt.Design("model-admission")
    library = volt.Library("volt.tests.models3d.missing", version="1.0.0")
    resistor = _model_part(
        library,
        "Resistor-330R",
        manufacturer="Yageo",
        mpn="RC0603FR-07330RL",
        package="0603",
        footprint=_passive_0603(("passives", "R_0603_1608Metric")),
        model_3d=volt.PartModel3D(tmp_path / "missing-body.glb"),
        value="330",
    )

    with pytest.raises(FileNotFoundError):
        design.instantiate(resistor, ref="R1")

    assert json.loads(design.to_json())["components"] == []


def test_project_result_default_profile_keeps_absent_part_models_optional(tmp_path):
    header_asset = tmp_path / "header-body.glb"
    header_asset.write_bytes(b"header-glb")
    project = volt.Project("default-profile")

    @project.design
    def design():
        design = volt.Design("default-profile")
        library = volt.Library("volt.tests.models3d.default", version="1.0.0")
        header = _model_part(
            library,
            "Header-1x02",
            manufacturer="Generic",
            mpn="HDR-1x02",
            package="2.54mm-1x02",
            footprint=_header_1x02(),
            model_3d=volt.PartModel3D(header_asset),
            prefix="J",
        )
        resistor = _model_part(
            library,
            "Resistor-330R",
            manufacturer="Yageo",
            mpn="RC0603FR-07330RL",
            package="0603",
            footprint=_passive_0603(("passives", "R_0603_1608Metric")),
            value="330",
        )
        vcc = design.net("VCC", kind="power")
        gnd = design.net("GND", kind="ground")
        j1 = design.instantiate(header, ref="J1")
        r1 = design.instantiate(resistor, ref="R1")
        vcc += j1[1], r1[1]
        gnd += r1[2], j1[2]
        j1.dnp(False)
        r1.dnp(False)
        return design

    @project.board
    def board(context):
        design = context.design()
        pcb = design.add_board("Main")
        pcb.set_capability_profile(_delivery_profile())
        pcb.set_rectangular_outline(origin=(0, 0), size=(20, 10))
        pcb.place(design.component("J1"), at=(4, 5), locked=True)
        pcb.place(design.component("R1"), at=(10, 5))
        return pcb

    output = tmp_path / "default-profile.volt"
    project.run().write(output)

    manifest = json.loads((output / "manifest.volt.json").read_text(encoding="utf-8"))
    diagnostics_path = next(
        artifact["path"]
        for artifact in manifest["artifacts"]
        if artifact["kind"] == "diagnostics"
    )
    diagnostics = json.loads((output / diagnostics_path).read_text(encoding="utf-8"))
    assert diagnostics["summary"]["errors"] == 0
    assert [
        diagnostic["code"]
        for diagnostic in diagnostics["diagnostics"]
        if diagnostic["code"] == "PROJECT_PART_MODEL_3D_MISSING"
    ] == []
    assert "glb_asset" not in {
        artifact["kind"] for artifact in manifest["artifacts"]
    }
