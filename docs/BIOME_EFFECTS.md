# Effets environnementaux des biomes

Le catalogue du jeu contient **40 recettes visuelles : 28 effets passifs et
12 événements actifs**. Les recettes sont séparées de leur affectation aux
26 biomes. Plusieurs recettes réutilisent les mêmes formes de particules ou
les mêmes opérateurs d'écran ; elles ont leurs propres dimensions, couleurs,
densités, vitesses, turbulences, durées de vie et transitions.

Les données artistiques sont dans
`ANTAGONIZ/Source/ProjectUser/EnduranceBiomeEffectsCatalog.cpp`, avec les types
dans `ANTAGONIZ/Header/ProjectUser/EnduranceBiomeEffectsCatalog.hpp`.
`effectDefinitions()` contient les recettes ; `biomeEffectMatrix()` contient
les masques d'éligibilité. Les identifiants textuels ci-dessous servent aux commandes du jeu et de
l'éditeur. L'ancienne recette `cold_breath` a été remplacée par `ice_haze`
au même emplacement numérique : aucun effet de respiration n'est conservé.

Le contrôleur utilise la ligne du **biome dominant**. Une contribution de 1 %
d'un biome voisin n'active aucun de ses effets. Les événements marqués « actif »
ne démarrent pas parce que le joueur entre dans un biome : leur présence dans
la matrice signifie uniquement que le futur système d'événements peut les
demander à cet endroit.

## Les 40 recettes

La colonne « particules » indique la capacité du groupe quand il est actif,
à intensité non nulle, avec `Particle density = 1x`. Le réglage de densité
multiplie cette capacité ; celui d'intensité atténue l'opacité. À zéro, le groupe
est retiré. Un tiret désigne un effet d'écran uniquement.
Les couleurs d'émission sont composées dans le rendu HDR, avant le bloom.

