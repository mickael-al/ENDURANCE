// Extension 2D pour les champs climatiques en grandes régions uniformes.
// Cette famille ne filtre aucun voisinage d'image : elle construit directement
// une partition déterministe à partir de la position et de la seed.
#ifndef ENDURANCE_NOISE_REGIONS_2D_GLSL
#define ENDURANCE_NOISE_REGIONS_2D_GLSL

#include "noise_2d.glsl"

// Deux sites cellulaires les plus proches. Les positions permettent d'évaluer
// une tendance lente AU CENTRE de chaque région : l'intérieur reste donc plat.
struct NoiseRegionPair2D
{
    vec2 distances;
    vec2 firstSite;
    vec2 secondSite;
    vec2 selectors;
};

// Recherche 3x3 avec un centre par cellule. Le jitter [0.24,0.76] rend les
// formes irrégulières tout en limitant les régions minuscules ou gigantesques.
NoiseRegionPair2D noiseInternalRegionPair2D(vec2 p, uint regionSeed)
{
    ivec2 cell = ivec2(floor(p));
    vec2 local = fract(p);
    NoiseRegionPair2D result;
    result.distances = vec2(1.0e20);
    result.firstSite = result.secondSite = vec2(0.0);
    result.selectors = vec2(0.0);

    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        ivec2 offset = ivec2(x, y);
        uint h = noiseInternalHash2D(cell + offset, regionSeed);
        vec2 jitter = 0.24 + 0.52 * vec2(h & 65535u, h >> 16u) * (1.0 / 65535.0);
        vec2 delta = vec2(offset) + jitter - local;
        float distanceSquared = dot(delta, delta);
        vec2 site = vec2(cell + offset) + jitter;
        float selector = noiseInternalUintTo01(noiseInternalHash(h ^ 0xa511e9b3u));

        if (distanceSquared < result.distances.x)
        {
            result.distances.y = result.distances.x;
            result.secondSite = result.firstSite;
            result.selectors.y = result.selectors.x;
            result.distances.x = distanceSquared;
            result.firstSite = site;
            result.selectors.x = selector;
        }
        else if (distanceSquared < result.distances.y)
        {
            result.distances.y = distanceSquared;
            result.secondSite = site;
            result.selectors.y = selector;
        }
    }
    result.distances = sqrt(result.distances);
    return result;
}

// Une tendance Simplex très large corrèle les valeurs des régions voisines.
// La composante aléatoire dépend de valueSeed, pas de regionSeed : humidité et
// température peuvent partager exactement les mêmes frontières tout en portant
// deux valeurs indépendantes dans chaque région.
float noiseInternalRegionTone2D01(vec2 site, uint valueSeed)
{
    float trend = noiseSimplex2D01(site * 0.23 + vec2(13.7, -9.1),
                                   valueSeed ^ 0x1b873593u);
    float randomValue = noiseWhite2D01(site, valueSeed ^ 0x68bc21ebu);
    return clamp(mix(trend, randomValue, 0.22), 0.0, 1.0);
}

// Valeur constante au cœur d'une région. Près d'une frontière, le résultat
// rejoint la moyenne des deux régions : l'échange premier/second reste continu.
float noiseInternalRegionBlend2D01(NoiseRegionPair2D regions, uint valueSeed,
                                   float transitionWidth, float siteScale)
{
    float first = noiseInternalRegionTone2D01(regions.firstSite / siteScale,
                                               valueSeed);
    transitionWidth = max(transitionWidth, 0.0);
    if (transitionWidth == 0.0) return first;
    float second = noiseInternalRegionTone2D01(regions.secondSite / siteScale,
                                                valueSeed);
    float interior = smoothstep(0.0, transitionWidth,
                                regions.distances.y - regions.distances.x);
    return mix(0.5 * (first + second), first, interior);
}

