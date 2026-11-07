#ifndef __SOUND_MANAGER__
#define __SOUND_MANAGER__

#include "Initializer.hpp"
#include <iostream>
#include <vector>
#include <mutex>
#include "glm/glm.hpp"
#include "MemoryManager.hpp"

struct ALCdevice;
struct ALCcontext;
namespace Ge
{	
	class SoundBuffer;
	class AudioSource;
	class SoundManager final : public Initializer
	{
	public:
		SoundBuffer * createBuffer(const char * filepath, bool forceMono = false,
			bool retainAnalysisPcm = false);
		void releaseBuffer(SoundBuffer * sb);		
		AudioSource* createSource(SoundBuffer* sb, std::string name = "AudioSource", bool music = false);
		void releaseSource(AudioSource* as);
		void setListenerPosition(glm::vec3 position);
		void setListenerVelocity(glm::vec3 velocity);
		void setListenerDirection(glm::vec3 direction);		
		void setListenerGain(float volume);
		void setMusicVolume(float volume);
		void setSfxVolume(float volume);
		void setPaused(bool paused);
		bool isPaused() const;
		void setDistanceMode(int mode);
		int getDistanceMode() const;
	protected:
		friend class Engine;
		bool initialize();
		void release(); 
	private:
		MemoryPool<SoundBuffer> m_poolBuffer;
		MemoryPool<AudioSource> m_poolAudio;
		std::vector<SoundBuffer*> m_buffers;
		std::vector<AudioSource*> m_audios;
		std::vector<AudioSource*> m_pausedAudios;
		std::mutex m_audioMutex;
		ALCdevice* m_pDevice = nullptr;
		ALCcontext* m_pContext = nullptr;
		float m_orientation[6] = { 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 0.0f };
		float m_musicVolume = 1.0f;
		float m_sfxVolume = 1.0f;
		bool m_paused = false;
		int m_distanceMode = 0;//AL_NONE;AL_INVERSE_DISTANCE_CLAMPED
	};

};

#endif //!__SOUND_MANAGER__
