#ifndef __ENGINE_MODEL_BUFFER_SKELETON__
#define __ENGINE_MODEL_BUFFER_SKELETON__

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Vertex.hpp"
#include "ShapeBuffer.hpp"

struct GraphicsDataMisc;

struct Bone
{
	uint16_t idGlobal = 0;
	int16_t parent = -1;
	std::string name;

	glm::mat4 bindPose{ 1.0f };
	glm::mat4 inverseBindPose{ 1.0f };
	glm::mat4 worldPoseCluster{ 1.0f };
	glm::mat4 worldPoseObj{ 1.0f };
};

struct Skeleton
{
	std::vector<Bone> bones;

	// Espace commun des vertices du modele. Les matrices FBX d'os sont
	// exprimees dans l'espace global de la scene source (souvent en cm),
	// alors que les vertices restent dans l'espace local du mesh.
	glm::mat4 bindTransform{ 1.0f };
	glm::mat4 inverseBindTransform{ 1.0f };

	// Lookups stables entre le FBX du modele et les FBX d'animations.
	std::unordered_map<std::string, uint16_t> pathToID;
	std::unordered_map<std::string, uint16_t> nameToID;
};

struct BoneAnim
{
	std::vector<std::vector<glm::vec2>> T;
	std::vector<std::vector<glm::vec2>> R;
	std::vector<std::vector<glm::vec2>> S;
};

struct BonePose
{
	glm::vec3 translation{ 0.0f };
	glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	glm::vec3 scale{ 1.0f };
};

struct Animation
{
	std::string name;
	double max_duration = 0.0;
	double min_duration = 0.0;
	float sampleRate = 30.0f;
	uint32_t sampleCount = 0;

	// Courbes originales conservees pour le debug et l'outillage.
	std::vector<BoneAnim> bones;

	// Poses locales echantillonnees, frame-major:
	// samples[frame * boneCount + bone].
	// Le runtime interpole ces poses sans garder le fichier FBX en memoire.
	std::vector<BonePose> samples;

	double duration() const
	{
		return max_duration > min_duration ? max_duration - min_duration : 0.0;
	}
};

namespace Ge
{
	class ShapeBufferBase;

	class ShapeBufferSkeleton : public ShapeBuffer
	{
	public:
		ShapeBufferSkeleton(
			std::vector<std::vector<Vertex>>& vertices,
			std::vector<std::vector<uint32_t>>& index,
			Skeleton& skeleton,
			GraphicsDataMisc* gdm);
		~ShapeBufferSkeleton();

		void SetupVAO(unsigned int program) override;
		unsigned int getIndiceSize() const override;

		bool loadAnimation(const char* path);
		int loadAnimationClip(const char* path);
		int findAnimation(const std::string& name) const;

		const std::vector<ShapeBufferBase*>& getShapes() const;
		const Skeleton& getSkeleton() const;
		const std::vector<Animation>& getAnimations() const;

	private:
		std::vector<ShapeBufferBase*> m_shapes;
		Skeleton m_skeleton;
		std::vector<Animation> m_animations;
	};
}

#endif //!__ENGINE_MODEL_BUFFER_SKELETON__
