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
for mode in ("assembly", "assembly-unselected", "assembly-missing-options", "rating-accepted", "rating-current", "rating-budget", "rating-failure", "rating-unknown", "rating-conditional"):
    print(f"Checking installed-wheel report: {mode}", flush=True)
    data = json.loads(subprocess.check_output([str(fixture), mode], text=True))
    with tempfile.TemporaryDirectory(prefix="volt-manufacturing-wheel-") as directory:
        root = Path(directory)
        (root / "board").write_bytes(bytes.fromhex(data["artifact_hex"]))
        (root / "target").write_text(data["target"])
        (root / "expected").write_text(data["report"])
        environment = os.environ.copy()
        environment.pop("PYTHONPATH", None)
        subprocess.run([sys.executable, "-c", runner], cwd=root, env=environment, check=True)
print("Installed wheel: nine source-free assembly/rating reports match native bytes")

# The installed wheel must also reproduce full actual-export + checked assertion bytes.
evidence_runner = """
import json
from pathlib import Path
import volt
assert 'site-packages' in str(Path(volt.__file__).resolve())
x=json.loads(Path('evidence-input').read_text())
a=bytes.fromhex(x['artifact_hex']); before=bytes(a)
i=volt.ManufacturingEvidenceInput()
i.assertions=volt.read_manufacturing_evidence(x['evidence'])
i.attachments=[volt.ManufacturingEvidenceAttachment('source',x['source_bytes'].encode()),
               volt.ManufacturingEvidenceAttachment('review',x['review_bytes'].encode())]
r=volt.evaluate_manufacturing_export(a,volt.read_manufacturing_target(x['target']),i)
assert r.to_json()==x['report'] and a==before
assert volt.write_manufacturing_evidence(i.assertions)==x['evidence']
"""
for mode in ('evidence-fab','evidence-assembly','evidence-blocked','evidence-bottom','evidence-range'):
    print(f"Checking installed-wheel exact evidence: {mode}", flush=True)
    data = subprocess.check_output([str(fixture), mode], text=True)
    with tempfile.TemporaryDirectory(prefix='volt-evidence-wheel-') as directory:
        root = Path(directory)
        (root / 'evidence-input').write_text(data)
        environment = os.environ.copy()
        environment.pop('PYTHONPATH', None)
        subprocess.run([sys.executable,'-c',evidence_runner],cwd=root,env=environment,check=True)
print('Installed wheel: five synthetic native export/evidence reports match exact native bytes')
