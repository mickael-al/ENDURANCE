#include "glcore.hpp"
#include "FrameGpuProfiler.hpp"

namespace Ge
{
    void FrameGpuProfiler::begin(bool enabled, std::uint64_t epoch)
    {
        m_active = InFlightSamples;
        m_marked = 0;
        m_epoch = epoch;
        if (!enabled) return;
        if (!m_initialized)
        {
            for (auto& slot : m_slots) glGenQueries(StageCount + 1, slot.queries.data());
            m_initialized = true;
        }
        poll();
        for (unsigned i = 0; i < InFlightSamples; ++i)
        {
            auto& slot = m_slots[i];
            if (slot.pending) continue;
            m_active = i;
            slot.epoch = epoch;
            slot.sequence = ++m_sequence;
            slot.resetSerial = m_resetSerial;
            glQueryCounter(slot.queries[0], GL_TIMESTAMP);
            return;
        }
        ++m_dropped;
    }

    void FrameGpuProfiler::mark(Stage stage)
    {
        if (m_active == InFlightSamples) return;
        if (static_cast<unsigned>(stage) != m_marked || m_marked >= StageCount)
        { m_active = InFlightSamples; return; }
        glQueryCounter(m_slots[m_active].queries[++m_marked], GL_TIMESTAMP);
    }

    void FrameGpuProfiler::end(bool enabled, std::uint64_t epoch)
    {
        if (m_active == InFlightSamples) return;
        auto& slot = m_slots[m_active];
        slot.pending = enabled && slot.epoch == epoch && slot.resetSerial == m_resetSerial &&
            m_marked == StageCount;
        m_active = InFlightSamples;
    }

    void FrameGpuProfiler::poll()
    {
        for (auto& slot : m_slots)
        {
            if (!slot.pending) continue;
            GLint available = GL_FALSE;
            glGetQueryObjectiv(slot.queries[StageCount], GL_QUERY_RESULT_AVAILABLE, &available);
            if (available != GL_TRUE) continue;
            slot.pending = false;
            if (slot.epoch != m_epoch || slot.resetSerial != m_resetSerial) continue;
            std::array<GLuint64, StageCount + 1> stamps{};
            for (unsigned i = 0; i <= StageCount; ++i)
                glGetQueryObjectui64v(slot.queries[i], GL_QUERY_RESULT, &stamps[i]);
            Sample sample;
            sample.sampleNumber = slot.sequence;
            bool valid = true;
            for (unsigned i = 0; i < StageCount; ++i)
            {
                valid &= stamps[i + 1] >= stamps[i];
                sample.milliseconds[i] = double(stamps[i + 1] - stamps[i]) * 1e-6;
            }
            if (!valid) continue;
            sample.totalMilliseconds = double(stamps[StageCount] - stamps[0]) * 1e-6;
            if (sample.sampleNumber > m_last.sampleNumber) m_last = sample;
            if (!m_peak.sampleNumber || sample.totalMilliseconds > m_peak.totalMilliseconds) m_peak = sample;
        }
    }

    void FrameGpuProfiler::reset() noexcept
    {
        ++m_resetSerial;
        m_last = m_peak = Sample{};
        m_dropped = 0;
    }

    void FrameGpuProfiler::release() noexcept
    {
        if (m_initialized)
            for (auto& slot : m_slots) glDeleteQueries(StageCount + 1, slot.queries.data());
        m_slots = {};
        m_initialized = false;
        m_active = InFlightSamples;
        m_marked = 0;
        reset();
    }
}
