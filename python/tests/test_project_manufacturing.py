import hashlib
import importlib
import json
import os
import sys
import zipfile
from pathlib import Path

import pytest
import volt


def _write_project(
    root: Path,
    entrypoint: str = "project_entry:main",
    extra_config: str = "",
) -> None:
    root.mkdir(parents=True, exist_ok=True)
    root.joinpath("volt.toml").write_text(
        f"""[project]
entrypoint = "{entrypoint}"

[paths]
{extra_config}
""",
        encoding="utf-8",
    )


def _write_entrypoint(root: Path, body: str) -> None:
    root.joinpath("project_entry.py").write_text(body, encoding="utf-8")
    sys.modules.pop("project_entry", None)


def _run_project_direct(root: Path) -> volt.ProjectResult:
    previous_cwd = Path.cwd()
    sys.path.insert(0, str(root))
    sys.modules.pop("project_entry", None)
    os.chdir(root)
    try:
        module = importlib.import_module("project_entry")
        result = module.main()
    finally:
        os.chdir(previous_cwd)
        sys.modules.pop("project_entry", None)
        sys.path.remove(str(root))
    assert isinstance(result, volt.ProjectResult)
    return result


def _manufacturing_profile_metadata(root: Path) -> dict[str, str]:
    path = root / "profiles" / "generic.volt.json"
    return {
        "path": "profiles/generic.volt.json",
        "resolved_path": str(path),
    }


