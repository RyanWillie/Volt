"""Structured project workflow for Volt Python authoring."""

from __future__ import annotations

import json
import re
import shutil
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Callable, Iterable

from .design import Design
from .library import Library
from .pcb import Board
from .project_checks import BoardCheck, DesignCheck, SchematicCheck
from .schematic import Schematic


BuildFunction = Callable[..., object]


@dataclass(frozen=True)
class ProjectTestDefinition:
    """Registered product-intent test for one project stage."""

    name: str
    function: BuildFunction


@dataclass(frozen=True)
class ProjectRuleSuiteDefinition:
    """Registered project-level diagnostic suite."""

    name: str
    function: BuildFunction


@dataclass(frozen=True)
class StageResult:
    """Summary for one executed project stage."""

    name: str
    model_count: int
    tests: tuple["ProjectTestResult", ...] = ()


@dataclass(frozen=True)
class ProjectDiagnostic:
    """Diagnostic emitted during one project run with project source metadata."""

    stage: str
    source: str
    report: str
    severity: str
    code: str
    message: str
    entities: tuple[object, ...]
    design: str | None = None
    board: str | None = None
    rule: str | None = None


@dataclass(frozen=True)
class ProjectTestResult:
    """Result of one product-intent project test."""

    stage: str
    name: str
    ok: bool
    message: str = ""


class ProjectDiagnostics:
    """Ordered project diagnostics collected from model validators."""

    def __init__(self, diagnostics):
        self._diagnostics = tuple(diagnostics)

    def __iter__(self):
        """Iterate over diagnostics in deterministic report order."""
        return iter(self._diagnostics)

    def __len__(self) -> int:
        """Return the number of diagnostics."""
        return len(self._diagnostics)

    def errors(self, *, stage: ProjectStage | str | None = None) -> tuple[ProjectDiagnostic, ...]:
        """Return error diagnostics, optionally filtered by stage."""
        stage_name = _stage_name(stage)
        return tuple(
            diagnostic
            for diagnostic in self._diagnostics
            if diagnostic.severity == "error"
            and (stage_name is None or diagnostic.stage == stage_name)
        )

    @property
    def has_errors(self) -> bool:
        """Return whether any collected diagnostic has error severity."""
        return bool(self.errors())


class BuildContext:
    """Read-only access to models produced by earlier project stages."""

    def __init__(self, *, designs: Iterable[Design]):
        self._designs = tuple(designs)

    @property
    def designs(self) -> tuple[Design, ...]:
        """Return designs produced by the project design stage."""
        return self._designs

    def design(self, name: str | None = None) -> Design:
        """Return a design by stable name, or the only design."""
        return _one_or_named(self._designs, name, "design")


class ProjectRuleContext:
    """Read-only project summary passed to project-level rule suites."""

    def __init__(self, result: "ProjectResult"):
        self._project_name = result.project.name
        self._design_names = tuple(design.name for design in result.designs)
        self._schematic_names = tuple(
            _model_output_name(schematic, result.schematics)
            for schematic in result.schematics
        )
        self._board_names = tuple(
            _model_output_name(board, result.boards)
            for board in result.boards
        )

    @property
    def project_name(self) -> str:
        """Return the project name for this run."""
        return self._project_name

    @property
    def design_names(self) -> tuple[str, ...]:
        """Return stable design names in result order."""
        return self._design_names

    @property
    def schematic_names(self) -> tuple[str, ...]:
        """Return stable schematic result names in result order."""
        return self._schematic_names

    @property
    def board_names(self) -> tuple[str, ...]:
        """Return stable board result names in result order."""
        return self._board_names

    def diagnostic(
        self,
        *,
        severity: str,
        code: str,
        message: str,
        stage: str = "project",
        source: str | None = None,
        report: str = "project.rule",
        entities: tuple[object, ...] = (),
        design: str | None = None,
        board: str | None = None,
    ) -> ProjectDiagnostic:
        """Create a project-level diagnostic without exposing mutable models."""
        return ProjectDiagnostic(
            stage=stage,
            source=source or f"project:{self._project_name}",
            report=report,
            severity=severity,
            code=code,
            message=message,
            entities=entities,
            design=design,
            board=board,
        )


