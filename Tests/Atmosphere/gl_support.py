"""Texture and fullscreen helpers for actual production atmosphere GPU tests.

The blocking readbacks here are validation instrumentation. The renderer does
not import this module and is not allowed to wait for these measurements.
"""
from __future__ import annotations

import ctypes as C
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tests/Terrain"))
from gl_headless import GL, GLUnavailable, shader_source

TEXTURE_2D = 0x0DE1
TEXTURE_3D = 0x806F
TEXTURE_2D_ARRAY = 0x8C1A
TEXTURE_CUBE_MAP = 0x8513
RGBA16F = 0x881A
RGBA32F = 0x8814
R32F = 0x822E
R16F = 0x822D
RG16F = 0x822F
RGBA = 0x1908
RG = 0x8227
RED = 0x1903
FLOAT = 0x1406


class AtmosphereGL(GL):
    def __init__(self):
        super().__init__()
        self.textures = []
        self.framebuffers = []
        self.arrays = []
        signatures = {
            "glGenTextures": (None, C.c_int, C.POINTER(C.c_uint)),
            "glDeleteTextures": (None, C.c_int, C.POINTER(C.c_uint)),
            "glBindTexture": (None, C.c_uint, C.c_uint),
            "glActiveTexture": (None, C.c_uint),
            "glTexParameteri": (None, C.c_uint, C.c_uint, C.c_int),
            "glTexImage2D": (None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int,
                             C.c_int, C.c_uint, C.c_uint, C.c_void_p),
            "glTexImage3D": (None, C.c_uint, C.c_int, C.c_int, C.c_int, C.c_int,
                             C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_void_p),
            "glGetTexImage": (None, C.c_uint, C.c_int, C.c_uint, C.c_uint, C.c_void_p),
            "glGenerateMipmap": (None, C.c_uint),
            "glBindImageTexture": (None, C.c_uint, C.c_uint, C.c_int, C.c_ubyte,
                                   C.c_int, C.c_uint, C.c_uint),
            "glUniformMatrix4fv": (None, C.c_int, C.c_int, C.c_ubyte, C.POINTER(C.c_float)),
            "glGenFramebuffers": (None, C.c_int, C.POINTER(C.c_uint)),
            "glBindFramebuffer": (None, C.c_uint, C.c_uint),
            "glDeleteFramebuffers": (None, C.c_int, C.POINTER(C.c_uint)),
            "glFramebufferTexture2D": (None, C.c_uint, C.c_uint, C.c_uint, C.c_uint, C.c_int),
            "glDrawBuffers": (None, C.c_int, C.POINTER(C.c_uint)),
            "glCheckFramebufferStatus": (C.c_uint, C.c_uint),
            "glGenVertexArrays": (None, C.c_int, C.POINTER(C.c_uint)),
            "glDeleteVertexArrays": (None, C.c_int, C.POINTER(C.c_uint)),
            "glBindVertexArray": (None, C.c_uint),
            "glDrawArrays": (None, C.c_uint, C.c_int, C.c_int),
            "glViewport": (None, C.c_int, C.c_int, C.c_int, C.c_int),
            "glDisable": (None, C.c_uint),
        }
        for name, signature in signatures.items():
            self._fn(name, *signature)

    def texture(self, size, *, target=TEXTURE_2D, internal=RGBA16F,
                levels=1, value=float("nan"), linear=True):
        """Allocate every requested face/mip and poison unwritten texels."""
        texture = C.c_uint()
        self.glGenTextures(1, C.byref(texture))
        self.textures.append(texture.value)
        self.glBindTexture(target, texture.value)
        channels = 1 if internal in (R16F, R32F) else 2 if internal == RG16F else 4
        format_ = RED if channels == 1 else RG if channels == 2 else RGBA
        for level in range(levels):
            dimensions = tuple(max(1, n >> level) for n in size)
            components = channels
            for n in dimensions:
                components *= n
            if isinstance(value, (tuple, list)):
                assert len(value) == channels
                payload = struct.pack(f"<{channels}f", *value) * (components // channels)
            else:
                payload = struct.pack("<f", value) * components
            data = C.c_char_p(payload)
            if target in (TEXTURE_3D, TEXTURE_2D_ARRAY):
                self.glTexImage3D(target, level, internal, *dimensions, 0, format_, FLOAT, data)
            else:
                faces = range(6) if target == TEXTURE_CUBE_MAP else (0,)
                for face in faces:
                    face_target = 0x8515 + face if target == TEXTURE_CUBE_MAP else target
                    self.glTexImage2D(face_target, level, internal, *dimensions, 0, format_, FLOAT, data)
        filter_ = 0x2601 if linear else 0x2600
        self.glTexParameteri(target, 0x2800, filter_)
        self.glTexParameteri(target, 0x2801, 0x2703 if levels > 1 and linear else filter_)
        for axis in (0x2802, 0x2803, 0x8072):
            self.glTexParameteri(target, axis, 0x812F)
        self.glTexParameteri(target, 0x813D, levels - 1)
        self.check("test texture allocation")
        return texture.value

    def sampler(self, unit, texture, target=TEXTURE_2D):
        self.glActiveTexture(0x84C0 + unit)
        self.glBindTexture(target, texture)

    def image(self, binding, texture, *, internal=RGBA16F, level=0, layered=False):
        self.glBindImageTexture(binding, texture, level, layered, 0, 0x88BA, internal)

    def texels(self, texture, size, *, target=TEXTURE_2D, level=0, face=0, channels=4):
        self.glMemoryBarrier(self.ALL_BARRIER_BITS)
        previous = C.c_int()
        binding = {TEXTURE_2D: 0x8069, TEXTURE_3D: 0x806A, TEXTURE_CUBE_MAP: 0x8514, TEXTURE_2D_ARRAY: 0x8C1D}[target]
        self.glGetIntegerv(binding, C.byref(previous))
        self.glBindTexture(target, texture)
        count = channels
        for n in size:
            count *= max(1, n >> level)
        values = (C.c_float * count)()
        read_target = 0x8515 + face if target == TEXTURE_CUBE_MAP else target
        self.glGetTexImage(read_target, level, RED if channels == 1 else RG if channels == 2 else RGBA, FLOAT, values)
        self.glBindTexture(target, previous.value)
        self.check("test texture readback")
        return tuple(values)

    def matrix(self, program, name, values):
        self.glUseProgram(program)
        location = self.glGetUniformLocation(program, name.encode())
        assert location >= 0, f"Inactive matrix uniform {name}"
        self.glUniformMatrix4fv(location, 1, False, (C.c_float * 16)(*values))

    def fullscreen(self, program, width, height, outputs):
        framebuffer, array = C.c_uint(), C.c_uint()
        self.glGenFramebuffers(1, C.byref(framebuffer))
        self.glGenVertexArrays(1, C.byref(array))
        self.framebuffers.append(framebuffer.value)
        self.arrays.append(array.value)
        self.glBindFramebuffer(0x8D40, framebuffer.value)
        for i, texture in enumerate(outputs):
            self.glFramebufferTexture2D(0x8D40, 0x8CE0 + i, TEXTURE_2D, texture, 0)
        self.glDrawBuffers(len(outputs), (C.c_uint * len(outputs))(*(0x8CE0 + i for i in range(len(outputs)))))
        assert self.glCheckFramebufferStatus(0x8D40) == 0x8CD5
        self.glViewport(0, 0, width, height)
        self.glDisable(0x0B71)  # depth test
        self.glDisable(0x0BE2)  # blending
        self.glBindVertexArray(array.value)
        self.glUseProgram(program)
        self.glDrawArrays(0x0004, 0, 3)
        self.glBindFramebuffer(0x8D40, 0)
        self.check("test fullscreen pass")

    def close(self):
        for names, function in ((self.arrays, self.glDeleteVertexArrays),
                                (self.framebuffers, self.glDeleteFramebuffers),
                                (self.textures, self.glDeleteTextures)):
            if names:
                function(len(names), (C.c_uint * len(names))(*names))
                names.clear()
        super().close()


FULLSCREEN_VERTEX = """#version 430 core
out vec2 TexCoords;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    TexCoords = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
"""
