#!/usr/bin/env python3
"""Link a bounded scene to the production objects of an existing Ninja build.

No renderer substitute or source patching: only the executable's Main object is
replaced by this test entry point. Requires a completed offline GCC/Clang Ninja
build, an OpenGL display (or --xvfb), and the committed character assets.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def compile_smoke(build: Path, temporary: Path) -> Path:
    commands = subprocess.check_output(
        ["ninja", "-C", str(build), "-t", "commands", "ANTAGONIZ"], text=True
    ).splitlines()
    source = str(ROOT / "ANTAGONIZ/Source/Main.cpp")
    compile_line = next(line for line in commands if source in line and " -c " in line)
    command = shlex.split(compile_line)
    clean, skip = [], False
    for argument in command:
        if skip:
            skip = False
            continue
        if argument in ("-MT", "-MF"):
            skip = True
        elif argument not in ("-MD", "-MMD"):
            clean.append(argument)
    obj = temporary / "smoke.cpp.o"
    clean[clean.index("-o") + 1] = str(obj)
    clean[clean.index(source)] = str(ROOT / "Tests/Character/engine_smoke.cpp")
    subprocess.run(clean, cwd=build, check=True)
    link_line = next(line for line in reversed(commands) if "Source/Main.cpp.o" in line and " -o " in line)
    link = shlex.split(link_line)
    if link[:2] == [":", "&&"]:
        link = link[2:]
    if link[-2:] == ["&&", ":"]:
        link = link[:-2]
    link = [argument for argument in link if not argument.startswith("-Wl,--dependency-file=")]
    link.append("-rdynamic")
    for index, argument in enumerate(link):
        if argument.endswith("/Source/Main.cpp.o"):
            link[index] = str(obj)
    binary = temporary / "character-engine-smoke"
    link[link.index("-o") + 1] = str(binary)
    subprocess.run(link, cwd=build, check=True)
    return binary


def start_display(executable: Path, environment: dict, output: Path):
    # TCP also works in sandboxes that disallow filesystem Unix sockets.
    display = 91 + os.getpid() % 100
    environment["DISPLAY"] = f"127.0.0.1:{display}"
    environment["PATH"] = str(executable.parent) + os.pathsep + environment.get("PATH", "")
    xkb = executable.parent.parent / "share/X11/xkb"
    if xkb.is_dir():
        environment["XKB_CONFIG_ROOT"] = str(xkb)
    log = (output / "xvfb.log").open("w")
    server = subprocess.Popen(
        [str(executable), f":{display}", "-screen", "0", "1024x1024x24",
         "-nolisten", "unix", "-listen", "tcp", "-ac", "-noreset"],
        stdout=log, stderr=subprocess.STDOUT, env=environment,
    )
    x11 = ctypes.CDLL("libX11.so.6")
    x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x11.XOpenDisplay.restype = ctypes.c_void_p
    x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
    for _ in range(100):
        display_ptr = x11.XOpenDisplay(environment["DISPLAY"].encode())
        if display_ptr:
            x11.XCloseDisplay(display_ptr)
            return server, log
        if server.poll() is not None:
            break
        time.sleep(0.05)
    server.terminate()
    server.wait(timeout=5)
    log.close()
    raise RuntimeError("Xvfb did not accept a connection; see xvfb.log")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--xvfb", type=Path, help="Start this Xvfb alongside the engine")
    parser.add_argument("--timeout", type=float, default=180)
    parser.add_argument("--no-cloth", action="store_true", help="Diagnostic skinning-only garment comparison")
    parser.add_argument("--no-shadows", action="store_true", help="Diagnostic unshadowed lighting comparison")
    parser.add_argument("--no-normal-maps", action="store_true", help="Diagnostic geometry-normal comparison")
    parser.add_argument("--atmosphere", action="store_true", help="Run the existing game atmosphere at low quality, without terrain/clouds/moon")
    options = parser.parse_args()
    build, output = options.build_dir.resolve(), options.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment.setdefault("ALSOFT_DRIVERS", "null")
    for name in ("NO_CLOTH", "NO_SHADOWS", "NO_NORMAL_MAPS", "ATMOSPHERE"):
        if getattr(options, name.lower()):
            environment[f"ENDURANCE_SMOKE_{name}"] = "1"
    server, server_log = None, None
    with tempfile.TemporaryDirectory(prefix="character-engine-smoke-") as directory:
        binary = compile_smoke(build, Path(directory))
        try:
            if options.xvfb:
                server, server_log = start_display(options.xvfb.resolve(), environment, output)
            with (output / "engine.log").open("w") as log:
                subprocess.run([str(binary), str(ROOT), str(output)], check=True,
                               env=environment, stdout=log, stderr=subprocess.STDOUT,
                               timeout=options.timeout)
        finally:
            if server:
                server.terminate()
                server.wait(timeout=5)
                server_log.close()
    report = json.loads((output / "report.json").read_text())
    # PNG previews are convenience outputs, not a dependency of runtime checks.
    try:
        from PIL import Image
        for image in output.glob("*.ppm"):
            Image.open(image).save(image.with_suffix(".png"))
    except ImportError:
        pass
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