class ProjectStage:
    """Named workflow stage used as a decorator and result lookup key."""

    def __init__(self, project: "Project", name: str):
        self.project = project
        self.name = name
        self._function: BuildFunction | None = None
        self._tests: list[ProjectTestDefinition] = []

    def __call__(self, function: BuildFunction) -> BuildFunction:
        """Register this stage's product composition function."""
        if self._function is not None:
            raise RuntimeError(
                f"Project {self.project.name} {self.name} stage is already registered"
            )
        self._function = function
        return function

    def test(self, function: BuildFunction) -> BuildFunction:
        """Register a product-intent test for this stage."""
        self._tests.append(ProjectTestDefinition(function.__name__, function))
        return function

    @property
    def registered(self) -> bool:
        """Return whether this stage has a composition function."""
        return self._function is not None

    @property
    def tests(self) -> tuple[ProjectTestDefinition, ...]:
        """Return product-intent tests registered for this stage."""
        return tuple(self._tests)


@dataclass(frozen=True)
class _StageRun:
    stage: ProjectStage
    models: tuple[object, ...]
    tests: tuple[ProjectTestResult, ...]


class Project:
    """Canonical staged entrypoint for a Volt electronics project."""

    def __init__(
        self,
        name: str,
        *,
        version: str | None = None,
        description: str | None = None,
    ):
        if not isinstance(name, str):
            raise TypeError("Project name must be a string")
        if not name:
            raise ValueError("Project name must not be empty")
        self.name = name
        self.version = version
        self.description = description
        self.design = ProjectStage(self, "design")
        self.schematic = ProjectStage(self, "schematic")
        self.board = ProjectStage(self, "board")
        self._stage_order = (self.design, self.schematic, self.board)
        self._rule_suites: list[ProjectRuleSuiteDefinition] = []
        self._libraries: list[Library] = []

    def use_library(self, library: Library) -> Library:
        """Include a library's validation diagnostics in this project result."""
        if not isinstance(library, Library):
            raise TypeError("Project.use_library expects a Library")
        if library not in self._libraries:
            self._libraries.append(library)
        return library

    def rule_suite(self, name: str) -> Callable[[BuildFunction], BuildFunction]:
        """Register a project-level diagnostic suite in evaluation order."""
        if not isinstance(name, str):
            raise TypeError("Project rule suite name must be a string")
        if not name:
            raise ValueError("Project rule suite name must not be empty")
        if any(rule.name == name for rule in self._rule_suites):
            raise RuntimeError(f"Project {self.name} rule suite {name} is already registered")

        def register(function: BuildFunction) -> BuildFunction:
            self._rule_suites.append(ProjectRuleSuiteDefinition(name, function))
            return function

        return register

    def run(self) -> "ProjectResult":
        """Run the registered project stages and return one result."""
        return self._run_until(None)

    def run_through(
        self,
        stage: ProjectStage,
    ) -> "ProjectResult":
        """Run stages through the given stage handle."""
        if stage.project is not self:
            raise ValueError("Project stage belongs to a different project")
        if not stage.registered:
            raise RuntimeError(f"Project {self.name} {stage.name} stage is not registered")
        return self._run_until(stage)

    def _run_until(
        self,
        stop_stage: ProjectStage | None,
    ) -> "ProjectResult":
        if not any(stage.registered for stage in self._stage_order):
            raise RuntimeError(f"Project {self.name} has no build stages")

        models: list[object] = []
        model_ids: set[int] = set()
        runs: list[_StageRun] = []
        designs: tuple[Design, ...] = ()

        for stage in self._stage_order:
            if not stage.registered:
                continue
            if stage is self.design:
                result = stage._function()
            else:
                if not designs:
                    raise RuntimeError(
                        f"Project {self.name} {stage.name} stage requires a design stage"
                    )
                argument = designs[0] if len(designs) == 1 else BuildContext(designs=designs)
                result = stage._function(argument)
            stage_models = self._collect_stage_return(stage, result)
            for model in stage_models:
                if id(model) not in model_ids:
                    models.append(model)
                    model_ids.add(id(model))
            if stage is self.design:
                designs = stage_models
            tests = self._run_stage_tests(stage, stage_models)
            runs.append(_StageRun(stage, stage_models, tests))
            if stop_stage is stage:
                break

        return ProjectResult(self, runs=runs, models=models)

    def _collect_stage_return(
        self,
        stage: ProjectStage,
        value: object,
    ) -> tuple[object, ...]:
        models: list[object] = []
        model_ids: set[int] = set()
        self._collect_return(models, model_ids, value)
        if not models:
            raise RuntimeError(
                f"Project {self.name} {stage.name} stage must return at least one "
                f"{_expected_model_name(stage)} model"
            )
        expected_type = _expected_model_type(stage)
        wrong_models = [model for model in models if not isinstance(model, expected_type)]
        if wrong_models:
            raise TypeError(
                f"Project {self.name} {stage.name} stage must return "
                f"{_expected_model_name(stage)} models"
            )
        return tuple(models)

    def _collect_return(
        self,
        models: list[object],
        model_ids: set[int],
        value: object,
    ) -> None:
        if value is None:
            return
        if _is_known_model(value):
            if id(value) not in model_ids:
                models.append(value)
                model_ids.add(id(value))
            return
        if isinstance(value, (list, tuple)):
            for item in value:
                self._collect_return(models, model_ids, item)
            return
        raise TypeError("Project build stages must return Volt models or lists/tuples of Volt models")

    def _run_stage_tests(
        self,
        stage: ProjectStage,
        models: tuple[object, ...],
    ) -> tuple[ProjectTestResult, ...]:
        if not stage.tests:
            return ()
        results: list[ProjectTestResult] = []
        for test in stage.tests:
            try:
                check = _check_for_stage(stage, models)
                test.function(check)
            except AssertionError as error:
                results.append(
                    ProjectTestResult(stage.name, test.name, False, str(error))
                )
            except Exception as error:
                message = f"{type(error).__name__}: {error}"
                results.append(ProjectTestResult(stage.name, test.name, False, message))
            else:
                results.append(ProjectTestResult(stage.name, test.name, True))
        return tuple(results)


