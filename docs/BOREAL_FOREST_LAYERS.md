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
| `BOREAL_CAVITY_SCALE` | Taille des cellules Voronoï, 1 600 m |
| `BOREAL_CAVITY_WARP_SCALE` | Échelle des Perlin de déformation, 600 m |
| `BOREAL_CAVITY_WARP_STRENGTH` | Amplitude de cette déformation, 0,65 cellule |
| `BOREAL_SOLID_BASE` | Fraction solide cible au pied du massif, 0,80 |
| `BOREAL_SOLID_TOP` | Fraction solide cible au sommet du massif, 0,50 |

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
L'échelle du Voronoï reste de **1 600 m**, celle du Perlin de déformation de
600 m et son amplitude de 0,65 cellule. Les couches montagneuses sont conservées.

La fraction de roche cible diminue linéairement de **80 % au pied à 50 % au
sommet**, donc le vide passe de 20 % à 50 %. La hauteur utilisée est celle du
massif complet : `(Y - sol_initial) / hauteur_montagne`. Le gradient ne redémarre
pas après chaque couche de 600 m. Une table de quantiles du véritable champ
composé transforme la fraction de vide en seuil Voronoï ; sa valeur brute n'est
pas directement une probabilité. Elle tient aussi compte de la phase verticale
dans les cellules Voronoï, afin qu'un niveau proche d'un plan de la grille ne
devienne pas artificiellement plus dense. Les coordonnées et l'échelle du bruit
restent les mêmes.

La proportion moyenne visée sur l'épaisseur totale est donc d'environ **35 % de
vide**. Les proportions sont statistiques : chaque poche et chaque massif
peuvent différer. Les cavités sont bornées par le sommet des couches et le sol
d'origine. Elles ne creusent pas le substrat sous les nouvelles montagnes et ne
s'activent pas dans les colonnes sans surélévation.

Cette construction n'impose pas que chaque poche rejoigne l'extérieur ni que
tous les fragments de pierre soient reliés au sol. Elle ne simule pas une érosion.

## Vérification

```bash
EGL_PLATFORM=surfaceless python3 Tests/Terrain/calibrate_boreal_cavities.py --output docs/biome-validation/boreal_cavities_calibration.json
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_boreal_forest.py
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_boreal_forest.py --render-only
```

Le test compare le sol avec la recette antérieure privée de son seul monticule,
exécute les quatre bruits de production et contrôle le canal de contrôle, la
protection du sol, les mélanges de biomes et les échantillons communs entre LOD.
Il mesure le vide dans des volumes de montagnes réels, dans dix tranches de
hauteur normalisée, ainsi qu'à 1 % et 99 % de la hauteur du massif. La
concordance entre le volume de production et le champ calculé avec la hauteur
totale détecte un éventuel redémarrage du gradient entre deux couches.

Sur trois seeds et les trois échelles testées, les 294 912 points pris dans les
montagnes donnent **35,23 % de vide en moyenne** (34,85–35,61 % par cas).
Les sondes à 1 % et 99 % de hauteur donnent respectivement **79,49 % et
50,38 % de roche** ; les cibles à ces positions sont 79,7 % et 50,3 %. Les dix
tranches regroupées restent à moins de 1,45 point de pourcentage du profil
linéaire. Le sol et les quatre bruits de couches restent identiques ; les
36 864 contrôles de mélange et les échantillons de production/LOD passent.

Les mesures courantes et leur empreinte de sources figurent dans
`docs/biome-validation/boreal_forest.json`. L'aperçu
`docs/biome-validation/boreal_forest.png` est une reconstruction de ces densités
GLSL, pas une capture du jeu. Il correspond au Voronoï de 1 600 m et au gradient
80 → 50 % de roche.

Le plafond global de bande reste fixé à 16384 unités brutes. Les tests de cette
recette couvrent `detailScale` à 0,2, 0,35 et 1 avec décalage global nul ; ils ne
constituent pas une certification des offsets extrêmes, du moteur de collision
ni des performances d'un GPU Windows.
