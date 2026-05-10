#!/usr/bin/env python3
"""Check Volt build-system performance and measurement configuration."""

from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parents[1]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read(path: str) -> str:
    file_path = ROOT / path
    require(file_path.exists(), f"{path} must exist")
    return file_path.read_text(encoding="utf-8")


def check_test_presets() -> None:
    presets = json.loads(read("CMakePresets.json"))
    test_presets = {preset["name"]: preset for preset in presets["testPresets"]}
    workflow_presets = {preset["name"]: preset for preset in presets["workflowPresets"]}

    for name in ("dev", "asan"):
        preset = test_presets[name]
        jobs = preset.get("execution", {}).get("jobs")
        require(isinstance(jobs, int) and jobs >= 2, f"{name} test preset must run tests in parallel")
        require(name in workflow_presets, f"{name} workflow preset must exist")


def check_fetchcontent() -> None:
    io_cmake = read("src/io/CMakeLists.txt")

    require("URL " in io_cmake, "nlohmann/json must be fetched from a release archive")
    require("URL_HASH SHA256=" in io_cmake, "nlohmann/json archive must be hash-pinned")
    require(
        "GIT_REPOSITORY https://github.com/nlohmann/json.git" not in io_cmake,
        "nlohmann/json must not use a full Git clone",
    )
    require(
        "FETCHCONTENT_UPDATES_DISCONNECTED" in read("CMakeLists.txt"),
        "FetchContent dependency update checks must be disconnected by default",
    )


def check_coverage() -> None:
    compiler_options = read("cmake/VoltCompilerOptions.cmake")
    top_level = read("CMakeLists.txt")
    ci_workflow = read(".github/workflows/ci.yml")
    presets = json.loads(read("CMakePresets.json"))
    configure_presets = {preset["name"]: preset for preset in presets["configurePresets"]}
    build_presets = {preset["name"]: preset for preset in presets["buildPresets"]}
    workflow_presets = {preset["name"]: preset for preset in presets["workflowPresets"]}

    require("VOLT_ENABLE_COVERAGE" in compiler_options, "coverage option must be declared")
    require("volt_apply_coverage" in compiler_options, "coverage helper must be available")
    require("--coverage" in compiler_options, "coverage helper must add compiler coverage flags")
    require("coverage-report" in top_level, "coverage report target must be declared")
    require("LCOV_EXECUTABLE" in top_level, "coverage report target must use lcov")
    require("${PROJECT_SOURCE_DIR}/include/volt/*" in top_level, "coverage report must include public Volt headers")
    require("${PROJECT_SOURCE_DIR}/src/*" in top_level, "coverage report must include Volt source files")
    require("coverage" in configure_presets, "coverage configure preset must exist")
    require("coverage-report" in build_presets, "coverage-report build preset must exist")
    require("coverage" in workflow_presets, "coverage workflow preset must exist")
    require("cmake --workflow --preset coverage" in ci_workflow, "CI must run the coverage workflow")
    require("--fail-under-lines 80" in ci_workflow, "CI must enforce the 80% line coverage floor")

    for path in (
        "src/core/CMakeLists.txt",
        "src/python/CMakeLists.txt",
        "tests/CMakeLists.txt",
        "examples/CMakeLists.txt",
        "benchmarks/CMakeLists.txt",
    ):
        require("volt_apply_coverage" in read(path), f"{path} must apply coverage to compiled targets")


def check_benchmarks() -> None:
    top_level = read("CMakeLists.txt")
    presets = json.loads(read("CMakePresets.json"))
    configure_presets = {preset["name"]: preset for preset in presets["configurePresets"]}
    build_presets = {preset["name"]: preset for preset in presets["buildPresets"]}
    workflow_presets = {preset["name"]: preset for preset in presets["workflowPresets"]}

    require("VOLT_BUILD_BENCHMARKS" in top_level, "benchmark build option must be declared")
    require("add_subdirectory(benchmarks)" in top_level, "benchmarks directory must be conditionally added")
    require((ROOT / "benchmarks" / "CMakeLists.txt").exists(), "benchmarks CMake file must exist")
    require((ROOT / "benchmarks" / "kernel_benchmarks.cpp").exists(), "starter benchmark executable must exist")
    require("benchmarks" in configure_presets, "benchmarks configure preset must exist")
    require("benchmarks" in build_presets, "benchmarks build preset must exist")
    require("benchmarks" in workflow_presets, "benchmarks workflow preset must exist")


def main() -> int:
    checks = (check_test_presets, check_fetchcontent, check_coverage, check_benchmarks)
    failures = []
    for check in checks:
        try:
            check()
        except AssertionError as exc:
            failures.append(str(exc))

    if failures:
        for failure in failures:
            print(f"build-config check failed: {failure}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