class ProjectResult:
    """Output of one Volt project run."""

    def __init__(
        self,
        project: Project,
        *,
        runs: list[_StageRun] | None = None,
        models: list[object] | None = None,
    ):
        self.project = project
        self._runs = tuple(runs or ())
        self._stages = tuple(
            StageResult(run.stage.name, len(run.models), run.tests)
            for run in self._runs
        )
        self._models = tuple(models or ())
        _check_unique_model_names(self.designs, "design")
        self._diagnostics = ProjectDiagnostics(
            (
                *_collect_library_diagnostics(self.project._libraries),
                *_collect_default_diagnostics(self._runs),
                *_collect_rule_diagnostics(self.project._rule_suites, self),
            )
        )

    @property
    def ok(self) -> bool:
        """Return whether the project run has no errors."""
        return not self._diagnostics.has_errors and not self.test_failures()

    @property
    def diagnostics(self) -> ProjectDiagnostics:
        """Return diagnostics collected from default project checks."""
        return self._diagnostics

    @property
    def stages(self) -> tuple[StageResult, ...]:
        """Return executed stage summaries in run order."""
        return self._stages

    @property
    def designs(self) -> tuple[Design, ...]:
        """Return logical designs collected by the run."""
        return tuple(model for model in self._models if isinstance(model, Design))

    @property
    def schematics(self) -> tuple[Schematic, ...]:
        """Return schematic projections collected by the run."""
        return tuple(model for model in self._models if isinstance(model, Schematic))

    @property
    def boards(self) -> tuple[Board, ...]:
        """Return board projections collected by the run."""
        return tuple(model for model in self._models if isinstance(model, Board))

    @property
    def rule_suites(self) -> tuple[str, ...]:
        """Return project-level rule suites in evaluation order."""
        return tuple(rule.name for rule in self.project._rule_suites)

    def design(self, name: str | None = None) -> Design:
        """Return a collected design by name, or the only design."""
        return _one_or_named(self.designs, name, "design")

    def schematic(self, name: str | None = None) -> Schematic:
        """Return a collected schematic by name, or the only schematic."""
        return _one_or_named_projection(self.schematics, name, "schematic")

    def board(self, name: str | None = None) -> Board:
        """Return a collected board by name, or the only board."""
        return _one_or_named_projection(self.boards, name, "board")

    def stage(self, stage: ProjectStage | str) -> StageResult:
        """Return an executed stage summary by stage handle or name."""
        stage_name = _stage_name(stage)
        for item in self._stages:
            if item.name == stage_name:
                return item
        raise LookupError(f"Project result has no stage named {stage_name}")

    def test_failures(self) -> tuple[ProjectTestResult, ...]:
        """Return failed project tests in stage execution order."""
        return tuple(
            test
            for stage in self._stages
            for test in stage.tests
            if not test.ok
        )

    def write(self, path: str | Path) -> None:
        """Write a deterministic project result bundle to a directory."""
        root = Path(path)
        _prepare_bundle_root(root)

        used_paths: set[str] = set()
        artifacts: list[dict[str, str]] = []
        for model in self._models:
            if isinstance(model, Design):
                relative = _unique_path(
                    Path("logical") / f"{_safe_slug(model.name)}.volt.json",
                    used_paths,
                )
                _write_text(root / relative, model.to_json())
                artifacts.append(
                    _artifact_record(
                        "logical",
                        model.name,
                        relative,
                        "application/vnd.volt.logical+json",
                        group={"design": model.name},
                    )
                )
            elif isinstance(model, Schematic):
                output_name = _model_output_name(model, self.schematics)
                relative_json = _unique_path(
                    Path("schematic") / f"{_safe_slug(output_name)}.volt.schematic.json",
                    used_paths,
                )
                relative_svg = _unique_path(
                    Path("schematic") / f"{_safe_slug(output_name)}.svg",
                    used_paths,
                )
                _write_text(root / relative_json, model.to_json())
                _write_text(root / relative_svg, model.to_svg())
                group = {"design": model._design.name, "schematic": model.name}
                artifacts.append(
                    _artifact_record(
                        "schematic",
                        output_name,
                        relative_json,
                        "application/vnd.volt.schematic+json",
                        group=group,
                    )
                )
                artifacts.append(
                    _artifact_record(
                        "schematic_svg",
                        output_name,
                        relative_svg,
                        "image/svg+xml",
                        group=group,
                    )
                )
            elif isinstance(model, Board):
                output_name = _model_output_name(model, self.boards)
                relative_json = _unique_path(
                    Path("pcb") / f"{_safe_slug(output_name)}.volt.pcb.json",
                    used_paths,
                )
                relative_svg = _unique_path(
                    Path("pcb") / f"{_safe_slug(output_name)}.svg",
                    used_paths,
                )
                _write_text(root / relative_json, model.to_json())
                _write_text(root / relative_svg, model.to_svg())
                group = {"design": model._design.name, "board": model.name}
                artifacts.append(
                    _artifact_record(
                        "pcb",
                        output_name,
                        relative_json,
                        "application/vnd.volt.pcb+json",
                        group=group,
                    )
                )
                artifacts.append(
                    _artifact_record(
                        "pcb_svg",
                        output_name,
                        relative_svg,
                        "image/svg+xml",
                        group=group,
                    )
                )

        diagnostics_path = _unique_path(Path("diagnostics") / "diagnostics.json", used_paths)
        tests_path = _unique_path(Path("diagnostics") / "tests.json", used_paths)
        _write_json(root / diagnostics_path, _diagnostics_payload(self._diagnostics))
        _write_json(root / tests_path, _tests_payload(self._test_results()))
        artifacts.append(
            _artifact_record(
                "diagnostics",
                "Project diagnostics",
                diagnostics_path,
                "application/json",
            )
        )
        artifacts.append(
            _artifact_record(
                "project_tests",
                "Project tests",
                tests_path,
                "application/json",
            )
        )

        _write_json(
            root / "manifest.volt.json",
            {
                "format": "volt.project_result",
                "schema_version": 1,
                "project": {
                    "name": self.project.name,
                    "version": self.project.version,
                    "description": self.project.description,
                },
                "ok": self.ok,
                "stages": [
                    {
                        "name": stage.name,
                        "model_count": stage.model_count,
                        "tests": [
                            _test_result_payload(test)
                            for test in stage.tests
                        ],
                    }
                    for stage in self._stages
                ],
                "artifacts": artifacts,
                "diagnostics": {
                    "path": diagnostics_path.as_posix(),
                    "summary": _diagnostic_summary(self._diagnostics),
                },
                "tests": {
                    "path": tests_path.as_posix(),
                    "summary": _test_summary(self._test_results()),
                },
            },
        )

    def _test_results(self) -> tuple[ProjectTestResult, ...]:
        return tuple(test for stage in self._stages for test in stage.tests)


