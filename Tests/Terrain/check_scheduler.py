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
using GLint=int;using GLuint=unsigned;using GLint64=std::int64_t;using GLsizei=int;
using GLsizeiptr=std::int64_t;
constexpr unsigned GL_CURRENT_PROGRAM=1,GL_VERTEX_ARRAY_BINDING=2,
 GL_DRAW_INDIRECT_BUFFER_BINDING=3,GL_SHADER_STORAGE_BUFFER_BINDING=4,
 GL_SHADER_STORAGE_BUFFER_START=5,GL_SHADER_STORAGE_BUFFER_SIZE=6,
 GL_DRAW_INDIRECT_BUFFER=7,GL_SHADER_STORAGE_BUFFER=8,GL_DISPATCH_INDIRECT_BUFFER_BINDING=9,
 GL_DISPATCH_INDIRECT_BUFFER=10,GL_UNIFORM_BUFFER=11,GL_UNIFORM_BUFFER_BINDING=12,
 GL_UNIFORM_BUFFER_START=13,GL_UNIFORM_BUFFER_SIZE=14,GL_UNIFORM_BLOCK_DATA_SIZE=15,
 GL_DYNAMIC_DRAW=16,GL_BUFFER_SIZE=17,GL_INVALID_INDEX=0xffffffffu;
namespace TerrainFixtureGL {
struct Binding {unsigned buffer=0;GLint64 start=0,size=0;};
inline unsigned next=1,uniform=0,program=0,uploads=0,uniformWrites=0;
inline int blockBytes=12064;
inline bool allocationFailure=false;
inline std::map<unsigned,std::vector<unsigned char>> buffers;
inline std::map<unsigned,Binding> uniformBindings;
inline std::map<std::string,int> locations;
inline std::map<int,float> floatUniforms;
}
inline void glGetIntegerv(unsigned pname,GLint* x){
 *x=pname==GL_UNIFORM_BUFFER_BINDING?TerrainFixtureGL::uniform:
    pname==GL_CURRENT_PROGRAM?TerrainFixtureGL::program:0;}
inline void glGetIntegeri_v(unsigned pname,unsigned index,GLint* x){
 *x=pname==GL_UNIFORM_BUFFER_BINDING?TerrainFixtureGL::uniformBindings[index].buffer:0;}
inline void glGetInteger64i_v(unsigned pname,unsigned index,GLint64* x){
 auto b=TerrainFixtureGL::uniformBindings[index];
 *x=pname==GL_UNIFORM_BUFFER_START?b.start:pname==GL_UNIFORM_BUFFER_SIZE?b.size:0;}
inline void glUseProgram(unsigned p){TerrainFixtureGL::program=p;}
inline void glBindVertexArray(unsigned){}
inline void glBindBuffer(unsigned target,unsigned p){if(target==GL_UNIFORM_BUFFER)TerrainFixtureGL::uniform=p;}
inline void glBindBufferRange(unsigned target,unsigned index,unsigned p,GLint64 start,GLint64 size){
 if(target==GL_UNIFORM_BUFFER){TerrainFixtureGL::uniform=p;TerrainFixtureGL::uniformBindings[index]={p,start,size};}}
inline void glBindBufferBase(unsigned target,unsigned index,unsigned p){
 if(target==GL_UNIFORM_BUFFER){TerrainFixtureGL::uniform=p;TerrainFixtureGL::uniformBindings[index]={p,0,0};}}
inline GLint glGetUniformLocation(unsigned,const char* name){
 auto& locations=TerrainFixtureGL::locations;auto it=locations.find(name);
 if(it!=locations.end())return it->second;int result=int(locations.size());locations[name]=result;return result;}
inline void glProgramUniform1f(unsigned,int where,float value){
 TerrainFixtureGL::floatUniforms[where]=value;++TerrainFixtureGL::uniformWrites;}
