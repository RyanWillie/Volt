#!/usr/bin/env python3
"""Generate Mintlify MDX reference pages for Volt's public Python API."""

from __future__ import annotations

import argparse
import ast
from dataclasses import dataclass
import json
from pathlib import Path
import sys
from typing import Iterable


ROOT = Path(__file__).resolve().parents[1]
PYTHON_PACKAGE = ROOT / "python" / "volt"
VOLT_INIT = PYTHON_PACKAGE / "__init__.py"
DEFAULT_OUTPUT = ROOT / "docs-site" / "api" / "python"
DEFAULT_NAVIGATION = ROOT / "docs-site" / "api" / "python" / "navigation.json"

PAGE_ORDER = ("index", "design", "logical", "library", "schematic", "pcb", "diagnostics")
PAGE_TITLES = {
    "index": "Python API",
    "design": "Design",
    "logical": "Logical",
    "library": "Library",
    "schematic": "Schematic",
    "pcb": "PCB",
    "diagnostics": "Diagnostics",
}
PAGE_DESCRIPTIONS = {
    "index": "Generated reference for Volt's public Python API.",
    "design": "Generated reference for the design root and top-level authoring helpers.",
    "logical": "Generated reference for logical circuit handles and hierarchy inspection records.",
    "library": "Generated reference for library, component, pin, and schematic symbol specs.",
    "schematic": "Generated reference for schematic sheets, drawing helpers, and presentation handles.",
    "pcb": "Generated reference for PCB board, footprint, pad, and placement helpers.",
    "diagnostics": "Generated reference for diagnostics and validation report objects.",
}
PUBLIC_MAGIC_METHODS = {
    "__bool__",
    "__getitem__",
    "__iadd__",
    "__iter__",
    "__len__",
}


@dataclass(frozen=True)
class PublicObject:
    name: str
    module: str
    source_path: Path
    node: ast.AST


def literal_all(tree: ast.Module) -> tuple[str, ...]:
    for statement in tree.body:
        if isinstance(statement, ast.Assign):
            for target in statement.targets:
                if isinstance(target, ast.Name) and target.id == "__all__":
                    return tuple(str(value) for value in ast.literal_eval(statement.value))
    raise RuntimeError("python/volt/__init__.py does not define a literal __all__")


def import_map(tree: ast.Module) -> dict[str, str]:
    mapping: dict[str, str] = {}
    for statement in tree.body:
        if not isinstance(statement, ast.ImportFrom) or statement.level != 1:
            continue
        if statement.module is None:
            continue
        for alias in statement.names:
            public_name = alias.asname or alias.name
            mapping[public_name] = statement.module
    return mapping


def module_page(module: str) -> str:
    if module == "design":
        return "design"
    if module == "diagnostics":
        return "diagnostics"
    if module in {"library", "libraries"}:
        return "library"
    if module in {"pcb", "_footprint"}:
        return "pcb"
    if module in {"schematic", "_schematic_handles"}:
        return "schematic"
    if module == "logical":
        return "logical"
    return "logical"


def source_for_module(module: str) -> Path:
    parts = module.split(".")
    return PYTHON_PACKAGE.joinpath(*parts).with_suffix(".py")


def public_objects() -> list[PublicObject]:
    init_tree = ast.parse(VOLT_INIT.read_text(encoding="utf-8"))
    exports = literal_all(init_tree)
    imports = import_map(init_tree)
    result: list[PublicObject] = []

    for name in exports:
        module = imports.get(name)
        if module is None:
            raise RuntimeError(f"Export {name!r} is not imported in python/volt/__init__.py")
        source_path, node = resolve_definition(module, name)
        result.append(PublicObject(name=name, module=module, source_path=source_path, node=node))
    return result


def resolve_definition(module: str, name: str) -> tuple[Path, ast.AST]:
    source_path = source_for_module(module)
    tree = ast.parse(source_path.read_text(encoding="utf-8"))
    node = find_definition(tree, name)
    if node is not None:
        return source_path, node

    reexports = import_map(tree)
    reexported_module = reexports.get(name)
    if reexported_module is not None:
        return resolve_definition(reexported_module, name)

    raise RuntimeError(f"Could not find definition for exported name {name!r}")


def find_definition(tree: ast.Module, name: str) -> ast.AST | None:
    for statement in tree.body:
        if isinstance(statement, (ast.ClassDef, ast.FunctionDef)) and statement.name == name:
            return statement
        if isinstance(statement, ast.Assign):
            for target in statement.targets:
                if isinstance(target, ast.Name) and target.id == name:
                    return statement
        if isinstance(statement, ast.AnnAssign) and isinstance(statement.target, ast.Name):
            if statement.target.id == name:
                return statement
    return None


