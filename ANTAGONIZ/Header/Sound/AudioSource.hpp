#ifndef __AUDIO_SOURCE__
#define __AUDIO_SOURCE__

#include <cfloat>

#include "GObject.hpp"

namespace Ge
{	
	class SoundBuffer;
	class AudioSource : public GObject
	{
	public:
		AudioSource(SoundBuffer* sb, std::string name = "AudioSource", bool music = false);
		void play();
		void pause();
		void stop();
		void setPitch(float pitch);
		void setGain(float gain);
		void setBusGain(float gain);
		bool isMusic() const;
		void setRelative(bool state3D);
		float getPitch() const;
		float getGain() const;
		float getTime() const;
		void setTime(float t);
		int SourceState();
		void setPosition(glm::vec3 pos) override;
		void setRotation(glm::quat rot) override;
		void setEulerAngles(glm::vec3 eul) override;
		void setVelocity(glm::vec3 vel);
		glm::vec3 getVelocity() const;
		void setLoop(bool state);
		bool getLoop() const;
		SoundBuffer * getSoundBuffer() const;
		void setSoundBuffer(SoundBuffer* newBuffer);
		bool isPlaying();
		void setRolloffFactor(float rolloffFactor);
		float getRolloffFactor() const;
		void setMaxDistance(float maxDistance);
		float getMaxDistance() const;
		void setRefDistance(float refDistance);
		float getRefDistance() const;
		void mapMemory() override;
		void onGUI() override;
		void addWaterFilter(float gain = 0.7f,float hf = 0.1f);
		void clearFilter();
		~AudioSource();
	private:
		SoundBuffer* m_sb = nullptr;
		unsigned int m_sourceID = 0;
		float m_pitch = 1.0f;
		float m_gain = 1.0f;
		float m_busGain = 1.0f;
		bool m_music = false;
		glm::vec3 m_velocity;
		bool m_loop = false;
		unsigned int m_filter = -1;

		float orientation[6] = { 0.0f, 0.0f, -1.0f, 0.0f, -1.0f, 0.0f };
		float m_rolloffFactor = 1.0f;   // Rolloff factor
		float m_maxDistance = FLT_MAX;   // Max distance for attenuation
		float m_refDistance = 1.0f;		
	};
}
#endif //!__AUDIO_SOURCE__
