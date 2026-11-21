#pragma once
// Test-only instrumentation. Forced into all runner translation units so the
// counters describe production callers, including GL 1.x functions not exposed
// through GLEW pointers. Single render thread; no GPU queries or synchronization.
#include "glcore.hpp"
#include <cstdint>
namespace EndurancePerf
{
    struct Calls
    {
        std::uint64_t stateQueries=0, uniformLookups=0, bufferQueries=0;
        std::uint64_t bufferAllocations=0, bufferAllocationBytes=0;
        std::uint64_t bufferUpdates=0, bufferUpdateBytes=0, bufferCopies=0, bufferCopyBytes=0;
        std::uint64_t textureBinds=0, bufferBinds=0, programBinds=0, vertexArrayBinds=0;
        std::uint64_t barriers=0, dispatches=0, indirectCalls=0, indirectCommands=0;
    };
    inline Calls calls;
    inline bool enabled=false;
    inline void reset() { calls={}; }
    inline void getIntegerv(GLenum a,GLint* b) { if(enabled)++calls.stateQueries; glGetIntegerv(a,b); }
    inline void getIntegeri(GLenum a,GLuint b,GLint* c) { if(enabled)++calls.stateQueries; glGetIntegeri_v(a,b,c); }
    inline void getInteger64i(GLenum a,GLuint b,GLint64* c) { if(enabled)++calls.stateQueries; glGetInteger64i_v(a,b,c); }
    inline GLboolean isEnabled(GLenum a) { if(enabled)++calls.stateQueries; return glIsEnabled(a); }
    inline GLint getUniform(GLuint a,const GLchar* b) { if(enabled)++calls.uniformLookups; return glGetUniformLocation(a,b); }
    inline void getBuffer(GLuint a,GLenum b,GLint64* c) { if(enabled)++calls.bufferQueries; glGetNamedBufferParameteri64v(a,b,c); }
    inline void bufferData(GLuint a,GLsizeiptr b,const void* c,GLenum d)
    { if(enabled){++calls.bufferAllocations;calls.bufferAllocationBytes+=b;} glNamedBufferData(a,b,c,d); }
    inline void bufferSubData(GLuint a,GLintptr b,GLsizeiptr c,const void* d)
    { if(enabled){++calls.bufferUpdates;calls.bufferUpdateBytes+=c;} glNamedBufferSubData(a,b,c,d); }
    inline void copyBuffer(GLuint a,GLuint b,GLintptr c,GLintptr d,GLsizeiptr e)
    { if(enabled){++calls.bufferCopies;calls.bufferCopyBytes+=e;} glCopyNamedBufferSubData(a,b,c,d,e); }
    inline void bindTexture(GLenum a,GLuint b) { if(enabled)++calls.textureBinds; glBindTexture(a,b); }
    inline void bindTextureUnit(GLuint a,GLuint b) { if(enabled)++calls.textureBinds; glBindTextureUnit(a,b); }
    inline void bindBuffer(GLenum a,GLuint b) { if(enabled)++calls.bufferBinds; glBindBuffer(a,b); }
    inline void bindBufferBase(GLenum a,GLuint b,GLuint c) { if(enabled)++calls.bufferBinds; glBindBufferBase(a,b,c); }
    inline void bindBufferRange(GLenum a,GLuint b,GLuint c,GLintptr d,GLsizeiptr e)
    { if(enabled)++calls.bufferBinds; glBindBufferRange(a,b,c,d,e); }
    inline void useProgram(GLuint a) { if(enabled)++calls.programBinds; glUseProgram(a); }
    inline void bindVertexArray(GLuint a) { if(enabled)++calls.vertexArrayBinds; glBindVertexArray(a); }
    inline void barrier(GLbitfield a) { if(enabled)++calls.barriers; glMemoryBarrier(a); }
    inline void dispatch(GLuint a,GLuint b,GLuint c) { if(enabled)++calls.dispatches; glDispatchCompute(a,b,c); }
    inline void multiDraw(GLenum a,GLenum b,const void* c,GLsizei d,GLsizei e)
    { if(enabled){++calls.indirectCalls;calls.indirectCommands+=d;} glMultiDrawElementsIndirect(a,b,c,d,e); }
}
#undef glGetIntegeri_v
#undef glGetInteger64i_v
#undef glGetUniformLocation
#undef glGetNamedBufferParameteri64v
#undef glNamedBufferData
#undef glNamedBufferSubData
#undef glCopyNamedBufferSubData
#undef glBindTextureUnit
#undef glBindBuffer
#undef glBindBufferBase
#undef glBindBufferRange
#undef glUseProgram
#undef glBindVertexArray
#undef glMemoryBarrier
#undef glDispatchCompute
#undef glMultiDrawElementsIndirect
#define glGetIntegerv EndurancePerf::getIntegerv
#define glGetIntegeri_v EndurancePerf::getIntegeri
#define glGetInteger64i_v EndurancePerf::getInteger64i
#define glIsEnabled EndurancePerf::isEnabled
#define glGetUniformLocation EndurancePerf::getUniform
#define glGetNamedBufferParameteri64v EndurancePerf::getBuffer
#define glNamedBufferData EndurancePerf::bufferData
#define glNamedBufferSubData EndurancePerf::bufferSubData
#define glCopyNamedBufferSubData EndurancePerf::copyBuffer
#define glBindTexture EndurancePerf::bindTexture
#define glBindTextureUnit EndurancePerf::bindTextureUnit
#define glBindBuffer EndurancePerf::bindBuffer
#define glBindBufferBase EndurancePerf::bindBufferBase
#define glBindBufferRange EndurancePerf::bindBufferRange
#define glUseProgram EndurancePerf::useProgram
#define glBindVertexArray EndurancePerf::bindVertexArray
#define glMemoryBarrier EndurancePerf::barrier
#define glDispatchCompute EndurancePerf::dispatch
#define glMultiDrawElementsIndirect EndurancePerf::multiDraw
