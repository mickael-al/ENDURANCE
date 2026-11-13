"""GPU diagnostics for the actual (possibly warped) compact placement domains."""
from __future__ import annotations
from dataclasses import dataclass
import re
import numpy as np
from biome_test_support import BIOME_NAMES, biome_recipe_source
from check_biomes import BiomeProbe, profile_probe_source
from check_gpu import SHADERS


@dataclass(frozen=True)
class Placement:
    variable: str
    function: str
    arguments: tuple[str, ...]

    @property
    def spacing(self):
        value=float(self.arguments[0])
        return 1000./value if self.function=='landRkmSite' else value

    @property
    def salt(self):
        return int(self.arguments[2 if self.function=='landSparseSite' else 1].rstrip('uU'))

    @property
    def occupancy(self):
        return 1. if self.function=='landRkmSite' else float(self.arguments[-1])

    @property
    def sparse(self):return self.function=='landSparseSite'

    def call(self,p='p'):
        return self.function+'('+p+','+','.join(self.arguments)+')'


PLACEMENT_RE=re.compile(r'LandSite\s+(\w+)\s*=\s*(land(?:Sparse|Rkm)?Site)\(\s*p(?:\.xz)?\s*,\s*([^;]+)\);')


def placements(biome,directory=SHADERS,kind='relief'):
    source=(directory/'biomes'/BIOME_NAMES[biome]/(kind+'.C.glsl')).read_text()
    return [Placement(name,function,tuple(x.strip() for x in arguments.split(',')))
            for name,function,arguments in PLACEMENT_RE.findall(source)]


def placement_for(biome,salt,directory=SHADERS):
    return next(p for p in placements(biome,directory) if p.salt==salt)


# Test-only inverse: production samples the forward warp exactly once. Newton
# inversion allows us to target the true world anchor, including negative cells.
INVERSE_SPARSE=r'''
vec2 probeWorldFromDomain(vec2 target,float spacing,uint salt) {
    vec2 d=landSparseDirection(salt);
    vec2 p=target.x*d+target.y*vec2(-d.y,d.x);
    float e=max(0.5,spacing*0.0002);
    for(int i=0;i<12;++i) {
        vec2 error=landSparseDomain(p,spacing,salt)-target;
        vec2 a=(landSparseDomain(p+vec2(e,0),spacing,salt)-landSparseDomain(p-vec2(e,0),spacing,salt))/(2.0*e);
        vec2 b=(landSparseDomain(p+vec2(0,e),spacing,salt)-landSparseDomain(p-vec2(0,e),spacing,salt))/(2.0*e);
        p-=inverse(mat2(a,b))*error;
    }
    return p;
}
'''

ANCHOR_SOURCE=r'''
layout(local_size_x=8,local_size_y=8) in;
struct ProbeSample { vec4 terrain; vec4 volume; vec4 samples; vec4 shape; };
layout(std430,binding=6) writeonly buffer ProbeOutput { ProbeSample outputValues[]; };
uniform uint uProbeSize;
uniform vec2 uProbeOrigin;
uniform float uProbeSpacing;
void main() {
    uvec2 i=gl_GlobalInvocationID.xy;
    if(any(greaterThanEqual(i,uvec2(uProbeSize)))) return;
    vec2 domain=uProbeOrigin+vec2(i)*uProbeSpacing;
    vec2 p=INPUT_WORLD;
    LandSite s=SITE_CALL;
    vec2 anchor=ANCHOR_WORLD;
    p=anchor;
    LandSite centered=SITE_CALL;
    float e=max(0.5,uProbeSpacing*0.00002);
    p=anchor+vec2(e,0);vec2 a=SITE_CALL.q;
    p=anchor-vec2(e,0);a-=SITE_CALL.q;a/=2.0*e;
    p=anchor+vec2(0,e);vec2 b=SITE_CALL.q;
    p=anchor-vec2(0,e);b-=SITE_CALL.q;b/=2.0*e;
    mat2 j=inverse(mat2(a,b));
    outputValues[i.x+i.y*uProbeSize]=ProbeSample(vec4(anchor,s.radius,centered.mask),
        vec4(j[0].x,j[1].x,j[0].y,j[1].y),
        vec4(s.variant,length(centered.q),s.center),vec4(0));
}
'''


