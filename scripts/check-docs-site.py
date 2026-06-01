#!/usr/bin/env python3
"""Validate the public Mintlify docs site structure and Volt API coverage."""

from __future__ import annotations

import ast
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
DOCS_SITE = ROOT / "docs-site"
DOCS_CONFIG = DOCS_SITE / "docs.json"
API_REFERENCE = DOCS_SITE / "python" / "reference.mdx"
VOLT_INIT = ROOT / "python" / "volt" / "__init__.py"

REQUIRED_PAGES = {
    "index",
    "quickstart",
    "python/overview",
    "python/logical-authoring",
    "python/schematic-authoring",
    "python/pcb-authoring",
    "python/diagnostics",
    "python/reference",
    "examples/index",
    "examples/led",
    "examples/timer-555",
    "examples/pcb-led-board",
    "examples/stm32-usb-buck",
    "architecture/kernel-principles",
    "api/python/index",
    "api/python/design",
    "api/python/logical",
    "api/python/library",
    "api/python/schematic",
    "api/python/pcb",
    "api/python/diagnostics",
}

REQUIRED_WORKFLOW_SNIPPETS = {
    "quickstart": ("import volt", "design.validate()", "design.write("),
    "python/logical-authoring": ("design.net(", "design.R(", "net += pin"),
    "python/schematic-authoring": ("design.schematic(", "with sheet.drawing", "drawing.connect("),
    "python/pcb-authoring": ("design.board(", "board.place(", "board.validate()"),
    "python/diagnostics": ("DiagnosticReport", "has_errors", "diagnostic.code"),
}

REQUIRED_SOURCE_REFERENCES = {
    "examples/led": "examples/schematic_sugar/compact_led.py",
    "examples/timer-555": "examples/timer_555_led_blinker/main.py",
    "examples/pcb-led-board": "examples/pcb_led_board/main.py",
    "examples/stm32-usb-buck": "examples/stm32_usb_buck/",
}

INTERNAL_HREF_PATTERN = re.compile(r'href=["\']/([^"\'?#]+)')
MARKDOWN_LINK_PATTERN = re.compile(r'\[[^\]]+\]\((/[^)#?]+)')
API_REFERENCE_EXPORT_PATTERN = re.compile(r"`volt\.([A-Za-z][A-Za-z0-9_]*)`")
FORBIDDEN_SNIPPETS = {
    "python/pcb-authoring": (
        "add_via(gnd, x=",
        "diameter=",
        "from_layer=",
        "to_layer=",
        "add_zone(\n    gnd,",
        "clearance=",
        "add_text(\"Volt\", x=",
    ),
    "examples/stm32-usb-buck": (
        "board-style organization",
        "board = build_board()",
    ),
}


