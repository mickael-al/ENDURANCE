// Bibliothèque 2D autonome : coordonnées et seed explicites, aucun uniform.
// Les entrées et intermédiaires doivent être finis. Après TOUTE transformation,
// floor(p) et ses voisins doivent rester représentables en int GLSL.
// Une fréquence spatiale s'applique à p ; l'amplitude s'applique au résultat.
#ifndef ENDURANCE_NOISE_2D_GLSL
#define ENDURANCE_NOISE_2D_GLSL

#include "noise_common.glsl"
#include "noise_shaping.glsl"

// ============================================================================
// Helpers internes : hachage, gradients, interpolation et indices périodiques.
// ============================================================================

// Hash interne d'une cellule entière 2D : deux produits et un mélange uint32.
// Le débordement non signé est voulu. seed = worldSeed ^ salt reproduit
// exactement l'ancien fieldHash2 ; les coordonnées négatives sont conservées.
uint noiseInternalHash2D(ivec2 p, uint seed)
{
    return noiseInternalHash(uint(p.x) * 0x9e3779b9u ^ uint(p.y) * 0x85ebca6bu ^ seed);
}

// Produit scalaire interne avec l'une des huit directions historiques.
// Aucun sinus ni table ; les axes renvoient directement leur composante signée,
// y compris les zéros signés. Ce helper n'est pas un bruit borné à lui seul.
float noiseInternalGradient2D(uint h, vec2 p)
{
    uint g = h & 7u;
    if (g < 4u)
        return ((g & 1u) == 0u ? p.x : -p.x) + ((g & 2u) == 0u ? p.y : -p.y);
    return g == 4u ? p.x : g == 5u ? -p.x : g == 6u ? p.y : -p.y;
}

// Interpolation interne de quatre valeurs de cellule. u est le poids dans
// [0,1]^2, calculé par la variante cubique ou quintique ; retour [-1,1].
// Quatre hashes, aucune direction de gradient ni texture.
float noiseInternalValue2D(vec2 p, uint seed, vec2 u)
{
    ivec2 i = ivec2(floor(p));
    float a = noiseToSigned(noiseInternalUintTo01(noiseInternalHash2D(i, seed)));
    float b = noiseToSigned(noiseInternalUintTo01(noiseInternalHash2D(i + ivec2(1,0), seed)));
    float c = noiseToSigned(noiseInternalUintTo01(noiseInternalHash2D(i + ivec2(0,1), seed)));
    float d = noiseToSigned(noiseInternalUintTo01(noiseInternalHash2D(i + ivec2(1,1), seed)));
    return clamp(mix(mix(a,b,u.x), mix(c,d,u.x), u.y), -1.0, 1.0);
}

// Modulo interne positif, y compris pour les coordonnées négatives.
// period est positif et <=2^24 ; le reste porte sur une magnitude uint pour
// ne pas dépendre du traitement des opérandes signés négatifs par GLSL.
ivec2 noiseInternalWrap2D(ivec2 p, ivec2 period)
{
    bvec2 negative = lessThan(p, ivec2(0));
    uvec2 magnitude = mix(uvec2(p), uvec2(0u) - uvec2(p), negative);
    uvec2 remainder = magnitude % uvec2(period);
    uvec2 opposite = mix(uvec2(period) - remainder, uvec2(0u), equal(remainder,uvec2(0u)));
    return ivec2(mix(remainder, opposite, negative));
}

// ============================================================================
// Bases scalaires et données cellulaires : plages signées, wrappers 01, distances.
// ============================================================================

// Bruit blanc de grille, constant dans chaque cellule floor(p), dans [-1,1].
// Un seul hash ; les discontinuités sont intentionnelles. Ce n'est ni un bruit
// continu ni une nouvelle valeur à chaque appel pour des coordonnées identiques.
float noiseWhite2D(vec2 p, uint seed)
{
    return noiseToSigned(noiseInternalUintTo01(noiseInternalHash2D(ivec2(floor(p)), seed)));
}

// Version [0,1] du même bruit blanc ; un échantillonnage et un remappage affine.
// Les frontières des cellules restent discontinues, sans lissage supplémentaire.
float noiseWhite2D01(vec2 p, uint seed) { return noiseTo01(noiseWhite2D(p, seed)); }

