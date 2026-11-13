# Première révision des sept biomes froids — 13 novembre 2026

> Bilan historique de la première passe (`e1c6f52`). Les réglages et mesures des IDs 0–3 ci-dessous sont remplacés par la [seconde révision : largeur et placement](COLD_BIOME_SPACING_REVISION.md). Les IDs 4–6 restent inchangés.

Cette passe répond au retour de jeu sur les IDs **0 à 6**. Les recettes des IDs 7 à 25 ne sont pas retouchées. Les nouveaux reliefs s’ajoutent aux formes précédentes, sauf la hauteur de la toundra rocheuse qui est multipliée par deux. Les montagnes utilisent un profil, des contreforts ridged, du fBm et de la déformation de coordonnées ; les grandes incisions sont sculptées en 2D, puis les grottes et rochers en densité 3D.

## RKM et décisions

L’utilisateur a confirmé que **0,25 RKM signifie une formation tous les 4 km**, et non tous les 250 m. RKM est donc une fréquence nominale :

| RKM | Intervalle nominal |
| --- | --- |
| 0,25 | 4 km |
| 0,5 | 2 km |
| 1 | 1 km |
| 2 | 500 m |

`landRkmSite` place un candidat par cellule carrée de côté `1000/rkm`, avec décalage, orientation et taille déterministes par seed. Chaque type de formation a son propre sel. Ce réglage désigne l’espacement nominal d’une population en deux dimensions, **pas une garantie de rencontrer une entrée de grotte sur chaque kilomètre de trajet**. Une transition climatique peut atténuer ou supprimer une formation. Le site d’une montagne et celui d’une grotte sont indépendants.

Les 200 m de la faille polaire sont interprétés comme sa largeur d’ouverture ; « 200 % » pour la toundra rocheuse est interprété comme **×2**. Les fréquences non données ont été fixées pour cette première passe : montagnes gelées 0,5 RKM ; cuvettes boréales 1 RKM ; montagnes de steppe 0,5 RKM et failles de steppe 0,25 RKM.

## Formes implémentées

Les hauteurs ci-dessous sont relatives au terrain local, pour `detailScale=0,35`, dans un biome isolé. Les réglages de détail et les mélanges climatiques modifient leur amplitude.

| Biome | Ajouts | RKM |
| --- | --- | --- |
| Glace polaire | Déchirures sinueuses abruptes de 160–210 m de large, 230–330 m de profondeur, grottes sous leur fond avec accès en rampe. Massifs glacés asymétriques de 450–580 m. | Failles 0,5 ; massifs 0,25 |
| Toundra gelée | Petites chambres souterraines à environ 15 m sous le sol, plafond environ 6 m sous le sol et entrée ouverte ; montagnes ridged/fBm de 420–560 m. | Grottes 2 ; montagnes 0,5 |
| Toundra | Pics de 800 m avec contreforts ; rochers 3D d’environ 20 m. | Pics 1 ; rochers 0,5 |
| Taïga | Montagnes de 700 m ; anciens lits de rivière de 100–170 m de large, incisions, méandres, berges et terrasses. L’aspect érodé est géométrique, sans simulation hydraulique. | Montagnes 0,5 ; rivières 1 |
| Forêt boréale marécageuse | Surplombs plus hauts et larges, cuvettes de lac de 7–14 m, chambres déformées par Perlin 3D avec accès à la surface. Les cuvettes n’ajoutent pas de système d’eau. | Cuvettes 1 ; cavités 0,25 |
| Toundra rocheuse | Relief 2D antérieur doublé ; nouvelles cavités ouvertes. | Cavités 0,25 |
| Steppe froide | Montagnes de 620 m en strates de 70 m ; failles abruptes de 800 m de profondeur et 150–250 m de large. | Montagnes 0,5 ; failles 0,25 |

Les volumes gardent l’enveloppe précédente : épaisseur maximale brute 576, soit 201,6 m au réglage courant. Les failles de 800 m sont donc réalisées dans le relief 2D : elles ne sont pas tronquées par une bande 3D de 200 m. Les petites grottes suivent le sol local. Les anciens volumes sont conservés et leurs contributions combinées avec les nouveaux sans appliquer deux fois le masque.

