#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "glcore.hpp"
#include "EnduranceBiomeParticles.hpp"
#include "ParticleSystem.hpp"
#include "RenderingEngine.hpp"
#include "FrameBuffer.hpp"
#include "Atmosphere.hpp"
#include "GraphicsDataMisc.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

namespace
{
    using namespace EnduranceEnvironment;
    constexpr unsigned FamilyCount = static_cast<unsigned>(ParticleShape::Count);
    constexpr char ShaderRoot[] = "../ENDURANCE/Ressources/Shader/Environment/";
    struct RuntimeSettings
    {
        Ge::ParticleBaseGroup group;
        // Eight tightly packed float words after the existing 93-word ABI.
        float strength = 0, emission = 0, aspect = 1, variant = 0;
        float turbulence = 0, turbulenceScale = .12f, pulse = 0, seed = 0;
    };
    static_assert(sizeof(Ge::ParticleBaseGroup) == 93 * sizeof(float), "Particle settings ABI changed");
    static_assert(offsetof(RuntimeSettings, strength) == 93 * sizeof(float), "Particle appearance ABI");
    static_assert(sizeof(RuntimeSettings) == 101 * sizeof(float), "Particle runtime settings ABI");

    unsigned family(ParticleShape shape)
    {
        return static_cast<unsigned>(shape);
    }
    bool finiteFloat(float value)
    {
        // Release enables fast-math, which may erase std::isfinite checks.
        std::uint32_t bits;
        static_assert(sizeof bits == sizeof value, "Expected IEEE float word");
        std::memcpy(&bits,&value,sizeof bits);
        return (bits & 0x7f800000u) != 0x7f800000u;
    }
    float finiteClamped(float value, float low, float high, float fallback)
    { return finiteFloat(value) ? (std::max)(low, (std::min)(value, high)) : fallback; }
    bool finiteVector(const glm::vec3& v)
    { return finiteFloat(v.x) && finiteFloat(v.y) && finiteFloat(v.z); }

    struct IndexedBinding
    {
        GLint buffer=0;
        GLint64 start=0,size=0;
        void capture(GLenum binding, GLenum startName, GLenum sizeName, GLuint index)
        {
            glGetIntegeri_v(binding,index,&buffer);
            glGetInteger64i_v(startName,index,&start);
            glGetInteger64i_v(sizeName,index,&size);
        }
        void restore(GLenum target, GLuint index) const
        {
            if(buffer && size>0)
                glBindBufferRange(target,index,static_cast<GLuint>(buffer),static_cast<GLintptr>(start),static_cast<GLsizeiptr>(size));
            else
                glBindBufferBase(target,index,static_cast<GLuint>(buffer));
        }
    };

