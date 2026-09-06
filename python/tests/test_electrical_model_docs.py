"""Keep the complete public authoring example executable."""

from pathlib import Path
import re


def test_public_part_model_authoring_example_runs():
    document = Path(__file__).resolve().parents[2] / "docs" / "python-api.md"
    examples = re.findall(
        r"```python\n# part-electrical-model-doc-example\n(.*?)\n```",
        document.read_text(encoding="utf-8"),
        flags=re.DOTALL,
    )
    assert len(examples) == 1
    exec(compile(examples[0], str(document), "exec"), {})
