"""Structured project workflow for Volt Python authoring."""

from __future__ import annotations

import json
import re
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable

from ._project_model_lookup import model_output_name, one_or_named, one_or_named_projection
from ._project_models3d import collect_project_part_models_3d, copy_part_model_3d_asset
from .design import Design
from .diagnostics import DiagnosticEntity, DiagnosticMeasurement, DiagnosticOverlay
from .library import Library
from .pcb import Board
from .project_checks import (
    BoardStageChecks,
    DesignStageChecks,
    SchematicStageChecks,
)
from .schematic import Schematic


BuildFunction = Callable[..., object]


@dataclass(frozen=True)
class ProjectTestDefinition:
    """Registered product-intent test for one project stage."""

    name: str
    function: BuildFunction


@dataclass(frozen=True)
class StageResult:
    """Summary for one executed project stage."""

    name: str
    model_count: int
    tests: tuple["ProjectTestResult", ...] = ()


@dataclass(frozen=True)
class ProjectResource:
    """Explicit non-canonical authoring resource produced by a project stage."""

    name: str
    value: object


@dataclass(frozen=True)
class ProjectDiagnostic:
    """Diagnostic emitted during one project run with project source metadata."""

    stage: str
    source: str
    report: str
    severity: str
    code: str
    message: str
    entities: tuple[DiagnosticEntity, ...]
    category: str = "general"
    overlays: tuple[DiagnosticOverlay, ...] = ()
    measurement: DiagnosticMeasurement | None = None
    design: str | None = None
    board: str | None = None
    rule: str | None = None

    @property
    def expect_diagnostic_kwargs(self) -> dict[str, str]:
        """Return copyable ``Project.expect_diagnostic`` keyword arguments."""
        return _expect_diagnostic_kwargs(self)


@dataclass(frozen=True)
class ProjectTestResult:
    """Result of one product-intent project test."""

    stage: str
    name: str
    ok: bool
    message: str = ""


@dataclass(frozen=True)
class ExpectedDiagnostic:
    """Project-local policy for a known diagnostic."""

    code: str
    severity: str | None = None
    stage: str | None = None
    source: str | None = None
    report: str | None = None
    design: str | None = None
    board: str | None = None
    rule: str | None = None

    @property
    def expect_diagnostic_kwargs(self) -> dict[str, str]:
        """Return this policy as ``Project.expect_diagnostic`` keyword arguments."""
        return _expect_diagnostic_kwargs(self)

    def matches(self, diagnostic: ProjectDiagnostic) -> bool:
        """Return whether this expectation covers a project diagnostic."""
        return (
            self.code == diagnostic.code
            and (self.severity is None or self.severity == diagnostic.severity)
            and (self.stage is None or self.stage == diagnostic.stage)
            and (self.source is None or self.source == diagnostic.source)
            and (self.report is None or self.report == diagnostic.report)
            and (self.design is None or self.design == diagnostic.design)
            and (self.board is None or self.board == diagnostic.board)
            and (self.rule is None or self.rule == diagnostic.rule)
        )


@dataclass(frozen=True)
class ExpectedDiagnosticResult:
    """Match result for one expected diagnostic policy item."""

    code: str
    severity: str | None
    stage: str | None
    source: str | None
    report: str | None
    design: str | None
    board: str | None
    rule: str | None
    matched: bool

    @property
    def expect_diagnostic_kwargs(self) -> dict[str, str]:
        """Return the checked policy as ``Project.expect_diagnostic`` keyword arguments."""
        return _expect_diagnostic_kwargs(self)


@dataclass(frozen=True)
class ProjectArtifactPaths:
    """Flat artifact paths written for example and viewer consumers."""

    logical_json: Path | None = None
    schematic_json: Path | None = None
    schematic_svg: Path | None = None
    schematic_body_svg: Path | None = None
    schematic_svg_pages: tuple[Path, ...] = ()
    pcb_json: Path | None = None
    pcb_svg: Path | None = None
    pcb_layer_svgs: tuple[Path, ...] = ()
    kicad_pcb: Path | None = None
    diagnostics_json: Path | None = None