def _write_manufacturing_profile(root: Path) -> None:
    profile = {
        "format": "volt.capability_profile",
        "version": 1,
        "profile": {
            "name": "Generic 2-layer manufacturing test profile",
            "provenance": {
                "source": "Volt CLI manufacturing export test fixture",
                "as_of": "2026-06-21",
            },
            "minimum_track_width_mm": 0.2,
            "minimum_via_drill_mm": 0.3,
            "minimum_via_annular_mm": 0.6,
            "supported_copper_layer_counts": [2],
            "board_thickness_range_mm": {"minimum_mm": 0.8, "maximum_mm": 2.0},
            "available_copper_weights_oz": [1.0],
            "drill_diameter_range_mm": {"minimum_mm": 0.3, "maximum_mm": 6.0},
            "minimum_clearances": [
                {"first": "track", "second": "track", "clearance_mm": 0.2},
                {"first": "track", "second": "pad", "clearance_mm": 0.2},
            ],
        },
    }
    profile_path = root / "profiles" / "generic.volt.json"
    profile_path.parent.mkdir(parents=True, exist_ok=True)
    profile_path.write_text(
        json.dumps(profile, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def _write_manufacturing_entrypoint(
    root: Path,
    *,
    lossy: bool = False,
    board_profile: bool = True,
) -> None:
    lossy_feature = (
        """
        board.add(
            volt.Hole(
                center=(8.0, 24.0),
                diameter=2.4,
                role="mounting",
                label="FH1",
                finished_diameter=2.0,
            )
        )
"""
        if lossy
        else ""
    )
    profile_setup = (
        '    profile = volt.CapabilityProfile.from_file(Path("profiles/generic.volt.json"))\n'
        if board_profile
        else ""
    )
    board_profile_call = "        board.set_capability_profile(profile)\n" if board_profile else ""
    _write_entrypoint(
        root,
        f"""from pathlib import Path

import volt


def _rect_0603(ref):
    return volt.Footprint(
        ref,
        pads=(
            volt.FootprintPad.surface_mount(
                "1",
                at=(-0.75, 0.0),
                size=(0.8, 0.95),
                shape="rectangle",
            ),
            volt.FootprintPad.surface_mount(
                "2",
                at=(0.75, 0.0),
                size=(0.8, 0.95),
                shape="rectangle",
            ),
        ),
    )


def _header_1x02():
    return volt.Footprint(
        ("connectors", "PinHeader_1x02_P2.54mm"),
        pads=(
            volt.FootprintPad.through_hole(
                "1",
                at=(0.0, -1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
            volt.FootprintPad.through_hole(
                "2",
                at=(0.0, 1.27),
                size=(1.7, 1.7),
                drill=volt.FootprintDrill(1.0),
            ),
        ),
    )


def _net(design, name):
    return next(net for net in design.nets() if net.name == name)


def main():
{profile_setup}
    project = volt.Project("status-led")

    @project.design
    def design():
        design = volt.Design("status-led")
        library = volt.Library("volt.tests.cli.manufacturing", version="1.0.0")
        header = library.part(
            "Header-1x02",
            pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
            manufacturer="Generic",
            mpn="HDR-1x02",
            package="2.54mm-1x02",
            footprint=volt.Footprint(
                ("test", "Header1x02"),
                pads=_header_1x02().pads,
            ),
            pads={{1: "1", 2: "2"}},
            prefix="J",
        )
        resistor = library.part(
            "Resistor-330R",
            pins=(volt.PinSpec("1", 1), volt.PinSpec("2", 2)),
            manufacturer="Yageo",
            mpn="RC0603FR-07330RL",
            package="0603",
            footprint=_rect_0603(("test", "RectR0603")),
            pads={{1: "1", 2: "2"}},
            prefix="R",
            value="330",
        )
        led = library.part(
            "Status-LED",
            pins=(volt.PinSpec("A", 1), volt.PinSpec("K", 2)),
            manufacturer="Lite-On",
            mpn="LTST-C190KRKT",
            package="0603",
            footprint=_rect_0603(("test", "RectD0603")),
            pads={{"A": "1", "K": "2"}},
            prefix="D",
        )
        vcc = design.net("VCC", kind="power")
        led_a = design.net("LED_A")
        gnd = design.net("GND", kind="ground")
        j1 = design.instantiate(header, ref="J1")
        r1 = design.instantiate(resistor, ref="R1")
        d1 = design.instantiate(led, ref="D1")
        vcc += j1[1], r1[1]
        led_a += r1[2], d1["A"]
        gnd += d1["K"], j1[2]
        for component in (j1, r1, d1):
            component.dnp(False)
        return design

    @project.board
    def board(context):
        [design] = context.designs
        board = design.add_board("Control")
{board_profile_call}
        front = board.add_layer("F.Cu", role="copper", side="top")
        back = board.add_layer("B.Cu", role="copper", side="bottom")
        silk = board.add_layer("F.SilkS", role="silkscreen", side="top")
        board.set_layer_stack((front, back), thickness=1.6)
        board.set_design_rules(
            copper_clearance=0.25,
            min_track_width=0.25,
            min_via_drill=0.35,
            min_via_annular=0.7,
        )
        board.set_rectangular_outline(origin=(0.0, 0.0), size=(50.0, 30.0))
        board.add(volt.Hole(center=(3.0, 3.0), diameter=3.2, role="mounting", label="MH1"))
        board.place(design.component("J1"), at=(6.0, 15.0), locked=True)
        board.place(design.component("R1"), at=(18.0, 15.0))
        board.place(design.component("D1"), at=(28.0, 15.0), rotation=180.0)
        vcc = _net(design, "VCC")
        led_a = _net(design, "LED_A")
        gnd = _net(design, "GND")
        board.add_track(
            vcc,
            layer=front,
            points=((6.0, 13.73), (12.0, 12.0), (17.25, 15.0)),
            width=0.25,
        )
        board.add_track(
            led_a,
            layer=front,
            points=((18.75, 15.0), (23.0, 12.0), (28.75, 12.0), (28.75, 15.0)),
            width=0.25,
        )
        board.add_track(
            gnd,
            layer=back,
            points=((6.0, 16.27), (23.0, 20.0)),
            width=0.25,
        )
        board.add_track(
            gnd,
            layer=front,
            points=((23.0, 20.0), (27.25, 15.0)),
            width=0.25,
        )
        board.add_via(
            gnd,
            at=(23.0, 20.0),
            start_layer=front,
            end_layer=back,
            drill=0.35,
            annular=0.75,
        )
        board.add_text("REV A", at=(4.0, 27.0), layer=silk, size=1.0)
{lossy_feature}
        return board

    return project.run()
""",
    )


def _directory_bytes(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


def _write_manufacturing_project(
    root: Path,
    *,
    lossy: bool = False,
    board_profile: bool = True,
    config_profile: bool = True,
) -> None:
    _write_project(
        root,
        extra_config=(
            """
[manufacturing]
profile = "profiles/generic.volt.json"
"""
            if config_profile
            else ""
        ),
    )
    _write_manufacturing_profile(root)
    _write_manufacturing_entrypoint(root, lossy=lossy, board_profile=board_profile)


def test_project_result_manufacturing_package_writes_exact_native_handoff(
    tmp_path, monkeypatch
):
    def unexpected_dc_execution(*args, **kwargs):
        pytest.fail("manufacturing must not compile or solve an electrical model")

    monkeypatch.setattr(volt, "compile_electrical", unexpected_dc_execution)
    monkeypatch.setattr(volt, "solve_dc", unexpected_dc_execution)
    root = tmp_path / "board"
    direct_output = tmp_path / "direct-package"
    _write_manufacturing_project(root)

    result = _run_project_direct(root)
    written = result.write_manufacturing_package(
        direct_output,
        manufacturing_profile=_manufacturing_profile_metadata(root),
        archive=True,
    )

    assert written.status == "clean"
    assert written.output == direct_output
    assert written.archive == direct_output.with_suffix(".zip")
    assert {key: written.board[key] for key in ("design", "name", "output_name")} == {
        "design": "status-led",
        "name": "Control",
        "output_name": "Control",
    }
    assert written.board["compiled_board_provenance_digest"].startswith("sha256:")


def test_project_result_manufacturing_package_rerun_is_deterministic(tmp_path):
    root = tmp_path / "board"
    output = tmp_path / "direct-package"
    _write_manufacturing_project(root)
    result = _run_project_direct(root)

    result.write_manufacturing_package(
        output,
        manufacturing_profile=_manufacturing_profile_metadata(root),
        archive=True,
    )
    first = _directory_bytes(output)
    first_archive = output.with_suffix(".zip").read_bytes()

    repeated = result.write_manufacturing_package(
        output,
        manufacturing_profile=_manufacturing_profile_metadata(root),
        archive=True,
    )

    assert repeated.archive == output.with_suffix(".zip")
    assert _directory_bytes(output) == first
    assert output.with_suffix(".zip").read_bytes() == first_archive


def test_project_result_manufacturing_package_archive_false_removes_stale_archive(
    tmp_path,
):
    root = tmp_path / "board"
    output = tmp_path / "direct-package"
    _write_manufacturing_project(root)
    result = _run_project_direct(root)

    result.write_manufacturing_package(
        output,
        manufacturing_profile=_manufacturing_profile_metadata(root),
        archive=True,
    )
    archive = output.with_suffix(".zip")
    assert archive.exists()

    without_archive = result.write_manufacturing_package(
        output,
        manufacturing_profile=_manufacturing_profile_metadata(root),
        archive=False,
    )

    assert without_archive.archive is None
    assert output.exists()
    assert not archive.exists()


def test_project_result_manufacturing_package_refuses_fab_critical_loss(tmp_path):
    root = tmp_path / "board"
    output = tmp_path / "direct-package"
    _write_manufacturing_project(root, lossy=True)
    result = _run_project_direct(root)

    with pytest.raises(volt.ManufacturingPackageError) as error:
        result.write_manufacturing_package(
            output,
            manufacturing_profile=_manufacturing_profile_metadata(root),
        )

    assert error.value.status == "native-fabrication-loss"
    assert error.value.native_fabrication["coverage"] == {
        "classification": "fab-critical-loss",
        "fab_critical_loss": True,
    }
    assert [warning["construct"] for warning in error.value.native_fabrication["warnings"]] == [
        "board.feature.hole.finished_diameter"
    ]
    assert not output.exists()


def test_project_result_manufacturing_package_refuses_missing_required_profile_data(
    tmp_path,
):
    missing_config_root = tmp_path / "missing-config"
    missing_config_output = tmp_path / "missing-config-package"
    _write_manufacturing_project(missing_config_root, config_profile=False)
    missing_config = _run_project_direct(missing_config_root)

    with pytest.raises(volt.ManufacturingPackageError) as config_error:
        missing_config.write_manufacturing_package(missing_config_output)

    assert config_error.value.status == "missing-manufacturing-profile"
    assert config_error.value.board == {
        "design": "status-led",
        "name": "Control",
        "output_name": "Control",
    }
    assert not missing_config_output.exists()

    partial_config_output = tmp_path / "partial-config-package"
    with pytest.raises(volt.ManufacturingPackageError) as partial_error:
        missing_config.write_manufacturing_package(
            partial_config_output,
            manufacturing_profile={"path": "profiles/generic.volt.json"},
        )

    assert partial_error.value.status == "missing-manufacturing-profile"
    assert "resolved_path" in str(partial_error.value)
    assert not partial_config_output.exists()

    missing_board_profile_root = tmp_path / "missing-board-profile"
    missing_board_profile_output = tmp_path / "missing-board-profile-package"
    _write_manufacturing_project(missing_board_profile_root, board_profile=False)
    missing_board_profile = _run_project_direct(missing_board_profile_root)

    with pytest.raises(volt.ManufacturingPackageError) as board_error:
        missing_board_profile.write_manufacturing_package(
            missing_board_profile_output,
            manufacturing_profile=_manufacturing_profile_metadata(missing_board_profile_root),
        )

    assert board_error.value.status == "missing-board-capability-profile"
    assert not missing_board_profile_output.exists()


def test_project_result_manufacturing_package_reports_selector_errors_without_writing(
    tmp_path,
):
    root = tmp_path / "board"
    output = tmp_path / "direct-package"
    _write_project(root)
    _write_entrypoint(
        root,
        """import volt

def main():
    project = volt.Project("control-panel")

    @project.design
    def design():
        return (volt.Design("main-controller"), volt.Design("front-panel"))

    @project.board
    def board(context):
        boards = []
        for design in context.designs:
            board = design.add_board("Main")
            board.set_rectangular_outline(origin=(0, 0), size=(20, 10))
            boards.append(board)
        return tuple(boards)

    return project.run()
""",
    )
    result = _run_project_direct(root)

    with pytest.raises(LookupError, match="Project result has multiple boards"):
        result.write_manufacturing_package(output)
    assert not output.exists()

    with pytest.raises(LookupError, match="No board named 'missing'"):
        result.write_manufacturing_package(output, board="missing")
    assert not output.exists()


@pytest.fixture
def manufacturing_result(tmp_path):
    root = tmp_path / "board"
    _write_manufacturing_project(root)
    return _run_project_direct(root), _manufacturing_profile_metadata(root)


def _publish(result_and_profile, output, *, archive=True):
    result, profile = result_and_profile
    return result.write_manufacturing_package(
        output, manufacturing_profile=profile, archive=archive,
    )


def _snapshot(root):
    return _directory_bytes(root) if root.is_dir() else root.read_bytes()


@pytest.mark.parametrize("kind", ["file", "directory", "symlink", "dangling-symlink"])
def test_publication_refuses_unrelated_package_destinations(
    tmp_path, manufacturing_result, kind,
):
    output = tmp_path / "package"
    target = tmp_path / "unrelated-target"
    target.write_bytes(b"keep target")
    if kind == "file":
        output.write_bytes(b"keep file")
    elif kind == "directory":
        output.mkdir()
        (output / "unrelated.bin").write_bytes(b"keep directory")
    else:
        output.symlink_to(target if kind == "symlink" else tmp_path / "missing")
    before = None if kind == "dangling-symlink" else _snapshot(output)
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output)
    assert error.value.status == "unsafe-output"
    if kind != "dangling-symlink":
        assert _snapshot(output) == before
    if "symlink" in kind:
        assert output.is_symlink()
    assert target.read_bytes() == b"keep target"
    assert not list(tmp_path.glob(".package.publish-*"))


@pytest.mark.parametrize("archive", [True, False])
@pytest.mark.parametrize("change", [
    "extra-file", "changed-file", "empty-directory", "malformed", "unknown-version",
    "missing-marker", "marker-symlink", "content-symlink", "stale-generation", "boolean-version",
])
def test_publication_refuses_changed_or_invalid_package_ownership(
    tmp_path, manufacturing_result, archive, change,
):
    from volt import manufacturing as writer

    output = tmp_path / "package"
    _publish(manufacturing_result, output)
    marker = output / writer._OWNERSHIP_FILE
    if change == "extra-file":
        (output / "unrelated.bin").write_bytes(b"keep me")
    elif change == "changed-file":
        (output / "manufacturing/profile.json").write_bytes(b"keep edits")
    elif change == "empty-directory":
        (output / "unrelated-directory").mkdir()
    elif change == "missing-marker":
        marker.unlink()
    elif change in {"marker-symlink", "content-symlink"}:
        target = tmp_path / "outside"
        target.write_bytes(b"outside bytes")
        path = marker if change == "marker-symlink" else output / "manufacturing/profile.json"
        path.unlink()
        path.symlink_to(target)
    elif change == "malformed":
        marker.write_text("not json")
    else:
        payload = json.loads(marker.read_text())
        if change == "boolean-version":
            payload["schema_version"] = True
        else:
            payload["schema_version" if change == "unknown-version" else "generation"] = "invalid"
        marker.write_text(json.dumps(payload))
    before = _directory_bytes(tmp_path)
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output, archive=archive)
    assert error.value.status == "unsafe-output"
    assert _directory_bytes(tmp_path) == before
    if change == "empty-directory":
        assert (output / "unrelated-directory").is_dir()


@pytest.mark.parametrize("kind", ["file", "directory", "symlink", "dangling-symlink", "zip"])
def test_publication_refuses_unrelated_archive_when_requested(
    tmp_path, manufacturing_result, kind,
):
    output = tmp_path / "package"
    _publish(manufacturing_result, output, archive=False)
    archive = output.with_suffix(".zip")
    target = tmp_path / "outside"
    target.write_bytes(b"outside")
    if kind == "directory":
        archive.mkdir()
        (archive / "keep").write_bytes(b"keep")
    elif "symlink" in kind:
        archive.symlink_to(target if kind == "symlink" else tmp_path / "missing")
    elif kind == "zip":
        with zipfile.ZipFile(archive, "w") as unrelated:
            unrelated.writestr("keep", b"keep")
    else:
        archive.write_bytes(b"unrelated archive")
    before = _directory_bytes(tmp_path)
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output)
    assert error.value.status == "unsafe-output"
    assert _directory_bytes(tmp_path) == before
    if "symlink" in kind:
        assert archive.is_symlink()


