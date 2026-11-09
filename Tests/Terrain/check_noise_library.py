#!/usr/bin/env python3
"""Exercise the standalone noise API on a real GL context.

Checks ranges, deterministic seeds, periodicity, independent cellular oracles
and value-shaping contracts. --reference-shaders additionally compares the
unchanged ice recipes, forced controls and densities with a previous checkout.
Automatic blends and cell metadata may evolve with the climate remapping.
"""
from __future__ import annotations

import argparse
import itertools
import math
from pathlib import Path
import random
import re
import struct

from gl_headless import GL, shader_source

ROOT = Path(__file__).resolve().parents[2]
NOISE = ROOT / "Asset/Shader/Noise"
TERRAIN = ROOT / "Asset/Shader/ProceduralTerrain"


class Probe:
    """Compile a selected expression; the library requires no terrain uniforms."""
    def __init__(self, gl):
        self.gl = gl
        self.programs = {}
        self.inputs = gl.buffer(0)
        self.output = gl.buffer(0)

    def sample(self, expression, points, seed=1337, dimension=2, extra=""):
        key = expression, dimension, extra
        if key not in self.programs:
            include = "noise_shaping.glsl" if dimension == 0 else f"noise_{dimension}d.glsl"
            source = "#version 450 core\n" + shader_source(NOISE / include) + extra + """
layout(local_size_x=64) in;
layout(std430,binding=0) readonly buffer Inputs { vec4 points[]; };
layout(std430,binding=1) writeonly buffer Outputs { vec4 results[]; };
uniform uint probeSeed;
uniform uint probeCount;
void main()
{
    uint i=gl_GlobalInvocationID.x;
    if(i>=probeCount) return;
    vec4 p=points[i];
    results[i]=EXPRESSION;
}
""".replace("EXPRESSION", expression)
            self.programs[key] = self.gl.compile([(0x91B9, source)], expression)
        gl, program = self.gl, self.programs[key]
        rows = [tuple(point) + (0.,) * (4-len(point)) for point in points]
        raw = struct.pack(f"<{4*len(rows)}f", *(x for row in rows for x in row))
        gl.resize(self.inputs, raw)
        gl.resize(self.output, len(raw))
        gl.bind(self.inputs, 0)
        gl.bind(self.output, 1)
        gl.uniform(program, "probeSeed", "1ui", seed)
        gl.uniform(program, "probeCount", "1ui", len(rows))
        gl.dispatch_extent(program, len(rows))
        result = gl.read(self.output, len(raw))
        return result, list(struct.iter_unpack("<4f", result))


def hash32(value):
    value &= 0xffffffff
    value ^= value >> 16
    value = (value * 0x7feb352d) & 0xffffffff
    value ^= value >> 15
    value = (value * 0x846ca68b) & 0xffffffff
    return value ^ (value >> 16)


def test_primitives(probe):
    rng = random.Random(7309)
    total = 0
    for dimension in (2, 3):
        points = [tuple(rng.uniform(-64, 64) for _ in range(dimension)) for _ in range(512)]
        points += list(itertools.product((-1., 0., 1.), repeat=dimension))
        p = "p.xy" if dimension == 2 else "p.xyz"
        for family in ("White", "Value", "ValueCubic", "Perlin", "Simplex", "Voronoi", "CellValue"):
            name = f"noise{family}{dimension}D"
            expression = f"vec4({name}({p},probeSeed),{name}01({p},probeSeed),0,0)"
            for seed in (0, 1337, 0xffffffff):
                raw, rows = probe.sample(expression, points, seed, dimension)
                assert all(math.isfinite(v) for row in rows for v in row), name
                assert all(-1.000001 <= row[0] <= 1.000001 and -1e-7 <= row[1] <= 1.000001 for row in rows), name
                assert all(abs(row[1] - (0.5 + 0.5*row[0])) < 2e-7 for row in rows), (name, "signed/01 mapping")
                if seed == 1337:
                    assert raw == probe.sample(expression, points, seed, dimension)[0], (name, "determinism")
                    assert raw != probe.sample(expression, points, seed+1, dimension)[0], (name, "seed ignored")
                total += len(points)
        # Dyadic coordinates make period translations exactly representable.
        points = [tuple(rng.randrange(-1024, 1024)/16. for _ in range(dimension)) for _ in range(128)]
        periods = (7, 11, 5)[:dimension]
        period = f"ivec{dimension}(" + ",".join(map(str, periods)) + ")"
        function = f"noisePerlinPeriodic{dimension}D"
        expression = f"vec4({function}({p},probeSeed,{period}))"
        expected = probe.sample(expression, points, dimension=dimension)[0]
        for axis, distance in enumerate(periods):
            shifted = [tuple(x + (distance if j == axis else 0) for j,x in enumerate(point)) for point in points]
            assert expected == probe.sample(expression, shifted, dimension=dimension)[0], (function, axis)
        # Continuity across the rank-order boundaries of simplex cells.
        nearby = []
        for t in (-12.125, -2.75, -.125, 0., .125, 2.75, 12.125):
            for delta in (-1e-5, 1e-5):
                nearby.append((t+delta,t-delta) + ((t*.37,) if dimension == 3 else ()))
        _, rows = probe.sample(f"vec4(noiseSimplex{dimension}D({p},probeSeed))", nearby, dimension=dimension)
        assert all(abs(rows[i][0]-rows[i+1][0]) < .002 for i in range(0,len(rows),2)), (dimension,"simplex boundary")
    print(f"PASS noise bases: {total} signed/01 samples, seven families in 2D/3D, seeds, periodicity and simplex boundaries", flush=True)


