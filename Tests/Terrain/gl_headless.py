"""Small OpenGL/EGL test driver using only the Python standard library.

Readbacks and blocking waits in this module are test instrumentation, not the
terrain's runtime path. Requires desktop OpenGL 4.3+ and EGL on Linux.
"""

from __future__ import annotations

import ctypes as C
import os
from pathlib import Path
import re
import shutil


class GLUnavailable(RuntimeError):
    pass


def copy_terrain_shaders(source: Path, destination: Path) -> None:
    """Copy editable terrain fixtures together with their shared noise library.

    Legacy reference checkouts may keep noise inside ProceduralTerrain; current
    shaders resolve ../Noise relative to this directory, just like the engine.
    """
    source, destination = Path(source), Path(destination)
    shutil.copytree(source, destination)
    noise = source.parent / "Noise"
    target = destination.parent / "Noise"
    if noise.is_dir() and noise.resolve() != target.resolve():
        shutil.copytree(noise, target, dirs_exist_ok=True)


def shader_source(path: Path, defines: str = "", stack: tuple[Path, ...] = ()) -> str:
    path = path.resolve()
    if path in stack:
        raise RuntimeError(f"Recursive shader include: {path}")
    try:
        source = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        # Windows-edited shader comments may still be CP1252. Preserve files.
        source = path.read_text(encoding="cp1252")
    source = re.sub(
        r'^\s*#include\s+"([^"]+)"\s*$',
        lambda match: shader_source(path.parent / match.group(1), stack=stack + (path,)),
        source,
        flags=re.MULTILINE,
    )
    if defines:
        line = source.find("\n", source.find("#version"))
        source = source[: line + 1] + defines + "\n" + source[line + 1 :]
    return source