@pytest.mark.parametrize("existing_package", [True, False])
@pytest.mark.parametrize("kind", ["file", "directory", "symlink", "dangling-symlink"])
def test_archive_false_preserves_inferred_unrelated_sibling(
    tmp_path, manufacturing_result, existing_package, kind,
):
    output = tmp_path / "package"
    if existing_package:
        _publish(manufacturing_result, output, archive=False)
    sibling = output.with_suffix(".zip")
    target = tmp_path / "outside"
    target.write_bytes(b"outside bytes")
    if kind == "file":
        sibling.write_bytes(b"unrelated inferred zip")
    elif kind == "directory":
        sibling.mkdir()
        (sibling / "keep").write_bytes(b"unrelated directory bytes")
    else:
        sibling.symlink_to(target if kind == "symlink" else tmp_path / "missing")
    before = None if kind == "dangling-symlink" else _snapshot(sibling)
    assert _publish(manufacturing_result, output, archive=False).archive is None
    if kind == "dangling-symlink":
        assert sibling.is_symlink()
    else:
        assert _snapshot(sibling) == before
    assert target.read_bytes() == b"outside bytes"


@pytest.mark.parametrize("change", ["comment", "content", "other-generation", "missing", "symlink"])
@pytest.mark.parametrize("archive", [True, False])
def test_publication_refuses_stale_or_invalid_archive_ownership(
    tmp_path, manufacturing_result, change, archive,
):
    output = tmp_path / "package"
    _publish(manufacturing_result, output)
    sibling = output.with_suffix(".zip")
    if change == "comment":
        with zipfile.ZipFile(sibling, "a") as previous:
            previous.comment = b"unknown ownership"
    elif change == "content":
        with zipfile.ZipFile(sibling, "a") as previous:
            previous.writestr("unrelated.txt", b"keep me")
    elif change == "other-generation":
        other = tmp_path / "other"
        manufacturing_result[0].project.description = "another generation"
        _publish(manufacturing_result, other)
        sibling.write_bytes(other.with_suffix(".zip").read_bytes())
    elif change == "symlink":
        target = tmp_path / "outside.zip"
        target.write_bytes(sibling.read_bytes())
        sibling.unlink()
        sibling.symlink_to(target)
    else:
        sibling.unlink()
    before = _directory_bytes(tmp_path)
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output, archive=archive)
    assert error.value.status == "unsafe-output"
    assert _directory_bytes(tmp_path) == before


