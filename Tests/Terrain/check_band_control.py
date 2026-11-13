#!/usr/bin/env python3
"""A biome's 2D thickness alone must own the complete 3D band."""
import math
from pathlib import Path
import struct
import tempfile

from biome_mask_probe import replace_body
from check_gpu import FieldRunner, SHADERS, COLUMN_WORDS
from gl_headless import GL, copy_terrain_shaders


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def verify_zero_columns(field):
    side=field['side']
    tested=0
    for z in range(side):
        for x in range(side):
            offset=(x+z*side)*COLUMN_WORDS
            height,band=field['columns'][offset],field['columns'][offset+3]
            if band!=0.0:
                continue
            for y in range(side):
                world_y=field['origin'][1]+(y-1)*(1<<field['lod'])
                value=field['density'][x+y*side+z*side*side]
                assert value==f32(height-world_y),('Zero biome band still sculpts terrain',x,y,z,value,height)
                tested+=1
    return tested


def main():
    with tempfile.TemporaryDirectory(prefix='terrain-biome-band-') as temporary, GL() as gl:
        directory=Path(temporary)/'shaders'
        copy_terrain_shaders(SHADERS,directory)
        path=directory/'biomes/PolarIce/relief.C.glsl'
        source=path.read_text()
        version,body=source.split('\n',1)
        assert version.startswith('#version'), 'Biome entry must declare its GLSL version first'
        source=version+'\nuniform float probeThickness;\nuniform float probeHeight;\nuniform int probeSpatialBand;\n'+body
        source=replace_body(source,'fieldRelief', '''
    float width=probeSpatialBand==0 ? probeThickness
        : max(0.0,noisePerlin2D(p*0.05, uSeed ^ (715u))-0.1)*128.0;
    return vec4(probeHeight,width,-10.0,0.5);
''')
        path.write_text(source)
        # Exercise the pipeline contract independently of where the production
        # recipe places rare sites. A deliberately nonzero delta must still be
        # suppressed for a zero band and outside the prepared envelope.
        volume_path=directory/'biomes/PolarIce/volume.C.glsl'
        volume_path.write_text(replace_body(volume_path.read_text(),'fieldVolume',
                                           'return 1.0;'))
        runner=FieldRunner(gl,directory)
        polar_program=runner.relief_programs[0]
        for obsolete in ('uBandThickness','uBandVariation'):
            assert gl.glGetUniformLocation(polar_program,obsolete.encode())==-1,obsolete
        gl.uniform(polar_program,'probeSpatialBand','1i',0)
        gl.uniform(polar_program,'probeHeight','1f',8.)
        options=dict(cells=9,origin=(-16,-24,-16),seed=1337,
                     uBiomeOverride=0,uHeightAmplitude=0.,uBaseHeight=3.)
        disabled_samples=0
        for scale in (0.,.35,1.,2.):
            for raw in (-12.25,0.,12.25,35.,576.,800.):
                gl.uniform(polar_program,'probeThickness','1f',raw)
                field=runner.run(**options,uDetailScale=scale)
                width=min(max(raw,0.),576.)
                code=max(1,math.floor(f32(f32(width/576.)*65535.)+.5)) if width>0 else 0
                expected=f32(f32(code*f32(576./65535.))*f32(scale)) if f32(width*f32(scale))>.0002 else 0.
                assert all(math.isclose(v,expected,rel_tol=2e-7,abs_tol=1e-6)
                           for v in field['columns'][3::COLUMN_WORDS]),('Thickness has hidden addition',raw,scale)
                disabled_samples+=verify_zero_columns(field)

        # Author-controlled spatial noise can turn 3D off in entire columns;
        # no independent global variation may reopen them.
        gl.uniform(polar_program,'probeSpatialBand','1i',1)
        field=runner.run(**dict(options,cells=32,origin=(-16,-24,-16)),uDetailScale=.35)
        bands=field['columns'][3::COLUMN_WORDS]
        assert any(v==0 for v in bands) and any(v>0 for v in bands)
        assert max(bands)<=128.*.35
        assert all(math.isfinite(v) for v in field['density'])
        disabled_samples+=verify_zero_columns(field)
        assert any(abs(value-f32(field['columns'][(x+z*field['side'])*COLUMN_WORDS]
                     -(field['origin'][1]+y-1)))>1e-5
                   for z in range(field['side']) for y in range(field['side'])
                   for x in range(field['side'])
                   for value in [field['density'][x+y*field['side']+z*field['side']**2]])
        gl.check('biome-owned volume thickness')
        print(f'PASS biome-owned thickness: 24 scale/width cases, negative and zero widths, '
              f'upper clamp, locally noise-driven bands; {disabled_samples} unsculpted samples verified')

        # Exercise both recipe and final accumulation clamps with an independent
        # fixed oracle. Peaks/rifts of +/-800 world metres must survive .35 scale.
        gl.uniform(polar_program,'probeSpatialBand','1i',0)
        gl.uniform(polar_program,'probeThickness','1f',0.)
        for scale in (.35,1.):
            for raw in (-5000.,-4096.,-800./.35,-1000.,0.,1000.,800./.35,4096.,5000.):
                gl.uniform(polar_program,'probeHeight','1f',raw)
                field=runner.run(**options,uDetailScale=scale)
                expected=f32(3.+f32(f32(min(max(raw,-4096.),4096.))*f32(scale)))
                assert all(math.isclose(v,expected,rel_tol=2e-7,abs_tol=1e-5)
                           for v in field['columns'][0::COLUMN_WORDS]),('Raw height clamp/scale mismatch',raw,scale)
                verify_zero_columns(field)
        print('PASS relief height: 18 signed height/scale cases, +/-800m retained, +/-4096 raw clamp, packed bands unchanged')


if __name__=='__main__':
    main()
