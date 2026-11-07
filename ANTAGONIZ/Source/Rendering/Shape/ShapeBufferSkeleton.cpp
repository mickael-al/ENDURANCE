#include "glcore.hpp"
#include "ShapeBufferSkeleton.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "OpenFBX/src/ofbx.h"
#include "Debug.hpp"
#include "ShapeBufferBase.hpp"

namespace
{
	std::string getAnimationBonePath(const ofbx::Object* object)
	{
		if (object == nullptr)
		{
			return "";
		}

		std::vector<std::string> pathElements;
		while (object != nullptr)
		{
			if (object->name[0] != '\0')
			{
				pathElements.emplace_back(object->name);
			}
			object = object->getParent();
		}
		std::reverse(pathElements.begin(), pathElements.end());

		std::string path;
		for (const std::string& element : pathElements)
		{
			if (!path.empty())
			{
				path += "/";
			}
			path += element;
		}
		return path;
	}

	bool finiteMatrix(const glm::mat4& matrix)
	{
		for (int column = 0; column < 4; ++column)
			for (int row = 0; row < 4; ++row)
				if (!std::isfinite(matrix[column][row])) return false;
		return true;
	}

	glm::mat4 animationRootSpace(const Bone& bone, const ofbx::Object& object)
	{
		const ofbx::Object* parent = object.getParent();
		if (parent != nullptr && parent->getType() != ofbx::Object::Type::ROOT)
			return glm::mat4(glm::make_mat4(parent->getGlobalTransform().m));

		// Standalone clips can omit the model's non-bone armature node. Its
		// coordinate basis (units, axes and placement) still belongs to the
		// skeleton. Match the two default root poses once, then retain every
		// animated root displacement relative to that basis.
		const glm::mat4 sourceRest = glm::make_mat4(object.getGlobalTransform().m);
		const float determinant = glm::determinant(sourceRest);
		if (!finiteMatrix(sourceRest) || !finiteMatrix(bone.worldPoseObj) ||
			!std::isfinite(determinant) || std::abs(determinant) <= 0.0000001f)
			return glm::mat4(1.0f);
		const glm::mat4 basis = bone.worldPoseObj * glm::inverse(sourceRest);
		return finiteMatrix(basis) ? basis : glm::mat4(1.0f);
	}

	BonePose decomposePose(const glm::mat4& matrix)
	{
		BonePose pose;
		pose.translation = glm::vec3(matrix[3]);

		glm::vec3 axisX = glm::vec3(matrix[0]);
		glm::vec3 axisY = glm::vec3(matrix[1]);
		glm::vec3 axisZ = glm::vec3(matrix[2]);
		pose.scale = glm::vec3(
			glm::length(axisX),
			glm::length(axisY),
			glm::length(axisZ));

		const float epsilon = 0.000001f;
		if (pose.scale.x > epsilon) axisX /= pose.scale.x;
		if (pose.scale.y > epsilon) axisY /= pose.scale.y;
		if (pose.scale.z > epsilon) axisZ /= pose.scale.z;

		glm::mat3 rotationBasis(axisX, axisY, axisZ);
		if (glm::determinant(rotationBasis) < 0.0f)
		{
			pose.scale.x = -pose.scale.x;
			rotationBasis[0] = -rotationBasis[0];
		}

		pose.rotation = glm::normalize(glm::quat_cast(rotationBasis));
		if (!std::isfinite(pose.rotation.w) ||
			!std::isfinite(pose.rotation.x) ||
			!std::isfinite(pose.rotation.y) ||
			!std::isfinite(pose.rotation.z))
		{
			pose.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		}
		return pose;
	}

	void copyCurve(
		const ofbx::AnimationCurve* curve,
		std::vector<glm::vec2>& destination,
		Animation& animation)
	{
		if (curve == nullptr || curve->getKeyCount() <= 0)
		{
			return;
		}

		const int keyCount = curve->getKeyCount();
		const ofbx::i64* keyTimes = curve->getKeyTime();
		const float* keyValues = curve->getKeyValue();
		destination.resize(static_cast<size_t>(keyCount));

		for (int key = 0; key < keyCount; ++key)
		{
			const double time = ofbx::fbxTimeToSeconds(keyTimes[key]);
			animation.max_duration = (std::max)(animation.max_duration, time);
			animation.min_duration = (std::min)(animation.min_duration, time);
			destination[static_cast<size_t>(key)] =
				glm::vec2(keyValues[key], static_cast<float>(time));
		}
	}