def test_archive_transitions_are_owned_and_coherent(tmp_path, manufacturing_result):
    from volt import manufacturing as writer

    output = tmp_path / "package"
    for requested in (False, True, False, True, True, False):
        written = _publish(manufacturing_result, output, archive=requested)
        owner = writer._validate_directory(output)
        assert owner["archive"] is requested
        assert output.with_suffix(".zip").exists() is requested
        assert written.archive == (output.with_suffix(".zip") if requested else None)
        if requested:
            writer._validate_archive(written.archive, owner)
        assert not list(tmp_path.glob(".package.publish-*"))


@pytest.mark.parametrize("phase", ["project", "contents", "archive", "archive-validation", "directory-validation"])
def test_staging_failures_preserve_previous_generation(
    tmp_path, monkeypatch, manufacturing_result, phase,
):
    from volt import manufacturing as writer

    output = tmp_path / "package"
    _publish(manufacturing_result, output)
    before = _directory_bytes(tmp_path)

    def fail(*args, **kwargs):
        raise OSError("injected staging failure")

    if phase == "project":
        monkeypatch.setattr(volt.ProjectResult, "write", fail)
    else:
        name = {
            "contents": "_write_manufacturing_contents",
            "archive": "_write_deterministic_archive",
            "archive-validation": "_validate_archive",
            "directory-validation": "_validate_directory",
        }[phase]
        original = getattr(writer, name)

        def fail_staging(path, *args, **kwargs):
            if ".publish-" in str(path):
                raise OSError("injected staging failure")
            return original(path, *args, **kwargs)

        monkeypatch.setattr(writer, name, fail if phase == "contents" else fail_staging)
    with pytest.raises(volt.ManufacturingPackageError):
        _publish(manufacturing_result, output)
    assert _directory_bytes(tmp_path) == before
    assert not list(tmp_path.glob(".package.publish-*"))