// Bruit de valeurs quintique dans [-1,1] : quatre valeurs aléatoires interpolées.
// Continuité des deux premières dérivées aux frontières des cellules ; quatre
// hashes. Contrairement à Perlin, la valeur n'est pas nulle aux points entiers.
float noiseValue2D(vec2 p, uint seed)
{
    return noiseInternalValue2D(p, seed, noiseFadeQuintic(fract(p)));
}

// Version [0,1] du bruit de valeurs quintique ; même échantillonnage.
// Les quatre valeurs et leurs poids restent identiques à la version signée.
float noiseValue2D01(vec2 p, uint seed) { return noiseTo01(noiseValue2D(p, seed)); }

// Bruit de valeurs à interpolation cubique dans [-1,1], avec quatre hashes.
// Première dérivée continue ; ce nom désigne le fade cubique à quatre coins,
// pas une spline bicubique à seize valeurs.
float noiseValueCubic2D(vec2 p, uint seed)
{
    return noiseInternalValue2D(p, seed, noiseFadeCubic(fract(p)));
}

// Version [0,1] du bruit de valeurs cubique ; même échantillonnage.
// Quatre coins, avec la même continuité de première dérivée aux frontières.
float noiseValueCubic2D01(vec2 p, uint seed) { return noiseTo01(noiseValueCubic2D(p, seed)); }

// Perlin à huit directions, interpolation quintique et sortie [-1,1].
// Quatre coins évalués ensemble ; corps vectorisé historique conservé, sans
// table de permutations ni texture. Pour le terrain, passer worldSeed ^ salt.
float noisePerlin2D(vec2 p, uint seed)
{
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p), u = noiseFadeQuintic(f);
    uvec4 h = uvec4(i.x,i.x+1,i.x,i.x+1) * 0x9e3779b9u
            ^ uvec4(i.y,i.y,i.y+1,i.y+1) * 0x85ebca6bu ^ seed;
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;

    uvec4 g = h & 7u;
    vec4 dx = vec4(f.x,f.x-1.0,f.x,f.x-1.0);
    vec4 dy = vec4(f.y,f.y,f.y-1.0,f.y-1.0);
    vec4 a = mix(dx,dy,greaterThanEqual(g,uvec4(6u)));
    vec4 b = mix(vec4(0.0),dy,lessThan(g,uvec4(4u)));
    vec4 signedA = mix(a,-a,notEqual(g&1u,uvec4(0u)));
    vec4 n = mix(signedA + mix(b,-b,notEqual(g&2u,uvec4(0u))),
                 signedA,greaterThanEqual(g,uvec4(4u)));
    return clamp(mix(mix(n.x,n.y,u.x),mix(n.z,n.w,u.x),u.y),-1.0,1.0);
}

// Version [0,1] de Perlin. L'ordre 0.5 + 0.5*n est celui du terrain existant.
// Aucun second appel au bruit ni clamp supplémentaire.
float noisePerlin2D01(vec2 p, uint seed) { return noiseTo01(noisePerlin2D(p, seed)); }

// Perlin périodique dans [-1,1]. period donne le nombre de cellules par axe,
// borné à [1,2^24] ; seuls les indices des gradients sont repliés, pas fract(p).
// Quatre hashes plus les modulos : les raccords conservent le fade quintique.
float noisePerlinPeriodic2D(vec2 p, uint seed, ivec2 period)
{
    period = clamp(period, ivec2(1), ivec2(16777216));
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p), u = noiseFadeQuintic(f);
    float a = noiseInternalGradient2D(noiseInternalHash2D(noiseInternalWrap2D(i, period), seed), f);
    float b = noiseInternalGradient2D(noiseInternalHash2D(noiseInternalWrap2D(i + ivec2(1,0), period), seed), f - vec2(1,0));
    float c = noiseInternalGradient2D(noiseInternalHash2D(noiseInternalWrap2D(i + ivec2(0,1), period), seed), f - vec2(0,1));
    float d = noiseInternalGradient2D(noiseInternalHash2D(noiseInternalWrap2D(i + ivec2(1,1), period), seed), f - vec2(1,1));
    return clamp(mix(mix(a,b,u.x), mix(c,d,u.x), u.y), -1.0, 1.0);
}

// Version [0,1] de Perlin périodique ; même période entière par axe.
// Coût de quatre coins plus leurs modulos ; les périodes restent bornées à [1,2^24].
float noisePerlinPeriodic2D01(vec2 p, uint seed, ivec2 period)
{
    return noiseTo01(noisePerlinPeriodic2D(p, seed, period));
}