| N° | Identifiant | Type | Aspect et mouvement | Particules |
| ---: | --- | --- | --- | ---: |
| 1 | `heat_haze` | Passif | Réfraction animée de l'image : vagues ascendantes à plusieurs échelles, protégées par la profondeur. | — |
| 2 | `sand_gust` | Passif | Grains ocre en couches basses, rafales locales et rouleaux turbulents ; l'air s'ajoute au transport propre du sable. | 15 360 |
| 3 | `volcanic_ash` | Passif | Éclats de cendre irréguliers, chute lente, vrilles et basculements ; silhouette distincte de la neige. | 11 520 |
| 4 | `wind_pollen` | Passif | Aigrettes jaune pâle en suspension, microvolutes et dérive lente ; faible ascension propre. | 3 840 |
| 5 | `fireflies` | Passif | Noyaux lumineux multicolores, petites boucles indépendantes et clignotements désynchronisés ; très faible sensibilité au vent. | 720 |
| 6 | `light_snow` | Passif | Flocons à branches et agrégats, chute de 0,25–0,65 m/s, balancement et rotation lente visible. | 15 360 |
| 7 | `frost_rim` | Passif | Dendrites stables qui croissent depuis les bords ; vignette utilisée comme progression, centre dégagé. | — |
| 8 | `ice_crystals` | Passif | Prismes anguleux en suspension, chute très lente et scintillement intermittent par rotation. | 2 560 |
| 9 | `ground_snow_drift` | Passif | Rubans horizontaux de neige soulevée, ondulations basses et dérive collective ; pas de rideaux verticaux. | 800 |
| 10 | `wind_leaves` | Passif | Feuilles pointues, chute en feuille morte et autorotation ; excursions latérales plus fortes que les pétales. | 1 920 |
| 11 | `petals` | Passif | Pétales arrondis et creusés, chute douce, balancement ample et basculement lent. | 2 080 |
| 12 | `dry_seeds` | Passif | Samares et aigrettes allongées, rotation plus rapide et descente hélicoïdale légère. | 3 840 |
| 13 | `dust_motes` | Passif | Très fines poussières presque suspendues, agitation lente et vent fortement amorti. | 3 840 |
| 14 | `ground_mist` | Passif | Nappes horizontales gris bleu, opacité faible, lente ondulation à grande échelle. | 400 |
| 15 | `swamp_vapor` | Passif | Volutes verdâtres qui montent et s'enroulent ; diffusion plus lente que la fumée volcanique. | 480 |
| 16 | `sea_spray` | Passif | Gouttelettes projetées vers le haut et de biais, puis chute balistique sous la gravité. | 8 960 |
| 17 | `salt_mist` | Passif | Nappes marines étirées horizontalement, plus fines et moins opaques que les embruns. | 400 |
| 18 | `volcanic_embers` | Passif | Braises en forme de goutte, ascension thermique, vrilles et extinction ; émission HDR vacillante. | 1 920 |
| 19 | `fumaroles` | Passif | Panaches gris brun verticaux, ascension propre et grandes volutes ; sources dans une couche basse. | 400 |
| 20 | `spores` | Passif | Grains à enveloppe cellulaire, remontée très lente et dérive tourbillonnante douce. | 3 840 |
| 21 | `glowing_spores` | Passif | Spores cyan à membrane lumineuse, faibles pulsations et boucles lentes ascendantes. | 1 280 |
| 22 | `canopy_drips` | Passif | Gouttes fines issues d'une couche haute, chute verticale accélérée, faible déviation par le vent. | 3 840 |
| 23 | `insects` | Passif | Petites silhouettes à ailes, virages nerveux et oscillations indépendantes ; vent très faible. | 640 |
| 24 | `wind_streaks` | Passif | Filets longs et transparents suivant des courbes larges ; durée plus longue et vitesse propre modérée. | 960 |
| 25 | `ice_haze` | Passif | Voiles glacés horizontaux dans l'environnement sur 240 m de demi-portée ; aucune origine bouche/caméra. | 640 |
| 26 | `wet_lens` | Passif | Gouttes réfractantes évoluant sur l'image, distinctes des précipitations dans le monde. | — |
| 27 | `condensation` | Passif | Buée périphérique douce ; faible évolution temporelle, sans pulsation de respiration. | — |
| 28 | `sand_veil` | Passif | Voile atmosphérique chaud associé aux poussières, brun rouge et peu opaque. | — |
| 29 | `rain` | Actif | Pluie à longue traînée, descente rapide dominante, faible divergence et réponse amortie au vent. | 30 720 |
| 30 | `heavy_rain` | Actif | Rideau dense de pluie, traînées plus longues, haut volume de précipitations ; gouttes et buée sur l'écran. | 61 440 |
| 31 | `sandstorm` | Actif | Nuages de grains rapides et rouleaux à grande échelle ; voile ocre et faible réfraction. | 61 440 |
| 32 | `blizzard` | Actif | Flocons tournoyants dans un flux incliné, baisse verticale conservée, givre et voile froid. | 61 440 |
| 33 | `ash_storm` | Actif | Fragments sombres irréguliers, grosses volutes et chute en diagonale ; voile gris brun. | 40 960 |
| 34 | `thunderstorm` | Actif | Pluie dense, inclinaison variable, flash d'écran bref et rémanence humide. | 46 080 |
| 35 | `hail` | Actif | Grêlons facettés lourds, chute rapide et peu sensible au vent ; rotations plus vives. | 20 480 |
| 36 | `sleet` | Actif | Précipitations courtes et denses entre pluie et neige, inclinaison modérée, eau et givre léger. | 35 840 |
| 37 | `meteor_shower` | Actif | Fragments incandescents en trajectoire balistique haute, longues traînées et aucune influence du vent. | 320 |
| 38 | `lunar_dust_fall` | Actif | Éclats minéraux gris violet, chute lente avec rotation et voile froid discret. | 12 800 |
| 39 | `pollen_burst` | Actif | Envol dense d'aigrettes en volutes ascendantes, plus de turbulence que le pollen passif. | 20 480 |
| 40 | `spore_bloom` | Actif | Colonies de spores lumineuses cyan/violet en spirales ascendantes, membranes et pulsations lentes. | 9 600 |

