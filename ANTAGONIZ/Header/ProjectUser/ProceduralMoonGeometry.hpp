#ifndef ENDURANCE_PROCEDURAL_MOON_GEOMETRY_HPP
#define ENDURANCE_PROCEDURAL_MOON_GEOMETRY_HPP

#include <atomic>
#include <cstdint>
#include <vector>

namespace EnduranceMoon
{
// Offline sculpting coordinates: the intact Moon has radius one. Seed and
// detail settings are independent of material atlas resolution.
struct GeometryParams
{
    std::uint32_t seed = 68291;
    int resolution = 128;          // Cells across the main body; 32..256.
    float excavationStrength = 1.0f;
    int cavityCount = 6;
    float cavitySize = 0.72f;
    float cavityDepth = 0.78f;
    float excavationYaw = 60.0f;   // Degrees; zero faces +Z, +90 faces +X.
    float excavationPitch = 10.0f;
    float erosionStrength = 0.065f;
    float erosionScale = 5.0f;
    int erosionOctaves = 4;
    int debrisCount = 24;
    float debrisSpread = 0.65f;
    float debrisSize = 0.10f;
};
struct MoonVec3 { float x=0, y=0, z=0; };
struct MeshVertex
{
    MoonVec3 position;
    MoonVec3 normal;
    float u=0, v=0;
    // Linear RGB multiplier for the albedo atlas. Alpha is one; cavities are
    // slightly darker/warmer. This is material color, not baked sun lighting.
    float color[4] = {1,1,1,1};
};
struct GeometryPart
{
    std::uint32_t firstIndex=0, indexCount=0;
    bool debris=false;
    MoonVec3 center;
};
struct Geometry
{
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<GeometryPart> parts; // Body first, then independent fragments.
    MoonVec3 boundsMin, boundsMax;
};

GeometryParams sanitizeGeometry(GeometryParams params);
// CPU only, deterministic, no graphics context or shared RNG. Cancellation
// returns an empty geometry. Mesh indices are CCW when viewed from outside.
Geometry generateGeometry(const GeometryParams& params,
    const std::atomic_bool* cancel=nullptr);
}
#endif
