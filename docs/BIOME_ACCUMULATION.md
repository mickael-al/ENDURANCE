# Terrain : accumulation par biome

Le terrain possède un ensemble fixe de **61 programmes compute** : **26 passes de relief 2D, 26 passes de volume 3D et 9 programmes communs**. Chaque biome possède ses deux fonctions modifiables. Le nombre de programmes dépend du catalogue de biomes, jamais des combinaisons rencontrées dans les chunks.

Les programmes sont préparés à l’initialisation et conservés pendant le streaming. Le cache binaire évite de les recompiler lors des chargements suivants tant que leurs sources, leurs defines et le pilote restent compatibles. Une première compilation, une modification du code ou un changement de pilote peut donc encore demander du travail de compilation.

## Modifier un biome

Les recettes se trouvent dans [`Asset/Shader/ProceduralTerrain/biomes`](../Asset/Shader/ProceduralTerrain/biomes), avec un dossier par biome. Par exemple :

| Fichier de `biomes/PolarIce/` | Rôle |
| --- | --- |
| [`relief.C.glsl`](../Asset/Shader/ProceduralTerrain/biomes/PolarIce/relief.C.glsl) | Fonction `fieldRelief(vec2 p)` : hauteur et contrôles de bande 3D, avec son point d’entrée compute. |
| [`volume.C.glsl`](../Asset/Shader/ProceduralTerrain/biomes/PolarIce/volume.C.glsl) | Fonction `fieldVolume(vec3 p, FieldColumn column)` : contribution volumique, avec son point d’entrée compute. |

Chaque dossier contient seulement ces deux fichiers. Pour ajuster les formations, modifier directement la fonction ou les constantes dans le `.C.glsl` correspondant. Le suffixe `.C.glsl` désigne un compute shader ; la recette et son point d’entrée sont réunis dans ce même fichier.

Tous les biomes utilisent les mêmes noms `fieldRelief` et `fieldVolume`. Chaque fichier est compilé dans son propre programme, ce qui évite toute collision entre ces fonctions. Le dossier identifie le biome et `FIELD_BIOME_ID` relie sa passe aux poids et à la liste de chunks correspondants. Les noyaux communs `field_surface_accumulate.glsl` et `field_volume_accumulate.glsl`, inclus à la fin du fichier, contiennent `main()` et appellent directement ces fonctions. Chaque programme ne charge que ce biome et ses dépendances communes ; les autres recettes n’entrent pas dans sa clé de cache.

Pendant Play, sélectionner le biome voulu dans **« Biome a tester »**, puis cliquer sur **« Regenerer avec ces reglages »**. La régénération relit les sources et applique les nouveaux paramètres. Une erreur GLSL est signalée avec les fichiers et lignes concernés ; corriger la source puis régénérer. Les fichiers binaires générés ne remplacent jamais une source devenue invalide.

Les identifiants et noms des 26 dossiers sont déclarés dans [`ProceduralTerrainBiomes.hpp`](../ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainBiomes.hpp). `BiomeMatrix2D` et `BiomeMatrix3D` restent les matrices qui font autorité. Leur modification demande une recompilation C++ et un nouvel envoi de la table de mapping ; une recette GLSL peut être modifiée sans changer ces matrices.

## Contrat de la fonction 2D

`fieldRelief(p)` reçoit la position XZ en mètres et renvoie un `vec4` :

| Composante | Signification | Bornes avant `detailScale` |
| --- | --- | --- |
| `x` | Hauteur propre au biome, ajoutée à la hauteur macro. | −640 à +640 m |
| `y` | Épaisseur **totale** de la bande 3D. Zéro ferme cette bande. | 0 à 576 m |
| `z` | Décalage de la bande utilisé par la recette volumique. | −160 à +160 m |
| `w` | Contrôle supplémentaire dont chaque fonction 3D définit le sens. | 0 à 1 |

Chaque résultat est borné par `fieldClampRelief()`, puis ajouté aux quatre accumulateurs avec son poids local. La finalisation applique `detailScale` à la hauteur et à l’épaisseur ; le décalage est mis à l’échelle lors de la préparation volumique. La hauteur macro et le décalage global sont ajoutés une seule fois.

L’épaisseur provient entièrement des recettes 2D. Aucun minimum global ne transforme un `.y` nul en bande active. Une recette peut donc ouvrir ou fermer localement sa 3D avec un bruit 2D. La décision d’ouverture utilise l’épaisseur FP32 finale : la bande est fermée jusqu’à `0.0002` compris. Cette décision est conservée explicitement lors du compactage.

