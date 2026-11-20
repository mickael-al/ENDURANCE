# Plan marin ENDURANCE

## Règle de couverture

Le composant `ProceduralTerrainComponent` installe automatiquement la mer. Le
niveau est le paramètre existant `seaLevel` ; un changement s'applique avec
**Regenerer avec ces reglages**.

Chaque feuille publiée de l'octree dont les colonnes contiennent le biome
`Marine` (ID 25) fournit un carré entier à ce niveau. Seule la tranche verticale
`[minimumY, maximumY[` contenant la mer possède le carré. Cela évite de dessiner
plusieurs plans superposés pour les chunks empilés. Les feuilles sans triangles
de terrain restent éligibles : le fond marin peut être plusieurs chunks plus bas.
Les bornes du planner incluent le niveau marin, même hors de la bande du terrain.

Il n'y a aucun découpage côtier, masque de transition ou calcul de biome par
fragment. La présence marine utilise les échantillons de classification du chunk,
frontières incluses, halo exclu. Le test de profondeur masque l'eau derrière le
terrain opaque. Une partie basse terrestre incluse dans un carré marin peut donc
être recouverte d'eau : c'est le comportement demandé.

Les carrés sont reconstruits avec la **couverture publiée**, jamais avec une
sélection encore en génération. Un remplacement parent/enfants, une fusion et les
voisins de transition sont publiés ensemble. Un échec de génération conserve la
couverture précédente. Un échec d'upload de l'eau suspend son dessin pour cette
révision plutôt que de conserver des carrés devenus incompatibles.

## Coût et intégration

- Un indicateur `hasMarineSurface` voyage dans un mot réservé du compteur GPU
  existant. Il utilise la fence et la lecture des résultats déjà présentes,
  sans nouvelle attente GPU ni lecture synchrone du climat.
- La présence marine utilise le mélange d'altitude déjà calculé. Le calcul
  complet des poids de tous les biomes a été supprimé pour ce besoin ; le bit
  marin partage la réduction existante des signes de densité. Le cas particulier
  d'une matrice personnalisée contenant Marine conserve une vérification exacte.
- Un carré représente une instance de **16 octets** : coordonnées entières XZ,
  LOD et padding. Le vertex shader génère six sommets pour **deux triangles**,
  sans grille subdivisée, buffer d'indices ni déplacement géométrique.
  Il n'y a pas de mesh ni de matériau alloué par chunk.
- Un seul `glDrawArraysInstanced` dessine la liste non vide. Le clipping matériel
  rejette les triangles hors écran ; aucun nouvel algorithme de culling GPU.
- L'upload n'a lieu qu'à la publication d'une couverture ou au changement des
  paramètres du plan. Le buffer grandit seulement quand sa capacité est dépassée.
  Son stockage est renouvelé avant chaque upload pour permettre au pilote de
  conserver la version encore utilisée par un dessin précédent. Aucun transfert
  ni renouvellement lorsque la révision reste identique ; aucune fence ajoutée.
- Le décodage des coordonnées reprend le FMA canonique du terrain, afin de
  conserver les frontières communes entre les LOD et aux grandes coordonnées.
- Aucun framebuffer ni texture supplémentaire. Le shader lit le Z linéaire du
  G-buffer opaque existant et réutilise les ombres, l'IBL et l'atmosphère.
- Le test de profondeur est demandé avant le fragment. Le Z de vue est interpolé
  depuis les sommets, et les lumières directionnelles évitent les calculs
  d'atténuation des lumières ponctuelles.

Les carrés seuls coûtent peu, mais leur shader reste exécuté sur les pixels
visibles de l'eau. PBR, ombres et atmosphère partagent le GPU avec la génération.
La génération peut donc prendre plus de temps réel sans augmentation comparable
du coût de ses compute shaders, notamment avec un budget de soumissions par image.
Le bouton **Afficher la mer** coupe uniquement le rendu de l'eau, sans régénérer,
pour aider à distinguer ces coûts. Comparer la même scène, caméra, seed et caches ;
observer aussi les mesures GPU de génération existantes. Aucun gain de FPS sur
le GPU Windows n'est déduit des tests logiciels.

