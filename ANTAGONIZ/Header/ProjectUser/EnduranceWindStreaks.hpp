#pragma once

#include <glm/glm.hpp>
#include <cstddef>
#include <memory>
#include <string>

struct ptrClass;

// Game-owned world-space wind ribbons. Camera movement controls spawning only;
// existing streaks move with the wind and never follow an emitter transform.
class EnduranceWindStreaks
{
public:
    EnduranceWindStreaks();
    ~EnduranceWindStreaks();
    EnduranceWindStreaks(const EnduranceWindStreaks&) = delete;
    EnduranceWindStreaks& operator=(const EnduranceWindStreaks&) = delete;
    bool initialize(const ptrClass* engine);
    void update(float strength, const glm::vec3& cameraPosition, const glm::vec2& wind,
        float dt, bool teleport = false, float density = 1.0f);
    void release();
    std::size_t liveCount() const;
    const std::string& error() const;
    static constexpr std::size_t MaxStreaks = 128;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
