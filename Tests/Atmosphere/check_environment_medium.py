#!/usr/bin/env python3
"""Exercise the real world-fog hook with the environment's two physical layers.

Known coefficients and constant atlases provide an independent oracle. This
does not rewrite shader math or substitute a fullscreen colour/alpha effect.
"""
from __future__ import annotations

import math
import struct

from gl_support import AtmosphereGL, ROOT, RGBA32F, shader_source


def close(actual, expected, label, tolerance=2e-7):
    assert all(math.isfinite(v) for v in actual), (label, actual)
    assert max(abs(a-b) for a,b in zip(actual,expected)) < tolerance, (label,actual,expected)


def run():
    with AtmosphereGL() as gl:
        print('OpenGL:', gl.version, '/', gl.renderer, flush=True)
        source = '#version 450 core\n' + shader_source(
            ROOT/'ENDURANCE/Ressources/Shader/Atmosphere/game_fog.glsl') + '''
layout(local_size_x=1) in;
layout(std430,binding=0) buffer Result { vec4 value; };
uniform vec3 probePosition;
void main() { value=geGameFog(probePosition); }
'''
        program = gl.compile([(0x91B9, source)], 'actual environment participating medium')
        output = gl.buffer(16, 0)
        medium = gl.texture((1,1), internal=RGBA32F, value=(.0008,.0009,.001,.001))
        shape = gl.texture((1,1), internal=RGBA32F, value=(50.,100.,0.,.005))
        gl.sampler(6,medium); gl.sampler(7,shape)
        gl.uniform(program,'uGameFogOriginSize','4f',-1000.,-1000.,2000.,1.)
        gl.uniform(program,'uGameFogSeed','1ui',1337)

        def layer(index, extinction=0., albedo=(.8,.9,1.), base=0., height=100., noise=0., scale=.005,
                  wind=(0.,0.), sea=20.):
            gl.uniform(program,f'uEnvironmentMedium[{index}]','4f',
                       *(v*extinction for v in albedo),extinction)
            gl.uniform(program,f'uEnvironmentShape[{index}]','4f',base,height,noise,scale)
            gl.uniform(program,f'uEnvironmentWind[{index}]','4f',*wind,sea,0.)

        def sample(position, valid=False):
            gl.uniform(program,'uGameFogAtlasValid','1i',int(valid))
            gl.uniform(program,'probePosition','3f',*position)
            gl.dispatch(program,1)
            return struct.unpack('<4f',gl.read(output,16))

        layer(0); layer(1)
        assert sample((0,20,0))==(0.,0.,0.,0.)
        close(sample((0,50,0),True),(.0008,.0009,.001,.001),'Neutral override retains atlas')
        close(sample((0,150,0),True),tuple(v/math.e for v in (.0008,.0009,.001,.001)),
              'Neutral override retains world height')
        assert sample((40000,50,-40000),True)==(0.,0.,0.,0.)
        print('PASS neutral environmental state preserves the previous atlas and absent medium',flush=True)

        # Suspended weather is physical world media even while climate is being
        # rebuilt, outside the atlas, or in a biome whose atlas contains no fog.
        layer(1,.01,albedo=(.5,.6,.7),base=10.,height=100.)
        close(sample((0,30,0)),(.005,.006,.007,.01),'Weather with unavailable atlas')
        close(sample((40000,30,-40000),True),(.005,.006,.007,.01),'Weather outside atlas')
        close(sample((0,130,0)),tuple(v/math.e for v in (.005,.006,.007,.01)),
              'Weather altitude relative to scene sea level')
        close(sample((0,50,0),True),tuple(a+b*math.exp(-.2) for a,b in
              zip((.0008,.0009,.001,.001),(.005,.006,.007,.01))), 'Weather sums physical coefficients')
        # Camera position is not a fog input. Advecting noise must never move
        # this world-height profile; a changed camera cannot lift the layer.
        layer(1,.01,albedo=(.5,.6,.7),base=10.,height=100.,wind=(800.,-600.))
        close(sample((0,130,0)),tuple(v/math.e for v in (.005,.006,.007,.01)),
              'Wind cannot shift the height profile')
        print('PASS suspended weather: invalid/outside atlas, world altitude, additive coefficients and camera-independent height',flush=True)

        layer(1)
        layer(0,.002)
        close(sample((0,20,0)),(.0016,.0018,.002,.002),'Ground fallback uses scene sea level')
        close(sample((0,50,0),True),(.0016,.0018,.002,.002),'Ground uses atlas baseline without double density')
        close(sample((0,150,0),True),tuple(v/math.e for v in (.0016,.0018,.002,.002)),
              'Ground override and atlas share height origin')
        layer(0,.0005)
        close(sample((0,50,0),True),(.0008,.0009,.001,.001),'Weaker ground override does not reduce or double base fog')
        layer(0)
        close(sample((0,50,0),True),(.0008,.0009,.001,.001),'Clearing ground restores base')
        print('PASS ground density floor: atlas/fallback heights, no duplicate base fog and exact reset',flush=True)

        layer(1,.01,noise=.8,scale=.01)
        position=(125.,70.,-175.)
        before=sample(position)
        assert before==sample(position),'Paused world medium is unstable'
        offset=(240.,-80.)
        layer(1,.01,noise=.8,scale=.01,wind=offset)
        animated=sample(position)
        assert abs(animated[3]-before[3])>1e-7,'Structured weather does not advect'
        advected=sample((position[0]+offset[0],position[1],position[2]+offset[1]))
        close(advected,before,'Noise features follow integrated particle wind',tolerance=2e-6)
        for x in range(-5,6):
            value=sample((x*173.,60.+x*10.,x*-97.))
            assert all(0<=v<=value[3]+1e-8 for v in value[:3]),value
        layer(0);layer(1)
        assert sample(position)==(0.,0.,0.,0.),'Stopped weather retains density'
        gl.check('environment medium shader tests')
        print('PASS structured weather: deterministic pause, real wind advection, bounded scattering and exact-zero stop',flush=True)


if __name__=='__main__':
    run()