Le test isolé de `PrepareVolume` sur llvmpipe mesure des ratios après/avant
appariés de 0,985 sur terre et 0,967 en zone mixte. Sur terre, l'intervalle
interquartile recouvre 1 : ce résultat ne démontre pas de gain net. Ces mesures
portent uniquement sur cette petite étape et ne suffisent pas à expliquer un
ralentissement important du chargement. Script, paramètres et résultats sont
conservés dans `Tests/Sea/benchmark_metadata.py` et
`docs/terrain-sea-prepare-benchmark.json`.

Le moteur expose deux phases de callbacks transparents. La mer utilise
`BeforeMaterials`, après l'éclairage opaque et avant les matériaux transparents.
Les anciens callbacks gardent leur phase `AfterMaterials`. Le rendu de la mer
restaure les bindings qu'il modifie, le culling et l'écriture de profondeur. Le
composant retire son callback avant de détruire ses ressources.

## Où modifier le rendu

- `ENDURANCE/Ressources/Shader/Water/sea.fs.glsl` : variante de
  `Asset/Shader/water_transparency.fs.glsl`. Couleurs, transparence, profondeur de
  coloration et écume sont des constantes dans
  `main()`. Les couleurs sont en espace linéaire HDR.
- La transition colorée atteint maintenant sa couleur profonde à **36 m** de
  séparation en Z de vue, contre 12 m auparavant. `shallowColor` devient cyan
  `(0.025, 0.24, 0.27)` ; `deepColor` reste `(0.006, 0.025, 0.045)`.
  Ce réglage ne change pas la précision ni la portée du depth buffer.
- `ENDURANCE/Ressources/Shader/Water/sea.vs.glsl` : placement du plan et décodage
  des instances. Tous les sommets restent exactement à `seaLevel`.
- `ENDURANCE/Ressources/Shader/Water/sea_noise.glsl` : bruit et fonction
  `seaRippleSlope` pour les normales uniquement. `frequency = 0.125` définit
  l'échelle du bruit, `heightAmplitude = 0.40` la hauteur virtuelle utilisée par
  l'éclairage, et `velocity = (0.56, 0.28)` son animation avec `ubd.u_time`.
  La pente tient compte de l'amplitude et de la fréquence : coefficient **0.05**,
  contre 0.22 précédemment, soit des pentes **4,4 fois plus faibles** avant
  filtrage. Augmenter/diminuer `heightAmplitude` renforce/adoucit les normales
  sans déplacer le moindre sommet.
- `ANTAGONIZ/Source/ProjectUser/EnduranceSea.cpp` : ressources, uploads et dessin.
- `ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainSea.hpp` : format des tuiles et
  règle d'appartenance verticale.
- `ANTAGONIZ/Source/Miscellaneous/ProceduralGeneration.cpp` : liste publiée avec
  les transactions de LOD.

Les normales sont animées en coordonnées monde, avec dérivée analytique du bruit
et atténuation des détails sous-pixel. Le motif reste continu entre les tuiles.
L'écume n'est évaluée que près des intersections opaques.

La surface étant plane, les frontières des tuiles restent des segments droits,
quel que soit leur LOD. Aucun masque de raccord ni calcul de bruit n'est nécessaire
dans le vertex shader. L'animation des normales demeure continue en coordonnées
monde et s'atténue quand ses détails deviennent plus petits qu'un pixel.

La surface reste visible des deux côtés. Elle n'ajoute ni réfraction de l'image
ni brouillard sous-marin. Le tri général des objets transparents immergés reste celui du moteur.
Les tests logiciels valident le fonctionnement, pas le coût sur la carte du joueur.

## Validation visuelle dans le projet

1. Ouvrir la scène terrain, choisir **Fond marin**, puis régénérer. Le compteur
   `Mer : ... tuiles` doit devenir non nul et la surface rester au `seaLevel` choisi.
2. Passer en automatique, traverser les limites de LOD et s'éloigner/revenir.
   Vérifier l'absence de doubles surfaces, de fentes et de disparition pendant un remplacement.
3. Regarder une intersection avec le terrain, puis tourner la caméra vers
   l'horizon et passer sous le plan. Contrôler les reflets, l'écume et l'atmosphère.
4. Régénérer, arrêter et relancer la scène : le callback et les ressources ne
   doivent pas s'accumuler. La modification du shader est relue à la régénération.

Les tests automatisés et leurs commandes sont décrits dans `Tests/Sea/README.md`.