def relative(path: Path) -> str:
    try:
        return path.relative_to(ROOT).as_posix()
    except ValueError:
        return path.as_posix()


def first_sentence(docstring: str | None) -> str:
    if not docstring:
        return "No docstring yet."
    first_line = " ".join(docstring.strip().splitlines()).strip()
    if not first_line:
        return "No docstring yet."
    for marker in (". ", "? ", "! "):
        index = first_line.find(marker)
        if index != -1:
            return first_line[: index + 1]
    return first_line


def format_annotation(annotation: ast.AST | None) -> str:
    if annotation is None:
        return ""
    return ast.unparse(annotation)


def format_default(default: ast.AST | None) -> str:
    if default is None:
        return ""
    return " = " + ast.unparse(default)


def format_arg(arg: ast.arg, default: ast.AST | None = None) -> str:
    text = arg.arg
    annotation = format_annotation(arg.annotation)
    if annotation:
        text += f": {annotation}"
    text += format_default(default)
    return text


def format_signature(function: ast.FunctionDef) -> str:
    args = function.args
    positional = list(args.posonlyargs) + list(args.args)
    defaults: list[ast.AST | None] = [None] * (len(positional) - len(args.defaults)) + list(args.defaults)
    rendered = [format_arg(arg, default) for arg, default in zip(positional, defaults)]

    if args.vararg is not None:
        rendered.append("*" + format_arg(args.vararg))
    elif args.kwonlyargs:
        rendered.append("*")

    for arg, default in zip(args.kwonlyargs, args.kw_defaults):
        rendered.append(format_arg(arg, default))

    if args.kwarg is not None:
        rendered.append("**" + format_arg(args.kwarg))

    signature = f"{function.name}({', '.join(rendered)})"
    returns = format_annotation(function.returns)
    if returns:
        signature += f" -> {returns}"
    return signature


def public_methods(node: ast.ClassDef) -> list[ast.FunctionDef]:
    methods = []
    for statement in node.body:
        if not isinstance(statement, ast.FunctionDef):
            continue
        if is_property(statement):
            continue
        if statement.name.startswith("_") and statement.name not in PUBLIC_MAGIC_METHODS:
            continue
        methods.append(statement)
    return methods


def public_properties(node: ast.ClassDef) -> list[ast.FunctionDef]:
    properties = []
    for statement in node.body:
        if not isinstance(statement, ast.FunctionDef):
            continue
        if not is_property(statement):
            continue
        if statement.name.startswith("_"):
            continue
        properties.append(statement)
    return properties


def is_property(function: ast.FunctionDef) -> bool:
    for decorator in function.decorator_list:
        if isinstance(decorator, ast.Name) and decorator.id == "property":
            return True
    return False


def dataclass_fields(node: ast.ClassDef) -> list[str]:
    fields: list[str] = []
    for statement in node.body:
        if isinstance(statement, ast.AnnAssign) and isinstance(statement.target, ast.Name):
            fields.append(f"{statement.target.id}: {ast.unparse(statement.annotation)}")
    return fields


def object_anchor(name: str) -> str:
    return name.lower().replace("_", "-")


def table_cell(value: str) -> str:
    return value.replace("|", "\\|").replace("\n", " ")


def render_object(item: PublicObject) -> str:
    node = item.node
    source = relative(item.source_path)
    if isinstance(node, ast.ClassDef):
        lines = [
            f"## `volt.{item.name}`",
            "",
            first_sentence(ast.get_docstring(node)),
            "",
            f"Source: `{source}`",
            "",
        ]
        fields = dataclass_fields(node)
        if fields:
            lines.extend(["### Fields", "", "| Field |", "| --- |"])
            lines.extend(f"| `{table_cell(field)}` |" for field in fields)
            lines.append("")
        properties = public_properties(node)
        if properties:
            lines.extend(["### Properties", "", "| Property | Summary |", "| --- | --- |"])
            for prop in properties:
                annotation = format_annotation(prop.returns)
                signature = prop.name if not annotation else f"{prop.name}: {annotation}"
                lines.append(
                    f"| `{table_cell(signature)}` | {table_cell(first_sentence(ast.get_docstring(prop)))} |"
                )
            lines.append("")
        methods = public_methods(node)
        if methods:
            lines.extend(["### Methods", "", "| Method | Summary |", "| --- | --- |"])
            for method in methods:
                lines.append(
                    f"| `{table_cell(format_signature(method))}` | {table_cell(first_sentence(ast.get_docstring(method)))} |"
                )
            lines.append("")
        return "\n".join(lines)

    if isinstance(node, ast.FunctionDef):
        return "\n".join(
            [
                f"## `volt.{item.name}`",
                "",
                first_sentence(ast.get_docstring(node)),
                "",
                f"Source: `{source}`",
                "",
                "```python",
                format_signature(node),
                "```",
                "",
            ]
        )

    if isinstance(node, ast.Assign):
        value = ast.unparse(node.value)
    elif isinstance(node, ast.AnnAssign) and node.value is not None:
        value = ast.unparse(node.value)
    else:
        value = item.name
    return "\n".join(
        [
            f"## `volt.{item.name}`",
            "",
            "Public value or alias exported by `volt`.",
            "",
            f"Source: `{source}`",
            "",
            "```python",
            f"{item.name} = {value}",
            "```",
            "",
        ]
    )


