#include "SoundBuffer.hpp"

namespace Ge
{
	SoundBuffer::SoundBuffer(uint64_t size, uint32_t frequency, ALenum format, int8_t* buffer,double time,
		bool retainAnalysisPcm)
	{
		alGenBuffers(1, &m_bufferID);
		alBufferData(m_bufferID, format, buffer, size, frequency);		
		m_time = time;
		if (retainAnalysisPcm)
		{
			const unsigned int channels = format == AL_FORMAT_MONO8 || format == AL_FORMAT_MONO16
				? 1u : format == AL_FORMAT_STEREO8 || format == AL_FORMAT_STEREO16 ? 2u : 0u;
			const unsigned int bits = format == AL_FORMAT_MONO8 || format == AL_FORMAT_STEREO8
				? 8u : 16u;
			m_analysisPcm = makeSoundAnalysisPcm(buffer, size, frequency, channels, bits);
		}
	}

	ALuint SoundBuffer::getbufferID() const
	{
		return m_bufferID;
	}

	float SoundBuffer::getTime() const
	{
		return m_time;
	}

	SoundBuffer::~SoundBuffer()
	{
		alDeleteBuffers(1, &m_bufferID);
	}
}