def test_cellular(probe):
    rng = random.Random(112)
    count = 0
    for dimension in (2, 3):
        raw = struct.pack(f"<{dimension*64}f", *(rng.uniform(-8,8) for _ in range(dimension*64)))
        values = struct.unpack(f"<{dimension*64}f", raw)
        points = [values[i:i+dimension] for i in range(0,len(values),dimension)]
        p = "p.xy" if dimension == 2 else "p.xyz"
        expression = f"vec4(noiseCellularDistancesSquared{dimension}D({p},probeSeed),0,0)"
        _, distances = probe.sample(expression, points, dimension=dimension)
        metric_rows = [probe.sample(f"vec4(noiseCellularData{dimension}D({p},probeSeed,{metric}),0,0)",
                                   points, dimension=dimension)[1] for metric in range(3)]
        for i, point in enumerate(points):
            cell = tuple(math.floor(x) for x in point)
            candidates = []
            for offset in itertools.product(range(-3,4), repeat=dimension):
                coords = tuple(a+b for a,b in zip(cell,offset))
                h = hash32((coords[0]*0x9e3779b9) ^ (coords[1]*0x85ebca6b) ^
                           ((coords[2]*0xc2b2ae35) if dimension == 3 else 0) ^ 1337)
                h2 = hash32(h ^ 0x68bc21eb)
                bits = (h & 65535, h >> 16, h2 & 65535)[:dimension]
                delta = tuple(abs(c+.35+.30*b/65535.-x) for c,b,x in zip(coords,bits,point))
                site = (h2 >> 8)/16777215.
                candidates.append((sum(v*v for v in delta),sum(delta),max(delta),site))
            ordered = sorted(candidates)
            assert abs(distances[i][0]-ordered[0][0]) < 3e-6 and abs(distances[i][1]-ordered[1][0]) < 3e-6, (dimension,point,"F1/F2")
            for metric in range(3):
                best = min(candidates,key=lambda row:row[metric])
                target = math.sqrt(best[0]) if metric == 0 else best[metric]
                actual, site = metric_rows[metric][i][:2]
                assert abs(actual-target) < 3e-6 and abs(site-best[3]) < 2e-7, (dimension,point,metric,actual,target)
            count += 1
    print(f"PASS cellular: {count} points, F1/F2 and three metrics against independent 7^dimension site searches", flush=True)