// Patchwork hiérarchique dans [0,1]. regionSeed dessine les frontières ;
// valueSeed choisit les tons. Deux champs peuvent donc partager une partition
// sans être corrélés dans leurs valeurs.
// - warpStrength : courbure des frontières en unités de cellule, [0,0.45].
// - transitionWidth : largeur du gradient entre régions, [0,0.5].
// - detailScale : taille relative des petites régions, >=1.25.
// - detailAmount : proportion statistique de petites régions, [0,1].
// Coût maximal : deux Simplex de warp, deux recherches 3x3 et jusqu'à quatre
// Simplex de tendance. Aucun voisinage d'image, texture temporaire ou boucle rayon.
float noisePatchwork2D01(vec2 p, uint regionSeed, uint valueSeed,
                         float warpStrength, float transitionWidth,
                         float detailScale, float detailAmount)
{
    warpStrength = clamp(warpStrength, 0.0, 0.45);
    transitionWidth = clamp(transitionWidth, 0.0, 0.5);
    detailScale = clamp(detailScale, 1.25, 6.0);
    detailAmount = clamp(detailAmount, 0.0, 1.0);

    vec2 warp = vec2(
        noiseSimplex2D(p * 0.43 + vec2(17.3, -5.8), regionSeed ^ 0x9e3779b9u),
        noiseSimplex2D(p * 0.43 + vec2(-8.4, 21.7), regionSeed ^ 0x85ebca6bu));
    vec2 domain = p + warp * warpStrength;

    NoiseRegionPair2D coarse = noiseInternalRegionPair2D(
        domain, regionSeed ^ 0x243f6a88u);
    float coarseValue = noiseInternalRegionBlend2D01(
        coarse, valueSeed ^ 0x13198a2eu, transitionWidth, 1.0);
    if (detailAmount == 0.0) return coarseValue;

    NoiseRegionPair2D detail = noiseInternalRegionPair2D(
        domain * detailScale + vec2(31.7, -14.2),
        regionSeed ^ 0x85a308d3u);
    float detailValue = noiseInternalRegionBlend2D01(
        detail, valueSeed ^ 0x03707344u,
        transitionWidth * detailScale, detailScale);

    // Une petite cellule n'apparaît que si son identifiant dépasse le seuil.
    // Elle s'efface vers le champ principal à sa frontière, ce qui évite les
    // cassures dures et produit les îlots imbriqués de la référence.
    float selected = step(1.0 - detailAmount, detail.selectors.x);
    float detailInterior = smoothstep(0.0,
        max(0.035, transitionWidth * detailScale),
        detail.distances.y - detail.distances.x);
    float coarseInterior = smoothstep(0.0, max(0.025, transitionWidth),
        coarse.distances.y - coarse.distances.x);
    float weight = selected * detailInterior * coarseInterior;
    detailValue = mix(coarseValue, detailValue, 0.78);
    return clamp(mix(coarseValue, detailValue, weight), 0.0, 1.0);
}

// Réglage de départ conçu pour reproduire une segmentation « peinture à
// l'huile » : grandes masses, frontières organiques et environ 30 % d'îlots.
float noisePatchwork2D01(vec2 p, uint regionSeed, uint valueSeed)
{
    return noisePatchwork2D01(
        p, regionSeed, valueSeed, 0.30, 0.075, 2.45, 0.30);
}

// Variante autonome : la même seed détermine la partition et les tons.
float noisePatchwork2D01(vec2 p, uint seed, float warpStrength,
                         float transitionWidth, float detailScale,
                         float detailAmount)
{
    return noisePatchwork2D01(p, seed ^ 0x7f4a7c15u, seed,
                              warpStrength, transitionWidth,
                              detailScale, detailAmount);
}

float noisePatchwork2D01(vec2 p, uint seed)
{
    return noisePatchwork2D01(p, seed ^ 0x7f4a7c15u, seed);
}

// Versions signées [-1,1], adaptées à la même chaîne smoothstep que Perlin.
float noisePatchwork2D(vec2 p, uint regionSeed, uint valueSeed,
                       float warpStrength, float transitionWidth,
                       float detailScale, float detailAmount)
{
    return noiseToSigned(noisePatchwork2D01(
        p, regionSeed, valueSeed, warpStrength,
        transitionWidth, detailScale, detailAmount));
}

float noisePatchwork2D(vec2 p, uint regionSeed, uint valueSeed)
{
    return noiseToSigned(noisePatchwork2D01(p, regionSeed, valueSeed));
}

float noisePatchwork2D(vec2 p, uint seed, float warpStrength,
                       float transitionWidth, float detailScale,
                       float detailAmount)
{
    return noiseToSigned(noisePatchwork2D01(
        p, seed, warpStrength, transitionWidth, detailScale, detailAmount));
}

float noisePatchwork2D(vec2 p, uint seed)
{
    return noiseToSigned(noisePatchwork2D01(p, seed));
}

#endif