Le `.w` est transmis à la fonction 3D dans `column.blend.w`. Le noyau d’accumulation et le helper volumique ne lui donnent aucune signification automatique. Les recettes de départ choisissent explicitement d’atténuer leurs amplitudes lorsque ce contrôle baisse. Une nouvelle fonction peut l’utiliser pour placer des rochers, ouvrir des cavités, choisir une densité locale ou ne pas l’utiliser. Un `.w` nul ne ferme donc la géométrie que si la fonction du biome le décide.

Les primitives partagées se trouvent dans [`biome_relief_common.glsl`](../Asset/Shader/ProceduralTerrain/biome_relief_common.glsl) et [`Noise/noise_2d.glsl`](../Asset/Shader/Noise/noise_2d.glsl). Les constantes et calculs propres à un biome restent dans son dossier. Le catalogue 2D/3D, les compositions et les transformations scalaires sont décrits dans [NOISE_LIBRARY.md](NOISE_LIBRARY.md). Les fonctions de bruit prennent une seed explicite ; une recette transmet `uSeed ^ salt` pour conserver son motif.

## Contrat de la fonction 3D : renvoyer une contribution

La densité est positive dans le solide, négative dans le vide ; l’isovaleur zéro définit la surface. La fonction 3D d’un biome renvoie une **modification de densité**, et reçoit la colonne après la finalisation du relief :

```glsl
float fieldVolume(vec3 p, FieldColumn column)
{
    vec4 volume=FIELD_VOLUME;
    volume.yz*=uDetailScale;
    // Politique choisie par CE biome, remplaçable dans cette fonction.
    volume.y*=mix(0.55,1.0,column.blend.w);
    volume.z*=mix(0.85,1.0,column.blend.w);
    return fieldVolumeRecipeDelta(p,column,volume,FIELD_SHAPE,FIELD_STYLE);
}
```

Les trois constantes fournissent une recette de départ reprenant les paramètres du biome. Elles sont propres à cette fonction ; elles ne sont plus interpolées avec les paramètres d’autres biomes. Le corps de la fonction peut être remplacé pour créer des grottes, pierres, arches, strates ou autres formes spécifiques. Dans l’exemple, les amplitudes `volume.yz` sont déjà préparées en mètres mondiaux avant l’appel au helper ; celui-ci ne réapplique ni `detailScale` ni le contrôle `.w` à ces amplitudes.

La passe commune initialise le champ avec `column.terrain.x - p.y`, puis chaque biome actif ajoute :

```glsl
density[p] += localWeight * fieldVolume(worldPosition, column);
```

Si une construction produit une densité complète `sculpted`, la contribution correspondante est `sculpted - base`, où `base = column.terrain.x - p.y`. Par exemple, avec la convention « positif = solide », `max(base, rockDensity)` réunit un rocher au terrain ; la différence avec `base` est alors l’ajout à renvoyer. Une cavité peut utiliser `min(base, cavityDensity)` avant cette même soustraction. La recette doit aussi respecter la bande décrite ci-dessous et s’éteindre progressivement à sa limite.

Il ne faut ni multiplier à nouveau par le poids du biome, déjà appliqué par le noyau, ni renvoyer la hauteur de base une seconde fois. Une contribution nulle laisse le champ existant intact.

### Colonne disponible pour la recette

| Donnée | Contenu après finalisation |
| --- | --- |
| `column.terrain.x` | Hauteur finale après mélange du relief 2D, en mètres. |
| `column.terrain.yz` | Humidité et température. |
| `column.terrain.w` | Épaisseur totale, déjà multipliée par `detailScale`. |
| `column.blend.x` | Fraction locale du troisième axe de la matrice. |
| `column.blend.y` | Décalage global, variation éventuelle comprise, en mètres. |
| `column.blend.z` | Décalage de relief avant mise à l’échelle. |
| `column.blend.w` | Contrôle supplémentaire mélangé, décodé en float depuis un UNORM16. |

Les identifiants de biomes restent compactés dans `column.detail`. Le masque de candidats est calculé par la préparation GPU ; `detail.w` est réservé dans l’interface décodée. Une recette artistique ordinaire n’a pas besoin de ces métadonnées.

### Stockage compact et précision

L’interface `FieldColumn` de la recette reste une structure de floats et d’identifiants. L’accumulation 2D utilise un scratch de **48 octets** en FP32. Après toutes les contributions, `field_batch_finalize.C.glsl` écrit une seule fois le format `PackedFieldColumn` de **32 octets**, défini dans [`field_storage.glsl`](../Asset/Shader/ProceduralTerrain/field_storage.glsl).

