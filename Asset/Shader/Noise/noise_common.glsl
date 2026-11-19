// Bibliothèque de bruit partagée : aucun uniform, buffer ou noyau dimensionnel.
#ifndef ENDURANCE_NOISE_COMMON_GLSL
#define ENDURANCE_NOISE_COMMON_GLSL

const int NOISE_MAX_OCTAVES = 8;
const int NOISE_EUCLIDEAN = 0;
const int NOISE_MANHATTAN = 1;
const int NOISE_CHEBYSHEV = 2;

// Helpers techniques internes.

// Helper interne : mélange les 32 bits de x, sans état ni accès mémoire.
// Retour uint sur toute sa plage ; le débordement entier non signé est voulu.
// Corps identique à l'ancien fieldHash pour conserver les mondes existants.
uint noiseInternalHash(uint x)
{
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    return x ^ (x >> 16u);
}

// Helper interne : convertit les 24 bits hauts de h en valeur dans [0,1].
// Les deux extrémités sont possibles ; ce n'est pas un intervalle [0,1[.
// Évite une conversion uint32 -> float qui arrondirait plusieurs bits bas.
float noiseInternalUintTo01(uint h)
{
    return float(h >> 8u) * (1.0 / 16777215.0);
}

// Courbes d'interpolation publiques, sans génération de bruit.

// Interpolation quintique de Perlin pour f dans [0,1], retour dans [0,1].
// Dérivées première et seconde nulles aux extrémités ; aucun clamp implicite.
// L'ordre des opérations conserve l'expression scalaire historique.
float noiseFadeQuintic(float f)
{
    return f*f*f*(f*(f*6.0-15.0)+10.0);
}

// Même interpolation quintique, appliquée aux deux composantes de f.
// Entrée et sortie dans [0,1]^2 ; expression historique sans clamp.
vec2 noiseFadeQuintic(vec2 f)
{
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
}

// Même interpolation quintique, appliquée aux trois composantes de f.
// Entrée et sortie dans [0,1]^3 ; expression historique sans clamp.
vec3 noiseFadeQuintic(vec3 f)
{
    return f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
}

// Interpolation cubique pour f dans [0,1], retour dans [0,1].
// Dérivée première nulle aux extrémités ; moins d'opérations que la quintique.
// Aucun clamp : l'appelant fournit habituellement fract(position).
float noiseFadeCubic(float f)
{
    return f * f * (3.0 - 2.0 * f);
}

// Même interpolation cubique, appliquée aux deux composantes de f.
// Entrée et sortie dans [0,1]^2 ; aucun clamp ni échantillonnage de bruit.
vec2 noiseFadeCubic(vec2 f)
{
    return f * f * (3.0 - 2.0 * f);
}

// Même interpolation cubique, appliquée aux trois composantes de f.
// Entrée et sortie dans [0,1]^3 ; aucun clamp ni échantillonnage de bruit.
vec3 noiseFadeCubic(vec3 f)
{
    return f * f * (3.0 - 2.0 * f);
}

#endif
