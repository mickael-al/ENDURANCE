# Génération procédurale GPU

La nouvelle génération utilise un champ scalaire `float32`, une seed `uint32_t`, des coordonnées mondiales communes à tous les niveaux de détail et des chunks de 64³ cellules par défaut. Les fichiers `NoiseGeneration_old` et `Asset/Shader/Noise_old` restent disponibles comme référence ; la nouvelle génération ne dépend pas de leur exécution.

## Champ et biomes

`Asset/Shader/ProceduralTerrain/world_field.glsl` définit le monde. Les coordonnées d'un échantillon sont calculées dans une grille entière avant leur conversion en mètres :

```text
world = (origin + (sampleIndex - 1) * 2^lod) * baseSpacing
```

`origin` est exprimé en voxels du niveau le plus fin. La fréquence des bruits ne dépend ni du chunk ni du LOD. Deux chunks, ou deux LOD, qui échantillonnent la même position utilisent donc la même fonction. La seed participe directement au hachage entier des cellules de bruit ; elle n'est pas convertie en décalage flottant.

La génération du champ se déroule en trois passes :

1. `field_classify.C.glsl` calcule la hauteur macro, l'humidité, la température et les fractions locales de matrice pour toutes les colonnes XZ, **halo inclus**. Aucune recette de biome n'est exécutée dans cette passe. Les cellules d'interpolation rencontrées sont réduites avec des `atomicOr` en mémoire partagée, puis dans un masque global de 64 bits.
2. `field_columns.C.glsl` réutilise ces données et calcule le relief 2D, l'épaisseur et le décalage de bande, ainsi que les données locales nécessaires aux recettes 3D. Les occurrences d'un même biome sont regroupées avant d'évaluer sa recette de relief. Les identifiants viennent d'une table compacte de cellules ; les poids bilinéaires restent calculés sur le GPU par colonne.
3. `field_density.C.glsl` appelle les fonctions 3D indépendantes des biomes dont le poids local est positif, puis mélange leurs densités. Les quatre invocations Y d'un groupe partagent les données de colonne et les deux fractions climatiques. Les poids trilinéaires restent locaux et en `float32` ; ils ne sont ni moyennés à l'échelle du chunk ni calculés sur le CPU.

`ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainBiomes.hpp` contient les matrices qui font autorité : `BiomeMatrix2D` (5 × 5) et `BiomeMatrix3D` (5 × 5 × 5). Les axes sont humidité, température et hauteur macro relative au rivage ; le troisième axe n'est pas l'altitude de chaque voxel. Le C++ prépare une table de 64 cellules et l'envoie une fois au GPU. Modifier une matrice demande une recompilation C++, distincte du rechargement à chaud des recettes GLSL.

Chaque fonction `fieldVolume*(vec3 p, FieldColumn column, inout FieldVolumeNoise cache)` peut employer sa propre construction 3D. Les recettes livrées utilisent le helper facultatif `fieldVolumeFromRecipe()` pour les cavernes, feuillets, tubes, sections angulaires et strates. Leur base commune `fieldSharedVolumeNoise()` calcule trois bruits au maximum une fois par voxel, à la première demande, même si plusieurs recettes l'utilisent. Une fonction personnalisée peut employer d'autres bruits, notamment `fieldCellular3()` pour un Voronoï 3D, sans stockage volumique supplémentaire. Elle paie alors ses propres calculs. Les paramètres de `BiomeVolume` ne sont plus interpolés pour générer le champ : les résultats des fonctions sont mélangés, ce qui peut modifier l'apparence des transitions.

Les 25 cases de la matrice climatique disposent chacune d'une recette 2D et d'une recette 3D ; le fond marin ajoute une 26e paire. Leurs identifiants, références naturelles et artistiques, réglages et limites sont décrits dans [BIOMES.md](BIOMES.md). Les recettes combinent bruits de gradient, cellules euclidiennes ou angulaires, dunes orientées, terrasses adoucies et masses monumentales à placement borné. Le climat est remappé pour rendre accessibles les cases extrêmes. Les hachages et les proportions ont été remaniés ; une ancienne seed ne reproduit pas le paysage d'une version précédente des recettes.

