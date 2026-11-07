# Génération procédurale GPU

La nouvelle génération utilise un champ scalaire `float32`, une seed `uint32_t`, des coordonnées mondiales communes à tous les niveaux de détail et des chunks de 64³ cellules par défaut. Les fichiers `NoiseGeneration_old` et `Asset/Shader/Noise_old` restent disponibles comme référence ; la nouvelle génération ne dépend pas de leur exécution.

## Champ et biomes

`Asset/Shader/ProceduralTerrain/world_field.glsl` définit le monde. Les coordonnées d'un échantillon sont calculées dans une grille entière avant leur conversion en mètres :

```text
world = (origin + (sampleIndex - 1) * 2^lod) * baseSpacing
```

`origin` est exprimé en voxels du niveau le plus fin. La fréquence des bruits ne dépend ni du chunk ni du LOD. Deux chunks, ou deux LOD, qui échantillonnent la même position utilisent donc la même fonction. La seed participe directement au hachage entier des cellules de bruit ; elle n'est pas convertie en décalage flottant.

La génération se déroule en deux passes :

1. `field_columns.C.glsl` calcule une fois par colonne XZ la hauteur, l'humidité, la température, l'épaisseur et le décalage de la bande 3D, ainsi que les coefficients de ses bruits. La matrice 2D humidité/température mélange les recettes de relief. Les occurrences d'un même biome sont regroupées avant d'évaluer sa recette.
2. `field_density.C.glsl` produit le champ volumique. La matrice 3D humidité/température/hauteur du terrain choisit les coefficients de deux bruits 3D communs, précalculés par colonne. Cela conserve un mélange spatial continu sans réévaluer huit fonctions coûteuses par voxel. Les quatre invocations Y d'un groupe partagent les données de colonne en mémoire partagée.

Les recettes reprennent les principes du générateur précédent : bruits de gradient de type Perlin, masses cellulaires Voronoi, plateaux, terrasses adoucies, ravins et bandes 3D propres aux biomes. Les hachages et les proportions ont été remaniés ; une ancienne seed ne reproduit pas l'ancien paysage. Six recettes de prototype sont définies dans les matrices : roche, désert sec, forêt tempérée, terres humides, ravin et relief froid. Ce sont des règles de relief, pas une implémentation de végétation ou de matériaux définitifs.

Le champ est positif dans le solide et négatif dans le vide ; sa surface est l'isovaleur zéro. Il ne représente pas une distance signée. Hors de la bande utile, la densité revient à la hauteur de surface et aucun bruit 3D n'est calculé. L'épaisseur dépend du bruit et des biomes ; son décalage vertical est indépendant. Un décalage négatif peut donc enfouir le volume de grottes sans abaisser la surface principale. Une bande d'épaisseur nulle, ou un biome sans bruit volumique, conserve le relief de hauteur seul.

## Paramètres

Le contrat C++ se trouve dans `ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainField.hpp`. Les distances sont en mètres, les fréquences en cycles par mètre. Les valeurs ci-dessous décrivent le prototype et peuvent être réglées sans modifier la structure du pipeline.

| Paramètre | Défaut | Effet |
| --- | ---: | --- |
| `baseHeight` | 0 | Niveau moyen du terrain. |
| `heightAmplitude` | 320 | Amplitude du relief à grande échelle. |
| `heightFrequency` | 0,00015 | Fréquence du relief à grande échelle. |
| `climateFrequency` | 0,0005 | Taille des régions d'humidité et de température. |
| `bandThickness` | 64 | Épaisseur de base de la zone influencée par la génération 3D. |
| `bandVariation` | 16 | Variation positive d'épaisseur pilotée par le bruit. |
| `bandOffset` | 0 | Décalage vertical de la zone 3D ; négatif pour l'enfouir. |
| `bandOffsetVariation` | 0 | Amplitude de la variation signée du décalage. |
| `detailScale` | 0,35 | Intensité du relief et de l'épaisseur supplémentaires propres aux biomes. |
| `mode` | `Terrain` | `Plane` et `Sphere` fournissent des champs analytiques de diagnostic. |
| `sphereCenter`, `sphereRadius` | (0, 0, 0), 48 | Paramètres du mode sphère. |

`fieldHeightBounds()` fournit des bornes globales analytiques pour la sélection des chunks. Elles couvrent le relief et toute la bande déplacée, y compris les ravins. Avec les paramètres par défaut, elles valent environ [-797,5 ; 479] mètres. Elles sont volontairement plus larges que les hauteurs réellement atteintes ; il ne faut pas les remplacer par une estimation aux seuls coins d'un chunk. La sélection doit également tenir compte du halo et de la marge d'échantillonnage. `validFieldParameters()` rejette les valeurs non finies, même avec les options de compilation Release `-ffast-math`.

