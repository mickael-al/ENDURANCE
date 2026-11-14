# FrozenTundra : massifs irréguliers et chevauchements

Le biome reste entièrement montagneux par défaut. Le placement réutilisant les mêmes lignes X/Z et les mêmes dimensions par rangée est remplacé par des positions décalées indépendamment en deux dimensions. Les massifs ont des silhouettes ramifiées différentes et les chevauchements forts relient aussi leurs flancs supérieurs.

## Masque utilisateur

Dans [relief.C.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/relief.C.glsl) :

```glsl
float mountainPresence = 1.0;
```

Ce masque multiplie **tout** le relief : sommets, contreforts et cols. `0` supprime cette contribution, `0.5` réduit son amplitude de moitié, `1` conserve toute la région montagneuse. Remplacer `1.0` par une fonction de la position monde `p` permet de découper les grandes régions et les futures plaines, sans déplacer les massifs. Le macrorelief commun au monde continue à s'appliquer. La contribution 3D reste nulle.

## Dimensions et écarts

La construction se trouve dans [chain.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/chain.glsl).

| Réglage | Valeur | Effet |
| --- | --- | --- |
| `FROZEN_GAP_MIN/MAX` | −500 / +250 m | Distance signée entre frontières principales partagées par deux voisins, mesurée suivant leur normale commune. |
| `FROZEN_HEIGHT_MIN/MAX` | 1400 / 3200 m | Amplitudes nominales des sommets, relatives au macrorelief, à `detailScale=0.35`. |
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

Le test dédié vérifie les écarts **sur la géométrie des frontières**, leur réciprocité, les profils réellement superposés, les bornes des empreintes, la couverture, les masques constants et spatial, le déterminisme, l'accumulation de production et les raccords LOD. Il produit aussi une planche de neuf massifs isolés pour vérifier leurs silhouettes.

Résultats de cette révision sur deux seeds : 8192 échantillons de frontières, erreur géométrique maximale 1,59 mm ; 78–83 % des limites actives sont chevauchantes. Les 5025 points de chevauchement substantiel contrôlés contiennent bien les deux profils principaux. Les cols des chevauchements forts atteignent une hauteur médiane d’environ 1300 m. Les empreintes mesurées sur 128 sites et 64 directions sont entre 2,2 et 4,0 km. Les deux zones de 48 × 48 km n’ont aucun échantillon nu ; leurs reliefs médians sont 816/797 m. Les masques, les recherches élargies, 2662 densités de production et 250 points LOD partagés passent. Les 26 recettes passent aussi les contrôles généraux d’accumulation, de transitions et de LOD.

[Aperçu du relief réel GLSL](biome-validation/frozen_chains.png) · [Silhouettes isolées](biome-validation/frozen_massifs.png) · [Mesures](biome-validation/frozen_chains.json)

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_frozen_chains.py --output /tmp/frozen-mountains
```

Les limites CPU/GLSL et les budgets restent inchangés. **Régénérer le terrain** après mise à jour. Les aperçus et tests utilisent Mesa llvmpipe ; ils ne mesurent pas les performances du GPU Windows ni le rendu final en jeu.
