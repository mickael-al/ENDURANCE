// Bibliothèque 3D autonome : coordonnées et seed explicites, aucun uniform.
// Les entrées et intermédiaires doivent être finis. Après TOUTE transformation
// (octaves, déformation, grille Simplex), floor(p) et ses voisins doivent rester
// représentables en int GLSL. La précision spatiale reste celle du float32.
// Les bruits scalaires sont signés ; le suffixe 01 désigne leur version [0,1].
// Les distances, coordonnées et dérivées vectorielles gardent leurs unités.
#ifndef ENDURANCE_NOISE_3D_GLSL
#define ENDURANCE_NOISE_3D_GLSL

#include "noise_common.glsl"
#include "noise_shaping.glsl"

// ---------------------------------------------------------------------------
// Internes : hashes, gradients et interpolation, sans état ni uniform.
// ---------------------------------------------------------------------------

// Hash interne d'une cellule entière 3D ; le débordement uint32 est voulu.
// seed = worldSeed ^ salt reproduit exactement l'ancien fieldHash3, y compris
// pour les coordonnées négatives. Trois produits entiers, un hash commun.
uint noiseInternalHash3D(ivec3 p, uint seed)
{
    return noiseInternalHash(uint(p.x) * 0x9e3779b9u ^ uint(p.y) * 0x85ebca6bu ^
        uint(p.z) * 0xc2b2ae35u ^ seed);
}

// Produit scalaire interne avec les gradients historiques du Perlin 3D.
// Les seize indices et l'ordre des additions restent identiques ; aucun sinus
// ni texture. Ce produit scalaire n'est pas, à lui seul, un bruit borné.
float noiseInternalGradient3D(uint h, vec3 p)
{
    h &= 15u;
    float a = h < 8u ? p.x : p.y;
    float b = h < 4u ? p.y : (h == 12u || h == 14u ? p.x : p.z);
    return ((h & 1u) == 0u ? a : -a) + ((h & 2u) == 0u ? b : -b);
}

// Interpolation interne de huit valeurs signées, avec u dans [0,1]^3.
// u vient du fade cubique ou quintique ; huit hashes et sept interpolations,
// sans table de gradients. Le clamp absorbe les arrondis aux extrémités.
float noiseInternalValue3D(ivec3 i, vec3 u, uint seed)
{
    float n000 = noiseToSigned(noiseInternalUintTo01(noiseInternalHash3D(i, seed)));
    float n100 = noiseToSigned(noiseInternalUintTo01(noiseInternalHash3D(i+ivec3(1,0,0), seed)));
    float n010 = noiseToSigned(noiseInternalUintTo01(noiseInternalHash3D(i+ivec3(0,1,0), seed)));
    float n110 = noiseToSigned(noiseInternalUintTo01(noiseInternalHash3D(i+ivec3(1,1,0), seed)));
    float n001 = noiseToSigned(noiseInternalUintTo01(noiseInternalHash3D(i+ivec3(0,0,1), seed)));
    float n101 = noiseToSigned(noiseInternalUintTo01(noiseInternalHash3D(i+ivec3(1,0,1), seed)));
    float n011 = noiseToSigned(noiseInternalUintTo01(noiseInternalHash3D(i+ivec3(0,1,1), seed)));
    float n111 = noiseToSigned(noiseInternalUintTo01(noiseInternalHash3D(i+ivec3(1,1,1), seed)));
    return clamp(mix(mix(mix(n000,n100,u.x),mix(n010,n110,u.x),u.y),
        mix(mix(n001,n101,u.x),mix(n011,n111,u.x),u.y),u.z),-1.0,1.0);
}

// Modulo interne positif : period est positif et <=2^24 sur chaque axe.
// Le reste porte sur une magnitude uint, puis le signe est corrigé ; aucune
// dépendance au reste signé négatif ni négation susceptible de déborder en int.
ivec3 noiseInternalWrap3D(ivec3 p, ivec3 period)
{
    bvec3 negative = lessThan(p, ivec3(0));
    uvec3 magnitude = mix(uvec3(p), uvec3(0u) - uvec3(p), negative);
    uvec3 remainder = magnitude % uvec3(period);
    uvec3 opposite = mix(uvec3(period) - remainder, uvec3(0u), equal(remainder,uvec3(0u)));
    return ivec3(mix(remainder, opposite, negative));
}

