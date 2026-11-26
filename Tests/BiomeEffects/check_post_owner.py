#!/usr/bin/env python3
"""Validate actual PostProcess target ownership and pass routing on EGL.

Leaf passes are recorded test doubles; target allocation, resizing, ordering,
ping-pong decisions and release execute the unmodified production owner.
The screen shader/leaf implementation is tested separately by check_screen_effects.
"""
from pathlib import Path
import sys, tempfile, subprocess, os, ctypes as C
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tests/BiomeEffects'))
from check_screen_effects import SERVICES as BASE, AtmosphereGL

HARNESS=r'''
#include <vector>
#include <array>
#include <string>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include "glcore.hpp"
#define protected public
#define private public
#include "PostProcess.hpp"
#include "Bloom.hpp"
#include "Fog.hpp"
#include "VolumetricFog.hpp"
#include "DepthOfField.hpp"
#include "ColorTint.hpp"
#undef private
#undef protected
#include "Engine.hpp"
#include "GraphicsDataMisc.hpp"
#include "ShapeBuffer.hpp"
struct Call{const char*name;unsigned dst,src;};
static std::vector<Call> calls;
static void require(bool condition,const char*message){if(!condition)throw std::runtime_error(message);}
static void record(const char*name,unsigned dst,unsigned src){
 if(dst){glBindFramebuffer(GL_FRAMEBUFFER,dst);GLint attached=0;glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,&attached);require(src!=unsigned(attached),"read/write color feedback");require(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"incomplete pass output");}
 calls.push_back({name,dst,src});
}
namespace Ge{
#define SIMPLE_MODULE(Class) bool Class::initialize(GraphicsDataMisc*){return true;} void Class::release(){} void Class::resize(int,int){} void Class::onGui(PPSetting*,PPSetting*){}
SIMPLE_MODULE(Bloom)
SIMPLE_MODULE(Fog)
SIMPLE_MODULE(VolumetricFog)
SIMPLE_MODULE(DepthOfField)
SIMPLE_MODULE(ColorTint)
void Bloom::compute(unsigned,unsigned texture,unsigned,ShapeBuffer*,PPSetting*){record("bloom",m_outputFrameBuffer,texture);}
void Fog::compute(unsigned,unsigned,unsigned,ShapeBuffer*,PPSetting*){}
void VolumetricFog::compute(unsigned,unsigned,unsigned,ShapeBuffer*,PPSetting*){}
void DepthOfField::compute(unsigned dst,unsigned src,unsigned,ShapeBuffer*,PPSetting*){record("dof",dst,src);}
void ColorTint::compute(unsigned dst,unsigned src,unsigned,ShapeBuffer*,PPSetting*){record("tint",dst,src);}
bool EnvironmentScreenEffects::initialize(GraphicsDataMisc*){return true;}
void EnvironmentScreenEffects::release(){}
void EnvironmentScreenEffects::setState(const EnvironmentScreenState&s){m_state=s;}
bool EnvironmentScreenEffects::requested()const noexcept{return m_state.heat>0;}
void EnvironmentScreenEffects::compute(unsigned dst,unsigned src,unsigned,ShapeBuffer*,PPSetting*){record("environment",dst,src);}
}
static GLint textureWidth(unsigned id){if(!id)return 0;glBindTexture(GL_TEXTURE_2D,id);GLint result=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_WIDTH,&result);return result;}
extern "C" __attribute__((visibility("default"))) int run_post_check(){
 try{
 glewExperimental=GL_TRUE;auto status=glewInit();require(status==GLEW_OK||status==GLEW_ERROR_NO_GLX_DISPLAY,"glewInit");while(glGetError()!=GL_NO_ERROR){}
 GraphicsDataMisc graphics;graphics.str_width=64;graphics.str_height=32;Ge::Camera camera;graphics.current_camera=&camera;
 SettingManager settings;Ge::Engine::pointers.settingManager=&settings;
 Ge::PostProcess pp;Ge::ShapeBuffer quad;require(pp.initialize(&graphics),"PP initialization");
 auto draw=[&]{calls.clear();pp.compute(0,0,0,&quad);require(!calls.empty(),"missing bloom presentation");};
 draw();require(calls.size()==1&&calls[0].dst==0,"disabled routing");require(pp.m_postTextures[0]==0&&pp.m_postTextures[1]==0,"disabled target allocation");
 Ge::EnvironmentScreenState env;env.heat=.5f;pp.setEnvironmentEffects(env);draw();
 require(calls.size()==2&&calls[0].dst==pp.m_postFrameBuffers[0]&&calls[1].dst==0&&calls[1].src==pp.m_postTextures[0],"one pass routing");
 require(textureWidth(pp.m_postTextures[0])==64,"first target allocation");require(textureWidth(pp.m_postTextures[1])==0,"single pass allocated second target storage");
 const auto first=pp.m_postTextures[0];
 graphics.str_width=37;graphics.str_height=21;pp.resize(37,21);draw();require(textureWidth(first)==37&&textureWidth(pp.m_postTextures[1])==0,"single-pass resize allocated second storage");
 pp.m_setting->depth_of_field=true;draw();require(pp.m_postTextures[0]==first,"second-target upgrade replaced first");
 require(calls.size()==3&&std::string(calls[1].name)=="dof"&&calls[1].src==first&&calls[1].dst==pp.m_postFrameBuffers[1]&&calls[2].src==pp.m_postTextures[1]&&calls[2].dst==0,"two pass routing");
 require(textureWidth(pp.m_postTextures[1])==37,"second target allocation");const auto second=pp.m_postTextures[1];
 pp.m_setting->color_tint=true;draw();require(calls.size()==4&&calls[2].dst==pp.m_postFrameBuffers[0]&&calls[2].src==second&&calls[3].src==first&&calls[3].dst==0,"three pass ping-pong");
 pp.m_setting->depth_of_field=false;pp.m_setting->color_tint=false;draw();require(calls.size()==2&&calls[1].src==first&&pp.m_postTextures[1]==second,"two to one routing or unnecessary discard");
 graphics.str_width=91;graphics.str_height=47;pp.resize(91,47);draw();require(textureWidth(first)==91,"first-target resize");
 pp.m_setting->color_tint=true;draw();require(textureWidth(second)==91&&calls.size()==3&&calls[1].dst==pp.m_postFrameBuffers[1]&&calls[2].src==second,"reuse second target after single-pass resize");
 pp.clearEnvironmentEffects();draw();require(calls.size()==2&&calls[1].dst==0&&calls[1].src==first,"tint-only routing");
 pp.m_setting->color_tint=false;draw();require(calls.size()==1&&calls[0].dst==0,"return to disabled routing");
 graphics.str_width=0;calls.clear();pp.compute(0,0,0,&quad);require(calls.empty(),"zero-sized compute issued pass");
 pp.release();require(!glIsTexture(first)&&!glIsTexture(second),"post targets leaked");require(pp.m_postTextures[0]==0&&pp.m_postTextures[1]==0,"release did not clear handles");
 require(glGetError()==GL_NO_ERROR,"OpenGL error");
 std::cout<<"PASS production PostProcess owner: inactive no allocation, lazy 1->2 target storage, 0/1/2/3 pass routing without feedback, 2->1 reuse, odd resize/re-enable, release"<<std::endl;
 return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
}
'''