Les gouttes, grêlons et météores sont des effets visuels : ces recettes ne
définissent ni impacts physiques, ni dégâts, ni collisions avec le terrain.
L'orage comporte des flashes de lumière d'écran ; il ne crée pas une lumière
dynamique supplémentaire ni une carte d'ombres supplémentaire.

## Affectation aux 26 biomes

`meteor_shower` et `lunar_dust_fall` sont autorisés dans **tous** les biomes,
comme événements liés à la dislocation de la lune. Ils sont omis de la dernière
colonne pour garder la table lisible ; ils restent présents dans chaque masque.

| Biome | Passifs à l'entrée | Autres événements actifs autorisés |
| --- | --- | --- |
| `PolarIce` | `light_snow`, `frost_rim`, `ice_crystals`, `ice_haze` | `blizzard`, `hail`, `sleet` |
| `FrozenTundra` | `ground_snow_drift`, `ice_crystals`, `wind_streaks` | `blizzard`, `hail`, `sleet` |
| `Tundra` | `dry_seeds`, `light_snow`, `ice_haze` | `blizzard`, `sleet`, `hail` |
| `Taiga` | `light_snow`, `wind_leaves`, `dust_motes` | `rain`, `blizzard`, `sleet`, `pollen_burst` |
| `BorealBog` | `ground_mist`, `spores`, `condensation` | `rain`, `sleet`, `spore_bloom` |
| `RockyTundra` | `wind_streaks`, `ice_crystals`, `ice_haze` | `blizzard`, `hail`, `sleet` |
| `ColdSteppe` | `ground_snow_drift`, `dry_seeds`, `wind_streaks` | `blizzard`, `hail`, `sleet` |
| `BorealForest` | `wind_pollen`, `wind_leaves`, `light_snow` | `rain`, `sleet`, `blizzard`, `pollen_burst` |
| `ColdForest` | `ground_mist`, `canopy_drips`, `condensation` | `rain`, `heavy_rain`, `hail`, `sleet`, `spore_bloom` |
| `ColdMarsh` | `swamp_vapor`, `glowing_spores`, `frost_rim` | `rain`, `sleet`, `spore_bloom` |
| `DryPlain` | `dry_seeds`, `dust_motes`, `wind_streaks` | `rain`, `sandstorm`, `pollen_burst` |
| `Meadow` | `wind_pollen`, `petals`, `insects` | `rain`, `heavy_rain`, `thunderstorm`, `hail`, `pollen_burst` |
| `TemperateForest` | `wind_leaves`, `fireflies`, `dust_motes` | `rain`, `heavy_rain`, `thunderstorm`, `pollen_burst` |
| `TemperateRainforest` | `canopy_drips`, `wet_lens`, `condensation` | `rain`, `heavy_rain`, `thunderstorm`, `spore_bloom` |
| `TemperateMarsh` | `swamp_vapor`, `insects`, `fireflies` | `rain`, `heavy_rain`, `thunderstorm`, `spore_bloom` |
| `RockDesert` | `heat_haze`, `sand_gust`, `dust_motes` | `sandstorm` |
| `DrySavanna` | `heat_haze`, `dry_seeds`, `dust_motes` | `rain`, `sandstorm`, `pollen_burst` |
| `Savanna` | `wind_pollen`, `dry_seeds`, `insects` | `rain`, `heavy_rain`, `thunderstorm`, `pollen_burst` |
| `DryJungle` | `spores`, `wind_leaves`, `heat_haze` | `rain`, `thunderstorm`, `pollen_burst`, `spore_bloom` |
| `TropicalJungle` | `spores`, `glowing_spores`, `wet_lens`, `canopy_drips` | `rain`, `heavy_rain`, `thunderstorm`, `spore_bloom` |
| `VolcanicDesert` | `volcanic_ash`, `volcanic_embers`, `fumaroles`, `heat_haze` | `ash_storm`, `sandstorm` |
| `RedDesert` | `sand_gust`, `sand_veil`, `heat_haze` | `sandstorm` |
| `AridPlateau` | `sand_gust`, `dust_motes`, `wind_streaks` | `sandstorm`, `hail` |
| `BurningJungle` | `volcanic_ash`, `volcanic_embers`, `fumaroles`, `heat_haze` | `ash_storm`, `rain`, `thunderstorm` |
| `TropicalMangrove` | `sea_spray`, `swamp_vapor`, `fireflies`, `canopy_drips` | `rain`, `heavy_rain`, `thunderstorm`, `spore_bloom` |
| `Marine` | `sea_spray`, `salt_mist`, `wind_streaks` | `rain`, `heavy_rain`, `thunderstorm`, `hail` |

