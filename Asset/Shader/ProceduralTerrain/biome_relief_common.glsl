// Contrat de sortie des recettes 2D du terrain.
// Les bruits, compositions spatiales et transformations sont dans Noise/.
#ifndef ENDURANCE_BIOME_RELIEF_COMMON_GLSL
#define ENDURANCE_BIOME_RELIEF_COMMON_GLSL

#include "../Noise/noise_2d.glsl"
#include "field_limits.glsl"

// Borne le relief aux enveloppes connues du générateur et du stockage compact.
// x : hauteur, y : épaisseur totale, z : décalage relatif, w : contrôle local.
// Ce contrat appartient au terrain ; il ne fait pas partie de la bibliothèque de bruit.
vec4 fieldClampRelief(vec4 relief)
{
    return clamp(relief, vec4(-FIELD_MAX_RELIEF_HEIGHT, 0.0, -160.0, 0.0),
                         vec4(FIELD_MAX_RELIEF_HEIGHT, FIELD_MAX_VOLUME_THICKNESS, 160.0, 1.0));
}

#endif
