#!/usr/bin/env python3
"""Check physical invariants against closed-form scalar reference solutions."""
import math
import struct

from fixtures import Parameters
from gl_support import AtmosphereGL, ROOT


def check_common(gl):
    parameters=Parameters()
    parameters.upload(gl)
    program=gl.compute(ROOT/"Tests/Atmosphere/common_probe.C.glsl")
    output=gl.buffer(struct.pack("<f",float("nan"))*(32*9*4),0)
    gl.dispatch(program,1)
    values=struct.unpack("<1152f",gl.read(output,32*9*16))
    assert all(math.isfinite(value) for value in values),"Unwritten or non-finite common-function output"
    rows=[values[i*36:(i+1)*36] for i in range(32)]
    previous_t=(1,1,1)
    previous_r=previous_m=(1,1,1)
    for i,row in enumerate(rows):
        distance=i*13.5
        for channel,(source,extinction) in enumerate(zip((.07,.02,.11),(.02,.04,.08))):
            expected_t=math.exp(-extinction*distance)
            expected_s=source*(-math.expm1(-extinction*distance))/extinction
            assert abs(row[channel]-expected_s)<5e-5,("homogeneous in-scattering",i,channel,row[channel],expected_s)
            assert abs(row[4+channel]-expected_t)<3e-6,("Beer-Lambert transmittance",i,channel)
            assert 0<=row[4+channel]<=previous_t[channel]+1e-7
            assert abs(row[8+channel]-distance)<1e-5,("vacuum integral limit",i,channel)
            assert row[12+channel]==0,("artistic correction emitted light in darkness",i)
            assert 0<=row[20+channel]<=previous_r[channel]
            assert 0<=row[24+channel]<=previous_m[channel]
            assert row[28+channel]>=row[20+channel]+row[24+channel]-1e-10
        previous_t=row[4:7]
        previous_r=row[20:23]
        previous_m=row[24:27]
        assert abs(row[19]-1)<2e-6,("sky direction mapping round trip",i,row[19])
        assert row[23]>0 and row[27]>0,"Phase function became negative"
        assert row[32]==1 and abs(row[33]-.5)<1e-3 and row[34]==0 and row[35]==0,("Disk center/rim/outside/disabled mask",i,row[32:36])
    assert rows[-1][0]>1,"Reference HDR radiance unexpectedly clamped"
    gl.check("common atmosphere physical invariants")
    print("PASS common physical functions: analytic Beer-Lambert/in-scattering, vacuum limit, monotonic extinction, HDR, zero-light artistic correction, sky mapping, tiny solar disks",flush=True)


def main():
    with AtmosphereGL() as gl:
        print(f"OpenGL {gl.version}; renderer: {gl.renderer}",flush=True)
        check_common(gl)


if __name__=="__main__":
    main()
