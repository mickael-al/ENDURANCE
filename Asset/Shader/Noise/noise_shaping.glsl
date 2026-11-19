// Transformations scalaires : aucune dimension spatiale ni seed implicite.
#ifndef ENDURANCE_NOISE_SHAPING_GLSL
#define ENDURANCE_NOISE_SHAPING_GLSL

// Conversion et normalisation des valeurs.

// Conversion affine de x dans [-1,1] vers [0,1], sans clamp implicite.
// Une entrée hors contrat est extrapolée ; coût : une multiplication/addition.
float noiseTo01(float x)
{
    return 0.5 + 0.5 * x;
}

// Conversion affine de x dans [0,1] vers [-1,1], sans clamp implicite.
// Une entrée hors contrat est extrapolée ; ne normalise pas une distance brute.
float noiseToSigned(float x)
{
    return x * 2.0 - 1.0;
}

// Borne une valeur scalaire finie x dans [0,1].
// Ne change pas les unités ni le centrage ; aucun échantillonnage de bruit.
float noiseSaturate(float x)
{
    return clamp(x, 0.0, 1.0);
}

// Remappe x de [inMin,inMax] vers [outMin,outMax], avec extrapolation.
// Les intervalles inversés sont acceptés ; inMin == inMax retourne outMin.
// Les valeurs sont finies et les différences/divisions doivent rester finies.
float noiseRemap(float x, float inMin, float inMax, float outMin, float outMax)
{
    if (inMin == inMax) return outMin;
    return mix(outMin, outMax, (x - inMin) / (inMax - inMin));
}

// Même remappage, avec facteur borné à [0,1] avant l'interpolation.
// Retour entre les deux bornes de sortie, même inversées ; entrée dégénérée => outMin.
// Paramètres finis ; ne sert pas à remplacer les contrôles de domaine spatial.
float noiseRemapClamped(float x, float inMin, float inMax, float outMin, float outMax)
{
    if (inMin == inMax) return outMin;
    return mix(outMin, outMax, clamp((x - inMin) / (inMax - inMin), 0.0, 1.0));
}

// Courbes scalaires artistiques.

// Contraste autour de 0.5, avec résultat borné dans [0,1].
// strength=1 conserve x dans [0,1], 0 donne 0.5 ; un facteur négatif inverse.
// Corps exactement identique à l'ancien contrast(value,strength) du terrain.
float noiseContrast01(float value, float strength)
{
    return clamp((value - 0.5) * strength + 0.5, 0.0, 1.0);
}

// Contraste autour d'un pivot explicite ; résultat toujours borné dans [0,1].
// strength=0 donne le pivot borné ; strength<0 inverse autour de ce pivot.
// Les entrées sont finies ; aucun bruit supplémentaire n'est évalué.
float noiseContrast01(float value, float strength, float pivot)
{
    return clamp((value - pivot) * strength + pivot, 0.0, 1.0);
}

// Profil triangulaire : 0 aux extrémités, 1 à x=0.5, retour dans [0,1].
// x est borné à [0,1] ; sur ce domaine, expression exacte de l'ancien courbe01.
float noiseTriangle01(float x)
{
    x = clamp(x, 0.0, 1.0);
    return 1.0 - abs(2.0 * x - 1.0);
}

// Courbe puissance pow(x,gamma) ; x borné à [0,1], gamma forcé à >= 0.0001.
// gamma>1 assombrit, gamma<1 éclaircit ; conserve exactement les extrémités 0/1.
// Retour [0,1] ; l'appel à pow coûte plus qu'une simple multiplication.
float noiseGamma01(float x, float gamma)
{
    return pow(clamp(x, 0.0, 1.0), max(gamma, 0.0001));
}

// Courbe concave sqrt(x), avec entrée et sortie bornées à [0,1].
// Éclaircit les valeurs intermédiaires ; la borne évite sqrt d'une valeur négative.
float noiseConcave01(float x)
{
    return sqrt(clamp(x, 0.0, 1.0));
}

// Courbe convexe x*x, avec entrée et sortie bornées à [0,1].
// Assombrit les valeurs intermédiaires ; une multiplication, sans appel à pow.
float noiseConvex01(float x)
{
    x = clamp(x, 0.0, 1.0);
    return x * x;
}

// Courbe sinusoïdale d'adoucissement de x borné à [0,1], retour dans [0,1].
// Reprend sineEase : 0 au départ, 1 à l'arrivée, pente nulle aux deux extrémités.
// Un cosinus ; préférer noiseFadeCubic si cette forme précise n'est pas nécessaire.
float noiseSineEase01(float x)
{
    x = clamp(x, 0.0, 1.0);
    return 0.5 - 0.5 * cos(x * 3.14159265358979323846);
}

// Biais rationnel de x dans [0,1] ; bias=0.5 est l'identité.
// bias<0.5 abaisse les valeurs, bias>0.5 les relève ; les extrémités restent 0/1.
// bias est borné à [0.0001,0.9999] pour éviter les divisions singulières.
float noiseBias01(float x, float bias)
{
    x = clamp(x, 0.0, 1.0);
    bias = clamp(bias, 0.0001, 0.9999);
    return x / (((1.0 / bias) - 2.0) * (1.0 - x) + 1.0);
}

