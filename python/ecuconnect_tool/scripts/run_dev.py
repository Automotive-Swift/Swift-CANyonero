#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REQUIRED_PYTHON_PACKAGES = ["typer", "rich", "pybind11", "python-can"]


def run(cmd: list[str], allow_failure: bool = False, **kwargs) -> int:
    print("+", " ".join(cmd))
    if allow_failure:
        return subprocess.run(cmd, **kwargs).returncode
    subprocess.check_call(cmd, **kwargs)
    return 0


def ensure_packages(python: str, skip_install: bool) -> None:
    missing: list[str] = []
    for package in REQUIRED_PYTHON_PACKAGES:
        try:
            __import__(package)
        except Exception:
            missing.append(package)
    if not missing:
        return
    if skip_install:
        raise SystemExit(f"Missing Python packages: {', '.join(missing)}")
    run([python, "-m", "pip", "install", "--user", *missing])


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run ECUconnect tool from the repo without installing it.",
        add_help=False,
    )
    parser.add_argument("-h", "--help", action="store_true", help="Show this help and ecuconnect-tool help.")
    parser.add_argument("--python", default=sys.executable, help="Python executable to use.")
    parser.add_argument("--skip-install", action="store_true", help="Do not auto-install dependencies.")
    parser.add_argument("--configure-only", action="store_true", help="Only run CMake configure.")
    parser.add_argument("--build-only", action="store_true", help="Only build the extension.")
    parser.add_argument("cli_args", nargs=argparse.REMAINDER, help="Arguments passed to ecuconnect-tool.")
    args = parser.parse_args()

    if shutil.which("cmake") is None:
        raise SystemExit("cmake is required and was not found in PATH")

    ensure_packages(args.python, args.skip_install)

    repo_root = Path(__file__).resolve().parents[3]
    package_root = repo_root / "python" / "ecuconnect_tool"
    module_root = package_root / "ecuconnect_tool"
    build_root = package_root / "build"

    pybind11_dir = subprocess.check_output(
        [args.python, "-m", "pybind11", "--cmakedir"], text=True
    ).strip()

    cmake_args = [
        "cmake",
        "-S",
        str(package_root),
        "-B",
        str(build_root),
        "-DCMAKE_BUILD_TYPE=Release",
        f"-Dpybind11_DIR={pybind11_dir}",
        f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={module_root}",
    ]
    run(cmake_args)

    if args.configure_only:
        return 0

    run(["cmake", "--build", str(build_root), "-j"])

    if args.build_only:
        return 0

    cli_args = args.cli_args
    if cli_args and cli_args[0] == "--":
        cli_args = cli_args[1:]

    global_opts = {"--endpoint", "--rx-buffer", "--tx-buffer"}
    forwarded: list[str] = []
    i = 0
    while i < len(cli_args):
        item = cli_args[i]
        if item in global_opts:
            if i + 1 >= len(cli_args):
                raise SystemExit(f"Missing value for {item}")
            forwarded.extend([item, cli_args[i + 1]])
            del cli_args[i : i + 2]
            continue
        if any(item.startswith(f"{opt}=") for opt in global_opts):
            forwarded.append(item)
            del cli_args[i]
            continue
        i += 1

    env = os.environ.copy()
    env["PYTHONPATH"] = f"{package_root}{os.pathsep}{env.get('PYTHONPATH', '')}"

    if args.help:
        parser.print_help()
        return run([args.python, "-m", "ecuconnect_tool.cli", "--help"], env=env, allow_failure=True)

    return run([args.python, "-m", "ecuconnect_tool.cli", *forwarded, *cli_args], env=env, allow_failure=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("Interrupted.")
        raise SystemExit(130)
