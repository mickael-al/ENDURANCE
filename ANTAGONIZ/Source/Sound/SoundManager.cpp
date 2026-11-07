#include "SoundManager.hpp"
#include "Debug.hpp"
#include "SoundTypeLoader.hpp"
#include "AL/al.h"
#include "AL/alc.h"
#include "SoundBuffer.hpp"
#include "AudioSource.hpp"
#include <vector>
#include <algorithm>

namespace Ge
{
	bool SoundManager::initialize()
	{
		m_pDevice = alcOpenDevice(nullptr);
		if (!m_pDevice)
		{
			Debug::Warn("Echec il y a aucun pheripherique audio!");
			return true;
		}
		m_pContext = alcCreateContext(m_pDevice, nullptr);
		if (!m_pContext)
		{
			Debug::Error("Echec de la creation du context audio!");
			alcCloseDevice(m_pDevice);
			m_pDevice = nullptr;
			return false;
		}
		if (alcMakeContextCurrent(m_pContext) == ALC_FALSE)
		{
			Debug::Error("Echec de l'activation du context audio!");
			alcDestroyContext(m_pContext);
			m_pContext = nullptr;
			alcCloseDevice(m_pDevice);
			m_pDevice = nullptr;
			return false;
		}
		Debug::INITSUCCESS("SoundManager");
		return true;
	}

	SoundBuffer * SoundManager::createBuffer(const char* filepath, bool forceMono,
		bool retainAnalysisPcm)
	{
		if (filepath == nullptr || filepath[0] == '\0')
		{
			Debug::Warn("SoundManager::createBuffer(): empty path");
			return nullptr;
		}
		{
			std::lock_guard<std::mutex> lock(m_audioMutex);
			if (m_pContext == nullptr)
			{
				Debug::Warn("SoundManager::createBuffer(): audio context is unavailable");
				return nullptr;
			}
		}
		uint64_t size;
		uint32_t frequency;
		ALenum format;
		double time;
		int8_t* buffer = SoundTypeLoader::LoadWavFormat(filepath,&size,&frequency,&format,&time,forceMono);
		
		if (buffer == nullptr)
		{
			Debug::Error("SoundBuffer nullptr");
			return nullptr;
		}

		std::lock_guard<std::mutex> lock(m_audioMutex);
		if (m_pContext == nullptr)
		{
			delete[] buffer;
			Debug::Warn("SoundManager::createBuffer(): audio context was released while loading");
			return nullptr;
		}
		SoundBuffer* sb = m_poolBuffer.newObject(
			size, frequency, format, buffer, time, retainAnalysisPcm);
		delete[] buffer;
		buffer = nullptr;
		m_buffers.push_back(sb);
		return sb;
	}

	void SoundManager::releaseBuffer(SoundBuffer* sb)
	{
		std::lock_guard<std::mutex> lock(m_audioMutex);
		auto it = std::find(m_buffers.begin(), m_buffers.end(), sb);
		if (it != m_buffers.end()) 
		{
			m_buffers.erase(it);
			m_poolBuffer.deleteObject(sb);			
		}
	}

	AudioSource* SoundManager::createSource(SoundBuffer* sb, std::string name, bool music)
	{
		if (sb == nullptr)
		{
			Debug::Warn("SoundManager::createSource(): null sound buffer");
			return nullptr;
		}
		std::lock_guard<std::mutex> lock(m_audioMutex);
		if (m_pContext == nullptr)
		{
			Debug::Warn("SoundManager::createSource(): audio context is unavailable");
			return nullptr;
		}
		AudioSource* audio = m_poolAudio.newObject(sb, name, music);
		audio->setBusGain(music ? m_musicVolume : m_sfxVolume);
		m_audios.push_back(audio);
		return audio;
	}

	void SoundManager::releaseSource(AudioSource* as)
	{
		std::lock_guard<std::mutex> lock(m_audioMutex);
		auto it = std::find(m_audios.begin(), m_audios.end(), as);
		if (it != m_audios.end())
		{
			m_pausedAudios.erase(
				std::remove(m_pausedAudios.begin(), m_pausedAudios.end(), as),
				m_pausedAudios.end());
			m_audios.erase(it);
			m_poolAudio.deleteObject(as);
		}
	}

