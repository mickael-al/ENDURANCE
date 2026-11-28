#ifndef __ENGINE_FBX_MESH_METADATA__
#define __ENGINE_FBX_MESH_METADATA__

#include <cstdint>
#include <string>

// Source identifiers remain independent of mesh ordering and GPU resources.
// Texture paths are preserved verbatim; the asset layer resolves relocated files.
struct FBXMeshMetadata
{
	std::string meshName;
	std::string materialName;
	std::string diffuseTexture;
	std::string normalTexture;
	std::string roughnessTexture;
	std::string metallicTexture;
	std::string emissiveTexture;
	std::uint32_t sourceMaterialCount = 0;
};

#endif