## Portée et budgets

La révision visuelle multiplie par **10 le nombre de particules** et par
**10 la demi-portée horizontale XZ** de chaque recette existante. Les constantes
`ParticlePopulationScale` et `ParticleHorizontalReachScale` sont dans le header
du catalogue. Les arguments de `particles(...)` sont les valeurs de référence
avant ces deux multiplicateurs ; le tableau ci-dessus donne les capacités finales.

Les effets courants couvrent maintenant **140 à 320 mètres autour de la caméra**,
soit jusqu'à 640 mètres d'un bord à l'autre ; les fragments lunaires couvrent
2,2 km de demi-portée. `ice_haze`, qui remplace la respiration, est un nouveau
volume environnemental de 240 mètres de demi-portée.

Le volume est réparti en zones proche, intermédiaire et lointaine. Une simple
boîte dix fois plus grande dans les trois axes aurait dilué la population sur
un volume mille fois plus grand : les couches verticales sont donc définies
par effet. La neige et la pluie occupent des couches hautes ; la brume basse
conserve une couche mince. Ces hauteurs définissent la **naissance**, pas une
cage qui empêcherait fumées et braises de monter pendant leur vie.

- Maximum passif : **21 120 particules**, dans `Taiga` et `BorealForest`, à 1x.
- Neige légère : **15 360** ; cendres : **11 520** ; sable : **15 360** ;
  embruns : **8 960**.
- Forte pluie, tempête de sable et blizzard : **61 440** chacun à 1x.
- Les nappes transparentes ont **400–800** instances avec une opacité et une
  forme adaptées aux couches. Leur aspect horizontal est défini par un ratio
  hauteur/largeur inférieur à 1 ; les fumerolles conservent des panaches verticaux.
- Budget total : **524 288 particules, 8 groupes**, transitions comprises.
  Toutes les combinaisons autorisées d'un biome et deux événements tiennent
  dans ce budget même à 4x ; les transitions peuvent temporairement le saturer.
- Le flux reste à **32 octets par particule** : 16 Mio pour 524 288 particules,
  hors métadonnées et capacité/réserves de l'allocateur. Ce plafond n'est pas une
  allocation systématique pour chaque biome.

Ces nombres sont des capacités, pas une garantie de temps GPU. Le recouvrement
transparent et la résolution restent importants ; les mesures sur GTX 1660
restent nécessaires. La présence d'une recette dans le catalogue ou la matrice
ne crée aucune particule tant qu'elle n'est pas activée.

## Vent et comportements propres

Le vent des particules est un `vec2` XZ indépendant de celui des nuages. Sa
vitesse et sa direction évoluent continûment à partir d'un vent de base, d'une
variation et d'une période réglables. Les nuages ne sont pas modifiés par ces
paramètres. Par défaut, `windVariation = 0.65` et `windPeriod = 24` secondes.

