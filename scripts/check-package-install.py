#!/usr/bin/env python3
"""Install Volt to a scratch prefix and build a downstream consumer against the package.

This guards the CMake export set. Without it, a target added to the build but omitted from
`install(TARGETS ... EXPORT VoltTargets)` still passes every other gate, and downstream
`find_package(Volt)` breaks silently until a user reports it.
"""

from __future__ import annotations

from pathlib import Path
import platform
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "dev"
PREFIX_DIR = BUILD_DIR / "package-prefix"
CONSUMER_SOURCE_DIR = ROOT / "tests" / "packaging" / "consumer"
CONSUMER_BUILD_DIR = BUILD_DIR / "package-consumer"

EXPECTED_PACKAGE_FILES = (
    Path("VoltConfig.cmake"),
    Path("VoltConfigVersion.cmake"),
    Path("VoltTargets.cmake"),
)


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def cmake_cache_value(name: str) -> str:
    for line in (BUILD_DIR / "CMakeCache.txt").read_text(encoding="utf-8").splitlines():
        if line.startswith(f"{name}:"):
            return line.split("=", 1)[1]
    raise RuntimeError(f"{name} was not written to {BUILD_DIR / 'CMakeCache.txt'}")


def consumer_configure_command() -> list[str]:
    # Build the consumer with the same generator, compiler, and configuration that produced
    # the installed archives. A consumer can only link artifacts from a compatible toolchain,
    # so letting CMake pick a platform default is not a realistic downstream simulation. On
    # Windows it actively breaks: CMake defaults to Visual Studio/MSVC, which cannot consume
    # the GNU-format archives that Volt's Windows CI toolchain produces.
    command = [
        "cmake",
        "-S",
        str(CONSUMER_SOURCE_DIR),
        "-B",
        str(CONSUMER_BUILD_DIR),
        "-G",
        cmake_cache_value("CMAKE_GENERATOR"),
        f"-DCMAKE_CXX_COMPILER={cmake_cache_value('CMAKE_CXX_COMPILER')}",
        f"-DCMAKE_PREFIX_PATH={PREFIX_DIR}",
    ]

    build_type = cmake_cache_value("CMAKE_BUILD_TYPE")
    if build_type:
        command.append(f"-DCMAKE_BUILD_TYPE={build_type}")

    if platform.system() == "Darwin":
        # Volt's own presets pin this. Without it CMake selects the CommandLineTools SDK,
        # whose libc++ headers can require a newer clang than /usr/bin/c++ provides.
        command.append("-DCMAKE_OSX_SYSROOT=macosx")

    return command


def package_config_dir() -> Path:
    candidates = sorted(PREFIX_DIR.glob("lib*/cmake/Volt"))
    if not candidates:
        raise RuntimeError(f"No Volt package configuration directory under {PREFIX_DIR}")
    return candidates[0]


def check_installed_package_files() -> None:
    config_dir = package_config_dir()
    missing = [name for name in EXPECTED_PACKAGE_FILES if not (config_dir / name).exists()]
    if missing:
        raise RuntimeError(
            f"Installed package is missing {', '.join(str(name) for name in missing)} "
            f"in {config_dir}"
        )

    # The per-configuration file carries IMPORTED_LOCATION for each archive. Without it the
    # imported targets still resolve, so a consumer compiles and only fails at link time.
    if not list(config_dir.glob("VoltTargets-*.cmake")):
        raise RuntimeError(
            f"Installed package has no VoltTargets-<config>.cmake in {config_dir}, so imported "
            "targets would carry no library location"
        )

    if not (PREFIX_DIR / "include" / "volt" / "volt.hpp").exists():
        raise RuntimeError(f"Installed package is missing include/volt/volt.hpp under {PREFIX_DIR}")

    unexpected_roots = sorted(
        path.name for path in PREFIX_DIR.iterdir() if path.name not in {"include", "lib", "lib64"}
    )
    if unexpected_roots:
        raise RuntimeError(
            "VoltSDK installation contains non-SDK top-level entries: "
            + ", ".join(unexpected_roots)
        )


def main() -> int:
    if not (BUILD_DIR / "CMakeCache.txt").exists():
        raise RuntimeError(f"{BUILD_DIR} is not configured; run 'cmake --preset dev' first")

    for directory in (PREFIX_DIR, CONSUMER_BUILD_DIR):
        if directory.exists():
            shutil.rmtree(directory)

    run(
        [
            "cmake",
            "--install",
            str(BUILD_DIR),
            "--prefix",
            str(PREFIX_DIR),
            "--component",
            "VoltSDK",
        ]
    )
    check_installed_package_files()

    run(consumer_configure_command())
    run(["cmake", "--build", str(CONSUMER_BUILD_DIR)])
    run(["ctest", "--test-dir", str(CONSUMER_BUILD_DIR), "--output-on-failure"])

    print("package install check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
