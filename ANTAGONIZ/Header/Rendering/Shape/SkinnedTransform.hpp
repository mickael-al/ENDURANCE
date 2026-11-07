#ifndef __SKINNED_TRANSFORM__
#define __SKINNED_TRANSFORM__

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "GObject.hpp"
#include "SkeletalArmIK.hpp"

namespace Ge
{
	struct SkeletonID
	{
		unsigned int offMatrix = 0;
	};

	class ShapeBufferSkeleton;
	class SkinnedTransformManager;

	class SkinnedTransform final : public GObject
	{
	public:
		SkinnedTransform(ShapeBufferSkeleton* sbs, size_t offsetBone);
		~SkinnedTransform();

		ShapeBufferSkeleton* getBuffer() const;
		void mapMemory() override;
		SkeletonID getSkeletonData() const;
		size_t getSkeletonOff() const;
		unsigned int getSkeletonBufferID() const;

		// Demarre une animation du buffer sur cette instance.
		bool playAnimation(
			size_t animationIndex,
			bool loop = true,
			float playbackSpeed = 1.0f);

		// Joue deux clips simultanement. Les poids sont normalises,
		// par exemple 0.2f / 0.8f.
		bool blendAnimations(
			size_t firstAnimation,
			size_t secondAnimation,
			float firstWeight,
			float secondWeight,
			bool loop = true,
			float playbackSpeed = 1.0f);

		// Fait converger la pose courante vers un clip cible pendant
		// transitionDuration secondes. Un changement de direction pendant
		// le fondu repart de la pose melangee courante sans saut.
		bool transitionToAnimation(
			size_t animationIndex,
			float transitionDuration,
			bool loop = true,
			float playbackSpeed = 1.0f);

		void stopAnimation();
		void setPlaybackSpeed(float playbackSpeed);
		bool isAnimationPlaying() const;
		int getPrimaryAnimation() const;
		int getSecondaryAnimation() const;
		float getSecondaryWeight() const;
		const std::vector<glm::mat4>& getGlobalPose() const;
		void setArmIKTarget(std::size_t side, const ArmIKTarget& target);
		void clearArmIKTargets(bool immediate = false);
		bool getArmIKReference(std::size_t side, glm::vec3& shoulder, float& reach) const;

		// Remplace la pose animee par des matrices globales de squelette.
		// Utilise notamment par le ragdoll, qui reste ainsi compatible avec
		// exactement les memes meshes et la meme palette GPU.
		bool setExternalGlobalPose(
			const std::vector<glm::mat4>& globalMatrices);

	private:
		friend class SkinnedTransformManager;

		bool evaluateAnimation(float deltaTime);
		const std::vector<glm::mat4>& getBoneMatrices() const;
		void rebuildSkinningMatrices();

		ShapeBufferSkeleton* m_shapeBufferSkeleton = nullptr;
		SkeletonID m_sid;
		size_t m_offsetSID = 0;
		unsigned int m_ssboSkeletonID = 0;

		int m_primaryAnimation = -1;
		int m_secondaryAnimation = -1;
		double m_primaryTime = 0.0;
		double m_secondaryTime = 0.0;
		float m_primaryWeight = 1.0f;
		float m_secondaryWeight = 0.0f;
		float m_playbackSpeed = 1.0f;
		bool m_loop = true;
		bool m_playing = false;
		bool m_poseDirty = false;
		bool m_transitionActive = false;
		float m_transitionDuration = 0.0f;
		float m_transitionElapsed = 0.0f;

		std::vector<glm::mat4> m_localMatrices;
		std::vector<glm::mat4> m_transitionSourceMatrices;
		std::vector<glm::mat4> m_globalMatrices;
		std::vector<glm::mat4> m_boneMatrices;
		std::vector<unsigned char> m_resolveState;
		SkeletalArmIK m_armIK;
	};
}

#endif //!__SKINNED_TRANSFORM__
