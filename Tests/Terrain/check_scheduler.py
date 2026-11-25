#!/usr/bin/env python3
"""Exercise the actual terrain scheduler with controlled completion latency.

This builds ProceduralGeneration.cpp, LodPlanner, Streaming and Lod unchanged.
Only GL/engine services and the GPU/collision backends are test doubles. Thus
frame counts describe orchestration, not GPU performance or full-engine rendering.
An optional Git reference runs the same harness against the earlier scheduler.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MISC = Path('ANTAGONIZ/Header/Miscellaneous')
SOURCES = Path('ANTAGONIZ/Source/Miscellaneous')

GL_STUB = r'''
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "ProceduralTerrainMaterials.hpp"
using GLfloat=float;
using GLenum=unsigned;using GLint=int;using GLuint=unsigned;using GLint64=std::int64_t;using GLsizei=int;
using GLsizeiptr=std::int64_t;
constexpr unsigned GL_CURRENT_PROGRAM=1,GL_VERTEX_ARRAY_BINDING=2,
 GL_DRAW_INDIRECT_BUFFER_BINDING=3,GL_SHADER_STORAGE_BUFFER_BINDING=4,
 GL_SHADER_STORAGE_BUFFER_START=5,GL_SHADER_STORAGE_BUFFER_SIZE=6,
 GL_DRAW_INDIRECT_BUFFER=7,GL_SHADER_STORAGE_BUFFER=8,GL_DISPATCH_INDIRECT_BUFFER_BINDING=9,
 GL_DISPATCH_INDIRECT_BUFFER=10,GL_UNIFORM_BUFFER=11,GL_UNIFORM_BUFFER_BINDING=12,
 GL_UNIFORM_BUFFER_START=13,GL_UNIFORM_BUFFER_SIZE=14,GL_UNIFORM_BLOCK_DATA_SIZE=15,
 GL_DYNAMIC_DRAW=16,GL_BUFFER_SIZE=17,GL_INVALID_INDEX=0xffffffffu;
constexpr unsigned GL_ACTIVE_TEXTURE=18,GL_TEXTURE_BINDING_2D=19,GL_SAMPLER_BINDING=20,
 GL_TEXTURE0=100,GL_TEXTURE_2D=200,GL_UNPACK_ALIGNMENT=21,GL_UNPACK_ROW_LENGTH=22,
 GL_UNPACK_SKIP_ROWS=23,GL_UNPACK_SKIP_PIXELS=24,GL_PIXEL_UNPACK_BUFFER_BINDING=25,
 GL_RGBA8=37,GL_RGBA=38,GL_PIXEL_UNPACK_BUFFER=26,GL_R8=27,GL_TEXTURE_WIDTH=28,GL_RED=29,GL_UNSIGNED_BYTE=30,
 GL_TEXTURE_MIN_FILTER=31,GL_LINEAR_MIPMAP_LINEAR=32,GL_TEXTURE_MAG_FILTER=33,
 GL_LINEAR=34,GL_TEXTURE_WRAP_S=35,GL_TEXTURE_WRAP_T=36,GL_REPEAT=37;
constexpr bool GLEW_VERSION_4_6=true, GLEW_ARB_texture_filter_anisotropic=true, GLEW_EXT_texture_filter_anisotropic=true;
constexpr unsigned GL_TEXTURE_MAX_ANISOTROPY=39, GL_MAX_TEXTURE_MAX_ANISOTROPY=40;
namespace TerrainFixtureGL {
struct Binding {unsigned buffer=0;GLint64 start=0,size=0;};
inline unsigned next=1,uniform=0,program=0,uploads=0,uniformWrites=0;
inline int blockBytes=sizeof(ProceduralTerrain::TerrainMaterialTable);
inline bool allocationFailure=false,textureAllocationFailure=false;
inline unsigned activeTexture=GL_TEXTURE0,unpackBuffer=0,textureUploads=0,mipGenerations=0;
inline std::map<unsigned,unsigned> textureBindings,samplerBindings;
inline std::map<unsigned,int> textureLevels;
inline std::map<std::pair<unsigned,unsigned>,float> textureParameters;
inline std::map<unsigned,int> textures,pixelStore={{GL_UNPACK_ALIGNMENT,4}};
inline std::map<unsigned,std::vector<unsigned char>> buffers;
inline std::map<unsigned,Binding> uniformBindings;
inline std::map<std::string,int> locations;
inline std::map<int,float> floatUniforms;
inline std::map<int,std::array<float,3>> vec3Uniforms;
inline std::map<std::pair<unsigned,int>,float> programFloatUniforms;
inline std::map<std::pair<unsigned,int>,unsigned> programUIntUniforms;
inline std::map<std::pair<unsigned,int>,std::vector<unsigned>> programUIntArrays;
}
inline void glGetIntegerv(unsigned pname,GLint* x){
 *x=pname==GL_UNIFORM_BUFFER_BINDING?TerrainFixtureGL::uniform:
    pname==GL_CURRENT_PROGRAM?TerrainFixtureGL::program:
    pname==GL_ACTIVE_TEXTURE?TerrainFixtureGL::activeTexture:
    pname==GL_TEXTURE_BINDING_2D?TerrainFixtureGL::textureBindings[TerrainFixtureGL::activeTexture-GL_TEXTURE0]:
    pname==GL_PIXEL_UNPACK_BUFFER_BINDING?TerrainFixtureGL::unpackBuffer:TerrainFixtureGL::pixelStore[pname];}
inline void glGetIntegeri_v(unsigned pname,unsigned index,GLint* x){
 *x=pname==GL_UNIFORM_BUFFER_BINDING?TerrainFixtureGL::uniformBindings[index].buffer:
    pname==GL_SAMPLER_BINDING?TerrainFixtureGL::samplerBindings[index]:0;}
inline void glGetInteger64i_v(unsigned pname,unsigned index,GLint64* x){
 auto b=TerrainFixtureGL::uniformBindings[index];
 *x=pname==GL_UNIFORM_BUFFER_START?b.start:pname==GL_UNIFORM_BUFFER_SIZE?b.size:0;}
inline void glUseProgram(unsigned p){TerrainFixtureGL::program=p;}
inline void glBindVertexArray(unsigned){}
inline void glBindBuffer(unsigned target,unsigned p){if(target==GL_UNIFORM_BUFFER)TerrainFixtureGL::uniform=p;
 if(target==GL_PIXEL_UNPACK_BUFFER)TerrainFixtureGL::unpackBuffer=p;}
inline void glBindBufferRange(unsigned target,unsigned index,unsigned p,GLint64 start,GLint64 size){
 if(target==GL_UNIFORM_BUFFER){TerrainFixtureGL::uniform=p;TerrainFixtureGL::uniformBindings[index]={p,start,size};}}
inline void glBindBufferBase(unsigned target,unsigned index,unsigned p){
 if(target==GL_UNIFORM_BUFFER){TerrainFixtureGL::uniform=p;TerrainFixtureGL::uniformBindings[index]={p,0,0};}}
inline GLint glGetUniformLocation(unsigned,const char* name){
 auto& locations=TerrainFixtureGL::locations;auto it=locations.find(name);
 if(it!=locations.end())return it->second;int result=int(locations.size());locations[name]=result;return result;}
inline void glProgramUniform1f(unsigned program,int where,float value){
 TerrainFixtureGL::programFloatUniforms[{program,where}]=value;
 TerrainFixtureGL::floatUniforms[where]=value;++TerrainFixtureGL::uniformWrites;}
inline void glProgramUniform3f(unsigned,int where,float x,float y,float z){TerrainFixtureGL::vec3Uniforms[where]={x,y,z};}
inline void glProgramUniform1ui(unsigned program,int where,unsigned value){
 TerrainFixtureGL::programUIntUniforms[{program,where}]=value;}
inline void glProgramUniform1uiv(unsigned program,int where,int count,const unsigned* values){
 TerrainFixtureGL::programUIntArrays[{program,where}]={values,values+count};}
inline void glProgramUniform1i(unsigned,int,int){} inline void glUniform1i(int,int){}
inline GLuint glGetUniformBlockIndex(unsigned,const char*){return 0;}
inline void glGetActiveUniformBlockiv(unsigned,unsigned,unsigned,GLint* value){*value=TerrainFixtureGL::blockBytes;}
inline void glCreateBuffers(GLsizei count,GLuint* names){
 while(count--){*names=TerrainFixtureGL::next++;TerrainFixtureGL::buffers[*names++]={};}}
inline void glDeleteBuffers(GLsizei count,const GLuint* names){while(count--)TerrainFixtureGL::buffers.erase(*names++);}
inline void glNamedBufferData(GLuint name,GLsizeiptr size,const void* data,unsigned){
 if(TerrainFixtureGL::allocationFailure)return;auto& buffer=TerrainFixtureGL::buffers.at(name);
 buffer.resize(size);if(data)std::memcpy(buffer.data(),data,size);++TerrainFixtureGL::uploads;}
inline void glGetNamedBufferParameteri64v(GLuint name,unsigned,GLint64* size){*size=TerrainFixtureGL::buffers.at(name).size();}
inline void glActiveTexture(unsigned unit){TerrainFixtureGL::activeTexture=unit;}
inline void glBindTexture(unsigned,unsigned texture){TerrainFixtureGL::textureBindings[TerrainFixtureGL::activeTexture-GL_TEXTURE0]=texture;}
inline void glBindTextureUnit(unsigned unit,unsigned texture){TerrainFixtureGL::textureBindings[unit]=texture;}
inline void glBindSampler(unsigned unit,unsigned sampler){TerrainFixtureGL::samplerBindings[unit]=sampler;}
inline void glPixelStorei(unsigned field,int value){TerrainFixtureGL::pixelStore[field]=value;}
inline void glCreateTextures(unsigned,int count,unsigned* names){
 while(count--){*names=TerrainFixtureGL::next++;TerrainFixtureGL::textures[*names++]=0;}}
inline void glDeleteTextures(int count,const unsigned* names){while(count--)TerrainFixtureGL::textures.erase(*names++);}
inline void glTextureStorage2D(unsigned texture,int levels,unsigned,int width,int){
 if(!TerrainFixtureGL::textureAllocationFailure){TerrainFixtureGL::textures[texture]=width;TerrainFixtureGL::textureLevels[texture]=levels;}}
inline void glGetTextureLevelParameteriv(unsigned texture,int,unsigned,int* width){*width=TerrainFixtureGL::textures[texture];}
inline void glTextureSubImage2D(unsigned,int,int,int,int,int,unsigned,unsigned,const void*){++TerrainFixtureGL::textureUploads;}
inline void glGenerateTextureMipmap(unsigned){++TerrainFixtureGL::mipGenerations;}
inline void glTextureParameteri(unsigned texture,unsigned field,int value){TerrainFixtureGL::textureParameters[{texture,field}]=float(value);}
inline void glGetFloatv(unsigned,float* value){*value=16.f;}
inline void glTextureParameterf(unsigned texture,unsigned field,float value){TerrainFixtureGL::textureParameters[{texture,field}]=value;}
'''

ENGINE_STUB = r'''
#pragma once
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <vector>
#include "CustomRenderer.hpp"
struct SceneData {};
namespace Ge {
class GObject {
public: virtual ~GObject()=default;
 static std::vector<GObject*>& GetGObjects(){static std::vector<GObject*> objects;return objects;}
};
struct CameraUniform {glm::mat4 view{1},proj{1};};
class Camera:public GObject {
 float far=100;glm::vec3 position{};CameraUniform uniform;
public:
 Camera(){GetGObjects().push_back(this);}
 float getFar()const{return far;}void setFar(float value){far=value;}
 glm::vec3 getPosition()const{return position;}void setPosition(glm::vec3 value){position=value;}
 const CameraUniform* getUniformCamera()const{return &uniform;}
};
class CameraManager {public:Camera camera;Camera* getCurrentCamera(){return &camera;}};
class LightManager {public:bool getShadowMatrix(unsigned,glm::mat4& matrix){matrix=glm::mat4(1);return true;}};
class Atmosphere {public:glm::vec3 direction{0,1,0};glm::vec3 primarySpecularDirection()const{return direction;}};
class RenderingEngine {public:Atmosphere atmosphere;Atmosphere* getAtmosphere(){return &atmosphere;}};
class PhysicsWraper {};
class SettingManager {public:float gamma=2.2f;float getGamma()const{return gamma;}};
class ShaderPair {};
struct ShaderCacheOptions {bool enabled=false;};
class GraphiquePipeline {
 ShaderPair* pair=new ShaderPair;std::string error;unsigned program;bool valid;
public:
 GraphiquePipeline(unsigned id=1,bool success=true):program(id),valid(success){if(!valid)error="injected shader link failure";}
 bool isValid()const{return valid;}const std::string& getLastError()const{return error;}
 unsigned getProgram()const{return program;}ShaderPair* getShaderPair(){return pair;}
};
class GraphiquePipelineManager {
public:
 unsigned activePipelines=0,registeredRenderers=0,createdPipelines=0;
 bool failNextPipeline=false;
 std::vector<std::string> createdDefines;
 GraphiquePipeline* createPipeline(const std::string&,const std::string&,bool=false,bool=true,bool=false,
  int=1,std::string define="",const ShaderCacheOptions& = ShaderCacheOptions{}) {
  ++activePipelines;++createdPipelines;createdDefines.push_back(define);
  const bool valid=!failNextPipeline;failNextPipeline=false;return new GraphiquePipeline(createdPipelines,valid);
 }
 void destroyPipeline(GraphiquePipeline* p){--activePipelines;delete p;}
 void addCustomRenderer(CustomRenderer*){++registeredRenderers;}
 void removeCustomRenderer(CustomRenderer*){--registeredRenderers;}
};
}
struct ptrClass {
 Ge::GraphiquePipelineManager* graphiquePipelineManager=nullptr;
 Ge::CameraManager* cameraManager=nullptr;Ge::LightManager* lightManager=nullptr;
 Ge::RenderingEngine* renderingEngine=nullptr;
 Ge::PhysicsWraper* physicsEngine=nullptr;
 Ge::SettingManager* settingManager=nullptr;
};
namespace Ge {class Engine {public:static ptrClass* getPtrClassAddr(){static ptrClass value;return &value;}};}
'''


def build_and_run(source_root, temporary, label, baseline=False, instant_gpu=False, update_source=None):
    stub = temporary / 'stubs'
    stub.mkdir(exist_ok=True)
    (stub / 'glcore.hpp').write_text(GL_STUB)
    (stub / 'scheduler_engine.hpp').write_text(ENGINE_STUB)
    for name in ('Engine', 'PointeurClass', 'Camera', 'GraphiquePipeline', 'ShaderPair', 'RenderingEngine', 'Atmosphere'):
        (stub / (name + '.hpp')).write_text('#include "scheduler_engine.hpp"\n')
    binary = temporary / ('check_scheduler_' + label)
    flags = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O3', '-ffast-math', '-pthread']
    header = source_root / MISC / 'ProceduralGeneration.hpp'
    if 'sameFrameHandoffs' in header.read_text():
        flags.append('-DTERRAIN_SCHEDULER_LOOKAHEAD')
    if 'coverageComplete' in header.read_text() and (not update_source or
            'coverageComplete' in update_source.read_text()):
        flags.append('-DTERRAIN_SCHEDULER_COMPLETION')
    for include in (stub, source_root / MISC, ROOT / MISC,
                    ROOT / 'ANTAGONIZ/Header/Interface', ROOT / 'ThirdParty/glm',
                    ROOT / 'ThirdParty'):
        flags.extend(['-I', str(include)])
    files = [ROOT / 'Tests/Terrain/check_scheduler.cpp']
    files += [source_root / SOURCES / (name + '.cpp') for name in
              ('ProceduralGeneration', 'ProceduralTerrainPlanner',
               'ProceduralTerrainStreaming', 'ProceduralTerrainLod')]
    if update_source:
        files[1] = update_source.resolve()
    subprocess.run(flags + [str(p) for p in files] + ['-o', str(binary)], check=True)
    result = subprocess.run([str(binary)] + (['--baseline'] if baseline else [])
                            + (['--instant-gpu'] if instant_gpu else []),
                            text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    report = json.loads(result.stdout)
    production = files[1:] + [source_root / MISC / (name + '.hpp') for name in
                             ('ProceduralGeneration', 'ProceduralTerrainPlanner')]
    report['production_source_sha256'] = hashlib.sha256(
        b''.join(p.read_bytes() for p in production)).hexdigest()
    report['fixture_sha256'] = hashlib.sha256(files[0].read_bytes()).hexdigest()
    report['controlled_gpu_latency_updates'] = 0 if instant_gpu else 6
    report['controlled_gpu_stages'] = 'count/emit with deterministic empty-chunk pattern' if instant_gpu else 'single completion'
    report['worker_frame_pause_ms'] = 1
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    reference = parser.add_mutually_exclusive_group()
    reference.add_argument('--reference-ref', help='Earlier local Git commit, compared with the same harness')
    reference.add_argument('--reference-update', type=Path, help='Earlier ProceduralGeneration.cpp, same other sources')
    parser.add_argument('--instant-gpu', action='store_true',
                        help='Immediately ready count/emit fences and empty chunks, 3 in flight/16 per frame')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='terrain-scheduler-') as temp:
        temp = Path(temp)
        report = {'scope': 'Actual CPU scheduler/worker; controlled GPU completion, not hardware timing',
                  'current': build_and_run(ROOT, temp, 'current', instant_gpu=args.instant_gpu)}
        if args.reference_update:
            report['reference_update'] = str(args.reference_update)
            report['reference'] = build_and_run(ROOT, temp, 'reference', baseline=True,
                instant_gpu=args.instant_gpu, update_source=args.reference_update)
        if args.reference_ref:
            baseline = temp / 'reference'
            paths = [MISC / (name + '.hpp') for name in
                     ('ProceduralGeneration', 'ProceduralTerrainPlanner')]
            paths += [SOURCES / (name + '.cpp') for name in
                      ('ProceduralGeneration', 'ProceduralTerrainPlanner',
                       'ProceduralTerrainStreaming', 'ProceduralTerrainLod')]
            for path in paths:
                destination = baseline / path
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(subprocess.check_output(
                    ['git', 'show', args.reference_ref + ':' + path.as_posix()], cwd=ROOT))
            report['reference_ref'] = args.reference_ref
            report['reference'] = build_and_run(baseline, temp, 'reference', baseline=True,
                instant_gpu=args.instant_gpu)
    text = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    print(text, end='')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