inline void glProgramUniform1ui(unsigned,int,unsigned){}
inline void glProgramUniform1uiv(unsigned,int,int,const unsigned*){}
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
class PhysicsWraper {};
class SettingManager {public:float gamma=2.2f;float getGamma()const{return gamma;}};
class ShaderPair {};
struct ShaderCacheOptions {bool enabled=false;};
class GraphiquePipeline {
 ShaderPair* pair=new ShaderPair;std::string error;
public:bool isValid()const{return true;}const std::string& getLastError()const{return error;}
 unsigned getProgram()const{return 1;}ShaderPair* getShaderPair(){return pair;}
};
class GraphiquePipelineManager {
public:
 unsigned activePipelines=0,registeredRenderers=0;
 template<class... T>GraphiquePipeline* createPipeline(T&&...){++activePipelines;return new GraphiquePipeline;}
 void destroyPipeline(GraphiquePipeline* p){--activePipelines;delete p;}
 void addCustomRenderer(CustomRenderer*){++registeredRenderers;}
 void removeCustomRenderer(CustomRenderer*){--registeredRenderers;}
};
}
struct ptrClass {
 Ge::GraphiquePipelineManager* graphiquePipelineManager=nullptr;
 Ge::CameraManager* cameraManager=nullptr;Ge::LightManager* lightManager=nullptr;
 Ge::PhysicsWraper* physicsEngine=nullptr;
 Ge::SettingManager* settingManager=nullptr;
};
namespace Ge {class Engine {public:static ptrClass* getPtrClassAddr(){static ptrClass value;return &value;}};}
'''


def build_and_run(source_root, temporary, label, baseline=False):
    stub = temporary / 'stubs'
    stub.mkdir(exist_ok=True)
    (stub / 'glcore.hpp').write_text(GL_STUB)
    (stub / 'scheduler_engine.hpp').write_text(ENGINE_STUB)
    for name in ('Engine', 'PointeurClass', 'Camera', 'GraphiquePipeline', 'ShaderPair'):
        (stub / (name + '.hpp')).write_text('#include "scheduler_engine.hpp"\n')
    binary = temporary / ('check_scheduler_' + label)
    flags = [os.environ.get('CXX', 'c++'), '-std=c++17', '-O3', '-ffast-math', '-pthread']
    header = source_root / MISC / 'ProceduralGeneration.hpp'
    if 'sameFrameHandoffs' in header.read_text():
        flags.append('-DTERRAIN_SCHEDULER_LOOKAHEAD')
    for include in (stub, source_root / MISC, ROOT / MISC,
                    ROOT / 'ANTAGONIZ/Header/Interface', ROOT / 'ThirdParty/glm',
                    ROOT / 'ThirdParty'):
        flags.extend(['-I', str(include)])
    files = [ROOT / 'Tests/Terrain/check_scheduler.cpp']
    files += [source_root / SOURCES / (name + '.cpp') for name in
              ('ProceduralGeneration', 'ProceduralTerrainPlanner',
               'ProceduralTerrainStreaming', 'ProceduralTerrainLod')]
    subprocess.run(flags + [str(p) for p in files] + ['-o', str(binary)], check=True)
    result = subprocess.run([str(binary)] + (['--baseline'] if baseline else []),
                            text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    report = json.loads(result.stdout)
    production = files[1:] + [source_root / MISC / (name + '.hpp') for name in
                             ('ProceduralGeneration', 'ProceduralTerrainPlanner')]
    report['production_source_sha256'] = hashlib.sha256(
        b''.join(p.read_bytes() for p in production)).hexdigest()
    report['fixture_sha256'] = hashlib.sha256(files[0].read_bytes()).hexdigest()
    report['controlled_gpu_latency_updates'] = 6
    report['worker_frame_pause_ms'] = 1
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference-ref', help='Earlier local Git commit, compared with the same harness')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='terrain-scheduler-') as temp:
        temp = Path(temp)
        report = {'scope': 'Actual CPU scheduler/worker; controlled GPU completion, not hardware timing',
                  'current': build_and_run(ROOT, temp, 'current')}
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
            report['reference'] = build_and_run(baseline, temp, 'reference', baseline=True)
    text = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    print(text, end='')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
