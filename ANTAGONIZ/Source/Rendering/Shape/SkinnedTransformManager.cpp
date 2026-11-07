#include "SkinnedTransformManager.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

#include "DynamicGPUAllocator.hpp"
#include "ShapeBufferBase.hpp"
#include "PointeurClass.hpp"
#include "Model.hpp"
#include "SkinnedTransform.hpp"
#include "ShapeBufferSkeleton.hpp"
#include "Materials.hpp"
#include "GraphicsDataMisc.hpp"
#include "Debug.hpp"

namespace Ge
{
	class AnimationWorkerPool
	{
	public:
		AnimationWorkerPool()
		{
			const unsigned int hardwareThreads =
				std::thread::hardware_concurrency();
			const unsigned int workerCount =
				hardwareThreads > 1u
					? (std::min)(hardwareThreads - 1u, 4u)
					: 0u;
			for (unsigned int i = 0; i < workerCount; ++i)
			{
				m_threads.emplace_back([this]() { workerMain(); });
			}
		}

		~AnimationWorkerPool()
		{
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_stopping = true;
			}
			m_workAvailable.notify_all();
			for (std::thread& worker : m_threads)
			{
				if (worker.joinable())
				{
					worker.join();
				}
			}
		}

		void run(
			size_t jobCount,
			const std::function<void(size_t)>& function)
		{
			if (jobCount == 0)
			{
				return;
			}
			if (m_threads.empty() || jobCount == 1)
			{
				for (size_t i = 0; i < jobCount; ++i)
				{
					function(i);
				}
				return;
			}

			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_pendingJobs += jobCount;
				for (size_t i = 0; i < jobCount; ++i)
				{
					m_jobs.emplace_back([function, i]() { function(i); });
				}
			}
			m_workAvailable.notify_all();