// Simplex sur une grille triangulaire, dans [-1,1] : trois gradients pondérés
// par un support radial compact. Géométrie décrite par Stefan Gustavson dans
// https://itn-web.it.liu.se/~stegu76/simplexnoise/simplexnoise.pdf ; hash et huit
// gradients propres à cette bibliothèque, sans recopier ses tables de permutation.
float noiseSimplex2D(vec2 p, uint seed)
{
    const float skew = 0.36602540378443864676;
    const float unskew = 0.21132486540518711775;
    ivec2 cell = ivec2(floor(p + (p.x + p.y) * skew));
    float originOffset = (float(cell.x) + float(cell.y)) * unskew;
    vec2 d0 = p - (vec2(cell) - originOffset);
    ivec2 middle = d0.x > d0.y ? ivec2(1,0) : ivec2(0,1);
    vec2 d1 = d0 - vec2(middle) + unskew;
    vec2 d2 = d0 - 1.0 + 2.0 * unskew;
    vec3 attenuation = max(vec3(0.5) - vec3(dot(d0,d0),dot(d1,d1),dot(d2,d2)), 0.0);
    attenuation *= attenuation;
    attenuation *= attenuation;
    vec3 gradients = vec3(
        noiseInternalGradient2D(noiseInternalHash2D(cell, seed), d0),
        noiseInternalGradient2D(noiseInternalHash2D(cell + middle, seed), d1),
        noiseInternalGradient2D(noiseInternalHash2D(cell + ivec2(1), seed), d2));
    return clamp(70.0 * dot(attenuation, gradients), -1.0, 1.0);
}

// Version [0,1] du Simplex ; trois coins comme la version signée.
// Le remappage ne change ni la grille triangulaire ni les gradients évalués.
float noiseSimplex2D01(vec2 p, uint seed) { return noiseTo01(noiseSimplex2D(p, seed)); }

// Voronoï brut : x=distance F1, y=valeur du site dans [0,1]. Recherche 3x3,
// jitter fixe [0.35,0.65] ; métriques Euclidienne/Manhattan/Chebyshev.
// La valeur du site est DISCONTINUE aux frontières : masquer son influence
// par un support compact avant de l'employer comme déplacement de surface.
vec2 noiseCellularData2D(vec2 p, uint seed, int metric)
{
    ivec2 cell = ivec2(floor(p));
    vec2 f = fract(p);
    float best = 100.0;
    float site = 0.0;
    for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x)
    {
        ivec2 q = ivec2(x,z);
        uint h = noiseInternalHash2D(cell+q,seed);
        vec2 jitter = 0.35 + 0.30 * vec2(h & 65535u,h >> 16u) * (1.0/65535.0);
        vec2 delta = abs(vec2(q)+jitter-f);
        float d = metric == NOISE_MANHATTAN ? delta.x+delta.y :
                  metric == NOISE_CHEBYSHEV ? max(delta.x,delta.y) : dot(delta,delta);
        if(d<best)
        {
            best=d;
            site=noiseInternalUintTo01(noiseInternalHash(h^0x68bc21ebu));
        }
    }
    return vec2(metric == NOISE_MANHATTAN || metric == NOISE_CHEBYSHEV ? best : sqrt(best),site);
}

// Même paire F1/site, avec métrique Euclidienne par défaut ; neuf sites.
// Distance brute positive et valeur du site [0,1] ; une racine pour F1.
vec2 noiseCellularData2D(vec2 p, uint seed)
{
    return noiseCellularData2D(p, seed, NOISE_EUCLIDEAN);
}

// Distance F1 brute dans les unités de p ; jamais remappée implicitement.
// Borne conservative : 0.65*sqrt(2), 1.30 ou 0.65 selon la métrique ; neuf sites.
float noiseCellularDistance2D(vec2 p, uint seed, int metric)
{
    return noiseCellularData2D(p, seed, metric).x;
}

// Distance F1 Euclidienne par défaut ; sortie positive, pas un bruit signé.
// Recherche 3x3 avec une racine finale, borne conservative 0.65*sqrt(2).
float noiseCellularDistance2D(vec2 p, uint seed)
{
    return noiseCellularDistance2D(p, seed, NOISE_EUCLIDEAN);
}

