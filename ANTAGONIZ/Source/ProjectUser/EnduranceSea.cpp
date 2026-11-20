#include "glcore.hpp"
#include "EnduranceSea.hpp"
#include "PointeurClass.hpp"
#include "RenderingEngine.hpp"
#include "FrameBuffer.hpp"
#include "Atmosphere.hpp"
#include "ShaderCompilation.hpp"
#include <algorithm>
#include <limits>

namespace Endurance
{
    struct SeaRenderer::Impl
    {
        const ptrClass* engine = nullptr;
        unsigned program = 0, vao = 0, instances = 0;
        int levelLocation = -1, extentLocation = -1;
        std::size_t capacity = 0;
        std::uint64_t revision = 0;
        float level = 0, baseExtent = 0;
        bool hasRevision = false, revisionValid = false;
        Stats statistics;
        std::string failure;
    };

    SeaRenderer::SeaRenderer() : m(std::make_unique<Impl>()) {}
    SeaRenderer::~SeaRenderer() { shutdown(); }

    bool SeaRenderer::initialize(const ptrClass& engine)
    {
        shutdown();
        m->failure.clear();
        if (!engine.renderingEngine || !engine.renderingEngine->getFrameBuffer())
        {
            m->failure = "Sea requires the scene rendering engine and opaque framebuffer.";
            return false;
        }
        m->engine = &engine;
        const std::string path = "../ENDURANCE/Ressources/Shader/Water/sea.";
        m->program = Ge::buildShaderProgram({{GL_VERTEX_SHADER,path+"vs.glsl",{}},
            {GL_FRAGMENT_SHADER,path+"fs.glsl",{}}}, m->failure,
            "ENDURANCE sea", Ge::ShaderCacheOptions{true,{}});
        if (!m->program) { m->engine = nullptr; return false; }
        m->levelLocation = glGetUniformLocation(m->program,"seaLevel");
        m->extentLocation = glGetUniformLocation(m->program,"seaBaseExtent");
        glCreateVertexArrays(1,&m->vao);
        if (!m->vao)
        {
            m->failure = "Cannot allocate the sea vertex array.";
            shutdown();
            return false;
        }
        return true;
    }

    bool SeaRenderer::setTiles(const std::vector<ProceduralTerrain::SeaTile>& tiles,
        std::uint64_t revision, float seaLevel, float baseChunkExtent)
    {
        if (!m->program) return false;
        if (m->hasRevision && m->revision == revision && m->level == seaLevel &&
            m->baseExtent == baseChunkExtent) return m->revisionValid;
        // A failed publication never draws obsolete coarse tiles over the new
        // terrain. Remember it, so the same failing allocation is not retried
        // every frame; regeneration or the next publication permits a retry.
        m->hasRevision = true;
        m->revision = revision;
        m->level = seaLevel;
        m->baseExtent = baseChunkExtent;
        m->revisionValid = false;
        m->statistics.tiles = 0;
        m->failure.clear();
        if (!ProceduralTerrain::finiteFieldValue(seaLevel) ||
            !ProceduralTerrain::finiteFieldValue(baseChunkExtent) || baseChunkExtent <= 0.0f ||
            tiles.size() > static_cast<std::size_t>((std::numeric_limits<GLsizei>::max)()) ||
            tiles.size() > static_cast<std::size_t>((std::numeric_limits<GLsizeiptr>::max)()) / sizeof(ProceduralTerrain::SeaTile))
        {
            m->failure = "Invalid sea instance count or world scale.";
            return false;
        }
        for (const auto& tile : tiles)
        {
            if (tile.lod > 20 || tile.baseX < -(1<<24) || tile.baseX > (1<<24) ||
                tile.baseZ < -(1<<24) || tile.baseZ > (1<<24))
            {
                m->failure = "Invalid sea tile lattice coordinates.";
                return false;
            }
        }
        glProgramUniform1f(m->program,m->levelLocation,seaLevel);
        glProgramUniform1f(m->program,m->extentLocation,baseChunkExtent);
        if (!tiles.empty())
        {
            // GL errors are context-global. Attribute only errors produced by
            // this publication to its upload, not an earlier unrelated draw.
            // Otherwise a valid revision could remain hidden until it changes.
            while (glGetError() != GL_NO_ERROR) {}
            const auto bytes = tiles.size() * sizeof(ProceduralTerrain::SeaTile);
            if (tiles.size() > m->capacity)
            {
                // Allocate a replacement only when the existing capacity is
                // exhausted. Unchanged revisions never upload or allocate.
                const auto maximum = static_cast<std::size_t>((std::numeric_limits<GLsizeiptr>::max)()) / sizeof(ProceduralTerrain::SeaTile);
                const auto doubled = m->capacity <= maximum / 2 ? m->capacity * 2 : maximum;
                const auto capacity = (std::max)(tiles.size(),(std::max)(std::size_t{16},doubled));
                unsigned buffer = 0;
                glCreateBuffers(1,&buffer);
                if (buffer) glNamedBufferData(buffer,static_cast<GLsizeiptr>(capacity*sizeof(ProceduralTerrain::SeaTile)),nullptr,GL_STREAM_DRAW);
                if (!buffer || glGetError() != GL_NO_ERROR)
                {
                    if (buffer) glDeleteBuffers(1,&buffer);
                    m->failure = "Cannot allocate the sea instance buffer.";
                    return false;
                }
                if (m->instances) glDeleteBuffers(1,&m->instances);
                m->instances = buffer;
                m->capacity = capacity;
                m->statistics.allocatedBytes = capacity * sizeof(ProceduralTerrain::SeaTile);
            }
            else
            {
                // The previous frame can still read this SSBO. Orphan its
                // storage only on publication so the upload need not overwrite
                // an in-flight draw's backing store. Object/capacity are kept;
                // no fences, explicit waits or idle-frame allocations are added.
                glNamedBufferData(m->instances,
                    static_cast<GLsizeiptr>(m->capacity*sizeof(ProceduralTerrain::SeaTile)),
                    nullptr,GL_STREAM_DRAW);
                if (glGetError() != GL_NO_ERROR)
                {
                    m->failure = "Cannot refresh the published sea instance buffer.";
                    return false;
                }
            }
            glNamedBufferSubData(m->instances,0,static_cast<GLsizeiptr>(bytes),tiles.data());
            if (glGetError() != GL_NO_ERROR)
            {
                m->failure = "Cannot upload the published sea tiles.";
                return false;
            }
            ++m->statistics.uploads;
            m->statistics.uploadBytes += bytes;
        }
        m->statistics.tiles = static_cast<std::uint32_t>(tiles.size());
        m->revisionValid = true;
        return true;
    }