Ce vent représente une vitesse d'air à laquelle le matériau répond avec un
amortissement propre. Il n'est plus ajouté comme une accélération permanente
qui finit par emporter tous les effets à la même vitesse. Les effets disposent
d'abord de leur cinématique : chute, montée thermique, trajectoire balistique,
autorotation, vol autonome ou suspension. Leurs différences restent visibles
avec un vent nul.

La neige légère descend à **0,25–0,65 m/s**, tourne lentement, oscille et ne reçoit
que 18 % de l'influence du vent global. Les cendres utilisent une silhouette
irrégulière distincte. Pollen, samares, pétales, spores et lucioles disposent
également de formes et de réponses différentes. Les météores ignorent le vent.
L'effet de respiration a été supprimé ; aucune particule n'est émise à une
position de bouche supposée dans cette caméra à la troisième personne.

## Intégration et activation

La scène `ENDURANCE/Scene/Terrain.scene` contient
`EnduranceBiomeEffectsComponent`, juste après `EnduranceAtmosphereComponent`.
Les paramètres et les associations restent dans le jeu. Le moteur fournit
les particules, le mélange avec l'atmosphère et la passe d'écran, sans connaître
les biomes.

Le contrôleur réutilise le résultat climatique asynchrone de la skybox : le
résultat passe de 64 à 80 octets et contient l'identifiant du biome dominant
ainsi que son poids. Il n'y a ni nouveau calcul climatique ni attente GPU
supplémentaire. Le biome marin fait partie de cette sélection. Le prélèvement
climatique reste disponible quand le rendu atmosphérique est désactivé.

Seul le biome dominant fixe les cibles d'intensité. Les effets sortants ont un
fondu fini avant leur suppression ; cette courte coexistence évite une coupure
visuelle et ne dépend pas du poids des biomes minoritaires. Aucun événement
actif n'est tiré au sort ou démarré à l'entrée du biome. Un événement devenu
inéligible s'arrête progressivement. Une téléportation supprime les anciennes
particules et attend un prélèvement proche de la nouvelle position.

Dans l'inspecteur du composant, après démarrage de la scène :

- `Enabled`, `Particles` et `Screen effects` permettent de couper les effets.
- `Particle density` ajuste le nombre de particules : 0, 0.5x, 1x, 2x ou 4x.
  La valeur par défaut 1x utilise les recettes renforcées ci-dessus.
- `Passive intensity` et `Screen intensity` règlent leur opacité.
- `Wind XZ`, `Wind variation` et `Wind period` règlent uniquement le vent des particules.
- `Current passive effects` indique les effets du biome dominant.
- `Active events (manual trigger)` permet d'essayer les événements autorisés,
  avec durée et intensité. Deux événements peuvent être actifs simultanément.
- `Preview all 40 effects` isole une recette pendant 24 secondes, même hors de
  son biome. C'est un outil manuel d'édition, pas une activation automatique.

Le futur système de météo peut utiliser l'API suivante :

```cpp
#include "EnduranceBiomeEffects.hpp"

if (auto* effects = EnduranceBiomeEffectsComponent::active())
{
    // Retourne false si le biome courant n'autorise pas cet événement,
    // si le composant est désactivé ou si deux autres événements sont actifs.
    const bool started = effects->triggerEvent("sandstorm", 30.0f, 0.8f);
}
```

`stopEvent(EnduranceEnvironment::EffectId::Sandstorm)` ou `stopEvents()`
interrompent les événements avec leur fondu de sortie. Les durées sont bornées
à 600 secondes ; un nouvel appel pour un événement déjà actif renouvelle sa
durée. Ces événements ne modifient pas encore la physique du personnage,
le son, la météo générale ou les lumières de la scène.

## Coût et rendu