Le champ est positif dans le solide et négatif dans le vide ; sa surface est l'isovaleur zéro. Il ne représente pas une distance signée. Une fonction 3D renvoie une densité complète, pas uniquement un bruit ou une amplitude. Hors de sa bande utile, elle doit revenir continûment au champ de base `column.terrain.x - p.y`. L'épaisseur dépend du bruit et des biomes ; son décalage vertical est indépendant. Un décalage négatif peut donc enfouir le volume de grottes sans abaisser la surface principale. Une bande d'épaisseur nulle, ou un biome sans bruit volumique, conserve le relief de hauteur seul.

## Variantes de shaders et retour CPU

En mode terrain automatique, lorsque la spécialisation et son worker de compilation sont disponibles, une fence marque la fin de la classification. Une interrogation sans attente autorise ensuite la lecture CPU des **8 octets du masque de cellules** ; ni cartes climatiques ni fractions locales ne traversent ce chemin. `biomeMasksForCells()` construit les deux masques conservateurs des biomes de surface et de volume. Les coins seuls du chunk ne sont pas utilisés pour déduire sa couverture climatique : chaque colonne nécessaire au maillage a été classifiée.

`biomeDefines()` produit des defines tels que `BIOME_SURFACE_POLAR_ICE` et `BIOME_VOLUME_POLAR_ICE`. Ils retirent les appels aux biomes absents de la variante. Un biome inclus dans le masque peut néanmoins avoir un poids nul à un point donné : les gardes locales restent nécessaires. Les accumulations générique et spécialisée suivent le même ordre d'identifiants et conservent les mêmes poids, sans dépendre du découpage en chunks.

Le cache contient des paires de programmes **colonnes + densité**, indexées par la clé canonique 64 bits des deux masques. Ni poids, ni coordonnées, ni seed ne créent de nouvelle clé. La capacité vaut 32 variantes par défaut ; les entrées admises restent conservées jusqu'à l'arrêt ou la régénération, sans éviction LRU ni recompilation répétée des mêmes masques. Si la clé exacte manque, une variante prête couvrant tous les biomes demandés peut être réutilisée : les biomes supplémentaires ont un poids local nul. Parmi ces variantes, le moteur choisit celle dont le masque contient le moins de bits. Si aucune ne convient et que le cache est plein, le shader générique reste utilisé.

La file accepte au plus deux paires en attente de traitement ou d'adoption par défaut, et au plus une nouvelle paire est soumise par appel de suivi. Un seul worker traite les programmes successivement, en parallèle du thread de rendu ; `maxParallelBiomeCompiles` borne la file, pas le nombre de threads de compilation. Le shader générique déjà lié sert immédiatement lorsqu'une variante manque, compile encore ou a échoué. Les chunks générés avec ce repli n'attendent pas la variante et n'ont pas besoin d'être refaits lorsqu'elle est prête.

`Ge::BackgroundShaderCompiler` possède un thread dédié et utilise le contexte OpenGL partagé fourni par `Ge::SharedShaderContext` dans le moteur GLFW. La compilation, l'édition de liens, les journaux, la recherche et l'initialisation des uniforms ainsi qu'un petit dispatch de préparation s'exécutent dans ce worker. Celui-ci attend la fin de ses commandes avant de publier le programme ; le thread de rendu ne fait alors que récupérer le résultat préparé. Ce chemin ne dépend pas des extensions `ARB/KHR_parallel_shader_compile`. L'ancien service `Ge::ParallelShaderCompiler` reste disponible dans le moteur, mais le terrain ne l'utilise plus. Sans contexte partagé utilisable, la génération conserve les programmes génériques et évite le retour CPU intermédiaire. Un biome forcé possède déjà son masque et évite aussi cette lecture ; sa classification GPU reste utilisée pour préparer ses colonnes.

Les sources GLSL développées et les programmes compilés sont conservés **uniquement en mémoire pendant la vie du générateur**. Toutes les variantes utilisent ainsi le même contenu. **Regenerer avec ces reglages** arrête le worker, détruit les variantes et ce cache, relit les sources et tous leurs includes sur disque, puis reconstruit les programmes. Aucun cache disque de programmes n'est géré par ce système ; rien n'est conservé entre deux régénérations ou sessions. La compilation initiale des programmes génériques et la régénération explicite restent synchrones. L'arrêt peut attendre la fin d'un appel du pilote déjà engagé sur le worker.

