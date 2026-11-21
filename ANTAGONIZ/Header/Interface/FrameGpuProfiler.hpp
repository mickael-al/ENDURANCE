#ifndef GE_FRAME_GPU_PROFILER_HPP
#define GE_FRAME_GPU_PROFILER_HPP

#include <array>
#include <cstdint>

namespace Ge
{
    // Optional GPU timeline for the real frame, excluding HUD and presentation.
    // Timestamp intervals include GPU idle gaps caused by CPU submission. They
    // are not isolated shader costs. Results are read only after availability.
    class FrameGpuProfiler
    {
    public:
        FrameGpuProfiler() = default;
        FrameGpuProfiler(const FrameGpuProfiler&) = delete;
        FrameGpuProfiler& operator=(const FrameGpuProfiler&) = delete;
        enum class Stage : unsigned
        { Preparation, Opaque, Shadows, Atmosphere, Lighting, Transparency, PostProcess, Count };
        static constexpr unsigned StageCount = static_cast<unsigned>(Stage::Count);
        static constexpr unsigned InFlightSamples = 8;
        struct Sample
        {
            std::array<double, StageCount> milliseconds{};
            double totalMilliseconds = 0.0;
            std::uint64_t sampleNumber = 0;
        };

        // All calls, including release(), run on the owning OpenGL context.
        // Disabled frames perform no GL operations. Saturated rings skip a
        // sample rather than waiting, growing memory or reusing pending IDs.
        void begin(bool enabled, std::uint64_t epoch);
        void mark(Stage stage);
        void end(bool enabled, std::uint64_t epoch);
        void reset() noexcept;
        void release() noexcept;
        const Sample& last() const noexcept { return m_last; }
        const Sample& peak() const noexcept { return m_peak; }
        std::uint64_t droppedSamples() const noexcept { return m_dropped; }

    private:
        struct Slot
        {
            std::array<unsigned, StageCount + 1> queries{};
            std::uint64_t epoch = 0, sequence = 0, resetSerial = 0;
            bool pending = false;
        };
        void poll();
        std::array<Slot, InFlightSamples> m_slots{};
        Sample m_last{}, m_peak{};
        std::uint64_t m_epoch = 0, m_sequence = 0, m_dropped = 0, m_resetSerial = 0;
        unsigned m_active = InFlightSamples, m_marked = 0;
        bool m_initialized = false;
    };
}
#endif
