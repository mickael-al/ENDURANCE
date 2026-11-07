#include "ProceduralTerrainFrustum.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace ProceduralTerrain;

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static bool visible(const Frustum& frustum, float x, float y, float z, float extent)
{
    return intersectsFrustum(frustum, {{x, y, z}}, extent);
}

static void cameraAndShadow()
{
    const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 1.0f, 100.0f);
    const Frustum camera = makeFrustum(projection);
    require(visible(camera, -1, -1, -12, 2), "Camera rejected a central chunk");
    require(!visible(camera, -1, -1, 4, 2), "Camera accepted a chunk behind the eye");
    require(!visible(camera, 40, 0, -12, 2), "Camera accepted a chunk outside its side plane");
    require(!visible(camera, -1, -1, -110, 2), "Camera accepted a chunk beyond far");
    require(!visible(camera, -0.1f, -0.1f, -0.5f, 0.2f), "Camera accepted a chunk before near");
    require(visible(camera, -200, -200, -200, 400), "Frustum enclosed by a chunk must remain visible");

    const glm::vec3 eye(100, -40, -200);
    const Frustum moved = makeFrustum(projection * glm::lookAt(eye, eye + glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)));
    require(visible(moved, 110, -41, -201, 2), "Moved and rotated camera rejected forward chunk");
    require(!visible(moved, 80, -41, -201, 2), "Moved and rotated camera accepted rear chunk");
    require(!visible(moved, -1, -1, -12, 2), "Moving camera retained stale world frustum");

    const glm::mat4 lightMatrix = glm::ortho(-60.0f, 60.0f, -60.0f, 60.0f, 1.0f, 160.0f)
                               * glm::lookAt(glm::vec3(0, 80, -20), glm::vec3(0, 0, -20), glm::vec3(0, 0, -1));
    const Frustum light = makeFrustum(lightMatrix);
    require(!visible(camera, 40, 0, -12, 2) && visible(light, 40, 0, -12, 2),
            "Off-camera shadow caster must be selected by light frustum independently");
    require(!visible(light, 80, 0, -12, 2), "Light accepted a chunk outside its own cascade");
    const Frustum nextCascade = makeFrustum(glm::ortho(-120.0f, 120.0f, -120.0f, 120.0f, 1.0f, 240.0f)
                                          * glm::lookAt(glm::vec3(0, 80, -20), glm::vec3(0, 0, -20), glm::vec3(0, 0, -1)));
    require(visible(nextCascade, 80, 0, -12, 2), "Each cascade must use its own extent");
    std::cout << "PASS camera/shadow frusta: movement, rotation, independent off-camera casters and cascades\n";
}

static void boundaries()
{
    const Frustum identity = makeFrustum(glm::mat4(1.0f));
    require(visible(identity, 1, 0, 0, 1), "Positive boundary contact was culled");
    require(visible(identity, -2, 0, 0, 1), "Negative boundary contact was culled");
    require(visible(identity, 1, 1, 1, 0), "Corner contact was culled");
    require(visible(identity, 1.0000001f, 0, 0, 1), "Float32 boundary roundoff was culled");
    require(!visible(identity, 1.001f, 0, 0, 1), "Conservative margin is excessive near origin");

    for (float sign : {-1.0f, 1.0f})
    {
        const glm::vec3 eye(sign * 1000000.0f, -sign * 500000.0f, sign * 250000.0f);
        const glm::mat4 matrix = glm::ortho(-64.0f, 64.0f, -64.0f, 64.0f, 1.0f, 256.0f)
                               * glm::lookAt(eye, eye + glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
        const Frustum f = makeFrustum(matrix);
        require(visible(f, eye.x + 64, eye.y, eye.z - 64, 16), "Large-coordinate boundary contact was culled");
        require(visible(f, eye.x - 80, eye.y, eye.z - 64, 16), "Negative-side large-coordinate contact was culled");
        require(visible(f, eye.x - 8, eye.y - 8, eye.z - 80, 16), "Large-coordinate central chunk was culled");
        require(!visible(f, eye.x + 96, eye.y, eye.z - 64, 16), "Large-coordinate margin is excessive");
    }
    std::cout << "PASS conservative frustum contacts: planes/corners, float roundoff, positive/negative million-metre coordinates\n";
}

// Independent float clip-coordinate oracle: any corner inside all six clip
// inequalities proves that the box intersects. Plane tests may retain extra
// boxes, but must never discard any such proven visible box.
static bool hasVisibleCorner(const glm::mat4& matrix, const std::array<float, 3>& p, float extent)
{
    for (int corner = 0; corner < 8; ++corner)
    {
        const glm::vec4 clip = matrix * glm::vec4(p[0] + ((corner & 1) ? extent : 0.0f),
                                                p[1] + ((corner & 2) ? extent : 0.0f),
                                                p[2] + ((corner & 4) ? extent : 0.0f), 1.0f);
        if (clip.w >= 0 && clip.x >= -clip.w && clip.x <= clip.w &&
            clip.y >= -clip.w && clip.y <= clip.w && clip.z >= -clip.w && clip.z <= clip.w)
            return true;
    }
    return false;
}

static void clipOracle()
{
    std::mt19937 random(1337);
    std::uniform_real_distribution<float> coordinate(-300.0f, 300.0f), size(0.125f, 128.0f);
    std::size_t provenVisible = 0;
    for (const glm::vec3 eye : {glm::vec3(0), glm::vec3(-4096, 128, 8192), glm::vec3(1000000, -500000, 250000)})
    for (int view = 0; view < 2; ++view)
    {
        const glm::vec3 direction = view ? glm::normalize(glm::vec3(1, 0.2f, -0.6f)) : glm::vec3(0, 0, -1);
        const glm::mat4 projection = view ? glm::ortho(-90.0f, 90.0f, -70.0f, 70.0f, 1.0f, 240.0f)
                                          : glm::perspective(glm::radians(73.0f), 1.6f, 0.1f, 400.0f);
        const glm::mat4 matrix = projection * glm::lookAt(eye, eye + direction, glm::vec3(0, 1, 0));
        const Frustum frustum = makeFrustum(matrix);
        for (int i = 0; i < 4096; ++i)
        {
            const std::array<float, 3> origin{{eye.x + coordinate(random), eye.y + coordinate(random), eye.z + coordinate(random)}};
            const float extent = size(random);
            if (!hasVisibleCorner(matrix, origin, extent)) continue;
            ++provenVisible;
            require(intersectsFrustum(frustum, origin, extent), "Plane culling rejected a GPU-float clip-visible corner");
        }
    }
    require(provenVisible > 1000, "Insufficient visible oracle samples");
    std::cout << "PASS independent float clip oracle: " << provenVisible << " proven visible boxes retained\n";
}

int main()
{
    try { cameraAndShadow(); boundaries(); clipOracle(); }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    return 0;
}
