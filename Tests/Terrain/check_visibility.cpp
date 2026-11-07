#include "ProceduralTerrainVisibility.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using namespace ProceduralTerrain;

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

// Keep the basis exactly constant under translations, as with an affine camera
// model transform; lookAt(eye, eye+direction) can round its direction at 1e6 m.
static glm::mat4 cameraView(const glm::vec3& eye, float yaw = 0.0f)
{
    return glm::rotate(glm::mat4(1), yaw, glm::vec3(0, 1, 0))
        * glm::translate(glm::mat4(1), -eye);
}

static void invalidation()
{
    CameraVisibilityCache cache;
    const int cameraA = 0, cameraB = 1;
    auto projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    glm::vec3 position(0);
    require(cache.update(&cameraA, cameraView(position), projection, position, 1, 8), "First camera list not built");
    for (unsigned frame = 0; frame < 128; ++frame)
        require(!cache.update(&cameraA, cameraView(position), projection, position, 1, 8), "Stationary camera rebuilt list");
    for (unsigned tick = 1; tick < 32; ++tick)
    {
        position.x = tick * 0.25f;
        require(!cache.update(&cameraA, cameraView(position), projection, position, 1, 8), "Sub-step translation rebuilt list");
    }
    position.x = 8;
    require(cache.update(&cameraA, cameraView(position), projection, position, 1, 8), "Accumulated camera displacement did not refresh");
    position.x = 8.25f;
    require(!cache.update(&cameraA, cameraView(position), projection, position, 1, 8), "Displacement anchor did not reset");
    auto view = cameraView(position, .001f);
    require(cache.update(&cameraA, view, projection, position, 1, 8), "Small rotation failed to refresh immediately");
    require(!cache.update(&cameraA, view, projection, position, 1, 8), "Same rotated pose rebuilt list");
    projection = glm::perspective(glm::radians(70.0f), 1.7f, 1.0f, 100.0f);
    require(cache.update(&cameraA, view, projection, position, 1, 8), "Projection/aspect change did not refresh");
    require(cache.update(&cameraB, view, projection, position, 1, 8), "Camera identity change did not refresh");
    require(cache.update(&cameraB, view, projection, position, 2, 8), "Published mesh revision did not refresh");
    require(cache.update(&cameraB, view, projection, position, 2, 4), "Translation step edit did not refresh");
    cache.invalidate();
    require(cache.update(&cameraB, view, projection, position, 2, 4), "Explicit invalidation ignored");
    view[0][0] *= 1.1f;
    require(cache.update(&cameraB, view, projection, position, 2, 4), "Camera scale/basis change did not refresh");

    require(cache.update(&cameraA, cameraView(position), projection, position, 2, 0), "Zero-step setup failed");
    require(!cache.update(&cameraA, cameraView(position), projection, position, 2, 0), "Zero step rebuilt stationary list");
    position.x += .01f;
    require(cache.update(&cameraA, cameraView(position), projection, position, 2, 0), "Zero step failed to refresh translation");
    std::cout << "PASS visibility keys: stationary reuse, cumulative displacement, immediate pose/projection/identity/revision invalidation\n";
}

static void explicitEnteringChunks()
{
    const auto projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    CameraVisibilityCache cache;
    const int camera = 0;
    const glm::vec3 origin(0);
    cache.update(&camera, cameraView(origin), projection, origin, 1, 4);
    const Frustum exact = makeFrustum(projection);
    struct Case { std::array<float, 3> box; glm::vec3 movement; };
    const Case cases[] = {
        {{{11.5f, -.25f, -10.0f}}, glm::vec3(3, 0, 0)},
        {{{-.25f, 11.5f, -10.0f}}, glm::vec3(0, 3, 0)},
        {{{-.25f, -.25f, -102.0f}}, glm::vec3(0, 0, -3)},
        {{{-.25f, -.25f, .5f}}, glm::vec3(0, 0, 3)}
    };
    for (const auto& test : cases)
    {
        require(!intersectsFrustum(exact, test.box, .5f), "Entering-box fixture was already inside original frustum");
        require(intersectsFrustum(cache.frustum(), test.box, .5f), "Cached list omitted a soon-visible box");
        require(!cache.update(&camera, cameraView(test.movement), projection, test.movement, 1, 4), "Entering box required an unexpected list rebuild");
        require(intersectsFrustum(makeFrustum(projection * cameraView(test.movement)), test.box, .5f), "Entering-box fixture did not become visible");
    }
    std::cout << "PASS expanded frustum: side/top/near/far entrants remain in cached list until movement threshold\n";
}