@pytest.mark.parametrize("old_archive,new_archive", [(True, True), (True, False), (False, True), (False, False)])
@pytest.mark.parametrize("step", ["save-directory", "save-archive", "publish-archive", "publish-directory"])
def test_publication_rename_failures_restore_previous_generation(
    tmp_path, monkeypatch, manufacturing_result, old_archive, new_archive, step,
):
    if (step == "save-archive" and not old_archive) or (step == "publish-archive" and not new_archive):
        pytest.skip("transition does not perform this rename")
    output = tmp_path / "package"
    _publish(manufacturing_result, output, archive=old_archive)
    before = _directory_bytes(tmp_path)
    manufacturing_result[0].project.description = "new generation"
    original = Path.replace
    injected = False

    def replace(source, destination):
        nonlocal injected
        destination = Path(destination)
        operation = (
            "save-directory" if destination.name == "previous-package" else
            "save-archive" if destination.name == "previous-archive.zip" else
            "publish-archive" if source.name == "archive.zip" else
            "publish-directory" if source.name == "package" else "rollback"
        )
        if operation == step and not injected:
            injected = True
            raise OSError("injected rename failure")
        return original(source, destination)

    monkeypatch.setattr(Path, "replace", replace)
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output, archive=new_archive)
    assert injected
    assert error.value.status == "publication-failed"
    assert _directory_bytes(tmp_path) == before
    assert not list(tmp_path.glob(".package.publish-*"))


