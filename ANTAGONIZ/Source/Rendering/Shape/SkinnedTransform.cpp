#include <glcore.hpp>
#include "SkinnedTransform.hpp"

#include <algorithm>
#include <cmath>
#include <functional>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include "ShapeBufferSkeleton.hpp"

namespace
{
	glm::mat4 composePose(const BonePose& pose)
	{
		return
			glm::translate(glm::mat4(1.0f), pose.translation) *
			glm::mat4_cast(glm::normalize(pose.rotation)) *
			glm::scale(glm::mat4(1.0f), pose.scale);
	}

	bool decomposePoseMatrix(const glm::mat4& matrix, BonePose& pose)
	{
		pose.translation = glm::vec3(matrix[3]);
		pose.scale = glm::vec3(
			glm::length(glm::vec3(matrix[0])),
			glm::length(glm::vec3(matrix[1])),
			glm::length(glm::vec3(matrix[2])));
		if (pose.scale.x <= 0.000001f ||
			pose.scale.y <= 0.000001f ||
			pose.scale.z <= 0.000001f)
		{
			return false;
		}
		glm::mat3 rotation(1.0f);
		rotation[0] = glm::vec3(matrix[0]) / pose.scale.x;
		rotation[1] = glm::vec3(matrix[1]) / pose.scale.y;
		rotation[2] = glm::vec3(matrix[2]) / pose.scale.z;
		if (glm::determinant(rotation) < 0.0f)
		{
			pose.scale.x = -pose.scale.x;
			rotation[0] = -rotation[0];
		}
		pose.rotation = glm::normalize(glm::quat_cast(rotation));
		return std::isfinite(pose.translation.x) &&
			std::isfinite(pose.translation.y) &&
			std::isfinite(pose.translation.z) &&
			std::isfinite(pose.rotation.x) &&
			std::isfinite(pose.rotation.y) &&
			std::isfinite(pose.rotation.z) &&
			std::isfinite(pose.rotation.w);
	}

	bool sampleAnimation(
		const Animation& animation,
		size_t boneCount,
		double time,
		std::vector<BonePose>& destination)
	{
		if (animation.sampleCount == 0 ||
			boneCount == 0 ||
			animation.samples.size() <
				static_cast<size_t>(animation.sampleCount) * boneCount)
		{
			return false;
		}

		destination.resize(boneCount);
		const double duration = animation.duration();
		const double clampedTime =
			duration > 0.0 ? glm::clamp(time, 0.0, duration) : 0.0;
		const double framePosition =
			clampedTime * static_cast<double>(animation.sampleRate);
		const uint32_t firstFrame = (std::min)(
			static_cast<uint32_t>(std::floor(framePosition)),
			animation.sampleCount - 1u);
		const uint32_t secondFrame =
			(std::min)(firstFrame + 1u, animation.sampleCount - 1u);
		const float blend =
			firstFrame == secondFrame
				? 0.0f
				: static_cast<float>(
					framePosition - static_cast<double>(firstFrame));

		const size_t firstOffset = static_cast<size_t>(firstFrame) * boneCount;
		const size_t secondOffset = static_cast<size_t>(secondFrame) * boneCount;
		for (size_t bone = 0; bone < boneCount; ++bone)
		{
			const BonePose& first = animation.samples[firstOffset + bone];
			BonePose second = animation.samples[secondOffset + bone];
			if (glm::dot(first.rotation, second.rotation) < 0.0f)
			{
				second.rotation = -second.rotation;
			}

			BonePose& result = destination[bone];
			result.translation =
				glm::mix(first.translation, second.translation, blend);
			result.scale = glm::mix(first.scale, second.scale, blend);
			result.rotation = glm::normalize(
				glm::slerp(first.rotation, second.rotation, blend));
		}
		return true;
	}

	bool advanceAnimationTime(
		double& time,
		double duration,
		float deltaTime,
		float playbackSpeed,
		bool loop)
	{
		if (duration <= 0.0)
		{
			time = 0.0;
			return true;
		}

		time +=
			static_cast<double>(deltaTime) *
			static_cast<double>(playbackSpeed);
		if (loop)
		{
			time = std::fmod(time, duration);
			if (time < 0.0)
			{
				time += duration;
			}
			return false;
		}

		if (time >= duration)
		{
			time = duration;
			return true;
		}
		if (time <= 0.0 && playbackSpeed < 0.0f)
		{
			time = 0.0;
			return true;
		}
		return false;
	}
}