// Les deux distances Euclidiennes AU CARRÉ, ordonnées F1²<=F2², sans racines.
// Neuf sites ; ordre, jitter, hash et réduction exactement conservés pour
// PolarIce. La sortie brute ne doit pas être remappée avant un ancien ratio.
vec2 noiseCellularDistancesSquared2D(vec2 p, uint seed)
{
    ivec2 cell = ivec2(floor(p));
    vec2 f = fract(p);
    vec2 nearest = vec2(100.0);
    for (int z = -1; z <= 1; ++z)
    for (int x = -1; x <= 1; ++x)
    {
        ivec2 q = ivec2(x,z);
        uint h = noiseInternalHash2D(cell+q,seed);
        vec2 jitter = 0.35 + 0.30 * vec2(h & 65535u,h >> 16u) * (1.0/65535.0);
        vec2 delta = vec2(q)+jitter-f;
        float d = dot(delta,delta);
        nearest.y = min(nearest.y,max(nearest.x,d));
        nearest.x = min(nearest.x,d);
    }
    return nearest;
}

// F1 normalisé dans [0,1] par une borne conservative propre à la métrique.
// Le site de la cellule courante suffit à établir cette borne ; pas de gain
// empirique ni de promesse d'atteindre 1. Neuf sites comme la distance brute.
float noiseVoronoi2D01(vec2 p, uint seed, int metric)
{
    float bound = metric == NOISE_MANHATTAN ? 1.30 :
                  metric == NOISE_CHEBYSHEV ? 0.65 : 0.9192388155425118;
    return clamp(noiseCellularDistance2D(p,seed,metric) / bound, 0.0, 1.0);
}

// Version signée [-1,1] du F1 normalisé ; une seule recherche cellulaire.
// La normalisation conservative ne promet pas d'atteindre exactement les extrêmes.
float noiseVoronoi2D(vec2 p, uint seed, int metric)
{
    return noiseToSigned(noiseVoronoi2D01(p, seed, metric));
}

// Voronoï signé Euclidien par défaut ; même normalisation conservative.
// Neuf sites et une racine ; utiliser Distance2D pour conserver des unités brutes.
float noiseVoronoi2D(vec2 p, uint seed)
{
    return noiseVoronoi2D(p, seed, NOISE_EUCLIDEAN);
}

// Voronoï [0,1] Euclidien par défaut ; ne remplace pas la distance brute.
// Un seul calcul F1, divisé par la borne 0.65*sqrt(2) puis borné à [0,1].
float noiseVoronoi2D01(vec2 p, uint seed)
{
    return noiseVoronoi2D01(p, seed, NOISE_EUCLIDEAN);
}

// Valeur aléatoire du site Voronoï le plus proche, dans [0,1], neuf sites.
// Constante dans une région, DISCONTINUE à sa frontière ; utile pour des
// identités ou masques, avec un support adapté si elle déplace la géométrie.
float noiseCellValue2D01(vec2 p, uint seed, int metric)
{
    return noiseCellularData2D(p, seed, metric).y;
}

// Valeur de site remappée dans [-1,1], avec les mêmes discontinuités.
// Une seule recherche de neuf sites ; le remappage ne lisse pas les frontières.
float noiseCellValue2D(vec2 p, uint seed, int metric)
{
    return noiseToSigned(noiseCellValue2D01(p, seed, metric));
}

// Valeur de site signée, métrique Euclidienne par défaut ; neuf sites.
// Stable à l'intérieur d'une région, discontinue quand le site le plus proche change.
float noiseCellValue2D(vec2 p, uint seed)
{
    return noiseCellValue2D(p, seed, NOISE_EUCLIDEAN);
}

// Valeur de site [0,1], métrique Euclidienne par défaut ; neuf sites.
// Même identité de site que la version signée, sans interpolation entre régions.
float noiseCellValue2D01(vec2 p, uint seed)
{
    return noiseCellValue2D01(p, seed, NOISE_EUCLIDEAN);
}

// ============================================================================
// Compositions : crêtes, fractales, vecteurs, déformations de domaine et motifs.
// ============================================================================

// Crête mono-octave dans [0,1] : expression exacte de l'ancien fieldRidge2.
// Un Perlin et une valeur absolue ; différent de la fractale Ridged ci-dessous.
float noiseRidge2D01(vec2 p, uint seed) { return 1.0-abs(noisePerlin2D(p,seed)); }