// Gradient interne du Simplex : douze directions, arêtes du cube, sans table.
// Un modulo et deux composantes signées ; sa longueur est sqrt(2), et la
// normalisation finale appartient au noyau Simplex, pas à ce produit scalaire.
float noiseInternalSimplexGradient3D(uint h, vec3 p)
{
    uint g = h % 12u;
    float a = g < 8u ? p.x : p.y;
    float b = g < 4u ? p.y : p.z;
    return ((g & 1u) == 0u ? a : -a) + ((g & 2u) == 0u ? b : -b);
}

// ---------------------------------------------------------------------------
// Bases : bruits signés, versions 01 et données cellulaires géométriques.
// ---------------------------------------------------------------------------

// Bruit blanc de grille dans [-1,1], constant dans chaque cellule floor(p).
// Un seul hash ; les discontinuités sont intentionnelles. Même position et
// même seed donnent la même valeur, sans état aléatoire modifié par l'appel.
float noiseWhite3D(vec3 p, uint seed)
{
    return noiseToSigned(noiseInternalUintTo01(noiseInternalHash3D(ivec3(floor(p)), seed)));
}

// Version [0,1] du bruit blanc ; les discontinuités restent identiques.
// Un échantillonnage et un remappage affine, sans second hash.
float noiseWhite3D01(vec3 p, uint seed) { return noiseTo01(noiseWhite3D(p, seed)); }

// Bruit de valeurs quintique dans [-1,1] : huit valeurs aléatoires interpolées.
// Dérivées première et seconde continues aux frontières de cellules ; huit
// hashes. Contrairement à Perlin, il n'est pas nul aux coordonnées entières.
float noiseValue3D(vec3 p, uint seed)
{
    return noiseInternalValue3D(ivec3(floor(p)), noiseFadeQuintic(fract(p)), seed);
}

// Version [0,1] du bruit de valeurs quintique, avec les mêmes huit coins.
// Un remappage affine ; aucun bruit supplémentaire n'est évalué.
float noiseValue3D01(vec3 p, uint seed) { return noiseTo01(noiseValue3D(p, seed)); }

// Bruit de valeurs à fade cubique dans [-1,1], première dérivée continue.
// Huit hashes : « cubique » désigne l'interpolation 3t²-2t³ entre huit coins,
// pas une spline tricubique à soixante-quatre valeurs.
float noiseValueCubic3D(vec3 p, uint seed)
{
    return noiseInternalValue3D(ivec3(floor(p)), noiseFadeCubic(fract(p)), seed);
}

// Version [0,1] du bruit de valeurs cubique ; mêmes huit coins.
// Un remappage affine, sans ajouter de continuité au fade cubique.
float noiseValueCubic3D01(vec3 p, uint seed) { return noiseTo01(noiseValueCubic3D(p, seed)); }

// Perlin dans [-1,1], huit gradients et interpolation quintique.
// Hash, gradients et ordre des mix historiques conservés EXACTEMENT ; passer
// worldSeed ^ salt remplace l'ancien champ sans changer son échantillonnage.
float noisePerlin3D(vec3 p, uint seed)
{
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p), u = noiseFadeQuintic(f);
    float n000 = noiseInternalGradient3D(noiseInternalHash3D(i, seed), f);
    float n100 = noiseInternalGradient3D(noiseInternalHash3D(i+ivec3(1,0,0),seed), f-vec3(1,0,0));
    float n010 = noiseInternalGradient3D(noiseInternalHash3D(i+ivec3(0,1,0),seed), f-vec3(0,1,0));
    float n110 = noiseInternalGradient3D(noiseInternalHash3D(i+ivec3(1,1,0),seed), f-vec3(1,1,0));
    float n001 = noiseInternalGradient3D(noiseInternalHash3D(i+ivec3(0,0,1),seed), f-vec3(0,0,1));
    float n101 = noiseInternalGradient3D(noiseInternalHash3D(i+ivec3(1,0,1),seed), f-vec3(1,0,1));
    float n011 = noiseInternalGradient3D(noiseInternalHash3D(i+ivec3(0,1,1),seed), f-vec3(0,1,1));
    float n111 = noiseInternalGradient3D(noiseInternalHash3D(i+ivec3(1,1,1),seed), f-vec3(1,1,1));
    return clamp(mix(mix(mix(n000,n100,u.x),mix(n010,n110,u.x),u.y),
        mix(mix(n001,n101,u.x),mix(n011,n111,u.x),u.y),u.z),-1.0,1.0);
}

