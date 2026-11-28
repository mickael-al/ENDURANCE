#include "EnduranceCharacterAsset.hpp"
#include "Vertex.hpp"
#include "ofbx.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace EnduranceCharacter;
    void require(bool value, const char* message)
    { if (!value) throw std::runtime_error(message); }
    bool near(float a, float b, float tolerance = 1.0e-4f) { return std::abs(a - b) <= tolerance; }
    Vertex vertex(glm::vec3 p) { Vertex v{}; v.pos = p; return v; }
    std::string string(ofbx::DataView view)
    { return view.begin && view.end ? std::string(reinterpret_cast<const char*>(view.begin), reinterpret_cast<const char*>(view.end)) : ""; }

    void policyTests()
    {
        require(classify("Body", "N00_000_00_HairBack_00_HAIR (Instance)") == Part::Hair, "Body mesh is back hair");
        require(classify("Body.001", "N00_000_00_Body_00_SKIN (Instance)") == Part::Skin, "skin material recognized");
        require(classify("arbitrary_split_name", "N00_000_00_EyeIris_00_EYE (Instance)") == Part::Iris, "face does not depend on split index");
        require(classify("Body", "") == Part::Unknown, "ambiguous body must not be misclassified");
        const auto source = materialFiles(Part::Skin, "D:\\work\\Endurance.vrm.textures\\_04.png", "relative/face/_05.png");
        require(source.albedo == "_04.png" && source.normal == "_05.png", "portable texture basenames");
        const auto dress = materialFiles(Part::Dress, "old.png", "old_normal.png");
        require(dress.albedo == "dress.png" && dress.normal == "dress_normal.png", "renamed garment textures");
        require(materialFiles(Part::Unknown, "..", "folder/").albedo.empty(), "no parent path accepted as texture");
    }

    void normalizationTests()
    {
        // First mesh has a rotated/translated source basis, exactly the class
        // of Blender export transform that makes raw local-Y bounds incorrect.
        const glm::mat4 bind = glm::translate(glm::mat4(1), glm::vec3(-2, 90, 1)) *
            glm::rotate(glm::mat4(1), glm::radians(-90.0f), glm::vec3(1, 0, 0)) *
            glm::scale(glm::mat4(1), glm::vec3(44.0f));
        const glm::mat4 inv = glm::inverse(bind);
        std::vector<Vertex> body{vertex(glm::vec3(inv * glm::vec4(-10, -2, 0, 1))),
                                 vertex(glm::vec3(inv * glm::vec4(10, 158, 0, 1)))};
        std::vector<Vertex> hair{vertex(glm::vec3(inv * glm::vec4(0, 230, 0, 1)))};
        std::vector<AssetMeshView> meshes{{Part::Skin, &body}, {Part::Hair, &hair}};
        const auto n = normalizeCharacter(meshes, bind);
        require(n.valid && near(n.bodyHeight, 1.60f), "measured height excludes hair");
        require(near((n.meshToCharacter * glm::vec4(body[0].pos, 1)).y, 0.0f), "feet at origin");
        require(near((n.meshToCharacter * glm::vec4(body[1].pos, 1)).y, 1.70f), "normalizes to requested height");
        require(!normalizeCharacter({{Part::Hair, &hair}}, bind).valid, "no skin gives explicit error");
        body[1].pos.x = (std::numeric_limits<float>::infinity)();
        require(!normalizeCharacter(meshes, bind).valid, "nonfinite vertices rejected including fast-math builds");
    }

    void maskTests()
    {
        std::vector<Vertex> belt;
        for (int layer = 0; layer < 12; ++layer)
            for (int i = 0; i < 40; ++i)
            {
                const float angle = 6.2831853f * i / 40.0f;
                belt.push_back(vertex({std::sin(angle) * 0.16f, 0.97f + layer * 0.005f, std::cos(angle) * 0.10f}));
            }
        // Far more tessellation in hanging ends must not pull the waist down.
        for (int i = 0; i < 10000; ++i)
            belt.push_back(vertex({(i & 1) ? 0.02f : -0.02f, 0.60f + (i % 300) * 0.0008f, 0.12f}));
        Normalization n; n.valid = true; n.heightMetres = 1.70f;
        const auto layout = attachmentLayout({{Part::Belt, &belt}}, n);
        require(layout.waistFromBelt && layout.waistHeight > 0.97f && layout.waistHeight < 1.03f, "broad loop mode excludes dense tassels");
        std::vector<Vertex> samples{vertex({0, 1.2f, 0}), vertex({0, 0.95f, 0}), vertex({0, 0.5f, 0})};
        const auto dressPins = attachmentMask(Part::Dress, samples, glm::mat4(1), layout);
        require(dressPins[0] == 1.0f && dressPins[1] > 0.0f && dressPins[1] < 1.0f && dressPins[2] == 0.0f, "dress upper attachment and free hem");
        samples = {vertex({0, layout.waistHeight - 0.02f, 0}), vertex({0, layout.waistHeight - 0.07f, 0}), vertex({0, 0.5f, 0})};
        const auto beltPins = attachmentMask(Part::Belt, samples, glm::mat4(1), layout);
        require(beltPins[0] == 1.0f && beltPins[1] > 0.0f && beltPins[1] < 1.0f && beltPins[2] == 0.0f, "belt knot attached and tails progressively free");
        for (float pin : attachmentMask(Part::Pants, samples, glm::mat4(1), layout))
            require(pin == 1.0f, "pants never receive cloth simulation mask");
    }

    void assetTests(const char* path)
    {
        std::ifstream stream(path, std::ios::binary);
        require(static_cast<bool>(stream), "character FBX exists");
        const std::vector<ofbx::u8> bytes((std::istreambuf_iterator<char>(stream)), {});
        auto* scene = ofbx::load(bytes.data(), static_cast<int>(bytes.size()), static_cast<ofbx::u16>(ofbx::LoadFlags::TRIANGULATE));
        require(scene != nullptr, "character FBX parses");
        const auto* first = scene->getMesh(0);
        const glm::mat4 bind = glm::mat4(glm::make_mat4(first->getGlobalTransform().m)) *
            glm::mat4(glm::make_mat4(first->getGeometricMatrix().m));
        const glm::mat4 inverseBind = glm::inverse(bind);
        std::vector<std::vector<Vertex>> vertices(scene->getMeshCount());
        std::vector<AssetMeshView> views;
        for (int i = 0; i < scene->getMeshCount(); ++i)
        {
            const auto* mesh = scene->getMesh(i);
            const auto* geometry = mesh->getGeometry();
            require(geometry && geometry->getSkin(), "all authored meshes are skinned");
            require(mesh->getMaterialCount() == 1, "authored meshes are split by material");
            const auto* material = mesh->getMaterial(0);
            const Part role = classify(mesh->name, material->name);
            require(role != Part::Unknown, "every shipped mesh is semantically classified");
            const auto* diffuse = material->getTexture(ofbx::Texture::DIFFUSE);
            const auto* normal = material->getTexture(ofbx::Texture::NORMAL);
            const auto files = materialFiles(role, diffuse ? string(diffuse->getRelativeFileName()) : "",
                normal ? string(normal->getRelativeFileName()) : "");
            const std::string folder = "ENDURANCE/Ressources/Model/Character/";
            require(!files.albedo.empty() && static_cast<bool>(std::ifstream(folder + files.albedo)), "resolved albedo exists");
            require(!files.normal.empty() && static_cast<bool>(std::ifstream(folder + files.normal)), "resolved normal exists");
            const glm::mat4 meshToCommon = inverseBind * glm::mat4(glm::make_mat4(mesh->getGlobalTransform().m)) *
                glm::mat4(glm::make_mat4(mesh->getGeometricMatrix().m));
            for (int j = 0; j < geometry->getVertexCount(); ++j)
            {
                const auto p = geometry->getVertices()[j];
                vertices[i].push_back(vertex(glm::vec3(meshToCommon * glm::vec4(p.x, p.y, p.z, 1))));
            }
            views.push_back({role, &vertices[i]});
        }
        const auto n = normalizeCharacter(views, bind);
        require(n.valid && near(n.bodyHeight, 1.59701f, 0.001f), "actual FBX authoring height measured");
        const auto layout = attachmentLayout(views, n);
        require(layout.waistFromBelt && layout.waistHeight > 0.9f && layout.waistHeight < 1.2f, "actual waist inferred from belt geometry");
        for (const auto& view : views)
        {
            if (view.part != Part::Dress && view.part != Part::Belt) continue;
            const auto pins = attachmentMask(view.part, *view.vertices, n.meshToCharacter, layout);
            std::size_t hard = 0, soft = 0, free = 0;
            for (float pin : pins) { if (pin == 1) ++hard; else if (pin == 0) ++free; else ++soft; }
            require(hard > 0 && soft > 0 && free > 0, "actual garment has all three attachment zones");
            std::cout << partName(view.part) << " pins hard=" << hard << " soft=" << soft << " free=" << free << '\n';
        }
        std::cout << "Character body " << n.bodyHeight << " m -> " << n.heightMetres
                  << " m; waist=" << layout.waistHeight << " m\n";
        scene->destroy();
    }
}

int main(int argc, char** argv)
{
    try
    {
        policyTests(); normalizationTests(); maskTests();
        if (argc > 1) assetTests(argv[1]);
        std::cout << "Character asset policy tests passed\n";
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return EXIT_FAILURE; }
}