// Version signée [-1,1] de la crête mono-octave ; même échantillonnage.
// Un Perlin, une valeur absolue et un remappage ; aucune accumulation d'octaves.
float noiseRidge2D(vec2 p, uint seed) { return noiseToSigned(noiseRidge2D01(p,seed)); }

// fBm de Perlin normalisé dans [-1,1], jusqu'à huit octaves, un bruit par octave.
// octaves borné à [0,8], lacunarity à [1,8], gain à [0,1] ; zéro octave donne 0.
// Les paramètres sont finis ; les fréquences croissantes doivent rester dans
// le domaine entier. Un gain nul n'évalue que la première octave.
float noiseFbm2D(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    octaves = clamp(octaves, 0, NOISE_MAX_OCTAVES);
    lacunarity = clamp(lacunarity, 1.0, 8.0);
    gain = clamp(gain, 0.0, 1.0);
    if (octaves == 0) return 0.0;
    float sum = 0.0, normalization = 0.0, amplitude = 1.0;
    for (int i = 0; i < NOISE_MAX_OCTAVES; ++i)
    {
        if (i >= octaves || amplitude == 0.0) break;
        sum += amplitude * noisePerlin2D(p, seed + uint(i) * 0x9e3779b9u);
        normalization += amplitude;
        p *= lacunarity;
        amplitude *= gain;
    }
    return clamp(sum / normalization, -1.0, 1.0);
}

// Version [0,1] du fBm Perlin ; zéro octave donne 0.5, sans bruit évalué.
// Même bornage des paramètres ; jusqu'à huit Perlin, aucun échantillon ajouté.
float noiseFbm2D01(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseFbm2D(p, seed, octaves, lacunarity, gain));
}

// fBm de valeurs quintiques dans [-1,1] : jusqu'à huit bruits de quatre coins.
// Mêmes bornes [0,8]/[1,8]/[0,1] pour octaves/lacunarity/gain que noiseFbm2D.
// La normalisation par la somme des amplitudes évite de dépasser la plage.
float noiseValueFbm2D(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    octaves = clamp(octaves, 0, NOISE_MAX_OCTAVES);
    lacunarity = clamp(lacunarity, 1.0, 8.0);
    gain = clamp(gain, 0.0, 1.0);
    if (octaves == 0) return 0.0;
    float sum = 0.0, normalization = 0.0, amplitude = 1.0;
    for (int i = 0; i < NOISE_MAX_OCTAVES; ++i)
    {
        if (i >= octaves || amplitude == 0.0) break;
        sum += amplitude * noiseValue2D(p, seed + uint(i) * 0x9e3779b9u);
        normalization += amplitude;
        p *= lacunarity;
        amplitude *= gain;
    }
    return clamp(sum / normalization, -1.0, 1.0);
}

// Version [0,1] du fBm de valeurs ; zéro octave donne 0.5.
// Jusqu'à huit octaves de quatre coins, avec normalisation par les amplitudes.
float noiseValueFbm2D01(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseValueFbm2D(p, seed, octaves, lacunarity, gain));
}

// fBm Simplex normalisé dans [-1,1] ; trois coins par octave, jusqu'à huit.
// Même bornage des paramètres et même seed par octave que les autres fBm.
// Le coût augmente avec octaves, même si la sortie reste normalisée.
float noiseSimplexFbm2D(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    octaves = clamp(octaves, 0, NOISE_MAX_OCTAVES);
    lacunarity = clamp(lacunarity, 1.0, 8.0);
    gain = clamp(gain, 0.0, 1.0);
    if (octaves == 0) return 0.0;
    float sum = 0.0, normalization = 0.0, amplitude = 1.0;
    for (int i = 0; i < NOISE_MAX_OCTAVES; ++i)
    {
        if (i >= octaves || amplitude == 0.0) break;
        sum += amplitude * noiseSimplex2D(p, seed + uint(i) * 0x9e3779b9u);
        normalization += amplitude;
        p *= lacunarity;
        amplitude *= gain;
    }
    return clamp(sum / normalization, -1.0, 1.0);
}

// Version [0,1] du fBm Simplex ; zéro octave donne 0.5.
// Jusqu'à huit octaves de trois coins, avec les mêmes seeds et poids que la base.
float noiseSimplexFbm2D01(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseSimplexFbm2D(p, seed, octaves, lacunarity, gain));
}