Le worker déplace les appels de compilation hors du thread de rendu ; il ne garantit pas l'absence de saccades sur tout pilote. Compilation, préparation et génération continuent de partager les ressources CPU/GPU avec le jeu. Le terrain proche peut déjà être visible alors que des chunks distants ou des variantes sont encore en cours de traitement ; les compteurs de l'inspecteur permettent de distinguer cet état d'une génération réellement inactive.

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
| `detailScale` | 0,35 | Intensité du relief, de l'épaisseur, du décalage et des amplitudes 3D propres aux biomes. |
| `seaLevel` | 0 | Niveau de référence de la sélection marine. |
| `seaBlendWidth` | 64 | Largeur totale de transition marine, centrée sur `seaLevel`. |
| `biomeOverride` | -1 | Matrices automatiques ; 0–24 force une case ; 25 force le fond marin. |
| `mode` | `Terrain` | `Plane` et `Sphere` fournissent des champs analytiques de diagnostic. |
| `sphereCenter`, `sphereRadius` | (0, 0, 0), 48 | Paramètres du mode sphère. |

`fieldHeightBounds()` fournit des bornes globales analytiques pour la sélection des chunks. Elles couvrent le relief et toute la bande déplacée, y compris les ravins. Avec les paramètres par défaut, elles valent [-696 ; 696] mètres. Elles sont volontairement plus larges que les hauteurs réellement atteintes ; il ne faut pas les remplacer par une estimation aux seuls coins d'un chunk. La sélection doit également tenir compte du halo et de la marge d'échantillonnage. `validFieldParameters()` rejette les valeurs non finies, même avec les options de compilation Release `-ffast-math`.

Ces bornes supposent un relief propre au biome borné à [-640 ; 640], une épaisseur supplémentaire à [0 ; 320] et un décalage relatif combiné à [-160 ; 160], avant `detailScale`. Le helper 3D borne la somme du décalage de relief et de recette ; une fonction entièrement personnalisée doit conserver le même domaine fini. Élargir sa bande demande d'adapter ensemble les bornes C++, le rejet anticipé de `fieldDensityAtPrepared()` et la recette. Sans cela, la sélection peut omettre une formation pourtant produite par le shader.

Avant chaque travail, `validFieldDomain()` contrôle également les coordonnées maximales du halo, calculées en double. Le produit coordonnée × fréquence reste inférieur à 2³⁰ pour sécuriser les conversions en cellules entières des bruits. Les paramètres, les bornes et le domaine restent sous 2⁶⁰ afin d'éviter les débordements des intermédiaires flottants, notamment le calcul de distance du mode sphère. Ces contrôles empêchent les valeurs extrêmes d'atteindre les shaders ; ils ne compensent pas la perte progressive de précision des coordonnées `float` loin de l'origine.

## Mémoire et extraction

Chaque chunk de N³ cellules utilise `(N+3)³` échantillons : les N+1 points nécessaires aux cellules et un halo d'un point de chaque côté pour les gradients. Pour N=64, le champ fait 67³ floats, soit 1 203 052 octets. Les 67² colonnes occupent chacune 48 octets, soit 215 472 octets. Elles sont réécrites en place entre classification et relief, sans seconde carte de climat. Cela économise 71 824 octets par travail par rapport aux colonnes de 64 octets de la version précédente. Ces valeurs décrivent seulement le champ et les colonnes : il faut ajouter la table commune de cellules (1 024 octets), les compteurs de 32 octets par travail (16 auparavant), les autres buffers de meshing, les maillages résidents et les commandes de rendu. Les programmes compilés et leur cache consomment une mémoire supplémentaire dépendante du pilote ; la taille totale de ce cache n'est pas mesurée par ces compteurs.

Contrat des passes de champ :

| Buffer | Binding SSBO | Disposition |
| --- | ---: | --- |
| Densité | 0 | `float`, index `x + y*S + z*S*S`, `S=N+3`. |
| Colonnes | 1 | Deux `vec4` et un `uvec4` (48 octets), index `x + z*S`. |
| `terrain` | — | Hauteur, humidité, température, épaisseur totale. |
| `blend` | — | Fraction verticale de matrice, décalage global, décalage de relief, masque de roche. |
| `detail` | — | Bits du float d'activité polaire, identifiants 3D 0–5, identifiants 3D 6–7, réservé. |
| Compteurs | 3 | 32 octets : compteurs de meshing, masque de 64 cellules et réserves. |
| Cellules de matrice | 7 | 64 `uvec4` ; identifiants des quatre coins 2D et huit coins 3D compactés sur 5 bits. |