@dataclass(frozen=True)
class _BundlePolicySnapshot:
    """Diagnostic policy snapshot for one profile-specific project-result bundle."""

    diagnostics: "ProjectDiagnostics"
    tests: tuple[ProjectTestResult, ...]
    ok: bool
    status: str


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

    def __init__(self, *, designs: Iterable[Design], resources: Iterable[ProjectResource] = ()):
        self._designs = tuple(designs)
        self._resources = tuple(resources)

    @property
    def designs(self) -> tuple[Design, ...]:
        """Return designs produced by the project design stage."""
        return self._designs

    @property
    def resources(self) -> tuple[ProjectResource, ...]:
        """Return explicit authoring resources produced by prior stages."""
        return self._resources

    def design(self, name: str | None = None) -> Design:
        """Return a design by stable name, or the only design for the common case."""
        return one_or_named(self._designs, name, "design", "Project context")

    def resource(self, name: str, expected_type: type | tuple[type, ...] | None = None) -> object:
        """Return a named authoring resource from an earlier stage."""
        matches = [resource.value for resource in self._resources if resource.name == name]
        if len(matches) == 1:
            value = matches[0]
            if expected_type is not None and not isinstance(value, expected_type):
                expected = _type_phrase(expected_type)
                raise TypeError(f"Project resource {name} must be a {expected}")
            return value
        if len(matches) > 1:
            raise LookupError(f"Project context has multiple resources named {name}")
        raise LookupError(f"Project context has no resource named {name}")


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
    resources: tuple[ProjectResource, ...]
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
        self._libraries: list[Library] = []
        self._expected_diagnostics: list[ExpectedDiagnostic] = []

    def use_library(self, library: Library) -> Library:
        """Include a library's validation diagnostics in this project result."""
        if not isinstance(library, Library):
            raise TypeError("Project.use_library expects a Library")
        if library not in self._libraries:
            self._libraries.append(library)
        return library

    def expect_diagnostic(
        self,
        diagnostic: ProjectDiagnostic | ExpectedDiagnostic | ExpectedDiagnosticResult | None = None,
        *,
        code: str | None = None,
        severity: str | None = None,
        stage: ProjectStage | str | None = None,
        source: str | None = None,
        report: str | None = None,
        design: str | None = None,
        board: str | None = None,
        rule: str | None = None,
    ) -> ExpectedDiagnostic:
        """Declare a project-local diagnostic that is expected for this run."""
        if diagnostic is not None:
            if not isinstance(
                diagnostic,
                (ProjectDiagnostic, ExpectedDiagnostic, ExpectedDiagnosticResult),
            ):
                raise TypeError("Project.expect_diagnostic diagnostic must be a project diagnostic")
            if any(
                value is not None
                for value in (code, severity, stage, source, report, design, board, rule)
            ):
                raise ValueError(
                    "Project.expect_diagnostic cannot combine a diagnostic instance "
                    "with explicit match fields"
                )
            fields = diagnostic.expect_diagnostic_kwargs
            code = fields["code"]
            severity = fields.get("severity")
            stage = fields.get("stage")
            source = fields.get("source")
            report = fields.get("report")
            design = fields.get("design")
            board = fields.get("board")
            rule = fields.get("rule")
        if not isinstance(code, str) or not code:
            raise ValueError("Expected diagnostic code must be a non-empty string")
        expectation = ExpectedDiagnostic(
            code=code,
            severity=severity,
            stage=_stage_name(stage),
            source=source,
            report=report,
            design=design,
            board=board,
            rule=rule,
        )
        self._expected_diagnostics.append(expectation)
        return expectation

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
        resources: list[ProjectResource] = []

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
                argument = BuildContext(designs=designs, resources=resources)
                result = stage._function(argument)
            stage_models, stage_resources = self._collect_stage_return(stage, result)
            for model in stage_models:
                if id(model) not in model_ids:
                    models.append(model)
                    model_ids.add(id(model))
            if stage is self.design:
                designs = stage_models
            resources.extend(stage_resources)
            tests = self._run_stage_tests(stage, stage_models)
            runs.append(_StageRun(stage, stage_models, stage_resources, tests))
            if stop_stage is stage:
                break

        return ProjectResult(self, runs=runs, models=models)

    def _collect_stage_return(
        self,
        stage: ProjectStage,
        value: object,
    ) -> tuple[tuple[object, ...], tuple[ProjectResource, ...]]:
        models: list[object] = []
        model_ids: set[int] = set()
        resources: list[ProjectResource] = []
        self._collect_return(models, model_ids, resources, value)
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
        return tuple(models), tuple(resources)

    def _collect_return(
        self,
        models: list[object],
        model_ids: set[int],
        resources: list[ProjectResource],
        value: object,
    ) -> None:
        if value is None:
            return
        if _is_known_model(value):
            if id(value) not in model_ids:
                models.append(value)
                model_ids.add(id(value))
            return
        if isinstance(value, ProjectResource):
            if not isinstance(value.name, str) or not value.name:
                raise ValueError("Project resource names must be non-empty strings")
            resources.append(value)
            return
        if isinstance(value, (list, tuple)):
            for item in value:
                self._collect_return(models, model_ids, resources, item)
            return
        raise TypeError(
            "Project build stages must return Volt models, ProjectResource values, "
            "or lists/tuples of those values"
        )

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
            )
        )
        self._expected_diagnostic_results = _expected_diagnostic_results(
            self.project._expected_diagnostics,
            self._diagnostics,
        )
        self._expected_project_diagnostics = tuple(
            diagnostic
            for diagnostic in self._diagnostics
            if _matches_any_expected(diagnostic, self.project._expected_diagnostics)
        )
        self._unexpected_diagnostics = tuple(
            diagnostic
            for diagnostic in self._diagnostics
            if not _matches_any_expected(diagnostic, self.project._expected_diagnostics)
        )

    @property
    def ok(self) -> bool:
        """Return whether the project run has no errors."""
        if self.project._expected_diagnostics:
            return self.expected_diagnostics_ok and not self.test_failures()
        return not self._diagnostics.has_errors and not self.test_failures()

    @property
    def clean(self) -> bool:
        """Return whether the project run has no diagnostics or test failures."""
        return len(self._diagnostics) == 0 and not self.test_failures()

    @property
    def status(self) -> str:
        """Return clean, expected-diagnostics, or failed project status."""
        if self.clean:
            return "clean"
        if self.ok:
            return "expected-diagnostics"
        return "failed"

    @property
    def diagnostics(self) -> ProjectDiagnostics:
        """Return diagnostics collected from default project checks."""
        return self._diagnostics

    @property
    def expected_diagnostics(self) -> tuple[ExpectedDiagnosticResult, ...]:
        """Return declared expected diagnostic policy with match status."""
        return self._expected_diagnostic_results

    @property
    def expected_project_diagnostics(self) -> tuple[ProjectDiagnostic, ...]:
        """Return collected diagnostics covered by expected diagnostic policy."""
        return self._expected_project_diagnostics

    @property
    def unexpected_diagnostics(self) -> tuple[ProjectDiagnostic, ...]:
        """Return collected diagnostics not covered by expected diagnostic policy."""
        return self._unexpected_diagnostics

    @property
    def missing_expected_diagnostics(self) -> tuple[ExpectedDiagnosticResult, ...]:
        """Return expected diagnostics that did not appear in the project run."""
        return tuple(result for result in self._expected_diagnostic_results if not result.matched)

    @property
    def expected_diagnostics_ok(self) -> bool:
        """Return whether all diagnostics are expected and all expectations matched."""
        if not self.project._expected_diagnostics:
            return not self._diagnostics.has_errors
        return not self._unexpected_diagnostics and not self.missing_expected_diagnostics

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

    def design(self, name: str | None = None) -> Design:
        """Return a collected design by name, or the only design."""
        return one_or_named(self.designs, name, "design", "Project result")

    def schematic(self, name: str | None = None) -> Schematic:
        """Return a collected schematic by name, or the only schematic."""
        return one_or_named_projection(
            self.schematics, name, "schematic", "Project result"
        )

    def board(self, name: str | None = None) -> Board:
        """Return a collected board by name, or the only board."""
        return one_or_named_projection(self.boards, name, "board", "Project result")

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

    def write(self, path: str | Path, *, profile: str = "default") -> None:
        """Write a deterministic project result bundle, optionally with viewer-profile checks."""
        if profile not in {"default", "viewer"}:
            raise ValueError("ProjectResult.write profile must be 'default' or 'viewer'")
        root = Path(path)
        _prepare_bundle_root(root)

        used_paths: set[str] = set()
        artifacts: list[dict[str, object]] = []
        board_documents: list[tuple[Board, str]] = []
        for model in self._models:
            if isinstance(model, Design):
                design_text = model.to_json()
                relative = _unique_path(
                    Path("logical") / f"{_safe_slug(model.name)}.volt.json",
                    used_paths,
                )
                _write_text(root / relative, design_text)
                artifacts.append(
                    _artifact_record(
                        "logical",
                        model.name,
                        relative,
                        "application/vnd.volt.logical+json",
                        group={"design": model.name},
                    )
                )
                bom_json_path, bom_csv_path = _bom_artifact_paths(model, self.designs)
                relative_bom_json = _unique_path(bom_json_path, used_paths)
                relative_bom_csv = _unique_path(bom_csv_path, used_paths)
                _write_json(root / relative_bom_json, model.bom())
                _write_text(root / relative_bom_csv, model.bom_csv())
                artifacts.append(
                    _artifact_record(
                        "bom",
                        model.name,
                        relative_bom_json,
                        "application/vnd.volt.bom+json",
                        group={"design": model.name},
                    )
                )
                artifacts.append(
                    _artifact_record(
                        "bom_csv",
                        model.name,
                        relative_bom_csv,
                        "text/csv",
                        group={"design": model.name},
                    )
                )
                if model._has_sourcing_snapshot():
                    relative_sourcing = _unique_path(
                        _bom_sourcing_artifact_path(model, self.designs),
                        used_paths,
                    )
                    _write_text(root / relative_sourcing, model._bom_sourcing_snapshot_json())
                    artifacts.append(
                        _artifact_record(
                            "bom_sourcing_snapshot",
                            model.name,
                            relative_sourcing,
                            "application/vnd.volt.bom-sourcing-snapshot+json",
                            group={"design": model.name},
                        )
                    )
            elif isinstance(model, Schematic):
                output_name = model_output_name(model, self.schematics)
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
                relative_body_svg = _unique_path(
                    Path("schematic") / f"{_safe_slug(output_name)}.body.svg",
                    used_paths,
                )
                _write_text(root / relative_body_svg, model.to_body_svg())
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
                artifacts.append(
                    _artifact_record(
                        "schematic_body_svg",
                        output_name,
                        relative_body_svg,
                        "image/svg+xml",
                        group=group,
                    )
                )
                artifacts.extend(
                    self._write_schematic_page_artifacts(
                        root=root,
                        model=model,
                        output_name=output_name,
                        used_paths=used_paths,
                        group=group,
                    )
                )
            elif isinstance(model, Board):
                output_name = model_output_name(model, self.boards)
                board_text = model.to_json()
                relative_json = _unique_path(
                    Path("pcb") / f"{_safe_slug(output_name)}.volt.pcb.json",
                    used_paths,
                )
                relative_svg = _unique_path(
                    Path("pcb") / f"{_safe_slug(output_name)}.svg",
                    used_paths,
                )
                _write_text(root / relative_json, board_text)
                _write_text(root / relative_svg, model.to_svg())
                kicad_export = model.to_kicad_pcb()
                relative_kicad = _unique_path(
                    Path("pcb") / f"{_safe_slug(output_name)}.kicad_pcb",
                    used_paths,
                )
                _write_text(root / relative_kicad, kicad_export.text)
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
                artifacts.append(
                    _artifact_record(
                        "kicad_pcb",
                        output_name,
                        relative_kicad,
                        "application/x-kicad-pcb",
                        group=group,
                    )
                )
                board_documents.append((model, output_name))

        model_artifacts, model_diagnostics = self._write_part_model_3d_artifacts(
            root=root,
            boards=board_documents,
            used_paths=used_paths,
            profile=profile,
        )
        artifacts.extend(model_artifacts)

        bundle_policy = _bundle_policy_snapshot(self, extra_diagnostics=model_diagnostics)
        diagnostics_path = _unique_path(Path("diagnostics") / "diagnostics.json", used_paths)
        tests_path = _unique_path(Path("diagnostics") / "tests.json", used_paths)
        _write_json(
            root / diagnostics_path,
            _diagnostics_payload(
                self,
                diagnostics=bundle_policy.diagnostics,
                status=bundle_policy.status,
            ),
        )
        _write_json(root / tests_path, _tests_payload(bundle_policy.tests))
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
                "ok": bundle_policy.ok,
                "profile": profile,
                "status": bundle_policy.status,
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
                    "summary": _diagnostic_summary(bundle_policy.diagnostics),
                    "status": bundle_policy.status,
                },
                "tests": {
                    "path": tests_path.as_posix(),
                    "summary": _test_summary(bundle_policy.tests),
                },
            },
        )

    def write_artifacts(
        self,
        path: str | Path,
        *,
        slug: str | None = None,
        pcb_svg_options: dict[str, object] | None = None,
    ) -> ProjectArtifactPaths:
        """Write standard flat example/viewer artifacts and return their paths."""
        root = Path(path)
        root.mkdir(parents=True, exist_ok=True)
        base = slug or _safe_slug(self.project.name)
        design = self.design() if self.designs else None
        schematic = self.schematic() if self.schematics else None
        board = self.board() if self.boards else None

        logical_json = root / f"{base}.volt.json" if design is not None else None
        schematic_json = (
            root / f"{base}.volt.schematic.json" if schematic is not None else None
        )
        schematic_svg = root / f"{base}.svg" if schematic is not None else None
        schematic_body_svg = root / f"{base}.body.svg" if schematic is not None else None
        schematic_pages_dir = root / f"{base}.pages"
        pcb_json = root / f"{base}.volt.pcb.json" if board is not None else None
        pcb_svg = root / f"{base}.pcb.svg" if board is not None else None
        kicad_pcb = root / f"{base}.kicad_pcb" if board is not None else None
        diagnostics_json = root / f"{base}.validation.json"
        svg_options = dict(pcb_svg_options or {})
        separate_layers = svg_options.pop("separate_layers", False)
        if not isinstance(separate_layers, bool):
            raise TypeError("pcb_svg_options['separate_layers'] must be a boolean")
        if separate_layers and "layer" in svg_options:
            raise ValueError(
                "pcb_svg_options cannot include 'layer' when separate_layers is enabled"
            )

        if design is not None and logical_json is not None:
            _write_text(logical_json, design.to_json())
        schematic_svg_pages: tuple[Path, ...] = ()
        if schematic is not None:
            _write_text(schematic_json, schematic.to_json())
            _write_text(schematic_svg, schematic.to_svg())
            _write_text(schematic_body_svg, schematic.to_body_svg())
            shutil.rmtree(schematic_pages_dir, ignore_errors=True)
            schematic_svg_pages = schematic.write_svg_pages(schematic_pages_dir, prefix=base)
        pcb_layer_svgs: tuple[Path, ...] = ()
        if board is not None:
            pcb_json_text = board.to_json()
            _write_text(pcb_json, pcb_json_text)
            _write_text(pcb_svg, board.to_svg(**svg_options))
            if separate_layers:
                layer_paths: list[Path] = []
                layers = json.loads(pcb_json_text)["board"]["layers"]
                tokens = _pcb_svg_layer_filename_tokens([str(layer["name"]) for layer in layers])
                for layer, token in zip(layers, tokens):
                    layer_index = int(str(layer["id"]).removeprefix("board_layer:"))
                    layer_svg = root / f"{base}.pcb.{token}.svg"
                    _write_text(layer_svg, board.to_svg(**svg_options, layer=layer_index))
                    layer_paths.append(layer_svg)
                pcb_layer_svgs = tuple(layer_paths)
            _write_text(kicad_pcb, board.to_kicad_pcb().text)
        _write_json(diagnostics_json, _flat_diagnostics_payload(self))

        return ProjectArtifactPaths(
            logical_json=logical_json,
            schematic_json=schematic_json,
            schematic_svg=schematic_svg,
            schematic_body_svg=schematic_body_svg,
            schematic_svg_pages=schematic_svg_pages,
            pcb_json=pcb_json,
            pcb_svg=pcb_svg,
            pcb_layer_svgs=pcb_layer_svgs,
            kicad_pcb=kicad_pcb,
            diagnostics_json=diagnostics_json,
        )

    def _write_schematic_page_artifacts(
        self,
        *,
        root: Path,
        model: Schematic,
        output_name: str,
        used_paths: set[str],
        group: dict[str, str],
    ) -> list[dict[str, object]]:
        records: list[dict[str, object]] = []
        used_page_names: set[str] = set()
        for page in model.to_svg_pages():
            stem = _safe_slug(str(page["name"]))
            filename = f"{stem}.svg"
            if filename in used_page_names:
                filename = f"{stem}-sheet-{page['sheet']}.svg"
            used_page_names.add(filename)
            relative = _unique_path(
                Path("schematic") / f"{_safe_slug(output_name)}.pages" / filename,
                used_paths,
            )
            _write_text(root / relative, str(page["svg"]))
            records.append(
                _artifact_record(
                    "schematic_page_svg",
                    f"{output_name}:{page['name']}",
                    relative,
                    "image/svg+xml",
                    group=group,
                )
            )
        return records

    def _test_results(self) -> tuple[ProjectTestResult, ...]:
        return tuple(test for stage in self._stages for test in stage.tests)

    def _write_part_model_3d_artifacts(
        self,
        *,
        root: Path,
        boards: list[tuple[Board, str]],
        used_paths: set[str],
        profile: str,
    ) -> tuple[list[dict[str, object]], tuple[ProjectDiagnostic, ...]]:
        artifacts: list[dict[str, object]] = []
        bundle = collect_project_part_models_3d(boards, profile=profile)
        asset_paths: dict[str, str] = {}

        for asset in bundle.assets:
            relative_asset = _unique_path(
                Path("assets") / "models" / f"{asset.sha256}{asset.suffix}",
                used_paths,
            )
            copy_part_model_3d_asset(asset, root / relative_asset)
            asset_paths[asset.id] = relative_asset.as_posix()

        if bundle.assets or bundle.models:
            registry_path = _unique_path(Path("assets") / "part_models_3d.json", used_paths)
            _write_json(
                root / registry_path,
                {
                    "format": "volt.part_models_3d_registry",
                    "version": 1,
                    "assets": [
                        {
                            "id": asset.id,
                            "format": asset.format,
                            "path": asset_paths[asset.id],
                            "sha256": asset.sha256,
                        }
                        for asset in bundle.assets
                    ],
                    "models": [
                        {
                            "id": model.id,
                            "asset": model.asset,
                            "file_name": model.file_name,
                            "translation_mm": list(model.translation_mm),
                            "rotation_deg": model.rotation_deg,
                        }
                        for model in bundle.models
                    ],
                },
            )
            artifacts.append(
                _artifact_record(
                    "part_models_3d",
                    "Project part models",
                    registry_path,
                    "application/json",
                )
            )
            for asset in bundle.assets:
                artifacts.append(
                    _artifact_record(
                        "part_model_asset",
                        asset.id,
                        Path(asset_paths[asset.id]),
                        "application/octet-stream",
                        sha256=asset.sha256,
                    )
                )
        for board_record in bundle.boards:
            group = {"design": board_record.board._design.name, "board": board_record.board.name}
            relative = _unique_path(
                Path("pcb") / f"{_safe_slug(board_record.output_name)}.volt.models3d.json",
                used_paths,
            )
            _write_json(
                root / relative,
                {
                    "format": "volt.part_models_3d",
                    "version": 1,
                    "board": {
                        "design": board_record.board._design.name,
                        "name": board_record.board.name,
                    },
                    "placements": [
                        {
                            "placement": f"component_placement:{placement.placement}",
                            "component": f"component:{placement.component}",
                            "reference": placement.reference,
                            "model": placement.model,
                            "transform_matrix": placement.transform_matrix,
                        }
                        for placement in board_record.placements
                    ],
                },
            )
            artifacts.append(
                _artifact_record(
                    "pcb_models_3d",
                    board_record.output_name,
                    relative,
                    "application/json",
                    group=group,
                )
            )

        return artifacts, tuple(
            _part_model_3d_diagnostic(
                board=item.board,
                profile=profile,
                reference=item.reference,
                message=item.message,
            )
            for item in bundle.missing
        )