// Somme normalisée de abs(Perlin), puis remappage dans [-1,1].
// Jusqu'à huit bruits ; mêmes bornes d'octaves/lacunarity/gain que fBm.
// La valeur absolue forme des plis ; zéro octave reste la valeur neutre 0.
float noiseTurbulence2D(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    octaves = clamp(octaves, 0, NOISE_MAX_OCTAVES);
    lacunarity = clamp(lacunarity, 1.0, 8.0);
    gain = clamp(gain, 0.0, 1.0);
    if (octaves == 0) return 0.0;
    float sum = 0.0, normalization = 0.0, amplitude = 1.0;
    for (int i = 0; i < NOISE_MAX_OCTAVES; ++i)
    {
        if (i >= octaves || amplitude == 0.0) break;
        sum += amplitude * abs(noisePerlin2D(p, seed + uint(i) * 0x9e3779b9u));
        normalization += amplitude;
        p *= lacunarity;
        amplitude *= gain;
    }
    return noiseToSigned(clamp(sum / normalization, 0.0, 1.0));
}

// Turbulence dans sa plage naturelle [0,1] ; même travail, zéro octave =>0.5.
// Jusqu'à huit Perlin avec valeur absolue ; les paramètres suivent le contrat fBm.
float noiseTurbulence2D01(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseTurbulence2D(p, seed, octaves, lacunarity, gain));
}

// Crêtes fractales : moyenne pondérée de (1-abs(Perlin))², remappée dans [-1,1].
// Jusqu'à huit bruits ; mêmes bornes de paramètres que fBm, zéro octave =>0.
// Le carré resserre les crêtes ; aucune rétroaction ou amplitude cachée.
float noiseRidged2D(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    octaves = clamp(octaves, 0, NOISE_MAX_OCTAVES);
    lacunarity = clamp(lacunarity, 1.0, 8.0);
    gain = clamp(gain, 0.0, 1.0);
    if (octaves == 0) return 0.0;
    float sum = 0.0, normalization = 0.0, amplitude = 1.0;
    for (int i = 0; i < NOISE_MAX_OCTAVES; ++i)
    {
        if (i >= octaves || amplitude == 0.0) break;
        float ridge = 1.0 - abs(noisePerlin2D(p, seed + uint(i) * 0x9e3779b9u));
        sum += amplitude * ridge * ridge;
        normalization += amplitude;
        p *= lacunarity;
        amplitude *= gain;
    }
    return noiseToSigned(clamp(sum / normalization, 0.0, 1.0));
}

// Version [0,1] des crêtes fractales ; zéro octave donne 0.5.
// Le carré de chaque crête et la normalisation restent ceux de la version signée.
float noiseRidged2D01(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseRidged2D(p, seed, octaves, lacunarity, gain));
}

// Billow désigne ici la turbulence signée : 2*moyenne(abs(Perlin))-1.
// Alias explicite, PAS une nouvelle primitive ; même coût et paramètres que
// noiseTurbulence2D. Cette convention conserve une API signée cohérente.
float noiseBillow2D(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTurbulence2D(p, seed, octaves, lacunarity, gain);
}

// Alias [0,1] de la turbulence ; zéro octave donne 0.5.
// Même coût et mêmes plis ; ce nom ne désigne pas un autre noyau de bruit.
float noiseBillow2D01(vec2 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseBillow2D(p, seed, octaves, lacunarity, gain));
}

// fBm historique du terrain : trois Perlin, poids et décalages EXACTS.
// Retour [-1,1] ; worldSeed et salt restent séparés car (salt+i) est calculé
// AVANT le XOR. Ce composite n'est pas la boucle fBm paramétrable ci-dessus.
float noiseFbmTerrain2D(vec2 p, uint worldSeed, uint salt)
{
    return noisePerlin2D(p, worldSeed ^ salt) * 0.57142857
        + noisePerlin2D(p * 2.03 + vec2(19.7, -37.1), worldSeed ^ (salt + 1u)) * 0.28571429
        + noisePerlin2D(p * 4.11 + vec2(-43.2, 11.8), worldSeed ^ (salt + 2u)) * 0.14285714;
}

// Version [0,1] du fBm historique ; les trois échantillons restent inchangés.
// worldSeed et salt restent séparés, avec les mêmes additions avant XOR.
float noiseFbmTerrain2D01(vec2 p, uint worldSeed, uint salt)
{
    return noiseTo01(noiseFbmTerrain2D(p, worldSeed, salt));
}