    // Preserve only state touched by the medium binder and ParticleSystem.
    // Transparency callbacks have no ordering contract; this avoids relying
    // on a water/material/particle callback happening to run before this one.
    struct RenderState
    {
        GLint program=0,vao=0,activeTexture=0,uniformBuffer=0,storageBuffer=0;
        std::array<IndexedBinding,3> storage{};
        IndexedBinding medium;
        std::array<GLint,4> textures{},samplers{};
        GLboolean cull=GL_FALSE;
        const std::array<GLuint,4> units{{0,10,12,13}};
        const std::array<GLenum,4> targets{{GL_TEXTURE_2D,GL_TEXTURE_2D,GL_TEXTURE_3D,GL_TEXTURE_3D}};
        RenderState()
        {
            glGetIntegerv(GL_CURRENT_PROGRAM,&program);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
            glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);
            glGetIntegerv(GL_UNIFORM_BUFFER_BINDING,&uniformBuffer);
            glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&storageBuffer);
            medium.capture(GL_UNIFORM_BUFFER_BINDING,GL_UNIFORM_BUFFER_START,GL_UNIFORM_BUFFER_SIZE,6);
            for(unsigned i=0;i<storage.size();++i)
                storage[i].capture(GL_SHADER_STORAGE_BUFFER_BINDING,GL_SHADER_STORAGE_BUFFER_START,GL_SHADER_STORAGE_BUFFER_SIZE,6+i);
            for(unsigned i=0;i<units.size();++i)
            {
                glActiveTexture(GL_TEXTURE0+units[i]);
                glGetIntegerv(i<2?GL_TEXTURE_BINDING_2D:GL_TEXTURE_BINDING_3D,&textures[i]);
                glGetIntegeri_v(GL_SAMPLER_BINDING,units[i],&samplers[i]);
            }
            cull=glIsEnabled(GL_CULL_FACE);
        }
        ~RenderState()
        {
            for(unsigned i=0;i<units.size();++i)
            {
                glActiveTexture(GL_TEXTURE0+units[i]);
                glBindTexture(targets[i],static_cast<GLuint>(textures[i]));
                glBindSampler(units[i],static_cast<GLuint>(samplers[i]));
            }
            glActiveTexture(static_cast<GLenum>(activeTexture));
            medium.restore(GL_UNIFORM_BUFFER,6);
            for(unsigned i=0;i<storage.size();++i) storage[i].restore(GL_SHADER_STORAGE_BUFFER,6+i);
            glBindBuffer(GL_UNIFORM_BUFFER,static_cast<GLuint>(uniformBuffer));
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,static_cast<GLuint>(storageBuffer));
            if(cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
            glBindVertexArray(static_cast<GLuint>(vao));
            glUseProgram(static_cast<GLuint>(program));
            // Depth-write deliberately follows ParticleSystem's shared
            // currentDepthWrite protocol; the engine restores it after callbacks.
        }
    };
    struct UpdateState
    {
        GLint program=0,storage=0,copyRead=0,copyWrite=0;
        IndexedBinding bindingZero;
        UpdateState()
        {
            glGetIntegerv(GL_CURRENT_PROGRAM,&program);
            glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&storage);
            bindingZero.capture(GL_SHADER_STORAGE_BUFFER_BINDING,GL_SHADER_STORAGE_BUFFER_START,GL_SHADER_STORAGE_BUFFER_SIZE,0);
            glGetIntegerv(GL_COPY_READ_BUFFER_BINDING,&copyRead);
            glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING,&copyWrite);
        }
        ~UpdateState()
        {
            bindingZero.restore(GL_SHADER_STORAGE_BUFFER,0);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER,static_cast<GLuint>(storage));
            glBindBuffer(GL_COPY_READ_BUFFER,static_cast<GLuint>(copyRead));
            glBindBuffer(GL_COPY_WRITE_BUFFER,static_cast<GLuint>(copyWrite));
            glUseProgram(static_cast<GLuint>(program));
        }
    };
}

struct EnduranceBiomeParticles::Impl
{
    const ptrClass* engine = nullptr;
    std::unique_ptr<Ge::ParticleSystem> system;
    std::unique_ptr<Ge::ComputeShader> simulation, initializer, rebase;
    GLint rebaseDelta = -1;
    std::array<Ge::GraphiquePipeline*, FamilyCount> pipelines{};
    std::array<GLint,FamilyCount> mediumLocations{},resolutionLocations{};
    struct Group
    {
        std::size_t id = 0, count = 0, requestedCount = 0;
        RuntimeSettings settings;
    };
    std::array<Group, EffectCount> groups{};
    std::size_t particles = 0, liveGroups = 0;
    bool registered = false;
    std::string error;

    void registerCallback(bool needed)
    {
        if(registered == needed || !engine || !engine->renderingEngine) return;
        if(needed)
            engine->renderingEngine->addTransparencyCallback(this,
                [this](bool* depthWrite) { render(depthWrite); });
        else
            engine->renderingEngine->removeTransparencyCallback(this);
        registered = needed;
    }
    void render(bool* depthWrite)
    {
        if(!system || !liveGroups || !engine || !engine->renderingEngine) return;
        const RenderState state;
        Ge::Atmosphere* atmosphere = engine->renderingEngine->getAtmosphere();
        const auto* framebuffer = engine->renderingEngine->getFrameBuffer();
        const float invWidth = 1.0f / float((std::max)(engine->gdm->str_width, 1u));
        const float invHeight = 1.0f / float((std::max)(engine->gdm->str_height, 1u));
        std::array<bool, FamilyCount> bound{};
        const auto& definitions = effectDefinitions();
        for(std::size_t i=0; i<groups.size(); ++i)
        {
            if(!groups[i].id) continue;
            const unsigned f = family(definitions[i].particles.shape);
            if(bound[f]) continue;
            bound[f] = true;
            const auto* pipeline = pipelines[f];
            const unsigned program = pipeline->getProgram();
            if(atmosphere) atmosphere->bindParticleMedium(program,mediumLocations[f]);
            else glProgramUniform1i(program,mediumLocations[f],0);
            glProgramUniform2f(program,resolutionLocations[f],invWidth,invHeight);
        }
        // Compact signed view-depth is not the attached depth image. Sampling
        // it is safe during transparency and needs no copy/readback or new RT.
        glBindTextureUnit(0, framebuffer->getPosition());
        glBindSampler(0,0);
        system->render(depthWrite);
    }
    void remove(std::size_t i)
    {
        auto& group=groups[i];
        if(!group.id) return;
        system->removeGroup(group.id);
        particles-=group.count; --liveGroups;
        group={};
    }
    void clear()
    {
        registerCallback(false);
        // Delete groups/allocator before their shaders or engine services.
        system.reset();
        rebase.reset(); initializer.reset(); simulation.reset();
        for(auto*& pipeline:pipelines)
        {
            if(pipeline && engine && engine->graphiquePipelineManager)
                engine->graphiquePipelineManager->destroyPipeline(pipeline);
            pipeline=nullptr;
        }
        groups={}; particles=liveGroups=0; engine=nullptr;
    }
};

