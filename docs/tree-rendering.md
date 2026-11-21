# Affichage des arbres dans le terrain

Le composant `ProceduralTerrainComponent` active par défaut le rendu des positions
publiées par le terrain. Il charge les exports du catalogue
`ENDURANCE/Ressources/Trees/object_placement.json`, sans lancer le générateur du
laboratoire ni reconstruire les arbres. Le renderer appartient au jeu ; le
moteur fournit les passes G-buffer, les matrices des lumières et le cache d'ombres.

## Réglages

Le panneau **Affichage des arbres / LOD** expose ces paramètres sérialisés.
**Afficher les arbres** agit immédiatement, sans régénérer le terrain. Les
distances, seuils et budgets nécessitent **Régénérer avec ces réglages**.

| Paramètre | Défaut | Rôle |
|---|---:|---|
| `treeRenderingEnabled` | `true` | Active immédiatement le rendu et la préparation des arbres ; indépendant de la collecte des positions |
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

Masquer les arbres suspend leurs préparations, leurs draws et leurs ombres,
mais conserve les modèles déjà chargés. Le changement invalide les caches
d'ombres. Ce bouton permet de comparer une même scène avec et sans arbres,
sans confondre leur coût avec celui d'une régénération ou d'un rechargement.

## Pipeline et mémoire

1. Chaque page de placement reçoit un `allocationRevision` monotone. Ses
   positions restent immuables jusqu'à sa libération. Une publication sans
   changement des blocs d'arbres ne change pas leur révision. Le renderer
   conserve les transformations des pages inchangées ; un compute ne décode
   que les pages nouvelles ou réallouées. Il calcule l'échelle et la rotation
   autour de Y à partir du site entier et de la seed. Déplacer une position dans
   le pool ne change donc pas son apparence. Une publication manuelle avec
   `allocationRevision = 0` reste compatible, mais ses pages sont recalculées
   à chaque nouvelle révision de publication. Même si un autre éditeur de blocs
   avance sa révision sans changer une liste de pages immuables, la comparaison
   exacte conserve aussi les métadonnées et les commandes déjà préparées.
2. Le CPU rejette les blocs éloignés ou hors frustum à partir de leurs limites
   conservatrices. Leur ordre par modèle est préparé une seule fois à chaque
   publication, puis filtré sans nouveau tri à chaque passe. La sélection par
   distance et les limites des pages sont communes aux passes de caméra et
   d'ombres ; seuls leurs frustums sont filtrés séparément. Les limites chargées
   incluent les dimensions réelles des bakes et l'échelle maximale autorisée.
3. Un compute par passe traite les pages de 64 positions. Il rejette les sphères
   hors champ, choisit les LOD et compacte les références avec des atomiques en
   mémoire partagée. Des réservations atomiques regroupent ensuite les pages
   dans une liste par modèle et LOD. Il produit directement **cinq commandes
   indirectes par modèle**, quelle que soit la quantité de pages sélectionnées.
   Aucun compteur de visibilité ni aucune position ne revient sur le CPU.
4. Les deux LOD géométriques partagent un buffer de sommets et d'indices par
   matériau, avec leurs propres `firstIndex` et `baseVertex`. Les sommets et
   indices locaux restent identiques. Un groupe possède **trois appels**
   `glMultiDrawElementsIndirect` : deux commandes pour le bois, deux pour les
   feuilles, une pour l'imposteur, contre cinq appels précédemment. Les
   commandes des niveaux inutilisés ont zéro instance. Il n'y a ni entité de
   scène ni appel de rendu par arbre.

Les transformations occupent **32 octets par emplacement**, sans matrice 4 × 4.
Le cache réserve une page de 64 emplacements, soit **2 048 octets par page**,
même si elle n'est pas pleine. Les emplacements des pages libérées sont réutilisés.
Les positions compactes de 8 octets restent dans le pool du placement.

| Buffer du renderer | Taille utile avant arrondi de capacité |
|---|---:|
| Métadonnées des blocs publiés | 48 octets par page |
| Métadonnées des transformations à recalculer | 48 octets par page modifiée |
| Métadonnées des modèles | 96 octets par modèle, dont 32 octets d'offsets des LOD regroupés |
| Transformations | 2 048 octets par emplacement de page réservé |
| Sélection, par passe | 16 octets par page sélectionnée |
| Références visibles, par passe | 1 536 octets par page sélectionnée |
| Commandes indirectes, par passe | 100 octets par modèle sélectionné |

Les buffers de travail grandissent par puissances de deux à partir de 4 Kio et
conservent leur capacité pour les publications suivantes. L'agrandissement des
transformations copie les anciennes données de GPU à GPU. La limite de 65 535
pages borne leur buffer à 128 Mio ; les autres buffers s'ajoutent à ce total.
Le dimensionnement des références couvre les transitions rendant deux LOD à la fois.
Les listes et les calculs sont réutilisés tant que la publication, les modèles,
la matrice de passe et les paramètres de caméra pertinents restent identiques.
La caméra et chaque couche d'ombre disposent de listes distinctes.
Un déplacement de caméra qui conserve la même liste de pages ne renvoie pas
leurs métadonnées. Une passe vide termine sa sélection côté CPU sans requête
d'état OpenGL. Une passe réutilisée sauvegarde uniquement les trois bindings
SSBO nécessaires au dessin ; les autres bindings ne sont touchés que pendant
le compute. Les ombres ne lient pas la texture de normales inutilisée.