| Données persistantes | Stockage |
| --- | ---: |
| Hauteur, humidité, température, fraction d’altitude, décalage global | 5 floats FP32, 20 octets |
| Épaisseur avant `detailScale`, décalage relatif | UNORM16 sur `[0,576]`, SNORM16 sur `[-160,160]`, 4 octets |
| Huit IDs 3D, contrôle `.w`, cellule de matrice et état de bande | 40 + 16 + 6 + 1 bits dans 8 octets |

La hauteur, le climat et les fractions de poids ne sont pas quantifiés. L’épaisseur a un pas de `576/65535`, le décalage de `160/32767` et `.w` de `1/65535`. L’erreur d’arrondi ordinaire est d’un demi-pas ; à `detailScale=0,35`, elle vaut environ 1,54 mm d’épaisseur et 0,855 mm de décalage. Il s’agit de précision des contrôles, pas d’une borne sur le déplacement final de l’isosurface.

Une bande déjà active mais plus mince qu’un code utilise au minimum le code non nul 1, avec une erreur pouvant atteindre un pas. Un bit conserve la décision originale d’ouverture autour de `0.0002` : le décodage ne ferme pas une bande active et ne rouvre pas une bande fermée. Le masque `.w` conserve exactement les extrêmes 0 et 1. Les bornes GPU sont calculées sur les contrôles décodés, avec leur marge numérique habituelle. La même quantification s’applique aux coordonnées partagées entre chunks et LOD.

### Bande et bornes conservatrices

`fieldVolumeBand(column, recipeOffset)` calcule le centre et la demi-épaisseur utilisés par les recettes actuelles :

```glsl
float offset = column.blend.y
    + clamp(column.blend.z + recipeOffset, -160.0, 160.0) * uDetailScale;
float center = column.terrain.x + offset;
float halfThickness = 0.5 * column.terrain.w;
```

`fieldVolumeRecipeDelta()` renvoie zéro hors de cette bande et utilise un fondu vers zéro à son bord. Il permet de conserver le comportement de départ tout en isolant chaque biome dans son programme.

Le planificateur CPU et le rejet GPU réservent une enveloppe couvrant tous les centres autorisés, avec une extension de `160 × detailScale` autour de la hauteur décalée globalement, plus la demi-épaisseur. Les tests de rejet incluent le halo et conservent une marge numérique pour les arrondis flottants. Une nouvelle fonction doit rester dans cette enveloppe ; la passe d’accumulation ne l’évalue pas à l’extérieur. Une épaisseur nulle désactive également son exécution locale.

Pour étendre volontairement cette zone, adapter ensemble [`fieldHeightBounds()`](../ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainField.hpp), [`fieldVolumeEnvelope()`](../Asset/Shader/ProceduralTerrain/field_weights.glsl), les contrôles de la recette et les tests de frontières. Une simple estimation aux coins du chunk ne constitue pas une borne suffisante. De nouvelles fréquences de bruit doivent aussi respecter le domaine de conversion en cellules entières vérifié par `validFieldDomain()`.

## Poids locaux et frontières

Les listes de travaux déterminent quels chunks ont besoin d’une passe. Le poids de la contribution reste calculé **pour chaque colonne**, indépendamment de ces listes et de la taille du chunk.

Le relief utilise les poids de la matrice 2D et la transition marine. Le volume utilise les huit coins de la matrice 3D. Les occurrences répétées du même identifiant sont additionnées pour obtenir son poids total, et sa recette n’est exécutée qu’une fois par point. Le troisième axe de la matrice reste la hauteur macro relative au rivage, comme auparavant ; il ne correspond pas à la coordonnée Y de chaque voxel.

Il n’y a pas de seuil sur le poids moyen ou maximal d’un chunk. Tous les poids locaux strictement positifs sont conservés. Les masques sont construits à partir de l’ensemble des colonnes nécessaires, y compris les échantillons du halo `[-1, N+1]` utilisés par les normales et les transitions de LOD.

Les positions utilisent toujours la grille entière commune :

```glsl
world = (origin + (sampleIndex - 1) * (1 << lod)) * baseSpacing;
```

La seed, les bruits et les poids dépendent de cette position mondiale. Les passes sont exécutées dans l’ordre fixe des identifiants de biome, de 0 à 25, avec les mêmes règles d’accumulation `float32`. La compaction des listes peut changer quels chunks sont regroupés, sans changer l’ordre des contributions à un point partagé.

