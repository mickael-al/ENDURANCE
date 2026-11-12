# Génération procédurale GPU

La nouvelle génération utilise un champ scalaire `float32`, une seed `uint32_t`, des coordonnées mondiales communes à tous les niveaux de détail et des chunks de 64³ cellules par défaut. Les fichiers `NoiseGeneration_old` et `Asset/Shader/Noise_old` restent disponibles comme référence ; la nouvelle génération ne dépend pas de leur exécution.

## Champ et biomes

Le champ est construit par **accumulation des résultats de chaque biome**. Le guide [BIOME_ACCUMULATION.md](BIOME_ACCUMULATION.md) décrit les fichiers à modifier, le contrat des contributions 3D et les dépendances du cache. Les 25 cases climatiques et le fond marin possèdent chacun un dossier `Asset/Shader/ProceduralTerrain/biomes/<Name>/`, contenant seulement deux fichiers : `relief.C.glsl` définit `fieldRelief(vec2 p)` et `volume.C.glsl` définit `fieldVolume(vec3 p, FieldColumn column)`. Chaque fichier réunit la recette et son point d’entrée compute. Les noms des fonctions sont communs à tous les biomes, chacun étant compilé dans un programme distinct ; le noyau partagé appelle directement la fonction correspondante.

`field_common.glsl` regroupe les uniforms, la vue décodée `FieldColumn`, le mapping et les coordonnées mondiales. `field_storage.glsl` décrit séparément les colonnes finales compactées sur 32 octets ; les fonctions de biome continuent à recevoir des contrôles flottants. Les positions sont calculées sur une grille entière avant conversion en mètres :

```text
world = (origin + (sampleIndex - 1) * 2^lod) * baseSpacing
```

`origin` est exprimé en voxels du niveau le plus fin. La fréquence des bruits ne dépend ni du chunk ni du LOD. Deux chunks, ou deux LOD, qui échantillonnent la même position utilisent donc les mêmes entrées. La seed participe directement au hachage entier des cellules de bruit ; elle n'est pas convertie en décalage flottant.

Le climat de production utilise `noiseClimate2D(world, seed)` dans
`Asset/Shader/Noise/climate_2d.glsl`. Les constantes `ENDURANCE_CLIMATE_` y
regroupent la recette validée dans le laboratoire : somme de cinq fBm Simplex
par canal, huit octaves, lacunarité 2, gain 0,5, fréquence 0,0005 et multiplicateur
0,1 (fréquence effective 0,00005), seuil de transition 0,08, contraste 1,5,
gamma humidité 1,5 et température 2. L'attraction continue vers le niveau de
matrice le plus proche utilise un déplacement maximal de 0,0468. La matrice
conserve son interpolation quintique. Le Smoothstep de remappage global du
laboratoire n'intervient pas dans cette recette.

Ces réglages sont des constantes de shader ; le composant terrain n'expose plus
de fréquence climatique à l'exécution. La seed reste un paramètre du monde.
Les anciennes scènes contenant `climateFrequency` restent lisibles : cette clé
est ignorée, sans remplacer les nouvelles constantes. Le bouton **Régénérer avec
ces réglages** recharge aussi cet include et invalide le binaire concerné quand
son code change. Le relief macro, sa fréquence et les bandes 3D restent séparés
de cette recette climatique.

Le relief de base utilise un **fBm Simplex à 6 octaves**, lacunarité 2 et gain
0,5, normalisé dans [-1, 1]. `heightFrequency` règle la première octave et
`heightAmplitude` multiplie la somme normalisée ; la seed dédiée reste
`seed ^ 0x13198a2e`. Ce relief est calculé une fois par colonne XZ. Il participe
aussi à la sélection marine, donc changer sa forme change les côtes sans
modifier les champs d'humidité et de température.

Un lot de nouveaux chunks suit ces étapes :

1. Le backend recherche les colonnes XZ et le champ de densité dans leurs caches bornés. Un travail peut partager les données d’un autre travail du même lot ou d’un lot précédent ; les entrées référencées restent protégées contre l’éviction.
2. Pour les colonnes manquantes, `field_batch_classify.C.glsl` calcule la hauteur macro, l’humidité, la température, les données de mapping et les masques des contributions 2D, **halo inclus**.
3. `field_batch_jobs.C.glsl` construit sur le GPU une liste compacte de travaux par biome et les dimensions de leurs dispatchs indirects. Les programmes `relief.C.glsl` accumulent les quatre contrôles de relief en FP32, dans l’ordre des identifiants de biome.
4. `field_batch_finalize.C.glsl` applique les échelles et écrit une seule fois les colonnes finales compactées. Une barrière rend ces colonnes accessibles à tous les chunks qui les partagent, notamment les chunks empilés verticalement.
5. Pour chaque densité manquante, `field_batch_prepare.C.glsl` calcule des métadonnées conservatrices : signes possibles, masques 3D et intervalles Y utiles, pour le chunk et ses tuiles de colonnes. Les listes indirectes 3D sont mises à jour sans lecture CPU.
6. `field_batch_density.C.glsl` initialise le champ de base seulement si le chunk peut contenir une surface. Les programmes `volume.C.glsl` ajoutent leurs **contributions de densité pondérées**, dans le même ordre fixe, en rejetant les tuiles et intervalles sans contribution possible. Un champ entièrement solide ou entièrement vide peut rester non matérialisé : ses métadonnées suffisent pour le meshing.
7. Le comptage puis l’émission du maillage régulier et des transitions utilisent le champ final ou son état homogène. Les comptes par groupe sont conservés pour éviter le travail d’émission des groupes vides.

`submit()` met les demandes en attente. Le moteur appelle `dispatchQueued()` après sa boucle de soumission pour commencer le lot dans la même image ; `poll()` assure aussi ce lancement si l’appel explicite a été omis. Les travaux annulés avant le lancement ne sont pas envoyés au calcul. Chaque passe de biome traite tous les chunks présents dans sa liste. Les biomes absents reçoivent des dispatchs de zéro groupe ; les cas connus directement, tels qu’un champ analytique ou un détail nul, évitent aussi les soumissions inutiles. Aucun masque ni nombre de travaux par biome n’est lu sur le CPU. Le relief et la densité n’utilisent pas d’atomics flottants : une invocation possède chaque échantillon dans une passe, et des barrières de stockage séparent les passes successives. Les réductions locales limitent les atomiques entiers aux masques, aux signes et aux bornes Y des groupes. Les commandes indirectes disposent aussi de la barrière de commande requise.

`ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainBiomes.hpp` contient les matrices qui font autorité : `BiomeMatrix2D` (5 × 5) et `BiomeMatrix3D` (5 × 5 × 5). Les axes sont humidité, température et hauteur macro relative au rivage ; le troisième axe n'est pas l'altitude de chaque voxel. Le C++ prépare une table de 64 cellules et l'envoie une fois au GPU. Modifier une matrice demande une recompilation C++, distincte du rechargement des recettes GLSL.

Les poids restent **locaux à chaque colonne**, bilinéaires en surface et trilinéaires pour le volume. Les occurrences répétées d’un identifiant sont additionnées avant d’appliquer son résultat. Aucun seuil de poids moyen de chunk n’est utilisé : toutes les contributions locales positives survivent, y compris dans le halo `[-1,N+1]`. Le shader de préparation sélectionne les passes, sans remplacer ces poids par des pourcentages constants par chunk. Les accumulations utilisent `float32` et `precise` pour conserver l’ordre des opérations.

Chaque fonction 3D renvoie un **delta** par rapport à la densité de base `hauteur - p.y` :

```glsl
density[p] += biomeWeight(p) * biomeContribution(p);
```

Le champ est positif dans le solide et négatif dans le vide ; sa surface est l’isovaleur zéro. Ce n’est pas une distance signée. Les recettes de départ appellent un helper commun avec leurs propres paramètres, mais leur corps peut être remplacé librement dans les bornes prévues. Les paramètres de plusieurs biomes ne sont plus mélangés avant un évaluateur unique. Comme cet évaluateur est non linéaire, les zones de transition peuvent différer de l’ancienne génération. La hauteur finale reste en FP32 ; la nouvelle quantification des contrôles secondaires peut également modifier légèrement le champ par rapport à la version stockant toutes les composantes en FP32.

L’épaisseur vient de `relief.y × detailScale`, après accumulation locale du relief. Le stockage quantifie `.y` sur 16 bits dans [0 ; 576] avant l’application de `detailScale`, et le décalage relatif sur 16 bits signés dans [-160 ; 160]. Aucun bruit global ni épaisseur artistique supplémentaire ne s’ajoute. Une métadonnée conserve la décision initiale de bande ouverte ou fermée ; un garde numérique empêche qu’une très petite bande active soit fermée par la quantification. Un biome peut donc piloter `.y` par du bruit et le ramener à zéro. Le décalage vertical reste indépendant de la hauteur de surface.

`relief.w` est conservé comme contrôle supplémentaire sur 16 bits non signés dans [0 ; 1]. Chaque `fieldVolume()` décide explicitement comment l’utiliser, par exemple pour piloter l’amplitude ou la présence de roches. Les recettes actuelles gardent leur modulation de départ dans leur propre fonction ; le helper de volume et l’accumulateur n’appliquent plus de modulation cachée par `.w`. Une nouvelle recette peut employer ce contrôle autrement sans modifier le pipeline partagé. Une contribution nulle, une bande fermée ou un point extérieur à l’enveloppe 3D conserve la densité de base pour ce biome. Les noms, références naturelles et artistiques, réglages et limites sont décrits dans [BIOMES.md](BIOMES.md).

Les anciens points d’entrée de génération et les agrégats qui regroupaient les fonctions de tous les biomes ont été supprimés. Le dossier des shaders contient les passes de lot, les programmes spécialisés, le maillage, le rendu, les ombres et leurs dépendances. Les sondes de vérification sont conservées dans `Tests/Terrain`.

## Programmes et rechargement des shaders

Le catalogue contient **61 programmes compute fixes** : 26 reliefs 2D, 26 volumes 3D, les cinq programmes communs de préparation du champ et les quatre variantes de comptage/émission régulière/de transition. Les programmes de dessin et d’ombres sont distincts. Il n’existe aucune variante par combinaison de biomes et aucune compilation déclenchée pendant un déplacement dans le monde.

`Ge::buildShaderPrograms()` prépare le catalogue au démarrage ou à la régénération explicite. Il charge les binaires valides, puis utilise le service existant `Ge::ParallelShaderCompiler` pour les programmes manquants. Les extensions `GL_ARB_parallel_shader_compile` ou `GL_KHR_parallel_shader_compile` demandent une compilation parallèle au pilote. Si elles sont indisponibles, un repli synchrone construit les programmes pendant l’initialisation. Le terrain n’effectue pas d’appels OpenGL concurrents depuis plusieurs threads applicatifs et ne crée pas de contexte partagé. L’initialisation attend la préparation complète du catalogue ; le parallélisme du pilote ne garantit pas un temps de démarrage donné.

Les programmes partagent `ShaderCompilation` pour charger les includes relatifs, injecter les defines après `#version` et conserver les fichiers et lignes avec `#line`. Les erreurs de lecture, de compilation ou de liaison arrêtent proprement l’initialisation ; aucun programme invalide n’est publié. Les pipelines de dessin et d’ombres continuent à utiliser `Ge::GraphiquePipeline`.