def _is_known_model(value: object) -> bool:
    return isinstance(value, (Design, Schematic, Board))


def _expected_model_type(stage: ProjectStage):
    if stage.name == "design":
        return Design
    if stage.name == "schematic":
        return Schematic
    if stage.name == "board":
        return Board
    raise RuntimeError(f"Unsupported project stage {stage.name}")


def _expected_model_name(stage: ProjectStage) -> str:
    return _expected_model_type(stage).__name__


def _one_or_named(models: tuple[object, ...], name: str | None, kind: str):
    if name is None:
        if len(models) != 1:
            raise LookupError(f"Project result has {len(models)} {kind} models")
        return models[0]
    matches = [model for model in models if model.name == name]
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        raise LookupError(f"Project result has multiple {kind} models named {name}")
    raise LookupError(f"Project result has no {kind} named {name}")


def _one_or_named_projection(
    models: tuple[Board, ...] | tuple[Schematic, ...],
    name: str | None,
    kind: str,
):
    if name is None:
        if len(models) != 1:
            raise LookupError(f"Project result has {len(models)} {kind} models")
        return models[0]

    keyed_matches = [model for model in models if _model_output_name(model, models) == name]
    if len(keyed_matches) == 1:
        return keyed_matches[0]

    name_matches = [model for model in models if model.name == name]
    if len(name_matches) == 1:
        return name_matches[0]
    if len(name_matches) > 1:
        raise LookupError(
            f"Project result has multiple {kind} models named {name}; "
            f"use design:{kind}"
        )
    raise LookupError(f"Project result has no {kind} named {name}")


