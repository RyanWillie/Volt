"""Full native payload parity against synthetic source-free CompiledBoard bytes."""

import json
import os
import subprocess
from pathlib import Path

import pytest
import volt
from volt import _volt


@pytest.mark.parametrize("mode", [
    "complete", "missing", "netless-short", "pad-short", "modules", "multi",
    "rounding", "drill", "nc", "dnp", "lossy-short",
])
def test_native_completion_payload_parity_and_determinism(mode):
    native = json.loads(subprocess.check_output(
        [os.environ["VOLT_COMPLETION_PARITY_FIXTURE"], mode], text=True,
    ))
    artifact = bytes.fromhex(native["artifact_hex"])
    before = bytes(artifact)
    report = volt.evaluate_compiled_board_completion(artifact)
    assert report == native["report"]
    assert json.loads(_volt.evaluate_compiled_board_completion_json(artifact)) == report
    assert _volt.evaluate_compiled_board_completion_json(artifact) == _volt.evaluate_compiled_board_completion_json(artifact)
    assert artifact == before
    assert report["counts"]["components"] == len(report["components"])
    assert all(row["required"] for row in report["coverage"])
    assert all(d["entity_count"] == len(d["entities"]) for d in report["diagnostics"])
    expected = {
        "complete": "complete", "missing": "incomplete", "netless-short": "incomplete",
        "pad-short": "incomplete", "modules": "complete", "multi": "complete",
        "rounding": "not-evaluated", "drill": "not-evaluated", "nc": "incomplete",
        "dnp": "complete", "lossy-short": "incomplete",
    }
    assert report["status"] == expected[mode]


def test_live_board_completion_is_thin_and_reopens_without_source():
    library = volt.Library("test.completion-python", version="1")
    footprint = volt.Footprint(
        ("test.completion-python", "contact"),
        pads=(volt.FootprintPad.surface_mount("1", at=(0, 0), size=(1, 1), shape="circle"),),
    )
    part = library.part(
        "contact", pins=(volt.PinSpec("P", 1, role="passive"),),
        footprint=footprint, pads={"P": "1"},
        manufacturer="Synthetic", mpn="CONTACT", package="TEST",
    )
    design = volt.Design("completion-live")
    contact = design.instantiate(part, ref="U1")
    net = design.net("GND", kind="ground")
    net += contact[1]
    board = design.add_board("Main")
    front = board.add_layer("F.Cu", role="copper", side="top")
    back = board.add_layer("B.Cu", role="copper", side="bottom")
    board.set_layer_stack((front, back), thickness=1.6)
    board.set_rectangular_outline(origin=(0, 0), size=(20, 20))
    board.place(contact, at=(10, 10))
    board.set_capability_profile(volt.CapabilityProfile(
        name="Synthetic completion", source="native test fixture", as_of="2026-10-14",
        minimum_track_width=0.1, minimum_via_drill=0.2, minimum_via_annular=0.4,
    ))
    before = board.to_json()
    report = board.completion()
    artifact = board.compile_bytes()
    assert report == volt.evaluate_compiled_board_completion(artifact)
    assert report["status"] == "complete"
    assert report == json.loads(board._native.completion_json())
    assert board.to_json() == before
    del design, library, board, contact, part
    assert volt.evaluate_compiled_board_completion(artifact) == report


def test_completion_catalog_and_docs_are_synchronized():
    native = tuple(_volt.pcb_completion_diagnostic_codes())
    assert volt.PCB_COMPLETION_DIAGNOSTIC_CODES == native
    assert len(native) == 12
    docs = (Path(__file__).parents[2] / "docs/design/diagnostic-codes.html").read_text()
    assert all(code in docs for code in native)


def test_completion_rejects_malformed_native_artifact():
    with pytest.raises(volt.VoltError):
        volt.evaluate_compiled_board_completion(b"not-a-compiled-board")
