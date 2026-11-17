# Forêt froide : rivières régionales et gouffres poreux

ColdForest conserve les bruits de détail du sol, leurs seeds et leurs amplitudes.
L'ancien monticule en croissant, son balcon et sa petite alcôve 3D sont retirés.
Les vallées sont désormais excavées dans un substrat continu, sous le contrôle
d'un grand masque 2D. Des gouffres coniques d'environ 20 km de diamètre apparaissent
dans les régions actives, en dehors des rivières.

## Sol et rivières

Les réglages sont dans
`Asset/Shader/ProceduralTerrain/biomes/ColdForest/ground_settings.glsl`.

| Réglage | Valeur initiale | Effet |
|---|---:|---|
| `COLD_FOREST_REGION_SCALE` | 120 000 m | Échelle du masque régional |
| `COLD_FOREST_REGION_LOW` / `HIGH` | 0,042469 / 0,442469 | Début et fin du fondu régional |
| `COLD_FOREST_RIVER_SPACING` | 50 000 m | Espacement nominal des axes de vallées |
| `COLD_FOREST_RIVER_FLOOR_HALF_WIDTH` | 100 m | Demi-largeur du fond plat |
| `COLD_FOREST_RIVER_BANK_HALF_WIDTH` | 650 m | Demi-largeur totale des berges |
| `COLD_FOREST_RIVER_MEANDER` | 600 m | Amplitude maximale des méandres |
| `COLD_FOREST_RIVER_MEANDER_SCALE` | 12 000 m | Échelle des méandres |

`ground.glsl` construit un masque régional déformé, avec un fondu quintique,
puis le multiplie par le masque des vallées. Le résultat ne peut que diminuer
la hauteur du substrat. Lorsque le masque vaut zéro, le substrat est intact.
La profondeur conserve les anciens bruits et amplitudes des parois ; elle dépend
donc encore de `uDetailScale`. Les échantillons mesurés atteignent environ
160 m de profondeur à l'échelle 0,35.

Les seuils sont calibrés pour qu'environ 30 % de la surface ait un masque
supérieur ou égal à 0,5, sur un grand domaine et plusieurs seeds. Ce n'est ni
30 % de surface de rivière, ni une réduction de 30 % de l'ancienne couverture.
L'échelle régionale de 120 km et l'espacement de 50 km entre vallées permettent
d'accueillir les gouffres élargis en conservant leur exclusion des rivières.
Avec l'ancienne échelle régionale de 24 km, aucun des 264 196 sites sondés ne
pouvait accueillir un disque de 20 km avec les marges de sécurité.

Il s'agit de reliefs en forme de vallées : cette modification ne crée ni eau,
ni simulation d'écoulement ou d'érosion. Les axes restent globalement orientés,
avec des méandres, mais le grand masque interrompt leur présence sur plusieurs
kilomètres.

## Gouffres et spirale

Les réglages sont dans
`Asset/Shader/ProceduralTerrain/biomes/ColdForest/pit_settings.glsl`.

| Réglage | Valeur initiale | Effet |
|---|---:|---|
| `COLD_FOREST_PIT_SPACING` | 50 000 m | Taille des cellules de placement |
| `COLD_FOREST_PIT_RADIUS` | 10 000 m | Rayon du gouffre, soit 20 km de diamètre |
| `COLD_FOREST_PIT_DEPTH` | 800 m | Profondeur centrale en biome pur |
| `COLD_FOREST_PIT_RIM_WIDTH` | 2 000 m | Largeur de raccord progressif au sol |
| `COLD_FOREST_PIT_SPIRAL_DEPTH` | 110 m | Amplitude du relief en spirale |
| `COLD_FOREST_PIT_SPIRAL_TURNS` | 2 | Nombre de tours vers le centre |
| `COLD_FOREST_PORE_SCALE` | 240 m | Échelle du Perlin 3D des parois |
| `COLD_FOREST_PIT_EMPTY_RADIUS` | 0,70 | Rayon entièrement vide, en fraction du rayon total |
| `COLD_FOREST_PIT_REGION_MIN` | 0,80 | Présence régionale minimale pour accepter un site |

Le fond suit un cône inversé. Une modulation angulaire forme une spirale
descendante dont la phase et le sens varient selon le site. Son amplitude
s'annule au centre et à la périphérie. Le raccord de 2 000 m a une pente nulle
au bord : il rejoint progressivement le relief local, sans imposer un plateau
parfaitement horizontal à tout le terrain. La profondeur est relative au sol
de chaque colonne. `COLD_FOREST_PIT_SPIRAL_DEPTH = 0.0` conserve uniquement le cône.

La spirale utilise une formule directe, sans recherche itérative de distance
à un tore hélicoïdal. Dans les colonnes de biome pur, la profondeur est calculée
en 2D, puis transmise à la 3D dans `relief.w`, normalisée par la profondeur
maximale. La 3D n'a alors pas à recalculer le masque ni la spirale.

Le placement utilise le hash du seed et de la cellule de 50 × 50 km. Le centre
est décalé dans la moitié centrale de cette cellule. Un gouffre tient donc
entièrement dans sa cellule et aucune recherche des huit cellules voisines
n'est nécessaire. Le calcul emploie `floor`, également aux coordonnées négatives.
La distance minimale entre centres candidats est de 25 km. L'ancienne cellule
de 10 km ne pouvait pas contenir un gouffre de 20 km de diamètre. Le déplacement
de la grille préserve la méthode de hash et évite les coupures aux frontières.

