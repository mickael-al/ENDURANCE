#!/usr/bin/env python3
"""Exercise the production sea renderer's publication, GL ownership and lifetime.

The actual shaders are executed by check_render.py. This fixture replaces only
GL/engine services so allocation failures and previous draw state are controlled.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
GL_STUB = r'''
#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <vector>
using GLenum=unsigned; using GLuint=unsigned; using GLint=int; using GLsizei=int;
using GLsizeiptr=std::ptrdiff_t; using GLintptr=std::ptrdiff_t; using GLint64=std::int64_t;
using GLboolean=unsigned char;
constexpr GLenum GL_NO_ERROR=0,GL_OUT_OF_MEMORY=1,GL_INVALID_OPERATION=2,
 GL_VERTEX_SHADER=3,GL_FRAGMENT_SHADER=4,GL_DYNAMIC_DRAW=5,GL_CURRENT_PROGRAM=6,
 GL_VERTEX_ARRAY_BINDING=7,GL_ACTIVE_TEXTURE=8,GL_TEXTURE_BINDING_2D=9,
 GL_SHADER_STORAGE_BUFFER_BINDING=10,GL_SHADER_STORAGE_BUFFER_START=11,
 GL_SHADER_STORAGE_BUFFER_SIZE=12,GL_CULL_FACE=13,GL_DEPTH_WRITEMASK=14,
 GL_TEXTURE_2D=15,GL_TEXTURE_2D_ARRAY=16,GL_SHADER_STORAGE_BUFFER=17,GL_TRIANGLES=18,
 GL_STATIC_DRAW=19,GL_UNSIGNED_SHORT=20,GL_STREAM_DRAW=21,GL_TEXTURE0=100,GL_TEXTURE4=104;
constexpr GLboolean GL_FALSE=0,GL_TRUE=1;
namespace SeaFixture {
 struct Binding { GLuint buffer=0; GLintptr offset=0; GLsizeiptr size=0; };
 inline GLuint next=1000,program=0,vao=0,genericStorage=0;
 inline GLenum active=GL_TEXTURE0,error=GL_NO_ERROR;
 inline bool culling=true,depthWrite=true,failAllocation=false,failUpload=false,failCompile=false;
 inline unsigned allocations=0,uploads=0,uniforms=0,draws=0,atmosphereCalls=0;
 inline std::set<GLuint> inFlightStores;
 inline GLsizei lastInstances=0;
 inline std::map<GLuint,std::vector<unsigned char>> buffers;
 inline std::set<GLuint> vaos,programs;
 inline std::map<unsigned,GLuint> textures2D,texturesArray;
 inline std::map<unsigned,Binding> indexed;
 inline std::map<GLuint,GLuint> elements;
}
inline void glCreateVertexArrays(GLsizei n,GLuint* ids) { while(n--) { *ids=SeaFixture::next++; SeaFixture::vaos.insert(*ids++); } }
inline void glDeleteVertexArrays(GLsizei n,const GLuint* ids) { while(n--) { SeaFixture::elements.erase(*ids); assert(SeaFixture::vaos.erase(*ids++)==1); } }
inline void glCreateBuffers(GLsizei n,GLuint* ids) { while(n--) { *ids=SeaFixture::next++; SeaFixture::buffers[*ids++]; } }
inline void glDeleteBuffers(GLsizei n,const GLuint* ids) { while(n--) assert(SeaFixture::buffers.erase(*ids++)==1); }
inline void glDeleteProgram(GLuint id) { assert(SeaFixture::programs.erase(id)==1); }
inline GLint glGetUniformLocation(GLuint,const char*) { return 1; }
inline void glProgramUniform1f(GLuint,GLint,float) { ++SeaFixture::uniforms; }
inline void glProgramUniform1i(GLuint,GLint,int) { ++SeaFixture::uniforms; }
inline GLenum glGetError() { const auto e=SeaFixture::error; SeaFixture::error=0; return e; }
inline void glNamedBufferData(GLuint id,GLsizeiptr size,const void* data,GLenum) {
 ++SeaFixture::allocations;
 if(SeaFixture::failAllocation) { SeaFixture::failAllocation=false; SeaFixture::error=GL_OUT_OF_MEMORY; return; }
 SeaFixture::buffers.at(id).resize(static_cast<std::size_t>(size));
 SeaFixture::inFlightStores.erase(id); // orphan creates a new writable store
 if(data) std::memcpy(SeaFixture::buffers.at(id).data(),data,static_cast<std::size_t>(size));
}
inline void glVertexArrayElementBuffer(GLuint vao,GLuint buffer) {
 assert(SeaFixture::vaos.count(vao) && SeaFixture::buffers.count(buffer)); SeaFixture::elements[vao]=buffer;
}
inline void glNamedBufferSubData(GLuint id,GLintptr start,GLsizeiptr size,const void* data) {
 ++SeaFixture::uploads;
 assert(!SeaFixture::inFlightStores.count(id)); // no in-place overwrite of the last draw
 if(SeaFixture::failUpload) { SeaFixture::failUpload=false; SeaFixture::error=GL_INVALID_OPERATION; return; }
 auto& b=SeaFixture::buffers.at(id); assert(start>=0 && size>=0 && static_cast<std::size_t>(start+size)<=b.size());
 std::memcpy(b.data()+start,data,static_cast<std::size_t>(size));
}
inline void glGetIntegerv(GLenum p,GLint* v) {
 using namespace SeaFixture;
 if(p==GL_CURRENT_PROGRAM) *v=program; else if(p==GL_VERTEX_ARRAY_BINDING) *v=vao;
 else if(p==GL_ACTIVE_TEXTURE) *v=active; else if(p==GL_TEXTURE_BINDING_2D) *v=textures2D[active-GL_TEXTURE0];
 else if(p==GL_SHADER_STORAGE_BUFFER_BINDING) *v=genericStorage; else assert(false);
}
inline void glGetIntegeri_v(GLenum p,GLuint index,GLint* v) { assert(p==GL_SHADER_STORAGE_BUFFER_BINDING); *v=SeaFixture::indexed[index].buffer; }
inline void glGetInteger64i_v(GLenum p,GLuint index,GLint64* v) {
 if(p==GL_SHADER_STORAGE_BUFFER_START) *v=SeaFixture::indexed[index].offset;
 else { assert(p==GL_SHADER_STORAGE_BUFFER_SIZE); *v=SeaFixture::indexed[index].size; }
}
inline GLboolean glIsEnabled(GLenum p) { assert(p==GL_CULL_FACE); return SeaFixture::culling; }
inline void glGetBooleanv(GLenum p,GLboolean* v) { assert(p==GL_DEPTH_WRITEMASK); *v=SeaFixture::depthWrite; }
inline void glActiveTexture(GLenum t) { SeaFixture::active=t; }
inline void glBindTexture(GLenum target,GLuint t) {
 if(target==GL_TEXTURE_2D) SeaFixture::textures2D[SeaFixture::active-GL_TEXTURE0]=t;
 else { assert(target==GL_TEXTURE_2D_ARRAY); SeaFixture::texturesArray[SeaFixture::active-GL_TEXTURE0]=t; }
}
inline void glBindBuffer(GLenum target,GLuint b) { assert(target==GL_SHADER_STORAGE_BUFFER); SeaFixture::genericStorage=b; }
inline void glBindBufferBase(GLenum target,GLuint index,GLuint b) {
 glBindBuffer(target,b); SeaFixture::indexed[index]={b,0,b ? static_cast<GLsizeiptr>(SeaFixture::buffers.at(b).size()) : 0};
}
inline void glBindBufferRange(GLenum target,GLuint index,GLuint b,GLintptr start,GLsizeiptr size) {
 glBindBuffer(target,b); SeaFixture::indexed[index]={b,start,size};
}
inline void glUseProgram(GLuint p) { SeaFixture::program=p; }
inline void glBindVertexArray(GLuint v) { SeaFixture::vao=v; }
inline void glEnable(GLenum p) { assert(p==GL_CULL_FACE); SeaFixture::culling=true; }
inline void glDisable(GLenum p) { assert(p==GL_CULL_FACE); SeaFixture::culling=false; }
inline void glDepthMask(GLboolean b) { SeaFixture::depthWrite=b; }
inline void glDrawElementsInstanced(GLenum mode,GLsizei count,GLenum type,const void* indices,GLsizei instances) {
 using namespace SeaFixture;
 assert(mode==GL_TRIANGLES && type==GL_UNSIGNED_SHORT && indices==nullptr && count==384 && instances>0);
 assert(!culling && !depthWrite && programs.count(program) && vaos.count(vao));
 const auto& elementData=buffers.at(elements.at(vao)); assert(elementData.size()==384*2);
 for(std::size_t i=0;i<384;++i) { std::uint16_t index; std::memcpy(&index,elementData.data()+2*i,2); assert(index<81); }
 assert(textures2D[4]==111 && indexed[8].buffer && buffers.at(indexed[8].buffer).size()>=static_cast<std::size_t>(instances)*16);
 ++draws; lastInstances=instances;
 inFlightStores.insert(indexed[8].buffer);
}
'''
ENGINE_STUB = r'''
#pragma once
#include "glcore.hpp"
namespace Ge {
 class FrameBuffer { public: unsigned getPosition() const { return 111; } };
 class Atmosphere { public: void bindLighting(unsigned,bool) { ++SeaFixture::atmosphereCalls; glActiveTexture(GL_TEXTURE0+12); } };
 class RenderingEngine {
 public:
  FrameBuffer framebuffer; Atmosphere atmosphere; bool atmosphereEnabled=true;
  FrameBuffer* getFrameBuffer() { return &framebuffer; }
  Atmosphere* getAtmosphere() { return atmosphereEnabled ? &atmosphere : nullptr; }
 };
}
struct ptrClass { Ge::RenderingEngine* renderingEngine=nullptr; };
'''


def main():
    with tempfile.TemporaryDirectory(prefix="sea-runtime-") as directory:
        directory = Path(directory)
        stub = directory / "stubs"
        stub.mkdir()
        (stub / "glcore.hpp").write_text(GL_STUB)
        (stub / "sea_fixture_engine.hpp").write_text(ENGINE_STUB)
        for name in ("PointeurClass", "RenderingEngine", "FrameBuffer", "Atmosphere"):
            (stub / (name + ".hpp")).write_text('#include "sea_fixture_engine.hpp"\n')
        includes = (stub, ROOT / "ANTAGONIZ/Header/ProjectUser", ROOT / "ANTAGONIZ/Header/Miscellaneous",
                    ROOT / "ANTAGONIZ/Header/Rendering/Shader", ROOT / "ThirdParty/glm", ROOT / "ThirdParty")
        for label, optimization in (("normal", ["-O2"]), ("fast", ["-O3", "-ffast-math"])):
            command = [os.environ.get("CXX", "c++"), "-std=c++17", *optimization]
            for include in includes:
                command += ["-I", str(include)]
            binary = directory / label
            subprocess.run(command + [str(ROOT / "Tests/Sea/check_runtime.cpp"),
                str(ROOT / "ANTAGONIZ/Source/ProjectUser/EnduranceSea.cpp"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
