#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "glcore.hpp"
#include "EnduranceWindStreaks.hpp"
#include "PointeurClass.hpp"
#include "GraphiquePipeline.hpp"
#include "RenderingEngine.hpp"
#include "FrameBuffer.hpp"
#include "Atmosphere.hpp"
#include "GraphicsDataMisc.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace
{
    constexpr unsigned Segments = 24;
    constexpr float Radius = 80.0f;
    constexpr float Pi = 3.14159265358979323846f;
    constexpr double Tau = 6.283185307179586476925286766559;
    constexpr char ShaderRoot[] = "../ENDURANCE/Ressources/Shader/Environment/";

    bool finiteFloat(float value)
    {
        // The engine's Release build enables fast-math.
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        return (bits & 0x7f800000u) != 0x7f800000u;
    }
    float safeRange(float value, float low, float high, float fallback)
    { return finiteFloat(value) ? (std::max)(low, (std::min)(value, high)) : fallback; }
    bool finitePosition(const glm::vec3& p)
    { return finiteFloat(p.x) && finiteFloat(p.y) && finiteFloat(p.z); }
    float smooth(float low, float high, float value)
    {
        const float t = std::clamp((value-low)/(high-low), 0.0f, 1.0f);
        return t*t*(3.0f-2.0f*t);
    }

    struct Binding
    {
        GLint buffer = 0;
        GLint64 start = 0, size = 0;
        void capture(GLenum target, GLenum startName, GLenum sizeName, unsigned index)
        {
            glGetIntegeri_v(target, index, &buffer);
            glGetInteger64i_v(startName, index, &start);
            glGetInteger64i_v(sizeName, index, &size);
        }
        void restore(GLenum target, unsigned index) const
        {
            if (buffer && size > 0)
                glBindBufferRange(target, index, static_cast<GLuint>(buffer),
                    static_cast<GLintptr>(start), static_cast<GLsizeiptr>(size));
            else glBindBufferBase(target, index, static_cast<GLuint>(buffer));
        }
    };
    struct RenderState
    {
        GLint program = 0, vao = 0, activeTexture = 0, storage = 0, uniform = 0;
        GLint srcRGB = 0, dstRGB = 0, srcAlpha = 0, dstAlpha = 0, eqRGB = 0, eqAlpha = 0;
        GLboolean cull = GL_FALSE, blend = GL_FALSE, depth = GL_FALSE;
        Binding streaks, medium;
        const std::array<unsigned, 3> units{{0, 12, 13}};
        const std::array<GLenum, 3> targets{{GL_TEXTURE_2D, GL_TEXTURE_3D, GL_TEXTURE_3D}};
        std::array<GLint, 3> textures{}, samplers{};
        RenderState()
        {
            glGetIntegerv(GL_CURRENT_PROGRAM, &program);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
            glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &storage);
            glGetIntegerv(GL_UNIFORM_BUFFER_BINDING, &uniform);
            streaks.capture(GL_SHADER_STORAGE_BUFFER_BINDING, GL_SHADER_STORAGE_BUFFER_START, GL_SHADER_STORAGE_BUFFER_SIZE, 6);
            medium.capture(GL_UNIFORM_BUFFER_BINDING, GL_UNIFORM_BUFFER_START, GL_UNIFORM_BUFFER_SIZE, 6);
            glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB); glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha); glGetIntegerv(GL_BLEND_DST_ALPHA, &dstAlpha);
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &eqRGB); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqAlpha);
            cull = glIsEnabled(GL_CULL_FACE); blend = glIsEnabled(GL_BLEND); depth = glIsEnabled(GL_DEPTH_TEST);
            for (unsigned i = 0; i < units.size(); ++i)
            {
                glActiveTexture(GL_TEXTURE0+units[i]);
                glGetIntegerv(i == 0 ? GL_TEXTURE_BINDING_2D : GL_TEXTURE_BINDING_3D, &textures[i]);
                glGetIntegeri_v(GL_SAMPLER_BINDING, units[i], &samplers[i]);
            }
        }
        ~RenderState()
        {
            for (unsigned i = 0; i < units.size(); ++i)
            {
                glActiveTexture(GL_TEXTURE0+units[i]);
                glBindTexture(targets[i], static_cast<GLuint>(textures[i]));
                glBindSampler(units[i], static_cast<GLuint>(samplers[i]));
            }
            glActiveTexture(static_cast<GLenum>(activeTexture));
            streaks.restore(GL_SHADER_STORAGE_BUFFER, 6); medium.restore(GL_UNIFORM_BUFFER, 6);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(storage));
            glBindBuffer(GL_UNIFORM_BUFFER, static_cast<GLuint>(uniform));
            glBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
            glBlendEquationSeparate(eqRGB, eqAlpha);
            if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
            if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            glBindVertexArray(static_cast<GLuint>(vao)); glUseProgram(static_cast<GLuint>(program));
            // Depth writes follow the renderer's bool* protocol, as particles do.
        }
    };
    struct GpuStreak
    {
        glm::vec4 centerLength, directionWidth, curve, appearance;
    };
    static_assert(sizeof(GpuStreak) == 64, "Wind ribbon std430 layout");
}

