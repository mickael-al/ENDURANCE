#ifndef __SKINNED_TRANSFORM_MANAGER__
#define __SKINNED_TRANSFORM_MANAGER__

#include <string>
#include <vector>
#include <functional>

#include <glm/glm.hpp>

#include "Initializer.hpp"
#include "Manager.hpp"
#include "MemoryManager.hpp"

class DynamicGPUAllocator;

namespace Ge
{
	class AnimationWorkerPool;
	class ShapeBufferSkeleton;
	class Model;
	class SkinnedTransform;
	class Materials;

	class SkinnedTransformManager final : public InitializerPtr, public Manager
	{
	protected:
		friend class RenderingEngine;
		bool initialize(ptrClass* ptrc);
		void release();
		void updateStorage();

	public:
		SkinnedTransform* createSkinnedTransform(
			ShapeBufferSkeleton* sbs,
			std::vector<Model*>& models,
			std::string nom = "SkinnedTransform");
		SkinnedTransform* createSkinnedTransform(
			ShapeBufferSkeleton* sbs,
			std::string nom = "SkinnedTransform");
		bool attachSkinnedModels(
			SkinnedTransform* transform,
			ShapeBufferSkeleton* buffer,
			std::vector<Model*>& models,
			std::string namePrefix = "SkinnedPart");

		void updateAnimations(float deltaTime);
		// Called on the rendering thread after all animation and IK jobs join.
		// Runs each frame, including when the skeletal pose is unchanged.
		bool registerPostPoseCallback(SkinnedTransform* transform, void* owner,
			std::function<void(float)> callback);
		void unregisterPostPoseCallback(SkinnedTransform* transform, void* owner);
		void updateMatrix(
			SkinnedTransform* st,
			const std::vector<glm::mat4>& mats);
		bool applyExternalGlobalPose(
			SkinnedTransform* st,
			const std::vector<glm::mat4>& globalMatrices);
		void destroySkinnedTransform(SkinnedTransform* st);
		void setupMaterials(SkinnedTransform* st, Materials* mat);

	private:
		ptrClass* m_pc = nullptr;
		MemoryPool<SkinnedTransform> m_pool;
		DynamicGPUAllocator* m_bones = nullptr;
		AnimationWorkerPool* m_workers = nullptr;
		std::vector<SkinnedTransform*> m_transforms;
		struct PostPoseCallback
		{
			SkinnedTransform* transform = nullptr;
			void* owner = nullptr;
			std::function<void(float)> function;
		};
		std::vector<PostPoseCallback> m_postPoseCallbacks;
	};
}

#endif//!__SKINNED_TRANSFORM_MANAGER__