def page_frontmatter(title: str, description: str) -> str:
    return f"---\ntitle: {title}\ndescription: {description}\n---\n\n"


def render_index(objects_by_page: dict[str, list[PublicObject]]) -> str:
    lines = [
        page_frontmatter(PAGE_TITLES["index"], PAGE_DESCRIPTIONS["index"]).rstrip(),
        "Generated from `python/volt/__init__.py` and the source modules that define the exported objects.",
        "",
        "<Warning>",
        "  Do not edit generated API pages by hand. Run `python3 scripts/generate-python-api-docs.py` from the repository root.",
        "</Warning>",
        "",
        "## Sections",
        "",
        "| Section | Public exports |",
        "| --- | --- |",
    ]
    for page in PAGE_ORDER:
        if page == "index":
            continue
        exports = ", ".join(f"`volt.{item.name}`" for item in objects_by_page.get(page, ()))
        lines.append(f"| [{PAGE_TITLES[page]}](/api/python/{page}) | {exports} |")
    lines.append("")
    return "\n".join(lines)


def render_page(page: str, items: list[PublicObject]) -> str:
    body = [
        page_frontmatter(PAGE_TITLES[page], PAGE_DESCRIPTIONS[page]).rstrip(),
        "This page is generated from the public Python source. Add or improve docstrings in `python/volt` to improve the reference.",
        "",
    ]
    for item in items:
        body.append(render_object(item).rstrip())
        body.append("")
    return "\n".join(body)


def render_files() -> dict[Path, str]:
    objects = public_objects()
    objects_by_page: dict[str, list[PublicObject]] = {page: [] for page in PAGE_ORDER}
    for item in objects:
        objects_by_page[module_page(item.module)].append(item)

    files: dict[Path, str] = {
        Path("index.mdx"): render_index(objects_by_page),
    }
    for page in PAGE_ORDER:
        if page == "index":
            continue
        files[Path(f"{page}.mdx")] = render_page(page, objects_by_page[page])
    return files


def navigation_pages() -> list[str]:
    return [f"api/python/{page}" for page in PAGE_ORDER]


def write_files(output: Path, navigation: Path, files: dict[Path, str]) -> None:
    output.mkdir(parents=True, exist_ok=True)
    for relative_path, content in files.items():
        destination = output / relative_path
        destination.write_text(content.rstrip() + "\n", encoding="utf-8")
    navigation.parent.mkdir(parents=True, exist_ok=True)
    navigation.write_text(json.dumps(navigation_pages(), indent=2) + "\n", encoding="utf-8")


def stale_paths(output: Path, navigation: Path, files: dict[Path, str]) -> list[str]:
    stale: list[str] = []
    for relative_path, content in files.items():
        path = output / relative_path
        expected = content.rstrip() + "\n"
        if not path.exists() or path.read_text(encoding="utf-8") != expected:
            stale.append(relative_path.as_posix())
    expected_navigation = json.dumps(navigation_pages(), indent=2) + "\n"
    if not navigation.exists() or navigation.read_text(encoding="utf-8") != expected_navigation:
        stale.append(relative(navigation))
    return stale


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--navigation", type=Path, default=DEFAULT_NAVIGATION)
    parser.add_argument("--check", action="store_true", help="Fail if generated files are stale")
    return parser.parse_args(argv)


def main(argv: Iterable[str] = sys.argv[1:]) -> int:
    args = parse_args(argv)
    files = render_files()
    if args.check:
        stale = stale_paths(args.output, args.navigation, files)
        if stale:
            print(
                "generated API docs are stale; run "
                "`python3 scripts/generate-python-api-docs.py`",
                file=sys.stderr,
            )
            for path in stale:
                print(f"  {path}", file=sys.stderr)
            return 1
        print("generated API docs are up to date")
        return 0
    write_files(args.output, args.navigation, files)
    print(f"generated {len(files)} API reference pages in {relative(args.output)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