- Les particules utilisent le système GPU existant, avec **16 formes de
  shaders précompilées**. Les formes sont analytiques, sans atlas externe.
  Les petites variations de couleur, de mouvement et de taille viennent des
  recettes. Le rendu prend en compte la profondeur opaque et l'atmosphère. La simulation
  environnementale spécialisée conserve le flux compact et les groupes existants.
- Les particules déjà émises restent dans le monde ; l'émetteur suit la caméra.
  Il n'y a pas de requête de collision ou d'échantillonnage du terrain pour
  chaque particule. Brumes basses, fumerolles et embruns sont donc des ambiances
  autour du joueur, sans ancrage précis au sol, à la côte ou à un volcan. Un
  système de volumes abrités pourra les masquer plus tard en intérieur.
- Au plus **8 groupes et 524 288 particules** sont admis, transitions comprises.
  Au-delà du budget, l'admission attend qu'un groupe se libère ; un groupe peut
  être admis partiellement lorsque seule une fraction de sa capacité rentre.
  Le catalogue seul ne crée aucun groupe. Quand tous les effets de particules sont à zéro,
  leur callback de rendu est désinscrit : aucune simulation ni aucun dessin.
- Chaleur, givre, eau et buée partagent **16 variantes compilées**. Les effets
  d'écran actifs sont réunis en **une passe**, avec les cibles intermédiaires
  existantes. Une seule cible suffit si cette passe est la seule à en avoir
  besoin. Un état nul n'ajoute aucune passe. Les flashes sont calculés une fois
  côté CPU, puis composés à l'écran.
- L'émission des particules entre dans le rendu HDR et le bloom existant. Les
  effets sur l'écran sont appliqués après le traitement de couleur. La chaleur
  utilise une déformation UV animée à plusieurs échelles, protégée par la profondeur et les contours,
  sans lancer de rayons ni calculer un champ volumétrique.
- Ni nouveau shadow caster, ni carte d'ombres, ni compilation de shader lors
  d'un simple changement de biome. Les ressources de groupes sont créées et
  libérées à leur activation/extinction, pas à chaque déplacement de caméra.

Les références de principe sont la
[distorsion de chaleur de GPU Gems](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-6-fire-vulcan-demo)
et les [particules adoucies par profondeur de GPU Gems 3](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles).
Le présent rendu utilise les buffers du moteur et n'ajoute pas leur pipeline
de rendu à résolution réduite.


La direction artistique s'appuie également sur le
[guide des cristaux de neige de Kenneth Libbrecht (Caltech)](https://www.its.caltech.edu/~atomic/snowcrystals/class/class-old.htm)
pour séparer plaques, branches et agrégats, et sur
[Curl-Noise for Procedural Fluid Flow (Bridson et al.)](https://www.cs.ubc.ca/~rbridson/docs/bridson-siggraph2007-curlnoise.pdf)
pour privilégier des mouvements tourbillonnants contrôlables plutôt que le même
vecteur pour tous. Les trajectoires sont des approximations artistiques peu
coûteuses ; elles ne constituent pas une simulation complète de fluide ou de
microphysique. Les collisions avec le relief et les abris restent hors périmètre.

## Vérification

Les tests de `Tests/BiomeEffects/` couvrent le catalogue, l'éligibilité, les
transitions, les événements manuels, les facteurs 10x, la neige lente,
l'absence de respiration et le rendu des variantes dans un contexte
OpenGL EGL. `Tests/Atmosphere/check_game_climate.py` vérifie également la
sélection dominante contre le classificateur du terrain. Le contrôle C++ de
`Tests/Atmosphere/check_render_integration.py` compile les nouvelles unités avec
les en-têtes de production.

Ces vérifications ne remplacent pas une session visuelle dans l'éditeur Windows
ni une mesure sur GTX 1660. Les tailles, opacités et budgets pourront être
ajustés dans le catalogue après observation en jeu, sans réécrire les shaders.