Chaque point d’entrée contient directement la recette de son biome et de sa dimension. Modifier `biomes/PolarIce/relief.C.glsl` invalide uniquement son programme 2D ; modifier `biomes/PolarIce/volume.C.glsl` invalide uniquement son programme 3D. Les helpers et bruits communs invalident tous les programmes qui les incluent. Les bruits restent séparés entre `Noise/noise_2d.glsl` et `Noise/noise_3d.glsl`, avec `Noise/noise_common.glsl` pour les helpers et `Noise/noise_shaping.glsl` pour les transformations de valeurs. Le [catalogue de bruits](NOISE_LIBRARY.md) décrit les plages, les coûts et les appels à seed explicite. La table détaillée des dépendances figure dans [BIOME_ACCUMULATION.md](BIOME_ACCUMULATION.md#compilation-parallèle-et-cache).

Le terrain active `ShaderCacheOptions` pour ses programmes compute et ses deux pipelines de dessin/ombres. À chaque initialisation, les sources et leurs includes sont relus, puis une clé SHA-256 est calculée sur les stages ordonnés, les sources développées avec les defines et l’identité du pilote/contexte OpenGL. Dans un même lot de compilation, le contenu d’un fichier partagé n’est lu qu’une fois et l’identité du contexte n’est collectée qu’une fois ; chaque programme conserve son développement et ses diagnostics propres. Un changement de code, d’include, de define, de pilote ou de flags/profil du contexte produit une autre clé ; les dates de modification des fichiers ne décident pas de la validité.

En cas de correspondance, le moteur charge le programme lié avec `glProgramBinary` et vérifie son `GL_LINK_STATUS`, sans appel de compilation ou de liaison des sources. Sinon, il compile les sources, récupère le binaire avec `glGetProgramBinary` et le sauvegarde dans un fichier `.glbin`. Le fichier contient une version de format, le format binaire OpenGL, sa longueur, la clé et une empreinte SHA-256 du contenu. L’écriture passe par un fichier temporaire. Un fichier tronqué, corrompu, incompatible ou refusé par le pilote est traité comme une absence de cache. Un cache indisponible ou non inscriptible n’empêche pas la compilation normale ; une erreur des sources ne réutilise pas un ancien programme.

**Regenerer avec ces reglages** reconstruit le terrain avec cette procédure. Des sources inchangées réutilisent leurs binaires, y compris après un redémarrage du jeu. Changer uniquement la seed ou les réglages transmis par uniforms ne change pas la clé. Les emplacements d’uniforms sont redécouverts et les valeurs réappliquées après le chargement du binaire. L’initialisation et la régénération explicite attendent les programmes nécessaires ; le premier chargement d’une nouvelle source reste soumis au coût du pilote.

Le répertoire explicite `ShaderCacheOptions::directory` est prioritaire sur `ENDURANCE_SHADER_CACHE_DIR`. À défaut, le moteur utilise `%LOCALAPPDATA%/ENDURANCE/ShaderCache` sous Windows, ou `$XDG_CACHE_HOME/ENDURANCE/ShaderCache` puis `$HOME/.cache/ENDURANCE/ShaderCache` sous Linux. Si aucune de ces variables ne fournit d’emplacement, le repli est `Cache/ShaderPrograms` dans le répertoire de travail. Ces fichiers ne font pas partie des assets à versionner. Chaque binaire est limité à 128 Mio ; un programme plus gros reste utilisable mais n’est pas sauvegardé. Le cache ne supprime pas automatiquement les anciennes clés. Supprimer le cache force sa reconstruction au prochain chargement.

Les traces d’initialisation identifient les programmes et les décisions de cache. Leur durée dépend du pilote et de son propre cache ; une recompilation rapide ne suffit pas à mesurer le coût à froid. Le nombre de programmes, leur taille et le nombre de biomes actifs doivent être mesurés avec le coût GPU des passes d’accumulation, sur la carte cible.

Les sept paramètres de rendu constants — intensité et échelle du microdétail, seed, coloration LOD, biome forcé, niveau marin et largeur de transition marine — sont envoyés à l’initialisation uniquement. Les demandes de chunks transmettent leurs coordonnées, leur LOD et leur seed dans les descripteurs de lot ; les paramètres de champ restent fixés jusqu’à la prochaine initialisation.

## Paramètres

Le contrat C++ se trouve dans `ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainField.hpp`. Les distances sont en mètres, les fréquences en cycles par mètre. Les valeurs ci-dessous décrivent le prototype et peuvent être réglées sans modifier la structure du pipeline.

| Paramètre | Défaut | Effet |
| --- | ---: | --- |
| `baseHeight` | 0 | Niveau moyen du terrain. |
| `heightAmplitude` | 320 | Amplitude du relief à grande échelle. |
| `heightFrequency` | 0,00015 | Fréquence du relief à grande échelle. |
| `bandOffset` | 0 | Décalage vertical de la zone 3D ; négatif pour l'enfouir. |
| `bandOffsetVariation` | 0 | Amplitude de la variation signée du décalage. |
| `detailScale` | 0,35 | Intensité du relief, de l'épaisseur, du décalage et des amplitudes 3D propres aux biomes. |
| `seaLevel` | 0 | Niveau de référence de la sélection marine. |
| `seaBlendWidth` | 64 | Largeur totale de transition marine, centrée sur `seaLevel`. |
| `biomeOverride` | -1 | Matrices automatiques ; 0–24 force une case ; 25 force le fond marin. |
| `mode` | `Terrain` | `Plane` et `Sphere` fournissent des champs analytiques de diagnostic. |
| `sphereCenter`, `sphereRadius` | (0, 0, 0), 48 | Paramètres du mode sphère. |

Les capacités de génération sont indépendantes des recettes :

| Paramètre | Défaut C++ | Effet |
| --- | ---: | --- |
| `maxInFlight` | 2 | Nombre de travaux simultanés, de 1 à 16 ; la scène `Terrain.scene` en configure 3. |
| `columnCacheEntries` | 128 | Nombre de tuiles de colonnes XZ conservées. |
| `densityCacheEntries` | 32 | Nombre de champs de densité et états conservés. |
| `measureGenerationTimings` | `true` | Active les horodatages GPU des étapes ; `false` évite entièrement leurs queries GL. |

La capacité effective de chaque cache est au moins `maxInFlight`, pour protéger les travaux en cours. Une valeur de cache nulle désactive la conservation après le dernier consommateur, tout en autorisant le partage entre travaux simultanés. Augmenter les capacités peut éviter des reconstructions au prix de mémoire GPU réservée ; le meilleur réglage dépend du parcours et de la carte cible.

Les anciens réglages `bandThickness` et `bandVariation` ont été supprimés du composant, de la sérialisation et des uniforms GLSL. Le chargeur ignore leurs clés dans les anciennes scènes ; elles ne peuvent pas réintroduire une épaisseur après rechargement. Les valeurs `.y` des 26 recettes ont été converties pour approcher les anciennes bandes moyennes aux paramètres par défaut, puis sont modifiables indépendamment. Les épaisseurs nulles ou négatives sont bornées à zéro. Cette migration demande une recompilation C++ ; les modifications suivantes des valeurs GLSL utilisent le bouton de régénération habituel.

`fieldHeightBounds()` fournit des bornes globales analytiques pour la sélection des chunks. Elles couvrent le relief et toute la bande déplacée, y compris les ravins. Avec les paramètres par défaut, elles valent [-700,8 ; 700,8] mètres. Elles sont volontairement plus larges que les hauteurs réellement atteintes ; il ne faut pas les remplacer par une estimation aux seuls coins d'un chunk. La sélection doit également tenir compte du halo et de la marge d'échantillonnage. `validFieldParameters()` rejette les valeurs non finies, même avec les options de compilation Release `-ffast-math`.

Ces bornes supposent un relief propre au biome borné à [-640 ; 640], une épaisseur totale de biome à [0 ; 576] et un décalage relatif combiné à [-160 ; 160], avant `detailScale`. `fieldVolumeBand()` borne la somme du décalage de relief et de l’offset de la recette. `fieldVolumeEnvelope()` englobe toutes les positions possibles de cette bande pour le rejet des passes. Il utilise le même ordre d’addition que la recette et une marge vers l’extérieur : une réassociation flottante à grande coordonnée pourrait sinon supprimer un échantillon encore dans la bande. Une nouvelle fonction 3D doit conserver cette enveloppe ; l’élargir demande d’adapter ensemble les bornes C++, le rejet GPU et la recette. Sans cela, la sélection peut omettre une formation produite par le shader.

Avant chaque travail, `validFieldDomain()` contrôle également les coordonnées maximales du halo, calculées en double. Le produit coordonnée × fréquence reste inférieur à 2³⁰ pour sécuriser les conversions en cellules entières des bruits. Les paramètres, les bornes et le domaine restent sous 2⁶⁰ afin d'éviter les débordements des intermédiaires flottants, notamment le calcul de distance du mode sphère. Ces contrôles empêchent les valeurs extrêmes d'atteindre les shaders ; ils ne compensent pas la perte progressive de précision des coordonnées `float` loin de l'origine.

L'enveloppe fixe de ce contrôle vaut 0,25 et couvre les transformations des
recettes ainsi que le climat. Sa huitième octave atteint 0,0064 ; un facteur
conservateur de 4 couvre le skew Simplex et la somme des coordonnées de cellule,
soit 0,0256, inférieur à l'enveloppe. Si les defines climatiques augmentent ce
produit au-delà de 0,25, adapter aussi `fixedNoiseCoordinateRate` dans
`ProceduralTerrainField.hpp`. Le relief macro est contrôlé séparément avec
`heightFrequency × 2^5 × 4` : six octaves et le même facteur conservateur
Simplex. Le contrôle utilise le maximum entre ce produit et l'enveloppe fixe.

## Mémoire et extraction

Chaque chunk de N³ cellules utilise `(N+3)³` échantillons : les N+1 points nécessaires aux cellules et un halo d’un point de chaque côté pour les gradients. Pour N=64, le champ fait 67³ floats, soit 1 203 052 octets. Une tuile de 67² colonnes finales compactées occupe 143 648 octets, à raison de 32 octets par colonne. L’accumulation 2D utilise en plus une tranche temporaire de 48 octets par colonne et par travail simultané, soit 215 472 octets avant alignement. Elle évite de quantifier après chaque biome ; seule la finalisation compacte les contrôles.

Les colonnes et les densités finales ont des arènes distinctes, découpées en tranches alignées et gérées par des caches LRU bornés. La clé des colonnes est `(origin.x, origin.z, lod, seed)` ; celle des densités ajoute `origin.y`. Les paramètres de champ restent fixes pendant la vie du générateur. Le masque de transition ne fait pas partie de ces clés : changer un raccord réutilise le champ, puis refait le maillage nécessaire. Les travaux du même lot peuvent partager une entrée dès sa construction. Chaque consommateur la conserve jusqu’à la fin de ses lectures GPU ; ni l’annulation du propriétaire initial ni la pression du cache ne permettent d’écraser une entrée encore référencée.

Chaque densité possède un en-tête `uvec4` et un `uvec4` par tuile de 8 × 8 colonnes, soit `16 * (1 + ceil((N+3)/8)²)` octets : 1 312 octets à N=64. L’en-tête conserve les signes possibles et l’intervalle de groupes Y ; les tuiles conservent leur masque de biomes et leur intervalle. Un état homogène reste réutilisable sans lire le contenu non matérialisé de sa tranche de densité. Les métadonnées de compte par groupe d’extraction sont propres aux travaux : 8 192 octets pour les groupes réguliers et 384 octets pour les six faces de transition à N=64, avant alignement.

Il faut ajouter l’alignement des tranches, la table commune de cellules (1 024 octets), les descripteurs de travaux de 48 octets, les commandes/listes de dispatch indirect, les compteurs de meshing de 16 octets par travail, les autres buffers de meshing, les maillages résidents et les commandes de rendu. Les programmes compilés consomment une mémoire supplémentaire dépendante du pilote, non mesurée par ces compteurs. Il n’existe aucun champ complet supplémentaire par biome.

Contrat des passes de champ, avec `S=N+3` et des strides exprimés en éléments :

| Buffer | Binding SSBO | Disposition |
| --- | ---: | --- |
| Densité | 0 | `float`, adresse `densitySlot*densityStride + x + y*S + z*S*S`. |
| Colonnes d’entrée | 1 | Scratch de 48 octets pour classification/relief/finalisation ; colonnes finales de 32 octets pour préparation/densité/volume. Le descripteur fournit le slot correspondant. |
| Colonnes finales de sortie | 2 | Écriture par finalisation dans le cache de `PackedFieldColumn`. |
| États des champs | 3 | Arène entière des en-têtes et tuiles, indexée par `densitySlot*stateStride`. |
| Commandes et listes | 5 | 52 commandes `uvec4` suivies de 52 listes de 16 slots au maximum. Le même buffer sert au dispatch indirect. |
| Travaux du lot | 6 | Trois vecteurs de 16 octets : origine/LOD ; seed/flags/masques ; slots colonne/scratch/densité. |
| Cellules de matrice | 7 | 64 `uvec4` ; identifiants des quatre coins 2D et huit coins 3D compactés sur 5 bits, puis masque des biomes 3D. |

`PackedFieldColumn` conserve cinq scalaires FP32 : hauteur, humidité, température, fraction verticale de matrice et décalage global. Un mot contient l’épaisseur et le décalage relatif sur 16 bits chacun. Les deux derniers mots contiennent les huit identifiants 3D sur 5 bits, `.w` sur 16 bits, la cellule de matrice et l’état de la bande. La classification conserve temporairement la hauteur macro sans perte de bits dans `detail.w` du scratch ; ces données temporaires n’agrandissent pas les colonnes finales. Les fonctions de biome reçoivent la vue `FieldColumn` décodée, avec leurs contrôles flottants habituels.

Les poids sont reconstruits localement depuis le climat et la fraction verticale. Les densités et les valeurs climatiques restent en `float32`. Les groupes 3D chargent et partagent les colonnes et leurs poids entre plusieurs échantillons Y. Le rejet d’un groupe se fait avant les chargements partagés lorsqu’il est uniforme ; les invocations de padding restantes atteignent la barrière avant de sortir. Les recettes 3D évaluent leurs propres fonctions ; un delta exactement nul évite également la lecture et la réécriture du champ.

Les bindings sont réaffectés pour le meshing : densité 0, colonnes finales 1, tables 2, compteurs 3, sommets 4, comptes par groupe 5 et arène des états 6. L’uniform `uFieldStateIndex` sélectionne l’en-tête du champ dans cette arène entière, sans supposer qu’une tranche de 16 octets respecte l’alignement SSBO du pilote.

Les champs et les sommets restent sur le GPU pendant la génération et le rendu. La lecture de champs denses dans les tests est un outil de vérification ; elle ne fait pas partie du chemin d'exécution du jeu.

`ProceduralTerrainGpu` regroupe les travaux en attente, accumule les champs manquants par biome, puis lance pour chaque chunk le comptage des triangles réguliers et des transitions et leur émission. Chaque groupe non vide réserve sa plage avec un `atomicAdd` après un scan local. Le comptage régulier partage 225 échantillons entre 128 cellules ; l’émission en partage 539 pour inclure le halo nécessaire aux normales. Les nombres de sommets par groupe restent disponibles pour l’émission : un groupe vide sort avant toute lecture de sa tuile et avant le scan. Les champs prouvés homogènes sortent de la même manière dès le comptage. Les tuiles de transition s’adaptent aussi aux petits nombres de cellules.

Dans chaque cellule, une arête commune à plusieurs triangles est interpolée, normalisée et encodée une seule fois, puis recopiée à ses occurrences. Cette réutilisation conserve l’ordre des sommets, la quantification et les particularités des arêtes grossières de transition. Le climat d’une arête régulière demande une lecture de colonne si elle est verticale, deux si elle est horizontale ; les transitions conservent leur interpolation bilinéaire. Les contrôles de débordement et les normales issues du champ restent actifs.

Le comptage permet de réserver exactement l’espace requis, à l’alignement près, dans un pool de sommets fixe ; aucun buffer du pire cas n’est réservé pour chaque chunk. L’émission écrit directement dans ce pool. Il n’existe plus de passe GPU supplémentaire pour préparer une commande de dessin. Une fence sépare le comptage et l’allocation. Le CPU consulte les compteurs de triangles et d’erreur seulement lorsqu’elle est terminée. Après la fence d’émission, `poll()` vérifie l’absence de dépassement et l’égalité entre le curseur écrit et le nombre de sommets réservé avant de publier un handle utilisable. Ces données occupent le buffer de compteurs mappé de 16 octets. Les vérifications et les mesures de temps GPU ne bloquent pas en attendant la génération. Le budget de sommets par défaut est de 256 MiB ; le nombre de tranches de scratch et de métadonnées par travail suit `maxInFlight`, indépendamment des capacités des caches finaux.

`GpuMesher::clearFieldCaches()` invalide les entrées conservées uniquement quand aucun travail n’est en attente. Il refuse l’opération sur un générateur non initialisé ou en échec. Il conserve les programmes, les arènes, les statistiques et les maillages déjà publiés ; les demandes suivantes reconstruisent leurs champs. Cette opération permet notamment de comparer des parcours avec des caches de champs vides, sans confondre ce coût avec la compilation des shaders.

Chaque sommet occupe 12 octets : trois positions locales quantifiées sur 16 bits, deux composantes climatiques sur 8 bits et une normale octaédrique sur deux composantes de 16 bits. La quantification utilise 65534, avec un arrondi explicite `floor(q+0.5)`. Le côté grossier d'une transition est interpolé et quantifié dans le repère du chunk voisin, puis converti en codes entiers du chunk fin. Au rendu, une reconstruction mondiale canonique avec `fma` évite que deux origines de chunks réintroduisent des arrondis différents au même raccord. Le descripteur conserve le LOD et les coordonnées entières du chunk dans `metadata`, avec une limite de représentation exacte de 2²⁴ chunks de base par axe. Cette méthode ne nécessite ni sommets plus grands, ni calcul FP64. Les normales proviennent du gradient du champ, avec un repli défini lorsque ce gradient est nul.

Les transitions utilisent les tables régulières et de transition Transvoxel conservées avec leur licence dans `ThirdParty/transvoxel`. La sélection forme un octree équilibré sur les faces : les voisins diffèrent au maximum d'un niveau, et le chunk fin porte la transition. Le nombre de cellules par chunk reste constant ; seul le pas et donc l'étendue du chunk double à chaque LOD.

## Streaming

`ProceduralGeneration::prepareRenderer()` est appelé une fois par image, après le déplacement de la caméra active et avant les passes de rendu. Le streaming fonctionne donc aussi lorsque les scripts sont suspendus dans l’éditeur. Le composant de scène assure le cycle de vie et la configuration ; son `update()` ne lance pas une seconde génération.

Un worker CPU persistant calcule la sélection et les opérations de remplacement. Il ne fait aucun appel OpenGL. La sélection cible est mise en cache jusqu’à un déplacement de `32 * cellSize` mètres depuis sa dernière évaluation. La caméra peut continuer à se déplacer pendant le calcul : la prochaine demande prend sa position la plus récente, sans accumuler une file de positions anciennes.

`ProceduralTerrainStreaming::planLodTransaction` construit des transactions locales : ajout de quelques chunks absents, subdivision d’un parent, ou regroupement de ses enfants. Les voisins dont le masque Transvoxel change font partie de la même transaction, avec **au plus 32 nouveaux maillages**. La couverture initiale apparaît progressivement. Pour un remplacement, le parent ou les enfants précédents restent affichés jusqu’à ce que tous les nouveaux maillages locaux soient prêts ; la publication les échange ensemble. Les raffinements proches ne sont plus liés aux dégradations de toute la couronne lointaine.

Les priorités GPU sont : remplir les zones absentes du niveau le plus fin au plus grossier ; améliorer les chunks existants, en traitant d’abord les niveaux sources les plus grossiers ; dégrader en dernier, en commençant par les niveaux sources les plus fins. Le niveau le plus précis s’appelle **LOD 0 dans le code**. Les distances départagent les travaux d’un même niveau. Un prérequis nécessaire à l’équilibre 2:1 ou au budget de feuilles peut passer avant l’opération qui en dépend. Les retraits hors de la couverture cible ne demandent pas de génération et sont appliqués immédiatement. Les bits de transition d’une face devenue extérieure peuvent rester en place jusqu’à son prochain voisin : aucun raccord présent ne conserve un masque incompatible.

Une seule petite transaction GPU est engagée à la fois. Elle est menée à terme même si la caméra bouge, puis le worker repart de la couverture réellement publiée et de la dernière position. Les slots GPU restent alimentés par des lots de remplissage allant jusqu’à quatre chunks et par les subdivisions/raccords. Le défaut C++ est de deux travaux simultanés ; la scène en utilise trois et la validation autorise jusqu’à seize. Les limites `maxChunksPerFrame` et `maxInFlight` restent effectives. Augmenter cette dernière limite ne supprime pas les dépendances entre transactions. Le débit n’est pas garanti à vitesse de déplacement arbitraire.

Un changement de masque Transvoxel réutilise la densité conservée, puis reconstruit le maillage régulier et les transitions : les positions régulières près de la face dépendent aussi du raccord. Les chunks empilés partagent leurs colonnes XZ, et un retour dans une région peut réutiliser les champs encore présents. L’éviction LRU libère seulement les entrées sans consommateur GPU ; les budgets des caches et des maillages restent indépendants.

L’ancien et le nouveau maillage local coexistent pendant la préparation : il faut une marge de mémoire pour ce remplacement. Une marge de 32 slots au-delà de `maxSelectedChunks` évite un blocage par le seul nombre de slots pendant une transaction complète ; les valeurs par défaut 2048/1024 la fournissent, indépendamment du budget de sommets. Si un budget est dépassé ou si la génération échoue, la couverture publiée reste affichée et l’erreur est exposée dans les statistiques. Une nouvelle tentative a lieu après un déplacement suffisant de la caméra, ou après Stop/Play avec une configuration ajustée. Les limites de hauteur utilisées pour sélectionner les chunks incluent une marge correspondant au niveau de détail le plus large.

## Culling et ombres

Le rendu principal teste les AABB contre les six plans du frustum seulement lorsque la rotation, la projection, la caméra active ou la couverture changent, ou après `8 * cellSize` mètres de translation depuis le dernier calcul. Entre ces événements, la liste filtrée est réutilisée. Le frustum est élargi du pas de déplacement pour conserver tout chunk susceptible d’entrer dans le champ avant le prochain calcul. Les matrices de vue et de projection déjà calculées par la caméra sont composées en double, avec une marge supplémentaire pour les arrondis. Aucun champ GPU n’est relu.

Chaque index d’ombre possède son propre cache, invalidé par un changement de **sa matrice de lumière** ou de couverture. `LightManager::getShadowMatrix(index, matrix)` fournit la même matrice que celle du shader, sans recopier toutes les matrices ni lire le SSBO. Un relief hors du champ principal peut donc rester un occultant ; une cascade qui bouge est recalculée immédiatement.

La caméra et chaque passe d’ombre conservent également leur propre buffer de commandes de dessin indirectes. `drawMany()` prépare ces commandes de 16 octets depuis les handles validés, en filtrant ceux qui sont périmés. Son cache CPU et ses buffers par liste restent en place : une liste inchangée ne provoque aucun nouvel upload, et alterner entre les passes ne réécrit pas un buffer commun. L’ancien buffer global de commandes, dimensionné à `16 * maxResidentChunks` octets, n’est plus alloué ; les buffers des listes visibles sont conservés. Le dessin isolé `draw()` utilise directement le nombre de sommets, leur position dans le pool et l’instance du handle. Les timestamps GPU sont lus seulement après confirmation de disponibilité. Les fences sont interrogées sans attente ; un flush est regroupé lorsque de nouvelles commandes ont été soumises, au lieu d’être demandé pour chaque fence. Sans génération active, les sauvegardes de bindings compute et les parcours de sélection sont évités.

La caméra de l’éditeur publie désormais une seule pose finale par image modifiée, ce qui évite plusieurs recalculs des cascades pour rotation et déplacement simultanés. Son accélération avec Shift utilise le temps écoulé. Après un retard de rendu, la boucle ne lance plus une rafale d’images pour rattraper une dette ; le worker physique attend les échéances de ses pas fixes à 60 Hz et se réveille immédiatement lorsqu’une commande arrive. Il ne fait plus tourner Bullet avec des durées nulles ; après un long retard, son rattrapage est borné à quatre pas. Ces corrections de cadence ne constituent pas une mesure de performances sur le GPU cible.

La vérification du chemin d’ombre a également corrigé le rattachement des framebuffers lorsque la texture de profondeur est recréée, l’état de profondeur des passes, la singularité de la base de vue quand le soleil est vertical et l’angle de projection des faces de lumière ponctuelle. Le PCF compare neuf profondeurs échantillonnées séparément, rejette les coordonnées hors carte et les points derrière la projection, et borne le biais aux incidences rasantes. Le biais reste un réglage de prototype ; les transitions artistiques entre cascades et la résolution des cartes ne sont pas refondues ici.

Les lumières sont allouées avec `new`/`delete`, qui respectent leur alignement C++ : le pool précédent pouvait placer leurs données alignées sur 16 octets à une adresse décalée de 8 octets, provoquant un plantage. Cette allocation intervient à la création/destruction des lumières, pas pour chaque chunk ou chaque image. La lumière est retirée du gestionnaire avant sa destruction afin que les callbacks de mise à jour des ombres ne parcourent pas un objet détruit.

## Utilisation dans le moteur

Ouvrir `ENDURANCE/Scene/Terrain.scene` dans l'éditeur, puis lancer Play. L'objet vide « Procedural terrain » porte le script `ProceduralTerrainComponent`. La scène utilise la seed 1337 et une caméra à (80, 180, 120), au-dessus de la surface de départ. La scène principale et le réglage de démarrage du projet restent inchangés.

Les paramètres du script sont sérialisés à plat dans la scène. Les modifier avant Play ; arrêter puis relancer Play pour appliquer une nouvelle configuration. La sauvegarde conserve les paramètres et la seed, sans enregistrer le maillage généré. Les statistiques de résidence, mémoire et génération sont visibles dans l'inspecteur pendant Play. Le générateur augmente au besoin la distance de visibilité des caméras utilisées pendant son exécution, puis la restaure à l’arrêt si elle n’a pas été modifiée entre-temps. Le sélecteur « Biome a tester » et le bouton « Regenerer avec ces reglages » permettent aussi de reconstruire le terrain pendant Play. Chaque clic relit les shaders GLSL et les fichiers de biomes inclus, charge leurs binaires inchangés ou recompile les programmes invalidés, sans redémarrer le jeu. Le bouton reste disponible après une erreur GLSL ; les diagnostics indiquent les fichiers inclus et leurs lignes originales. Les constantes de réglage de la glace polaire sont décrites dans [BIOMES.md](BIOMES.md#réglage-de-la-glace-polaire).

Les contrôles de streaming sont `cellSize`, `maxLod`, `viewDistance`, `lodDistanceFactor`, `maxChunksPerFrame`, `maxInFlight`, `vertexBudgetMiB`, `maxResidentChunks` et `maxSelectedChunks`. `columnCacheEntries` et `densityCacheEntries` règlent la conservation des champs ; `measureGenerationTimings` active les mesures GPU de génération. Ces paramètres sont également sérialisés et sont appliqués à la prochaine initialisation ou régénération explicite. `cellSize` désigne le pas d’échantillonnage du LOD le plus fin ; les chunks conservent 64 cellules par axe. La distance souhaitée doit rester compatible avec les budgets de résidence et de génération.

La génération utilise automatiquement le catalogue fixe de 26 programmes 2D et 26 programmes 3D, plus les programmes communs. Aucune option de spécialisation par combinaison de biomes n’est nécessaire. Les anciennes options `enableBiomeVariants`, `maxBiomeVariants` et `maxParallelBiomeCompiles` ne sont pas réintroduites ; le parcours ne crée aucun nouveau programme. Voir [le guide d’édition des recettes](BIOME_ACCUMULATION.md#modifier-un-biome).

Les compteurs « Camera » indiquent les chunks dessinés et rejetés par le frustum principal ; « Shadow passes » additionne ces nombres sur les passes d’ombre de l’image. « LOD selections », « published groups » et « LOD groups » permettent de vérifier le suivi de caméra et l’avancement des transactions locales. La taille maximale d’une transaction, son temps de planification CPU, les mises à jour du culling et le nombre/octets des uploads de listes de dessin sont également exposés. Les compteurs de culling et d’uploads doivent se stabiliser à l’arrêt une fois la génération terminée (si les lumières sont immobiles).

« Background terrain work is still active » reste affiché tant que `rebuilding` ou `pendingJobs` est actif. « Terrain generation is idle » signifie que ces deux indicateurs sont nuls ; il ne décrit pas l’activité des autres systèmes du moteur, notamment des collisions. Un terrain déjà visible peut donc encore avoir du travail en arrière-plan.

« Coverage generation » mesure le temps écoulé pour produire la couverture demandée, avec la planification CPU et l’ordonnancement entre images. « GPU last batch » sépare le dernier lot en relief 2D, préparation, accumulation 3D et comptage. « GPU last emission » indique l’émission mesurée séparément ; « request latency » mesure le délai CPU entre la soumission d’une demande et sa fin. « GPU accumulated » additionne des intervalles GPU disjoints : le travail partagé est compté une seule fois par lot, et chaque émission une seule fois. Il ne faut pas attribuer le coût d’un lot complet à chacun de ses chunks.

« Field cache hits » et « Built fields » distinguent les réutilisations des constructions de colonnes et de densités. « GPU fields » sépare la mémoire réservée aux caches et états, au scratch 2D et aux comptes par groupe, alignement compris. Les programmes, maillages et autres métadonnées ne font pas partie de ces trois valeurs. « Free vertex memory » indique la plus grande plage libre et le nombre de plages, pour rendre visible la fragmentation du pool de sommets.

Les horodatages GPU utilisent un anneau borné, lu uniquement après confirmation de disponibilité. Si l’anneau est occupé, le générateur continue sans attendre et incrémente le nombre d’échantillons non mesurés. `pendingTimingSamples` expose les mesures qui attendent encore leur lecture. Avec `measureGenerationTimings=false`, aucune query de chronométrage n’est créée, soumise ou lue ; les fences de synchronisation, les compteurs de cache et le temps de couverture restent actifs. Le protocole pour comparer la branche de référence et cette version est détaillé dans [TERRAIN_OPTIMIZATION_COMPARISON.md](TERRAIN_OPTIMIZATION_COMPARISON.md).

**« Mesurer les saccades »** active des horodatages CPU légers dans `drawFrame`, désactivés par défaut. L'inspecteur conserve la dernière image complète et la plus lente depuis la remise à zéro, avec ses durées d'animation, de caméra/cascades, de préparation des `CustomRenderer` (dont le terrain), de rendu/HUD et de swap. Les valeurs du pic appartiennent toutes à la même image ; elles comprennent les attentes du pilote et de la VSync, sans mesurer directement le temps GPU. Les comportements exécutés avant `drawFrame` ne sont pas inclus. Désactiver la mesure fige les résultats ; la remise à zéro écarte aussi l'image en cours. Pour diagnostiquer un pic à l'arrêt, attendre la fin de la génération, remettre les mesures à zéro puis reproduire le problème sans interrompre le débogueur : sa pause serait incluse dans les durées. Un temps élevé dans le swap seul ne prouve pas une surcharge GPU : l'attente de synchronisation verticale y est également comptée.

`debugLod` colore les niveaux de détail pour examiner les transitions. `microDetailStrength` et `microDetailScale` règlent une perturbation procédurale des normales, filtrée selon l'empreinte du pixel. Le rendu utilise pour l'instant des couleurs de biomes : les textures et leur projection triplanaire définitives ne sont pas ajoutées dans cette étape.

## Collisions locales

`collisionEnabled` active le cache de collisions autour de la caméra ; `collisionRadius` vaut 128 mètres par défaut. L’API active les collisions par défaut, mais `Terrain.scene` les désactive dans sa configuration actuelle. Le sélecteur conserve le LOD 0 dans cette zone lorsqu’elles sont activées. Les collisions réutilisent uniquement les triangles locaux utiles et ne relisent jamais le champ dense, ni ne recalculent ses bruits sur le CPU.

Les sommets compressés sont copiés de façon asynchrone vers un buffer de transit mappé de manière persistante. Après signalement de la fence, un pool fixe de workers CPU décode directement ce mapping ; le thread de rendu ne recopie plus les sommets dans un vecteur CPU intermédiaire. Le décodage reconstruit les positions mondiales canoniques avec `fma`, écarte les triangles dégénérés, puis libère le slot de transit. Le worker construit ensuite le BVH Bullet et prépare sa publication via la file de commandes physique. Cette séparation permet de réutiliser le transit pendant la construction de la collision ou l’attente de publication.

Les limites par défaut sont deux slots de transit, deux workers, quatre constructions en attente, une copie et 16 Mio copiés par image, avec au maximum 16 Mio par slot de transit. La file de constructions borne le travail CPU indépendamment du nombre de slots GPU. L’arrêt attend la fin des workers avant de démarrer le démontage des mappings ; les annulations et remplacements conservent la propriété des ressources jusqu’au dernier consommateur.

Les chunks proches prêts peuvent alimenter ce cache avant la publication de leur transaction visuelle. La liste des candidats est reconstruite lorsque les maillages changent ; à caméra immobile et sans travail de collision en attente, le cache ne reparcourt pas les candidats à chaque image. Les collisions précédentes restent actives jusqu’à leur remplacement ; une marge de sortie évite leur suppression au bord du rayon. La collision peut donc arriver après l’affichage d’une zone : sa disponibilité doit être prise en compte par le futur contrôleur de personnage.

## Vérifications et limites

Les tests de calcul utilisent directement les shaders GLSL 450 dans un contexte OpenGL sans fenêtre :

```bash
python3 Tests/Terrain/check_gpu.py
```

Ils contrôlent notamment le plan et la sphère analytiques, la répétition d’une seed, les positions communes entre chunks et entre LOD, l’écriture du halo, les paramètres de bande et l’absence de valeurs non finies. Les fixtures d’optimisation vérifient aussi le stockage compacté, les très petites bandes, le contrôle `.w` dans la recette, les rejets conservateurs à grande coordonnée et LOD élevé, les 256 cas réguliers et 512 cas de transition, ainsi que les triangles encodés avant/après réutilisation des arêtes. Les tests de cache comparent la géométrie complète à une exécution sans conservation, y compris les annulations, alias, évictions et champs homogènes. Le backend vérifie le vidage des caches au repos et l’absence de queries GL lorsque la mesure est désactivée ; une fixture de collision exécute le mapping persistant et le BVH Bullet.

Un contexte EGL/OpenGL de calcul est nécessaire ; un environnement sans support adapté est signalé comme indisponible. Les commandes des vérifications CPU, du backend asynchrone, des collisions et de la rasterisation sont détaillées dans [Tests/Terrain/README.md](../Tests/Terrain/README.md). Les tests du chargement commun, des erreurs, de la propriété des programmes et des classes moteur sont décrits dans [Tests/ShaderCompilation/README.md](../Tests/ShaderCompilation/README.md). Les tests GPU ne remplacent pas une mesure de débit, de compilation à froid et de mémoire sur la carte graphique cible ; le [guide de comparaison](TERRAIN_OPTIMIZATION_COMPARISON.md) sépare ces mesures des vérifications de validité.

Le déterminisme vérifié concerne une même implémentation GPU avec une même seed et les mêmes paramètres. Une égalité binaire entre toutes les familles de cartes et tous les pilotes n'est pas garantie par les calculs flottants GLSL. Le futur multijoueur devra identifier la version du générateur et les paramètres en plus de la seed.

Le champ peut produire des surplombs et des volumes dans sa bande 3D, mais la grille ne peut conserver un détail plus petit que son pas d'échantillonnage. Éloigner le LOD double ce pas : les détails fins disparaissent progressivement. La présente version ne fournit ni érosion, ni édition persistante du terrain, ni végétation, ni synchronisation réseau des modifications. Les microdétails de matériau et les textures définitives restent une étape distincte.
