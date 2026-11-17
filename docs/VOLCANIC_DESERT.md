# Désert volcanique : volcans et sol fracturé

`VolcanicDesert` (biome 20) remplace les anciennes nappes à gradins, petites
calderas et sculpture de coulée 3D. Le nouveau terrain associe de grands volcans
à cratère ouvert et un sol de plaques basaltiques séparées par des fissures.

## Réglages

Dans `Asset/Shader/ProceduralTerrain/biomes/VolcanicDesert/settings.glsl` :

| Constante | Valeur initiale | Effet |
|---|---:|---|
| `VOLCANIC_CELL_SIZE` | 10 000 m | Taille des cellules de placement |
| `VOLCANIC_RADIUS_RANGE` | 1 800–2 800 m | Rayon nominal des volcans |
| `VOLCANIC_HEIGHT_RANGE` | 900–1 700 m | Hauteur nominale au-dessus du sol local |
| `VOLCANIC_CRATER_RADIUS_RANGE` | 0,20–0,30 | Rayon du cratère, en fraction du rayon du volcan |
| `VOLCANIC_CRATER_FLOOR_RANGE` | 0,22–0,40 | Altitude du fond, en fraction de la hauteur du volcan |

Les hauteurs sont exprimées en mètres pour `uDetailScale = 0.35` ; elles suivent
ce réglage global à une autre valeur. L'ellipse, la déformation et les petites
variations du rebord modifient les dimensions effectivement mesurées. Le relief
macro du terrain et les transitions climatiques restent également appliqués.

Dans `ground.glsl` :

| Constante | Valeur initiale | Effet |
|---|---:|---|
| `VOLCANIC_GROUND_PLATE_SCALE` | 700 m | Échelle des plaques Voronoï |
| `VOLCANIC_GROUND_WARP_SCALE` | 1 100 m | Échelle de la déformation principale |
| `VOLCANIC_GROUND_WARP_STRENGTH` | 180 m | Amplitude de cette déformation |
| `VOLCANIC_GROUND_CRACK_WIDTH` | 40 m | Largeur approximative des fissures |
| `VOLCANIC_GROUND_CRACK_DEPTH` | 65 m | Profondeur nominale, variable avec un bruit large |

## Placement et forme

Une graine issue du seed et de l'identifiant entier de cellule choisit un centre
dans sa moitié centrale. Il y a un candidat par carré de 10 × 10 km, avec une
distance minimale de 5 km entre centres candidats de cellules adjacentes.
La distance entre deux volcans visibles n'est donc pas constamment de 10 km.
Le biome doit être présent pour que le candidat contribue au terrain.

Chaque site possède son rayon, sa hauteur, son orientation, son allongement et
ses dimensions de cratère. Un bruit déforme le contour, tandis que des ravines
radiales et un relief secondaire modèlent les flancs. Le bord du cratère entoure
un fond nettement plus bas ; le volcan ne se termine pas par un simple pic.
Les neuf cellules voisines sont examinées : un pied de volcan peut dépasser sa
cellule sans être tronqué. Les éventuels chevauchements prennent le maximum
des hauteurs pour éviter de doubler artificiellement les sommets.

Le sol utilise la différence entre les deux premières distances Voronoï, qui
s'annule sur les limites des plaques. Une rotation et deux déformations de
coordonnées rendent ces limites moins régulières. Cette différence est une
approximation de distance : les 40 m indiquent une largeur de départ, pas une
largeur géométrique exacte partout. Les fissures sont creusées dans la hauteur,
avec de petites lèvres et une rugosité fine entre les plaques. Leur contribution
diminue sur les volcans pour laisser les ravines des flancs dominer la silhouette.

La recette est entièrement décrite par sa hauteur : le champ 3D renvoie zéro
et sa bande est nulle. Les fissures et cratères sont donc réellement ouverts
dans le maillage du terrain, sans grotte sous un toit ni sculpture indépendante.
Ce changement ne crée ni lave fluide, ni matériau incandescent.

## Vérification reproductible

```bash
python Tests/Terrain/check_volcanic_desert.py
```

Le contrôle exécute les fonctions GLSL de production sur OpenGL/llvmpipe :
placement déterministe sur deux seeds et coordonnées négatives, profils
radiaux de 16 volcans dans 64 directions, cratères ouverts et raccord des pieds,
fissures présentes et profondes, continuité aux limites des cellules, limites
de hauteur et densité sur trois échelles, comparaison au pipeline de production
et échantillons communs aux LOD.

Les résultats mesurés et l'aperçu sont dans
`docs/biome-validation/volcanic_desert.json` et `volcanic_desert.png`.
L'image reconstruit la hauteur réellement calculée par les shaders : ce n'est
pas une capture du jeu. Le détail des fissures est montré à part avec un pas
de 3,5 m, car la vue générale à 75 m ne les résout pas correctement.
Les vérifications ne mesurent pas le temps GPU sous Windows ni les collisions.
