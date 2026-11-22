#ifndef ENDURANCE_PROCEDURAL_MOON_EXPORT_HPP
#define ENDURANCE_PROCEDURAL_MOON_EXPORT_HPP

#include "ProceduralMoon.hpp"
#include "ProceduralMoonGeometry.hpp"
#include <string>

namespace EnduranceMoon
{
// Native offline export. GLB is preferred; .gltf embeds the same buffer and PNG.
// No lighting is baked. Positions are in normalized Moon units (radius one).
// Every fragment remains a separate glTF node. A single atomic file replacement
// publishes mesh, material and texture together; failure preserves prior output.
bool saveModel(const Geometry& geometry,const Surface& surface,
    const std::string& path,std::string& error,
    const GeometryParams* geometryRecipe=nullptr,const Params* materialRecipe=nullptr);
}
#endif