			std::unique_lock<std::mutex> lock(m_mutex);
			m_workFinished.wait(
				lock,
				[this]() { return m_pendingJobs == 0; });
		}

	private:
		void workerMain()
		{
			for (;;)
			{
				std::function<void()> job;
				{
					std::unique_lock<std::mutex> lock(m_mutex);
					m_workAvailable.wait(
						lock,
						[this]()
						{
							return m_stopping || !m_jobs.empty();
						});
					if (m_stopping && m_jobs.empty())
					{
						return;
					}
					job = std::move(m_jobs.front());
					m_jobs.pop_front();
				}

				job();

				{
					std::lock_guard<std::mutex> lock(m_mutex);
					--m_pendingJobs;
					if (m_pendingJobs == 0)
					{
						m_workFinished.notify_all();
					}
				}
			}
		}

		std::vector<std::thread> m_threads;
		std::deque<std::function<void()>> m_jobs;
		std::mutex m_mutex;
		std::condition_variable m_workAvailable;
		std::condition_variable m_workFinished;
		size_t m_pendingJobs = 0;
		bool m_stopping = false;
	};

	bool SkinnedTransformManager::initialize(ptrClass* ptrc)
	{
		m_pc = ptrc;
		m_bones = new DynamicGPUAllocator();
		m_workers = new AnimationWorkerPool();
		return true;
	}

	void SkinnedTransformManager::release()
	{
		m_postPoseCallbacks.clear();
		delete m_workers;
		m_workers = nullptr;

		for (SkinnedTransform* transform : m_transforms)
		{
			if (transform != nullptr)
			{
				m_bones->free(transform->getSkeletonOff());
				m_pool.deleteObject(transform);
			}
		}
		m_transforms.clear();

		delete m_bones;
		m_bones = nullptr;
		m_pc = nullptr;
	}

	void SkinnedTransformManager::updateStorage()
	{
	}

	SkinnedTransform* SkinnedTransformManager::createSkinnedTransform(
		ShapeBufferSkeleton* sbs,
		std::vector<Model*>& models,
		std::string nom)
	{
		if (sbs == nullptr || m_pc == nullptr || m_pc->modelManager == nullptr)
		{
			return nullptr;
		}

		SkinnedTransform* transform =
			createSkinnedTransform(sbs, nom);
		if (transform == nullptr)
		{
			return nullptr;
		}

		if (!attachSkinnedModels(transform, sbs, models, nom))
		{
			destroySkinnedTransform(transform);
			return nullptr;
		}
		return transform;
	}

	bool SkinnedTransformManager::attachSkinnedModels(
		SkinnedTransform* transform,
		ShapeBufferSkeleton* buffer,
		std::vector<Model*>& models,
		std::string namePrefix)
	{
		if (transform == nullptr ||
			buffer == nullptr ||
			transform->getBuffer() == nullptr ||
			m_pc == nullptr ||
			m_pc->modelManager == nullptr)
		{
			return false;
		}

		const Skeleton& reference =
			transform->getBuffer()->getSkeleton();
		const Skeleton& part = buffer->getSkeleton();
		if (reference.bones.size() != part.bones.size())
		{
			Debug::Warn(
				"Cannot attach skinned part %s: skeleton size differs (%llu != %llu)",
				namePrefix.c_str(),
				static_cast<unsigned long long>(part.bones.size()),
				static_cast<unsigned long long>(reference.bones.size()));
			return false;
		}
		for (size_t bone = 0; bone < reference.bones.size(); ++bone)
		{
			if (reference.bones[bone].name != part.bones[bone].name)
			{
				Debug::Warn(
					"Cannot attach skinned part %s: bone %llu is %s instead of %s",
					namePrefix.c_str(),
					static_cast<unsigned long long>(bone),
					part.bones[bone].name.c_str(),
					reference.bones[bone].name.c_str());
				return false;
			}
		}

		const std::vector<ShapeBufferBase*>& shapes = buffer->getShapes();
		const size_t initialModelCount = models.size();
		for (size_t i = 0; i < shapes.size(); ++i)
		{
			const std::string modelName =
				namePrefix + "_" + std::to_string(i);
			Model* model =
				m_pc->modelManager->createModel(shapes[i], modelName);
			if (model != nullptr)
			{
				model->setParent(transform);
				// setParent preserves the model's world transform. A skinned part
				// must instead use the shared skeleton as its exact local space;
				// otherwise that preserved inverse transform cancels the character
				// root rotation and scale.
				model->setLocalPosition(glm::vec3(0.0f));
				model->setLocalEuler(glm::vec3(0.0f));
				model->setLocalScale(glm::vec3(1.0f));
				models.push_back(model);
			}
		}
		return models.size() > initialModelCount;
	}

	SkinnedTransform* SkinnedTransformManager::createSkinnedTransform(
		ShapeBufferSkeleton* sbs,
		std::string nom)
	{
		if (sbs == nullptr || m_bones == nullptr)
		{
			return nullptr;
		}

		const size_t boneCount = sbs->getSkeleton().bones.size();
		if (boneCount == 0)
		{
			return nullptr;
		}

		std::vector<glm::mat4> basePose(
			boneCount,
			glm::mat4(1.0f));
		const size_t boneOffset =
			m_bones->allocate(
				basePose.data(),
				boneCount * sizeof(glm::mat4));

		SkinnedTransform* transform =
			m_pool.newObject(sbs, boneOffset);
		transform->setName(nom);
		m_transforms.push_back(transform);
		return transform;
	}

	void SkinnedTransformManager::updateAnimations(float deltaTime)
	{
		if (m_workers == nullptr || m_bones == nullptr)
		{
			return;
		}

		std::vector<SkinnedTransform*> evaluated;
		evaluated.resize(m_transforms.size(), nullptr);
		m_workers->run(
			m_transforms.size(),
			[&](size_t index)
			{
				SkinnedTransform* transform = m_transforms[index];
				if (transform != nullptr &&
					transform->evaluateAnimation(deltaTime))
				{
					evaluated[index] = transform;
				}
			});

		// OpenGL reste exclusivement sur le thread de rendu.
		for (SkinnedTransform* transform : evaluated)
		{
			if (transform != nullptr)
			{
				updateMatrix(
					transform,
					transform->getBoneMatrices());
			}
		}

		// Secondary motion must continue settling even if evaluateAnimation
		// skipped an idle/stopped skeleton. No OpenGL callback runs on workers.
		std::vector<std::pair<SkinnedTransform*, void*>> callbacks;
		callbacks.reserve(m_postPoseCallbacks.size());
		for (const PostPoseCallback& callback : m_postPoseCallbacks)
		{
			callbacks.emplace_back(callback.transform, callback.owner);
		}
		for (const auto& key : callbacks)
		{
			const auto found = std::find_if(m_postPoseCallbacks.begin(),
				m_postPoseCallbacks.end(), [&](const PostPoseCallback& callback)
				{
					return callback.transform == key.first && callback.owner == key.second;
				});
			if (found != m_postPoseCallbacks.end())
			{
				// Copy before calling: a callback may unregister itself safely.
				const std::function<void(float)> function = found->function;
				function(deltaTime);
			}
		}
	}

	bool SkinnedTransformManager::registerPostPoseCallback(
		SkinnedTransform* transform, void* owner, std::function<void(float)> callback)
	{
		if (transform == nullptr || owner == nullptr || !callback ||
			std::find(m_transforms.begin(), m_transforms.end(), transform) == m_transforms.end())
		{
			return false;
		}
		unregisterPostPoseCallback(transform, owner);
		m_postPoseCallbacks.push_back({ transform, owner, std::move(callback) });
		return true;
	}

	void SkinnedTransformManager::unregisterPostPoseCallback(
		SkinnedTransform* transform, void* owner)
	{
		m_postPoseCallbacks.erase(std::remove_if(m_postPoseCallbacks.begin(),
			m_postPoseCallbacks.end(), [&](const PostPoseCallback& callback)
			{
				return callback.transform == transform && callback.owner == owner;
			}), m_postPoseCallbacks.end());
	}

	void SkinnedTransformManager::updateMatrix(
		SkinnedTransform* st,
		const std::vector<glm::mat4>& mats)
	{
		if (st == nullptr || m_bones == nullptr || mats.empty())
		{
			return;
		}
		m_bones->update(
			st->getSkeletonOff(),
			mats.data(),
			mats.size() * sizeof(glm::mat4));
	}

	bool SkinnedTransformManager::applyExternalGlobalPose(
		SkinnedTransform* st,
		const std::vector<glm::mat4>& globalMatrices)
	{
		if (st == nullptr ||
			!st->setExternalGlobalPose(globalMatrices))
		{
			return false;
		}
		updateMatrix(st, st->getBoneMatrices());
		return true;
	}

	void SkinnedTransformManager::destroySkinnedTransform(
		SkinnedTransform* st)
	{
		if (st == nullptr)
		{
			return;
		}

		m_transforms.erase(
			std::remove(m_transforms.begin(), m_transforms.end(), st),
			m_transforms.end());
		m_postPoseCallbacks.erase(std::remove_if(m_postPoseCallbacks.begin(),
			m_postPoseCallbacks.end(), [st](const PostPoseCallback& callback)
			{
				return callback.transform == st;
			}), m_postPoseCallbacks.end());
		if (m_bones != nullptr)
		{
			m_bones->free(st->getSkeletonOff());
		}
		m_pool.deleteObject(st);
	}

	void SkinnedTransformManager::setupMaterials(
		SkinnedTransform* st,
		Materials* mat)
	{
		if (st == nullptr ||
			mat == nullptr ||
			m_pc == nullptr ||
			m_pc->gdm == nullptr ||
			m_bones == nullptr ||
			m_pc->gdm->str_default_material == mat)
		{
			return;
		}

		std::vector<unsigned int>& additionalBuffers =
			mat->getAditionalSSBO();
		additionalBuffers.clear();
		additionalBuffers.push_back(m_bones->getBufferID());
		additionalBuffers.push_back(st->getSkeletonBufferID());

		GraphiquePipeline* pipeline = mat->getPipeline();
		if (pipeline == m_pc->gdm->str_default_pipeline_forward_transparency)
		{
			mat->setPipeline(
				m_pc->gdm->str_skinned_pipeline_transparency);
		}
		else if (pipeline == nullptr ||
			pipeline == m_pc->gdm->str_default_pipeline)
		{
			mat->setPipeline(m_pc->gdm->str_skinned_pipeline);
		}
		mat->setShadowPipeline(
			m_pc->gdm->str_skinned_pipeline_shadow);
	}
}