// Version [0,1] de Perlin : 0.5 + 0.5*n, sans clamp supplémentaire.
// Un seul Perlin est évalué ; la seed et les huit gradients sont inchangés.
float noisePerlin3D01(vec3 p, uint seed) { return noiseTo01(noisePerlin3D(p, seed)); }

// Perlin périodique dans [-1,1], period en nombre entier de cellules par axe.
// period est borné à [1,2^24] ; seuls les indices des gradients sont repliés,
// pas fract(p). Huit hashes et les modulos, raccords quintiques conservés.
float noisePerlinPeriodic3D(vec3 p, uint seed, ivec3 period)
{
    period = clamp(period, ivec3(1), ivec3(16777216));
    ivec3 i = ivec3(floor(p));
    vec3 f = fract(p), u = noiseFadeQuintic(f);
    float n000 = noiseInternalGradient3D(noiseInternalHash3D(noiseInternalWrap3D(i, period), seed), f);
    float n100 = noiseInternalGradient3D(noiseInternalHash3D(noiseInternalWrap3D(i+ivec3(1,0,0),period),seed), f-vec3(1,0,0));
    float n010 = noiseInternalGradient3D(noiseInternalHash3D(noiseInternalWrap3D(i+ivec3(0,1,0),period),seed), f-vec3(0,1,0));
    float n110 = noiseInternalGradient3D(noiseInternalHash3D(noiseInternalWrap3D(i+ivec3(1,1,0),period),seed), f-vec3(1,1,0));
    float n001 = noiseInternalGradient3D(noiseInternalHash3D(noiseInternalWrap3D(i+ivec3(0,0,1),period),seed), f-vec3(0,0,1));
    float n101 = noiseInternalGradient3D(noiseInternalHash3D(noiseInternalWrap3D(i+ivec3(1,0,1),period),seed), f-vec3(1,0,1));
    float n011 = noiseInternalGradient3D(noiseInternalHash3D(noiseInternalWrap3D(i+ivec3(0,1,1),period),seed), f-vec3(0,1,1));
    float n111 = noiseInternalGradient3D(noiseInternalHash3D(noiseInternalWrap3D(i+ivec3(1,1,1),period),seed), f-vec3(1,1,1));
    return clamp(mix(mix(mix(n000,n100,u.x),mix(n010,n110,u.x),u.y),
        mix(mix(n001,n101,u.x),mix(n011,n111,u.x),u.y),u.z),-1.0,1.0);
}

// Version [0,1] de Perlin périodique, mêmes périodes et raccords par axe.
// Un remappage affine ; le coût reste celui des huit gradients repliés.
float noisePerlinPeriodic3D01(vec3 p, uint seed, ivec3 period)
{
    return noiseTo01(noisePerlinPeriodic3D(p, seed, period));
}

// Simplex dans [-1,1] : quatre gradients sur une grille de tétraèdres.
// Géométrie décrite par Stefan Gustavson, implémentation avec notre hash :
// https://itn-web.it.liu.se/~stegu76/simplexnoise/simplexnoise.pdf . Aucune table
// de permutation copiée ; quatre supports radiaux, facteur 32 puis clamp.
float noiseSimplex3D(vec3 p, uint seed)
{
    const float skew = 1.0 / 3.0;
    const float unskew = 1.0 / 6.0;
    ivec3 cell = ivec3(floor(p + (p.x + p.y + p.z) * skew));
    float originOffset = (float(cell.x) + float(cell.y) + float(cell.z)) * unskew;
    vec3 d0 = p - (vec3(cell) - originOffset);
    ivec3 rank = ivec3(0);
    if (d0.x >= d0.y) ++rank.x; else ++rank.y;
    if (d0.x >= d0.z) ++rank.x; else ++rank.z;
    if (d0.y >= d0.z) ++rank.y; else ++rank.z;
    ivec3 first = ivec3(greaterThanEqual(rank, ivec3(2)));
    ivec3 second = ivec3(greaterThanEqual(rank, ivec3(1)));
    vec3 d1 = d0 - vec3(first) + unskew;
    vec3 d2 = d0 - vec3(second) + 2.0 * unskew;
    vec3 d3 = d0 - 1.0 + 3.0 * unskew;
    vec4 attenuation = max(vec4(0.6) - vec4(dot(d0,d0),dot(d1,d1),dot(d2,d2),dot(d3,d3)), 0.0);
    attenuation *= attenuation;
    attenuation *= attenuation;
    vec4 gradients = vec4(
        noiseInternalSimplexGradient3D(noiseInternalHash3D(cell, seed), d0),
        noiseInternalSimplexGradient3D(noiseInternalHash3D(cell + first, seed), d1),
        noiseInternalSimplexGradient3D(noiseInternalHash3D(cell + second, seed), d2),
        noiseInternalSimplexGradient3D(noiseInternalHash3D(cell + ivec3(1), seed), d3));
    return clamp(32.0 * dot(attenuation, gradients), -1.0, 1.0);
}