def test_rollback_failure_retains_recovery_and_no_accepted_directory(
    tmp_path, monkeypatch, manufacturing_result,
):
    output = tmp_path / "package"
    _publish(manufacturing_result, output)
    before = _directory_bytes(output)
    old_zip = output.with_suffix(".zip").read_bytes()
    original = Path.replace

    def replace(source, destination):
        if (source.name == "package" and Path(destination) == output) or source.name == "previous-archive.zip":
            raise OSError("injected publication/rollback failure")
        return original(source, destination)

    monkeypatch.setattr(Path, "replace", replace)
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output)
    assert error.value.status == "publication-incomplete"
    assert not output.exists()
    [recovery] = tmp_path.glob(".package.publish-*")
    assert str(recovery) in str(error.value)
    assert _directory_bytes(recovery / "previous-package") == before
    assert (recovery / "previous-archive.zip").read_bytes() == old_zip


def test_two_checkout_roots_produce_identical_package_and_archive_bytes(tmp_path):
    outputs = []
    for name in ("checkout-one", "checkout-two"):
        root = tmp_path / name
        _write_manufacturing_project(root)
        result = _run_project_direct(root)
        output = root / "dist" / "package"
        profile = _manufacturing_profile_metadata(root)
        # Absolute configured paths must be canonicalized too.
        profile["path"] = profile["resolved_path"]
        result.write_manufacturing_package(output, manufacturing_profile=profile, archive=True)
        outputs.append(output)
        payload = json.loads((output / "manufacturing/profile.json").read_text())
        assert payload["config"] == {
            "content_sha256": hashlib.sha256(Path(profile["resolved_path"]).read_bytes()).hexdigest(),
        }
        assert payload["board"]["provenance"]["as_of"] == "2026-06-21"
        manifest = json.loads((output / "manufacturing/manifest.json").read_text())
        assert manifest["schema_version"] == 2
        for content in _directory_bytes(output).values():
            assert str(root).encode() not in content
    assert _directory_bytes(outputs[0]) == _directory_bytes(outputs[1])
    assert outputs[0].with_suffix(".zip").read_bytes() == outputs[1].with_suffix(".zip").read_bytes()