def anchor_probe(gl,biome,placement,seed=1337,size=10,directory=SHADERS,origin=None,min_active_fraction=.4):
    spacing=placement.spacing
    source=ANCHOR_SOURCE.replace('SITE_CALL',placement.call())
    inverse=lambda name:f'probeWorldFromDomain({name},{spacing},{placement.salt}u)'
    source=source.replace('INPUT_WORLD',inverse('domain') if placement.sparse else 'domain')
    source=source.replace('ANCHOR_WORLD',inverse('s.center') if placement.sparse else 's.center')
    if placement.sparse:source=INVERSE_SPARSE+source
    probe=BiomeProbe(gl,directory=directory,source=source)
    if origin is None:origin=((-size//2+.5)*spacing,)*2
    options=dict(biome=biome,size=size,origin=tuple(origin),spacing=spacing,uSeed=seed)
    first=probe.run(**options)
    assert first['raw']==probe.run(**options)['raw'],'Placement is not deterministic'
    assert first['raw']!=probe.run(**dict(options,uSeed=seed+1))['raw'],'Placement ignores seed'
    data=np.asarray(first['values']).reshape(-1,16)
    assert np.max(data[:,9])<max(.1,spacing*1.e-5),('Domain inversion residual',data[:,9].max())
    assert len(np.unique(data[:,:2],axis=0))==size*size,'Placement anchors collapse'
    expected=np.floor(np.stack(np.meshgrid(origin[0]+np.arange(size)*spacing,
        origin[1]+np.arange(size)*spacing),axis=-1).reshape(-1,2)/spacing).astype(int)
    assert np.array_equal(np.floor(data[:,10:12]/spacing).astype(int),expected),'Wrong placement cells'
    active=data[data[:,3]>.5]
    assert len(active)>=size*size*min_active_fraction,('Too few active candidates',len(active))
    return sorted(active,key=lambda row:float(np.dot(row[:2],row[:2]))),data


class OmitPlacementProbe(BiomeProbe):
    """Isolate a feature's contribution without changing its surrounding ground.

    Only its site mask is disabled in a diagnostic compilation. The production
    height/volume functions, seed, ground noises and all other sites are intact.
    """
    def __init__(self,gl,salt,anchor=None,**kwargs):
        super().__init__(gl,**kwargs)
        self.salt=salt
        self.anchor=anchor

    def program_for(self,biome):
        if biome not in self.programs:
            directory=self.directory/'biomes'/BIOME_NAMES[biome]
            source=[]
            count=0
            for name in ('relief.C.glsl','volume.C.glsl'):
                code=biome_recipe_source(directory/name)
                if source:code=re.sub(r'^#version[^\n]*\n','',code,count=1)
                def disable(match):
                    nonlocal count
                    variable,function,arguments=match.groups()
                    p=Placement(variable,function,tuple(x.strip() for x in arguments.split(',')))
                    if p.salt!=self.salt:return match.group(0)
                    count+=1
                    condition='' if self.anchor is None else f'if(length({variable}.center-vec2({float(self.anchor[0])},{float(self.anchor[1])}))<1.0)'
                    return match.group(0)+'\n'+condition+variable+'.mask=0.0;'
                code=PLACEMENT_RE.sub(disable,code)
                source.append(code)
            assert count>0,('Missing feature to isolate',biome,self.salt)
            self.programs[biome]=self.gl.compile([(0x91B9,''.join(source)+profile_probe_source()+self.main_source)],'isolated '+BIOME_NAMES[biome])
        return self.programs[biome]