def test_shaping(probe):
    points = [(x/64.,) for x in range(-16,81)]
    curves = ("noiseTriangle01(p.x)", "noiseGamma01(p.x,2.0)", "noiseConcave01(p.x)",
              "noiseConvex01(p.x)", "noiseBias01(p.x,0.2)", "noiseGain01(p.x,0.8)",
              "noiseContrast01(p.x,2.0)", "noiseContrast01(p.x,2.0,0.25)",
              "noiseSineEase01(p.x)", "noiseSmoothstep(0.0,1.0,p.x)")
    for curve in curves:
        _, rows = probe.sample(f"vec4({curve})",points,dimension=0)
        assert all(math.isfinite(row[0]) and 0. <= row[0] <= 1. for row in rows), curve
    for levels in (-1,1,2,4,17,100000000):
        for softness in (0.,1e-12,.5,1.):
            expression = f"vec4(noiseQuantize01(p.x,{levels}),noiseTerraces01(p.x,{levels},{softness:.12f}),0,0)"
            _,rows=probe.sample(expression,points,dimension=0)
            assert all(0. <= v <= 1. for row in rows for v in row[:2]), expression
            assert rows[16][:2] == (0.,0.) and rows[80][:2] == (1.,1.), expression
            assert all(rows[i][j]<=rows[i+1][j] for i in range(len(rows)-1) for j in (0,1)), expression
    expression = "vec4(noiseRemap(p.x,1,1,3,4),noiseSmoothstep(.5,.5,p.x),noiseMound01(p.x,0),noiseMesa01(p.x,1,1))"
    _,rows=probe.sample(expression,points,dimension=0)
    for (x,),row in zip(points,rows):
        assert row == (3.,float(x>=.5),0.,float(x<1.)), (x,row)
    _,rows=probe.sample("vec4(noiseTerrace(p.x,0,.5),noiseSmoothMin(p.x,0,0),noiseSmoothMax(p.x,0,0),noiseRemapClamped(p.x,1,0,2,4))",points,dimension=0)
    for (x,),row in zip(points,rows):
        assert row[:3] == (x,min(x,0.),max(x,0.)), (x,row)
        assert 2. <= row[3] <= 4.
    _,rows=probe.sample("vec4(noiseTo01(noiseToSigned(p.x)),noiseBias01(p.x,.5),noiseGain01(p.x,.5),noiseSnap(p.x,0))",points,dimension=0)
    for (x,),row in zip(points,rows):
        bounded=max(0.,min(1.,x))
        assert max(abs(row[0]-x),abs(row[1]-bounded),abs(row[2]-bounded),abs(row[3]-x)) < 1e-7
    print("PASS shaping: ranges, endpoints, monotonic quantization, zero widths, inverted/degenerate intervals and identity controls",flush=True)


