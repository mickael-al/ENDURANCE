# Marécage froid : grandes sphères poreuses

La hauteur 2D de `ColdMarsh` est conservée intégralement : bassins, terrasses,
bruits fins et petits reliefs de berge de la recette précédente. L'ancienne
partie 3D, composée de lèvres de berge et d'une petite découpe, est remplacée
par de grandes formations sphériques, creusées par un Voronoï déformé.

## Dimensions et placement

Les réglages sont dans
`Asset/Shader/ProceduralTerrain/biomes/ColdMarsh/sphere_settings.glsl`.

| Constante | Valeur initiale | Rôle |
|---|---:|---|
| `COLD_MARSH_SPHERE_SPACING` | 10 000 m | Taille de la cellule de placement |
| `COLD_MARSH_SPHERE_RADIUS` | 1 000 m | Rayon de l'enveloppe, donc diamètre de 2 km |
| `COLD_MARSH_SPHERE_BURIED_FRACTION` | 0,25 | Partie du diamètre vertical enfouie |
| `COLD_MARSH_VORONOI_SCALE` | 900 m | Échelle des grandes cavités |
| `COLD_MARSH_WARP_SCALE` | 700 m | Échelle du Perlin qui déforme le Voronoï |
| `COLD_MARSH_WARP_STRENGTH` | 0,65 | Amplitude de cette déformation en cellules de Voronoï |
| `COLD_MARSH_PORE_THRESHOLD` | 0,43 | Seuil séparant le vide et la pierre |
| `COLD_MARSH_DETAIL_SCALE` | 100 m | Échelle du bruit fin sur les courbes |
| `COLD_MARSH_DETAIL_AMPLITUDE` | 22 m | Amplitude maximale de ce détail dans le champ de densité |

Un hash du seed et de l'identifiant de cellule choisit le centre dans la moitié
centrale de chaque cellule de 10 × 10 km. Il y a un candidat par cellule ; le
climat et les poids du biome déterminent ensuite sa présence dans le terrain.
Les centres ne sont pas alignés sur les centres géométriques des cellules.
La distance minimale entre deux centres candidats est de 5 km. Avec le rayon
initial et ses marges, chaque formation tient dans sa cellule ; aucun parcours
des cellules voisines n'est nécessaire. `floor` donne le même placement aux
coordonnées négatives.

La sphère est rigide dans l'espace : son enveloppe utilise `length(q) - radius`.
Le sol de référence est la hauteur du terrain au centre du site, incluant le
relief macro et la hauteur 2D inchangée de ce biome. Le centre de la sphère est
placé 500 m au-dessus de cette hauteur. Son enveloppe va donc de **−500 m à
+1 500 m** par rapport au sol d'ancrage : un quart du diamètre sous ce sol et
trois quarts au-dessus. Ces pourcentages concernent la hauteur, pas le volume
d'une calotte sphérique. Le terrain peut varier ailleurs sous cette sphère.

L'ancre est stockée dans le contrôle de colonne comme une altitude absolue
normalisée. La même altitude quantifiée est retrouvée dans toutes les colonnes,
ce qui évite de déformer la sphère en la faisant suivre chaque ondulation du sol.
L'erreur maximale théorique à l'échelle 0,35 et avec l'amplitude macro de 1 000 m
est d'environ 7 cm.

## Voronoï et bruit fin

`porosity.glsl` transforme d'abord la position avec trois Perlin 3D indépendants.
La position originale reste dans le domaine : le bruit déforme les coordonnées
sans rabattre tout l'espace dans une petite plage. Un Voronoï 3D à grande échelle
est ensuite évalué dans ces coordonnées. Son seuil produit de grosses ouvertures,
des parois courbes et des arches à l'intérieur de l'enveloppe sphérique.

Un Perlin supplémentaire à 100 m modifie légèrement la densité des grandes
courbes. Son amplitude de 22 m reste faible devant les cellules de 900 m : il
ajoute des irrégularités sans remplacer la forme principale par une multitude
de petites cavités. Les phases et les graines dépendent du site, pour varier
la structure interne entre les sphères.

Le bruit est intersecté avec l'enveloppe sphérique. Même avec du détail, aucun
morceau de la formation ne dépasse ce volume nominal. Le résultat est ensuite
réuni au terrain d'origine : la nouvelle 3D ajoute de la pierre et ne creuse pas
le sol. Les pores enterrés sont donc remplis par le terrain existant. La partie
exposée peut contenir des passages, cavités et surplombs ; tous les fragments
ne sont pas garantis connectés au sol. Le seuil est un réglage artistique et
ne prétend pas imposer un pourcentage exact de solide par sphère.

## Intégration et limites

Le calcul 2D garde exactement la formule de hauteur antérieure, mais remplace
les anciens contrôles de volume par la bande et l'ancre nécessaires aux sphères.
En biome pur, le volume décode cette ancre. À une frontière climatique, il la
recalcule, car le contrôle partagé contient aussi les contributions voisines.

La bande couvre la section verticale de la sphère à chaque colonne, avec des
marges pour le fondu et la quantification. Les points éloignés de la sphère et
ceux où le terrain est déjà plus solide sont rejetés avant les bruits 3D.
L'enveloppe du moteur garde son plafond de 16 384 unités brutes : des échelles
très faibles, de forts décalages de bande ou des mélanges de biomes peuvent
réduire la géométrie effectivement visible. Les dimensions nominales décrivent
le biome pur avec les réglages usuels, sans exiger une sphère intacte aux
frontières entre biomes.

## Vérification

```bash
python Tests/Terrain/check_cold_marsh.py
```

Ce contrôle exécute les recettes GLSL réelles sur OpenGL/llvmpipe. Il vérifie :

- la formule complète de hauteur antérieure, y compris les anciens petits
  reliefs 2D de berge ;
- le placement déterministe, les coordonnées négatives et les limites de cellule ;
- la sphère de 2 km et son enfouissement vertical de 25 % ;
- des volumes de pierre et de vide non triviaux, la déformation Perlin et
  l'amplitude du détail fin ;
- la conservation du terrain, les bandes et les sphères sur trois échelles ;
- l'indépendance du contrôle en mélange, puis le pipeline de production et
  les échantillons communs aux LOD.

Les mesures sont enregistrées dans `docs/biome-validation/cold_marsh.json`.
`cold_marsh.png` reconstruit la densité GLSL avec un pas de 20 m horizontal et
25 m vertical ; ce n'est pas une capture du jeu. Les vérifications logicielles
ne constituent pas un benchmark du GPU Windows, un test de collisions ou
une simulation d'eau.