static bool visibleCorner(const glm::mat4& projection, const glm::mat4& view,
                          const std::array<float, 3>& origin, float extent)
{
    for (unsigned corner = 0; corner < 8; ++corner)
    {
        const glm::vec4 clip = projection * (view * glm::vec4(origin[0] + ((corner & 1) ? extent : 0),
                                                origin[1] + ((corner & 2) ? extent : 0),
                                                origin[2] + ((corner & 4) ? extent : 0), 1));
        if (clip.w >= 0 && clip.x >= -clip.w && clip.x <= clip.w &&
            clip.y >= -clip.w && clip.y <= clip.w && clip.z >= -clip.w && clip.z <= clip.w) return true;
    }
    return false;
}

static void movingClipOracle()
{
    std::mt19937 random(71037);
    std::uniform_real_distribution<float> coordinate(-180, 180), movement(-1, 1), extent(.25f, 64);
    struct Box { std::array<float, 3> origin; float size; bool cached; };
    const int camera = 0;
    std::size_t visible = 0, refreshes = 0;
    for (const glm::vec3 anchor : {glm::vec3(0), glm::vec3(-4096, 128, 8192), glm::vec3(1000000, -500000, 250000)})
    for (unsigned orthographic = 0; orthographic < 2; ++orthographic)
    {
        const auto projection = orthographic
            ? glm::ortho(-60.0f, 60.0f, -40.0f, 40.0f, 1.0f, 160.0f)
            : glm::perspective(glm::radians(70.0f), 1.7f, 1.0f, 160.0f);
        CameraVisibilityCache cache;
        refreshes += cache.update(&camera, cameraView(anchor, .6f), projection, anchor, 1, 8);
        std::vector<Box> boxes;
        for (unsigned i = 0; i < 1024; ++i)
        {
            Box box{{{anchor.x + coordinate(random), anchor.y + coordinate(random), anchor.z + coordinate(random)}}, extent(random), false};
            box.cached = intersectsFrustum(cache.frustum(), box.origin, box.size);
            boxes.push_back(box);
        }
        for (unsigned pose = 0; pose < 64; ++pose)
        {
            glm::vec3 delta(movement(random), movement(random), movement(random));
            delta *= 7.5f / (std::max)(1.0f, glm::length(delta));
            const glm::vec3 position = anchor + delta;
            const auto view = cameraView(position, .6f);
            require(!cache.update(&camera, view, projection, position, 1, 8), "Oracle unexpectedly refreshed sub-step translation");
            for (const auto& box : boxes)
                if (visibleCorner(projection, view, box.origin, box.size))
                {
                    ++visible;
                    require(box.cached, "Cached list dropped a float clip-visible box during sub-step motion");
                }
        }
    }
    require(visible > 10000 && refreshes == 6, "Insufficient moving-frustum oracle coverage");
    std::cout << "PASS moving float clip oracle: " << visible << " visible boxes retained across 384 reused camera poses\n";
}

static void shadowInvalidation()
{
    ShadowVisibilityCache first, second;
    auto matrix = glm::ortho(-32.0f, 32.0f, -32.0f, 32.0f, 1.0f, 128.0f);
    auto wide = glm::ortho(-128.0f, 128.0f, -128.0f, 128.0f, 1.0f, 512.0f);
    require(first.update(matrix, 1) && second.update(wide, 1), "Shadow caches did not initialize independently");
    for (unsigned frame = 0; frame < 128; ++frame)
        require(!first.update(matrix, 1) && !second.update(wide, 1), "Static shadow matrices rebuilt lists");
    matrix *= glm::translate(glm::mat4(1), glm::vec3(-.001f, 0, 0));
    require(first.update(matrix, 1), "Small shadow-matrix movement was incorrectly deferred");
    require(!second.update(wide, 1), "Other cascade was unnecessarily invalidated");
    require(first.update(matrix, 2) && second.update(wide, 2), "Mesh publication failed to invalidate shadow caches");
    first.invalidate();
    require(first.update(matrix, 2), "Explicit shadow invalidation ignored");
    require(!intersectsFrustum(first.frustum(), {{64, 0, -10}}, 1) &&
            intersectsFrustum(second.frustum(), {{64, 0, -10}}, 1), "Shadow indices reused another cascade's frustum");
    std::cout << "PASS shadow cache: per-cascade reuse, exact matrix changes and geometry revision invalidation\n";
}

int main()
{
    try { invalidation(); explicitEnteringChunks(); movingClipOracle(); shadowInvalidation(); }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    return 0;
}