def _check_unique_model_names(models: tuple[object, ...], kind: str) -> None:
    seen: set[str] = set()
    duplicates: set[str] = set()
    for model in models:
        if model.name in seen:
            duplicates.add(model.name)
        seen.add(model.name)
    if duplicates:
        names = ", ".join(sorted(duplicates))
        raise RuntimeError(f"Project result has duplicate {kind} model names: {names}")


def _model_output_name(model: object, siblings: tuple[object, ...]) -> str:
    if isinstance(model, (Board, Schematic)):
        if len(siblings) > 1:
            return f"{_projection_key_part(model._design.name)}:{_projection_key_part(model.name)}"
    return model.name


def _projection_key_part(name: str) -> str:
    return name.replace("~", "~0").replace(":", "~1")


def _stage_name(stage: ProjectStage | str | None) -> str | None:
    if isinstance(stage, ProjectStage):
        return stage.name
    return stage


def _collect_default_diagnostics(runs: tuple[_StageRun, ...]) -> tuple[ProjectDiagnostic, ...]:
    diagnostics: list[ProjectDiagnostic] = []
    for run in runs:
        for model in run.models:
            if isinstance(model, Design):
                diagnostics.extend(
                    _report_diagnostics(
                        run.stage.name,
                        f"logical:{model.name}",
                        "logical.default",
                        model.validate(),
                        design=model.name,
                    )
                )
            elif isinstance(model, Schematic):
                diagnostics.extend(
                    _report_diagnostics(
                        run.stage.name,
                        f"schematic:{model.name}",
                        "schematic.readiness",
                        model.validate(),
                        design=model._design.name,
                    )
                )
                diagnostics.extend(
                    _report_diagnostics(
                        run.stage.name,
                        f"schematic:{model.name}",
                        "schematic.readability",
                        model.validate_readability(),
                        design=model._design.name,
                    )
                )
            elif isinstance(model, Board):
                diagnostics.extend(
                    _report_diagnostics(
                        run.stage.name,
                        f"pcb:{model.name}",
                        "pcb.board",
                        model.validate(),
                        design=model._design.name,
                        board=model.name,
                    )
                )
                diagnostics.extend(
                    _report_diagnostics(
                        run.stage.name,
                        f"pcb:{model.name}",
                        "logical.pcb_ready",
                        model._design.validate_for_pcb(),
                        design=model._design.name,
                        board=model.name,
                    )
                )
    return tuple(diagnostics)