@pytest.mark.parametrize("operation", ["save-directory", "save-archive", "publish-archive", "publish-directory"])
@pytest.mark.parametrize("after_rename", [False, True])
def test_cancellation_at_rename_boundaries_preserves_a_coherent_generation(
    tmp_path, monkeypatch, manufacturing_result, operation, after_rename,
):
    from volt import manufacturing as writer

    output = tmp_path / "package"
    _publish(manufacturing_result, output)
    before = _directory_bytes(tmp_path)
    manufacturing_result[0].project.description = "new generation"
    original = Path.replace
    injected = False

    def replace(source, destination):
        nonlocal injected
        destination = Path(destination)
        step = (
            "save-directory" if destination.name == "previous-package" else
            "save-archive" if destination.name == "previous-archive.zip" else
            "publish-archive" if source.name == "archive.zip" else
            "publish-directory" if source.name == "package" else "rollback"
        )
        if step == operation and not injected:
            injected = True
            if after_rename:
                original(source, destination)
            raise KeyboardInterrupt("injected cancellation")
        return original(source, destination)

    monkeypatch.setattr(Path, "replace", replace)
    with pytest.raises(KeyboardInterrupt):
        _publish(manufacturing_result, output)
    assert injected
    if operation == "publish-directory" and after_rename:
        manifest = json.loads((output / "manufacturing/manifest.json").read_text())
        assert manifest["project"]["description"] == "new generation"
    else:
        assert _directory_bytes(tmp_path) == before
    owner = writer._validate_directory(output)
    writer._validate_archive(output.with_suffix(".zip"), owner)
    assert not list(tmp_path.glob(".package.publish-*"))


