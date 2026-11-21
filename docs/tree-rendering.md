# Affichage des arbres dans le terrain

Le composant `ProceduralTerrainComponent` active par défaut le rendu des positions
publiées par le terrain. Il charge les exports du catalogue
`ENDURANCE/Ressources/Trees/object_placement.json`, sans lancer le générateur du
laboratoire ni reconstruire les arbres. Le renderer appartient au jeu ; le
moteur fournit les passes G-buffer, les matrices des lumières et le cache d'ombres.

## Réglages

Le panneau **Affichage des arbres / LOD** expose ces paramètres sérialisés.
Appliquer les modifications avec **Régénérer avec ces réglages**.

| Paramètre | Défaut | Rôle |
|---|---:|---|
| `treeRenderingEnabled` | `true` | Active les arbres ; indépendant de la collecte des positions |
| `treeRenderDistance` | 2 048 m | Portée maximale depuis la caméra principale ; extinction progressive sur les derniers 10 % |
| `treeLod0Coverage` | 0,25 | Au-dessus : maillage détaillé LOD0 |
| `treeImpostorCoverage` | 0,06 | Au-dessous : imposteur LOD3 à 17 vues ; entre les deux : maillage LOD1 |
| `treeLodTransition` | 0,15 | Demi-largeur relative du fondu autour de chaque seuil |
| `treeModelBudgetMiB` | 512 Mio | Budget GPU des maillages et textures chargés |

Les seuils comparent le diamètre projeté de la sphère englobante à la hauteur
de l'écran. La projection, la taille de l'export et l'échelle de l'instance sont
prises en compte : les grands arbres conservent leur maillage plus loin.
La caméra orthographique utilise directement son échelle de projection.
Les deux bandes de transition ne doivent pas se chevaucher ; une configuration
invalide est signalée. Le renderer ne crée pas de LOD2 absent des exports.

La distance d'affichage ne génère aucun secteur supplémentaire. Les limites
du [placement](object-placement.md), notamment `maximumLod`, le nombre de
secteurs et la rétention, restent applicables. Une région lointaine jamais
collectée peut donc être vide. Les arbres n'ont pas encore de collision.

## Pipeline et mémoire

1. Sur une nouvelle publication des positions, un compute décode les coordonnées
   compactes, calcule l'échelle et la rotation autour de Y à partir du site entier
   et de la seed. Un déplacement dans le pool ne change pas cette transformation.
   Ce cache coûte **32 octets par instance publiée**, en plus des 8 octets de
   position déjà présents ; aucune matrice 4 × 4 n'est stockée.
2. Le CPU rejette les blocs éloignés ou hors frustum à partir de leurs limites
   conservatrices. Il groupe les blocs restants par modèle. Les limites chargées
   incluent les dimensions réelles des bakes et l'échelle maximale autorisée.
3. Un compute par passe traite les pages de 64 positions. Il rejette les sphères
   hors champ, choisit les LOD et compacte les références avec des atomiques en
   mémoire partagée. Il produit directement les commandes indirectes. Aucun
   compteur de visibilité ni aucune position ne revient sur le CPU.
4. Les groupes sont rendus avec `glMultiDrawElementsIndirect`. Un groupe possède
   au plus cinq appels : bois/feuillage de chaque maillage et imposteurs. Les
   commandes des niveaux inutilisés ont zéro instance. Il n'y a ni entité de
   scène ni appel de rendu par arbre.

Pour chaque bloc sélectionné et chaque passe conservée, les buffers occupent
1 536 octets de références, 100 octets de commandes et 4 octets de sélection.
Ce dimensionnement couvre aussi les transitions qui rendent deux LOD à la fois.
Les listes et les calculs sont réutilisés tant que la publication, les modèles,
la matrice de passe et les paramètres de caméra pertinents restent identiques.
La caméra et chaque couche d'ombre disposent de listes distinctes.