Le mélange de résultats 3D peut modifier la forme des transitions par rapport à l’ancien mélange de paramètres : l’évaluateur volumique est non linéaire. Les recettes de départ conservent leur construction, mais le stockage compact introduit les petites différences de contrôles décrites plus haut. Une comparaison bit à bit avec l’ancienne structure FP32 complète n’est donc pas attendue pour les densités sculptées.

## Exécution d’un lot de chunks

`submit()` réserve les travaux sans lancer immédiatement toutes les passes. Le moteur appelle `dispatchQueued()` à la fin de sa boucle de soumission : les chunks disponibles sont regroupés et leur calcul commence dans la même image. Pour les autres utilisateurs du backend, `poll()` lance aussi les travaux encore en attente. Un travail annulé avant ce lancement est retiré du lot ; son résultat d’annulation est publié par `poll()`.

1. **Recherche dans les caches.** Les colonnes sont identifiées par XZ, LOD et seed ; les densités par XYZ, LOD et seed. Les paramètres et programmes restent fixes pendant la vie de ces caches. Les colonnes partagées par plusieurs chunks verticaux du lot n’ont qu’un seul producteur. Un changement du seul masque de raccord peut réutiliser le champ.
2. **Classification commune.** `field_batch_classify.C.glsl` calcule la hauteur macro, le climat, le mapping et les masques 2D des colonnes absentes du cache, halo inclus.
3. **Listes de passes sur GPU.** `field_batch_jobs.C.glsl` compacte les slots de chunks par biome et écrit les dimensions des dispatchs indirects. Une passe absente reçoit zéro groupe à exécuter.
4. **Accumulation 2D.** Les programmes `relief.C.glsl` traitent chacun les nouvelles colonnes de leur liste. Un seul thread possède une colonne dans une passe ; seules les quatre composantes d’accumulation sont écrites.
5. **Finalisation des colonnes.** `field_batch_finalize.C.glsl` applique les échelles et écrit le format compact de 32 octets. Une barrière rend ces résultats visibles aux chunks qui partagent ces colonnes.
6. **Préparation des volumes.** `field_batch_prepare.C.glsl` utilise les colonnes finales, nouvelles ou cachées. Il réduit les signes possibles et les enveloppes de toutes les colonnes du halo. Un chunk sûrement plein ou vide évite la matérialisation du volume et le maillage. Chaque tuile XZ de 8×8 colonnes reçoit un masque de biomes et une plage conservatrice de groupes Y ; les listes indirectes limitent également leur dimension Y aux plages utiles.
7. **Initialisation de la densité.** `field_batch_density.C.glsl` écrit la base une seule fois pour les champs manquants qui peuvent contenir une surface. Les modes analytiques plan et sphère restent disponibles.
8. **Accumulation 3D.** Les programmes `volume.C.glsl` rejettent les tuiles et groupes Y inutiles avant les chargements et la barrière locale, puis ajoutent leurs contributions en ordre fixe. Un delta exactement nul ne lit ni ne réécrit la densité ; aucun epsilon n’élimine les petites contributions.
9. **Extraction du maillage.** Le comptage puis l’émission régulière et de transition utilisent le champ final. Les groupes de cellules sans triangles peuvent être ignorés par l’émission. Aucun maillage intermédiaire n’est construit pour les biomes.

Les cinq programmes de préparation du champ et les quatre variantes de meshing forment les **9 programmes communs**. Les deux pipelines de dessin et d’ombres sont distincts de ces 61 programmes compute.

Une barrière de stockage sépare les passes qui lisent les écritures précédentes. Les commandes indirectes reçoivent également une barrière de commande avant leur utilisation. Ces barrières ordonnent le travail GPU sans attendre sa fin sur le CPU. Les atomics de la préparation concernent seulement les masques et bornes entières, réduits d’abord par groupe ; la densité et le relief utilisent des additions ordinaires. Les compteurs du meshing conservent leurs atomics entiers.

Dans un groupe 3D de 8 × 4 × 4 invocations, les 32 colonnes XZ et leurs poids sont chargés une fois puis partagés par les quatre invocations Y : **1 664 octets de mémoire partagée**. Les invocations de padding passent par la barrière commune avant leur sortie.

