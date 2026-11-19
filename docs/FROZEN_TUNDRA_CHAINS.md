# FrozenTundra : régions montagneuses, plaines et grottes rares

Les massifs irréguliers sont conservés. Un masque de grande échelle découpe maintenant des régions montagneuses et des plaines de toundra, avec des transitions progressives sur plusieurs kilomètres. De rares cavernes 3D creusent les montagnes suffisamment épaisses. Le dernier réglage utilisateur de hauteur `× 2.0` est conservé.

## Régions et transition

[regions.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/regions.glsl) rassemble le masque et le sol des plaines. [relief.C.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/relief.C.glsl) les compose :

```glsl
float mountainPresence = frozenMountainPresence(p);
float h = plain / 0.35 + mountains * FROZEN_MOUNTAIN_HEIGHT_GAIN * mountainPresence;
```

| Réglage | Valeur | Effet |
| --- | --- | --- |
| `FROZEN_REGION_SCALE` | 38000 m | Échelle du bruit principal qui organise les grandes régions. |
| `FROZEN_REGION_TRANSITION` | 6500 m | Largeur nominale du fondu, à partir d'une distance approchée ; la largeur exacte varie selon la géométrie locale. |
| `FROZEN_REGION_THRESHOLD` | 0,02 | Décalage de la séparation : augmenter favorise les plaines, diminuer favorise les montagnes. |
| `FROZEN_MOUNTAIN_HEIGHT_GAIN` | 2,0 | Multiplicateur brut de hauteur conservé depuis le dernier réglage utilisateur. |

Le domaine est déformé à deux grandes échelles, puis deux fréquences de bruit sont combinées. Une normalisation par le gradient local transforme le signal en distance approchée : une zone de bruit plus pentue ne comprime plus automatiquement tout le fondu sur une bande étroite. Le mélange quintique a une pente et une dérivée de pente nulles aux extrémités. Les montagnes s'abaissent progressivement, sans test binaire de présence ni seuil appliqué directement à leur hauteur.

Pour personnaliser le découpage, modifier **`frozenMountainPresence(p)` dans regions.glsl**. Le relief et les grottes utilisent tous deux cette fonction. Un retour constant `1.0` rétablit les montagnes partout ; `0.0` laisse le sol de toundra et désactive les grottes. Les sites des massifs ne changent pas avec le masque.

## Plaines

`frozenPlainHeight(p)` combine des ondulations de plusieurs kilomètres, des reliefs intermédiaires plus doux, de petits bourrelets orientés par le vent, des cuvettes peu profondes et un détail inférieur au mètre. Le même sol continue sous les massifs pour éviter un raccord entre deux recettes indépendantes. Les cuvettes ne créent pas d'eau ni de végétation.

## Grottes 3D rares

[caves.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/caves.glsl) porte les réglages de rareté et de support ; [volume.C.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/volume.C.glsl) construit les cavernes.

| Réglage | Valeur | Effet |
| --- | --- | --- |
| `FROZEN_CAVE_SPACING` | 6000 m | Taille des cellules candidates. |
| `FROZEN_CAVE_OCCUPANCY` | 0,10 | Probabilité de retenir une candidate avant les conditions de montagne. |
| `FROZEN_CAVE_BAND` | 4096 unités brutes | Enveloppe verticale maximale, ouverte seulement près des candidates admissibles. |
| `FROZEN_CAVE_BEDROCK` | 30 m de référence | Épaisseur de socle protégée au-dessus de la borne supérieure du sol de plaine. |

La population nominale est d'une candidate par 360 km² **avant** les filtres : présence montagneuse forte au centre et localement, épaisseur de terrain suffisante, et contribution dominante de ce biome. Ce n'est donc pas une grotte tous les 6 km.

Chaque cavité combine une grande salle d'environ 380–580 m de largeur et 120–170 m de hauteur, une salle secondaire et une galerie courbe qui débouche à l'extérieur. Des irrégularités Perlin 3D larges déforment les parois ; elles ne remplacent pas la galerie explicite. Le champ enlève uniquement de la matière. Le sol, le plafond et le socle sont protégés, et les plaines sans montagne n'ouvrent pas de bande 3D.

La construction réutilise les primitives initiales `landEllipsoid`, `landCapsule`, `landUnion` et `landVolumeLocal`. Elle contient bien un volume d'air avec plafond et sol ; ses hauteurs suivent le terrain local selon le fonctionnement de ce helper. Elle ne promet donc pas un plancher horizontal fixe en coordonnées monde. Les dimensions verticales suivent `uDetailScale / 0.35`.