struct EnduranceWindStreaks::Impl
{
    struct Streak
    {
        glm::dvec3 center{};
        glm::vec3 direction{1, 0, 0};
        float age = 0, lifetime = 0, length = 0, width = 0;
        float lateral = 0, vertical = 0, cycles = 0;
        double phase = 0;
        bool alive = false;
    };
    const ptrClass* engine = nullptr;
    Ge::GraphiquePipeline* pipeline = nullptr;
    GLuint vao = 0, buffer = 0;
    GLint anchorLocation = -1, resolutionLocation = -1, atmosphereLocation = -1;
    std::array<Streak, MaxStreaks> streaks{};
    std::array<GpuStreak, MaxStreaks> upload{};
    glm::dvec3 anchor{};
    std::uint32_t rng = 0xb38c6f25u;
    std::size_t count = 0;
    bool registered = false, anchored = false;
    std::string error;

    float random()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return float(rng >> 8) * (1.0f / 16777216.0f);
    }
    void spawn(Streak& s, const glm::vec3& camera, const glm::vec3& direction)
    {
        const float angle = random()*2.0f*Pi;
        const float radius = std::sqrt(0.025f+0.975f*random())*Radius*.8f;
        s.center = glm::dvec3(camera)+glm::dvec3(std::cos(angle)*radius, -3.0f+15.0f*random(), std::sin(angle)*radius);
        s.direction = direction;
        s.age = 0; s.lifetime = 6.0f+4.0f*random();
        s.length = 10.0f+14.0f*random(); s.width = .025f+.04f*random();
        s.lateral = .25f+.65f*random(); s.vertical = .12f+.25f*random();
        s.cycles = .6f+.65f*random(); s.phase = random()*Tau;
        s.alive = true;
    }
    void registerCallback(bool needed)
    {
        if (registered == needed || !engine || !engine->renderingEngine) return;
        if (needed) engine->renderingEngine->addTransparencyCallback(this,
            [this](bool* depthWrite) { render(depthWrite); });
        else engine->renderingEngine->removeTransparencyCallback(this);
        registered = needed;
    }
    void render(bool* depthWrite)
    {
        if (!count || !pipeline || !engine || !engine->renderingEngine) return;
        const RenderState state;
        const GLuint program = pipeline->getProgram();
        auto* atmosphere = engine->renderingEngine->getAtmosphere();
        if (atmosphere) atmosphere->bindParticleMedium(program, atmosphereLocation);
        else glProgramUniform1i(program, atmosphereLocation, 0);
        glProgramUniform2f(program, resolutionLocation,
            1.0f/float((std::max)(engine->gdm->str_width,1u)),
            1.0f/float((std::max)(engine->gdm->str_height,1u)));
        const glm::vec3 origin(anchor);
        glProgramUniform3fv(program, anchorLocation, 1, &origin.x);
        glBindTextureUnit(0, engine->renderingEngine->getFrameBuffer()->getPosition());
        glBindSampler(0, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, buffer);
        glBindVertexArray(vao); glUseProgram(program);
        glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glEnable(GL_DEPTH_TEST);
        glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (*depthWrite) { *depthWrite = false; glDepthMask(GL_FALSE); }
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 2*(Segments+1), static_cast<GLsizei>(count));
    }
    void clear()
    {
        registerCallback(false);
        if (buffer) glDeleteBuffers(1, &buffer);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (pipeline && engine && engine->graphiquePipelineManager)
            engine->graphiquePipelineManager->destroyPipeline(pipeline);
        pipeline = nullptr; vao = buffer = 0; engine = nullptr;
        count = 0; anchored = false; streaks = {}; rng = 0xb38c6f25u;
    }
};

EnduranceWindStreaks::EnduranceWindStreaks() : m(std::make_unique<Impl>()) {}
EnduranceWindStreaks::~EnduranceWindStreaks() { release(); }
bool EnduranceWindStreaks::initialize(const ptrClass* engine)
{
    release(); m->error.clear();
    if (!engine || !engine->renderingEngine || !engine->graphiquePipelineManager || !engine->gdm)
    { m->error = "Wind streaks require an initialized rendering engine."; return false; }
    m->engine = engine;
    Ge::ShaderCacheOptions cache; cache.enabled = true;
    m->pipeline = engine->graphiquePipelineManager->createPipeline(
        std::string(ShaderRoot)+"wind_streaks.fs.glsl", std::string(ShaderRoot)+"wind_streaks.vs.glsl",
        false, true, true, 2, "#define ENDURANCE_WIND_SEGMENTS "+std::to_string(Segments)+"\n", cache);
    if (!m->pipeline || !m->pipeline->isValid())
    {
        m->error = m->pipeline ? m->pipeline->getLastError() : "Unable to create wind streak pipeline.";
        m->clear(); return false;
    }
    const GLuint program = m->pipeline->getProgram();
    m->anchorLocation = glGetUniformLocation(program, "uWindAnchor");
    m->resolutionLocation = glGetUniformLocation(program, "uEnvironmentInvResolution");
    m->atmosphereLocation = glGetUniformLocation(program, "uAtmosphereActive");
    glCreateVertexArrays(1, &m->vao); glCreateBuffers(1, &m->buffer);
    if (!m->vao || !m->buffer)
    { m->error = "Unable to allocate wind streak resources."; m->clear(); return false; }
    glNamedBufferData(m->buffer, sizeof(m->upload), nullptr, GL_DYNAMIC_DRAW);
    return true;
}

