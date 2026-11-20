#!/usr/bin/env python3
"""Roundtrip production baked trees and independently inspect portable glTF2.

No model importer is used for the independent inspection: buffer ranges,
accessor encodings and triangle indices are checked directly with Python.
"""
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def inspect_gltf(path):
    document=json.loads(path.read_text())
    assert document["asset"]["version"]=="2.0"
    buffers=[]
    for description in document["buffers"]:
        file=(path.parent/description["uri"]).resolve()
        assert file.is_relative_to(path.parent.resolve()),"Buffer escapes bake directory"
        data=file.read_bytes()
        assert len(data)==description["byteLength"],"Buffer byteLength differs from exported file"
        buffers.append(data)
    formats={5120:("b",1),5121:("B",1),5122:("h",2),5123:("H",2),5125:("I",4),5126:("f",4)}
    components={"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4,"MAT4":16}
    decoded=[]
    for accessor in document["accessors"]:
        view=document["bufferViews"][accessor["bufferView"]]
        code,size=formats[accessor["componentType"]]
        n=components[accessor["type"]]
        offset=view.get("byteOffset",0)+accessor.get("byteOffset",0)
        stride=view.get("byteStride",n*size)
        assert offset%size==0 and stride>=n*size
        end=offset+(accessor["count"]-1)*stride+n*size
        assert end<=view.get("byteOffset",0)+view["byteLength"]
        assert end<=len(buffers[view["buffer"]])
        values=[struct.unpack_from("<"+code*n,buffers[view["buffer"]],offset+i*stride) for i in range(accessor["count"])]
        assert all(math.isfinite(v) for row in values for v in row)
        for key,operation in (("min",min),("max",max)):
            if key in accessor:
                for component in range(n):
                    value=operation(row[component] for row in values)
                    assert abs(value-accessor[key][component])<=max(1e-5,abs(value)*1e-6),"Incorrect accessor bounds"
        decoded.append(values)
    triangles=0
    for mesh in document["meshes"]:
        for primitive in mesh["primitives"]:
            assert primitive.get("mode",4)==4
            attributes=primitive["attributes"]
            assert {"POSITION","NORMAL","TEXCOORD_0","COLOR_0"}.issubset(attributes)
            count=len(decoded[attributes["POSITION"]])
            assert all(len(decoded[index])==count for index in attributes.values())
            indices=decoded[primitive["indices"]]
            assert len(indices)%3==0 and all(0<=x[0]<count for x in indices)
            triangles+=len(indices)//3
            for normal in decoded[attributes["NORMAL"]]:
                assert abs(sum(x*x for x in normal)-1)<0.01
    assert triangles>0
    for image in document.get("images",[]):
        data=(path.parent/image["uri"]).read_bytes()
        assert data[:8]==b"\x89PNG\r\n\x1a\n", "Material image is not a PNG"
        w,h=struct.unpack_from(">II",data,16)
        assert w>0 and h>0
    for material in document.get("materials",[]):
        if material.get("alphaMode")=="MASK":
            assert 0<material.get("alphaCutoff",.5)<1 and material.get("doubleSided",False)
    print(f"Independent glTF inspection: {path.name}, {triangles} triangles, {len(decoded)} accessors")

def main():
    with tempfile.TemporaryDirectory(prefix="endurance-tree-bake-") as temporary:
        directory=Path(temporary)
        stb=directory/"stb.o"
        subprocess.run([os.environ.get("CC","cc"),"-O2","-c",str(ROOT/"ThirdParty/stb-cmake/stb_shared.c"),"-o",str(stb)],check=True)
        for label,flags in (("normal",["-O2"]),("fast",["-O3","-ffast-math"])):
            binary=directory/label
            command=[os.environ.get("CXX","c++"),"-std=c++17",*flags,"-I",str(ROOT/"ThirdParty"),"-I",str(ROOT/"ThirdParty/glm"),"-I",str(ROOT/"ANTAGONIZ/Header/ProjectUser")]
            command.extend(str(ROOT/path) for path in ("Tests/Trees/check_bake.cpp","ANTAGONIZ/Source/ProjectUser/ProceduralTree.cpp","ANTAGONIZ/Source/ProjectUser/ProceduralTreeBake.cpp"))
            subprocess.run(command+[str(stb),"-o",str(binary)],check=True)
            output=directory/(label+"-output")
            subprocess.run([str(binary),str(output)],check=True,timeout=120)
            files=list((output/"asset").glob("*.gltf"))
            assert files,"No portable glTF export"
            for file in files: inspect_gltf(file)

if __name__=="__main__": main()
