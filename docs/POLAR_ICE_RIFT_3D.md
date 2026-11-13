# Glace polaire : parois de faille en 3D

Révision du 14 novembre 2026. La faille garde son incision 2D et reçoit une déformation horizontale dépendant de X, Y et Z. Les parois présentent désormais des décrochements, renfoncements et surplombs qui varient avec la profondeur. Le bruit ne sert pas seulement à déplacer verticalement la surface ou à modifier ses normales.

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
| `POLAR_RIFT_WALL_WARP = vec2(22,44)` | `shape.glsl` | Amplitudes du déplacement longitudinal/transversal principal, en mètres du domaine local. |
| `POLAR_RIFT_WALL_DETAIL = vec2(4,7)` | `shape.glsl` | Amplitudes du détail secondaire. |
| `vec3(180,90,130)` / `vec3(55,28,45)` | `volume.C.glsl`, `polarRiftWallWarp()` | Échelles X/Y/Z des deux bruits ; augmenter Y allonge les irrégularités verticalement. |

Les constantes d’amplitude servent aussi à calculer la zone où la bande 3D doit être ouverte. Le bruit est borné à ±1 pour respecter cette enveloppe. La déformation s’atténue entre 4 et 18 % de la profondeur puis entre 82 et 96 % : l’ouverture et le fond retrouvent le contour d’origine. Le masque compact de la population reste fixe, donc les sculptures ne débordent pas sur une cellule voisine.

## Champ volumique et bande

Le calcul retrouve le niveau de la rive en annulant seulement l’incision 2D pondérée du biome polaire. À chaque altitude, il décale les coordonnées horizontales et réévalue le contour de la faille. La différence entre l’ancien creusement et le creusement déplacé modifie le champ de terrain avant les opérations de réunion avec la nappe et de soustraction de la grotte. Cet ordre évite qu’un bruit négatif creuse accidentellement le champ de distance extérieur de la grotte.

La bande de 201,6 m ne suffisait pas pour déplacer latéralement une paroi profonde de 700–900 m. Une enveloppe conservatrice élargie couvre maintenant seulement la zone balayée par les parois déformées. Le fond plat et le reste du biome gardent les bandes habituelles. Le maximum accepté passe de 576 à 8192 unités brutes. Le stockage reste à 32 octets par colonne : un bit auparavant libre sélectionne la gamme étendue, tandis que les bandes jusqu’à 576 conservent exactement leur précision et leur encodage précédents. Les offsets restent bornés à ±160.

Les bornes CPU et les rejets GPU sont synchronisés avec la nouvelle gamme. La limite de sélection reste 2048 : le contrôle CPU atteint au maximum 1980 feuilles sur les quatre positions testées de la scène. La bande profonde augmente le travail volumique près des parois ; le coût sur GPU Windows reste à mesurer en jeu.

## Validation

La coupe de référence, seed 1337, est comparée au même champ dont seule la déformation des parois est désactivée. Les colonnes, la bande, la nappe et la grotte sont identiques dans les deux cas.

- Déplacement latéral mesuré : maximum 34,29 m, médiane 13,32 m sur les parois centrales échantillonnées.
- 31 colonnes de la coupe présentent plusieurs transitions verticales solide/air au milieu de la faille : preuve de surplombs dans le champ échantillonné.
- 677 échantillons changent de signe à plus de 202 m de la surface 2D initiale.
- Ouverture et fond identiques, valeurs finies, aucune contribution hors de la bande.
- 1331 densités comparées au pipeline de production, 125 points partagés entre LOD identiques.
- Seconde seed (17) : déplacement maximum 43,73 m, 45 colonnes avec surplomb et 671 changements profonds ; mêmes contrôles du fond, de l’ouverture et du pipeline réussis.
- Les 26 recettes, transitions et poids locaux passent les tests généraux ; les treize formations froides passent les contrôles de cavités et de pipeline/LOD.
- Contrat : 612 allers-retours de stockage, 40 cas de bande/échelle et 18 de hauteur ; 41154 densités de bande large identiques au calcul exhaustif et à un oracle indépendant.

Les tests exécutent les shaders réels sous OpenGL 4.5/Mesa llvmpipe. Les coupes ne valident pas les collisions de la capsule ni la traversabilité en jeu.

- [Coupe avant/après](biome-validation/polar_rift_volume.png)
- [Mesures détaillées](biome-validation/polar_rift_volume.json)
- [Seconde seed](biome-validation/polar_rift_volume_seed17.json)

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar_rift_volume.py --output /tmp/polar-rift-volume
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar_rift_volume.py --seed 17 --output /tmp/polar-rift-volume-seed17
```

Le changement des bornes CPU demande de recompiler le moteur, puis de régénérer le terrain pour reconstruire les colonnes avec les nouvelles bandes.
