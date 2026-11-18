"""Explicit SI-unit fixtures, not copied runtime quality defaults."""
from __future__ import annotations

import math
import struct

FIELDS = """
Radii Rayleigh Mie Absorption Medium SunDirection SunRadiance SunDisk
MoonDirection MoonRadiance MoonDisk Horizon Middle Zenith Art Camera Volume Fog
FogNoise CloudLayer CloudShape CloudWind ShadowOrigin Shadow Quality Samples
Control Misc CloudWeather CloudProfiles
""".split()
IDENTITY = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.]


class Parameters:
    """Pack the published std140 ABI and offer isolated parameter perturbations."""
    def __init__(self):
        self.values = {name: [0., 0., 0., 0.] for name in FIELDS}
        self.matrices = {name: IDENTITY.copy() for name in ("InvView", "InvProjection", "View")}
        self.set("Radii", 6360000., 6460000., 0., 0.)
        self.set("Rayleigh", 5.802e-6, 13.558e-6, 33.1e-6, 8000.)
        self.set("Mie", 3.996e-6, 3.996e-6, 3.996e-6, 1200.)
        self.set("Absorption", 0.65e-6, 1.881e-6, 0.085e-6, 25000.)
        self.set("Medium", .444e-6, .8, 15000., 1.)
        self.set("SunDirection", 0., math.sqrt(.5), math.sqrt(.5), 1.)
        self.set("SunRadiance", 20., 19., 18., 0.)
        self.set("SunDisk", 100., 95., 90., .00465)
        self.set("Horizon", .8, .9, 1., .5)
        self.set("Middle", .25, .45, .8, .35)
        self.set("Zenith", .1, .2, .5, 1.)
        self.set("Art", 1., 1., 1., .2)
        self.set("Camera", 0., 100., 0., 0.)
        self.set("Volume", 20000., .5, 0., 0.)
        self.set("Fog", .8, .85, .9, 200.)
        self.set("FogNoise", 500., .6, 10., 5.)
        self.set("CloudLayer", 2000., 1500., .5, .004)
        self.set("CloudShape", 4000., 4., .25, .6)
        self.set("CloudWind", 30., 5., 20000., 0.)
        self.set("Shadow", -1, -1, 0, 0)
        self.set("Quality", 24, 40, 8, 12)
        self.set("Samples", 2, 4, 4, 12)
        self.set("Control", 1, 0, 0, 0)
        self.set("Misc", 1337, 0, 0, 0)
        self.set("CloudWeather", 48000., .65, 1.4, .7)
        self.set("CloudProfiles", .25, .5, .25, 0.)

    def set(self, name, *values):
        assert name in self.values and len(values) == 4
        self.values[name] = list(values)
        return self

    def bytes(self):
        data = bytearray()
        for name in FIELDS:
            scalar = "I" if name == "Misc" else "i" if name in ("Shadow", "Quality", "Samples", "Control") else "f"
            data.extend(struct.pack("<4" + scalar, *self.values[name]))
        for name in ("InvView", "InvProjection", "View"):
            data.extend(struct.pack("<16f", *self.matrices[name]))
        assert len(data) == 672
        return bytes(data)

    def upload(self, gl, buffer=None):
        if buffer is None:
            buffer = gl.buffer(self.bytes())
        else:
            gl.write(buffer, self.bytes())
        gl.glBindBufferBase(0x8A11, 6, buffer)  # GL_UNIFORM_BUFFER
        return buffer
