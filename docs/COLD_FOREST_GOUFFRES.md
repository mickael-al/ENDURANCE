# Forêt froide : placement rapproché et gouffres profonds

ColdForest conserve les bruits du sol et les vallées creusées par un masque
régional. Les gouffres gardent 20 km de diamètre et un cœur entièrement vide
jusqu'à 70 % de leur rayon lorsque le masque local est plein. Leur placement revient aux cellules de 10 km, avec
les régions de 24 km et les axes de vallées espacés de 9 km. La profondeur utilise
maintenant la capacité maximale de la bande 3D, au lieu de rester limitée à 800 m.

## Réglages

Les paramètres sont dans les fichiers du dossier
`Asset/Shader/ProceduralTerrain/biomes/ColdForest`.

| Fichier / constante | Valeur | Effet |
|---|---:|---|
| `ground_settings.glsl` — `COLD_FOREST_REGION_SCALE` | 24 000 m | Taille du masque régional restaurée |
| `COLD_FOREST_REGION_LOW` / `HIGH` | 0,042469 / 0,442469 | Environ 30 % de couverture dominante |
| `COLD_FOREST_RIVER_SPACING` | 9 000 m | Espacement nominal des vallées restauré |
| `COLD_FOREST_RIVER_FLOOR_HALF_WIDTH` | 100 m | Demi-largeur du fond plat |
| `COLD_FOREST_RIVER_BANK_HALF_WIDTH` | 650 m | Demi-largeur totale des berges |
| `COLD_FOREST_RIVER_MEANDER` | 600 m | Amplitude maximale des méandres |
| `COLD_FOREST_RIVER_MEANDER_SCALE` | 12 000 m | Échelle des méandres |
| `pit_settings.glsl` — `COLD_FOREST_PIT_SPACING` | 10 000 m | Taille des cellules de placement |
| `COLD_FOREST_PIT_RADIUS` | 10 000 m | Rayon du cône avant découpe locale |
| `COLD_FOREST_PIT_DEPTH_SCALE` | 1 | Fraction de la profondeur maximale disponible |
| `COLD_FOREST_PIT_RIM_WIDTH` | 2 000 m | Raccord progressif du cône au sol |
| `COLD_FOREST_PIT_SPIRAL_DEPTH` | 110 m | Amplitude du relief en spirale |
| `COLD_FOREST_PIT_SPIRAL_TURNS` | 2 | Nombre de tours vers le centre |
| `COLD_FOREST_PORE_SCALE` | 240 m | Échelle du Perlin 3D |
| `COLD_FOREST_PIT_EMPTY_RADIUS` | 0,70 | Partie entièrement vide du rayon |
| `COLD_FOREST_PIT_ANCHOR_RADIUS` | 1 100 m | Rayon de protection des ancrages vis-à-vis des rivières |
| `COLD_FOREST_PIT_RIVER_FADE` | 500 m | Raccord entre berges protégées et gouffres |
| `COLD_FOREST_PIT_BAND_MARGIN` | 96 m | Marge conservée autour du fond |

La couverture de 30 % désigne la surface où le masque régional est au moins
égal à 0,5, sur un grand domaine. Elle ne représente pas directement la surface
des rivières ou des gouffres. Le fondu quintique et les bruits du sol restent
inchangés. Les rivières abaissent le substrat ; elles ne le surélèvent pas.
Aucune eau ni simulation d'écoulement n'est ajoutée.

## Ancrages restaurés et chevauchements

Le hash de la cellule et du seed place un centre dans la moitié centrale de
chaque cellule de 10 × 10 km. La distance minimale entre centres candidats est
de 5 km. L'admission utilise à nouveau le voisinage initial d'environ 1 km autour
de cet ancrage pour exclure les rivières, au lieu d'exiger qu'un disque de
20 km soit entièrement contenu dans une région sans rivière. La région n'est
plus un motif de rejet global : son masque est appliqué localement.

Un cône de 20 km traverse forcément plusieurs cellules de placement. La passe
2D examine donc les neuf cellules du voisinage 3 × 3 et combine les profondeurs
et les fractions de vide de leurs candidats admissibles. Les enveloppes
s'annulent avant que leurs cellules sortent de ce voisinage ; le changement de
cellule d'évaluation ne coupe pas un gouffre. Les formations peuvent se
chevaucher et se rejoindre.

