#ifndef GE_TEXTURE_BINDINGS_HPP
#define GE_TEXTURE_BINDINGS_HPP

#include "glcore.hpp"

namespace Ge
{
    // Nonzero names must identify initialized 2D textures. Same final state as
    // successive glActiveTexture/glBindTexture(GL_TEXTURE_2D) calls. In particular
    // a missing texture must not clear unrelated targets,
    // which glBindTextures would do for a zero name.
    inline void bindTexture2DSet(unsigned first, unsigned count, const unsigned* textures)
    {
        if (!count) return;
        bool allPresent = true;
        for (unsigned i = 0; i < count; ++i) allPresent = allPresent && textures[i] != 0;
        if (allPresent)
        {
            glBindTextures(first, count, textures);
            glActiveTexture(GL_TEXTURE0 + first + count - 1);
        }
        else
        {
            for (unsigned i = 0; i < count; ++i)
            {
                glActiveTexture(GL_TEXTURE0 + first + i);
                glBindTexture(GL_TEXTURE_2D, textures[i]);
            }
        }
    }
}
#endif