class GL:
    SHADER_STORAGE_BUFFER = 0x90D2
    ALL_BARRIER_BITS = 0xFFFFFFFF

    def __init__(self):
        os.environ.setdefault("EGL_PLATFORM", "surfaceless")
        try:
            self.egl = C.CDLL("libEGL.so.1")
            self.lib = C.CDLL("libGL.so.1")
        except OSError as error:
            raise GLUnavailable(str(error)) from error
        self.buffers: list[int] = []
        self.programs: list[int] = []
        self._egl("eglGetDisplay", C.c_void_p, C.c_void_p)
        self._egl("eglInitialize", C.c_uint, C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_int))
        self._egl("eglBindAPI", C.c_uint, C.c_uint)
        self._egl("eglChooseConfig", C.c_uint, C.c_void_p, C.POINTER(C.c_int), C.POINTER(C.c_void_p), C.c_int, C.POINTER(C.c_int))
        self._egl("eglCreateContext", C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.POINTER(C.c_int))
        self._egl("eglMakeCurrent", C.c_uint, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p)
        self._egl("eglDestroyContext", C.c_uint, C.c_void_p, C.c_void_p)
        self._egl("eglTerminate", C.c_uint, C.c_void_p)
        self._egl("eglGetProcAddress", C.c_void_p, C.c_char_p)
        self.display = self.egl.eglGetDisplay(None)
        major, minor = C.c_int(), C.c_int()
        if not self.display or not self.egl.eglInitialize(self.display, C.byref(major), C.byref(minor)):
            raise GLUnavailable("Cannot initialize an EGL display (try EGL_PLATFORM=surfaceless).")
        if not self.egl.eglBindAPI(0x30A2):
            raise GLUnavailable("EGL desktop OpenGL API is unavailable.")
        attributes = (C.c_int * 5)(0x3033, 1, 0x3040, 8, 0x3038)
        config, count = C.c_void_p(), C.c_int()
        if not self.egl.eglChooseConfig(self.display, attributes, C.byref(config), 1, C.byref(count)) or not count.value:
            raise GLUnavailable("No EGL OpenGL configuration.")
        self.context = None
        for gl_minor in (5, 3):
            attributes = (C.c_int * 7)(0x3098, 4, 0x30FB, gl_minor, 0x30FD, 1, 0x3038)
            self.context = self.egl.eglCreateContext(self.display, config, None, attributes)
            if self.context:
                break
        if not self.context or not self.egl.eglMakeCurrent(self.display, None, None, self.context):
            raise GLUnavailable("Cannot create a surfaceless OpenGL 4.3+ core context.")
        self._fn("glGetString", C.c_char_p, C.c_uint)
        self._fn("glGetError", C.c_uint)
        self._fn("glGetIntegerv", None, C.c_uint, C.POINTER(C.c_int))
        self._fn("glCreateShader", C.c_uint, C.c_uint)
        self._fn("glShaderSource", None, C.c_uint, C.c_int, C.POINTER(C.c_char_p), C.POINTER(C.c_int))
        self._fn("glCompileShader", None, C.c_uint)
        self._fn("glGetShaderiv", None, C.c_uint, C.c_uint, C.POINTER(C.c_int))
        self._fn("glGetShaderInfoLog", None, C.c_uint, C.c_int, C.POINTER(C.c_int), C.c_void_p)
        self._fn("glDeleteShader", None, C.c_uint)
        self._fn("glCreateProgram", C.c_uint)
        self._fn("glAttachShader", None, C.c_uint, C.c_uint)
        self._fn("glTransformFeedbackVaryings", None, C.c_uint, C.c_int, C.POINTER(C.c_char_p), C.c_uint)
        self._fn("glLinkProgram", None, C.c_uint)
        self._fn("glGetProgramiv", None, C.c_uint, C.c_uint, C.POINTER(C.c_int))
        self._fn("glGetProgramInfoLog", None, C.c_uint, C.c_int, C.POINTER(C.c_int), C.c_void_p)
        self._fn("glDeleteProgram", None, C.c_uint)
        self._fn("glUseProgram", None, C.c_uint)
        self._fn("glGetUniformLocation", C.c_int, C.c_uint, C.c_char_p)
        for name, ctype, arity in (("1ui", C.c_uint, 1), ("1i", C.c_int, 1), ("1f", C.c_float, 1), ("2f", C.c_float, 2), ("3f", C.c_float, 3), ("4f", C.c_float, 4), ("3i", C.c_int, 3), ("3ui", C.c_uint, 3)):
            self._fn("glUniform" + name, None, C.c_int, *([ctype] * arity))
        self._fn("glGenBuffers", None, C.c_int, C.POINTER(C.c_uint))
        self._fn("glDeleteBuffers", None, C.c_int, C.POINTER(C.c_uint))
        self._fn("glBindBuffer", None, C.c_uint, C.c_uint)
        self._fn("glBindBufferBase", None, C.c_uint, C.c_uint, C.c_uint)
        self._fn("glBufferData", None, C.c_uint, C.c_ssize_t, C.c_void_p, C.c_uint)
        self._fn("glBufferSubData", None, C.c_uint, C.c_ssize_t, C.c_ssize_t, C.c_void_p)
        self._fn("glGetBufferSubData", None, C.c_uint, C.c_ssize_t, C.c_ssize_t, C.c_void_p)
        self._fn("glDispatchCompute", None, C.c_uint, C.c_uint, C.c_uint)
        self._fn("glMemoryBarrier", None, C.c_uint)
        self._fn("glFinish", None)
        self.version = self.glGetString(0x1F02).decode()
        self.renderer = self.glGetString(0x1F01).decode()

    def _egl(self, name, result, *arguments):
        function = getattr(self.egl, name)
        function.restype = result
        function.argtypes = arguments

    def _fn(self, name, result, *arguments):
        address = self.egl.eglGetProcAddress(name.encode())
        if not address:
            raise GLUnavailable(f"Missing OpenGL entry point: {name}")
        setattr(self, name, C.CFUNCTYPE(result, *arguments)(address))

    def compile(self, stages: list[tuple[int, str]], label: str = "shader", feedback: tuple[str, ...] = ()) -> int:
        shaders = []
        program = self.glCreateProgram()
        try:
            for kind, source in stages:
                shader = self.glCreateShader(kind)
                shaders.append(shader)
                strings = (C.c_char_p * 1)(source.encode())
                self.glShaderSource(shader, 1, strings, None)
                self.glCompileShader(shader)
                status = C.c_int()
                self.glGetShaderiv(shader, 0x8B81, C.byref(status))
                if not status.value:
                    log = C.create_string_buffer(65536)
                    self.glGetShaderInfoLog(shader, len(log), None, log)
                    raise AssertionError(f"{label}: shader compilation failed:\n{log.value.decode()}")
                self.glAttachShader(program, shader)
            if feedback:
                names=(C.c_char_p*len(feedback))(*(name.encode() for name in feedback))
                self.glTransformFeedbackVaryings(program,len(feedback),names,0x8C8C)
            self.glLinkProgram(program)
            status = C.c_int()
            self.glGetProgramiv(program, 0x8B82, C.byref(status))
            if not status.value:
                log = C.create_string_buffer(65536)
                self.glGetProgramInfoLog(program, len(log), None, log)
                raise AssertionError(f"{label}: program link failed:\n{log.value.decode()}")
        except BaseException:
            self.glDeleteProgram(program)
            raise
        finally:
            for shader in shaders:
                self.glDeleteShader(shader)
        self.programs.append(program)
        return program

    def compute(self, path: Path, defines: str = "") -> int:
        return self.compile([(0x91B9, shader_source(path, defines))], str(path))

    def uniform(self, program: int, name: str, suffix: str, *values) -> None:
        self.glUseProgram(program)
        location = self.glGetUniformLocation(program, name.encode())
        if location >= 0:
            getattr(self, "glUniform" + suffix)(location, *values)

    def buffer(self, payload: bytes | int, binding: int | None = None) -> int:
        buffer = C.c_uint()
        self.glGenBuffers(1, C.byref(buffer))
        self.glBindBuffer(self.SHADER_STORAGE_BUFFER, buffer.value)
        if isinstance(payload, int):
            self.glBufferData(self.SHADER_STORAGE_BUFFER, payload, None, 0x88EA)
        else:
            self.glBufferData(self.SHADER_STORAGE_BUFFER, len(payload), C.c_char_p(payload), 0x88EA)
        self.buffers.append(buffer.value)
        if binding is not None:
            self.bind(buffer.value, binding)
        return buffer.value

    def resize(self, buffer: int, payload: bytes | int) -> None:
        self.glBindBuffer(self.SHADER_STORAGE_BUFFER, buffer)
        if isinstance(payload, int):
            self.glBufferData(self.SHADER_STORAGE_BUFFER, payload, None, 0x88EA)
        else:
            self.glBufferData(self.SHADER_STORAGE_BUFFER, len(payload), C.c_char_p(payload), 0x88EA)

    def bind(self, buffer: int, binding: int) -> None:
        self.glBindBufferBase(self.SHADER_STORAGE_BUFFER, binding, buffer)

    def write(self, buffer: int, payload: bytes, offset: int = 0) -> None:
        self.glBindBuffer(self.SHADER_STORAGE_BUFFER, buffer)
        self.glBufferSubData(self.SHADER_STORAGE_BUFFER, offset, len(payload), C.c_char_p(payload))

    def read(self, buffer: int, size: int, offset: int = 0) -> bytes:
        self.glBindBuffer(self.SHADER_STORAGE_BUFFER, buffer)
        data = C.create_string_buffer(size)
        self.glGetBufferSubData(self.SHADER_STORAGE_BUFFER, offset, size, data)
        return data.raw

    def dispatch(self, program: int, x: int, y: int = 1, z: int = 1, barrier: int = ALL_BARRIER_BITS) -> None:
        self.glUseProgram(program)
        self.glDispatchCompute(x, y, z)
        self.glMemoryBarrier(barrier)

    def dispatch_extent(self, program: int, x: int, y: int = 1, z: int = 1) -> None:
        local = (C.c_int * 3)()
        self.glGetProgramiv(program, 0x8267, local)
        groups = tuple((extent + size - 1) // size for extent, size in zip((x, y, z), local))
        self.dispatch(program, *groups)

    def check(self, label: str = "OpenGL") -> None:
        error = self.glGetError()
        if error:
            raise AssertionError(f"{label}: GL error {error:#x}")

    def close(self) -> None:
        for program in self.programs:
            self.glDeleteProgram(program)
        for buffer in self.buffers:
            self.glDeleteBuffers(1, C.byref(C.c_uint(buffer)))
        self.egl.eglMakeCurrent(self.display, None, None, None)
        self.egl.eglDestroyContext(self.display, self.context)
        self.egl.eglTerminate(self.display)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()