Un masque local diminue ensuite la profondeur et la fraction de vide aux
limites des régions et au voisinage des rivières. Les vallées restent protégées,
mais elles peuvent désormais découper le contour d'un grand gouffre. Les 20 km
décrivent l'enveloppe nominale avant cette découpe, et non un cercle complet
obligatoirement visible dans chaque site. Cette découpe évite de supprimer
presque tous les candidats lorsque le rayon dépasse la taille des zones libres.

## Profondeur maximale et densité 3D

`coldForestPitMaximumDepth` utilise la bande maximale existante de 16 384 unités
brutes. Avec `COLD_FOREST_PIT_DEPTH_SCALE = 1`, la profondeur disponible vaut :

```text
max(0, 8192 × uDetailScale − 96 − abs(décalage global local))
```

| `uDetailScale` | Profondeur maximale, décalage global nul |
|---:|---:|
| 0,20 | 1 542,4 m |
| 0,35 | 2 771,2 m |
| 1,00 | 8 096 m |

Ces valeurs concernent le centre d'un ancrage pleinement actif en biome pur.
Le profil conique, le masque local et les mélanges de biomes peuvent réduire la
profondeur ailleurs. La marge de 96 m conserve le raccord et le travail de densité
à l'intérieur de la bande autorisée. Le plafond global du moteur n'est pas relevé.

La spirale reste une formule directe, sans recherche itérative sur un tore
hélicoïdal. `COLD_FOREST_PIT_SPIRAL_DEPTH = 0.0` garde le cône sans modulation.
Le raccord extérieur a une pente nulle et rejoint progressivement le terrain.

La fraction de vide vaut 100 % jusqu'à 7 km de rayon autour d'un candidat ; elle
redescend ensuite de 100 % à 0 % sur les 3 km périphériques. Le Perlin 3D ne laisse
aucun bloc dans un cœur pleinement actif. Le fondu local des régions et des
rivières peut remettre de la matière près de leurs limites. Les quantiles du
bruit réel règlent la porosité périphérique ; ses proportions restent
statistiques sur plusieurs formations. La connectivité de tous les fragments
périphériques n'est pas imposée.

En biome pur, la passe 2D transmet la fraction de vide dans `relief.w` et encode
la profondeur dans la largeur de bande. La passe 3D les récupère, utilise un
bruit ancré dans le monde et excave seulement entre le sol et le fond. Elle ne
refait pas la recherche des neuf candidats à chaque voxel. Aux frontières de
biomes, les champs sont recalculés car les contrôles partagés sont mélangés ;
la profondeur est pondérée puis bornée par l'enveloppe réellement disponible.
Un fondu près des limites de bande évite les coupures.

## Vérification

```bash
python Tests/Terrain/check_cold_forest.py
python Tests/Terrain/check_cold_forest_regions.py
python Tests/Terrain/calibrate_cold_forest_porosity.py
```

Les sondes exécutent le GLSL de production sur OpenGL/llvmpipe. Les rapports
sont dans `docs/biome-validation`. `cold_forest.png` reconstruit la véritable
densité sur 26 km, avec un pas horizontal d'environ 135 m ; ce n'est pas une capture du
jeu et les plus petits détails ne sont pas résolus.

Le contrôle dédié vérifie les ancrages, le cône isolé, la protection locale des
rivières, les chevauchements aux frontières de cellules, la profondeur maximale
à trois échelles, les mélanges et les densités du pipeline réel. Il vérifie aussi
explicitement la présence de cavités dans une zone de 40 km autour de l'origine
avec le seed 1337, afin d'éviter une nouvelle régression de visibilité.

Résultats de cette validation :

- Dans les 40 × 40 km autour de l'origine, 3 743 des 25 921 sondes sont réellement
  dans l'air à 100 m sous le sol initial. Le point visible sondé le plus proche
  est à X = −5 250 m, Z = −1 500 m ; sa densité à Y ≈ −183,5 m est négative.
  La profondeur maximale mesurée dans cette fenêtre est de 2 161,5 m.
- Les centres pleinement actifs atteignent la profondeur maximale calculée
  aux trois échelles 0,2 / 0,35 / 1. Les 24 576 sondes de volume ne montrent
  aucune excavation ignorée par la bande du pipeline.
- Les 18 paires de sondes placées directement sur les fonds coniques aux
  frontières de cellules ont un écart maximal de densité de 4,4 cm, compatible
  avec la quantification de la bande. Les champs non quantifiés restent continus.
- Les mélanges de biomes, les 1 331 densités du pipeline réel et les 125 points
  LOD partagés passent également.