// Version [0,1] de Simplex ; mêmes quatre coins et supports radiaux.
// Un remappage affine, sans nouveau hash ni nouvelle grille.
float noiseSimplex3D01(vec3 p, uint seed) { return noiseTo01(noiseSimplex3D(p, seed)); }

// Voronoï brut : x=distance F1, y=valeur stable du site dans [0,1].
// Recherche 3x3x3 avec jitter [0.35,0.65] ; métriques Euclidienne, Manhattan,
// Chebyshev, autre entier=>Euclidienne. Site DISCONTINU aux frontières.
// Le parcours z/y/x, le test strict < et F1 Euclidien historiques sont conservés.
vec2 noiseCellularData3D(vec3 p, uint seed, int metric)
{
    ivec3 cell = ivec3(floor(p));
    vec3 f = fract(p);
    float best = 100.0;
    float site = 0.0;
    for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        ivec3 q = ivec3(x,y,z);
        uint h = noiseInternalHash3D(cell+q,seed);
        uint h2 = noiseInternalHash(h^0x68bc21ebu);
        vec3 jitter = 0.35+0.30*vec3(h&65535u,h>>16u,h2&65535u)*(1.0/65535.0);
        vec3 delta = vec3(q)+jitter-f;
        vec3 distanceAxis = abs(delta);
        float d = metric == NOISE_MANHATTAN ? distanceAxis.x+distanceAxis.y+distanceAxis.z :
                  metric == NOISE_CHEBYSHEV ? max(max(distanceAxis.x,distanceAxis.y),distanceAxis.z) : dot(delta,delta);
        if (d < best)
        {
            best = d;
            site = noiseInternalUintTo01(h2);
        }
    }
    return vec2(metric == NOISE_MANHATTAN || metric == NOISE_CHEBYSHEV ? best : sqrt(best),site);
}

// Même paire F1/site, avec la métrique Euclidienne par défaut ; 27 sites.
// La distance garde les unités de p ; elle n'est pas un bruit signé normalisé.
vec2 noiseCellularData3D(vec3 p, uint seed)
{
    return noiseCellularData3D(p, seed, NOISE_EUCLIDEAN);
}

// Distance F1 brute, positive, dans les unités de p ; 27 sites, aucun stockage.
// Bornes conservatives : 0.65*sqrt(3), 1.95 ou 0.65 selon la métrique.
float noiseCellularDistance3D(vec3 p, uint seed, int metric)
{
    return noiseCellularData3D(p, seed, metric).x;
}

// Distance F1 Euclidienne par défaut ; même recherche de 27 sites.
// La sortie n'est pas remappée : utiliser Voronoi pour une plage [-1,1]/[0,1].
float noiseCellularDistance3D(vec3 p, uint seed)
{
    return noiseCellularDistance3D(p, seed, NOISE_EUCLIDEAN);
}

// Deux distances Euclidiennes AU CARRÉ, F1²<=F2² ; 27 sites, aucune racine.
// Preuve du voisinage avec jitter [0.35,0.65] : tout site extérieur est à
// distance >=1.35 ; le site courant est à <=0.65*sqrt(3). Pour le voisin vers
// la face la plus proche, avec d=max(abs(fract(p)-0.5)) dans [0,0.5],
// distance²<=(1.15-d)²+2*(0.15+d)²<=1.3675<1.35². Deux sites intérieurs
// précèdent donc tous les sites extérieurs. Cette garantie dépend du jitter.
vec2 noiseCellularDistancesSquared3D(vec3 p, uint seed)
{
    ivec3 cell = ivec3(floor(p));
    vec3 f = fract(p);
    vec2 nearest = vec2(100.0);
    for (int z = -1; z <= 1; ++z)
    for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
    {
        ivec3 q = ivec3(x,y,z);
        uint h = noiseInternalHash3D(cell+q,seed);
        uint h2 = noiseInternalHash(h^0x68bc21ebu);
        vec3 jitter = 0.35+0.30*vec3(h&65535u,h>>16u,h2&65535u)*(1.0/65535.0);
        vec3 delta = vec3(q)+jitter-f;
        float d = dot(delta,delta);
        nearest.y = min(nearest.y,max(nearest.x,d));
        nearest.x = min(nearest.x,d);
    }
    return nearest;
}

