#!/usr/bin/env python3
"""Rasterized perspective spotlight shadows and real deferred integration.

A physical 2 m plate 5 m from the light casts a known expanding silhouette on
receivers at 10/20 m. The production helper samples the actual depth array;
the real deferred shader must also darken the occluded pixels. Layer zero is
empty to detect wrong indexing. All readbacks are test instrumentation.
"""
import ctypes as C
import math
import re
import struct

from check_screen_composition import ScreenGL, DeferredFixture
from gl_support import ROOT, RGBA32F, TEXTURE_2D, TEXTURE_2D_ARRAY, FULLSCREEN_VERTEX, shader_source

SIZE = (81, 67)


def perspective(angle):
    f = 1 / math.tan(math.radians(angle) / 2)
    near, far = .1, 1000.
    return (f, 0, 0, 0, 0, f, 0, 0, 0, 0, -(far+near)/(far-near), -1,
            0, 0, -2*far*near/(far-near), 0)


class SpotGL(ScreenGL):
    def __init__(self):
        super().__init__()
        for name, signature in {
            "glFramebufferTextureLayer": (None, C.c_uint, C.c_uint, C.c_uint, C.c_int, C.c_int),
            "glDrawBuffer": (None, C.c_uint), "glReadBuffer": (None, C.c_uint),
            "glEnable": (None, C.c_uint), "glDepthFunc": (None, C.c_uint),
            "glDepthMask": (None, C.c_ubyte), "glClearDepth": (None, C.c_double),
            "glClear": (None, C.c_uint),
        }.items():
            self._fn(name, *signature)
        self.depth_program = self.compile([(0x8B31, """#version 450
uniform mat4 projectionView;
void main() {
    const vec2 p[6]=vec2[6](vec2(-1,-1),vec2(1,-1),vec2(1,1),
                           vec2(-1,-1),vec2(1,1),vec2(-1,1));
    gl_Position=projectionView*vec4(p[gl_VertexID],-5,1);
}"""), (0x8B30, "#version 450\nvoid main() {}")], "perspective plate rasterization")
        self.probe = self.compile([(0x8B31, FULLSCREEN_VERTEX), (0x8B30, "#version 450\n" +
            shader_source(ROOT / "Asset/Shader/Shadow/spot_surface.glsl") + """
layout(binding=0) uniform sampler2DArray depthMap;
uniform mat4 projectionView;
uniform vec4 domain;
uniform float receiverZ;
uniform int layer;
uniform vec2 biasTexels;
in vec2 TexCoords;
out vec4 result;
void main() {
    vec3 p=vec3(mix(domain.xy,domain.zw,TexCoords),receiverZ);
    float shadow=geSpotSurfaceShadow(depthMap,projectionView,layer,p,vec3(0),
        vec3(0,0,1),vec3(0),biasTexels);
    result=vec4(shadow,p);
}""")], "actual perspective shadow helper")

    def rasterize(self, projection, resolution=256):
        texture = C.c_uint()
        self.glGenTextures(1, C.byref(texture)); self.textures.append(texture.value)
        self.glBindTexture(TEXTURE_2D_ARRAY, texture.value)
        self.glTexImage3D(TEXTURE_2D_ARRAY, 0, 0x81A7, resolution, resolution, 2, 0, 0x1902, 0x1406, None)
        for key, value in ((0x2800, 0x2600), (0x2801, 0x2600), (0x2802, 0x812F),
                           (0x2803, 0x812F), (0x813D, 0), (0x884C, 0)):
            self.glTexParameteri(TEXTURE_2D_ARRAY, key, value)
        framebuffer, array = C.c_uint(), C.c_uint()
        self.glGenFramebuffers(1, C.byref(framebuffer)); self.framebuffers.append(framebuffer.value)
        self.glGenVertexArrays(1, C.byref(array)); self.arrays.append(array.value)
        self.glBindFramebuffer(0x8D40, framebuffer.value)
        self.glDrawBuffer(0); self.glReadBuffer(0)
        self.glViewport(0, 0, resolution, resolution)
        self.glEnable(0x0B71); self.glDepthFunc(0x0201); self.glDepthMask(True)
        self.glClearDepth(1); self.glDisable(0x0BE2); self.glDisable(0x0B44)
        self.glBindVertexArray(array.value)
        self.matrix(self.depth_program, "projectionView", projection)
        for layer in range(2):
            self.glFramebufferTextureLayer(0x8D40, 0x8D00, texture.value, 0, layer)
            assert self.glCheckFramebufferStatus(0x8D40) == 0x8CD5
            self.glClear(0x00000100)
            if layer == 1:
                self.glDrawArrays(0x0004, 0, 6)
        self.glBindFramebuffer(0x8D40, 0)
        self.glMemoryBarrier(self.ALL_BARRIER_BITS)
        self.check("spot plate depth array rasterization")
        return texture.value

    def evaluate(self, texture, projection, z=-10., domain=(-6., -6., 6., 6.), layer=1):
        self.matrix(self.probe, "projectionView", projection)
        self.uniform(self.probe, "domain", "4f", *domain)
        self.uniform(self.probe, "receiverZ", "1f", z)
        self.uniform(self.probe, "layer", "1i", layer)
        self.uniform(self.probe, "biasTexels", "2f", .5, 1.5)
        output = self.texture(SIZE, internal=RGBA32F)
        self.sampler(0, texture, TEXTURE_2D_ARRAY)
        self.fullscreen(self.probe, *SIZE, [output])
        values = self.texels(output, SIZE)
        assert all(math.isfinite(v) for v in values)
        return [values[i:i+4] for i in range(0, len(values), 4)]