    void SeaRenderer::draw(bool* currentDepthWrite)
    {
        if (!m->program || !m->instances || !m->statistics.tiles || !m->engine) return;
        const auto* framebuffer = m->engine->renderingEngine->getFrameBuffer();
        if (!framebuffer || !framebuffer->getPosition()) return;
        // The engine owns shared lighting bindings. Preserve only the state
        // this dedicated draw changes; no FBO copy or per-frame buffer rebuild.
        GLint previousProgram=0, previousVao=0, previousActive=0, previousPosition=0, previousInstances=0, previousStorage=0;
        GLint64 instanceOffset=0, instanceSize=0;
        glGetIntegerv(GL_CURRENT_PROGRAM,&previousProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&previousVao);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&previousActive);
        glActiveTexture(GL_TEXTURE4);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&previousPosition);
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING,&previousStorage);
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,8,&previousInstances);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,8,&instanceOffset);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,8,&instanceSize);
        const bool wasCulling = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
        GLboolean depthWrite = GL_TRUE;
        if (currentDepthWrite) depthWrite = *currentDepthWrite ? GL_TRUE : GL_FALSE;
        else glGetBooleanv(GL_DEPTH_WRITEMASK,&depthWrite);

        if (auto* atmosphere = m->engine->renderingEngine->getAtmosphere())
            atmosphere->bindLighting(m->program,true);
        else
        {
            glProgramUniform1i(m->program,glGetUniformLocation(m->program,"uAtmosphereActive"),0);
            glProgramUniform1i(m->program,glGetUniformLocation(m->program,"uAtmosphereIblActive"),0);
        }
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D,framebuffer->getPosition());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,8,m->instances);
        glUseProgram(m->program);
        glBindVertexArray(m->vao);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        if (currentDepthWrite) *currentDepthWrite = false;
        glDrawArraysInstanced(GL_TRIANGLES,0,6,static_cast<GLsizei>(m->statistics.tiles));
        ++m->statistics.draws;

        glDepthMask(depthWrite);
        if (currentDepthWrite) *currentDepthWrite = depthWrite == GL_TRUE;
        if (wasCulling) glEnable(GL_CULL_FACE);
        glBindVertexArray(static_cast<unsigned>(previousVao));
        glUseProgram(static_cast<unsigned>(previousProgram));
        if (previousInstances && instanceSize > 0)
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER,8,static_cast<unsigned>(previousInstances),instanceOffset,instanceSize);
        else glBindBufferBase(GL_SHADER_STORAGE_BUFFER,8,static_cast<unsigned>(previousInstances));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,static_cast<unsigned>(previousStorage));
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D,static_cast<unsigned>(previousPosition));
        glActiveTexture(static_cast<unsigned>(previousActive));
    }

    void SeaRenderer::shutdown() noexcept
    {
        if (!m) return;
        if (m->instances) glDeleteBuffers(1,&m->instances);
        if (m->vao) glDeleteVertexArrays(1,&m->vao);
        if (m->program) glDeleteProgram(m->program);
        m->instances=m->vao=m->program=0;
        m->engine=nullptr;
        m->capacity=0;
        m->hasRevision=m->revisionValid=false;
        m->statistics={};
    }
    const std::string& SeaRenderer::error() const noexcept { return m->failure; }
    const SeaRenderer::Stats& SeaRenderer::stats() const noexcept { return m->statistics; }
}
