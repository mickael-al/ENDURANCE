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
bool bake(const Tree& tree, const std::filesystem::path& directory, std::string& error,
          const std::vector<ImpostorView>* impostors = nullptr);

// Restricted reader for our versioned generated assets, not a general glTF
// importer. Does not regenerate geometry and does not call OpenGL.
bool loadBakedTree(const std::filesystem::path& directory, Tree& tree, std::string& error);
}

#endif