Chaque site est accepté ou rejeté en entier. L'exclusion des rivières utilise
une borne conservative sur le déplacement des méandres, qui protège tout le
disque du gouffre. Le masque régional est contrôlé au centre et sur huit points
d'un cercle élargi à 12 000 m. Les tests vérifient aussi l'intérieur des disques.

Une cellule ne signifie pas un gouffre garanti : le filtrage des rivières et des
régions élimine les sites incompatibles. La densité nominale est d'une candidate
par 2 500 km², avant ce filtrage. Le réglage de 50 km n'est donc pas une distance
garantie entre deux gouffres rencontrés.

## Parois 3D

`pit_porosity.glsl` produit un Perlin 3D de grande échelle, avec un seuil ajusté
à sa distribution réelle. La fraction de vide vaut 100 % du centre jusqu'à
7 000 m de rayon, soit les premiers 70 %. Elle décroît ensuite linéairement
sur les 3 000 m périphériques : 100 % à 7 km, 50 % à 8,5 km, 0 % à 10 km. Ces proportions
concernent le champ poreux avant sa découpe par le cône. Elles sont statistiques
sur plusieurs sites ; un gouffre individuel conserve des amas, arches et parois
irrégulières uniquement dans cette périphérie. L'intérieur du cône jusqu'à 7 km
est intégralement évidé, sans bloc de Perlin. La connectivité de tous les morceaux
de roche périphériques n'est pas imposée.

`volume.C.glsl` applique ce champ uniquement comme excavation sous le sol et
au-dessus du fond conique. Il ne relève pas le terrain. L'axe est ouvert depuis
la surface jusqu'au fond ; le sol sous le fond reste solide. Les cavités sont
volumétriques, avec des surplombs possibles, et non un simple bruit de hauteur.

Aux frontières de biomes, le contrôle mélangé ne peut pas être décodé comme
celui de ColdForest seul : le shader recalcule l'admission et la profondeur
près d'un candidat. La profondeur est pondérée par la présence locale de la
forêt froide, puis la contribution 3D suit l'accumulation habituelle. Ce fondu
est donc non linéaire. Les dimensions de 20 km × 800 m décrivent le biome pur.

La bande de travail couvre le fond et les marges de transition. Un fondu près
de la limite réelle de bande évite une coupure de densité. Le contrat existant
conserve toutefois son plafond de 16 384 unités brutes : une échelle très
faible ou des réglages extrêmes de décalage peuvent réduire la géométrie visible.
Les vérifications ci-dessous couvrent les échelles 0,2 / 0,35 / 1, avec décalage
global nul. Le shader tourne sur le pipeline existant ; aucun débit sur le GPU
Windows du jeu n'est déduit des mesures de validation logicielle.

## Vérification

```bash
python Tests/Terrain/calibrate_cold_forest_porosity.py
python Tests/Terrain/check_cold_forest_regions.py
python Tests/Terrain/check_cold_forest.py
```

Les sondes exécutent le GLSL de production avec OpenGL sur llvmpipe. Le rapport
est `docs/biome-validation/cold_forest.json`, et l'image du même nom reconstruit
la véritable densité ; ce n'est pas une capture du jeu. L'aperçu couvre 26 km
avec un pas horizontal de 100 m, donc les plus petits détails ne sont pas résolus.

- `cold_forest_regions.json` mesure une couverture dominante de 30,000019 % sur
  2 105 352 points et huit seeds ; la moyenne du masque vaut environ 30,016 %.
- 196 608 colonnes sur trois seeds vérifient le sol et les rivières : les
  formules de l'ancien détail sont conservées. Les calculs indépendants diffèrent
  au maximum de 0,0024 unité brute, soit moins de 1 mm à l'échelle 0,35, à cause
  des chemins d'arrondi du compilateur.
- 243 294 points répartis dans 46 disques acceptés ont un masque de rivière nul.
  Leur masque régional minimal mesuré est 0,8585. Les sites retenus représentent
  3,68 % des 1 250 cellules sondées.
- Les 43 068 sondes de géométrie respectent 20 000 m de diamètre et 800 m de
  profondeur. À 100 m du bord, la descente mesurée reste inférieure à 9 cm.
  La jointure angulaire de la spirale diffère de moins de 4 mm entre les deux
  côtés sondés.
- Dix rayons sur 19 sites donnent un gradient de vide à moins de 0,23 point
  de pourcentage des cibles. Le cœur jusqu'à 70 % est entièrement vide, et le
  bord entièrement solide, avant découpe par l'enveloppe.
- 49 152 sondes de densité sur trois échelles vérifient l'excavation, la bande,
  l'ouverture centrale et la protection du sol sous le fond. Les 24 336 points
  sondés à l'intérieur du cœur de 7 km sont tous évidés. Neuf cas de mélange
  vérifient l'indépendance vis-à-vis du contrôle du biome voisin.
- 84 sondes de frontières de placement, 1 331 densités du pipeline réel et
  125 points LOD partagés passent.
