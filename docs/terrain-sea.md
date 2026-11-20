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

- Le masque de biomes de surface voyage dans un mot réservé du compteur GPU
  existant. Il utilise la fence et la lecture des résultats déjà présentes,
  sans nouvelle attente GPU ni lecture synchrone du climat.
- Un carré représente une instance de **16 octets** : coordonnées entières XZ,
  LOD et padding. Le vertex shader génère ses six sommets. Il n'y a pas de mesh
  ni de matériau alloué par chunk.
- Un seul `glDrawArraysInstanced` dessine la liste non vide. Le clipping matériel
  rejette les triangles hors écran ; aucun nouvel algorithme de culling GPU.
- L'upload n'a lieu qu'à la publication d'une couverture ou au changement des
  paramètres du plan. Le buffer grandit seulement quand sa capacité est dépassée.
- Le décodage des coordonnées reprend le FMA canonique du terrain, afin de
  conserver les frontières communes entre les LOD et aux grandes coordonnées.
- Aucun framebuffer ni texture supplémentaire. Le shader lit le Z linéaire du
  G-buffer opaque existant et réutilise les ombres, l'IBL et l'atmosphère.

Le moteur expose deux phases de callbacks transparents. La mer utilise
`BeforeMaterials`, après l'éclairage opaque et avant les matériaux transparents.
Les anciens callbacks gardent leur phase `AfterMaterials`. Le rendu de la mer
restaure les bindings qu'il modifie, le culling et l'écriture de profondeur. Le
composant retire son callback avant de détruire ses ressources.

## Où modifier le rendu

- `ENDURANCE/Ressources/Shader/Water/sea.fs.glsl` : variante de
  `Asset/Shader/water_transparency.fs.glsl`. Couleurs, transparence, profondeur de
  coloration, écume, fréquence et force des normales sont des constantes dans
  `main()`. Les couleurs sont en espace linéaire HDR.
- `ENDURANCE/Ressources/Shader/Water/sea.vs.glsl` : placement du plan et décodage
  des instances. Aucun déplacement vertical des sommets.
- `ANTAGONIZ/Source/ProjectUser/EnduranceSea.cpp` : ressources, uploads et dessin.
- `ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainSea.hpp` : format des tuiles et
  règle d'appartenance verticale.
- `ANTAGONIZ/Source/Miscellaneous/ProceduralGeneration.cpp` : liste publiée avec
  les transactions de LOD.

Les normales sont animées en coordonnées monde, avec dérivée analytique du bruit
et atténuation des détails sous-pixel. Le motif reste continu entre les tuiles.
L'écume n'est évaluée que près des intersections opaques.

Cette première version fournit une surface plane, visible des deux côtés. Elle
n'ajoute ni réfraction de l'image, ni simulation de vagues, ni brouillard
sous-marin. Le tri général des objets transparents immergés reste celui du moteur.
Les tests logiciels valident le fonctionnement, pas le coût sur la carte du joueur.

## Validation visuelle dans le projet

1. Ouvrir la scène terrain, choisir **Fond marin**, puis régénérer. Le compteur
   `Mer : ... tuiles` doit devenir non nul et la surface être au `seaLevel` choisi.
2. Passer en automatique, traverser les limites de LOD et s'éloigner/revenir.
   Vérifier l'absence de doubles surfaces et de disparition pendant un remplacement.
3. Regarder une intersection avec le terrain, puis tourner la caméra vers
   l'horizon et passer sous le plan. Contrôler les reflets, l'écume et l'atmosphère.
4. Régénérer, arrêter et relancer la scène : le callback et les ressources ne
   doivent pas s'accumuler. La modification du shader est relue à la régénération.

Les tests automatisés et leurs commandes sont décrits dans `Tests/Sea/README.md`.
