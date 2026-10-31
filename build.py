#!/usr/bin/env python3
"""
Build script for gfx.

Configures, builds and tests the project with CMake. The project relies on C++ modules and
`import std;`, which CMake can only build with the Ninja generator, so Ninja is the default
(unlike the Makefile-based scripts of other projects).
"""

import argparse
import multiprocessing
import os
import platform
import shutil
import stat
import subprocess
import sys
from pathlib import Path
from typing import Callable, List, Mapping

# This script lives in the project root.
PROJECT_DIRECTORY = Path(__file__).resolve().parent


def _remove_read_only(function: Callable[[str], object], path: str, _error: object) -> None:
    """Clears the read-only attribute (Git marks its object files read-only) and retries the removal."""
    os.chmod(path, stat.S_IWRITE)
    function(path)


def _matching_c_compiler(cxx_compiler: str) -> str:
    """Returns the C compiler of the toolchain of @p cxx_compiler (g++ -> gcc, clang++ -> clang)."""
    for cxx_name, c_name in (("clang++", "clang"), ("g++", "gcc"), ("c++", "cc")):
        if cxx_compiler.endswith(cxx_name):
            return cxx_compiler[: -len(cxx_name)] + c_name
    return cxx_compiler


class CMakeBuilder:
    """Drives the CMake configure, build and test steps for gfx."""

    def __init__(self, arguments: argparse.Namespace) -> None:
        self.arguments = arguments
        self.source_directory = Path(arguments.source_directory).resolve()
        build_directory = arguments.build_directory or f"build/{arguments.build_type.lower()}"
        self.build_directory = (self.source_directory / build_directory).resolve()
        self.environment = self._create_environment()

    def _create_environment(self) -> Mapping[str, str]:
        """Returns the process environment, with the MSYS2 UCRT64 toolchain prepended to PATH on Windows."""
        environment = dict(os.environ)
        if platform.system() == "Windows":
            toolchain_directory = Path(self.arguments.msys2_root) / "ucrt64" / "bin"
            if toolchain_directory.is_dir():
                environment["PATH"] = f"{toolchain_directory}{os.pathsep}{environment.get('PATH', '')}"
            else:
                print(f"Warning: MSYS2 toolchain directory not found: {toolchain_directory}")
        return environment

    def _run(self, command: List[str]) -> bool:
        print(f"> {' '.join(command)}", flush=True)
        try:
            return subprocess.run(command, env=self.environment, check=False).returncode == 0
        except FileNotFoundError as error:
            print(f"Error: cannot run '{command[0]}': {error}")
            return False

    def configure(self) -> bool:
        print(f"\n{'=' * 70}\nConfiguring gfx in {self.build_directory}\n{'=' * 70}")
        command = [
            "cmake", "-S", str(self.source_directory), "-B", str(self.build_directory),
            "-G", self.arguments.generator,
            f"-DCMAKE_BUILD_TYPE={self.arguments.build_type}",
            f"-DCMAKE_C_COMPILER={self._compiler_path(self.arguments.c_compiler or _matching_c_compiler(self.arguments.compiler))}",
            f"-DCMAKE_CXX_COMPILER={self._compiler_path(self.arguments.compiler)}",
            f"-DGFX_BUILD_TESTS={'ON' if self.arguments.build_tests else 'OFF'}",
            f"-DGFX_WARNINGS_AS_ERRORS={'ON' if self.arguments.warnings_as_errors else 'OFF'}",
            # Development builds build every backend (the GPU physics backend is OFF by default for consumers).
            "-DGFX_PHYSICS_BACKEND_GPU_XPBD=ON",
        ]
        if self.arguments.import_std_gate:
            command.append(f"-DGFX_IMPORT_STD_GATE={self.arguments.import_std_gate}")
        if self.arguments.install_prefix:
            command.append(f"-DCMAKE_INSTALL_PREFIX={self.arguments.install_prefix}")
        if self.arguments.verbose_configure:
            command.extend(["--trace", "--trace-expand"])
        return self._run(command) and self._check_build_type()

    def _compiler_path(self, compiler: str) -> str:
        """Returns the absolute path of @p compiler found on the build PATH (or @p compiler itself).

        CMake deletes the cache when a compiler differs from the cached one, and the configure step that
        follows loses every other -D option (the build type among them); absolute paths compare exactly.
        """
        found = shutil.which(compiler, path=self.environment.get("PATH"))
        return Path(found).as_posix() if found else compiler

    def _check_build_type(self) -> bool:
        """Fails when the cache does not hold the requested build type (e.g. after CMake reset it)."""
        cache = self.build_directory / "CMakeCache.txt"
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_BUILD_TYPE:"):
                configured = line.split("=", 1)[1]
                if configured != self.arguments.build_type:
                    print(f"Error: {self.build_directory} is configured as {configured!r}, not {self.arguments.build_type!r}.")
                    print(f"  Suggestion: python build.py --build-type {self.arguments.build_type} --clean-all, then build again.")
                    return False
                return True
        return True

    def build(self) -> bool:
        print(f"\n{'=' * 70}\nBuilding gfx\n{'=' * 70}")
        if self.arguments.clean_first:
            self._run(["cmake", "--build", str(self.build_directory), "--target", "clean"])
        jobs = self.arguments.jobs if self.arguments.jobs > 0 else multiprocessing.cpu_count()
        command = ["cmake", "--build", str(self.build_directory), "--parallel", str(jobs)]
        if self.arguments.target:
            command.extend(["--target", self.arguments.target])
        if self.arguments.verbose_build:
            command.append("--verbose")
        return self._run(command)

    def test(self) -> bool:
        print(f"\n{'=' * 70}\nRunning tests\n{'=' * 70}")
        command = ["ctest", "--test-dir", str(self.build_directory), "--output-on-failure", "--parallel"]
        if self.arguments.jobs > 0:
            command.append(str(self.arguments.jobs))
        if self.arguments.test_regex:
            command.extend(["--tests-regex", self.arguments.test_regex])
        if self.arguments.verbose_tests:
            command.append("--verbose")
        return self._run(command)

    def install(self) -> bool:
        print(f"\n{'=' * 70}\nInstalling gfx\n{'=' * 70}")
        command = ["cmake", "--install", str(self.build_directory)]
        if self.arguments.install_prefix:
            command.extend(["--prefix", self.arguments.install_prefix])
        return self._run(command)

    def list_targets(self) -> bool:
        return self._run(["cmake", "--build", str(self.build_directory), "--target", "help"])

    def remove_build_directory(self) -> bool:
        if self.build_directory.exists():
            if sys.version_info >= (3, 12):
                shutil.rmtree(self.build_directory, onexc=_remove_read_only)
            else:
                shutil.rmtree(self.build_directory, onerror=_remove_read_only)
            print(f"Removed {self.build_directory}")
        return True


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Configure, build and test gfx with CMake.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python build.py                          # Debug build into build/debug
  python build.py --test                   # build, then run all tests in parallel
  python build.py --build-type Release --test
  python build.py --test --test-regex Toolchain
  python build.py --clean-all              # delete the build directory
