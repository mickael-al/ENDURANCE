#!/usr/bin/env python3
"""Exercise the production timestamp ring with an intentionally delayed GPU."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GL = """#pragma once
using GLuint=unsigned; using GLint=int; using GLsizei=int; using GLenum=unsigned; using GLuint64=unsigned long long;
constexpr GLenum GL_TIMESTAMP=1,GL_QUERY_RESULT_AVAILABLE=2,GL_QUERY_RESULT=3;
constexpr GLint GL_FALSE=0,GL_TRUE=1;
void glGenQueries(GLsizei,GLuint*); void glDeleteQueries(GLsizei,const GLuint*);
void glQueryCounter(GLuint,GLenum); void glGetQueryObjectiv(GLuint,GLenum,GLint*);
void glGetQueryObjectui64v(GLuint,GLenum,GLuint64*);
"""

def main():
    with tempfile.TemporaryDirectory(prefix="endurance-frame-profiler-") as directory:
        tmp = Path(directory)
        (tmp / "glcore.hpp").write_text(GL)
        for flags, name in ((["-O2"], "normal"), (["-O3", "-ffast-math"], "fast-math")):
            binary = tmp / name
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", *flags,
                "-I", str(tmp), "-I", str(ROOT / "ANTAGONIZ/Header/Interface"),
                str(ROOT / "ANTAGONIZ/Source/Rendering/FrameGpuProfiler.cpp"),
                str(ROOT / "Tests/Rendering/check_frame_profiler.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

if __name__ == "__main__":
    main()