Avant chaque travail, `validFieldDomain()` contrôle également les coordonnées maximales du halo, calculées en double. Le produit coordonnée × fréquence reste inférieur à 2³⁰ pour sécuriser les conversions en cellules entières des bruits. Les paramètres, les bornes et le domaine restent sous 2⁶⁰ afin d'éviter les débordements des intermédiaires flottants, notamment le calcul de distance du mode sphère. Ces contrôles empêchent les valeurs extrêmes d'atteindre les shaders ; ils ne compensent pas la perte progressive de précision des coordonnées `float` loin de l'origine.

## Mémoire et extraction

Chaque chunk de N³ cellules utilise `(N+3)³` échantillons : les N+1 points nécessaires aux cellules et un halo d'un point de chaque côté pour les gradients. Pour N=64, le champ fait 67³ floats, soit 1 203 052 octets. Les 67² colonnes contiennent chacune deux `vec4`, soit 143 648 octets. Ces valeurs décrivent les données de génération, pas le budget total du terrain : il faut ajouter les buffers de classification, les maillages résidents et les commandes de rendu.

Contrat des passes de champ :

| Buffer | Binding SSBO | Disposition |
| --- | ---: | --- |
| Densité | 0 | `float`, index `x + y*S + z*S*S`, `S=N+3`. |
| Colonnes | 1 | Deux `vec4`, index `x + z*S`. |
| `terrain` | — | Hauteur, humidité, température, épaisseur totale. |
| `volume` | — | Décalage, poids du bruit 3D fin, poids du bruit 3D large, transition. |

Les champs et les sommets restent sur le GPU pendant la génération et le rendu. La lecture de champs denses dans les tests est un outil de vérification ; elle ne fait pas partie du chemin d'exécution du jeu.

`ProceduralTerrainGpu` enchaîne le précalcul des colonnes, la densité, le comptage des triangles réguliers et des transitions, puis leur émission. Chaque groupe réserve sa plage avec un `atomicAdd` après un scan local. Le comptage régulier partage 225 échantillons entre 128 cellules ; l'émission en partage 539 pour inclure le halo nécessaire aux normales. Le comptage permet de réserver exactement l'espace requis, à l'alignement près, dans un pool de sommets fixe ; aucun buffer du pire cas n'est réservé pour chaque chunk. La seconde passe écrit directement dans ce pool puis finalise la commande de dessin indirecte.

Une fence sépare le comptage et l'allocation. Le CPU consulte seulement les 16 octets de compteurs lorsque cette fence est terminée ; une seconde fence conditionne la publication du maillage. Les vérifications et les mesures de temps GPU ne bloquent pas en attendant la génération. Le budget de sommets par défaut est de 256 MiB, avec deux jeux de buffers temporaires réutilisables. Les colonnes sont précalculées pour chaque travail : il n'existe pas encore de cache de colonnes entre les chunks empilés.

Chaque sommet occupe 12 octets : trois positions locales quantifiées sur 16 bits, deux composantes climatiques sur 8 bits et une normale octaédrique sur deux composantes de 16 bits. La quantification utilise 65534, avec un arrondi explicite `floor(q+0.5)`. Le côté grossier d'une transition est interpolé et quantifié dans le repère du chunk voisin, puis converti en codes entiers du chunk fin. Au rendu, une reconstruction mondiale canonique avec `fma` évite que deux origines de chunks réintroduisent des arrondis différents au même raccord. Le descripteur conserve le LOD et les coordonnées entières du chunk dans `metadata`, avec une limite de représentation exacte de 2²⁴ chunks de base par axe. Cette méthode ne nécessite ni sommets plus grands, ni calcul FP64. Les normales proviennent du gradient du champ, avec un repli défini lorsque ce gradient est nul.

Les transitions utilisent les tables régulières et de transition Transvoxel conservées avec leur licence dans `ThirdParty/transvoxel`. La sélection forme un octree équilibré sur les faces : les voisins diffèrent au maximum d'un niveau, et le chunk fin porte la transition. Le nombre de cellules par chunk reste constant ; seul le pas et donc l'étendue du chunk double à chaque LOD.

## Streaming

`ProceduralGeneration` prépare une couverture complète correspondant à la caméra. Lors d'un déplacement, l'ancienne couverture reste visible jusqu'à ce que tous les nouveaux maillages requis soient prêts ; le remplacement est ensuite effectué d'un bloc. La première couverture attend également sa génération complète. Cette politique évite de publier des faces LOD dont le voisin n'est pas encore prêt.