def fail(message: str) -> None:
    print(f"docs-site check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def load_config() -> dict:
    if not DOCS_CONFIG.exists():
        fail(f"{DOCS_CONFIG.relative_to(ROOT)} does not exist")
    try:
        return json.loads(DOCS_CONFIG.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        fail(f"{DOCS_CONFIG.relative_to(ROOT)} is not valid JSON: {error}")


def collect_pages(node) -> set[str]:
    pages: set[str] = set()
    if isinstance(node, str):
        pages.add(node)
        return pages
    if isinstance(node, list):
        for item in node:
            pages.update(collect_pages(item))
        return pages
    if isinstance(node, dict):
        if "href" in node and "pages" not in node and "groups" not in node:
            return pages
        root = node.get("root")
        if isinstance(root, str):
            pages.add(root)
        for key in ("pages", "groups", "tabs", "anchors", "dropdowns", "versions", "products", "menu"):
            if key in node:
                pages.update(collect_pages(node[key]))
    return pages


def exported_api_names() -> tuple[str, ...]:
    tree = ast.parse(VOLT_INIT.read_text(encoding="utf-8"))
    for statement in tree.body:
        if isinstance(statement, ast.Assign):
            for target in statement.targets:
                if isinstance(target, ast.Name) and target.id == "__all__":
                    value = ast.literal_eval(statement.value)
                    return tuple(str(item) for item in value)
    fail("python/volt/__init__.py does not define a literal __all__")


def page_text(page: str) -> str:
    path = DOCS_SITE / f"{page}.mdx"
    if not path.exists():
        fail(f"navigation references missing page {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


def page_key(path: Path) -> str:
    return path.with_suffix("").as_posix()


def check_frontmatter(page: str, text: str) -> None:
    if not text.startswith("---\n"):
        fail(f"{page}.mdx is missing frontmatter")
    end = text.find("\n---\n", 4)
    if end == -1:
        fail(f"{page}.mdx frontmatter is not closed")
    frontmatter = text[4:end]
    for field in ("title:", "description:"):
        if field not in frontmatter:
            fail(f"{page}.mdx frontmatter is missing {field[:-1]}")


def check_api_reference() -> None:
    if not API_REFERENCE.exists():
        fail(f"{API_REFERENCE.relative_to(ROOT)} does not exist")
    reference = API_REFERENCE.read_text(encoding="utf-8")
    exports = set(exported_api_names())
    missing = [
        name for name in exports
        if f"`volt.{name}`" not in reference and f"`{name}`" not in reference
    ]
    if missing:
        fail(
            "python/reference.mdx does not mention exported API names: "
            + ", ".join(missing)
        )
    documented = set(API_REFERENCE_EXPORT_PATTERN.findall(reference))
    extra = sorted(documented - exports)
    if extra:
        fail(
            "python/reference.mdx documents names that are not exported by volt.__all__: "
            + ", ".join(extra)
        )


def main() -> int:
    config = load_config()
    for key in ("$schema", "name", "theme", "colors", "navigation"):
        if key not in config:
            fail(f"docs.json is missing required Mintlify field {key!r}")
    if config["$schema"] != "https://mintlify.com/docs.json":
        fail("docs.json must use the Mintlify docs.json schema URL")
    if not isinstance(config.get("colors"), dict) or not config["colors"].get("primary"):
        fail("docs.json must define colors.primary")

    navigation_pages = collect_pages(config["navigation"])
    missing_required = sorted(REQUIRED_PAGES - navigation_pages)
    if missing_required:
        fail("navigation is missing required pages: " + ", ".join(missing_required))

    for page in sorted(navigation_pages):
        text = page_text(page)
        check_frontmatter(page, text)

    all_mdx_pages = {
        page_key(path.relative_to(DOCS_SITE))
        for path in DOCS_SITE.rglob("*.mdx")
    }
    unlisted = sorted(all_mdx_pages - navigation_pages)
    if unlisted:
        fail("MDX pages are not in navigation: " + ", ".join(unlisted))

    for page, snippets in REQUIRED_WORKFLOW_SNIPPETS.items():
        text = page_text(page)
        missing = [snippet for snippet in snippets if snippet not in text]
        if missing:
            fail(f"{page}.mdx is missing required workflow snippets: {', '.join(missing)}")

    for page, snippets in FORBIDDEN_SNIPPETS.items():
        text = page_text(page)
        present = [snippet for snippet in snippets if snippet in text]
        if present:
            fail(f"{page}.mdx contains stale or invalid snippets: {', '.join(present)}")

    for page, source in REQUIRED_SOURCE_REFERENCES.items():
        text = page_text(page)
        if source not in text:
            fail(f"{page}.mdx does not mention source path {source}")
        source_path = ROOT / source
        if not source_path.exists():
            fail(f"{page}.mdx mentions missing source path {source}")

    for page in sorted(navigation_pages):
        text = page_text(page)
        links = list(INTERNAL_HREF_PATTERN.finditer(text)) + list(
            MARKDOWN_LINK_PATTERN.finditer(text)
        )
        for match in links:
            href = match.group(1).lstrip("/").rstrip("/")
            if href and href not in navigation_pages:
                fail(f"{page}.mdx links to unknown internal page /{href}")

    check_api_reference()

    if re.search(r"\b(TBD|TODO|coming soon)\b", "\n".join(page_text(page) for page in navigation_pages), re.I):
        fail("public docs contain placeholder language")

    print(f"docs-site check passed: {len(navigation_pages)} pages, {len(exported_api_names())} API exports covered")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
