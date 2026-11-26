#pragma once
#include "glcore.hpp"
namespace Ge {
class Atmosphere {
public:
    unsigned buffer=0,volume=0;
    // A service double deliberately changes all state the real medium binder
    // owns. Rendering/sampling correctness is tested by check_particle_gpu.py.
    void bindParticleMedium(unsigned program,int location) {
        glBindBufferBase(GL_UNIFORM_BUFFER,6,buffer);
        for(unsigned unit: {12u,13u}) {
            glActiveTexture(GL_TEXTURE0+unit);
            glBindTexture(GL_TEXTURE_3D,volume);
            glBindSampler(unit,0);
        }
        glProgramUniform1i(program,location,0);
    }
};
}