EnduranceBiomeParticles::EnduranceBiomeParticles(): m(std::make_unique<Impl>()) {}
EnduranceBiomeParticles::~EnduranceBiomeParticles() { release(); }
bool EnduranceBiomeParticles::initialize(const ptrClass* engine)
{
    release(); m->error.clear();
    if(!engine || !engine->renderingEngine || !engine->graphiquePipelineManager || !engine->gdm)
    { m->error="Biome particles require an initialized rendering engine."; return false; }
    m->engine=engine;
    Ge::ShaderCacheOptions cache;
    cache.enabled=true;
    m->simulation=std::make_unique<Ge::ComputeShader>(std::string(ShaderRoot)+"particle_sim.C.glsl", "", "Biome particle simulation", cache);
    m->initializer=std::make_unique<Ge::ComputeShader>(std::string(ShaderRoot)+"particle_init.C.glsl", "", "Biome particle initialization", cache);
    m->rebase=std::make_unique<Ge::ComputeShader>(std::string(ShaderRoot)+"particle_rebase.C.glsl", "", "Biome particle rebase", cache);
    if(!m->simulation->isValid() || !m->initializer->isValid() || !m->rebase->isValid())
    {
        m->error="Biome particle volume shaders failed to compile: "+m->simulation->getLastError()
            +m->initializer->getLastError()+m->rebase->getLastError();
        m->clear(); return false;
    }
    m->rebaseDelta=glGetUniformLocation(m->rebase->getProgram(),"uAnchorDelta");
    for(unsigned f=0; f<FamilyCount; ++f)
    {
        m->pipelines[f]=engine->graphiquePipelineManager->createPipeline(
            std::string(ShaderRoot)+"particle.fs.glsl", std::string(ShaderRoot)+"particle.vs.glsl",
            false, true, true, 2, "#define GE_BIOME_PARTICLE_KIND "+std::to_string(f)+"\n", cache);
        if(!m->pipelines[f] || !m->pipelines[f]->isValid())
        {
            m->error=m->pipelines[f] ? m->pipelines[f]->getLastError() : "Unable to create biome particle pipeline.";
            m->clear(); return false;
        }
        const unsigned program=m->pipelines[f]->getProgram();
        m->mediumLocations[f]=glGetUniformLocation(program,"uAtmosphereActive");
        m->resolutionLocations[f]=glGetUniformLocation(program,"uEnvironmentInvResolution");
    }
    m->system=std::make_unique<Ge::ParticleSystem>();
    // An empty ParticleSystem normally registers a transparent pass. This
    // adapter registers its wrapper only while at least one effect is alive.
    engine->renderingEngine->removeTransparencyCallback(m->system.get());
    if(!m->system->isValid())
    {
        m->error="Engine particle simulation/init shader failed to compile.";
        m->clear(); return false;
    }
    return true;
}