L'ancien et le nouveau jeu de chunks peuvent coexister pendant la préparation : le budget mémoire doit absorber cette période. Si un budget est dépassé ou si la génération échoue, la couverture précédente reste affichée et l'erreur est exposée dans les statistiques. Une nouvelle tentative a lieu après un déplacement suffisant de la caméra, ou après Stop/Play avec une configuration ajustée. Les limites de hauteur utilisées pour sélectionner les chunks incluent une marge correspondant au niveau de détail le plus large.

## Utilisation dans le moteur

Ouvrir `ENDURANCE/Scene/Terrain.scene` dans l'éditeur, puis lancer Play. L'objet vide « Procedural terrain » porte le script `ProceduralTerrainComponent`. La scène utilise la seed 1337 et une caméra à (80, 180, 120), au-dessus de la surface de départ. La scène principale et le réglage de démarrage du projet restent inchangés.

Les paramètres du script sont sérialisés à plat dans la scène. Les modifier avant Play ; arrêter puis relancer Play pour appliquer une nouvelle configuration. La sauvegarde conserve les paramètres et la seed, sans enregistrer le maillage généré. Les statistiques de résidence, mémoire et génération sont visibles dans l'inspecteur pendant Play. Le composant augmente la distance de visibilité de la caméra pendant son exécution puis la restaure à l'arrêt.

Les contrôles de streaming sont `cellSize`, `maxLod`, `viewDistance`, `lodDistanceFactor`, `maxChunksPerFrame`, `maxInFlight`, `vertexBudgetMiB`, `maxResidentChunks` et `maxSelectedChunks`. `cellSize` désigne le pas d'échantillonnage du LOD le plus fin ; les chunks conservent 64 cellules par axe. La distance souhaitée doit rester compatible avec les budgets de résidence et de génération.

`debugLod` colore les niveaux de détail pour examiner les transitions. `microDetailStrength` et `microDetailScale` règlent une perturbation procédurale des normales, filtrée selon l'empreinte du pixel. Le rendu utilise pour l'instant des couleurs de biomes : les textures et leur projection triplanaire définitives ne sont pas ajoutées dans cette étape.

## Collisions locales

`collisionEnabled` active le cache de collisions, actif par défaut, autour de la caméra ; `collisionRadius` vaut 128 mètres par défaut. Le sélecteur conserve le LOD 0 dans cette zone. Les collisions réutilisent uniquement les triangles locaux utiles et ne relisent jamais le champ dense, ni ne recalculent ses bruits sur le CPU.

Cette fonction constitue l'exception aux transferts de géométrie : les sommets compressés sont copiés de façon asynchrone vers une zone de transit GPU, puis lus après signalement de la fence. La construction de la collision est déléguée à un worker CPU, puis publiée via la file de commandes physique. Le cache limite cette activité à une copie par image, deux travaux en attente et 16 MiB par zone de transit. Les chunks proches prêts peuvent alimenter ce cache avant la publication de la couverture visuelle complète. Les collisions précédentes restent actives jusqu'à leur remplacement ; une marge de sortie évite leur suppression au bord du rayon. La collision peut donc arriver après l'affichage d'une zone : sa disponibilité doit être prise en compte par le futur contrôleur de personnage.

## Vérifications et limites

Les tests de calcul utilisent directement les shaders GLSL 450 dans un contexte OpenGL sans fenêtre :

```bash
python3 Tests/Terrain/check_gpu.py
```

Ils contrôlent notamment le plan et la sphère analytiques, la répétition d'une seed, les positions communes entre chunks et entre LOD, l'écriture du halo, les paramètres de bande et l'absence de valeurs non finies. Un contexte EGL/OpenGL de calcul est nécessaire ; un environnement sans support adapté est signalé comme indisponible. Les commandes des vérifications CPU, du backend asynchrone et de la rasterisation sont détaillées dans [Tests/Terrain/README.md](../Tests/Terrain/README.md). Les tests GPU ne remplacent pas une mesure de débit et de mémoire sur la carte graphique cible.

Le déterminisme vérifié concerne une même implémentation GPU avec une même seed et les mêmes paramètres. Une égalité binaire entre toutes les familles de cartes et tous les pilotes n'est pas garantie par les calculs flottants GLSL. Le futur multijoueur devra identifier la version du générateur et les paramètres en plus de la seed.

Le champ peut produire des surplombs et des volumes dans sa bande 3D, mais la grille ne peut conserver un détail plus petit que son pas d'échantillonnage. Éloigner le LOD double ce pas : les détails fins disparaissent progressivement. La présente version ne fournit ni érosion, ni édition persistante du terrain, ni végétation, ni synchronisation réseau des modifications. Les microdétails de matériau et les textures définitives restent une étape distincte.