// F1 normalisé dans [0,1], divisé par une borne conservative de sa métrique.
// Le site courant suffit à cette borne ; aucune promesse d'atteindre 1 et
// aucun gain empirique. Coût identique à F1 brut : recherche de 27 sites.
float noiseVoronoi3D01(vec3 p, uint seed, int metric)
{
    float bound = metric == NOISE_MANHATTAN ? 1.95 :
                  metric == NOISE_CHEBYSHEV ? 0.65 : 1.1258330249197703;
    return clamp(noiseCellularDistance3D(p,seed,metric) / bound, 0.0, 1.0);
}

// Version signée [-1,1] du F1 normalisé ; mêmes coordonnées et métrique.
// Une recherche cellulaire puis un remappage affine, sans nouveau site.
float noiseVoronoi3D(vec3 p, uint seed, int metric)
{
    return noiseToSigned(noiseVoronoi3D01(p, seed, metric));
}

// Voronoï signé Euclidien par défaut ; normalisation conservative identique.
// Une recherche de 27 sites ; préférer F1 brut pour une distance géométrique.
float noiseVoronoi3D(vec3 p, uint seed)
{
    return noiseVoronoi3D(p, seed, NOISE_EUCLIDEAN);
}

// Voronoï [0,1] Euclidien par défaut ; sortie normalisée, pas une distance.
// Une recherche de 27 sites comme la version paramétrée par une métrique.
float noiseVoronoi3D01(vec3 p, uint seed)
{
    return noiseVoronoi3D01(p, seed, NOISE_EUCLIDEAN);
}

// Valeur aléatoire [0,1] du site Voronoï le plus proche, selon metric.
// Constante dans une région et DISCONTINUE aux frontières ; 27 sites. Un
// déplacement de surface doit masquer ces frontières avec un support adapté.
float noiseCellValue3D01(vec3 p, uint seed, int metric)
{
    return noiseCellularData3D(p, seed, metric).y;
}

// Valeur de site remappée dans [-1,1], mêmes discontinuités et même métrique.
// Une recherche cellulaire puis un remappage affine, aucun échantillon ajouté.
float noiseCellValue3D(vec3 p, uint seed, int metric)
{
    return noiseToSigned(noiseCellValue3D01(p, seed, metric));
}

// Valeur de site signée, métrique Euclidienne par défaut ; 27 sites.
// Le résultat identifie une région, sans continuité garantie entre régions.
float noiseCellValue3D(vec3 p, uint seed)
{
    return noiseCellValue3D(p, seed, NOISE_EUCLIDEAN);
}

// Valeur de site [0,1], métrique Euclidienne par défaut ; 27 sites.
// Aucune interpolation des valeurs de deux sites voisins n'est effectuée.
float noiseCellValue3D01(vec3 p, uint seed)
{
    return noiseCellValue3D01(p, seed, NOISE_EUCLIDEAN);
}

// ---------------------------------------------------------------------------
// Compositions : crêtes, fractales, déformations et motifs spécialisés.
// ---------------------------------------------------------------------------

// Crête mono-octave dans [0,1] : 1-abs(Perlin), sans carré ni normalisation.
// Un Perlin et une valeur absolue ; différente de la fractale Ridged.
float noiseRidge3D01(vec3 p, uint seed) { return 1.0-abs(noisePerlin3D(p,seed)); }

// Version signée [-1,1] de la crête mono-octave, mêmes coordonnées et seed.
// Un seul Perlin ; le remappage affine ne change pas la forme des crêtes.
float noiseRidge3D(vec3 p, uint seed) { return noiseToSigned(noiseRidge3D01(p,seed)); }