def main():
    with tempfile.TemporaryDirectory(prefix='endurance-post-owner-') as temporary:
        directory=Path(temporary)
        services=dict(BASE)
        services['PointeurClass.hpp']='''#pragma once
    namespace Ge { class GraphiquePipelineManager; }
    class SettingManager { public: float gamma=2.2f;float* getGammaAddr(){return &gamma;} };
    struct ptrClass { Ge::GraphiquePipelineManager* graphiquePipelineManager=nullptr; SettingManager* settingManager=nullptr; };
    '''
        services['Debug.hpp']=services['Debug.hpp'].replace('float*,float)', 'float*,float=0.f)').replace(' inline void TreePop()', ' inline void TextUnformatted(const char*) {}\n inline void TreePop()')
        for name,source in services.items():(directory/name).write_text(source)
        harness=directory/'post_owner.cpp';harness.write_text(HARNESS)
        library=directory/'post_owner.so'
        command=[os.environ.get('CXX','c++'),'-std=c++17','-O1','-fPIC','-shared','-DGLEW_NO_GLU','-pthread','-fvisibility=hidden']
        for path in (directory,ROOT/'ANTAGONIZ/Header/Rendering/Shader',ROOT/'ANTAGONIZ/Header/Interface',ROOT/'ANTAGONIZ/Header/Struct',ROOT/'ThirdParty',ROOT/'ThirdParty/glm'):
            command+=['-I',str(path)]
        command += [str(harness),str(ROOT/'ANTAGONIZ/Source/Rendering/Shader/PostProcess.cpp'),'-l:libGLEW.so.2.2','-l:libGL.so.1','-l:libEGL.so.1','-ldl','-Wl,-z,defs','-o',str(library)]
        subprocess.run(command,check=True)
        with AtmosphereGL() as gl:
            api=C.CDLL(str(library));api.run_post_check.restype=C.c_int
            result=api.run_post_check()
            gl.check('post owner integration')
            if result:raise SystemExit(result)

if __name__=='__main__':main()
