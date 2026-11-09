#ifndef ENDURANCE_FIELD_CLIMATE_GLSL
#define ENDURANCE_FIELD_CLIMATE_GLSL
#include "../Noise/noise_shaping.glsl"

// Fraction de chaque intervalle climatique réservée à une transition douce.
// 0.2 donne des rampes sur [0.10,0.15], [0.35,0.40], [0.60,0.65], [0.85,0.90].
const float FIELD_CLIMATE_TRANSITION_WIDTH = 0.2;

// Ramène l'humidité (x) et la température (y), déjà dans [0,1], vers les cinq
// niveaux de la matrice : 0, 0.25, 0.5, 0.75, 1. Les cœurs sont plus homogènes ;
// les frontières dominantes restent aux mêmes seuils, sans égaliser les surfaces.
// Le remappage est local, indépendant du chunk/LOD, et n'évalue aucun bruit.
vec2 fieldClimatePlateaus(vec2 climate)
{
    return vec2(noiseTerraces01(climate.x, 5, FIELD_CLIMATE_TRANSITION_WIDTH),
                noiseTerraces01(climate.y, 5, FIELD_CLIMATE_TRANSITION_WIDTH));
}
#endif