def check_helper(gl):
    for angle in (45., 90., 120.):
        projection = perspective(angle)
        for resolution in (128, 256):
            texture = gl.rasterize(projection, resolution)
            for distance in (10., 20.):
                half = distance / 5  # Independent pinhole-projection geometry.
                result = gl.evaluate(texture, projection, z=-distance,
                                     domain=(-3*half, -3*half, 3*half, 3*half))
                core = [v for v, x, y, _ in result if max(abs(x), abs(y)) < .65*half]
                exterior = [v for v, x, y, _ in result if max(abs(x), abs(y)) > 1.4*half]
                assert core and max(core) < 1e-6, (angle, resolution, distance, "occluder lost")
                assert exterior and min(exterior) > 1-1e-6, (angle, resolution, distance, "false shadow")
            for layer in (-1, 0, 2):
                assert min(row[0] for row in gl.evaluate(texture, projection, layer=layer)) > 1-1e-6
            for z, domain in ((10., (-1., -1., 1., 1.)), (-.05, (-.01, -.01, .01, .01)),
                              (-2000., (-1., -1., 1., 1.)), (-10., (100., 100., 110., 110.))):
                assert min(row[0] for row in gl.evaluate(texture, projection, z=z, domain=domain)) > 1-1e-6
    print("PASS spot perspective silhouette: 12 angle/resolution/distance cases, wrong layers, behind/near/far/outside receivers", flush=True)


