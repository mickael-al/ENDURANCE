# Forêt boréale : quatre couches discrètes et cavités Voronoï

BorealForest conserve le bruit du sol de sa recette précédente, y compris son
warp, ses larges ondulations et le détail fin. Le monticule localisé et les deux
masses 3D creusées de l'ancienne recette sont supprimés. Les nouvelles couches
s'ajoutent à ce sol sans changer les autres biomes.

## Relief demandé

Quatre champs simplex fBm indépendants ont chacun cinq octaves. Leurs valeurs
sont normalisées dans `[0,1]` puis comparées **strictement à 0,7** : une valeur
supérieure donne `1`, les autres donnent `0`. Les quatre résultats sont ajoutés.
Il existe donc cinq niveaux de surélévation : zéro, une, deux, trois ou quatre
couches. Les limites des plateaux sont volontairement discrètes ; aucun fondu
n'est introduit à la place du seuil demandé.

Le point de départ est une échelle principale de **24 km** et une hauteur de
**600 m par couche à `detailScale=0,35`**. Les cinq octaves apportent aussi des
variations plus petites : 24 km ne signifie pas que chaque plateau mesure 24 km.
Un fBm ne suit pas une distribution uniforme ; le seuil 0,7 ne donne pas 30 % de
surface active. Les proportions réelles sont mesurées dans le rapport.

Les constantes se trouvent dans
`Asset/Shader/ProceduralTerrain/biomes/BorealForest/settings.glsl` :

| Réglage | Effet |
|---|---|
| `BOREAL_MOUNTAIN_SCALE` | Taille horizontale des champs de montagnes |
| `BOREAL_MOUNTAIN_OCTAVES` | Nombre d'octaves, initialement cinq |
| `BOREAL_MOUNTAIN_THRESHOLD` | Seuil strict d'activation, initialement 0,7 |
| `BOREAL_LAYER_HEIGHT` | Hauteur brute d'une couche avant `detailScale` |
| `BOREAL_CAVITY_SCALE` | Taille des cellules Voronoï, initialement 400 m |
| `BOREAL_CAVITY_WARP_SCALE` | Échelle des Perlin de déformation, 600 m |
| `BOREAL_CAVITY_WARP_STRENGTH` | Amplitude de cette déformation, 0,65 cellule |

## Excavation 3D limitée aux nouvelles couches

Le canal de contrôle `w` transmet uniquement la hauteur montagneuse normalisée,
soit le nombre de couches divisé par quatre. En biome pur, le volume décode ce
nombre arrondi après le stockage 16 bits ; il ne recalcule pas les quatre fBm
pour chaque voxel. Aux frontières mélangées, ce canal contient aussi le contrôle
des voisins : le volume recalcule alors sa propre contribution et tient compte
de son poids de biome.

Trois Perlin 2D indépendants sont évalués sur les plans `YZ`, `ZX` et `XY` des
positions 3D. Ils déforment les trois coordonnées injectées dans un Voronoï 3D.
La coordonnée Y reste donc active : les cavités ne sont pas des trous 2D extrudés.
Les échelles initiales sont larges, environ 400 m pour le Voronoï et 600 m pour
la déformation Perlin.

Le seuil du Voronoï est calibré sur son véritable champ composé pour obtenir
**environ 60 % d'air dans les nouvelles montagnes**. Ce pourcentage est une
proportion statistique de volume, pas une valeur de bruit égale à 0,6. Les
cavités sont bornées par le sommet des couches et le sol d'origine. Elles ne
creusent pas le substrat sous les nouvelles montagnes et ne s'activent pas dans
les colonnes sans surélévation.

Cette construction n'impose pas que chaque poche rejoigne l'extérieur ni que
tous les fragments de pierre soient reliés au sol. Elle ne simule pas une érosion.

## Vérification

```bash
EGL_PLATFORM=surfaceless python3 Tests/Terrain/calibrate_boreal_cavities.py --output docs/biome-validation/boreal_cavities_calibration.json
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_boreal_forest.py
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_boreal_forest.py --render-only
```

Le test compare le sol avec la recette antérieure privée de son seul monticule,
exécute les quatre bruits de production, mesure le vide dans des volumes de
montagnes réels et contrôle le décodage du canal de contrôle, la protection du
sol, les mélanges de biomes et les échantillons communs entre LOD. Sur 786 432 colonnes et trois seeds, le sol est bit-identique à la recette
antérieure sans le monticule. La couverture montagneuse mesurée atteint
27,54–27,69 %, avec tous les niveaux de zéro à quatre observés. Les 294 912
points de volume donnent 59,76–60,41 % de vide suivant le seed et l'échelle.
Les 36 864 points mélangés ne dépendent pas du contrôle des biomes voisins.
Les 1 331 densités de production et 125 points LOD ciblés concordent. Les mesures
figurent dans `docs/biome-validation/boreal_forest.json`. L'aperçu éventuel est
une reconstruction des densités GLSL, pas une capture du jeu.

Le plafond global de bande reste fixé à 16384 unités brutes. Les tests de cette
recette couvrent `detailScale` à 0,2, 0,35 et 1 avec décalage global nul ; ils ne
constituent pas une certification des offsets extrêmes, du moteur de collision
ni des performances d'un GPU Windows.
