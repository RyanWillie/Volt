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


def check_ci_tooling() -> None:
    ci_workflow = read(".github/workflows/ci.yml")

    require("graphviz" not in ci_workflow, "CI must not install Graphviz when Doxygen dot output is disabled")
    require(
        "HAVE_DOT               = NO" in read("docs/Doxyfile.in"),
        "Doxygen dot output must be disabled when CI does not install Graphviz",
    )
    require(
        "choco install" not in ci_workflow,
        "Windows CI must use preinstalled hosted-runner tools instead of Chocolatey installs",
    )
    require(
        "if: runner.os == 'Linux'\n        run: cmake --build --preset dev --target docs" in ci_workflow,
        "CI must build documentation only on Ubuntu",
    )


def check_python_pytest_harness() -> None:
    python_cmake = read("src/python/CMakeLists.txt")
    ci_workflow = read(".github/workflows/ci.yml")
    gitignore = read(".gitignore")
    pytest_config = read("pytest.ini")
    requirements = read("requirements-dev.txt")
    install_script = read("scripts/install-python-dev-deps.py")

    require("pytest" in requirements, "pytest must be an explicit dev/test dependency")
    require("testpaths = python/tests" in pytest_config, "pytest discovery must be scoped to Python tests")
    require("python_files = test_*.py" in pytest_config, "pytest file discovery must stay explicit")
    require("--tb=short" in pytest_config, "pytest failures must keep useful tracebacks in CTest output")
    require(".pytest_cache/" in gitignore, "pytest cache output must be ignored")
    require(
        "python scripts/install-python-dev-deps.py" in ci_workflow,
        "CI must install Python dev/test dependencies through the CMake-selected interpreter",
    )
    require(
        '"cmake", "--preset", "dev"' in install_script,
        "Python dev dependency installer must discover the dev preset interpreter",
    )
    require(
        "Python3_EXECUTABLE" in install_script,
        "Python dev dependency installer must install into CMake's selected interpreter",
    )
    require(
        "${Python3_EXECUTABLE} -m pytest" in python_cmake,
        "Python tests must run pytest through CMake's selected interpreter",
    )
    require(
        "-p no:cacheprovider" in python_cmake,
        "CTest Python pytest entries must not share pytest cache state across parallel processes",
    )
    require(
        "PYTHONPATH=path_list_prepend:${PROJECT_BINARY_DIR}/python" in python_cmake,
        "Python pytest entries must import the built Python package from the build tree",
    )
    require(
        "Effective PYTHONPATH: build tree, source root, test helpers." in python_cmake,
        "Python pytest entries must document the path precedence that imports the built extension",
    )
    require(
        'string(REPLACE "\\\\" "." test_name ${test_name})' in python_cmake,
        "Python pytest CTest names must handle native Windows separators",
    )
    require(
        "test_runner.py" not in python_cmake,
        "Python CTest registration must not depend on the old custom runner",
    )


def check_python_package_build() -> None:
    pyproject = read("pyproject.toml")
    python_cmake = read("src/python/CMakeLists.txt")
    runtime_copy_cmake = read("cmake/VoltCopyRuntimeDependencies.cmake")
    ci_workflow = read(".github/workflows/ci.yml")
    gitignore = read(".gitignore")
    requirements = read("requirements-dev.txt")
    build_script = read("scripts/build-python-wheel.py")
    smoke_script = read("scripts/smoke-python-wheel.py")

    require('name = "volt-eda"' in pyproject, "Python distribution name must be volt-eda")
    require('requires-python = ">=3.10"' in pyproject, "Python package must declare Python 3.10+")
    require(
        'build-backend = "scikit_build_core.build"' in pyproject,
        "Python package must use scikit-build-core",
    )
    require(
        'wheel.packages = ["python/volt"]' in pyproject,
        "Python wheel must include the public volt package from python/volt",
    )
    require(
        'VOLT_BUILD_PYTHON = "ON"' in pyproject,
        "Python wheel build must enable the C++ extension",
    )
    for option in ("VOLT_BUILD_TESTS", "VOLT_BUILD_DOCS", "VOLT_BUILD_EXAMPLES", "VOLT_BUILD_BENCHMARKS"):
        require(
            f'{option} = "OFF"' in pyproject,
            f"Python wheel build must disable {option}",
        )
    require("install(TARGETS _volt" in python_cmake, "Python extension target must be installed")
    require("LIBRARY DESTINATION volt" in python_cmake, "Python extension library must install into the volt package")
    require("RUNTIME DESTINATION volt" in python_cmake, "Python extension runtime must install into the volt package")
    require("sys.base_prefix" in python_cmake, "Windows wheel builds must search the base Python runtime")
    require("Vv][Cc][Rr][Uu][Nn][Tt][Ii][Mm][Ee" in runtime_copy_cmake, "Windows wheels must not vendor MSVC runtime DLLs")
    require("Mm][Ss][Vv][Cc][Pp" in runtime_copy_cmake, "Windows wheels must not vendor MSVC C++ runtime DLLs")
    require(
        'CMAKE_POSITION_INDEPENDENT_CODE = "ON"' in pyproject,
        "Python wheel build must force position-independent code",
    )
    require("build>=1.2" in requirements, "Python dev dependencies must include the build frontend")
    require("dist/" in gitignore, "Python wheel artifacts must be ignored")
    require("Python3_EXECUTABLE" in build_script, "Python wheel build must use CMake's selected interpreter")
    require("Python3_EXECUTABLE" in smoke_script, "Python wheel smoke test must use CMake's selected interpreter")
    require("tags:" in ci_workflow, "CI must run on release tag pushes for Python wheels")
    require("- 'v*'" in ci_workflow, "Python wheel tag trigger must be version-tag scoped")
    require(
        ci_workflow.count("if: startsWith(github.ref, 'refs/tags/')") >= 3,
        "Python wheel build, smoke test, and artifact upload must only run for tag pushes",
    )
    require("python scripts/build-python-wheel.py" in ci_workflow, "CI must build the Python wheel")
    require(
        "python scripts/smoke-python-wheel.py" in ci_workflow,
        "CI must smoke-install the built Python wheel",
    )
    require("actions/upload-artifact" in ci_workflow, "CI must retain the built Python wheel as an artifact")
    require("dist/*.whl" in ci_workflow, "CI wheel artifact must include built wheels")
    require("twine upload" not in ci_workflow, "CI must not publish to PyPI yet")
    require("pypa/gh-action-pypi-publish" not in ci_workflow, "CI must not publish to PyPI yet")


def main() -> int:
    checks = (
        check_test_presets,
        check_fetchcontent,
        check_coverage,
        check_benchmarks,
        check_ci_tooling,
        check_python_pytest_harness,
        check_python_package_build,
    )
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