namespace Ge
{
	SkinnedTransform::SkinnedTransform(
		ShapeBufferSkeleton* sbs,
		size_t offBone)
		: m_shapeBufferSkeleton(sbs),
		  m_offsetSID(offBone)
	{
		m_sid.offMatrix =
			static_cast<unsigned int>(offBone / sizeof(glm::mat4));
		if (sbs != nullptr) m_armIK.bind(sbs->getSkeleton());

		glGenBuffers(1, &m_ssboSkeletonID);
		glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ssboSkeletonID);
		glBufferData(
			GL_SHADER_STORAGE_BUFFER,
			sizeof(SkeletonID),
			&m_sid,
			GL_STATIC_DRAW);
	}

	SkinnedTransform::~SkinnedTransform()
	{
		glDeleteBuffers(1, &m_ssboSkeletonID);
	}

	ShapeBufferSkeleton* SkinnedTransform::getBuffer() const
	{
		return m_shapeBufferSkeleton;
	}

	void SkinnedTransform::mapMemory()
	{
	}

	SkeletonID SkinnedTransform::getSkeletonData() const
	{
		return m_sid;
	}

	size_t SkinnedTransform::getSkeletonOff() const
	{
		return m_offsetSID;
	}

	unsigned int SkinnedTransform::getSkeletonBufferID() const
	{
		return m_ssboSkeletonID;
	}

	bool SkinnedTransform::playAnimation(
		size_t animationIndex,
		bool loop,
		float playbackSpeed)
	{
		if (m_shapeBufferSkeleton == nullptr ||
			animationIndex >=
				m_shapeBufferSkeleton->getAnimations().size())
		{
			return false;
		}

		m_primaryAnimation = static_cast<int>(animationIndex);
		m_secondaryAnimation = -1;
		m_primaryTime = 0.0;
		m_secondaryTime = 0.0;
		m_primaryWeight = 1.0f;
		m_secondaryWeight = 0.0f;
		m_playbackSpeed = playbackSpeed;
		m_loop = loop;
		m_playing = true;
		m_poseDirty = true;
		m_transitionActive = false;
		m_transitionDuration = 0.0f;
		m_transitionElapsed = 0.0f;
		m_transitionSourceMatrices.clear();
		return true;
	}

	bool SkinnedTransform::blendAnimations(
		size_t firstAnimation,
		size_t secondAnimation,
		float firstWeight,
		float secondWeight,
		bool loop,
		float playbackSpeed)
	{
		if (m_shapeBufferSkeleton == nullptr)
		{
			return false;
		}

		const size_t animationCount =
			m_shapeBufferSkeleton->getAnimations().size();
		if (firstAnimation >= animationCount ||
			secondAnimation >= animationCount)
		{
			return false;
		}

		firstWeight = (std::max)(0.0f, firstWeight);
		secondWeight = (std::max)(0.0f, secondWeight);
		const float totalWeight = firstWeight + secondWeight;
		if (totalWeight <= 0.000001f)
		{
			return false;
		}

		m_primaryAnimation = static_cast<int>(firstAnimation);
		m_secondaryAnimation = static_cast<int>(secondAnimation);
		m_primaryTime = 0.0;
		m_secondaryTime = 0.0;
		m_primaryWeight = firstWeight / totalWeight;
		m_secondaryWeight = secondWeight / totalWeight;
		m_playbackSpeed = playbackSpeed;
		m_loop = loop;
		m_playing = true;
		m_poseDirty = true;
		m_transitionActive = false;
		m_transitionDuration = 0.0f;
		m_transitionElapsed = 0.0f;
		m_transitionSourceMatrices.clear();
		return true;
	}

	bool SkinnedTransform::transitionToAnimation(
		size_t animationIndex,
		float transitionDuration,
		bool loop,
		float playbackSpeed)
	{
		if (m_shapeBufferSkeleton == nullptr)
		{
			return false;
		}

		const std::vector<Animation>& animations =
			m_shapeBufferSkeleton->getAnimations();
		if (animationIndex >= animations.size())
		{
			return false;
		}
		if (!m_playing || m_primaryAnimation < 0 ||
			static_cast<size_t>(m_primaryAnimation) >= animations.size())
		{
			return playAnimation(animationIndex, loop, playbackSpeed);
		}

		const int targetAnimation = static_cast<int>(animationIndex);
		m_loop = loop;
		m_playbackSpeed = playbackSpeed;
		transitionDuration = (std::max)(0.0f, transitionDuration);

		if (m_transitionActive &&
			targetAnimation == m_secondaryAnimation)
		{
			m_transitionDuration = transitionDuration;
			m_transitionElapsed =
				m_secondaryWeight * m_transitionDuration;
			return true;
		}

		if (m_transitionActive &&
			targetAnimation == m_primaryAnimation &&
			m_secondaryAnimation >= 0 &&
			m_transitionSourceMatrices.empty())
		{
			// Inverse le fondu en conservant exactement la pose actuelle:
			// mix(A, B, w) == mix(B, A, 1 - w).
			std::swap(m_primaryAnimation, m_secondaryAnimation);
			std::swap(m_primaryTime, m_secondaryTime);
			m_secondaryWeight = 1.0f - m_secondaryWeight;
			m_primaryWeight = 1.0f - m_secondaryWeight;
			m_transitionDuration = transitionDuration;
			m_transitionElapsed =
				m_secondaryWeight * m_transitionDuration;
			m_poseDirty = true;
		}
		else if (targetAnimation != m_primaryAnimation ||
			(m_transitionActive && !m_transitionSourceMatrices.empty()))
		{
			if (m_transitionActive)
			{
				// Freeze the exact mixed pose currently displayed. The next fade
				// starts from it, so A->B interrupted by C cannot jump back to A.
				m_transitionSourceMatrices = m_localMatrices;
			}
			else
			{
				m_transitionSourceMatrices.clear();
			}
			const Animation& source =
				animations[static_cast<size_t>(m_primaryAnimation)];
			const Animation& target = animations[animationIndex];
			const double sourceDuration = source.duration();
			const double targetDuration = target.duration();
			double phase = 0.0;
			if (sourceDuration > 0.0)
			{
				phase = std::fmod(m_primaryTime, sourceDuration) /
					sourceDuration;
				if (phase < 0.0)
				{
					phase += 1.0;
				}
			}

			m_secondaryAnimation = targetAnimation;
			m_secondaryTime = phase * targetDuration;
			m_primaryWeight = 1.0f;
			m_secondaryWeight = 0.0f;
			m_transitionDuration = transitionDuration;
			m_transitionElapsed = 0.0f;
			m_transitionActive = true;
			m_poseDirty = true;
		}
		else
		{
			return true;
		}

		if (m_transitionDuration <= 0.000001f)
		{
			m_primaryAnimation = m_secondaryAnimation;
			m_primaryTime = m_secondaryTime;
			m_secondaryAnimation = -1;
			m_secondaryTime = 0.0;
			m_primaryWeight = 1.0f;
			m_secondaryWeight = 0.0f;
			m_transitionActive = false;
			m_transitionSourceMatrices.clear();
		}
		m_playing = true;
		return true;
	}

	void SkinnedTransform::stopAnimation()
	{
		m_playing = false;
		m_armIK.clear(true);
		m_transitionSourceMatrices.clear();
	}

	void SkinnedTransform::setPlaybackSpeed(float playbackSpeed)
	{
		m_playbackSpeed = playbackSpeed;
	}

	bool SkinnedTransform::isAnimationPlaying() const
	{
		return m_playing;
	}

	int SkinnedTransform::getPrimaryAnimation() const
	{
		return m_primaryAnimation;
	}

	int SkinnedTransform::getSecondaryAnimation() const
	{
		return m_secondaryAnimation;
	}

	float SkinnedTransform::getSecondaryWeight() const
	{
		return m_secondaryWeight;
	}

	const std::vector<glm::mat4>& SkinnedTransform::getGlobalPose() const
	{
		return m_globalMatrices;
	}

	void SkinnedTransform::setArmIKTarget(std::size_t side, const ArmIKTarget& target)
	{
		if (m_shapeBufferSkeleton == nullptr) return;
		m_armIK.setTarget(side, target,
			getModelMatrix() * m_shapeBufferSkeleton->getSkeleton().inverseBindTransform);
		m_poseDirty = true;
	}

	void SkinnedTransform::clearArmIKTargets(bool immediate)
	{
		if (m_armIK.needsEvaluation()) m_poseDirty = true;
		m_armIK.clear(immediate);
	}

	bool SkinnedTransform::getArmIKReference(std::size_t side, glm::vec3& shoulder, float& reach) const
	{
		return m_shapeBufferSkeleton != nullptr && m_armIK.reference(side, m_globalMatrices,
			getModelMatrix() * m_shapeBufferSkeleton->getSkeleton().inverseBindTransform,
			shoulder, reach);
	}

	bool SkinnedTransform::setExternalGlobalPose(
		const std::vector<glm::mat4>& globalMatrices)
	{
		if (m_shapeBufferSkeleton == nullptr ||
			globalMatrices.size() !=
				m_shapeBufferSkeleton->getSkeleton().bones.size())
		{
			return false;
		}
		m_playing = false;
		m_poseDirty = false;
		m_transitionActive = false;
		m_transitionSourceMatrices.clear();
		m_globalMatrices = globalMatrices;
		m_armIK.clear(true);
		rebuildSkinningMatrices();
		return true;
	}

	const std::vector<glm::mat4>& SkinnedTransform::getBoneMatrices() const
	{
		return m_boneMatrices;
	}

	void SkinnedTransform::rebuildSkinningMatrices()
	{
		if (m_shapeBufferSkeleton == nullptr)
		{
			m_boneMatrices.clear();
			return;
		}
		const Skeleton& skeleton = m_shapeBufferSkeleton->getSkeleton();
		const size_t boneCount = skeleton.bones.size();
		if (m_globalMatrices.size() != boneCount)
		{
			m_boneMatrices.clear();
			return;
		}
		m_boneMatrices.resize(boneCount);
		for (size_t bone = 0; bone < boneCount; ++bone)
		{
			m_boneMatrices[bone] =
				skeleton.inverseBindTransform *
				m_globalMatrices[bone] *
				skeleton.bones[bone].inverseBindPose *
				skeleton.bindTransform;
		}
	}

	bool SkinnedTransform::evaluateAnimation(float deltaTime)
	{
		if ((!m_playing && !m_poseDirty && !m_armIK.needsEvaluation()) ||
			m_shapeBufferSkeleton == nullptr ||
			m_primaryAnimation < 0)
		{
			return false;
		}

		const std::vector<Animation>& animations =
			m_shapeBufferSkeleton->getAnimations();
		if (static_cast<size_t>(m_primaryAnimation) >= animations.size())
		{
			m_playing = false;
			m_poseDirty = false;
			return false;
		}

		const Animation& primary =
			animations[static_cast<size_t>(m_primaryAnimation)];
		const bool primaryEnded = m_playing
			? advanceAnimationTime(
				m_primaryTime,
				primary.duration(),
				deltaTime,
				m_playbackSpeed,
				m_loop)
			: false;

		if (m_transitionActive)
		{
			m_transitionElapsed += (std::max)(0.0f, deltaTime);
			m_secondaryWeight =
				m_transitionDuration > 0.000001f
					? glm::clamp(
						m_transitionElapsed / m_transitionDuration,
						0.0f,
						1.0f)
					: 1.0f;
			m_primaryWeight = 1.0f - m_secondaryWeight;
		}

		std::vector<BonePose> primaryPose;
		if (!sampleAnimation(
				primary,
				m_shapeBufferSkeleton->getSkeleton().bones.size(),
				m_primaryTime,
				primaryPose))
		{
			m_playing = false;
			m_poseDirty = false;
			return false;
		}
		if (m_transitionActive &&
			m_transitionSourceMatrices.size() == primaryPose.size())
		{
			for (size_t bone = 0; bone < primaryPose.size(); ++bone)
			{
				BonePose frozenPose;
				if (decomposePoseMatrix(
						m_transitionSourceMatrices[bone],
						frozenPose))
				{
					primaryPose[bone] = frozenPose;
				}
			}
		}

		bool secondaryEnded = true;
		std::vector<BonePose> secondaryPose;
		const bool hasSecondary =
			m_secondaryAnimation >= 0 &&
			static_cast<size_t>(m_secondaryAnimation) < animations.size() &&
			m_secondaryWeight > 0.0f;
		if (hasSecondary)
		{
			const Animation& secondary =
				animations[static_cast<size_t>(m_secondaryAnimation)];
			secondaryEnded = m_playing
				? advanceAnimationTime(
					m_secondaryTime,
					secondary.duration(),
					deltaTime,
					m_playbackSpeed,
					m_loop)
				: false;
			if (!sampleAnimation(
					secondary,
					m_shapeBufferSkeleton->getSkeleton().bones.size(),
					m_secondaryTime,
					secondaryPose))
			{
				secondaryPose.clear();
			}
		}

		const Skeleton& skeleton = m_shapeBufferSkeleton->getSkeleton();
		const size_t boneCount = skeleton.bones.size();
		m_localMatrices.resize(boneCount);
		m_globalMatrices.resize(boneCount);
		m_boneMatrices.resize(boneCount);
		m_resolveState.assign(boneCount, 0u);

		for (size_t bone = 0; bone < boneCount; ++bone)
		{
			BonePose blended = primaryPose[bone];
			if (!secondaryPose.empty())
			{
				BonePose secondary = secondaryPose[bone];
				if (glm::dot(blended.rotation, secondary.rotation) < 0.0f)
				{
					secondary.rotation = -secondary.rotation;
				}
				blended.translation = glm::mix(
					blended.translation,
					secondary.translation,
					m_secondaryWeight);
				blended.scale = glm::mix(
					blended.scale,
					secondary.scale,
					m_secondaryWeight);
				blended.rotation = glm::normalize(
					glm::slerp(
						blended.rotation,
						secondary.rotation,
						m_secondaryWeight));
			}
			m_localMatrices[bone] = composePose(blended);
		}

		std::function<void(size_t)> resolveGlobal =
			[&](size_t bone)
			{
				if (m_resolveState[bone] == 2u)
				{
					return;
				}
				if (m_resolveState[bone] == 1u)
				{
					// Un FBX valide ne doit pas contenir de cycle.
					m_globalMatrices[bone] = m_localMatrices[bone];
					m_resolveState[bone] = 2u;
					return;
				}

				m_resolveState[bone] = 1u;
				const int parent = skeleton.bones[bone].parent;
				if (parent >= 0 &&
					static_cast<size_t>(parent) < boneCount &&
					static_cast<size_t>(parent) != bone)
				{
					resolveGlobal(static_cast<size_t>(parent));
					m_globalMatrices[bone] =
						m_globalMatrices[static_cast<size_t>(parent)] *
						m_localMatrices[bone];
				}
				else
				{
					m_globalMatrices[bone] = m_localMatrices[bone];
				}
				m_resolveState[bone] = 2u;
			};

		for (size_t bone = 0; bone < boneCount; ++bone)
		{
			resolveGlobal(bone);
		}
		// Les vertices sont dans l'espace local commun des meshes, tandis
		// que les os FBX sont exprimes dans l'espace global de la scene.
		// Keep m_localMatrices as the unmodified animation pose: transitions and
		// the next frame must never accumulate the previous IK correction.
		m_armIK.apply(m_globalMatrices,
			getModelMatrix() * skeleton.inverseBindTransform, deltaTime);
		rebuildSkinningMatrices();

		if (m_transitionActive && m_secondaryWeight >= 1.0f)
		{
			m_primaryAnimation = m_secondaryAnimation;
			m_primaryTime = m_secondaryTime;
			m_secondaryAnimation = -1;
			m_secondaryTime = 0.0;
			m_primaryWeight = 1.0f;
			m_secondaryWeight = 0.0f;
			m_transitionActive = false;
			m_transitionDuration = 0.0f;
			m_transitionElapsed = 0.0f;
			m_transitionSourceMatrices.clear();
		}

		if (!m_loop && primaryEnded && (!hasSecondary || secondaryEnded))
		{
			m_playing = false;
		}
		m_poseDirty = false;
		return true;
	}
}
