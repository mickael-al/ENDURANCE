# FrozenTundra : terrain montagneux dense

La recette remplit désormais tout le biome de massifs et de cols. L'ancien espacement de 12 km entre chaînes a été supprimé : les montagnes occupent les deux directions, et leurs contreforts se rejoignent. Les grandes plaines seront choisies ensuite par le masque utilisateur. La contribution volumique reste nulle ; les anciens pingos et cavités ne sont pas réintroduits.

## Masque de présence

Dans [relief.C.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/relief.C.glsl), la variable demandée reste :

```glsl
float mountainPresence = 1.0;
```

Elle multiplie **tout** le relief montagneux, y compris les contreforts et les cols. `0` supprime cette contribution, `0.5` réduit son amplitude de moitié, `1` remplit la zone. Remplacer `1.0` par un masque de la position monde `p` permet de découper les grandes régions montagneuses et les futures plaines, sans déplacer les massifs. Le macrorelief commun au monde continue à s'appliquer. Le résultat du masque est borné entre zéro et un.

## Dimensions

Les paramètres sont dans [chain.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/chain.glsl), dont le nom est conservé pour ne pas changer les chemins existants.

| Constante | Valeur | Signification |
| --- | --- | --- |
| `FROZEN_WIDTH_MIN/MAX` | 1000 / 6000 m | Dimensions nominales indépendantes des empreintes sur les deux axes du placement. |
| `FROZEN_GAP_MIN/MAX` | −500 / +250 m | Écart signé entre intervalles voisins sur chacun des deux axes ; négatif = chevauchement. |
| `FROZEN_HEIGHT_MIN/MAX` | 1400 / 3200 m | Hauteur nominale des sommets, relative au macrorelief, à `detailScale=0.35`. |

Ces écarts décrivent les **empreintes nominales de placement**, pas une distance exacte entre les silhouettes sculptées au sol. La déformation du domaine, l'orientation propre à chaque sommet et les contreforts modifient les contours visibles. Les contreforts dépassent les empreintes principales pour produire les cols et supprimer les bandes nues.

## Construction

Deux partitions indépendantes d'intervalles variables remplissent le plan. Des blocs de quatre intervalles à sommes constantes permettent un accès direct aux sites, y compris aux coordonnées négatives, sans liste CPU ni parcours depuis l'origine. Les blocs voisins sont évalués pour conserver les chevauchements aux frontières.

Une rotation globale puis deux cisaillements continus courbent le placement dans les deux directions. Leur composition ne replie pas le domaine et conserve les surfaces. Chaque massif possède aussi sa propre orientation. Le profil de sommet accepté est conservé : crête principale, deux crêtes secondaires descendantes, contreforts, cirque latéral et bruit ridged.

Des contreforts inférieurs plus larges descendent autour de chaque massif et se rejoignent dans les deux directions. Leur hauteur varie avec la distance au sommet et le bruit de crêtes ; aucun plan horizontal constant n'est ajouté. L'assemblage par maximum conserve les hauteurs des sommets sans gonfler les chevauchements par addition.

La recherche porte sur 3 × 3 blocs, avec rejet géométrique avant les calculs de relief coûteux. La portée de chaque site est au plus 6000 m par axe pour des blocs de 13500 m. Revoir cette borne si les dimensions ou la portée des contreforts changent fortement.

Ce modèle est une construction procédurale dirigée artistiquement, pas une simulation d'érosion ou de tectonique.

## Validation de cette révision

- 8192 intervalles sur deux axes et deux seeds : largeurs dans 1000–6000 m, écarts dans −500 à +250 m, chevauchements et écarts positifs présents. Erreur maximale de raccord : 1,71 mm dans les coordonnées locales des blocs.
- Sur deux zones de 48 × 48 km échantillonnées tous les 125 m : aucun point de relief inférieur ou égal à 1 m, aucune ligne vide et aucune grande plaine nue. Hauteurs médianes 491 et 513 m ; maxima échantillonnés 2849 et 3050 m.
- La recherche 3 × 3 blocs est identique bit à bit à une référence 7 × 7 sur les points contrôlés. Déterminisme, finitude, masques constants 0/0,5/1 et masque spatial progressif vérifiés.
- Pour chaque seed : 1331 densités concordent avec l'accumulation de production et 125 points partagés entre LOD sont identiques.
- Les 26 recettes passent les contrôles de transitions, rivages, poids locaux, bornes, accumulation et LOD.

[Aperçu du relief réel GLSL sur 24 × 24 km](biome-validation/frozen_chains.png) · [Mesures](biome-validation/frozen_chains.json)

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_frozen_chains.py --output /tmp/frozen-mountains
```

Les limites CPU/GLSL de la révision précédente restent inchangées : relief brut ±10240, scène sauvegardée à 3072 feuilles. Cette correction modifie uniquement les shaders du biome : **régénérer le terrain** après mise à jour. Les tests utilisent Mesa llvmpipe ; l'aperçu ne constitue pas une mesure des performances du GPU Windows ni une validation du rendu en jeu.
