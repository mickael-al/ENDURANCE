#include "glcore.hpp"
#include "FrameGpuProfiler.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

struct Query { GLuint64 timestamp = 0; bool ready = false; };
static std::unordered_map<GLuint, Query> queries;
static GLuint nextId = 1;
static GLuint64 clockValue = 0;
static unsigned reads = 0, checks = 0, counters = 0, deletes = 0;
static void require(bool condition, const char* message)
{ if (!condition) throw std::runtime_error(message); }
void glGenQueries(GLsizei n, GLuint* ids)
{ for (int i = 0; i < n; ++i) { ids[i] = nextId++; queries.emplace(ids[i], Query{}); } }
void glDeleteQueries(GLsizei n, const GLuint* ids)
{ for (int i = 0; i < n; ++i) { require(queries.erase(ids[i]) == 1, "Query deleted twice"); ++deletes; } }
void glQueryCounter(GLuint id, GLenum target)
{
    require(target == GL_TIMESTAMP, "Profiler must not nest elapsed-time queries");
    queries.at(id) = {clockValue += 1000000, false}; ++counters;
}
void glGetQueryObjectiv(GLuint id, GLenum property, GLint* result)
{
    require(property == GL_QUERY_RESULT_AVAILABLE, "Profiler requested a blocking availability check");
    *result = queries.at(id).ready ? GL_TRUE : GL_FALSE; ++checks;
}
void glGetQueryObjectui64v(GLuint id, GLenum property, GLuint64* value)
{
    require(property == GL_QUERY_RESULT && queries.at(id).ready,
        "Profiler read a result before the GPU completed it");
    *value = queries.at(id).timestamp; ++reads;
}
static void completeGpu() { for (auto& entry : queries) entry.second.ready = true; }
using Profiler = Ge::FrameGpuProfiler;
static void completeFrame(Profiler& profiler, bool enabled = true, std::uint64_t epoch = 1)
{
    for (unsigned i = 0; i < Profiler::StageCount; ++i) profiler.mark(static_cast<Profiler::Stage>(i));
    profiler.end(enabled, epoch);
}

int main()
{
    try
    {
        Profiler profiler;
        profiler.begin(false, 1); completeFrame(profiler, false);
        require(queries.empty() && counters == 0 && checks == 0, "Disabled profiler touched OpenGL");
        for (unsigned i = 0; i < Profiler::InFlightSamples; ++i)
        { profiler.begin(true, 1); completeFrame(profiler); }
        const auto issued = counters;
        profiler.begin(true, 1); completeFrame(profiler);
        require(profiler.droppedSamples() == 1 && counters == issued && reads == 0,
            "Saturated profiler waited, read unfinished data or overwrote a pending sample");
        require(queries.size() == Profiler::InFlightSamples * (Profiler::StageCount + 1),
            "Profiler memory exceeded its fixed query ring");
        completeGpu(); profiler.begin(true, 1);
        require(profiler.last().sampleNumber == Profiler::InFlightSamples &&
            std::abs(profiler.last().totalMilliseconds - 7.0) < 1e-10,
            "Completed GPU timeline was not assembled exactly");
        for (auto duration : profiler.last().milliseconds)
            require(std::abs(duration - 1.0) < 1e-10, "Stage boundary duration is wrong");
        completeFrame(profiler); // A pending sample from the old settings epoch.
        const auto last = profiler.last().sampleNumber;
        completeGpu(); profiler.begin(true, 2);
        require(profiler.last().sampleNumber == last, "Old-epoch result replaced current measurements");
        completeFrame(profiler, true, 2);
        profiler.reset(); completeGpu(); profiler.begin(true, 2);
        require(profiler.last().sampleNumber == 0 && profiler.peak().sampleNumber == 0,
            "Reset resurrected a pending pre-reset sample");
        completeFrame(profiler, true, 2);
        completeGpu(); profiler.begin(true, 2);
        require(profiler.last().sampleNumber != 0, "Profiler failed to resume after reset");
        // The inspector may reset while the measured frame is still being drawn.
        profiler.reset(); completeFrame(profiler, true, 2);
        completeGpu(); profiler.begin(true, 2);
        require(profiler.last().sampleNumber == 0, "Reset during frame published a partial old sample");
        profiler.mark(Profiler::Stage::Shadows); completeFrame(profiler, true, 2);
        completeGpu(); profiler.begin(true, 2);
        require(profiler.last().sampleNumber == 0, "Missing stage boundaries were accepted as a valid frame");
        completeFrame(profiler, false, 2);
        const auto beforeDisabled = counters + checks + reads;
        profiler.begin(false, 3); completeFrame(profiler, false, 3);
        require(counters + checks + reads == beforeDisabled, "Paused profiler still polled the driver");
        profiler.release(); profiler.release();
        require(queries.empty() && deletes == Profiler::InFlightSamples * (Profiler::StageCount + 1),
            "Query release leaked or double-deleted resources");
        std::cout << "PASS production FrameGpuProfiler: disabled zero GL, bounded ring, no blocking reads, stage intervals, epoch/reset/pause and release\n";
    }
    catch (const std::exception& e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