La table décrit les colonnes après `field_columns.C.glsl`. La classification utilise temporairement `terrain.x` pour la hauteur macro, `blend.xyz` pour les trois fractions locales (`blend.w` vaut zéro) et `detail.w` pour l'identifiant entier de cellule. Seuls les identifiants sont compactés : densités, climat, activité et fractions restent des floats 32 bits. La passe de densité reconstruit les fractions climatiques une fois par colonne partagée, puis calcule les poids des huit coins ; les identifiants répétés sont regroupés avant les appels aux recettes.

Les champs et les sommets restent sur le GPU pendant la génération et le rendu. La lecture de champs denses dans les tests est un outil de vérification ; elle ne fait pas partie du chemin d'exécution du jeu.

`ProceduralTerrainGpu` enchaîne la classification, la sélection éventuelle d'une variante, le relief par colonne, la densité, le comptage des triangles réguliers et des transitions, puis leur émission. Chaque groupe réserve sa plage avec un `atomicAdd` après un scan local. Le comptage régulier partage 225 échantillons entre 128 cellules ; l'émission en partage 539 pour inclure le halo nécessaire aux normales. Le comptage permet de réserver exactement l'espace requis, à l'alignement près, dans un pool de sommets fixe ; aucun buffer du pire cas n'est réservé pour chaque chunk. La seconde passe écrit directement dans ce pool puis finalise la commande de dessin indirecte.

Après l'éventuelle fence de classification, une fence sépare le comptage et l'allocation. Le CPU consulte les compteurs de triangles et d'erreur seulement lorsqu'elle est terminée ; une dernière fence conditionne la publication du maillage. Ces données partagent le buffer mappé de 32 octets avec le masque de cellules. Les vérifications et les mesures de temps GPU ne bloquent pas en attendant la génération. Le budget de sommets par défaut est de 256 MiB, avec deux jeux de buffers temporaires réutilisables. Les colonnes sont précalculées pour chaque travail : il n'existe pas encore de cache de colonnes entre les chunks empilés.

Chaque sommet occupe 12 octets : trois positions locales quantifiées sur 16 bits, deux composantes climatiques sur 8 bits et une normale octaédrique sur deux composantes de 16 bits. La quantification utilise 65534, avec un arrondi explicite `floor(q+0.5)`. Le côté grossier d'une transition est interpolé et quantifié dans le repère du chunk voisin, puis converti en codes entiers du chunk fin. Au rendu, une reconstruction mondiale canonique avec `fma` évite que deux origines de chunks réintroduisent des arrondis différents au même raccord. Le descripteur conserve le LOD et les coordonnées entières du chunk dans `metadata`, avec une limite de représentation exacte de 2²⁴ chunks de base par axe. Cette méthode ne nécessite ni sommets plus grands, ni calcul FP64. Les normales proviennent du gradient du champ, avec un repli défini lorsque ce gradient est nul.

Les transitions utilisent les tables régulières et de transition Transvoxel conservées avec leur licence dans `ThirdParty/transvoxel`. La sélection forme un octree équilibré sur les faces : les voisins diffèrent au maximum d'un niveau, et le chunk fin porte la transition. Le nombre de cellules par chunk reste constant ; seul le pas et donc l'étendue du chunk double à chaque LOD.

## Streaming

`ProceduralGeneration::prepareRenderer()` est appelé une fois par image, après le déplacement de la caméra active et avant les passes de rendu. Le streaming fonctionne donc aussi lorsque les scripts sont suspendus dans l’éditeur. Le composant de scène assure le cycle de vie et la configuration ; son `update()` ne lance pas une seconde génération.

Un worker CPU persistant calcule la sélection et les opérations de remplacement. Il ne fait aucun appel OpenGL. La sélection cible est mise en cache jusqu’à un déplacement de `32 * cellSize` mètres depuis sa dernière évaluation. La caméra peut continuer à se déplacer pendant le calcul : la prochaine demande prend sa position la plus récente, sans accumuler une file de positions anciennes.

`ProceduralTerrainStreaming::planLodTransaction` construit des transactions locales : ajout de quelques chunks absents, subdivision d’un parent, ou regroupement de ses enfants. Les voisins dont le masque Transvoxel change font partie de la même transaction, avec **au plus 32 nouveaux maillages**. La couverture initiale apparaît progressivement. Pour un remplacement, le parent ou les enfants précédents restent affichés jusqu’à ce que tous les nouveaux maillages locaux soient prêts ; la publication les échange ensemble. Les raffinements proches ne sont plus liés aux dégradations de toute la couronne lointaine.