def _is_known_model(value: object) -> bool:
    return isinstance(value, (Design, Schematic, Board))


def _type_phrase(expected_type: type | tuple[type, ...]) -> str:
    if isinstance(expected_type, tuple):
        return " or ".join(item.__name__ for item in expected_type)
    return expected_type.__name__


def _matches_any_expected(
    diagnostic: ProjectDiagnostic,
    expectations: Iterable[ExpectedDiagnostic],
) -> bool:
    return any(expectation.matches(diagnostic) for expectation in expectations)


def _expect_diagnostic_kwargs(
    diagnostic: ProjectDiagnostic | ExpectedDiagnostic | ExpectedDiagnosticResult,
) -> dict[str, str]:
    return {
        key: value
        for key, value in {
            "code": diagnostic.code,
            "severity": diagnostic.severity,
            "stage": diagnostic.stage,
            "source": diagnostic.source,
            "report": diagnostic.report,
            "design": diagnostic.design,
            "board": diagnostic.board,
            "rule": diagnostic.rule,
        }.items()
        if value is not None
    }


def _expected_diagnostic_results(
    expectations: Iterable[ExpectedDiagnostic],
    diagnostics: ProjectDiagnostics,
) -> tuple[ExpectedDiagnosticResult, ...]:
    return tuple(
        ExpectedDiagnosticResult(
            code=expectation.code,
            severity=expectation.severity,
            stage=expectation.stage,
            source=expectation.source,
            report=expectation.report,
            design=expectation.design,
            board=expectation.board,
            rule=expectation.rule,
            matched=any(expectation.matches(diagnostic) for diagnostic in diagnostics),
        )
        for expectation in expectations
    )


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
                diagnostics.extend(
                    _report_diagnostics(
                        run.stage.name,
                        f"logical:{model.name}",
                        "logical.bom_ready",
                        model.validate_bom_readiness(),
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
                diagnostics.extend(
                    _report_diagnostics(
                        run.stage.name,
                        f"pcb:{model.name}",
                        "pcb.kicad_export",
                        model.to_kicad_pcb().diagnostics,
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
                    category=getattr(diagnostic, "category", "general"),
                    overlays=getattr(diagnostic, "overlays", ()),
                    measurement=getattr(diagnostic, "measurement", None),
                )
            )
    return tuple(diagnostics)


def _is_diagnostic_entity(entity: object) -> bool:
    return (
        hasattr(entity, "kind")
        and isinstance(entity.kind, str)
        and hasattr(entity, "index")
        and isinstance(entity.index, int)
    )


def _report_diagnostics(
    stage: str,
    source: str,
    report: str,
    diagnostics,
    *,
    design: str | None = None,
    board: str | None = None,
    rule: str | None = None,
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
            category=diagnostic.category,
            overlays=diagnostic.overlays,
            measurement=getattr(diagnostic, "measurement", None),
            design=design,
            board=board,
            rule=rule if rule is not None else getattr(diagnostic, "rule", None),
        )
        for diagnostic in diagnostics
    ]


def _check_for_stage(stage: ProjectStage, models: tuple[object, ...]):
    if stage.name == "design":
        return DesignStageChecks(models)
    if stage.name == "schematic":
        return SchematicStageChecks(models)
    if stage.name == "board":
        return BoardStageChecks(models)
    raise RuntimeError(f"Unsupported project stage {stage.name}")


_SAFE_PATH_CHARS = re.compile(r"[^A-Za-z0-9._-]+")
_PCB_SVG_LAYER_TOKEN_CHARS = re.compile(r"[^A-Za-z0-9]+")


def _safe_slug(name: str) -> str:
    cleaned = _SAFE_PATH_CHARS.sub("-", name.strip()).strip("-._")
    return cleaned or "model"


def _bom_artifact_paths(model: Design, designs: tuple[Design, ...]) -> tuple[Path, Path]:
    if len(designs) == 1:
        return Path("bom") / "bom.json", Path("bom") / "bom.csv"
    slug = _safe_slug(model.name)
    return Path("bom") / f"{slug}.bom.json", Path("bom") / f"{slug}.bom.csv"


def _bom_sourcing_artifact_path(model: Design, designs: tuple[Design, ...]) -> Path:
    if len(designs) == 1:
        return Path("bom") / "sourcing.json"
    return Path("bom") / f"{_safe_slug(model.name)}.sourcing.json"


def _pcb_svg_layer_filename_token(layer_name: str) -> str:
    cleaned = _PCB_SVG_LAYER_TOKEN_CHARS.sub("_", layer_name.strip()).strip("_")
    return cleaned or "layer"


def _pcb_svg_layer_filename_tokens(layer_names: list[str]) -> list[str]:
    seen: dict[str, int] = {}
    tokens: list[str] = []
    for name in layer_names:
        token = _pcb_svg_layer_filename_token(name)
        count = seen.get(token, 0) + 1
        seen[token] = count
        tokens.append(token if count == 1 else f"{token}_{count}")
    return tokens


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


def _bundle_policy_snapshot(
    result: ProjectResult,
    *,
    extra_diagnostics: tuple[ProjectDiagnostic, ...],
) -> _BundlePolicySnapshot:
    diagnostics = (
        result.diagnostics
        if not extra_diagnostics
        else ProjectDiagnostics((*result.diagnostics, *extra_diagnostics))
    )
    tests = result._test_results()
    return _BundlePolicySnapshot(
        diagnostics=diagnostics,
        tests=tests,
        ok=_bundle_ok(result, diagnostics, tests),
        status=_bundle_status(result, diagnostics, tests),
    )


def _bundle_ok(
    result: ProjectResult,
    diagnostics: ProjectDiagnostics,
    tests: tuple[ProjectTestResult, ...],
) -> bool:
    if result.project._expected_diagnostics:
        return _expected_diagnostics_ok(
            result.project._expected_diagnostics,
            diagnostics,
        ) and not _test_summary(tests)["failed"]
    return not diagnostics.has_errors and not _test_summary(tests)["failed"]


def _bundle_status(
    result: ProjectResult,
    diagnostics: ProjectDiagnostics,
    tests: tuple[ProjectTestResult, ...],
) -> str:
    if len(diagnostics) == 0 and not _test_summary(tests)["failed"]:
        return "clean"
    if _bundle_ok(result, diagnostics, tests):
        return "expected-diagnostics"
    return "failed"


def _expected_diagnostics_ok(
    expectations: Iterable[ExpectedDiagnostic],
    diagnostics: ProjectDiagnostics,
) -> bool:
    expected = tuple(expectations)
    if not expected:
        return not diagnostics.has_errors
    _matches, unexpected, missing = _diagnostic_policy_snapshot(expected, diagnostics)
    return not unexpected and not missing


def _part_model_3d_diagnostic(
    *,
    board: Board,
    profile: str,
    reference: str,
    message: str,
) -> ProjectDiagnostic:
    return ProjectDiagnostic(
        stage="bundle",
        source=f"pcb:{board.name}",
        report=f"project.bundle:{profile}",
        severity="error",
        code="PROJECT_PART_MODEL_3D_MISSING",
        message=f"{reference}: {message}",
        entities=(),
        category="viewer",
        design=board._design.name,
        board=board.name,
    )


def _prepare_bundle_root(root: Path) -> None:
    if root.exists() and not root.is_dir():
        raise NotADirectoryError(root)
    root.mkdir(parents=True, exist_ok=True)
    if any(root.iterdir()) and not _is_project_result_bundle(root):
        raise FileExistsError(
            f"Refusing to overwrite {root}: not an existing Volt project-result bundle"
        )
    for directory in ("logical", "schematic", "pcb", "bom", "diagnostics", "assets"):
        shutil.rmtree(root / directory, ignore_errors=True)
    manifest = root / "manifest.volt.json"
    if manifest.exists():
        manifest.unlink()


def _is_project_result_bundle(root: Path) -> bool:
    manifest = root / "manifest.volt.json"
    if not manifest.exists():
        return False
    try:
        payload = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return False
    return (
        payload.get("format") == "volt.project_result"
        and payload.get("schema_version") == 1
    )


def _write_json(path: Path, payload: object) -> None:
    _write_text(path, json.dumps(payload, indent=2, sort_keys=True) + "\n")


def _artifact_record(
    kind: str,
    name: str,
    path: Path,
    media_type: str,
    *,
    group: dict[str, str] | None = None,
    sha256: str | None = None,
) -> dict[str, object]:
    record: dict[str, object] = {
        "kind": kind,
        "name": name,
        "path": path.as_posix(),
        "media_type": media_type,
    }
    if group is not None:
        record["group"] = group
    if sha256 is not None:
        record["sha256"] = sha256
    return record


def _diagnostics_payload(
    result: ProjectResult,
    *,
    diagnostics: ProjectDiagnostics | None = None,
    status: str | None = None,
) -> dict:
    diagnostics = result.diagnostics if diagnostics is None else diagnostics
    status = result.status if status is None else status
    expected, unexpected, missing = _diagnostic_policy_snapshot(
        result.project._expected_diagnostics,
        diagnostics,
    )
    return {
        "status": status,
        "summary": _diagnostic_summary(diagnostics),
        "diagnostics": [
            _project_diagnostic_payload(diagnostic)
            for diagnostic in diagnostics
        ],
        "expected": [
            _expected_diagnostic_result_payload(expectation)
            for expectation in expected
        ],
        "unexpected": [
            _project_diagnostic_payload(diagnostic)
            for diagnostic in unexpected
        ],
        "missing_expected": [
            _expected_diagnostic_result_payload(expectation)
            for expectation in missing
        ],
    }


def _flat_diagnostics_payload(
    result: ProjectResult,
    *,
    diagnostics: ProjectDiagnostics | None = None,
    status: str | None = None,
) -> dict:
    diagnostics = tuple(result.diagnostics if diagnostics is None else diagnostics)
    status = result.status if status is None else status
    expected, unexpected, missing = _diagnostic_policy_snapshot(
        result.project._expected_diagnostics,
        ProjectDiagnostics(diagnostics),
    )
    reports: dict[str, dict] = {}
    for report_name in _flat_report_names(diagnostics):
        report_diagnostics = tuple(
            diagnostic for diagnostic in diagnostics if _flat_report_name(diagnostic) == report_name
        )
        reports[report_name] = {
            "summary": _diagnostic_summary(ProjectDiagnostics(report_diagnostics)),
            "diagnostics": [_flat_diagnostic_payload(diagnostic) for diagnostic in report_diagnostics],
        }
    return {
        "status": status,
        "summary": _diagnostic_summary(ProjectDiagnostics(diagnostics)),
        "diagnostics": [
            {"source": _flat_report_name(diagnostic), **_flat_diagnostic_payload(diagnostic)}
            for diagnostic in diagnostics
        ],
        "reports": reports,
        "expected": [
            _expected_diagnostic_result_payload(expectation)
            for expectation in expected
        ],
        "unexpected": [
            {"source": _flat_report_name(diagnostic), **_flat_diagnostic_payload(diagnostic)}
            for diagnostic in unexpected
        ],
        "missing_expected": [
            _expected_diagnostic_result_payload(expectation)
            for expectation in missing
        ],
    }


def _diagnostic_policy_snapshot(
    expectations: Iterable[ExpectedDiagnostic],
    diagnostics: ProjectDiagnostics,
) -> tuple[
    tuple[ExpectedDiagnosticResult, ...],
    tuple[ProjectDiagnostic, ...],
    tuple[ExpectedDiagnosticResult, ...],
]:
    expected = tuple(expectations)
    matches = _expected_diagnostic_results(expected, diagnostics)
    unexpected = tuple(
        diagnostic for diagnostic in diagnostics if not _matches_any_expected(diagnostic, expected)
    )
    missing = tuple(result for result in matches if not result.matched)
    return matches, unexpected, missing


def _flat_report_names(diagnostics: tuple[ProjectDiagnostic, ...]) -> tuple[str, ...]:
    names: list[str] = []
    for diagnostic in diagnostics:
        name = _flat_report_name(diagnostic)
        if name not in names:
            names.append(name)
    for name in (
        "logical_design",
        "pcb_board",
        "pcb_readiness",
        "schematic_readability",
        "schematic_readiness",
    ):
        if name not in names:
            names.append(name)
    return tuple(names)


def _flat_report_name(diagnostic: ProjectDiagnostic) -> str:
    return {
        "logical.default": "logical_design",
        "logical.pcb_ready": "pcb_readiness",
        "schematic.readability": "schematic_readability",
        "schematic.readiness": "schematic_readiness",
        "pcb.board": "pcb_board",
    }.get(diagnostic.report, diagnostic.report)


def _flat_diagnostic_payload(diagnostic: ProjectDiagnostic) -> dict[str, object]:
    return {
        "severity": diagnostic.severity,
        "category": diagnostic.category,
        "code": diagnostic.code,
        "message": diagnostic.message,
        "entities": [_diagnostic_entity_payload(entity) for entity in diagnostic.entities],
        "overlays": [_diagnostic_overlay_payload(overlay) for overlay in diagnostic.overlays],
        "measurement": _diagnostic_measurement_payload(diagnostic.measurement),
        "rule": diagnostic.rule,
        "expect_diagnostic_kwargs": diagnostic.expect_diagnostic_kwargs,
    }


def _project_diagnostic_payload(diagnostic: ProjectDiagnostic) -> dict[str, object]:
    return {
        "stage": diagnostic.stage,
        "source": diagnostic.source,
        "report": diagnostic.report,
        "severity": diagnostic.severity,
        "category": diagnostic.category,
        "code": diagnostic.code,
        "message": diagnostic.message,
        "entities": [_diagnostic_entity_payload(entity) for entity in diagnostic.entities],
        "overlays": [_diagnostic_overlay_payload(overlay) for overlay in diagnostic.overlays],
        "measurement": _diagnostic_measurement_payload(diagnostic.measurement),
        "design": diagnostic.design,
        "board": diagnostic.board,
        "rule": diagnostic.rule,
        "expect_diagnostic_kwargs": diagnostic.expect_diagnostic_kwargs,
    }


def _expected_diagnostic_result_payload(result: ExpectedDiagnosticResult) -> dict[str, object]:
    return {
        "code": result.code,
        "severity": result.severity,
        "stage": result.stage,
        "source": result.source,
        "report": result.report,
        "design": result.design,
        "board": result.board,
        "rule": result.rule,
        "matched": result.matched,
        "expect_diagnostic_kwargs": result.expect_diagnostic_kwargs,
    }


def _diagnostic_entity_payload(entity: object) -> object:
    if _is_diagnostic_entity(entity):
        return {"kind": entity.kind, "index": entity.index}
    raise TypeError("Project diagnostic entities must be DiagnosticEntity references")


def _diagnostic_overlay_payload(overlay: object) -> dict[str, object]:
    if not isinstance(overlay, DiagnosticOverlay):
        raise TypeError("Project diagnostic overlays must be DiagnosticOverlay values")
    return {
        "kind": overlay.kind,
        "points": [[point[0], point[1]] for point in overlay.points],
        "entities": [_diagnostic_entity_payload(entity) for entity in overlay.entities],
        "layers": [_diagnostic_entity_payload(layer) for layer in overlay.layers],
    }


def _diagnostic_measurement_payload(measurement: object) -> dict[str, object] | None:
    if measurement is None:
        return None
    if not isinstance(measurement, DiagnosticMeasurement):
        raise TypeError("Project diagnostic measurement must be a DiagnosticMeasurement value")
    return {
        "actual_mm": measurement.actual_mm,
        "required_mm": measurement.required_mm,
    }


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