def check_deferred(gl, angle=90., camera_x=3.):
    projection = perspective(angle)
    depth = gl.rasterize(projection)
    fixture = DeferredFixture(gl, SIZE, 0, 0, "opaque", camera_position=(camera_x, 0., 0.))
    extent = 6. if angle == 90 else .06
    positions = [(2*extent*(x+.5)/SIZE[0]-extent, 2*extent*(y+.5)/SIZE[1]-extent, -10., 0.)
                 for y in range(SIZE[1]) for x in range(SIZE[0])]
    view_positions = [(x-camera_x, y, z, w) for x, y, z, w in positions]
    for unit, values in ((0, view_positions), (1, [(0, 0, 1, 1)]*len(positions)),
                         (2, [(1, 1, 1, 1)]*len(positions)), (3, [(0, .8, 1, 1)]*len(positions))):
        fixture.samplers[unit] = (gl.field_texture(SIZE, values, repeat=True), TEXTURE_2D)
    fixture.samplers[4] = (depth, TEXTURE_2D_ARRAY)
    light = bytearray(80)
    struct.pack_into("<3f", light, 0, 0, 0, 0)
    struct.pack_into("<3f", light, 16, 1, 1, 1)
    struct.pack_into("<3f", light, 32, 0, 0, -1)
    struct.pack_into("<2f4i", light, 44, 1000, angle, 2, 1, -1, -1)
    struct.pack_into("<2f", light, 68, .5, 1.5)
    gl.buffer(bytes(light), 1)
    gl.buffer(struct.pack("<i4fi", 1, 0., 1., 0., 75., 0), 2)
    gl.buffer(struct.pack("<20f", *projection, 0, 0, 0, -1000)*2, 3)
    vertex = shader_source(ROOT / "Asset/Shader/forward.vs.glsl")
    fragment = shader_source(ROOT / "Asset/Shader/forward.fs.glsl")
    program = gl.compile([(0x8B31, vertex), (0x8B30, fragment)], "actual deferred spotlight consumer")
    occluded = fixture.render(program, False)
    struct.pack_into("<i", light, 56, 0)  # Same scene, empty depth layer.
    gl.buffer(bytes(light), 1)
    unoccluded = fixture.render(program, False)
    core, exterior = [], []
    for position, dark, bright in zip(positions, occluded, unoccluded):
        if max(abs(position[0]), abs(position[1])) < min(1.3, extent*.25):
            assert min(bright) > 1, "Spotlight baseline did not illuminate the receiver"
            core.append(max(dark)/max(bright))
        if 3 < max(abs(position[0]), abs(position[1])) < 5:
            exterior.append(max(abs(a-b) for a, b in zip(dark, bright)))
    assert core and max(core) < .02, ("Spot branch ignored its shadow map", max(core))
    if angle == 90:
        assert exterior and max(exterior) < 1e-5, "Spot shadow leaked outside projected plate"
    print(f"PASS actual deferred spotlight branch ({angle:g} deg, camera X={camera_x:g}): opaque receiver shadowed by perspective plate; lit baseline and exterior correct", flush=True)


def check_fresnel_rounding(gl):
    for name in ("forward.fs.glsl", "forward_transparency.fs.glsl", "water_transparency.fs.glsl"):
        source = (ROOT / "Asset/Shader" / name).read_text()
        helper = re.search(r"vec3 fresnelSchlick\([^}]+\}", source).group(0)
        fragment = "#version 450\n" + helper + """
uniform float cosine;
out vec4 result;
void main() { result=vec4(fresnelSchlick(cosine,vec3(.04,.2,.8)),1); }
"""
        program = gl.compile([(0x8B31, FULLSCREEN_VERTEX), (0x8B30, fragment)], name + " Fresnel roundoff")
        output = gl.texture((1, 1), internal=RGBA32F)
        for cosine in (1., 1.000000238418579, -0.000000238418579):
            gl.uniform(program, "cosine", "1f", cosine)
            gl.fullscreen(program, 1, 1, [output])
            values = gl.texels(output, (1, 1))[:3]
            expected = (.04, .2, .8) if cosine >= 1 else (1., 1., 1.)
            assert all(math.isfinite(v) for v in values)
            assert max(abs(a-b) for a, b in zip(values, expected)) < 1e-6
    print("PASS actual Fresnel functions: rounded unit cosines remain finite in opaque, transparent and water shaders", flush=True)


if __name__ == "__main__":
    gl = SpotGL()
    try:
        check_helper(gl)
        check_fresnel_rounding(gl)
        check_deferred(gl)
        check_deferred(gl, camera_x=0.)
        check_deferred(gl, 1.)
    finally:
        gl.close()
