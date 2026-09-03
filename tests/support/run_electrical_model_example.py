"""Execute the published examples, then reopen in a source-free subprocess."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    mode, source = sys.argv[1:3]
    with tempfile.TemporaryDirectory(prefix="volt-electrical-example-") as temporary:
        root = Path(temporary)
        output = root / "artifacts"
        environment = dict(os.environ)
        environment.pop("PYTHONPATH", None)
        environment["PYTHONDONTWRITEBYTECODE"] = "1"
        if mode == "native":
            subprocess.run([source, "write", str(output)], cwd=root, env=environment, check=True)
            (output / "library.voltlib").unlink()
            subprocess.run([source, "inspect", str(output)], cwd=root, env=environment, check=True)
        elif mode == "python":
            environment["PYTHONPATH"] = str(Path(sys.argv[3]).resolve())
            author = root / "author.py"
            inspector = root / "reopen.py"
            shutil.copyfile(Path(source) / "main.py", author)
            shutil.copyfile(Path(source) / "reopen.py", inspector)
            subprocess.run([sys.executable, str(author), str(output)],
                           cwd=root, env=environment, check=True)
            author.unlink()
            (output / "library.voltlib").unlink()
            subprocess.run([sys.executable, str(inspector), str(output)],
                           cwd=root, env=environment, check=True)
        else:
            raise ValueError(f"Unknown example mode: {mode}")


if __name__ == "__main__":
    main()