def test_composites(probe):
    rng = random.Random(823)
    evaluated = 0
    for dimension in (2,3):
        p = "p.xy" if dimension == 2 else "p.xyz"
        coords = [tuple(rng.randrange(-256,256)/16. for _ in range(3)) for _ in range(48)]
        points = [point+(float(count),) for count in (0,1,5,8,99) for point in coords]
        for family in ("Fbm","ValueFbm","SimplexFbm","Turbulence","Ridged","Billow"):
            name=f"noise{family}{dimension}D"
            base="Value" if family=="ValueFbm" else "Simplex" if family=="SimplexFbm" else "Perlin"
            expression=f"vec4({name}({p},probeSeed,int(p.w),2.1,.55),{name}01({p},probeSeed,int(p.w),2.1,.55),noise{base}{dimension}D({p},probeSeed),0)"
            _,rows=probe.sample(expression,points,dimension=dimension)
            for index,row in enumerate(rows):
                assert all(math.isfinite(v) for v in row),name
                assert -1.000001<=row[0]<=1.000001 and -1e-7<=row[1]<=1.000001,name
                assert abs(row[1]-(.5+.5*row[0]))<2e-7,(name,"mapping")
                if index<len(coords):assert row[:2]==(0.,.5),(name,"zero octaves")
                elif index<2*len(coords):
                    target=row[2]
                    if family in ("Turbulence","Billow"):target=2.*abs(target)-1.
                    elif family=="Ridged":target=2.*(1.-abs(target))**2-1.
                    assert abs(row[0]-target)<3e-7,(name,"single octave",row,target)
            assert rows[3*len(coords):4*len(coords)]==rows[4*len(coords):],(name,"octave bound")
            evaluated+=len(rows)
        expression=(f"vec4(noiseFbm{dimension}D({p},probeSeed,8,2.,0.),noisePerlin{dimension}D({p},probeSeed),"
                    f"noiseFbm{dimension}D({p},probeSeed,4,0.,.5),noiseFbm{dimension}D({p},probeSeed,4,1.,.5))")
        _,rows=probe.sample(expression,coords,dimension=dimension)
        assert all(abs(row[0]-row[1])<1e-7 and row[2]==row[3] for row in rows),(dimension,"gain/lacunarity bounds")
        # Degenerate warp parameters are exact identities; amplitude is a
        # per-component bound, not a bound on the length of the vector.
        vector = "vec4(noiseDomainWarp2D(p.xy,probeSeed,.75,1.3),0,0)" if dimension==2 else "vec4(noiseDomainWarp3D(p.xyz,probeSeed,.75,1.3),0)"
        _,rows=probe.sample(vector,coords,dimension=dimension)
        assert all(abs(row[j]-point[j])<=.750002 for row,point in zip(rows,coords) for j in range(dimension))
        for amplitude,frequency in ((0.,1.3),(.75,0.)):
            vector=(f"vec4(noiseDomainWarp2D(p.xy,probeSeed,{amplitude},{frequency}),0,0)" if dimension==2 else
                    f"vec4(noiseDomainWarp3D(p.xyz,probeSeed,{amplitude},{frequency}),0)")
            _,rows=probe.sample(vector,coords,dimension=dimension)
            assert all(row[:dimension]==point[:dimension] for row,point in zip(rows,coords))
        for family,parameters in (("WarpedPerlin",".75,1.3"),("Marble",".7,.3"),("Wood",".7,.3"),("Cracks",".15"),("Ridge",None)):
            arguments=f"{p},probeSeed"+(f",{parameters}" if parameters else "")
            name=f"noise{family}{dimension}D"
            _,rows=probe.sample(f"vec4({name}({arguments}),{name}01({arguments}),0,0)",coords,dimension=dimension)
            assert all(-1.000001<=row[0]<=1.000001 and -1e-7<=row[1]<=1.000001 and abs(row[1]-(.5+.5*row[0]))<2e-7 for row in rows),name
        vector=("vec4(noiseVector2D(p.xy,probeSeed),0,0)" if dimension==2 else "vec4(noiseVector3D(p.xyz,probeSeed),0)")
        _,rows=probe.sample(vector,coords,dimension=dimension)
        assert all(math.isfinite(v) and -1.000001<=v<=1.000001 for row in rows for v in row)
        # Curl of a potential: test divergence using the same central stencil.
        # Dyadic coordinates/epsilon keep all shifted sample positions exact.
        epsilon=1./64
        neighbors=[]
        for point in coords[:12]:
            for axis in range(dimension):
                for sign in (-1,1):
                    neighbors.append(tuple(value+(sign*epsilon if i==axis else 0.) for i,value in enumerate(point)))
        vector=(f"vec4(noiseCurl2D(p.xy,probeSeed,{epsilon}),0,0)" if dimension==2 else
                f"vec4(noiseCurl3D(p.xyz,probeSeed,{epsilon}),0)")
        _,rows=probe.sample(vector,neighbors,dimension=dimension)
        assert all(math.isfinite(v) for row in rows for v in row),(dimension,"curl finite")
        for offset in range(0,len(rows),2*dimension):
            divergence=sum((rows[offset+2*axis+1][axis]-rows[offset+2*axis][axis])/(2*epsilon) for axis in range(dimension))
            assert abs(divergence)<.002,(dimension,"curl divergence",divergence)
    # Historical spatial composites retain raw units and disabled support.
    points=[(i/8.,i/11.,0.,0.) for i in range(96)]
    for expression in ("vec4(noiseDunes2D01(p.xy,vec2(.8,.6),3.,probeSeed))",
                       "vec4(noiseLandmark2D01(p.xy,3.,probeSeed,NOISE_EUCLIDEAN,.30,.5,.75))",
                       "vec4(noiseFbmTerrain2D01(p.xy,probeSeed,112u))"):
        _,rows=probe.sample(expression,points)
        assert all(0.<=row[0]<=1.000001 for row in rows),expression
    _,rows=probe.sample("vec4(noiseDunes2D01(p.xy,vec2(1,0),0.,probeSeed),noiseLandmark2D01(p.xy,3.,probeSeed,0,.3,.5,0.),noiseCracks2D01(p.xy,probeSeed,0.),0)",points)
    assert all(row==(0.,0.,0.,0.) for row in rows)
    print(f"PASS compositions: {evaluated} fractal samples, octave identities/bounds, warp, patterns and divergence-free curl stencils",flush=True)


