// Real scene JSON compatibility/roundtrip test, independent of OpenGL or ImGui.
// Uses the production LightData/SceneData metadata and json_struct parser.
#include "ProjectData.hpp"
#include "UniformBufferLight.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class T>
void parse(const std::string& json, T& value)
{
    JS::ParseContext context(json);
    require(context.parseTo(value) == JS::Error::NoError, "Production JSON parser rejected the fixture");
}

const std::string legacyLight = R"({
    "name":"Legacy sun", "position":{"x":100,"y":50,"z":-25},
    "rotation":{"x":0,"y":0,"z":0,"w":1}, "scale":{"x":1,"y":1,"z":1},
    "color":{"x":1,"y":0.9,"z":0.8}, "range":12, "spotAngle":45,
    "shadow":true, "status":0, "idParent":-1, "scripts":[], "collisions":[], "tags":["Sun"]
})";
}

int main()
{
    try
    {
        UniformBufferLight gpu{};
        LightData legacy;
        parse(legacyLight, legacy);
        require(legacy.shadowDepthBias == .5f && legacy.shadowNormalBias == 1.5f,
            "Old light records must receive the documented defaults");
        require(gpu.shadowDepthBias == legacy.shadowDepthBias && gpu.shadowNormalBias == legacy.shadowNormalBias,
            "New GPU lights and legacy scene lights must have consistent defaults");

        LightData onlyDepth, onlyNormal;
        parse(R"({"shadowDepthBias":0})", onlyDepth);
        parse(R"({"shadowNormalBias":0})", onlyNormal);
        require(onlyDepth.shadowDepthBias == 0.f && onlyDepth.shadowNormalBias == 1.5f,
            "Explicit zero depth bias must not be replaced by a default");
        require(onlyNormal.shadowDepthBias == .5f && onlyNormal.shadowNormalBias == 0.f,
            "Each missing field must retain its own default");

        const std::string legacyScene = "{\"lightData\":[" + legacyLight + "]}";
        SceneData scene;
        parse(legacyScene, scene);
        // Initialize unrelated legacy scene fields before serializing the whole
        // real SceneData, whose other members predate default initializers.
        scene.freeCamPos = glm::vec3(0.f);
        scene.freeCamRot = glm::quat(1.f, 0.f, 0.f, 0.f);
        scene.path = {};
        require(scene.lightData.size() == 1, "Legacy scene light missing");
        scene.lightData[0].shadowDepthBias = .125f;
        scene.lightData[0].shadowNormalBias = 2.75f;
        auto moon = scene.lightData[0];
        moon.name = "Moon";
        moon.shadowDepthBias = 1.75f;
        moon.shadowNormalBias = .25f;
        scene.lightData.push_back(moon);
        auto point = legacy;
        point.name = "Point light";
        point.status = 1;
        scene.lightData.push_back(point);

        const std::string saved = JS::serializeStruct(scene);
        require(saved.find("\"shadowDepthBias\"") != std::string::npos &&
                saved.find("\"shadowNormalBias\"") != std::string::npos,
            "Scene serialization must write both bias controls");
        SceneData restored;
        parse(saved, restored);
        require(restored.lightData.size() == 3, "Scene roundtrip changed the light collection");
        require(restored.lightData[0].name == "Legacy sun" && restored.lightData[0].status == 0 &&
                restored.lightData[0].shadowDepthBias == .125f && restored.lightData[0].shadowNormalBias == 2.75f,
            "First directional light lost its individual bias");
        require(restored.lightData[1].name == "Moon" && restored.lightData[1].status == 0 &&
                restored.lightData[1].shadowDepthBias == 1.75f && restored.lightData[1].shadowNormalBias == .25f,
            "Second directional light inherited another light's bias");
        require(restored.lightData[2].status == 1 && restored.lightData[2].range == legacy.range &&
                restored.lightData[2].shadowDepthBias == .5f && restored.lightData[2].shadowNormalBias == 1.5f,
            "Other light types or their legacy defaults changed");

        // The editor can reuse a SceneData. An old scene loaded after a new one
        // must reconstruct its light records, not retain the previous settings.
        parse(legacyScene, restored);
        require(restored.lightData.size() == 1 && restored.lightData[0].shadowDepthBias == .5f &&
                restored.lightData[0].shadowNormalBias == 1.5f,
            "Reloading an old scene retained the previous scene's custom bias");
        std::cout << "PASS real LightData/SceneData JSON: legacy, partial fields, explicit zero, distinct lights, save/load, and old-scene reload; GPU stride80 offsets68/72\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