// fBm de Perlin normalisé dans [-1,1], un bruit par octave, au plus huit.
// octaves borné à [0,8], lacunarity à [1,8], gain à [0,1] ; zéro octave=>0.
// Les coordonnées de toutes les octaves doivent rester dans le domaine entier.
// Un gain nul arrête après la première octave, sans division par zéro.
float noiseFbm3D(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    octaves = clamp(octaves, 0, NOISE_MAX_OCTAVES);
    lacunarity = clamp(lacunarity, 1.0, 8.0);
    gain = clamp(gain, 0.0, 1.0);
    if (octaves == 0) return 0.0;
    float sum = 0.0, normalization = 0.0, amplitude = 1.0;
    for (int i = 0; i < NOISE_MAX_OCTAVES; ++i)
    {
        if (i >= octaves || amplitude == 0.0) break;
        sum += amplitude * noisePerlin3D(p, seed + uint(i) * 0x9e3779b9u);
        normalization += amplitude;
        p *= lacunarity;
        amplitude *= gain;
    }
    return clamp(sum / normalization, -1.0, 1.0);
}

// Version [0,1] du fBm Perlin, mêmes octaves, fréquences et amplitudes.
// Zéro octave donne 0.5 sans évaluer de bruit ; au plus huit Perlin.
float noiseFbm3D01(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseFbm3D(p, seed, octaves, lacunarity, gain));
}

// fBm de valeurs quintiques normalisé dans [-1,1], huit hashes par octave.
// Paramètres bornés comme fBm : octaves [0,8], lacunarity [1,8], gain [0,1].
// Zéro octave=>0 ; la somme des amplitudes borne la normalisation.
float noiseValueFbm3D(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    octaves = clamp(octaves, 0, NOISE_MAX_OCTAVES);
    lacunarity = clamp(lacunarity, 1.0, 8.0);
    gain = clamp(gain, 0.0, 1.0);
    if (octaves == 0) return 0.0;
    float sum = 0.0, normalization = 0.0, amplitude = 1.0;
    for (int i = 0; i < NOISE_MAX_OCTAVES; ++i)
    {
        if (i >= octaves || amplitude == 0.0) break;
        sum += amplitude * noiseValue3D(p, seed + uint(i) * 0x9e3779b9u);
        normalization += amplitude;
        p *= lacunarity;
        amplitude *= gain;
    }
    return clamp(sum / normalization, -1.0, 1.0);
}

// Version [0,1] du fBm de valeurs ; zéro octave donne 0.5.
// Même échantillonnage et mêmes bornes de paramètres que la version signée.
float noiseValueFbm3D01(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseValueFbm3D(p, seed, octaves, lacunarity, gain));
}

// fBm Simplex normalisé dans [-1,1], quatre gradients par octave.
// Même bornage [0,8]/[1,8]/[0,1] et même seed par octave que les autres fBm.
// Zéro octave=>0 ; la grille déformée de chaque octave doit rester représentable.
float noiseSimplexFbm3D(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    octaves = clamp(octaves, 0, NOISE_MAX_OCTAVES);
    lacunarity = clamp(lacunarity, 1.0, 8.0);
    gain = clamp(gain, 0.0, 1.0);
    if (octaves == 0) return 0.0;
    float sum = 0.0, normalization = 0.0, amplitude = 1.0;
    for (int i = 0; i < NOISE_MAX_OCTAVES; ++i)
    {
        if (i >= octaves || amplitude == 0.0) break;
        sum += amplitude * noiseSimplex3D(p, seed + uint(i) * 0x9e3779b9u);
        normalization += amplitude;
        p *= lacunarity;
        amplitude *= gain;
    }
    return clamp(sum / normalization, -1.0, 1.0);
}

// Version [0,1] du fBm Simplex ; zéro octave donne 0.5.
// Un remappage affine, sans ajouter de gradient aux quatre par octave.
float noiseSimplexFbm3D01(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseSimplexFbm3D(p, seed, octaves, lacunarity, gain));
}

// Turbulence signée : moyenne pondérée de abs(Perlin), remappée dans [-1,1].
// Jusqu'à huit bruits ; bornes d'octaves/lacunarity/gain identiques au fBm.
// Les valeurs absolues créent des plis ; zéro octave reste la valeur neutre 0.
float noiseTurbulence3D(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    octaves = clamp(octaves, 0, NOISE_MAX_OCTAVES);
    lacunarity = clamp(lacunarity, 1.0, 8.0);
    gain = clamp(gain, 0.0, 1.0);
    if (octaves == 0) return 0.0;
    float sum = 0.0, normalization = 0.0, amplitude = 1.0;
    for (int i = 0; i < NOISE_MAX_OCTAVES; ++i)
    {
        if (i >= octaves || amplitude == 0.0) break;
        sum += amplitude * abs(noisePerlin3D(p, seed + uint(i) * 0x9e3779b9u));
        normalization += amplitude;
        p *= lacunarity;
        amplitude *= gain;
    }
    return noiseToSigned(clamp(sum / normalization, 0.0, 1.0));
}