def _collect_library_diagnostics(libraries: tuple[Library, ...] | list[Library]):
    diagnostics: list[ProjectDiagnostic] = []
    for library in libraries:
        result = library.build()
        for diagnostic in result.diagnostics:
            diagnostics.append(
                ProjectDiagnostic(
                    stage="library",
                    source=diagnostic.source,
                    report=f"library:{library.namespace}",
                    severity=diagnostic.severity,
                    code=diagnostic.code,
                    message=diagnostic.message,
                    entities=diagnostic.entities,
                )
            )
    return tuple(diagnostics)


def _collect_rule_diagnostics(
    rule_suites: tuple[ProjectRuleSuiteDefinition, ...] | list[ProjectRuleSuiteDefinition],
    result: ProjectResult,
) -> tuple[ProjectDiagnostic, ...]:
    diagnostics: list[ProjectDiagnostic] = []
    context = ProjectRuleContext(result)
    for rule_suite in rule_suites:
        diagnostics.extend(
            _project_rule_diagnostics(rule_suite.name, rule_suite.function(context))
        )
    return tuple(diagnostics)


def _project_rule_diagnostics(
    rule_name: str,
    value: object,
) -> tuple[ProjectDiagnostic, ...]:
    if value is None:
        return ()
    if isinstance(value, ProjectDiagnostic):
        return (_rule_diagnostic(rule_name, value),)
    if isinstance(value, ProjectDiagnostics):
        return tuple(_rule_diagnostic(rule_name, diagnostic) for diagnostic in value)
    if isinstance(value, (list, tuple)):
        diagnostics: list[ProjectDiagnostic] = []
        for item in value:
            diagnostics.extend(_project_rule_diagnostics(rule_name, item))
        return tuple(diagnostics)
    raise TypeError("Project rule suites must return ProjectDiagnostic objects or sequences")


def _rule_diagnostic(rule_name: str, diagnostic: ProjectDiagnostic) -> ProjectDiagnostic:
    if diagnostic.rule == rule_name:
        return diagnostic
    return replace(diagnostic, rule=diagnostic.rule or rule_name)


def _report_diagnostics(
    stage: str,
    source: str,
    report: str,
    diagnostics,
    *,
    design: str | None = None,
    board: str | None = None,
):
    return [
        ProjectDiagnostic(
            stage=stage,
            source=source,
            report=report,
            severity=diagnostic.severity,
            code=diagnostic.code,
            message=diagnostic.message,
            entities=diagnostic.entities,
            design=design,
            board=board,
        )
        for diagnostic in diagnostics
    ]


