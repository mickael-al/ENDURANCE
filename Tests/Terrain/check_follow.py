#!/usr/bin/env python3
"""Link the production camera/terrain integration fixture against an existing Linux CMake build."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',required=True,type=Path,help='Built CMake tree with compile_commands.json')
    parser.add_argument('--link-command',type=Path,help='Optional {cwd, argv} JSON for a build with replacement object files')
    args=parser.parse_args();build=args.build_dir.resolve()
    entries=json.loads((build/'compile_commands.json').read_text())
    template=next(c for c in entries if Path(c['file']).name=='ProceduralGeneration.cpp')
    if args.link_command:
        link=json.loads(args.link_command.read_text())
    else:
        path=build/'ANTAGONIZ/CMakeFiles/ANTAGONIZ.dir/link.txt'
        link={'cwd':str(build/'ANTAGONIZ'),'argv':shlex.split(path.read_text())}
    with tempfile.TemporaryDirectory(prefix='terrain-follow-') as temporary:
        temporary=Path(temporary);obj=temporary/'check_follow.o';exe=temporary/'check_follow'
        compile_command=shlex.split(template['command'])
        compile_command[compile_command.index('-o')+1]=str(obj)
        compile_command[compile_command.index('-c')+1]=str(ROOT/'Tests/Terrain/check_follow.cpp')
        subprocess.run(compile_command,cwd=template['directory'],check=True)
        command=[];main_removed=False
        for argument in link['argv']:
            # The optional replacement build uses flattened source-relative names.
            if argument.endswith('/Source/Main.cpp.o') or argument.endswith('/Source_Main.cpp.o'):
                main_removed=True;continue
            command.append(argument)
        if not main_removed:
            raise RuntimeError('Cannot identify Main.cpp.o in link command; refusing to link two mains')
        command[command.index('-o')+1]=str(exe)
        command.insert(command.index('-o'),str(obj));command.extend(['-l:libEGL.so.1'])
        subprocess.run(command,cwd=link['cwd'],check=True)
        environment=os.environ.copy();environment.setdefault('EGL_PLATFORM','surfaceless')
        return subprocess.run([str(exe),str(ROOT/'Asset/Shader/ProceduralTerrain')],cwd=ROOT,env=environment).returncode

if __name__=='__main__':
    raise SystemExit(main())