// Turbulence dans [0,1], sa plage naturelle ; zéro octave donne 0.5.
// Au plus huit Perlin, mêmes graines et normalisation que la version signée.
float noiseTurbulence3D01(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseTurbulence3D(p, seed, octaves, lacunarity, gain));
}

// Crêtes fractales : moyenne de (1-abs(Perlin))², remappée dans [-1,1].
// Même bornage de paramètres que fBm, zéro octave=>0, au plus huit bruits.
// Le carré resserre les crêtes ; aucune rétroaction ni amplitude cachée.
float noiseRidged3D(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    octaves = clamp(octaves, 0, NOISE_MAX_OCTAVES);
    lacunarity = clamp(lacunarity, 1.0, 8.0);
    gain = clamp(gain, 0.0, 1.0);
    if (octaves == 0) return 0.0;
    float sum = 0.0, normalization = 0.0, amplitude = 1.0;
    for (int i = 0; i < NOISE_MAX_OCTAVES; ++i)
    {
        if (i >= octaves || amplitude == 0.0) break;
        float ridge = 1.0 - abs(noisePerlin3D(p, seed + uint(i) * 0x9e3779b9u));
        sum += amplitude * ridge * ridge;
        normalization += amplitude;
        p *= lacunarity;
        amplitude *= gain;
    }
    return noiseToSigned(clamp(sum / normalization, 0.0, 1.0));
}

// Version [0,1] des crêtes fractales ; zéro octave donne 0.5.
// Même coût et mêmes paramètres bornés que la version signée.
float noiseRidged3D01(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseRidged3D(p, seed, octaves, lacunarity, gain));
}

// Billow est ici un alias de la turbulence signée : 2*moyenne(abs(Perlin))-1.
// Ce n'est pas une primitive distincte ; même coût, mêmes bornes de paramètres
// et même valeur neutre que noiseTurbulence3D, sans seconde évaluation.
float noiseBillow3D(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTurbulence3D(p, seed, octaves, lacunarity, gain);
}

// Alias [0,1] de la turbulence ; zéro octave donne 0.5.
// Un remappage affine, sans nouvelle couche de bruit ni coût caché.
float noiseBillow3D01(vec3 p, uint seed, int octaves, float lacunarity, float gain)
{
    return noiseTo01(noiseBillow3D(p, seed, octaves, lacunarity, gain));
}

// Trois Perlin décorrélés : chaque composante du vecteur est dans [-1,1].
// Trois bruits, graines dérivées par XOR ; la longueur peut atteindre sqrt(3),
// ce n'est donc ni une direction unitaire ni un champ sans divergence.
vec3 noiseVector3D(vec3 p, uint seed)
{
    return vec3(noisePerlin3D(p, seed ^ 0x68bc21ebu),
                noisePerlin3D(p, seed ^ 0x02e5be93u),
                noisePerlin3D(p, seed ^ 0x967a889bu));
}

// Version composante par composante dans [0,1]^3 ; mêmes trois Perlin.
// Le remappage ne normalise pas la longueur et ne crée pas une direction.
vec3 noiseVector3D01(vec3 p, uint seed)
{
    return vec3(0.5) + 0.5 * noiseVector3D(p, seed);
}

// Déforme p par trois Perlin ; retourne des COORDONNÉES dans les unités de p.
// amplitude peut être signée ; frequency est ramenée à >=0. Déplacement par
// axe <=abs(amplitude). amplitude ou fréquence nulles évitent les trois bruits.
vec3 noiseDomainWarp3D(vec3 p, uint seed, float amplitude, float frequency)
{
    frequency = max(frequency, 0.0);
    if (amplitude == 0.0 || frequency == 0.0) return p;
    return p + amplitude * noiseVector3D(p * frequency, seed);
}

// Perlin dans un domaine déformé, retour [-1,1] : trois bruits de déplacement
// puis un bruit final. L'appelant applique l'échelle générale aux coordonnées p.
float noiseWarpedPerlin3D(vec3 p, uint seed, float amplitude, float frequency)
{
    return noisePerlin3D(noiseDomainWarp3D(p, seed, amplitude, frequency), seed);
}