def test_layout_and_joint_include(probe):
    for filename in ("noise_2d.glsl","noise_3d.glsl"):
        source=(NOISE/filename).read_text()
        code=re.sub(r"//[^\n]*|/\*.*?\*/","",source,flags=re.S)
        assert not re.search(r"\b(uniform|uSeed)\b",code),filename
        # The helper section precedes the artistic entry points.
        internal=[match.start() for match in re.finditer(r"^(?:uint|float|[iu]?vec[234])\s+noiseInternal\w+\s*\(",code,re.M)]
        public=[match.start() for match in re.finditer(r"^(?:uint|float|[iu]?vec[234])\s+noise(?!Internal)\w+\s*\(",code,re.M)]
        assert not internal or max(internal)<min(public),(filename,"helper placement")
    probe.sample("vec4(noisePerlin2D(p.xy,probeSeed),noisePerlin3D(p.xyz,probeSeed),noiseTo01(0.),1.)",[(.125,.25,.5)],
                 extra=shader_source(NOISE/"noise_3d.glsl"))
    print("PASS library organization: independent dimensions, helper sections, no implicit uniforms, joint inclusion",flush=True)


def test_ice_reference(gl, reference):
    from biome_test_support import biome_recipe_source
    from field_batch_runner import BatchFieldRunner
    reference = Path(reference)
    rng=random.Random(812)
    points=[(rng.uniform(-8192,8192),rng.uniform(-8192,8192)) for _ in range(512)]
    points += [(0.,0.),(-0.,-0.),(64.,-64.),(-1024.,1024.)]
    raw=struct.pack(f"<{len(points)*2}f",*(x for p in points for x in p))
    inputs=gl.buffer(raw,0);output=gl.buffer(len(points)*16,1)
    body="""
layout(local_size_x=64) in;
layout(std430,binding=0) readonly buffer InputPoints { vec2 points[]; };
layout(std430,binding=1) writeonly buffer OutputRelief { vec4 relief[]; };
uniform uint probeCount;
void main() { uint i=gl_GlobalInvocationID.x; if(i<probeCount) relief[i]=fieldRelief(points[i]); }
"""
    for biome in ("PolarIce","FrozenTundra"):
        programs=[gl.compile([(0x91B9,biome_recipe_source(directory/"biomes"/biome/"relief.C.glsl")+body)],biome+" raw relief")
                  for directory in (reference,TERRAIN)]
        for seed in (0,1337,0xffffffff):
            samples=[]
            for program in programs:
                gl.bind(inputs,0);gl.bind(output,1)
                gl.uniform(program,"probeCount","1ui",len(points))
                gl.uniform(program,"uSeed","1ui",seed)
                gl.dispatch_extent(program,len(points))
                samples.append(gl.read(output,len(points)*16))
            assert samples[0]==samples[1],(biome,seed,"raw relief changed")
    old,new=BatchFieldRunner(gl,reference),BatchFieldRunner(gl,TERRAIN)
    fields=0
    for biome in (0,1):
        for seed in (0,1337,98765):
            for origin,lod in (((-16,32,-16),0),((-512,0,256),2),((1024,128,-2048),4)):
                options=dict(cells=8,origin=origin,lod=lod,seed=seed,uBiomeOverride=biome,uBandOffsetVariation=32.)
                before,after=old.run(**options),new.run(**options)
                assert before["density_raw"]==after["density_raw"],(biome,seed,origin,lod,"density")
                # Keep every packed bit except the cell's four H/T bits, which
                # are computed before the forced-biome override. Its two
                # altitude bits remain checked. New climate
                # weights intentionally change automatic blended terrain.
                for a,b in zip(struct.iter_unpack("<8I",before["columns_packed_raw"]),
                               struct.iter_unpack("<8I",after["columns_packed_raw"])):
                    assert a[:7]==b[:7] and (a[7]&~(15<<24))==(b[7]&~(15<<24)),(biome,seed,origin,lod,"packed controls")
                for offset in range(0,len(before["columns_raw"]),48):
                    assert before["columns_raw"][offset:offset+32]==after["columns_raw"][offset:offset+32],(biome,seed,origin,lod,"decoded controls")
                fields+=1
    print(f"PASS preserved ice: 3096 raw reliefs and {fields} forced fields with bitwise identical controls/densities (four H/T cell bits excluded)",flush=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference-shaders",type=Path)
    args=parser.parse_args()
    with GL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        probe=Probe(gl)
        test_primitives(probe)
        test_cellular(probe)
        test_shaping(probe)
        test_composites(probe)
        test_layout_and_joint_include(probe)
        if args.reference_shaders:test_ice_reference(gl,args.reference_shaders)
        gl.check("standalone noise library")


if __name__=="__main__":
    main()