// Transforme les coordonnées dans la base direction/perpendiculaire.
// Deux produits scalaires, aucun bruit ; direction unitaire donne une rotation,
// une autre longueur ajoute une échelle. Expression historique inchangée.
vec2 noiseRotate2D(vec2 p, vec2 direction)
{
    return vec2(dot(p, direction), dot(p, vec2(-direction.y, direction.x)));
}

// Vecteur de deux Perlin décorrélés par des seeds XOR fixes ; deux échantillons.
// Chaque composante est dans [-1,1], mais le vecteur n'est PAS unitaire : sa
// norme peut atteindre sqrt(2). Aucun déplacement ni normalisation implicite.
vec2 noiseVector2D(vec2 p, uint seed)
{
    return vec2(noisePerlin2D(p, seed ^ 0x68bc21ebu),
                noisePerlin2D(p, seed ^ 0x02e5be93u));
}

// Même vecteur remappé composante par composante dans [0,1]^2.
// Deux Perlin seulement ; la norme reste distincte d'une direction unitaire.
vec2 noiseVector2D01(vec2 p, uint seed)
{
    return vec2(0.5) + 0.5 * noiseVector2D(p, seed);
}

// Déforme p avec deux Perlin indépendants ; renvoie des COORDONNÉES, pas un
// bruit borné. amplitude utilise les unités de p et peut être signée ; fréquence
// négative ramenée à 0. Déplacement par composante borné par abs(amplitude).
vec2 noiseDomainWarp2D(vec2 p, uint seed, float amplitude, float frequency)
{
    frequency = max(frequency, 0.0);
    if (amplitude == 0.0 || frequency == 0.0) return p;
    vec2 q = p * frequency;
    vec2 displacement = noiseVector2D(q, seed);
    return p + amplitude * displacement;
}

// Perlin dans un domaine déformé, retour [-1,1] : deux bruits de déplacement
// puis un bruit final. L'appelant applique l'échelle générale aux coordonnées p.
float noiseWarpedPerlin2D(vec2 p, uint seed, float amplitude, float frequency)
{
    return noisePerlin2D(noiseDomainWarp2D(p, seed, amplitude, frequency), seed);
}

// Version [0,1] du Perlin déformé ; au plus trois échantillons Perlin.
// Même amplitude et fréquence de déformation, sans nouveau calcul de déplacement.
float noiseWarpedPerlin2D01(vec2 p, uint seed, float amplitude, float frequency)
{
    return noiseTo01(noiseWarpedPerlin2D(p, seed, amplitude, frequency));
}

// Train de dunes dans [0,1] : pente longue, pente courte et phase déformée
// par un Perlin. wavelength<=0 désactive le motif ; direction est normalement
// unitaire. L'expression des paramètres valides reste celle des anciens biomes.
float noiseDunes2D01(vec2 p, vec2 direction, float wavelength, uint seed)
{
    if (wavelength <= 0.0) return 0.0;
    vec2 oriented = noiseRotate2D(p, direction);
    float warp = noisePerlin2D(p * (1.0 / (wavelength * 3.0)), seed) * 0.32;
    float phase = fract(oriented.x / wavelength + warp);
    return phase < 0.76 ? smoothstep(0.0, 0.76, phase)
                        : 1.0 - smoothstep(0.76, 1.0, phase);
}

// Version signée [-1,1] des dunes ; wavelength<=0 donne -1.
// Même Perlin de phase et mêmes rampes, suivis d'un remappage affine.
float noiseDunes2D(vec2 p, vec2 direction, float wavelength, uint seed)
{
    return noiseToSigned(noiseDunes2D01(p, direction, wavelength, seed));
}

// Relief de sites dans [0,1] : neuf sites cellulaires et un Perlin de contour.
// spacing/radius<=0 ou occupancy<=0 désactive ; occupancy est borné à [0,1].
// radius s'exprime en cellules : rester sous 0.35 pour assurer un support
// disjoint des frontières de sites et masquer la discontinuité de leurs valeurs.
float noiseLandmark2D01(vec2 p, float spacing, uint seed, int metric,
                      float radius, float crown, float occupancy)
{
    occupancy = clamp(occupancy, 0.0, 1.0);
    if (spacing <= 0.0 || radius <= 0.0 || occupancy == 0.0) return 0.0;
    vec2 site = noiseCellularData2D(p / spacing, seed, metric);
    float enabled = step(1.0 - occupancy, site.y);
    float strength = 0.65 + 0.35 * clamp((site.y - (1.0 - occupancy)) / occupancy, 0.0, 1.0);
    float radiusVariation = 0.72 + 0.24 * fract(site.y * 23.17);
    float crownVariation = clamp(crown * (0.65 + 0.65 * fract(site.y * 41.73)), 0.0, 0.85);
    float contour = noisePerlin2D(p * (3.0 / spacing), seed ^ 0xa511e9b3u) * (0.04 * radius);
    return noiseMesa01(site.x + contour, radius * radiusVariation, crownVariation) * enabled * strength;
}