	void SoundManager::setListenerPosition(glm::vec3 position)
	{
		alListener3f(AL_POSITION, position.x, position.y, position.z);
	}

	void SoundManager::setListenerVelocity(glm::vec3 velocity)
	{
		alListener3f(AL_VELOCITY, velocity.x, velocity.y, velocity.z);
	}

	void SoundManager::setListenerDirection(glm::vec3 direction)
	{
		m_orientation[0] = direction.x;
		m_orientation[1] = direction.y;
		m_orientation[2] = direction.z;
		// OpenAL expects the forward vector followed by the listener's up vector.
		// The engine is Y-up; using -Y mirrors the right axis and swaps stereo.
		m_orientation[3] = 0.0f;
		m_orientation[4] = 1.0f;
		m_orientation[5] = 0.0f;
		alListenerfv(AL_ORIENTATION, m_orientation);
	}

	void SoundManager::setListenerGain(float volume)
	{
		alListenerf(AL_GAIN, glm::clamp(volume, 0.0f, 1.0f));
	}

	void SoundManager::setMusicVolume(float volume)
	{
		std::lock_guard<std::mutex> lock(m_audioMutex);
		m_musicVolume = glm::clamp(volume, 0.0f, 1.0f);
		for (AudioSource* audio : m_audios)
		{
			if (audio != nullptr && audio->isMusic())
			{
				audio->setBusGain(m_musicVolume);
			}
		}
	}

	void SoundManager::setSfxVolume(float volume)
	{
		std::lock_guard<std::mutex> lock(m_audioMutex);
		m_sfxVolume = glm::clamp(volume, 0.0f, 1.0f);
		for (AudioSource* audio : m_audios)
		{
			if (audio != nullptr && !audio->isMusic())
			{
				audio->setBusGain(m_sfxVolume);
			}
		}
	}

	void SoundManager::setPaused(bool paused)
	{
		std::lock_guard<std::mutex> lock(m_audioMutex);
		if (m_paused == paused)
		{
			return;
		}

		m_paused = paused;
		if (paused)
		{
			m_pausedAudios.clear();
			for (AudioSource* audio : m_audios)
			{
				if (audio != nullptr && audio->isPlaying())
				{
					audio->pause();
					m_pausedAudios.push_back(audio);
				}
			}
			return;
		}

		for (AudioSource* audio : m_pausedAudios)
		{
			if (audio != nullptr &&
				std::find(m_audios.begin(), m_audios.end(), audio) != m_audios.end() &&
				audio->SourceState() == AL_PAUSED)
			{
				audio->play();
			}
		}
		m_pausedAudios.clear();
	}

	bool SoundManager::isPaused() const
	{
		return m_paused;
	}

	void SoundManager::setDistanceMode(int mode)
	{
		m_distanceMode = mode;
		alDistanceModel(mode);
	}

	int SoundManager::getDistanceMode() const
	{
		return m_distanceMode;
	}

	void SoundManager::release()
	{
		std::lock_guard<std::mutex> lock(m_audioMutex);
		for (int i = 0; i < m_audios.size(); i++)
		{
			m_poolAudio.deleteObject(m_audios[i]);
		}
		m_audios.clear();
		m_pausedAudios.clear();
		m_paused = false;
		for (int i = 0; i < m_buffers.size(); i++)
		{			
			m_poolBuffer.deleteObject(m_buffers[i]);
		}
		m_buffers.clear();
		if (m_pContext != nullptr)
		{
			alcMakeContextCurrent(nullptr);
			alcDestroyContext(m_pContext);
			m_pContext = nullptr;
		}
		if (m_pDevice != nullptr)
		{
			alcCloseDevice(m_pDevice);
			m_pDevice = nullptr;
		}
		Debug::RELEASESUCCESS("SoundManager");
	}
}