## Validation des régions et cavités

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_frozen_regions.py --output /tmp/frozen-regions
```

Le test échantillonne les shaders réels : continuité aux bords du masque, grandes régions de chaque type, détails des plaines, rareté et absence de cavités hors montagne. La comparaison du volume avec/sans cavités identifie les plafonds et sols, puis un parcours de connexité vérifie que l'air des salles rejoint l'extérieur. L'accumulation de production et les points partagés entre LOD sont aussi contrôlés.

Sur les deux seeds mesurées, les zones de 96 × 96 km contiennent 33–36 % de plaines, 29–30 % de montagnes à présence pleine et le reste en transition. Les 32 contrôles de raccord ne détectent pas de saut fini. La bande des grottes ne couvre que 0,59–0,71 % de ces zones. Les six cavités contrôlées possèdent plafond et sol ; 99,55–100 % des échantillons d'air sous plafond rejoignent l'extérieur, avec un dégagement vertical échantillonné de 137,5–187,5 m. Les 2662 densités de production et 250 points LOD ciblés près des parois excavées concordent.

[Aperçu des régions](biome-validation/frozen_regions.png) · [Coupe 3D d'une grotte](biome-validation/frozen_cave_1337.png) · [Mesures régionales et volumétriques](biome-validation/frozen_regions.json)

## Dimensions et écarts

La construction se trouve dans [chain.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/chain.glsl).

| Réglage | Valeur | Effet |
| --- | --- | --- |
| `FROZEN_GAP_MIN/MAX` | −500 / +250 m | Distance signée entre frontières principales partagées par deux voisins, mesurée suivant leur normale commune. |
| `FROZEN_HEIGHT_MIN/MAX` | 1400 / 3200 | Amplitudes du générateur de massifs isolé. Le biome applique ensuite le gain utilisateur `2.0` et `uDetailScale` : environ 980–2240 m à 0,35 et présence pleine, avant ajout du sol. |
| `FROZEN_SITE_SPACING` | 2750 m | Échelle de la trame utilisée uniquement pour retrouver les candidats. |
| `FROZEN_SITE_JITTER` | 620 m | Rayon du déplacement indépendant de chaque centre dans le plan. |
| `FROZEN_FACE_BEND` | 120 m | Amplitude maximale des courbes partagées entre voisins. |
| `FROZEN_COL_REACH` | 240 m | Portée des contreforts au-delà des frontières principales. |

`FROZEN_WIDTH_MIN/MAX = 1000/6000` décrivent les bornes demandées. Les dimensions sont produites **ensemble** par l'espacement, le déplacement et les courbes : changer seulement ces deux constantes de bornes ne redimensionne pas le relief. Avec les paramètres présents, chaque empreinte principale contient un disque de rayon au moins 510 m et reste dans un disque de rayon au plus 2947,8 m. Ses largeurs restent donc entre 1 et 6 km. Cela ne signifie pas une distribution uniforme de tailles entre ces deux extrêmes ; les empreintes mesurées sont principalement autour de 2 à 4 km, et leurs jonctions forment des ensembles plus grands.

Pour chaque paire, un tirage symétrique fournit `gap = mix(-500, 250, u*u)`. Environ 82 % des tirages sont donc négatifs, avec une moyenne théorique de −250 m. **Les deux voisins utilisent la même frontière courbe et le même écart** : un écart négatif superpose effectivement leurs profils principaux. La distance est locale, suivant la normale reliant les centres à coordonnée tangentielle égale ; ce n'est pas une distance minimale universelle entre tous les contours irréguliers. Une troisième montagne peut limiter une portion de frontière.

Les contreforts et les cols dépassent les empreintes principales. Sur les paires fortement chevauchantes, des cols hauts et courbes réunissent les flancs : les massifs fusionnent en groupes au lieu de conserver chacun une vallée fermée autour de lui. Les écarts positifs gardent des cols plus bas.

## Formes

Chaque massif reçoit une orientation propre, une échelle de crêtes et deux à cinq branches de longueurs, largeurs et courbures différentes. Des sommets secondaires et de petits cols varient le profil longitudinal. La rotation se fait dans l'espace physique avant une mise à l'échelle isotrope : l'étirement selon un axe global ne réoriente plus toutes les montagnes en longues barres parallèles.

Les flancs descendent progressivement sur toute l'empreinte. Ils ne saturent pas en plateau derrière une mince bordure. Le bruit de crêtes complète cette structure à plusieurs échelles. L'assemblage par maximum conserve les limites de hauteur sans additionner les sommets aux croisements.

Ce modèle est une construction procédurale dirigée artistiquement, pas une simulation d'érosion ou de tectonique.

## Recherche GPU et validation

La trame triangulaire permet de retrouver directement les candidats, sans liste CPU ni parcours depuis l'origine. Le déplacement de chaque centre dépend de ses deux coordonnées. La recherche utilise 19 sites candidats et jusqu'à 36 voisins pour leurs frontières, avec rejet géométrique et bornes conservatrices avant les calculs coûteux.

La portée principale est inférieure à 3000 m, celle des contreforts à 3428 m et celle d'un col à 4177 m de chacun de ses deux centres. Ces bornes restent compatibles avec les couronnes de recherche. Les tests comparent le résultat à des couronnes élargies, y compris aux coordonnées négatives.

Le test `check_frozen_chains.py` isole les massifs pour vérifier les écarts **sur la géométrie des frontières**, leur réciprocité, les profils réellement superposés, les bornes des empreintes, la couverture, les masques constants et spatial, le déterminisme, l'accumulation de production et les raccords LOD. Il produit aussi une planche de neuf massifs isolés pour vérifier leurs silhouettes.

Régression des massifs isolés, hors masque régional et au gain historique de référence : 8192 échantillons de frontières, erreur géométrique maximale 1,59 mm ; 78–83 % des limites actives sont chevauchantes. Les 5025 points de chevauchement substantiel contrôlés contiennent bien les deux profils principaux. Les cols des chevauchements forts atteignent une hauteur médiane d’environ 1300 m. Les empreintes mesurées sur 128 sites et 64 directions sont entre 2,2 et 4,0 km. Les deux zones de 48 × 48 km n’ont aucun échantillon nu ; leurs reliefs médians sont 816/797 m. Les masques, les recherches élargies, 2662 densités de production et 250 points LOD partagés passent. Les 26 recettes passent aussi les contrôles généraux d’accumulation, de transitions et de LOD.

[Aperçu des massifs isolés, masque régional désactivé](biome-validation/frozen_chains.png) · [Silhouettes isolées](biome-validation/frozen_massifs.png) · [Mesures](biome-validation/frozen_chains.json)

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_frozen_chains.py --output /tmp/frozen-mountains
```

Les limites CPU/GLSL et les budgets restent inchangés. **Régénérer le terrain** après mise à jour. Les aperçus et tests utilisent Mesa llvmpipe ; ils ne mesurent pas les performances du GPU Windows ni le rendu final en jeu.
