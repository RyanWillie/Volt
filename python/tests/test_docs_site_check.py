from __future__ import annotations

import importlib.util
from pathlib import PureWindowsPath


def _load_docs_site_checker():
    spec = importlib.util.spec_from_file_location(
        "check_docs_site",
        "scripts/check-docs-site.py",
    )
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_docs_site_page_key_uses_posix_separators_for_windows_paths():
    checker = _load_docs_site_checker()

    assert checker.page_key(PureWindowsPath("api\\python\\design.mdx")) == "api/python/design"