void EnduranceWindStreaks::update(float strength, const glm::vec3& cameraPosition,
    const glm::vec2& wind, float dt, bool teleport, float density)
{
    if (!m->pipeline) return;
    strength = safeRange(strength, 0.0f, 1.0f, 0.0f);
    density = safeRange(density, 0.0f, 4.0f, 0.0f);
    dt = safeRange(dt, 0.0f, 1.0f, 0.0f);
    const std::size_t requested = density > 0 ? (std::max)(std::size_t(1), std::size_t(32.0f*density+.5f)) : 0;
    if (strength <= 0 || !requested || !finitePosition(cameraPosition))
    {
        m->count = 0; m->streaks = {}; m->anchored = false;
        m->registerCallback(false); return;
    }
    if (teleport) { m->streaks = {}; m->anchored = false; }
    if (!m->anchored || glm::length(glm::dvec3(cameraPosition)-m->anchor) > 512.0)
    { m->anchor = glm::dvec3(cameraPosition); m->anchored = true; }
    const glm::vec3 velocity(safeRange(wind.x,-100.0f,100.0f,0.0f), 0,
        safeRange(wind.y,-100.0f,100.0f,0.0f));
    const float speed = glm::length(velocity);
    const glm::dvec3 displacement = glm::dvec3(velocity)*static_cast<double>(dt);
    const double travelled = glm::length(displacement);
    const glm::vec3 heading = speed > .001f ? velocity/speed : glm::vec3(1,0,0);
    const float turnBlend = 1.0f-std::exp(-2.0f*dt);
    m->count = 0;
    for (std::size_t i = 0; i < MaxStreaks; ++i)
    {
        auto& s = m->streaks[i];
        if (i >= requested) { s.alive = false; continue; }
        if (s.alive)
        {
            s.center += displacement;
            s.age += dt;
            // One wavelength of world-space travel advances one wave cycle.
            // Integrating displacement keeps gusts continuous and freezes the
            // curve in calm air; no unrelated wall-clock oscillator remains.
            s.phase = std::fmod(s.phase+travelled*(Tau*s.cycles/s.length), Tau);
            if (speed > .001f)
            {
                const glm::vec3 direction = glm::mix(s.direction, heading, turnBlend);
                s.direction = glm::dot(direction,direction) > 1e-8f ? glm::normalize(direction) : heading;
            }
        }
        glm::vec3 delta(s.center-glm::dvec3(cameraPosition));
        if (!s.alive || s.age >= s.lifetime || glm::dot(glm::vec2(delta.x,delta.z),glm::vec2(delta.x,delta.z)) > (Radius+16)*(Radius+16)
            || std::abs(delta.y) > 28.0f)
        { m->spawn(s, cameraPosition, heading); delta = glm::vec3(s.center-glm::dvec3(cameraPosition)); }
        const float radial = glm::length(glm::vec2(delta.x,delta.z));
        const float lifeFade = smooth(0,.6f,s.age)*(1.0f-smooth(s.lifetime-1.2f,s.lifetime,s.age));
        const float volumeFade = (1.0f-smooth(Radius*.75f,Radius,radial))*(1.0f-smooth(18,26,std::abs(delta.y)));
        auto& gpu = m->upload[m->count++];
        gpu.centerLength = glm::vec4(glm::vec3(s.center-m->anchor),s.length);
        gpu.directionWidth = glm::vec4(s.direction,s.width);
        gpu.curve = glm::vec4(s.lateral,s.vertical,s.cycles,static_cast<float>(s.phase));
        gpu.appearance = glm::vec4(.34f*strength*lifeFade*volumeFade*smooth(0,.8f,speed),.95f,.98f,1.0f);
    }
    glNamedBufferSubData(m->buffer, 0, static_cast<GLsizeiptr>(m->count*sizeof(GpuStreak)), m->upload.data());
    m->registerCallback(m->count != 0);
}

void EnduranceWindStreaks::release() { if (m) m->clear(); }
std::size_t EnduranceWindStreaks::liveCount() const { return m->count; }
const std::string& EnduranceWindStreaks::error() const { return m->error; }
