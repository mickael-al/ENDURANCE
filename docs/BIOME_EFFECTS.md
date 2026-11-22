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
les masques d'éligibilité. Les identifiants textuels ci-dessous restent stables
pour les commandes du jeu et de l'éditeur.

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
| 1 | `heat_haze` | Passif | Mirage de chaleur : ondulation lente et large de l'image. | — |
| 2 | `sand_gust` | Passif | Grains sableux brun doré emportés horizontalement en petites bourrasques. | 1 536 |
| 3 | `volcanic_ash` | Passif | Flocons de cendre sombres, chute lente, rotation et dérive turbulente. | 1 152 |
| 4 | `wind_pollen` | Passif | Petites aigrettes jaune pâle portées doucement par le vent. | 384 |
| 5 | `fireflies` | Passif | Points lumineux vert, turquoise, ambre et bleu, trajectoires ondulées et pulsations. | 72 |
| 6 | `light_snow` | Passif | Flocons blancs bleutés de tailles variées, chute lente et balancement. | 1 536 |
| 7 | `frost_rim` | Passif | Cristallisation de givre sur les bords de l'écran, centre dégagé. | — |
| 8 | `ice_crystals` | Passif | Petits cristaux anguleux bleutés avec scintillement discret. | 256 |
| 9 | `ground_snow_drift` | Passif | Voiles de neige allongés et peu opaques, balayés horizontalement. | 80 |
| 10 | `wind_leaves` | Passif | Feuilles olive tournoyantes, légères descentes et variations latérales. | 192 |
| 11 | `petals` | Passif | Pétales rosés, formes arrondies, rotation et lente dérive. | 208 |
| 12 | `dry_seeds` | Passif | Graines allongées couleur paille, plus rapides que le pollen. | 384 |
| 13 | `dust_motes` | Passif | Minuscules poussières peu opaques, mouvement presque immobile. | 384 |
| 14 | `ground_mist` | Passif | Grandes nappes de brume grise bleutée, basses et très transparentes. | 40 |
| 15 | `swamp_vapor` | Passif | Petites volutes verdâtres qui remontent lentement. | 48 |
| 16 | `sea_spray` | Passif | Embruns cyan pâle, projetés de biais puis ramenés vers le bas. | 896 |
| 17 | `salt_mist` | Passif | Voiles marins étirés, moins denses et plus lents que les embruns. | 40 |
| 18 | `volcanic_embers` | Passif | Braises orange rouge montantes, émission HDR et vacillement. | 192 |
| 19 | `fumaroles` | Passif | Volutes gris brun ascendants, tailles et turbulences plus grandes que les braises. | 40 |
| 20 | `spores` | Passif | Spores mates vert olive, remontée lente et diffusion douce. | 384 |
| 21 | `glowing_spores` | Passif | Spores cyan lumineuses, lentes pulsations et ascension régulière. | 128 |
| 22 | `canopy_drips` | Passif | Gouttes espacées et allongées, accélération verticale depuis le haut. | 384 |
| 23 | `insects` | Passif | Petites silhouettes sombres, mouvements nerveux et battements rapides. | 64 |
| 24 | `wind_streaks` | Passif | Rares filets de vent fins et transparents, orientés selon leur déplacement. | 96 |
| 25 | `cold_breath` | Passif | Petites volutes de condensation au voisinage de la caméra. | 12 |
| 26 | `wet_lens` | Passif | Quelques gouttelettes réfractantes sur l'image dans les biomes humides. | — |
| 27 | `condensation` | Passif | Buée douce en périphérie, évolution très lente. | — |
| 28 | `sand_veil` | Passif | Léger voile de poussière chaude, nuancé de brun rouge. | — |
| 29 | `rain` | Actif | Pluie oblique à fines traînées, accompagnée de gouttes sur l'écran. | 3 072 |
| 30 | `heavy_rain` | Actif | Pluie plus dense, plus longue et plus rapide ; eau et légère buée sur l'écran. | 6 144 |
| 31 | `sandstorm` | Actif | Sable dense et rapide, fortes turbulences, voile brun et distorsion légère. | 6 144 |
| 32 | `blizzard` | Actif | Neige presque horizontale, densité accrue, givre et voile blanc bleuté. | 6 144 |
| 33 | `ash_storm` | Actif | Cendres épaisses en diagonale, fortes turbulences et voile gris brun. | 4 096 |
| 34 | `thunderstorm` | Actif | Pluie soutenue avec flashes d'orage et écran mouillé. | 4 608 |
| 35 | `hail` | Actif | Grêlons anguleux relativement gros, chute rapide et accélérée. | 2 048 |
| 36 | `sleet` | Actif | Grésil à traînées courtes, vitesse intermédiaire, mélange d'eau et de givre. | 3 584 |
| 37 | `meteor_shower` | Actif | Rares fragments incandescents à longues traînées, projetés haut au-dessus du joueur. | 32 |
| 38 | `lunar_dust_fall` | Actif | Cristaux et poussières gris violet tombant lentement, léger voile froid. | 1 280 |
| 39 | `pollen_burst` | Actif | Nuage de graines et pollen plus dense, plus rapide, légèrement doré. | 2 048 |
| 40 | `spore_bloom` | Actif | Envol dense de spores lumineuses turquoise et violettes, pulsations lentes. | 960 |

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
| `PolarIce` | `light_snow`, `frost_rim`, `ice_crystals`, `cold_breath` | `blizzard`, `hail`, `sleet` |
| `FrozenTundra` | `ground_snow_drift`, `ice_crystals`, `wind_streaks` | `blizzard`, `hail`, `sleet` |
| `Tundra` | `dry_seeds`, `light_snow`, `cold_breath` | `blizzard`, `sleet`, `hail` |
| `Taiga` | `light_snow`, `wind_leaves`, `dust_motes` | `rain`, `blizzard`, `sleet`, `pollen_burst` |
| `BorealBog` | `ground_mist`, `spores`, `condensation` | `rain`, `sleet`, `spore_bloom` |
| `RockyTundra` | `wind_streaks`, `ice_crystals`, `cold_breath` | `blizzard`, `hail`, `sleet` |
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