## Limite de hauteur corrigée

Le plafond antérieur de ±640 unités brutes limitait le relief propre au biome à ±224 m au réglage courant. Il passe à **±4096**, soit ±1433,6 m, dans les clamps GLSL et les bornes conservatrices CPU. Le stockage de hauteur FP32, les bandes et les offsets compactés ne changent pas.

Cette enveloppe plus haute demandait 1097 feuilles au test de sélection par défaut, au-delà de l’ancienne limite de 1024. Les trois valeurs de sélection par défaut passent à **2048** ; les budgets de chunks résidents et de sommets ne changent pas. La scène existante avait déjà une limite de sélection de 2048 : quatre positions, dont des altitudes de ±1000 m, restent sous cette limite (maximum 1932). La scène elle-même n’a pas été modifiée.

## Mesures exécutées

Mesures sous OpenGL 4.5 / Mesa llvmpipe, seed 1337, contre les anciennes recettes de `f8960ea`. Les sites sont choisis parmi des ancres calculées sur GPU pour isoler la formation ciblée : ce n’est pas une statistique de fréquence sur un parcours.

| Mesure ciblée | Résultat |
| --- | --- |
| Massif polaire | +497,71 m |
| Faille polaire | −238,66 m |
| Montagne gelée | +456,27 m |
| Pic de toundra | +800,00 m |
| Rocher de toundra | 19,69 m au-dessus du relief |
| Montagne de taïga | +700,00 m |
| Montagne de steppe | +620,00 m |
| Faille de steppe | −800,00 m |
| Toundra rocheuse | Hauteur de biome exactement doublée sur 4225 points |

Les cinq sondes volumiques vérifient les cavités/rochers, les valeurs finies et les bandes. Les cavités testées contiennent de l’air relié à l’extérieur sous un plafond. Les treize sites sont comparés à l’accumulation de production : **17303 densités et 1625 échantillons LOD partagés**. Les contrôles généraux couvrent les 52 programmes, les 26 recettes, les transitions et poids locaux ; les rejets conservateurs restent identiques à l’évaluation exhaustive sur 41154 densités aux grandes coordonnées. Le test de bande couvre 24 cas et 18 nouveaux cas de hauteur. Les sept sites historiques passent également en deux lots (0–3 puis 4–6). Une comparaison indépendante des IDs 7–25 avec les anciennes recettes, sur deux seeds et trois origines, donne des colonnes et 496584 densités identiques bit à bit (124146 positions).

- [Mesures détaillées](biome-validation/cold_revision.json)
- [Profils avant/après et coupes 3D](biome-validation/cold_profiles.png)

Les figures représentent des échantillons GLSL à échelle physique identique sur les deux axes, sans textures ni végétation. L’entrée d’une grotte peut se trouver hors du plan de coupe affiché. Ces sondes ne certifient pas la traversabilité de la capsule, les collisions, le grappin ni les performances du GPU Windows. Le ressenti et les fréquences utiles se régleront lors du prochain essai en jeu.

## Reproduire

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_cold_biome_revision.py --reference-shaders /chemin/vers/ancienne-version/Asset/Shader/ProceduralTerrain --output /tmp/endurance-cold
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_biome_landforms.py --biomes 0 1 2 3 --resume --no-render --output /tmp/endurance-cold-sites
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_biome_landforms.py --biomes 4 5 6 --resume --no-render --output /tmp/endurance-cold-sites
```

Les anciens atlas des 25 biomes restent archivés avec leur bilan historique ; leurs empreintes de source ne sont plus celles de cette passe. Les tests de reprise refusent de les réutiliser silencieusement comme mesures courantes.

Référence technique : [GPU Gems 3, Generating Complex Procedural Terrains Using the GPU](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu), pour la composition à plusieurs échelles et les densités volumiques. Les proportions et fréquences de cette passe suivent les objectifs de jeu de l’utilisateur.