	uint16_t findBone(
		const Skeleton& skeleton,
		const ofbx::Object* object,
		bool& found)
	{
		found = false;
		if (object == nullptr)
		{
			return 0;
		}

		const auto pathIt = skeleton.pathToID.find(getAnimationBonePath(object));
		if (pathIt != skeleton.pathToID.end())
		{
			found = true;
			return pathIt->second;
		}

		const auto nameIt = skeleton.nameToID.find(object->name);
		if (nameIt != skeleton.nameToID.end())
		{
			found = true;
			return nameIt->second;
		}
		return 0;
	}

	ofbx::Vec3 evaluateCurveNode(
		const ofbx::AnimationCurveNode* curve,
		const ofbx::Vec3& fallback,
		double time)
	{
		return curve != nullptr ? curve->getNodeLocalTransform(time) : fallback;
	}
}

namespace Ge
{
	ShapeBufferSkeleton::ShapeBufferSkeleton(
		std::vector<std::vector<Vertex>>& vertices,
		std::vector<std::vector<uint32_t>>& index,
		Skeleton& skeleton,
		GraphicsDataMisc* gdm)
		: m_skeleton(skeleton)
	{
		for (size_t i = 0; i < vertices.size(); ++i)
		{
			m_shapes.push_back(new ShapeBufferBase(vertices[i], index[i], gdm));
		}
	}

	ShapeBufferSkeleton::~ShapeBufferSkeleton()
	{
		for (ShapeBufferBase* shape : m_shapes)
		{
			delete shape;
		}
		m_shapes.clear();
		m_animations.clear();
	}

	void ShapeBufferSkeleton::SetupVAO(unsigned int program)
	{
		for (ShapeBufferBase* shape : m_shapes)
		{
			shape->SetupVAO(program);
		}
	}

	unsigned int ShapeBufferSkeleton::getIndiceSize() const
	{
		unsigned int indexCount = 0;
		for (const ShapeBufferBase* shape : m_shapes)
		{
			indexCount += shape->getIndiceSize();
		}
		return indexCount;
	}

	const std::vector<ShapeBufferBase*>& ShapeBufferSkeleton::getShapes() const
	{
		return m_shapes;
	}

	const Skeleton& ShapeBufferSkeleton::getSkeleton() const
	{
		return m_skeleton;
	}

	const std::vector<Animation>& ShapeBufferSkeleton::getAnimations() const
	{
		return m_animations;
	}