void EnduranceBiomeParticles::update(const std::array<float, EffectCount>& strengths,
    const glm::vec3& cameraPosition, const glm::vec3& cameraForward,
    const glm::vec2& wind, float dt, bool teleport, float density)
{
    if(!m->system) return;
    std::optional<UpdateState> state;
    const auto captureState=[&state]() { if(!state) state.emplace(); };
    (void)dt; // Existing ParticleSystem dispatch supplies its bounded delta time.
    (void)cameraForward; // All recipes are environmental, not character/mouth effects.
    const auto& definitions=effectDefinitions();
    const bool validPosition=finiteVector(cameraPosition);
    density=finiteClamped(density,0.0f,4.0f,1.0f);
    const auto requestedCount=[density](const ParticlePreset& preset) -> std::size_t {
        if(density<=0 || !preset.count) return 0;
        return (std::max)(std::size_t(1),std::size_t(float(preset.count)*density+.5f));
    };
    // Release inactive slots first, before admitting the new dominant biome.
    // Density is an authoring/quality control: resizing happens only when it
    // changes the requested count, never while an effect merely fades.
    for(std::size_t i=0; i<EffectCount; ++i)
        if(!validPosition || !finiteFloat(strengths[i]) || strengths[i]<=0.0f
            || !requestedCount(definitions[i].particles)
            || (m->groups[i].id && m->groups[i].requestedCount!=requestedCount(definitions[i].particles)))
        {
            if(m->groups[i].id) captureState();
            m->remove(i);
        }
    if(validPosition)
    for(std::size_t i=0; i<EffectCount; ++i)
    {
        const auto& preset=definitions[i].particles;
        const float strength=finiteClamped(strengths[i],0.0f,1.0f,0.0f);
        const auto requested=requestedCount(preset);
        if(strength<=0 || !requested) continue;
        auto& group=m->groups[i];
        if(group.id && group.count<requested && MaxLiveParticles-m->particles>=requested-group.count)
        {
            // A transition may temporarily consume the budget with outgoing
            // effects. Promote a truncated group once its full count fits;
            // stable saturated budgets never repeatedly rebuild this group.
            captureState();
            m->remove(i);
        }
        RuntimeSettings next;
        next.group=preset.group;
        next.group.emitPosition += cameraPosition;
        // A stable local anchor retains slow drift precision at world distances
        // of tens of kilometers. Camera movement alone never drags particles.
        next.group.target=group.id && !teleport ? group.settings.group.target : cameraPosition;
        const glm::vec3 relativeCamera=cameraPosition-next.group.target;
        if(group.id && !teleport && glm::dot(relativeCamera,relativeCamera)>1024.0f*1024.0f)
        {
            const glm::vec3 delta=next.group.target-cameraPosition;
            captureState();
            glProgramUniform3fv(m->rebase->getProgram(),m->rebaseDelta,1,&delta.x);
            m->system->dispatchGroup(group.id,m->rebase.get());
            next.group.target=cameraPosition;
        }
        // Game-specific simulation interprets externalForce as air velocity;
        // unlike the generic force integrator it cannot accelerate wind forever.
        next.group.externalForce += glm::vec3(
            finiteClamped(wind.x,-100,100,0),0,finiteClamped(wind.y,-100,100,0))*preset.windScale;
        next.group.flags &= ~Ge::AttachToEmitPoint;
        next.strength=strength; next.emission=preset.emission;
        next.aspect=preset.aspect; next.variant=float(static_cast<unsigned>(preset.shape));
        next.turbulence=preset.turbulence; next.turbulenceScale=preset.turbulenceScale;
        next.pulse=preset.pulse; next.seed=float(i)*13.371f;
        if(!group.id)
        {
            if(m->liveGroups>=MaxLiveGroups || m->particles>=MaxLiveParticles) continue;
            group.count=(std::min)(requested,MaxLiveParticles-m->particles);
            if(!group.count) continue;
            captureState();
            group.id=m->system->createGroup(&next,sizeof next,group.count,
                m->engine->gdm->str_default_texture,m->simulation.get(),m->pipelines[family(preset.shape)],nullptr,m->initializer.get());
            group.settings=next;
            group.requestedCount=requested;
            m->particles+=group.count; ++m->liveGroups;
        }
        else
        {
            // No per-frame allocation or batching rebuild for a moving camera
            // or a transition fade: only the small settings block is uploaded.
            if(std::memcmp(&group.settings,&next,sizeof next)!=0)
            {
                captureState();
                m->system->updateGroup(group.id,&next,sizeof next);
                group.settings=next;
            }
            if(teleport) { captureState(); m->system->restartGroup(group.id); }
        }
    }
    m->registerCallback(m->liveGroups!=0);
}
void EnduranceBiomeParticles::release() { if(m) m->clear(); }
std::size_t EnduranceBiomeParticles::liveParticleCount() const { return m->particles; }
std::size_t EnduranceBiomeParticles::groupCount() const { return m->liveGroups; }
const std::string& EnduranceBiomeParticles::error() const { return m->error; }
