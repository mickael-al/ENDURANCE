#!/usr/bin/env python3
"""Reproducible production-subsystem measurements; not whole-game FPS.

Linux: build a shared runner and use EGL. Windows: --write-config prepares the
same input for the standalone CMake/GLFW target (see README.md).
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
SOURCES=("ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainGpu.cpp",
         "ANTAGONIZ/Source/Miscellaneous/ProceduralTerrainLod.cpp",
         "ANTAGONIZ/Source/ProjectUser/EnduranceObjectPlacement.cpp",
         "ANTAGONIZ/Source/ProjectUser/TreeRenderGpu.cpp",
         "ANTAGONIZ/Source/ProjectUser/ProceduralTree.cpp",
         "ANTAGONIZ/Source/ProjectUser/ProceduralTreeBake.cpp",
         "ANTAGONIZ/Source/Rendering/Shader/ShaderCompilation.cpp",
         "ANTAGONIZ/Source/Rendering/Shader/ShaderBinaryCache.cpp",
         "ANTAGONIZ/Source/Rendering/Shader/ShaderProgramBatch.cpp",
         "ANTAGONIZ/Source/Rendering/Shader/ParallelShaderCompiler.cpp",
         "ANTAGONIZ/Source/Rendering/Shader/ComputeShader.cpp",
         "ANTAGONIZ/Source/Rendering/Shader/Atmosphere.cpp")

def scene(path):
    document=json.loads(path.read_text().replace("\0",""))
    def find(value):
        if isinstance(value,dict):
            if value.get("pathName")=="ProceduralTerrainComponent": return json.loads(value["data"])
            for item in value.values():
                result=find(item)
                if result is not None:return result
        elif isinstance(value,list):
            for item in value:
                result=find(item)
                if result is not None:return result
    settings=find(document)
    if settings is None: raise ValueError("Terrain component missing from scene")
    camera=document["freeCamPos"]
    return settings,[camera[key] for key in ("x","y","z")]

def build(root,library):
    library.parent.mkdir(parents=True,exist_ok=True)
    def inputs():
        support=sorted({path for base in (root/"ANTAGONIZ/Header",root/"ANTAGONIZ/Source",root/"Tests/Atmosphere/runtime_fixture")
                        for suffix in ("*.hpp","*.h","*.inl") for path in base.rglob(suffix)})
        return {"engine_root":str(root),"harness_sha256":hashlib.sha256((HERE/"benchmark.cpp").read_bytes()).hexdigest(),
            "instrumentation_sha256":hashlib.sha256((HERE/"gl_call_probe.hpp").read_bytes()).hexdigest(),
            "camera_fixture_sha256":hashlib.sha256((HERE/"runtime_fixture/Camera.hpp").read_bytes()).hexdigest(),
            "source_sha256":{path:hashlib.sha256((root/path).read_bytes()).hexdigest() for path in SOURCES},
            "support_sha256":{str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in support}}
    provenance=inputs()
    with tempfile.TemporaryDirectory(prefix="endurance-perf-build-") as temporary:
        stb=Path(temporary)/"stb.o"
        pending=Path(temporary)/"runner.so"
        subprocess.run([os.environ.get("CC","cc"),"-O2","-fPIC","-c",str(root/"ThirdParty/stb-cmake/stb_shared.c"),"-o",str(stb)],check=True)
        command=[os.environ.get("CXX","c++"),"-std=c++17","-O3","-ffast-math","-fPIC","-shared","-DGLEW_NO_GLU","-DENDURANCE_PERFORMANCE_SHARED","-pthread"]
        command.extend(["-include",str(HERE/"gl_call_probe.hpp")])
        command.extend(["-I",str(HERE/"runtime_fixture"),"-I",str(root/"Tests/Atmosphere/runtime_fixture")])
        if "allocationRevision" in (root/"ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainObjects.hpp").read_text():
            command.append("-DENDURANCE_PERF_ALLOCATION_REVISION")
        header=(root/"ANTAGONIZ/Header/ProjectUser/TreeRenderGpu.hpp").read_text()
        if all(name in header for name in ("transformedInstances","transformPagesBuilt","transformPagesReused","indirectCommands")):
            command.append("-DENDURANCE_PERF_INCREMENTAL_TREES")
        if "shadowContentRevision(" in header:
            command.append("-DENDURANCE_PERF_LAYER_TREE_REVISIONS")
        for path in ("ThirdParty","ThirdParty/glm","ANTAGONIZ/Header/Struct","ANTAGONIZ/Header/Miscellaneous",
                     "ANTAGONIZ/Header/Interface","ANTAGONIZ/Header/ProjectUser","ANTAGONIZ/Header/Rendering/Shader"):
            command.extend(["-I",str(root/path)])
        command.append(str(HERE/"benchmark.cpp")); command.extend(str(root/path) for path in SOURCES)
        glew=ctypes.util.find_library("GLEW")
        if not glew: raise RuntimeError("Install GLEW for the Linux runner or use the standalone CMake target")
        command.extend([str(stb),"-l:"+glew,"-l:libGL.so.1","-l:libEGL.so.1","-o",str(pending)])
        subprocess.run(command,check=True)
        if provenance!=inputs():
            raise RuntimeError("Sources changed during compilation; refusing to label a mixed build. Rebuild after edits finish.")
        shutil.copy2(pending,library)
        provenance["library_sha256"]=hashlib.sha256(library.read_bytes()).hexdigest()
        library.with_suffix(".manifest.json").write_text(json.dumps(provenance,indent=2)+"\n")

def shader_digest(root):
    paths=sorted((root/"Asset/Shader/ProceduralTerrain").rglob("*.glsl"))+sorted((root/"Asset/Shader/Noise").rglob("*.glsl"))+sorted((root/"ENDURANCE/Ressources/Shader/Trees").rglob("*.glsl"))+sorted((root/"Asset/Shader/Atmosphere").rglob("*.glsl"))
    digest=hashlib.sha256()
    for path in paths:
        digest.update(str(path.relative_to(root)).encode()); digest.update(path.read_bytes())
    return digest.hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine-root",type=Path,default=ROOT)
    parser.add_argument("--scene",type=Path)
    parser.add_argument("--scenario",choices=("selection","generation","trees"),default="selection")
    parser.add_argument("--cells",type=int,choices=(8,16,32,64),default=64)
    parser.add_argument("--jobs",type=int)
    parser.add_argument("--max-requests",type=int,default=32,help="0 generates the whole selected coverage; default is explicitly a subset")
    parser.add_argument("--objects",action="store_true")
    parser.add_argument("--disable-caches",action="store_true")
    parser.add_argument("--render-during-generation",action="store_true")
    parser.add_argument("--biome",type=int,choices=range(-1,26))
    parser.add_argument("--plane",action="store_true",help="Controlled flat-surface diagnostic, not the scene's terrain")
    parser.add_argument("--vertex-budget-mib",type=int)
    parser.add_argument("--timeout-seconds",type=int,default=180)
    parser.add_argument("--instances",type=int,default=64)
    parser.add_argument("--models",type=int,default=1)
    parser.add_argument("--frames",type=int,default=8)
    parser.add_argument("--resolution",type=int,default=320)
    parser.add_argument("--shadows",action="store_true")
    parser.add_argument("--shadow-layers",type=int,default=0,choices=range(5),help="Actual tree depth passes, 0–4; --shadows remains an alias for one")
    parser.add_argument("--shadow-resolution",type=int,default=128)
    parser.add_argument("--cache-shadows",action="store_true",help="Use production ShadowMapCache with renderer content revisions")
    parser.add_argument("--atmosphere",action="store_true",help="Add actual atmosphere/volumetric passes with explicitly reduced targets and Ultra sample counts")
    parser.add_argument("--library",type=Path,help="Reuse a runner compiled from the chosen engine root")
    parser.add_argument("--compile-only",action="store_true")
    parser.add_argument("--write-config",type=Path,help="Write standalone input JSON without compiling or running")
    parser.add_argument("--output",type=Path)
    args=parser.parse_args(); root=args.engine_root.resolve()
    terrain,camera=scene(args.scene.resolve() if args.scene else root/"ENDURANCE/Scene/Terrain.scene")
    options={key:value for key,value in vars(args).items() if key not in ("engine_root","scene","library","compile_only","write_config","output") and value is not None}
    options.update(root=str(root),harness=str(HERE),terrain=terrain,camera=camera)
    if args.write_config:
        args.write_config.parent.mkdir(parents=True,exist_ok=True); args.write_config.write_text(json.dumps(options,indent=2)+"\n")
        print(args.write_config); return 0
    if args.resolution<32 or args.resolution>4096 or args.max_requests<0 or args.jobs is not None and not 1<=args.jobs<=16:
        parser.error("Invalid resolution/request count/job count")
    with tempfile.TemporaryDirectory(prefix="endurance-perf-") as temporary:
        library=args.library.resolve() if args.library else Path(temporary)/"benchmark.so"
        if args.compile_only or not library.exists():build(root,library)
        if args.compile_only: print(library); return 0
        manifest=library.with_suffix(".manifest.json")
        provenance=json.loads(manifest.read_text()) if manifest.exists() else {"warning":"Legacy runner without build manifest; rebuild for source provenance"}
        if provenance.get("engine_root",str(root))!=str(root):raise ValueError("Runner was compiled against a different engine root")
        if provenance.get("library_sha256")!=hashlib.sha256(library.read_bytes()).hexdigest():raise ValueError("Runner binary does not match its manifest")
        shaders_before=shader_digest(root)
        runner=ctypes.CDLL(str(library)); run=runner.endurance_performance
        run.argtypes=[ctypes.c_char_p]; run.restype=ctypes.c_char_p
        os.environ.setdefault("ENDURANCE_SHADER_CACHE_DIR",str(Path(tempfile.gettempdir())/"endurance-performance-shaders"))
        os.environ.setdefault("MESA_GL_VERSION_OVERRIDE","4.6"); os.environ.setdefault("MESA_GLSL_VERSION_OVERRIDE","460")
        if args.scenario=="selection":result=json.loads(run(json.dumps(options).encode()))
        else:
            sys.path.insert(0,str(ROOT/"Tests/Terrain")); from gl_headless import GL
            with GL():result=json.loads(run(json.dumps(options).encode()))
    result["host"]={"platform":platform.platform(),"cpu":platform.processor(),"logical_cpus":os.cpu_count()}
    result["provenance"]=provenance
    shaders_after=shader_digest(root)
    result["provenance"]["runtime_shader_sha256"]=shaders_before
    if shaders_before!=shaders_after: result["error"]="Runtime shader files changed during measurement; result is not a valid comparison"
    catalogue=root/"ENDURANCE/Ressources/Trees/object_placement.json"
    result["provenance"]["placement_catalogue_sha256"]=hashlib.sha256(catalogue.read_bytes()).hexdigest()
    if args.scenario=="trees":
        assets=[]
        for prototype in json.loads(catalogue.read_text())["prototypes"][:args.models]:
            folder=catalogue.parent/prototype["asset"]
            digest=hashlib.sha256()
            for path in sorted(folder.rglob("*")):
                if path.is_file():
                    digest.update(str(path.relative_to(folder)).encode()); digest.update(path.read_bytes())
            assets.append({"asset":prototype["asset"],"sha256":digest.hexdigest()})
        result["provenance"]["baked_assets"]=assets
    try:
        result["provenance"]["commit"]=subprocess.check_output(["git","rev-parse","HEAD"],cwd=root,text=True).strip()
        result["provenance"]["dirty"]=bool(subprocess.check_output(["git","status","--porcelain"],cwd=root,text=True))
    except (OSError,subprocess.CalledProcessError): pass
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True); args.output.write_text(json.dumps(result,indent=2)+"\n")
        print(args.output)
    else: print(json.dumps(result,indent=2))
    if "error" in result: print(result["error"],file=sys.stderr);return 1
    if result.get("software_renderer"): print("Software GL: do not extrapolate these results to game FPS on a hardware GPU.")
    if args.scenario=="generation" and not result["result"]["completed_fixture"]:return 2
    return 0

if __name__=="__main__": raise SystemExit(main())
