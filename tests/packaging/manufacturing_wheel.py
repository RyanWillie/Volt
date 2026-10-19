"""Source-free exact native manufacturing report parity from the installed wheel."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

fixture = Path(sys.argv[1]).resolve()
runner = """
from pathlib import Path
import volt
assert 'site-packages' in str(Path(volt.__file__).resolve())
artifact = Path('board').read_bytes()
target_bytes = Path('target').read_text()
target = volt.read_manufacturing_target(target_bytes)
report = volt.evaluate_manufacturing_coverage(artifact, target).to_json()
assert report == Path('expected').read_text()
assert volt.write_manufacturing_target(target) == target_bytes
assert artifact == Path('board').read_bytes()
"""
for mode in ("assembly", "rating-accepted", "rating-current", "rating-budget", "rating-failure", "rating-unknown"):
    data = json.loads(subprocess.check_output([str(fixture), mode], text=True))
    with tempfile.TemporaryDirectory(prefix="volt-manufacturing-wheel-") as directory:
        root = Path(directory)
        (root / "board").write_bytes(bytes.fromhex(data["artifact_hex"]))
        (root / "target").write_text(data["target"])
        (root / "expected").write_text(data["report"])
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        subprocess.run([sys.executable, "-c", runner], cwd=root, env=environment, check=True)
print("Installed wheel: six source-free assembly/rating reports match native bytes")