// Version signée [-1,1] des landmarks ; un site désactivé donne -1.
// Recherche de neuf sites et un Perlin de contour, sans autre bruit ajouté.
float noiseLandmark2D(vec2 p, float spacing, uint seed, int metric,
                    float radius, float crown, float occupancy)
{
    return noiseToSigned(noiseLandmark2D01(p, spacing, seed, metric, radius, crown, occupancy));
}

// Marbre : bandes sinusoïdales suivant p.y, perturbées par un Perlin, [-1,1].
// frequency>=0 règle les cycles par unité de p ; distortion est un décalage de
// phase signé en cycles. Coût : un Perlin et un sinus, pas une nouvelle base.
float noiseMarble2D(vec2 p, uint seed, float frequency, float distortion)
{
    float phase = p.y * max(frequency, 0.0) + distortion * noisePerlin2D(p, seed);
    return sin(6.28318530717958647692 * phase);
}

// Marbre dans [0,1] ; même motif sinusoïdal et même bruit de phase.
// Un Perlin et un sinus ; fréquence négative ramenée à zéro comme dans la base.
float noiseMarble2D01(vec2 p, uint seed, float frequency, float distortion)
{
    return noiseTo01(noiseMarble2D(p, seed, frequency, distortion));
}

// Bois : anneaux sinusoïdaux centrés à l'origine de p, perturbés par un Perlin.
// Retour [-1,1] ; frequency>=0 en cycles/unité, distortion en cycles signés.
// Une longueur, un Perlin et un sinus ; déplacer/scaler p pour régler le motif.
float noiseWood2D(vec2 p, uint seed, float frequency, float distortion)
{
    float phase = length(p) * max(frequency, 0.0) + distortion * noisePerlin2D(p, seed);
    return sin(6.28318530717958647692 * phase);
}

// Bois dans [0,1] ; même centre et mêmes anneaux.
// Une longueur, un Perlin et un sinus, puis un remappage de la sortie.
float noiseWood2D01(vec2 p, uint seed, float frequency, float distortion)
{
    return noiseTo01(noiseWood2D(p, seed, frequency, distortion));
}

// Fissures dans [0,1], maximales aux frontières de Voronoï où F2-F1=0.
// width est une largeur en différence de distances, pas une SDF de la frontière.
// width<=0 désactive ; coût : neuf sites, deux racines et un smoothstep.
float noiseCracks2D01(vec2 p, uint seed, float width)
{
    if (width <= 0.0) return 0.0;
    vec2 distances = sqrt(noiseCellularDistancesSquared2D(p, seed));
    return 1.0 - smoothstep(0.0, width, distances.y - distances.x);
}

// Version signée [-1,1] des fissures ; width<=0 donne -1.
// Même recherche F1/F2 et deux racines ; aucune seconde évaluation cellulaire.
float noiseCracks2D(vec2 p, uint seed, float width)
{
    return noiseToSigned(noiseCracks2D01(p, seed, width));
}

// Champ de rotation (dN/dy,-dN/dx), calculé par différences centrées de Perlin.
// Quatre échantillons, sortie vectorielle NON bornée ni normalisée ; epsilon
// devient max(abs(epsilon),1e-4). À grande coordonnée, choisir un pas représentable.
vec2 noiseCurl2D(vec2 p, uint seed, float epsilon)
{
    float e = max(abs(epsilon), 0.0001);
    float dx = noisePerlin2D(p + vec2(e,0.0), seed) - noisePerlin2D(p - vec2(e,0.0), seed);
    float dy = noisePerlin2D(p + vec2(0.0,e), seed) - noisePerlin2D(p - vec2(0.0,e), seed);
    return vec2(dy,-dx) / (2.0 * e);
}

#endif
