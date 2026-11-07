#ifndef __SOUND_BUFFER__
#define __SOUND_BUFFER__

#include <iostream>
#include <stdint.h>
#include "AL/al.h"
#include "AL/alc.h"
#include "SoundPcm.hpp"

namespace Ge
{
	class SoundBuffer
	{
	public:
		SoundBuffer(uint64_t size, uint32_t frequency, ALenum format, int8_t* buffer,double time,
			bool retainAnalysisPcm = false);
		ALuint getbufferID() const;
		float getTime() const;
		// Optional compact copy made from the already decoded PCM at load time.
		// Shared read-only ownership permits analysis without OpenAL readbacks.
		const std::shared_ptr<const SoundAnalysisPcm>& analysisPcm() const { return m_analysisPcm; }
		~SoundBuffer();
	private:
		ALuint m_bufferID = 0;
		float m_time = 0.0f;
		std::shared_ptr<const SoundAnalysisPcm> m_analysisPcm;
	};
}

#endif //!__SOUND_BUFFER__