Un seul travail CPU charge/décode un modèle et prépare ses mipmaps en arrière-plan.
Le thread GL publie au plus un modèle terminé par préparation. Le cache privilégie
les secteurs proches, respecte le budget mémoire et limite le nombre de modèles
résidents à 64 (`RenderSettings::maximumResidentModels`). Les modèles moins
prioritaires peuvent être évacués ; un modèle qui ne tient pas est reporté sans
boucle de rechargement à chaque image. Il n'y a pas de capture d'imposteur en jeu.
Le chargement initial est progressif ; un upload GL peut néanmoins avoir un coût
ponctuel. Les budgets n'incluent pas les allocations internes du pilote.

Le worker compacte les sommets de 64 à **48 octets** en retirant le seul attribut
de vent inutilisé ; positions, normales, UV et couleurs conservent leur précision.
La recherche de couverture des mipmaps alpha utilise un histogramme de 256
valeurs, au lieu de parcourir tous les pixels à chaque étape de recherche. Le
résultat reste identique octet par octet. Une table précalculée remplace les
conversions sRGB répétées. Les 17 couches d'imposteurs sont regroupées par mip
avant l'upload : deux tableaux de 256 px nécessitent **18 uploads**, contre
306 uploads couche par couche. Ces nombres excluent les textures bois/feuilles.

## Matériaux, imposteurs et ombres

Le bois et les feuilles utilisent les couleurs et masques exportés. Le feuillage
est découpé par alpha à 0,5, avec écriture de profondeur et sans tri transparent.
Les mipmaps préservent la couverture. Les normales sont transformées dans le
monde et les couleurs sont encodées suivant le gamma du G-buffer existant.
Métal = 0, AO matériau = 1 ; roughness 0,90 pour le bois, 0,82 pour les feuilles
et 0,86 pour l'imposteur. L'éclairage différé, l'IBL, le SSAO et l'atmosphère
restent responsables de l'éclairage et de l'intégration dans le paysage.

Le renderer utilise un compute de transformation et deux variantes de culling
(projection perspective/orthographique). Les **huit programmes graphiques fixes**
couvrent bois, feuilles, imposteur perspective et imposteur parallèle, chacun
en G-buffer et en ombres. Seuls trois sont utilisés dans une passe. Il ne compile
aucune variante par biome. `TREE_FOLIAGE`, `TREE_IMPOSTOR`, `TREE_PARALLEL`,
`TREE_SHADOW`, `TREE_ORTHOGRAPHIC` et `TREE_LOD_TRANSITIONS` retirent les choix
stables à la compilation. Les programmes sont préparés à l'initialisation et
réutilisent le cache de binaires ; aucun shader ne compile pendant un draw.
Le shader maillage n'embarque pas les projections et interpolants des 17 vues ;
le bois en passe d'ombre ne lit aucune texture. La visibilité, le LOD, le fondu
et le rejet alpha restent des décisions dynamiques par instance ou pixel.

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
propres à chaque couche comparent exactement les pages sélectionnées, leurs
identités d'allocation, les versions des modèles chargés et la caméra contrôlant
les LOD/fondus. Une publication extérieure à la couche ne l'invalide plus.
Les couches sans arbres ignorent les mouvements de caméra sans effet sur leur
contenu. Cette interrogation reste entièrement CPU, sans lecture GPU ni hash
susceptible de collision. Si les buffers de visibilité échouent à s'allouer,
la couche est signalée non réutilisable et le prochain draw réessaie une fois,
même sans nouvelle publication : une ombre vide ne reste pas figée en cache.
Chaque passe restaure les états GL qu'elle modifie ; SSBO 0 appartient au moteur.

## Mesures exposées

Le panneau affiche les modèles chargés/en attente, les mémoires GPU estimées,
les blocs sélectionnés, les pages de transformations construites/réutilisées et
les temps cumulés de chargement CPU et d'appels d'upload GL. `RenderStats` expose
aussi `transformedInstances`, `transformBuilds`, `cullDispatches`, `drawCalls`,
`indirectCommands`, `selectionUploads` et `distanceSelections`. Les deux derniers
comptent les uploads de listes et les sélections communes par distance.
Ces compteurs sont cumulatifs : prendre leur différence entre
deux instants pour mesurer le travail d'une période. `instances`, `cameraBlocks`
et `shadowBlocks` décrivent le dernier état préparé/rendu, pas un total cumulé.

`workingBytes` additionne les capacités réellement réservées des buffers du
renderer ; `modelBytes` couvre les maillages et textures chargés. Les temps
`assetLoadMilliseconds` et `assetUploadMilliseconds` sont des durées CPU, pas
des temps GPU ni une durée totale de génération. Aucune lecture de compteur GPU
n'est ajoutée au chemin de rendu pour alimenter ces statistiques.

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
champ, l'état OpenGL, les caches et le repli sans imposteur. Ils vérifient aussi
la conservation des transformations lors de la croissance d'un buffer, le
recyclage des pages et le regroupement de plusieurs pages/modèles en commandes.
Les régressions couvrent aussi les trois appels regroupés, les variantes sans
transition/orthographiques, les publications identiques, les invalidations par
couche et une allocation de visibilité volontairement refusée puis rétablie.
`python Tests/Trees/check_core.py` compare les mipmaps optimisées à l'ancienne
méthode sur plusieurs dimensions et seuils alpha, avec et sans fast-math.
Mesa logiciel valide le fonctionnement, pas les performances sur le GPU cible.
