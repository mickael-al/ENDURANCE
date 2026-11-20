#ifndef ENDURANCE_PROCEDURAL_TREE_BAKE_HPP
#define ENDURANCE_PROCEDURAL_TREE_BAKE_HPP

#include "ProceduralTree.hpp"
#include <filesystem>
#include <string>

namespace EnduranceTrees
{
// The JSON representation is also suitable for the scene's script settings.
std::string paramsToJson(const Params& params);
bool paramsFromJson(const std::string& text, Params& params, std::string& error);
bool savePreset(const std::filesystem::path& path, const Params& params, std::string& error);
bool loadPreset(const std::filesystem::path& path, Params& params, std::string& error);

// PNG only, 1..2048 pixels per axis, <=32 MiB encoded file. Retains raw RGBA
// (top row first); generate() chooses alpha, or luminance for opaque masks.
// Uses a private decoder and never changes the engine's global stb state.
bool loadLeafMask(const std::filesystem::path& path, Image& image, std::string& error);

// Atomically publishes a new directory; existing exports are never overwritten.
// glTF2 + tree.bin retain linear COLOR_0 and custom _WIND data. PNG base colors
// are sRGB encoded; manifest.json carries the skeleton and alpha coverage mips.
// lod3, when supplied, is exactly two neutral-lit RGBA captures at 0/45-degree
// elevation. Their sRGB bytes are written unchanged; no normal PNGs are emitted.
bool bake(const Tree& tree, const std::filesystem::path& directory, std::string& error,
          const std::vector<ImpostorView>* impostors = nullptr,
          const std::vector<ImpostorView>* lod3 = nullptr);

// Restricted reader for our versioned generated assets, not a general glTF
// importer. Does not regenerate geometry and does not call OpenGL.
// The optional LOD3 output retains sRGB color bytes, not linear mask bytes.
// Older assets without a paired billboard load successfully with an empty vector.
bool loadBakedTree(const std::filesystem::path& directory, Tree& tree, std::string& error,
                   std::vector<ImpostorView>* lod3 = nullptr);
}

#endif
