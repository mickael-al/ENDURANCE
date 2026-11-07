#!/usr/bin/env python3
"""Exercise production camera movement and render pacing with a deterministic clock.

Only platform/input/pose publication are replaced by fakes, so no display or GPU
is required. The camera class and both update implementations come from source.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


FAKES = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

struct GraphicsDataMisc { void* str_window = nullptr; };
double clockNow = 0;
int polls = 0;
bool glfwWindowShouldClose(void*) { return clockNow >= 0.5; }
void glfwPollEvents() { clockNow += 0.00025; ++polls; }

namespace Ge {
struct InputManager {
    std::array<bool, 512> keys{};
    bool rightMouse = false;
    double mouseX = 0, mouseY = 0;
    int updates = 0;
    bool getKey(int key) const { return keys[key]; }
    bool getMouse(int) const { return rightMouse; }
    double axisMouseX() const { return mouseX; }
    double axisMouseY() const { return mouseY; }
    void updateAxis() { ++updates; }
};
struct Time {
    inline static float dt = 0;
    double previousFixed = 0, previousFrame = 0;
    float fixedDelta = 0;
    static float GetDeltaTime() { return dt; }
    void fixedUpdateTime() {
        fixedDelta = static_cast<float>(clockNow - previousFixed);
        previousFixed = clockNow;
    }
    float getFixedDeltaTime() const { return fixedDelta; }
    void updateTime() {
        dt = static_cast<float>(clockNow - previousFrame);
        previousFrame = clockNow;
    }
};
struct Camera {
    struct Pose {
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };
    int publications = 0;
    Camera(GraphicsDataMisc*, int) {}
    glm::vec3 getPosition() const { return m_transform.position; }
    glm::vec3 transformDirectionAxeX() {
        return glm::rotate(m_transform.rotation, glm::vec3(-1.0f, 0.0f, 0.0f));
    }
    glm::vec3 transformDirectionAxeZ() {
        return glm::rotate(m_transform.rotation, glm::vec3(0.0f, 0.0f, -1.0f));
    }
    void updateLocal() { ++publications; }
    void setPosition(glm::vec3 position) {
        m_transform.position = position;
        updateLocal();
    }
    void setEulerAngles(glm::vec3 euler) {
        m_eulerAngles = euler;
        m_transform.rotation = glm::quat(glm::radians(euler));
        updateLocal();
    }
protected:
    Pose m_transform{};
    glm::vec3 m_eulerAngles{0.0f};
    float m_priority = 0;
};
struct BehaviourManager {
    int fixedCalls = 0, updates = 0;
    void fixedUpdate() { ++fixedCalls; }
    void update() { ++updates; }
};
struct SettingManager { int getFps() const { return 60; } };
struct RenderingEngine {
    std::vector<double> frameStarts;
    void drawFrame() {
        frameStarts.push_back(clockNow);
        clockNow += frameStarts.size() == 5 ? 0.080 : 0.001;
    }
};
struct Pointers { InputManager* inputManager = nullptr; };
struct Engine {
    inline static Pointers pointers{};
    static Pointers& getPtrClass() { return pointers; }
    GraphicsDataMisc* m_graphicsDataMisc;
    Time* m_time;
    BehaviourManager* m_behaviourManager;
    SettingManager* m_settingManager;
    InputManager* m_inputManager;
    RenderingEngine* m_renderingEngine;
    float m_lag = 0;
    void update();
};
}
'''

CHECKS = r'''
void near(float actual, float expected, float tolerance = 0.002f) {
    if (std::abs(actual - expected) > tolerance) {
        std::cerr << "Expected " << expected << ", got " << actual << '\n';
        std::abort();
    }
}

void movement(const std::vector<float>& intervals, bool boost) {
    Ge::InputManager input;
    Ge::Engine::pointers.inputManager = &input;
    Ge::FlyCamera camera(nullptr);
    input.keys[87] = true;
    input.keys[340] = boost;
    for (float interval : intervals) {
        Ge::Time::dt = interval;
        camera.updateCamera();
    }
    near(-camera.getPosition().z, boost ? 40.0f : 10.0f);
    assert(camera.publications == static_cast<int>(intervals.size()));
    input.keys[340] = false;
    Ge::Time::dt = 0.25f;
    camera.updateCamera();
    near(-camera.getPosition().z, boost ? 42.5f : 12.5f);
}

int main() {
    for (int hz : {30, 60, 144}) {
        movement(std::vector<float>(hz, 1.0f / hz), false);
        movement(std::vector<float>(hz, 1.0f / hz), true);
    }
    std::vector<float> jitter;
    for (int i = 0; i < 20; ++i)
        jitter.insert(jitter.end(), {0.005f, 0.015f, 0.030f});
    movement(jitter, false);
    movement(jitter, true);

    Ge::InputManager input;
    Ge::Engine::pointers.inputManager = &input;
    Ge::FlyCamera camera(nullptr);
    Ge::Time::dt = 0.1f;
    input.rightMouse = true;
    camera.updateCamera();
    assert(camera.publications == 0); // Holding RMB without motion is idle.
    input.mouseX = 90.0 / 0.2; // Rotate to -90 degrees around Y.
    input.keys[87] = input.keys[68] = input.keys[32] = true;
    camera.updateCamera();
    assert(camera.publications == 1); // Rotation and three axes publish once.
    near(camera.getPosition().x, 1.0f);
    near(camera.getPosition().y, 1.0f);
    near(camera.getPosition().z, 1.0f); // Movement uses the new orientation.
    camera.setLock(true);
    camera.updateCamera();
    assert(camera.publications == 1);

    GraphicsDataMisc graphics;
    Ge::Time time;
    Ge::BehaviourManager behaviour;
    Ge::SettingManager settings;
    Ge::RenderingEngine renderer;
    Ge::Engine engine{&graphics, &time, &behaviour, &settings, &input, &renderer};
    engine.update();
    assert(renderer.frameStarts.size() > 20);
    int consecutiveShort = 0, maximumShort = 0;
    for (std::size_t i = 1; i < renderer.frameStarts.size(); ++i) {
        const double interval = renderer.frameStarts[i] - renderer.frameStarts[i - 1];
        consecutiveShort = interval < 0.008 ? consecutiveShort + 1 : 0;
        maximumShort = std::max(maximumShort, consecutiveShort);
    }
    // An 80 ms render must not create a burst of queued catch-up frames.
    // One short phase-alignment frame is allowed by the fractional remainder.
    assert(maximumShort <= 1);
    assert(behaviour.fixedCalls == polls);
    assert(behaviour.updates == static_cast<int>(renderer.frameStarts.size()));
    assert(input.updates == behaviour.updates);
    std::cout << "Camera: 10 m base / 40 m boost over 1 s at 30, 60, 144 Hz and jitter; "
                 "one final pose publication; render debt discarded after 80 ms stall.\n";
}
'''


def main():
    header = (ROOT / "ANTAGONIZ/Header/Rendering/Camera/FlyCamera.hpp").read_text()
    camera = (ROOT / "ANTAGONIZ/Source/Rendering/Camera/FlyCamera.cpp").read_text()
    engine = (ROOT / "ANTAGONIZ/Source/Engine.cpp").read_text(encoding="latin-1")
    header = re.sub(r'^#include.*$', '', header, flags=re.MULTILINE)
    camera = re.sub(r'^#include.*$', '', camera, flags=re.MULTILINE)
    source = '\n'.join((FAKES, header, camera,
                         "namespace Ge {", function(engine, "void Engine::update()"),
                         "}", CHECKS))
    with tempfile.TemporaryDirectory(prefix="endurance-camera-motion-") as temporary:
        path = Path(temporary)
        (path / "check.cpp").write_text(source)
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O2",
                        "-DENDURANCE_ENABLE_NETWORK=0", "-I", str(ROOT / "ThirdParty/glm"),
                        str(path / "check.cpp"), "-o", str(path / "check")], check=True)
        subprocess.run([str(path / "check")], check=True)


if __name__ == "__main__":
    main()