## Budgets des recettes

- Trois effets passifs sont affectés à la plupart des biomes, quatre au maximum.
- Le maximum passif est de **2 112 particules** (`Taiga` et `BorealForest`) aux réglages de base.
- Les particules qui doivent remplir le volume sont renforcées : neige 1 536,
  cendres 1 152, bourrasques de sable 1 536 et embruns 896. Les grandes nappes
  transparentes, les lucioles et les insectes conservent des densités plus sobres.
- Les plus gros événements sont `heavy_rain`, `sandstorm` et `blizzard`, avec
  **6 144 particules** chacun aux réglages de base.
- Les nappes transparentes sont limitées à 40–80 particules pour réduire le
  recouvrement de grands quads. Les effets ponctuels ont davantage de particules
  mais de petites surfaces projetées.
- Les recettes n'allouent aucune ressource GPU. Leur présence dans le catalogue
  ou dans un masque d'événements ne constitue pas une activation.

Ces nombres sont des budgets d'instances, pas une garantie de temps GPU : la
résolution, le recouvrement transparent et l'intensité des événements restent
déterminants. Les réglages devront être appréciés sur la GTX 1660 cible.

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
- `Passive intensity`, `Screen intensity` et `Wind XZ` règlent leur présence.
- `Current passive effects` indique les effets du biome dominant.
- `Active events (manual trigger)` permet d'essayer les événements autorisés,
  avec durée et intensité. Deux événements peuvent être actifs simultanément.
- `Preview all 40 effects` isole une recette pendant 12 secondes, même hors de
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

- Les particules utilisent le système GPU existant, avec **8 familles de
  shaders précompilées**. Les formes sont analytiques, sans atlas externe.
  Les petites variations de couleur, de mouvement et de taille viennent des
  recettes. Le rendu prend en compte la profondeur opaque et l'atmosphère.
- Les particules déjà émises restent dans le monde ; l'émetteur suit la caméra.
  Il n'y a pas de requête de collision ou d'échantillonnage du terrain pour
  chaque particule. Brumes basses, fumerolles et embruns sont donc des ambiances
  autour du joueur, sans ancrage précis au sol, à la côte ou à un volcan. Un
  système de volumes abrités pourra les masquer plus tard en intérieur.
- Au plus **8 groupes et 32 768 particules** sont admis, transitions comprises.
  Au-delà du budget, l'admission attend qu'un groupe se libère ; un groupe peut
  être admis partiellement lorsque seule une fraction de sa capacité rentre.
  Le catalogue
  seul ne crée aucun groupe. Quand tous les effets de particules sont à zéro,
  leur callback de rendu est désinscrit : aucune simulation ni aucun dessin.
- Chaleur, givre, eau et buée partagent **16 variantes compilées**. Les effets
  d'écran actifs sont réunis en **une passe**, avec les cibles intermédiaires
  existantes. Une seule cible suffit si cette passe est la seule à en avoir
  besoin. Un état nul n'ajoute aucune passe. Les flashes sont calculés une fois
  côté CPU, puis composés à l'écran.
- L'émission des particules entre dans le rendu HDR et le bloom existant. Les
  effets sur l'écran sont appliqués après le traitement de couleur. La chaleur
  utilise une déformation UV faible protégée par la profondeur et les contours,
  sans lancer de rayons ni calculer un champ volumétrique.
- Ni nouveau shadow caster, ni carte d'ombres, ni compilation de shader lors
  d'un simple changement de biome. Les ressources de groupes sont créées et
  libérées à leur activation/extinction, pas à chaque déplacement de caméra.

Les références de principe sont la
[distorsion de chaleur de GPU Gems](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-6-fire-vulcan-demo)
et les [particules adoucies par profondeur de GPU Gems 3](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles).
Le présent rendu utilise les buffers du moteur et n'ajoute pas leur pipeline
de rendu à résolution réduite.

## Vérification

Les tests de `Tests/BiomeEffects/` couvrent le catalogue, l'éligibilité, les
transitions, les événements manuels et le rendu des variantes dans un contexte
OpenGL EGL. `Tests/Atmosphere/check_game_climate.py` vérifie également la
sélection dominante contre le classificateur du terrain. Le contrôle C++ de
`Tests/Atmosphere/check_render_integration.py` compile les nouvelles unités avec
les en-têtes de production.

Ces vérifications ne remplacent pas une session visuelle dans l'éditeur Windows
ni une mesure sur GTX 1660. Les tailles, opacités et budgets pourront être
ajustés dans le catalogue après observation en jeu, sans réécrire les shaders.