// Version [0,1] du Perlin déformé ; au plus quatre échantillons Perlin.
// Même ordre amplitude/fréquence et mêmes coordonnées que la version signée.
float noiseWarpedPerlin3D01(vec3 p, uint seed, float amplitude, float frequency)
{
    return noiseTo01(noiseWarpedPerlin3D(p, seed, amplitude, frequency));
}

// Marbre : bandes sinusoïdales suivant p.y, perturbées par un Perlin, [-1,1].
// frequency>=0 en cycles/unité de p ; distortion est une phase signée en cycles.
// Un Perlin et un sinus ; déplacer ou tourner p pour orienter les bandes.
float noiseMarble3D(vec3 p, uint seed, float frequency, float distortion)
{
    float phase = p.y * max(frequency, 0.0) + distortion * noisePerlin3D(p, seed);
    return sin(6.28318530717958647692 * phase);
}

// Marbre dans [0,1] ; même phase, fréquence et distortion que la version signée.
// Un remappage affine, aucun échantillonnage ni motif supplémentaire.
float noiseMarble3D01(vec3 p, uint seed, float frequency, float distortion)
{
    return noiseTo01(noiseMarble3D(p, seed, frequency, distortion));
}

// Bois : anneaux sinusoïdaux autour de l'axe Y, perturbés par un Perlin, [-1,1].
// frequency>=0 en cycles/unité ; distortion en cycles signés. Une longueur,
// un Perlin et un sinus ; transformer p pour changer l'axe, le centre et l'échelle.
float noiseWood3D(vec3 p, uint seed, float frequency, float distortion)
{
    float phase = length(p.xz) * max(frequency, 0.0) + distortion * noisePerlin3D(p, seed);
    return sin(6.28318530717958647692 * phase);
}

// Bois dans [0,1] ; mêmes anneaux autour de Y et même déformation de phase.
// Un remappage affine, sans nouvelle longueur ni échantillonnage de bruit.
float noiseWood3D01(vec3 p, uint seed, float frequency, float distortion)
{
    return noiseTo01(noiseWood3D(p, seed, frequency, distortion));
}

// Fissures dans [0,1] : masque de proximité de F2-F1=0, 27 sites et deux racines.
// width est dans les unités de p ; width<=0 désactive le masque. F2-F1 n'est
// pas une distance exacte à la frontière : la largeur géométrique peut varier.
float noiseCracks3D01(vec3 p, uint seed, float width)
{
    if (width <= 0.0) return 0.0;
    vec2 nearest = sqrt(noiseCellularDistancesSquared3D(p, seed));
    return 1.0 - smoothstep(0.0, width, nearest.y-nearest.x);
}

// Version signée [-1,1] des fissures ; width<=0 donne -1.
// Même recherche cellulaire, mêmes limites de largeur que la version 01.
float noiseCracks3D(vec3 p, uint seed, float width)
{
    return noiseToSigned(noiseCracks3D01(p, seed, width));
}

// Rotationnel d'un potentiel à trois Perlin indépendants : vec3 NON borné.
// Douze Perlin, différences centrées ; epsilon=max(abs(epsilon),1e-4) dans
// les unités de p. Approximation numérique, pas une direction normalisée ;
// un epsilon trop petit face à p perd de la précision en float32.
vec3 noiseCurl3D(vec3 p, uint seed, float epsilon)
{
    epsilon = max(abs(epsilon), 1e-4);
    vec3 dx = vec3(epsilon,0.0,0.0);
    vec3 dy = vec3(0.0,epsilon,0.0);
    vec3 dz = vec3(0.0,0.0,epsilon);
    uint sx = seed ^ 0x68bc21ebu;
    uint sy = seed ^ 0x02e5be93u;
    uint sz = seed ^ 0x967a889bu;
    float dAzdy = noisePerlin3D(p+dy,sz) - noisePerlin3D(p-dy,sz);
    float dAydz = noisePerlin3D(p+dz,sy) - noisePerlin3D(p-dz,sy);
    float dAxdz = noisePerlin3D(p+dz,sx) - noisePerlin3D(p-dz,sx);
    float dAzdx = noisePerlin3D(p+dx,sz) - noisePerlin3D(p-dx,sz);
    float dAydx = noisePerlin3D(p+dx,sy) - noisePerlin3D(p-dx,sy);
    float dAxdy = noisePerlin3D(p+dy,sx) - noisePerlin3D(p-dy,sx);
    return vec3(dAzdy-dAydz,dAxdz-dAzdx,dAydx-dAxdy) / (2.0 * epsilon);
}

#endif