Les priorités GPU sont : remplir les zones absentes du niveau le plus fin au plus grossier ; améliorer les chunks existants, en traitant d’abord les niveaux sources les plus grossiers ; dégrader en dernier, en commençant par les niveaux sources les plus fins. Le niveau le plus précis s’appelle **LOD 0 dans le code**. Les distances départagent les travaux d’un même niveau. Un prérequis nécessaire à l’équilibre 2:1 ou au budget de feuilles peut passer avant l’opération qui en dépend. Les retraits hors de la couverture cible ne demandent pas de génération et sont appliqués immédiatement. Les bits de transition d’une face devenue extérieure peuvent rester en place jusqu’à son prochain voisin : aucun raccord présent ne conserve un masque incompatible.

Une seule petite transaction GPU est engagée à la fois. Elle est menée à terme même si la caméra bouge, puis le worker repart de la couverture réellement publiée et de la dernière position. Les deux slots GPU par défaut restent alimentés par des lots de remplissage allant jusqu’à quatre chunks et par les subdivisions/raccords. Les limites `maxChunksPerFrame` et `maxInFlight` restent effectives. Le débit n’est pas garanti à vitesse de déplacement arbitraire.

L’ancien et le nouveau maillage local coexistent pendant la préparation : il faut une marge de mémoire pour ce remplacement. Une marge de 32 slots au-delà de `maxSelectedChunks` évite un blocage par le seul nombre de slots pendant une transaction complète ; les valeurs par défaut 2048/1024 la fournissent, indépendamment du budget de sommets. Si un budget est dépassé ou si la génération échoue, la couverture publiée reste affichée et l’erreur est exposée dans les statistiques. Une nouvelle tentative a lieu après un déplacement suffisant de la caméra, ou après Stop/Play avec une configuration ajustée. Les limites de hauteur utilisées pour sélectionner les chunks incluent une marge correspondant au niveau de détail le plus large.

## Culling et ombres

Le rendu principal teste les AABB contre les six plans du frustum seulement lorsque la rotation, la projection, la caméra active ou la couverture changent, ou après `8 * cellSize` mètres de translation depuis le dernier calcul. Entre ces événements, la liste filtrée est réutilisée. Le frustum est élargi du pas de déplacement pour conserver tout chunk susceptible d’entrer dans le champ avant le prochain calcul. Les matrices de vue et de projection déjà calculées par la caméra sont composées en double, avec une marge supplémentaire pour les arrondis. Aucun champ GPU n’est relu.

Chaque index d’ombre possède son propre cache, invalidé par un changement de **sa matrice de lumière** ou de couverture. `LightManager::getShadowMatrix(index, matrix)` fournit la même matrice que celle du shader, sans recopier toutes les matrices ni lire le SSBO. Un relief hors du champ principal peut donc rester un occultant ; une cascade qui bouge est recalculée immédiatement.

La caméra et chaque passe d’ombre conservent également leur propre buffer de commandes de dessin indirectes. Une alternance entre ces passes ne réécrit plus le même buffer à chaque image. Une liste de commandes inchangée ne provoque aucun nouvel upload ; les handles périmés sont filtrés avant le dessin. Les timestamps GPU sont lus seulement après confirmation de disponibilité. Les fences sont interrogées sans attente ; un flush est regroupé lorsque de nouvelles commandes ont été soumises, au lieu d’être demandé pour chaque fence. Sans génération active, les sauvegardes de bindings compute et les parcours de sélection sont évités.

La caméra de l’éditeur publie désormais une seule pose finale par image modifiée, ce qui évite plusieurs recalculs des cascades pour rotation et déplacement simultanés. Son accélération avec Shift utilise le temps écoulé. Après un retard de rendu, la boucle ne lance plus une rafale d’images pour rattraper une dette ; la boucle physique à pas fixe reste inchangée. Ces corrections de cadence ne constituent pas une mesure de performances sur le GPU cible.