Un seul travail CPU charge/décode un modèle et prépare ses mipmaps en arrière-plan.
Le thread GL publie au plus un modèle terminé par préparation. Le cache privilégie
les secteurs proches, respecte le budget mémoire et limite le nombre de modèles
résidents à 64 (`RenderSettings::maximumResidentModels`). Les modèles moins
prioritaires peuvent être évacués ; un modèle qui ne tient pas est reporté sans
boucle de rechargement à chaque image. Il n'y a pas de capture d'imposteur en jeu.
Le chargement initial est progressif ; un upload GL peut néanmoins avoir un coût
ponctuel. Les budgets n'incluent pas les allocations internes du pilote.

## Matériaux, imposteurs et ombres

Le bois et les feuilles utilisent les couleurs et masques exportés. Le feuillage
est découpé par alpha à 0,5, avec écriture de profondeur et sans tri transparent.
Les mipmaps préservent la couverture. Les normales sont transformées dans le
monde et les couleurs sont encodées suivant le gamma du G-buffer existant.
Métal = 0, AO matériau = 1 ; roughness 0,90 pour le bois, 0,82 pour les feuilles
et 0,86 pour l'imposteur. L'éclairage différé, l'IBL, le SSAO et l'atmosphère
restent responsables de l'éclairage et de l'intégration dans le paysage.

Le LOD3 utilise les 17 captures exportées : 8 azimuts à 15°, 8 à 60° et le zénith.
Le vertex shader choisit au plus trois vues selon la direction et la rotation
de l'arbre. Une projection rayon/plan aligne leurs UV. Le fragment shader mélange
les couleurs en espace linéaire avec alpha prémultiplié et renormalise les
normales objet. Il ne lit pas les 17 paires par pixel. Un export sans imposteur
garde le LOD1 à distance. Un export illisible est signalé et ne bloque pas le terrain.

Les transitions utilisent deux masques de couverture complémentaires, stables
pour une caméra immobile. Elles limitent les sauts de silhouette mais peuvent
laisser une trame visible en mouvement sans antialiasing temporel. L'imposteur
reste une approximation : pas de profondeur par pixel ni d'auto-ombrage
géométrique, et les vues sous 15° utilisent l'anneau inférieur. Le vent est
désactivé dans ce renderer pour tous les LOD.

Les ombres utilisent les mêmes transformations, distances et seuils de LOD que
la caméra principale, mais leur propre frustum et leur propre orientation
d'imposteur. Un arbre hors champ peut donc projeter une ombre visible. Les
feuilles et imposteurs appliquent leur découpe alpha dans la passe d'ombre ;
leurs cartes ne projettent pas de rectangles opaques. Les révisions de contenu
invalident le cache d'ombres quand le placement, les modèles ou les LOD changent.
Chaque passe restaure les états GL qu'elle modifie ; SSBO 0 appartient au moteur.

## Code et validation

- `ANTAGONIZ/Source/ProjectUser/EnduranceTreeRenderer.cpp` : connexion au moteur.
- `ANTAGONIZ/Source/ProjectUser/TreeRenderGpu.cpp` : cache, chargement, compute et draws.
- `ENDURANCE/Ressources/Shader/Trees/instances.C.glsl` : transformations stables.
- `ENDURANCE/Ressources/Shader/Trees/cull.C.glsl` : visibilité, LOD et commandes indirectes.
- `ENDURANCE/Ressources/Shader/Trees/render.vs.glsl` et `render.fs.glsl` : G-buffer et ombres.
- `ENDURANCE/Ressources/Shader/Trees/impostor.glsl` : sélection et repères des captures.

`python Tests/Trees/check_runtime.py` exerce le renderer de production dans un
contexte OpenGL/EGL, avec un export réel du dépôt. `--fast-math` vérifie également
les options agressives de compilation ; `--captures dossier` sauvegarde les
vues de contrôle. Ces tests inspectent les commandes GPU, les trois LOD, leurs
transitions, la stabilité des transformations, le G-buffer, les ombres hors
champ, l'état OpenGL, les caches et le repli sans imposteur.
Mesa logiciel valide le fonctionnement, pas les performances sur le GPU cible.
