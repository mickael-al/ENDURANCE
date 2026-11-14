# Glace polaire : parois de faille en 3D

La faille combine une incision 2D et une déformation horizontale dépendant de X, Y et Z. Les parois présentent des décrochements, renfoncements et surplombs qui varient avec la profondeur. Les deux bruits de paroi ont désormais des longueurs d'onde multipliées par 2,5, avec les mêmes amplitudes : leurs formes sont plus étalées.

La silhouette n'utilise plus une bifurcation obligatoire en Y. Chaque site choisit une courbe principale et zéro à trois ramifications. Leur départ, côté, direction, longueur et largeur varient indépendamment. Le tracé reste déterministe pour une même seed et une même position ; les deux programmes partagent exactement la même silhouette. Des branches proches peuvent se rejoindre.

## Réglages préservés

Le point de départ est le commit utilisateur `1c933e8` : Voronoï à 700 m, amplitude de massif demandée 450–1580 m, faille d’espacement 8000 m, facteur d’empreinte 1,5, sel 209 et demi-largeur 80–405 m. Ces valeurs sont conservées. Le plafond global de relief reste ±4096 unités brutes (±1433,6 m à `detailScale=0,35`) ; la valeur de massif demandée n’est donc pas une garantie d’atteindre 1580 m. Le helper de placement continue de borner le facteur d’empreinte à 1.

Le calcul volumique utilisait encore l’ancienne population de faille (facteur 0,25, sel 109), différente du relief modifié par l’utilisateur. Les deux programmes utilisent maintenant les mêmes constantes dans [PolarIce/shape.glsl](../Asset/Shader/ProceduralTerrain/biomes/PolarIce/shape.glsl), afin qu’un réglage de placement s’applique simultanément au relief, aux parois et à la grotte.

## Ajuster le résultat

| Réglage | Emplacement | Effet |
| --- | --- | --- |
| `POLAR_RIFT_SPACING` | `shape.glsl` | Espacement nominal des failles. |
| `POLAR_RIFT_FOOTPRINT` | `shape.glsl` | Taille du support, donc principalement longueur ; maximum effectif 1 dans le helper actuel. |
| `POLAR_RIFT_HALF_WIDTH_MIN/MAX` | `shape.glsl` | Demi-largeur minimale/maximale avant variation du contour. |
| `polarRiftDepth()` | `shape.glsl` | Profondeur 700–900 m au réglage de détail 0,35. |
| `POLAR_RIFT_WALL_STRENGTH` | `shape.glsl` | Intensité globale ; 0 désactive la sculpture et sa bande supplémentaire. |
| `POLAR_RIFT_WALL_NOISE_SCALE = 2.5` | `shape.glsl` | Échelle des deux bruits 3D ; augmenter étale les formes dans les trois directions, sans modifier leur amplitude. |
| `POLAR_RIFT_WALL_WARP = vec2(22,44)` | `shape.glsl` | Amplitudes du déplacement longitudinal/transversal principal, en mètres du domaine local. |
| `POLAR_RIFT_WALL_DETAIL = vec2(4,7)` | `shape.glsl` | Amplitudes du détail secondaire. |
| `vec3(180,90,130)` / `vec3(55,28,45)` | `volume.C.glsl`, `polarRiftWallWarp()` | Échelles X/Y/Z de base, multipliées par `POLAR_RIFT_WALL_NOISE_SCALE` : 450/225/325 m et 137,5/70/112,5 m actuellement. |
| `polarRiftBranchCount()` | `shape.glsl` | Probabilités de 0/1/2/3 branches : 38/34/21/7 %. |
| `polarRiftTrunkParameters()` / `polarRiftTrunkNoise()` | `shape.glsl` | Courbure, dérive et irrégularité large du tracé principal. |
| `polarRiftBranchLayout()` | `shape.glsl` | Départ, longueur signée, écart latéral signé et largeur des branches. |

Les constantes d’amplitude servent aussi à calculer la zone où la bande 3D doit être ouverte. Le bruit est borné à ±1 pour respecter cette enveloppe. La déformation s’atténue entre 4 et 18 % de la profondeur puis entre 82 et 96 % : l’ouverture et le fond retrouvent le contour d’origine. Le masque compact de la population reste fixe, donc les sculptures ne débordent pas sur une cellule voisine.

## Champ volumique et bande

Le calcul retrouve le niveau de la rive en annulant seulement l’incision 2D pondérée du biome polaire. À chaque altitude, il décale les coordonnées horizontales et réévalue le contour de la faille. La différence entre l’ancien creusement et le creusement déplacé modifie le champ de terrain avant les opérations de réunion avec la nappe et de soustraction de la grotte. Cet ordre évite qu’un bruit négatif creuse accidentellement le champ de distance extérieur de la grotte.