La vérification du chemin d’ombre a également corrigé le rattachement des framebuffers lorsque la texture de profondeur est recréée, l’état de profondeur des passes, la singularité de la base de vue quand le soleil est vertical et l’angle de projection des faces de lumière ponctuelle. Le PCF compare neuf profondeurs échantillonnées séparément, rejette les coordonnées hors carte et les points derrière la projection, et borne le biais aux incidences rasantes. Le biais reste un réglage de prototype ; les transitions artistiques entre cascades et la résolution des cartes ne sont pas refondues ici.

Les lumières sont allouées avec `new`/`delete`, qui respectent leur alignement C++ : le pool précédent pouvait placer leurs données alignées sur 16 octets à une adresse décalée de 8 octets, provoquant un plantage. Cette allocation intervient à la création/destruction des lumières, pas pour chaque chunk ou chaque image. La lumière est retirée du gestionnaire avant sa destruction afin que les callbacks de mise à jour des ombres ne parcourent pas un objet détruit.

## Utilisation dans le moteur

Ouvrir `ENDURANCE/Scene/Terrain.scene` dans l'éditeur, puis lancer Play. L'objet vide « Procedural terrain » porte le script `ProceduralTerrainComponent`. La scène utilise la seed 1337 et une caméra à (80, 180, 120), au-dessus de la surface de départ. La scène principale et le réglage de démarrage du projet restent inchangés.

Les paramètres du script sont sérialisés à plat dans la scène. Les modifier avant Play ; arrêter puis relancer Play pour appliquer une nouvelle configuration. La sauvegarde conserve les paramètres et la seed, sans enregistrer le maillage généré. Les statistiques de résidence, mémoire et génération sont visibles dans l'inspecteur pendant Play. Le générateur augmente au besoin la distance de visibilité des caméras utilisées pendant son exécution, puis la restaure à l’arrêt si elle n’a pas été modifiée entre-temps. Le sélecteur « Biome a tester » et le bouton « Regenerer avec ces reglages » permettent aussi de reconstruire le terrain pendant Play. Chaque clic relit et recompile les shaders GLSL, y compris les fichiers de biomes inclus, sans redémarrer le jeu. Le bouton reste disponible après une erreur GLSL ; les diagnostics indiquent les fichiers inclus et leurs lignes originales. Les constantes de réglage de la glace polaire sont décrites dans [BIOMES.md](BIOMES.md#réglage-de-la-glace-polaire).

Les contrôles de streaming sont `cellSize`, `maxLod`, `viewDistance`, `lodDistanceFactor`, `maxChunksPerFrame`, `maxInFlight`, `vertexBudgetMiB`, `maxResidentChunks` et `maxSelectedChunks`. `cellSize` désigne le pas d'échantillonnage du LOD le plus fin ; les chunks conservent 64 cellules par axe. La distance souhaitée doit rester compatible avec les budgets de résidence et de génération.

Les options C++ `enableBiomeVariants`, `maxBiomeVariants` et `maxParallelBiomeCompiles`, dans `ProceduralGeneration::Config` puis `GpuConfig`, contrôlent la spécialisation. Leurs défauts sont respectivement `true`, 32 et 2. `enableBiomeVariants` est aussi sérialisé dans le composant et exposé par **« Specialiser les shaders de biomes »** ; son changement s’applique à la prochaine régénération. Les deux limites de cache et de file de compilation restent des options C++ ; augmenter `maxParallelBiomeCompiles` ne crée pas de worker supplémentaire. Pour comparer les chemins générique et spécialisé sur le GPU cible, garder les mêmes seed, paramètres et trajet, puis examiner séparément le premier passage avec compilation et les réutilisations du cache. Régénérer vide ce cache. La spécialisation vise les mêmes densités, sans garantie de gain de débit ou de FPS.

Les compteurs « Camera » indiquent les chunks dessinés et rejetés par le frustum principal ; « Shadow passes » additionne ces nombres sur les passes d’ombre de l’image. « LOD selections », « published groups » et « LOD groups » permettent de vérifier le suivi de caméra et l’avancement des transactions locales. La taille maximale d’une transaction, son temps de planification CPU, les mises à jour du culling et le nombre/octets des uploads de listes de dessin sont également exposés. Les compteurs de culling et d’uploads doivent se stabiliser à l’arrêt une fois la génération terminée (si les lumières sont immobiles).