def _check_for_stage(stage: ProjectStage, models: tuple[object, ...]):
    if stage.name == "design":
        return DesignCheck(_one_or_named(models, None, "design"))
    if stage.name == "schematic":
        return SchematicCheck(_one_or_named(models, None, "schematic"))
    if stage.name == "board":
        return BoardCheck(_one_or_named(models, None, "board"))
    raise RuntimeError(f"Unsupported project stage {stage.name}")


_SAFE_PATH_CHARS = re.compile(r"[^A-Za-z0-9._-]+")


def _safe_slug(name: str) -> str:
    cleaned = _SAFE_PATH_CHARS.sub("-", name.strip()).strip("-._")
    return cleaned or "model"


def _unique_path(relative: Path, used_paths: set[str]) -> Path:
    candidate = relative
    counter = 2
    while candidate.as_posix() in used_paths:
        candidate = relative.with_name(f"{relative.stem}-{counter}{relative.suffix}")
        counter += 1
    used_paths.add(candidate.as_posix())
    return candidate


def _write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def _prepare_bundle_root(root: Path) -> None:
    if root.exists() and not root.is_dir():
        raise NotADirectoryError(root)
    root.mkdir(parents=True, exist_ok=True)
    for directory in ("logical", "schematic", "pcb", "diagnostics"):
        shutil.rmtree(root / directory, ignore_errors=True)
    manifest = root / "manifest.volt.json"
    if manifest.exists():
        manifest.unlink()


def _write_json(path: Path, payload: object) -> None:
    _write_text(path, json.dumps(payload, indent=2, sort_keys=True) + "\n")


def _artifact_record(
    kind: str,
    name: str,
    path: Path,
    media_type: str,
    *,
    group: dict[str, str] | None = None,
) -> dict[str, object]:
    record: dict[str, object] = {
        "kind": kind,
        "name": name,
        "path": path.as_posix(),
        "media_type": media_type,
    }
    if group is not None:
        record["group"] = group
    return record


def _diagnostics_payload(diagnostics: ProjectDiagnostics) -> dict:
    return {
        "summary": _diagnostic_summary(diagnostics),
        "diagnostics": [
            {
                "stage": diagnostic.stage,
                "source": diagnostic.source,
                "report": diagnostic.report,
                "severity": diagnostic.severity,
                "code": diagnostic.code,
                "message": diagnostic.message,
                "entities": [_diagnostic_entity_payload(entity) for entity in diagnostic.entities],
                "design": diagnostic.design,
                "board": diagnostic.board,
                "rule": diagnostic.rule,
            }
            for diagnostic in diagnostics
        ],
    }


def _diagnostic_entity_payload(entity: object) -> object:
    if hasattr(entity, "kind") and hasattr(entity, "index"):
        return {"kind": entity.kind, "index": entity.index}
    return entity


def _diagnostic_summary(diagnostics: ProjectDiagnostics) -> dict[str, int]:
    summary = {"errors": 0, "infos": 0, "warnings": 0}
    for diagnostic in diagnostics:
        if diagnostic.severity == "error":
            summary["errors"] += 1
        elif diagnostic.severity == "warning":
            summary["warnings"] += 1
        else:
            summary["infos"] += 1
    return summary


def _tests_payload(tests: tuple[ProjectTestResult, ...]) -> dict:
    return {
        "summary": _test_summary(tests),
        "tests": [_test_result_payload(test) for test in tests],
    }


def _test_summary(tests: tuple[ProjectTestResult, ...]) -> dict[str, int]:
    return {
        "passed": sum(1 for test in tests if test.ok),
        "failed": sum(1 for test in tests if not test.ok),
    }


def _test_result_payload(test: ProjectTestResult) -> dict[str, object]:
    return {
        "stage": test.stage,
        "name": test.name,
        "ok": test.ok,
        "message": test.message,
    }