""")
    parser.add_argument("--source-directory", default=str(PROJECT_DIRECTORY),
                        help="Project source directory (default: directory of this script)")
    parser.add_argument("--build-directory",
                        help="Build directory, relative to the source directory (default: build/<build type>)")
    parser.add_argument("-G", "--generator", default="Ninja",
                        help="CMake generator (default: Ninja; required for C++ modules with `import std`)")
    parser.add_argument("--compiler", default="g++", help="C++ compiler (default: g++)")
    parser.add_argument("--c-compiler", default="",
                        help="C compiler, for the C dependencies (default: the one matching --compiler)")
    parser.add_argument("--msys2-root", default=os.environ.get("MSYS2_ROOT", "C:/msys64"),
                        help="MSYS2 installation whose ucrt64/bin is prepended to PATH on Windows "
                             "(default: $MSYS2_ROOT or C:/msys64)")
    parser.add_argument("--build-type", choices=["Debug", "Release", "RelWithDebInfo", "MinSizeRel"],
                        default="Debug", help="Build type (default: Debug)")
    parser.add_argument("--import-std-gate",
                        help="UUID for CMake's experimental `import std` gate (see CMakeLists.txt)")
    parser.add_argument("--no-tests", dest="build_tests", action="store_false",
                        help="Do not build the unit tests")
    parser.add_argument("--no-warnings-as-errors", dest="warnings_as_errors", action="store_false",
                        help="Do not treat compiler warnings as errors")

    parser.add_argument("--configure-only", action="store_true", help="Only run the configure step")
    parser.add_argument("--clean-first", action="store_true", help="Clean before building")
    parser.add_argument("--clean-all", action="store_true", help="Delete the build directory and exit")
    parser.add_argument("--target", help="Build only this target")
    parser.add_argument("--list-targets", action="store_true", help="List the available targets and exit")
    parser.add_argument("-j", "--jobs", type=int, default=0,
                        help=f"Parallel jobs for building and testing (default: {multiprocessing.cpu_count()})")

    parser.add_argument("--test", action="store_true", help="Run the tests (in parallel) after building")
    parser.add_argument("--test-regex", help="Run only the tests matching this regular expression")
    parser.add_argument("--install", action="store_true", help="Install after building")
    parser.add_argument("--install-prefix", help="Installation prefix")

    parser.add_argument("--verbose-configure", action="store_true", help="Trace the CMake configure step")
    parser.add_argument("--verbose-build", action="store_true", help="Show the full compiler command lines")
    parser.add_argument("--verbose-tests", action="store_true", help="Show the full test output")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    builder = CMakeBuilder(arguments)

    if arguments.clean_all:
        return 0 if builder.remove_build_directory() else 1
    if arguments.list_targets:
        return 0 if builder.list_targets() else 1

    if not builder.configure():
        print("Configuration failed.")
        return 1
    if arguments.configure_only:
        return 0
    if not builder.build():
        print("Build failed.")
        return 1
    if arguments.test and not builder.test():
        print("Tests failed.")
        return 1
    if arguments.install and not builder.install():
        print("Installation failed.")
        return 1

    print(f"\nDone. Build artifacts are in {builder.build_directory}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