« Biome shaders » indique les entrées du cache, celles encore en compilation et le mode `background worker` ou `generic fallback`. « Compiler submissions / cache-full fallbacks » compte les paires admises pour compilation et les demandes de nouvelle variante refusées faute de place dans le cache. « Variant uses / fallback uses / compile errors » suit les sélections de variantes exactes ou couvrantes, les replis lors de la spécialisation et ses échecs ; le chemin entièrement générique sans worker ne tente pas de spécialisation. Ces compteurs sont remis à zéro avec le générateur.

« Background terrain work is still active » reste affiché tant que `rebuilding`, `pendingJobs` ou `pendingBiomeVariants` est actif. « Terrain generation and shader compilation are idle » signifie que ces trois indicateurs sont nuls ; il ne décrit pas l'activité des autres systèmes du moteur, notamment des collisions. Un terrain déjà visible peut donc encore avoir du travail en arrière-plan.

« Field scratch » mesure les buffers de densité et de colonnes de tous les travaux simultanés, sans les programmes, maillages ni autres métadonnées. « classification » expose le temps GPU de la classification du dernier travail mesuré ; « Last generation » additionne classification, génération/comptage et émission. Ces temps n'incluent pas le délai entre deux étapes CPU ni le coût de compilation et ne constituent pas une mesure du temps total d'apparition d'un chunk. Les résultats ne sont lus que lorsqu'ils sont disponibles.

`debugLod` colore les niveaux de détail pour examiner les transitions. `microDetailStrength` et `microDetailScale` règlent une perturbation procédurale des normales, filtrée selon l'empreinte du pixel. Le rendu utilise pour l'instant des couleurs de biomes : les textures et leur projection triplanaire définitives ne sont pas ajoutées dans cette étape.

## Collisions locales

`collisionEnabled` active le cache de collisions, actif par défaut, autour de la caméra ; `collisionRadius` vaut 128 mètres par défaut. Le sélecteur conserve le LOD 0 dans cette zone. Les collisions réutilisent uniquement les triangles locaux utiles et ne relisent jamais le champ dense, ni ne recalculent ses bruits sur le CPU.

Cette fonction constitue l'exception aux transferts de géométrie : les sommets compressés sont copiés de façon asynchrone vers une zone de transit GPU, puis lus après signalement de la fence. La construction de la collision est déléguée à un worker CPU, puis publiée via la file de commandes physique. Le cache limite cette activité à une copie par image, deux travaux en attente et 16 MiB par zone de transit. Les chunks proches prêts peuvent alimenter ce cache avant la publication de leur transaction visuelle. La liste des candidats est reconstruite lorsque les maillages changent ; à caméra immobile et sans travail de collision en attente, le cache ne reparcourt pas les candidats à chaque image. Les collisions précédentes restent actives jusqu'à leur remplacement ; une marge de sortie évite leur suppression au bord du rayon. La collision peut donc arriver après l'affichage d'une zone : sa disponibilité doit être prise en compte par le futur contrôleur de personnage.

## Vérifications et limites

Les tests de calcul utilisent directement les shaders GLSL 450 dans un contexte OpenGL sans fenêtre :

```bash
python3 Tests/Terrain/check_gpu.py
```

Ils contrôlent notamment le plan et la sphère analytiques, la répétition d'une seed, les positions communes entre chunks et entre LOD, l'écriture du halo, les paramètres de bande et l'absence de valeurs non finies. Un contexte EGL/OpenGL de calcul est nécessaire ; un environnement sans support adapté est signalé comme indisponible. Les commandes des vérifications CPU, du backend asynchrone et de la rasterisation sont détaillées dans [Tests/Terrain/README.md](../Tests/Terrain/README.md). Les tests GPU ne remplacent pas une mesure de débit et de mémoire sur la carte graphique cible.

Le déterminisme vérifié concerne une même implémentation GPU avec une même seed et les mêmes paramètres. Une égalité binaire entre toutes les familles de cartes et tous les pilotes n'est pas garantie par les calculs flottants GLSL. Le futur multijoueur devra identifier la version du générateur et les paramètres en plus de la seed.

Le champ peut produire des surplombs et des volumes dans sa bande 3D, mais la grille ne peut conserver un détail plus petit que son pas d'échantillonnage. Éloigner le LOD double ce pas : les détails fins disparaissent progressivement. La présente version ne fournit ni érosion, ni édition persistante du terrain, ni végétation, ni synchronisation réseau des modifications. Les microdétails de matériau et les textures définitives restent une étape distincte.
