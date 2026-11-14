# Glace polaire : cavités larges et rares dans les montagnes

Les flancs des massifs sont creusés par les valeurs élevées d'un Perlin 3D à grande échelle. Une seule octave produit des poches larges ; le seuil élevé limite leur présence. Chaque massif utilise sa propre seed dérivée de son ancre et de la seed du monde. Les coordonnées verticales sont celles du monde, ce qui permet des cavités avec un plafond et des ouvertures sur les flancs.

Le relief 2D, les dimensions et le placement des montagnes restent identiques. La soustraction s'applique au volume après les autres sculptures. La crête, le pied et la population de failles sont exclus du masque de creusement. La profondeur diminue sur les petites montagnes pour garder une base solide.

## Réglages

Ils se trouvent en tête de [PolarIce/shape.glsl](../Asset/Shader/ProceduralTerrain/biomes/PolarIce/shape.glsl).

| Paramètre | Valeur | Effet |
| --- | --- | --- |
| `POLAR_MASSIF_CAVE_SCALE` | `vec3(850, 650, 850)` | Longueurs d'onde X/Y/Z, en mètres du domaine local. Augmenter élargit le bruit ; ce n'est pas le diamètre garanti des cavités. |
| `POLAR_MASSIF_CAVE_THRESHOLD` | `0.50` | Augmenter rend les cavités plus rares et plus petites ; diminuer creuse davantage. |
| `POLAR_MASSIF_CAVE_MAX_DEPTH` | `360` | Profondeur maximale sous la surface à `detailScale=0.35`. La hauteur locale et le mélange de biomes peuvent la réduire. Mettre 0 désactive cet ajout. |

Le masque de flanc passe progressivement de zéro à un entre les contours de montagne 0,20 et 0,32, puis revient à zéro entre 0,62 et 0,82. La profondeur reste au plus à 65 % d'une estimation conservatrice de l'épaisseur du massif. Le creusement s'atténue avant sa limite basse. La bande 3D s'élargit localement pour contenir ces poches, dans le contrat étendu déjà utilisé par les failles.

## Vérification

Le test compare le champ réel à une compilation où seule la soustraction des cavités de montagne est désactivée. Les colonnes et les autres sculptures restent identiques. Il vérifie la rareté dans les zones admissibles, l'absence d'ajout de matière, les plafonds, les ouvertures proches de la surface, les zones protégées, le déterminisme et la concordance avec l'accumulation GPU et les points LOD partagés.

La comparaison avec la révision précédente retrouve exactement les mêmes hauteurs sur 25 350 points autour de six montagnes, sur deux seeds. Les tests généraux des 26 recettes et des treize formations froides passent également ; les contributions ciblées du massif polaire (1422,94 m après plafond global) et de la faille (−734,13 m) sont préservées.

Sur douze massifs (six pour chacune des seeds 1337 et 17), le creusement représente 1,02 % et 1,34 % des échantillons solides des flancs admissibles. Les sondes trouvent respectivement 138 et 244 colonnes ouvertes à la surface, et 1155 et 1829 échantillons creusés sous un plafond solide. Ces nombres dépendent de la grille de mesure ; ce ne sont pas des nombres de grottes. Pour chaque seed, 1331 densités concordent avec le pipeline de production et 125 points LOD partagés sont identiques.

- [Coupe des cavités](biome-validation/polar_massif_caves.png)
- [Mesures, seed 1337](biome-validation/polar_massif_caves.json)
- [Mesures, seed 17](biome-validation/polar_massif_caves_seed17.json)

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar_massif_caves.py --output /tmp/polar-massif-caves
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar_massif_caves.py --seed 17 --output /tmp/polar-massif-caves-seed17
```

La figure provient des shaders sous OpenGL/Mesa llvmpipe. Le rendu en jeu, les collisions et le coût sur le GPU Windows restent à apprécier. Régénérer le terrain recompile les shaders et reconstruit les volumes.