La bande de 201,6 m ne suffisait pas pour déplacer latéralement une paroi profonde de 700–900 m. Une enveloppe conservatrice élargie couvre maintenant seulement la zone balayée par les parois déformées. Le fond plat et le reste du biome gardent les bandes habituelles. Le maximum accepté, initialement porté de 576 à 8192 unités brutes pour les failles, est désormais de 16384 pour couvrir aussi les colonnes inclinées de Taiga au-dessus d’un terrain plus bas. Le stockage reste à 32 octets par colonne : un bit auparavant libre sélectionne la gamme étendue, tandis que les bandes jusqu’à 576 conservent exactement leur précision et leur encodage précédents. Le pas de quantification des bandes larges est de 16384/65535, soit environ 0,25 unité brute ; les hauteurs restent en FP32. Les offsets restent bornés à ±160.

Les bornes CPU et les rejets GPU sont synchronisés avec la nouvelle gamme. La limite de sélection reste 2048 : le contrôle CPU atteint au maximum 1980 feuilles sur les quatre positions testées de la scène. La bande profonde augmente le travail volumique près des parois ; le coût sur GPU Windows reste à mesurer en jeu.

## Validation

Les coupes choisissent des failles sans branche afin de mesurer la déformation d'une même paroi : à une jonction, la première intersection solide/air pourrait changer de branche et fausser le déplacement mesuré. Le champ est comparé à une compilation où seule la déformation des parois est désactivée. Les colonnes, la bande, la nappe et la grotte sont identiques. L'atlas et le test de couverture contrôlent aussi les silhouettes ramifiées.

- Sur 98 sites et deux seeds : 39 failles sans branche, 35 avec une branche, 17 avec deux et 7 avec trois. L'atlas compare douze silhouettes dans la même orientation locale.
- Bruit 3D : variation verticale RMS entre échantillons de 4 m réduite de 3,012 à 1,212 ; amplitude RMS transversale pratiquement identique, 18,95 → 18,91 m.
- Coupe seed 1337 : déplacement latéral maximum 38,00 m, médiane 13,67 m sur les parois centrales échantillonnées.
- 19 colonnes présentent plusieurs transitions verticales solide/air au milieu de la faille : preuve de surplombs dans le champ échantillonné.
- 661 échantillons changent de signe à plus de 202 m de la surface 2D initiale.
- Ouverture et fond identiques, valeurs finies, aucune contribution hors de la bande.
- 1331 densités comparées au pipeline de production, 125 points partagés entre LOD identiques.
- Seconde seed (17) : déplacement maximum 44,29 m, 21 colonnes avec surplomb et 831 changements profonds ; mêmes contrôles du fond, de l'ouverture et du pipeline réussis.
- Couverture : 24 sites, 893 976 points, 13 décalages couvrant les bornes du déplacement et des valeurs intérieures. Aucun contour modifié hors de la zone pleinement couverte.
- Les 26 recettes, transitions et poids locaux passent les tests généraux ; les treize formations froides passent les contrôles de cavités et de pipeline/LOD.
- La précédente révision a validé le contrat de bande, inchangé ici : 612 allers-retours de stockage, 40 cas de bande/échelle et 18 de hauteur ; 41154 densités de bande large identiques au calcul exhaustif et à un oracle indépendant.

Les tests exécutent les shaders réels sous OpenGL 4.5/Mesa llvmpipe. Les coupes ne valident pas les collisions de la capsule ni la traversabilité en jeu.

- [Coupe avant/après](biome-validation/polar_rift_volume.png)
- [Mesures détaillées](biome-validation/polar_rift_volume.json)
- [Seconde seed](biome-validation/polar_rift_volume_seed17.json)
- [Douze silhouettes de failles](biome-validation/polar_rift_shapes.png)
- [Échelle du bruit avant/après](biome-validation/polar_rift_noise_scale.png)
- [Mesures de diversité et de bruit](biome-validation/polar_rift_variation.json)
- [Couverture des parois et branches](biome-validation/polar_rift_coverage.json)

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar_rift_volume.py --output /tmp/polar-rift-volume
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar_rift_volume.py --seed 17 --output /tmp/polar-rift-volume-seed17
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar_rift_variation.py --output /tmp/polar-rift-variation
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar_rift_coverage.py --output /tmp/polar-rift-coverage
```

Cette révision de forme et d'échelle demande de régénérer le terrain pour recompiler les shaders. La première installation des bandes étendues de la révision précédente demande aussi de recompiler le moteur.