@pytest.mark.parametrize("generation_fails", [True, False])
def test_cleanup_failure_has_typed_outcome_and_preserves_output(
    tmp_path, monkeypatch, manufacturing_result, generation_fails,
):
    from volt import manufacturing as writer

    output = tmp_path / "package"
    _publish(manufacturing_result, output)
    before = _directory_bytes(output)

    def fail(*args, **kwargs):
        raise OSError("injected cleanup/generation failure")

    monkeypatch.setattr(writer.shutil, "rmtree", fail)
    if generation_fails:
        monkeypatch.setattr(writer, "_write_manufacturing_contents", fail)
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output)
    assert error.value.status == "publication-cleanup-failed"
    assert _directory_bytes(output) == before
    [transaction] = tmp_path.glob(".package.publish-*")
    assert str(transaction) in str(error.value)
    writer._validate_archive(output.with_suffix(".zip"), writer._validate_directory(output))


def test_malformed_zip_compression_is_a_typed_ownership_refusal(tmp_path, manufacturing_result):
    output = tmp_path / "package"
    _publish(manufacturing_result, output)
    archive = output.with_suffix(".zip")
    content = bytearray(archive.read_bytes())
    # Corrupt both header compression fields without changing the ownership comment.
    for signature, offset in ((b"PK\x03\x04", 8), (b"PK\x01\x02", 10)):
        index = content.index(signature)
        content[index + offset:index + offset + 2] = (99).to_bytes(2, "little")
    archive.write_bytes(content)
    before = _directory_bytes(tmp_path)
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output)
    assert error.value.status == "unsafe-output"
    assert _directory_bytes(tmp_path) == before


@pytest.mark.parametrize("archive", [True, False])
def test_generation_refuses_output_changed_while_staging(
    tmp_path, monkeypatch, manufacturing_result, archive,
):
    from volt import manufacturing as writer

    output = tmp_path / "package"
    _publish(manufacturing_result, output, archive=archive)
    original = writer._write_manufacturing_contents

    def change_previous(*args, **kwargs):
        original(*args, **kwargs)
        (output / "keep-edits.txt").write_bytes(b"new unrelated bytes")

    monkeypatch.setattr(writer, "_write_manufacturing_contents", change_previous)
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output, archive=archive)
    assert error.value.status == "unsafe-output"
    assert (output / "keep-edits.txt").read_bytes() == b"new unrelated bytes"
    assert not list(tmp_path.glob(".package.publish-*"))


@pytest.mark.parametrize("archive", [True, False])
def test_first_publication_failure_removes_only_own_staging(
    tmp_path, monkeypatch, manufacturing_result, archive,
):
    output = tmp_path / "package"
    earlier_recovery = tmp_path / ".package.publish-earlier"
    earlier_recovery.mkdir()
    (earlier_recovery / "keep").write_bytes(b"previous recovery bytes")
    before = _directory_bytes(tmp_path)
    original = Path.replace

    def replace(source, destination):
        if source.name == "package" and Path(destination) == output:
            raise OSError("injected first-publication failure")
        return original(source, destination)

    monkeypatch.setattr(Path, "replace", replace)
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output, archive=archive)
    assert error.value.status == "publication-failed"
    assert _directory_bytes(tmp_path) == before
    assert list(tmp_path.glob(".package.publish-*")) == [earlier_recovery]


def test_unreadable_profile_has_typed_host_path_diagnostic(tmp_path, manufacturing_result):
    result, profile = manufacturing_result
    missing = tmp_path / "missing-profile.volt.json"
    profile["resolved_path"] = str(missing)
    output = tmp_path / "package"
    with pytest.raises(volt.ManufacturingPackageError) as error:
        _publish(manufacturing_result, output)
    assert error.value.status == "invalid-manufacturing-profile"
    assert str(missing) in str(error.value)
    assert not output.exists()