	int ShapeBufferSkeleton::findAnimation(const std::string& name) const
	{
		for (size_t i = 0; i < m_animations.size(); ++i)
		{
			if (m_animations[i].name == name)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}

	int ShapeBufferSkeleton::loadAnimationClip(const char* path)
	{
		const size_t firstNewAnimation = m_animations.size();
		if (!loadAnimation(path) || firstNewAnimation >= m_animations.size())
		{
			return -1;
		}
		return static_cast<int>(firstNewAnimation);
	}

	bool ShapeBufferSkeleton::loadAnimation(const char* path)
	{
		FILE* file = fopen(path, "rb");
		if (file == nullptr)
		{
			Debug::Warn("Cannot load animation: %s", path);
			return false;
		}

		fseek(file, 0, SEEK_END);
		const long fileSize = ftell(file);
		fseek(file, 0, SEEK_SET);
		if (fileSize <= 0)
		{
			fclose(file);
			Debug::Warn("Animation FBX is empty: %s", path);
			return false;
		}

		auto* content = new ofbx::u8[static_cast<size_t>(fileSize)];
		const size_t readSize = fread(content, 1, static_cast<size_t>(fileSize), file);
		if (readSize != static_cast<size_t>(fileSize))
		{
			delete[] content;
			fclose(file);
			Debug::Warn("Cannot read animation FBX: %s", path);
			return false;
		}

		const ofbx::LoadFlags flags =
			ofbx::LoadFlags::IGNORE_GEOMETRY |
			ofbx::LoadFlags::IGNORE_BLEND_SHAPES |
			ofbx::LoadFlags::IGNORE_CAMERAS |
			ofbx::LoadFlags::IGNORE_LIGHTS |
			ofbx::LoadFlags::IGNORE_TEXTURES |
			ofbx::LoadFlags::IGNORE_SKIN |
			ofbx::LoadFlags::IGNORE_MATERIALS |
			ofbx::LoadFlags::IGNORE_POSES |
			ofbx::LoadFlags::IGNORE_VIDEOS;

		ofbx::IScene* scene =
			ofbx::load(content, static_cast<int>(fileSize), static_cast<ofbx::u16>(flags));
		if (scene == nullptr)
		{
			delete[] content;
			fclose(file);
			Debug::Warn("Failed to parse animation FBX %s: %s", path, ofbx::getError());
			return false;
		}

		const size_t boneCount = m_skeleton.bones.size();
		std::vector<BonePose> bindLocalPose(boneCount);
		for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
		{
			const Bone& bone = m_skeleton.bones[boneIndex];
			glm::mat4 localBind = bone.bindPose;
			if (bone.parent >= 0 &&
				static_cast<size_t>(bone.parent) < boneCount)
			{
				localBind =
					glm::inverse(m_skeleton.bones[static_cast<size_t>(bone.parent)].bindPose) *
					bone.bindPose;
			}
			bindLocalPose[boneIndex] = decomposePose(localBind);
		}

		const size_t previousAnimationCount = m_animations.size();
		const unsigned int stackCount = scene->getAnimationStackCount();
		for (unsigned int stackIndex = 0; stackIndex < stackCount; ++stackIndex)
		{
			const ofbx::AnimationStack* stack = scene->getAnimationStack(stackIndex);
			const ofbx::AnimationLayer* layer = stack != nullptr ? stack->getLayer(0) : nullptr;
			if (stack == nullptr || layer == nullptr)
			{
				continue;
			}

			Animation animation;
			animation.name = stack->name;
			if (animation.name.empty())
			{
				animation.name = "Animation_" + std::to_string(stackIndex);
			}
			animation.max_duration = (std::numeric_limits<double>::lowest)();
			animation.min_duration = (std::numeric_limits<double>::max)();
			animation.bones.resize(boneCount);

			std::vector<const ofbx::Object*> animationNodes(boneCount, nullptr);
			size_t matchedBoneCount = 0;
			const ofbx::Object* const* objects = scene->getAllObjects();
			const int objectCount = scene->getAllObjectCount();
			for (int objectIndex = 0; objectIndex < objectCount; ++objectIndex)
			{
				const ofbx::Object* object = objects[objectIndex];
				// Skin clusters and node attributes may share a bone's name.
				// Only hierarchy nodes carry the transform curves to sample.
				if (object == nullptr || !object->isNode()) continue;
				bool found = false;
				const uint16_t boneID = findBone(m_skeleton, object, found);
				if (!found || static_cast<size_t>(boneID) >= boneCount)
				{
					continue;
				}
				if (animationNodes[boneID] == nullptr)
				{
					++matchedBoneCount;
				}
				animationNodes[boneID] = object;

				BoneAnim& boneAnimation = animation.bones[boneID];
				const ofbx::AnimationCurveNode* translation =
					layer->getCurveNode(*object, "Lcl Translation");
				const ofbx::AnimationCurveNode* rotation =
					layer->getCurveNode(*object, "Lcl Rotation");
				const ofbx::AnimationCurveNode* scaling =
					layer->getCurveNode(*object, "Lcl Scaling");

				boneAnimation.T.resize(3);
				boneAnimation.R.resize(3);
				boneAnimation.S.resize(3);
				for (int axis = 0; axis < 3; ++axis)
				{
					copyCurve(
						translation != nullptr ? translation->getCurve(axis) : nullptr,
						boneAnimation.T[axis],
						animation);
					copyCurve(
						rotation != nullptr ? rotation->getCurve(axis) : nullptr,
						boneAnimation.R[axis],
						animation);
					copyCurve(
						scaling != nullptr ? scaling->getCurve(axis) : nullptr,
						boneAnimation.S[axis],
						animation);
				}
			}

			Debug::Info(
				"Animation %s matched %llu/%llu skeleton bones",
				animation.name.c_str(),
				static_cast<unsigned long long>(matchedBoneCount),
				static_cast<unsigned long long>(boneCount));
			if (matchedBoneCount != boneCount)
			{
				Debug::Warn(
					"Animation %s is missing %llu skeleton bones",
					animation.name.c_str(),
					static_cast<unsigned long long>(
						boneCount - matchedBoneCount));
			}

			if (animation.min_duration == (std::numeric_limits<double>::max)() ||
				animation.max_duration == (std::numeric_limits<double>::lowest)())
			{
				Debug::Warn(
					"Animation stack %s contains no keyframes",
					animation.name.c_str());
				continue;
			}

			std::vector<glm::mat4> rootSpaces(boneCount, glm::mat4(1.0f));
			for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
				if (m_skeleton.bones[boneIndex].parent < 0 && animationNodes[boneIndex] != nullptr)
					rootSpaces[boneIndex] = animationRootSpace(
						m_skeleton.bones[boneIndex], *animationNodes[boneIndex]);

			const float sceneFrameRate = scene->getSceneFrameRate();
			animation.sampleRate =
				std::isfinite(sceneFrameRate) && sceneFrameRate > 0.0f
					? glm::clamp(sceneFrameRate, 1.0f, 120.0f)
					: 30.0f;

			const double duration = animation.duration();
			animation.sampleCount =
				duration > 0.0
					? static_cast<uint32_t>(
						std::ceil(duration * animation.sampleRate)) + 1u
					: 1u;
			animation.samples.resize(
				static_cast<size_t>(animation.sampleCount) * boneCount);

			for (uint32_t frame = 0; frame < animation.sampleCount; ++frame)
			{
				const double localTime = duration > 0.0
					? (std::min)(
						static_cast<double>(frame) /
							static_cast<double>(animation.sampleRate),
						duration)
					: 0.0;
				const double sourceTime = animation.min_duration + localTime;

				for (size_t boneIndex = 0; boneIndex < boneCount; ++boneIndex)
				{
					BonePose pose = bindLocalPose[boneIndex];
					const ofbx::Object* object = animationNodes[boneIndex];
					if (object != nullptr)
					{
						const ofbx::AnimationCurveNode* translation =
							layer->getCurveNode(*object, "Lcl Translation");
						const ofbx::AnimationCurveNode* rotation =
							layer->getCurveNode(*object, "Lcl Rotation");
						const ofbx::AnimationCurveNode* scaling =
							layer->getCurveNode(*object, "Lcl Scaling");

						const ofbx::Vec3 localTranslation = evaluateCurveNode(
							translation,
							object->getLocalTranslation(),
							sourceTime);
						const ofbx::Vec3 localRotation = evaluateCurveNode(
							rotation,
							object->getLocalRotation(),
							sourceTime);
						const ofbx::Vec3 localScaling = evaluateCurveNode(
							scaling,
							object->getLocalScaling(),
							sourceTime);
						const ofbx::Matrix localMatrix =
							object->evalLocal(
								localTranslation,
								localRotation,
								localScaling);
						glm::mat4 sampledLocal = glm::make_mat4(localMatrix.m);
						if (m_skeleton.bones[boneIndex].parent < 0)
						{
							sampledLocal = rootSpaces[boneIndex] * sampledLocal;
						}
						pose = decomposePose(sampledLocal);
					}

					animation.samples[
						static_cast<size_t>(frame) * boneCount + boneIndex] = pose;
				}
			}

			m_animations.push_back(std::move(animation));
		}

		scene->destroy();
		delete[] content;
		fclose(file);

		const bool loaded = m_animations.size() > previousAnimationCount;
		if (!loaded)
		{
			Debug::Warn("No usable animation found in FBX: %s", path);
		}
		return loaded;
	}
}