Les arènes de cache sont bornées par `columnCacheEntries` et `densityCacheEntries` ; leurs slots utilisés par des travaux en cours ne peuvent pas être évincés. Les caches peuvent être désactivés pour comparaison, tout en gardant les slots nécessaires aux travaux simultanés. Il n’existe pas de champ complet par biome. Avec `N` cellules par axe, une densité possède `(N+3)³` floats et une tuile de colonnes finales `(N+3)²` enregistrements de 32 octets, plus l’alignement éventuel. Pour 64 cellules, cela représente 1 203 052 octets de densité et 143 648 octets de colonnes. Le scratch FP32 de 48 octets par colonne ne sert qu’à la construction 2D. Les métadonnées de volume ajoutent un en-tête et un `uvec4` par tuile de 8×8 colonnes.

La plage Y est déterminée à partir des positions mondiales réelles des extrémités des groupes, sans reconvertir des grandes coordonnées flottantes en indices par division. Les rejets restent conservateurs aux grandes coordonnées et entre LOD. La compaction et les dimensions de dispatch restent sur le GPU ; aucun retour CPU n’est utilisé pour sélectionner les biomes.

## Compilation parallèle et cache

[`ShaderProgramBatch`](../ANTAGONIZ/Header/Rendering/Shader/ShaderProgramBatch.hpp) prépare le catalogue fixe à l’initialisation. Il charge d’abord les binaires valides et envoie les programmes manquants au service [`ParallelShaderCompiler`](../ANTAGONIZ/Header/Rendering/Shader/ParallelShaderCompiler.hpp). Celui-ci utilise `GL_ARB_parallel_shader_compile` ou `GL_KHR_parallel_shader_compile` pour demander au pilote un travail de compilation parallèle.

Il s’agit du parallélisme fourni par le pilote : le terrain ne lance pas des appels OpenGL concurrents depuis plusieurs threads applicatifs. Si l’extension est indisponible, la compilation de repli se fait pendant l’initialisation. L’initialisation attend que le catalogue soit prêt ; le déplacement dans le monde ne déclenche aucune nouvelle compilation. Le gain dépend du pilote et ne constitue pas une garantie sur le temps de démarrage.

Les clés du cache incluent les sources développées, les defines, les stages et l’identité du pilote/contexte. Chaque variante contient directement sa recette ; une modification dans un autre dossier de biome ne change donc pas cette clé.

| Modification | Programmes compute à recompiler si aucun binaire correspondant n’existe |
| --- | --- |
| `biomes/PolarIce/relief.C.glsl` | La passe 2D de PolarIce. |
| `biomes/PolarIce/volume.C.glsl` | La passe 3D de PolarIce. |
| `biome_relief_common.glsl` | Les passes 2D qui utilisent ce helper. |
| `Noise/noise_shaping.glsl` | Les programmes 2D et 3D qui utilisent la bibliothèque ; aucune fonction spatiale dans ce fichier. |
| `Noise/noise_2d.glsl` | La classification et les passes 2D qui l’incluent. |
| `biome_volume_common.glsl` ou `Noise/noise_3d.glsl` | Les passes 3D qui l’incluent. |
| `field_surface_accumulate.glsl` | Les 26 passes 2D. |
| `field_volume_accumulate.glsl` | Les 26 passes 3D. |
| Un fichier commun de coordonnées, de poids ou de disposition | Tous les programmes qui incluent ce fichier. |
| Seed ou réglage transmis par uniform | Aucun : les valeurs sont réappliquées aux programmes chargés. |

`ENDURANCE_SHADER_CACHE_DIR` permet de choisir le répertoire du cache. Un binaire corrompu, incompatible ou refusé par le pilote entraîne une compilation normale. Un répertoire non inscriptible empêche sa sauvegarde, pas l’utilisation du terrain. Les sources restent nécessaires à la validation des dépendances, même lorsqu’un binaire est présent.

`Asset/Shader/ProceduralTerrain` contient uniquement les shaders utilisés par le terrain et leurs dépendances. Les anciens agrégats de biomes et les anciens points d’entrée de génération ont été supprimés. Les outils de vérification restent dans `Tests/Terrain` et utilisent les passes actuelles ou sondent séparément les recettes des fichiers `.C.glsl`.

## Ce qu’il faut mesurer

La spécialisation diminue la quantité de code de chaque programme et isole les modifications d’un biome. L’accumulation ajoute en revanche une lecture et une écriture du champ par passe 3D active. Le regroupement par biome réduit les changements de programme et la sélection indirecte évite de lancer des groupes pour les biomes absents.

Le coût dépend donc du nombre de biomes réellement actifs, de la taille des lots, du coût des fonctions et de la bande passante GPU. Il faut comparer ces mesures sur la carte cible, avec les mêmes trajets et réglages. La validation de compilation, des poids et des frontières ne remplace pas une mesure de performances.