// Gain symétrique autour de 0.5 : gain=0.5 est l'identité, gain>0.5 contraste.
// Entrée/sortie [0,1], points 0, 0.5 et 1 conservés ; gain borné à [0.0001,0.9999].
// Utilise un seul biais rationnel sur la moitié concernée du domaine.
float noiseGain01(float x, float gain)
{
    x = clamp(x, 0.0, 1.0);
    gain = clamp(gain, 0.0001, 0.9999);
    return x < 0.5 ? 0.5 * noiseBias01(2.0 * x, 1.0 - gain)
                   : 1.0 - 0.5 * noiseBias01(2.0 - 2.0 * x, 1.0 - gain);
}

// Transition cubique dans [0,1] entre deux seuils finis edge0 et edge1.
// Bords égaux : step(edge0,x) ; bords inversés : transition décroissante.
// Sur edge0<edge1, conserve directement le smoothstep GLSL habituel.
float noiseSmoothstep(float edge0, float edge1, float x)
{
    if (edge0 == edge1) return step(edge0, x);
    if (edge0 > edge1) return 1.0 - smoothstep(edge1, edge0, x);
    return smoothstep(edge0, edge1, x);
}

// Quantification et profils de relief, indépendants de la dimension spatiale.

// Arrondit x au plus proche de levels niveaux uniformes incluant exactement 0 et 1.
// Entrée/sortie [0,1] ; levels est borné à [2,16777216], la résolution du float.
// Aux demi-intervalles, choisit le niveau supérieur ; aucun niveau ne dépasse 1.
float noiseQuantize01(float x, int levels)
{
    x = clamp(x, 0.0, 1.0);
    float intervals = float(clamp(levels, 2, 16777216) - 1);
    return clamp(floor(x * intervals + 0.5) / intervals, 0.0, 1.0);
}

// Terrasses sur levels niveaux incluant 0 et 1 ; x borné à [0,1].
// softness dans [0,1] règle la largeur des rampes ; 0 donne les marches dures.
// levels est borné à [2,16777216] ; retour [0,1], sans division par levels-1 nul.
float noiseTerraces01(float x, int levels, float softness)
{
    x = clamp(x, 0.0, 1.0);
    float intervals = float(clamp(levels, 2, 16777216) - 1);
    float cell = x * intervals;
    float halfWidth = 0.5 * clamp(softness, 0.0, 1.0);
    float ramp = noiseSmoothstep(0.5 - halfWidth, 0.5 + halfWidth, fract(cell));
    return clamp((floor(cell) + ramp) / intervals, 0.0, 1.0);
}

// Attire une valeur vers round(value), avec strength borné à [0,1].
// 0 conserve la valeur, 1 atteint l'entier ; sortie dans les unités d'entrée.
// Les égalités à un demi-entier suivent la convention de round du pilote GLSL.
float noiseSnap(float value, float strength)
{
    float nearest = round(value);
    return mix(value, nearest, clamp(strength, 0.0, 1.0));
}

// Bosse radiale : 1 au centre, 0 à partir de radius ; sortie [0,1].
// distanceToSite et radius ont les mêmes unités ; radius<=0 désactive le profil.
// Pour radius>0, expression exacte de l'ancien fieldSurfaceMound.
float noiseMound01(float distanceToSite, float radius)
{
    if (radius <= 0.0) return 0.0;
    return 1.0 - smoothstep(0.0, radius, distanceToSite);
}

// Plateau radial : 1 jusqu'à radius*crown, puis descente vers 0 à radius.
// crown borné à [0,1] ; crown=1 donne un disque dur, radius<=0 un profil nul.
// Retour [0,1] ; expression historique exacte pour radius>0 et 0<=crown<1.
float noiseMesa01(float distanceToSite, float radius, float crown)
{
    if (radius <= 0.0) return 0.0;
    crown = clamp(crown, 0.0, 1.0);
    float innerRadius = radius * crown;
    if (innerRadius == radius) return 1.0 - step(radius, distanceToSite);
    return 1.0 - smoothstep(innerRadius, radius, distanceToSite);
}

// Terrasse une hauteur, y compris négative ; stepHeight dans les mêmes unités.
// stepHeight<=0 conserve height ; softness borné à [0,1], 0 donne une marche dure.
// Sur les paramètres valides, conserve l'expression de fieldSurfaceTerrace.
float noiseTerrace(float height, float stepHeight, float softness)
{
    if (stepHeight <= 0.0) return height;
    float cell = height / stepHeight;
    float halfWidth = clamp(softness, 0.0, 1.0) * 0.5;
    if (0.5 - halfWidth == 0.5 + halfWidth)
        return (floor(cell) + step(0.5, fract(cell))) * stepHeight;
    return (floor(cell) + smoothstep(0.5 - halfWidth, 0.5 + halfWidth, fract(cell))) * stepHeight;
}

// Combinaison de champs signés ; la sortie conserve leurs unités.

// Minimum adouci de deux champs signés a/b ; radius utilise les mêmes unités.
// radius<=0 donne min(a,b) ; sinon le résultat est abaissé d'au plus radius/4.
// Utile pour les unions SDF ; la sortie n'est pas un bruit borné dans [-1,1].
float noiseSmoothMin(float a, float b, float radius)
{
    if (radius <= 0.0) return min(a, b);
    float h = max(radius - abs(a - b), 0.0) / radius;
    return min(a, b) - h * h * radius * 0.25;
}

// Maximum adouci de deux champs signés a/b ; radius utilise les mêmes unités.
// radius<=0 donne max(a,b) ; sinon le résultat est relevé d'au plus radius/4.
// Complément du minimum adouci, utile pour les intersections de champs signés.
float noiseSmoothMax(float a, float b, float radius)
{
    return -noiseSmoothMin(-a, -b, radius);
}

#endif
