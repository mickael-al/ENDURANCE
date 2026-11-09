# Les biomes d’ENDURANCE

La matrice reprend les **25 cases de l’image de référence**, avec une fonction de relief 2D et une fonction volumique 3D indépendante pour chacune. Un 26e biome décrit le fond marin. Les noms de forêt, de prairie ou de mangrove désignent ici le terrain destiné à accueillir ces milieux ; la végétation sera un système séparé.

## Matrice climatique et identifiants

Les lignes vont du très froid au très chaud ; les colonnes vont du très sec au très humide. Les identifiants sont stables, de gauche à droite puis de haut en bas.

| Température / humidité | Très sec | Sec | Modéré | Humide | Très humide |
| --- | --- | --- | --- | --- | --- |
| Très froid | **0** Glace polaire | **1** Toundra gelée | **2** Toundra | **3** Taïga | **4** Forêt boréale marécageuse |
| Froid | **5** Toundra rocheuse | **6** Steppe froide | **7** Forêt boréale | **8** Forêt froide | **9** Marécage froid |
| Tempéré | **10** Plaine sèche | **11** Prairie | **12** Forêt tempérée | **13** Forêt tempérée humide | **14** Marais tempéré |
| Chaud | **15** Désert de roche | **16** Savane sèche | **17** Savane | **18** Jungle sèche | **19** Jungle tropicale |
| Très chaud | **20** Désert volcanique | **21** Désert rouge | **22** Plateau aride | **23** Jungle brûlante | **24** Mangrove tropicale |

Les matrices qui font autorité sont `BiomeMatrix2D` et `BiomeMatrix3D`, dans `ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainBiomes.hpp`. La première contient 5 × 5 identifiants ; la seconde 5 × 5 × 5. Son troisième axe représente la hauteur macro relativement au rivage, **pas l’altitude du voxel**. Modifier ces tableaux demande une recompilation C++ ; le bouton de régénération ne recharge que les sources GLSL et les réglages de scène. La table compacte de leurs 64 cellules d’interpolation est envoyée une fois au GPU. Les fractions locales et les poids bilinéaires/trilinéaires sont calculés sur le GPU pour chaque colonne XZ, sans aller-retour de carte climatique vers le CPU.

**25 : fond marin.** La mer est sélectionnée à partir de la hauteur de référence, avant ajout du relief propre aux biomes. Elle se mélange progressivement avec la terre autour de `seaLevel`, suivant `seaBlendWidth`. Une grotte terrestre ne change donc pas de biome uniquement parce que son échantillon 3D descend sous le niveau marin. Ce biome génère la géométrie du fond ; il ne constitue pas un rendu d’eau ni un système de nage.

## Direction du terrain

La recherche distingue les phénomènes naturels et leur interprétation pour le jeu : le climat seul ne détermine pas la géologie. Associer une famille de formes à chaque case est un choix de direction artistique, pour rendre les régions reconnaissables à grande distance.

Les [références naturelles](BIOME_REFERENCES_NATURE.md) couvrent le pergélisol, la glaciation, les dunes, les mesas, les karsts, le volcanisme, les marais et les canyons sous-marins. Les [références artistiques et de déplacement](BIOME_REFERENCES_ART.md) étudient notamment Journey, Sword of the Sea, Sonic Frontiers, Shadow of the Colossus et les paysages des films de Studio Ghibli. Ces documents distinguent leurs observations des propositions de design.

Le terrain combine trois échelles :

1. **Relief régional** : hauteur de base et grandes pentes continues, sur des centaines de mètres à plusieurs kilomètres.
2. **Signature du biome** : nappes glacées, bosses allongées, terrasses, dunes orientées, bassins, tables ou grandes colonnes. Les formes plus fantastiques viennent de leurs proportions et de leurs évidements.
3. **Sculpture volumique** : cavernes, entailles, surplombs et passages sous les masses de terrain, dans une bande déplaçable verticalement. Le microrelief de surface reste principalement visuel.

Les zones de liaison privilégient des courbes longues pour conserver de la vitesse. Les ruptures franches et les formes hautes sont davantage concentrées dans les monuments et escarpements, qui peuvent servir d’ancrages au futur grappin.

## Recettes présentes dans le code

Les formes ci-dessous sont produites par les recettes actuelles. Les tubes et cavernes sont des motifs du champ, pas des passages scénarisés dont les entrées seraient garanties à chaque emplacement.

| ID / biome | Relief 2D | Sculpture 3D |
| --- | --- | --- |
| 0 — Glace polaire | Voronoï déformé, ondulations, escarpements et lacs glacés | Petites cavités angulaires enterrées |
| 1 — Toundra gelée | Crêtes et montagnes à plusieurs échelles, avec bande 3D liée au relief | Petites cavités angulaires enterrées |
| 2 — Toundra | Ondulations allongées et rares affleurements arrondis | Tubes discrets sous le sol |
| 3 — Taïga | Vallées larges, cordons allongés et dos rocheux | Cavernes et passages dans les masses |
| 4 — Forêt boréale marécageuse | Bassins bas, cordons et îlots minéraux | Cavités aplaties sous les reliefs |
| 5 — Toundra rocheuse | Crêtes à gradins et blocs anguleux | Joints, cavités angulaires et strates |
| 6 — Steppe froide | Grandes ondulations et rares tables basses | Tubes étroits davantage enterrés |
| 7 — Forêt boréale | Bosses allongées et grands dos arrondis | Cavernes arrondies et fractures |
| 8 — Forêt froide | Vallées encaissées et contreforts en diamant | Cavités dans les flancs et couches |
| 9 — Marécage froid | Bassins cellulaires et dômes espacés | Sous-cavages limités |
| 10 — Plaine sèche | Sol peu accidenté et buttes rares | Petites cavités sous la plaine |
| 11 — Prairie | Longues ondulations et collines douces | Tubes et renfoncements discrets |
| 12 — Forêt tempérée | Collines et affleurements arrondis | Cavernes et surplombs |
| 13 — Forêt tempérée humide | Vallons à paliers et tours rocheuses | Volumes karstiques plus larges |
| 14 — Marais tempéré | Terrasses basses, chenaux et îlots | Évidements de berges et couches |
| 15 — Désert de roche | Nappes orientées et grandes mesas à gradins | Arches, abris et évidements stratifiés |
| 16 — Savane sèche | Pentes douces et grands dômes isolés | Sous-cavages et passages localisés |
| 17 — Savane | Collines orientées et groupes d’affleurements | Cavités arrondies et joints |
| 18 — Jungle sèche | Sol à paliers et tours anguleuses plus denses | Passages et évidements angulaires |
| 19 — Jungle tropicale | Tours karstiques hautes dans des vallons | Cavités hautes et perforations |
| 20 — Désert volcanique | Grands dômes à caldera, nappes et blocs prismatiques | Tubes, sections angulaires et strates |
| 21 — Désert rouge | Dunes asymétriques orientées et monuments à gradins | Arches et évidements en couches |
| 22 — Plateau aride | Tables anguleuses à larges paliers | Évidements horizontaux et surplombs |
| 23 — Jungle brûlante | Pentes orientées, gradins et colonnes prismatiques | Tubes, joints et chambres irrégulières |
| 24 — Mangrove tropicale | Côte basse, chenaux et îlots espacés | Sous-cavages de berges |
| 25 — Fond marin | Plateaux négatifs, canyons orientés et reliefs isolés | Alcôves, strates et cavités sous le fond |

Les monuments n’occupent pas systématiquement chaque cellule : leur présence, leur hauteur, leur rayon et leur couronne varient avec la seed. Une déformation 2D bornée casse les contours trop parfaits tout en conservant le rayon maximal de leur enveloppe.

## Réglage de la glace polaire

Pour isoler cette recette pendant Play, choisir **Glace polaire** dans « Biome a tester ». Modifier ses fichiers GLSL, les enregistrer, puis cliquer sur **Regenerer avec ces reglages**. Le bouton relit les sources et leurs includes, réutilise les binaires valides et recompile seulement les programmes dont les dépendances ont changé. Il reste disponible après une erreur de compilation ; le diagnostic indique les fichiers et lignes concernés.

Le relief 2D se trouve dans **`fieldRelief()`**, dans [`biomes/PolarIce/relief.C.glsl`](../Asset/Shader/ProceduralTerrain/biomes/PolarIce/relief.C.glsl). Ce fichier contient directement la recette et inclut son noyau d’accumulation, qui appelle `fieldRelief()`. Les réglages conservés sont :

| Réglage | Valeur | Effet |
| --- | ---: | --- |
| `POLAR_BASIN_SPACING` | 64 m | Échelle des cellules du Voronoï. |
| `POLAR_BASIN_WARP` | 1,618 | Déformation lente des coordonnées du Voronoï. |
| `POLAR_ROLL_AMPLITUDE` | 18 | Amplitude des grandes ondulations avant `detailScale`. |
| `POLAR_FINE_AMPLITUDE` | 2,5 | Amplitude du bruit fin avant `detailScale`. |

Le code actif part de la distance au carré au site de Voronoï le plus proche, multipliée par 10. Il ajoute les ondulations, un masque d’escarpement, un masque de lac glacé et un relief de rive. Le macrorelief global de la scène s’ajoute au résultat. Les anciens noms `POLAR_BASIN_DEPTH` et `POLAR_BASIN_FLOOR` ne sont pas des contrôles de cette recette.

Le `vec4` de sortie conserve le contrat suivant :

| Composante | Rôle |
| --- | --- |
| `.x` | Hauteur ajoutée au macrorelief, multipliée par `detailScale`. |
| `.y` | Épaisseur **totale** de la bande 3D, multipliée par `detailScale`. Zéro ferme la bande locale. |
| `.z` | Décalage de relief, combiné au décalage volumique avant mise à l’échelle. |
| `.w` | Contrôle local entre 0 et 1, interprété uniquement par `fieldVolume()` du biome. Stocké sur 16 bits après accumulation. |

L’épaisseur polaire est actuellement modulée par un bruit 2D et varie entre **0 et 241 m avant `detailScale`**, avec un décalage de relief de −40 et un contrôle `.w` nul. À `detailScale = 0,35`, son épaisseur totale varie donc entre 0 et environ 84,35 m lorsqu’elle est le seul biome, à la quantification du stockage près. Une transition mélange localement les épaisseurs des biomes contributeurs. Aucun bruit global d’épaisseur ne s’ajoute aux valeurs des recettes. La décision d’ouverture FP32 au seuil `0.0002` est conservée dans un bit de métadonnées ; un zéro ne devient jamais une bande active.

La fonction 3D est **`fieldVolume()`**, dans [`biomes/PolarIce/volume.C.glsl`](../Asset/Shader/ProceduralTerrain/biomes/PolarIce/volume.C.glsl). Son noyau d’accumulation appelle directement `fieldVolume()`. Ses contrôles de départ sont :

```glsl
const vec4 FIELD_VOLUME = vec4(-20, 30, 72, 0.4);
const vec4 FIELD_SHAPE = vec4(.35, .50, .15, .10);
const vec4 FIELD_STYLE = vec4(.85, .70, .15, .30);
```

Modifier ces constantes règle la recette actuelle. Remplacer le corps de `fieldVolume()` permet d’écrire une autre construction 3D pour la glace, indépendamment des autres biomes. La fonction renvoie **une contribution à la densité**, et non une densité de base à ajouter de nouveau. Son code prépare explicitement les amplitudes et choisit comment `column.blend.w` les influence. Le helper générique et l’accumulateur ne réinterprètent pas ce contrôle. Le [guide d’accumulation](BIOME_ACCUMULATION.md#contrat-de-la-fonction-3d--renvoyer-une-contribution) détaille ce contrat et les limites de sa bande.

## Paramètres des recettes 3D

Chaque `biomes/<Name>/volume.C.glsl` contient trois constantes `vec4` et une fonction `fieldVolume`. Les noms sont identiques dans chaque dossier : les programmes sont compilés séparément, sans collision de symboles. Les constantes règlent le helper partagé `fieldVolumeRecipeDelta()` ; elles n’imposent pas cette forme de calcul aux futures recettes. Un programme de production ne contient que la fonction du biome sélectionné et ses helpers, sans table de paramètres des 26 biomes.

| Groupe | Composante | Signification avant préparation de la colonne |
| --- | --- | --- |
| `FIELD_VOLUME` | `.x` | Décalage vertical supplémentaire en mètres avant `detailScale`. Négatif : bande plus enfouie. |
| | `.y` | Amplitude de déformation du champ de hauteur, avant `detailScale` et le masque rocheux. |
| | `.z` | Échelle de densité du creusement, avant `detailScale` et le masque rocheux. Ce n’est pas l’espacement des grottes. |
| | `.w` | Fraction de la demi-épaisseur utilisée pour adoucir les bords de la bande, normalement entre 0 et 1. |
| `FIELD_SHAPE` | `.x` | Poids du premier bruit 3D, isotrope. |
| | `.y` | Poids du deuxième bruit, variant plus vite verticalement. |
| | `.z` | Poids du troisième bruit, variant plus vite horizontalement. |
| | `.w` | Seuil de rayon des cavités dans l’espace des valeurs du bruit ; ce n’est pas un rayon en mètres. |
| `FIELD_STYLE` | `.x` | Mélange de cavités en feuillets (0) vers des tubes (1). |
| | `.y` | Section des tubes : arrondie (0), en diamant (0,5), carrée (1). |
| | `.z` | Réintroduit une part du premier bruit dans la déformation de surface, entre 0 et 1. |
| | `.w` | Intensité de la modulation du rayon par les strates, entre 0 et 1. |

Dans le helper de départ, les poids `shape.xyz` ne sont pas normalisés automatiquement. Les amplitudes `volume.yz` et le rayon `shape.w` doivent rester non négatifs. La fonction du biome multiplie d’abord ses amplitudes par `uDetailScale`, puis choisit l’usage de `.w`, avant de les transmettre au helper. Celui-ci reçoit donc les amplitudes préparées en mètres mondiaux. Les fréquences et sels des trois bruits sont définis dans `biome_volume_common.glsl`. Modifier ce helper affecte tous ses utilisateurs ; une fréquence ou une géométrie propre à un biome peut être écrite directement dans son fichier.

`column.blend.w` n’est pas un poids de biome. C’est le quatrième résultat 2D accumulé, borné à `[0,1]`, puis stocké en UNORM16 et redécodé en float pour la fonction 3D. Par exemple, une recette de pierres peut s’en servir pour décider où construire ses propres volumes ; une recette de cavités peut lui donner un autre sens. Les recettes initiales conservent les facteurs `mix(0.55,1.0,w)` pour le déplacement et `mix(0.85,1.0,w)` pour le creusement, écrits dans chaque fonction 3D. Un `.w` nul ne désactive donc pas automatiquement ces recettes de départ, mais une autre fonction peut choisir ce comportement.

Pour désactiver la sculpture d’une recette par défaut, mettre ses `volume.y` et `volume.z` à zéro ; son relief 2D reste actif. Une fonction personnalisée peut simplement renvoyer `0.0`. À une frontière, les contributions des biomes voisins restent évaluées et pondérées localement. Il n’y a plus d’interpolation des douze paramètres entre biomes.

Les constantes de chaque `volume.C.glsl` constituent l’unique emplacement de réglage de sa recette de départ. Les anciens agrégats de tous les biomes et leur tableau de paramètres ont été supprimés ; les sondes de `Tests/Terrain` utilisent les recettes actuelles.

## Composition et coût GPU

La préparation commune calcule le climat et la hauteur macro pour chaque colonne XZ, halo inclus. Le GPU construit ensuite des listes de chunks par biome et les commandes de dispatch indirect. Les programmes de relief ajoutent leurs résultats pondérés dans quatre accumulateurs ; la finalisation applique les échelles. La base de densité est écrite une seule fois, puis chaque fonction 3D ajoute son delta pondéré dans le même champ.

Les poids dépendent de l’humidité, de la température et de la hauteur macro **à cette colonne**. Ils ne deviennent pas un pourcentage constant par chunk. Les identifiants répétés parmi les coins d’une matrice voient leurs poids additionnés avant la contribution. Tous les poids strictement positifs sont conservés ; aucun seuil moyen de chunk n’élimine une petite zone active. Les halos et transitions de LOD utilisent le même repère mondial et le même ordre d’accumulation.

Le mélange porte sur les **résultats des fonctions 3D**. Comme le calcul de densité est non linéaire, les formes des transitions peuvent différer de la génération précédente qui mélangeait les paramètres. Les recettes initiales des biomes forcés conservent leur construction, avec les arrondis du passage densité complète → delta et la quantification des contrôles stockés. Reproduire un monde demande la seed, les réglages et la même version du générateur.

Une colonne finale occupe **32 octets**, soit 143 648 octets pour 67² colonnes avant alignement. La construction 2D conserve un scratch FP32 de 48 octets par colonne et ne quantifie qu’après l’ensemble des additions. Hauteur, climat, fraction d’altitude et décalage global restent en FP32 ; épaisseur, décalage de relief et `.w` utilisent 16 bits chacun. Le [contrat de stockage](BIOME_ACCUMULATION.md#stockage-compact-et-précision) précise leurs erreurs et le traitement des bandes très minces. La densité et son accumulation restent en `float32`.

Les arènes conservent des caches bornés : les colonnes sont réutilisables entre chunks verticaux et les densités entre demandes où seul le masque de raccord change. Les listes de biomes stockent seulement les slots ; il n’existe aucun champ complet par biome. Le GPU réduit les signes et enveloppes sur le halo complet pour éliminer les chunks sûrement pleins ou vides avant la matérialisation du champ. Chaque tuile XZ de 8×8 reçoit un masque et une plage Y conservatrice. Les dispatchs limitent leur hauteur et les groupes inutiles sortent avant leurs chargements de colonnes.

Le noyau 3D partage 32 colonnes décodées et leurs poids entre quatre échantillons Y, avec 1 664 octets de mémoire partagée par groupe. Une contribution exactement nulle ne lit ni ne réécrit la densité ; les faibles poids positifs restent conservés. Les sommets restent à 12 octets et les compteurs de meshing à 16 octets par travail. Le comptage mémorise les groupes contenant des triangles pour que l’émission ignore les groupes vides.

Les additions de relief et de densité n’utilisent pas d’atomics flottants. Les passes successives sont séparées par des barrières mémoire ; les atomics de classification concernent uniquement les masques et bornes entières, après réduction locale par groupe. Le terrain produit ensuite un seul maillage à partir du champ terminé. Les compteurs de meshing conservent leurs atomics entiers.

Le catalogue est fixe : **26 programmes 2D + 26 programmes 3D + 9 programmes communs = 61 compute shaders**, en plus des pipelines de dessin et d’ombres. La nouvelle préparation de volumes est séparée de la finalisation afin de partager des colonnes entre plusieurs chunks. `ShaderProgramBatch` réutilise les binaires disponibles et compile les manquants avec `ParallelShaderCompiler`, via les extensions ARB/KHR du pilote. Le repli sans extension est synchrone à l’initialisation. Aucun déplacement dans le monde ne crée de nouvelle variante.

**Regenerer avec ces reglages** relit les sources, charge leurs binaires valides ou recompile les programmes modifiés, puis reconstruit les chunks. Éditer `biomes/PolarIce/relief.C.glsl` concerne uniquement le programme 2D polaire ; éditer son `volume.C.glsl` concerne uniquement son programme 3D. Un helper partagé invalide les variantes qui l’incluent. Changer seulement la seed ou les uniforms conserve les clés. Le détail du pipeline et du cache se trouve dans [BIOME_ACCUMULATION.md](BIOME_ACCUMULATION.md).

Les programmes plus petits réduisent la complexité de compilation par variante, mais plusieurs biomes actifs impliquent plusieurs lectures/écritures du champ. Le regroupement des chunks par biome et les dispatchs de zéro groupe pour les biomes absents limitent ce coût. Le résultat en temps GPU doit être mesuré sur la carte cible ; les tests de justesse ne prouvent pas une accélération.

## Contrat du champ 3D

Le champ est positif dans le solide, négatif dans le vide ; zéro définit la surface. Ce n’est pas une distance signée. La base `hauteur - p.y` est écrite une seule fois. Les fonctions `fieldVolume` renvoient seulement leur modification, et le noyau applique le poids local. Hors de sa zone utile, une recette renvoie zéro ; les recettes de départ s’éteignent progressivement au bord de leur bande.

`fieldVolumeBand()` borne la somme du décalage de relief et de l’offset de la recette à **[-160 ; 160] × `uDetailScale`**, puis ajoute le décalage global. L’épaisseur totale reste celle issue de la 2D. `fieldVolumeEnvelope()` couvre toutes les positions autorisées de cette bande pour la sélection des passes et le rejet des échantillons. Son ordre d’addition suit celui du helper et une marge flottante élargit la borne : une réassociation à grande coordonnée pourrait autrement rogner la bande.

Une fonction personnalisée doit respecter l’enveloppe et rester nulle lorsque la bande est fermée. Pour la dépasser, adapter ensemble le rejet GPU, `fieldHeightBounds()` côté C++ et la recette. Les bruits doivent utiliser la position mondiale et la seed commune, sans dépendance à l’origine propre du chunk, au LOD ou au temps. Les tests de raccords, de champs finis et de poids décrits dans [Tests/Terrain/README.md](../Tests/Terrain/README.md) accompagnent toute modification des bornes.

## Espaces de déplacement

Les objectifs fournis sont interprétés en mètres : environ **10 m au minimum visé dans les régions denses**, **100 m dans les grandes régions ouvertes**, et davantage dans les déserts. Les grandes formations utilisent un espacement et un déplacement de centre bornés, indépendants du bruit qui sculpte leur profil.

Ces valeurs sont des objectifs de composition. Elles ne garantissent pas encore qu’un personnage peut parcourir partout le monde à sa vitesse maximale : il faudra aussi vérifier les pentes, les transitions entre biomes, les ouvertures 3D, les collisions, la portée du grappin et la géométrie de la capsule. Une fréquence de bruit ne constitue pas à elle seule une largeur minimale de passage.

## Atlas de diagnostic

Ces images ont été calculées à partir des shaders de production, avec la seed **1337**, les paramètres par défaut et chaque biome forcé séparément. Cet atlas précède la révision des cuvettes polaires ; sa case 0 représente encore l’ancienne recette. `Tests/Terrain/check_polar.py --preview /tmp/polar-relief.png` produit une vue mesurée de la nouvelle forme.

- [Atlas des hauteurs](images/biome_height_atlas.jpg) : 1 536 × 1 536 m par case, échantillonnage de 8 m. La palette et l’éclairage sont normalisés par case pour lire les formes ; ce ne sont pas les matériaux du jeu.
- [Coupes du champ 3D](images/biome_density_sections.png) : noir = solide, blanc = vide. Chaque coupe passe par le point culminant échantillonné de sa case ; son Z est indiqué. Les échelles verticales diffèrent entre cases.

Les hauteurs montrent le relief avant sculpture 3D ; les coupes montrent les évidements réellement présents dans le champ. Ces diagnostics ne remplacent pas un parcours avec la caméra et les collisions du jeu. En vue aérienne, une trame cellulaire reste perceptible dans certaines jungles denses forcées : l’implantation des formations est un réglage artistique à poursuivre après le premier essai en jeu.

## Essayer chaque biome

Ouvrir `ENDURANCE/Scene/Terrain.scene`, puis lancer Play. Dans l’interface du composant, **« Biome a tester »** permet de choisir les matrices automatiques ou un biome forcé. **« Regenerer avec ces reglages »** reconstruit le terrain avec les paramètres actuels.

Le catalogue de passes par biome est utilisé automatiquement. Le bouton de régénération recharge les fonctions 2D et 3D des dossiers de biomes et leurs helpers, puis réutilise les programmes inchangés du cache. Il n’y a pas de variante à choisir selon la combinaison de biomes rencontrée.

Le message « Background terrain work is still active » distingue un terrain déjà visible d’une génération terminée ; il devient « Terrain generation is idle » lorsque la reconstruction et les travaux GPU sont terminés. Cet indicateur ne mesure pas l’activité des autres systèmes du moteur. **« Mesurer les saccades »** conserve le détail de l’image la plus lente ; voir [les compteurs du générateur](PROCEDURAL_TERRAIN.md#utilisation-dans-le-moteur) pour son interprétation.

Les paramètres sont enregistrés avec le script :

| Paramètre | Défaut | Effet |
| --- | ---: | --- |
| `biomeOverride` | -1 | Matrices automatiques ; 0–24 force une case ; 25 force le fond marin. |
| `seaLevel` | 0 m | Niveau de référence utilisé pour la sélection marine. |
| `seaBlendWidth` | 64 m | Étendue de la transition verticale autour du niveau marin. |
| `detailScale` | 0,35 | Intensité du relief et de la sculpture propres aux biomes. |
| `bandOffset` | 0 m | Décalage supplémentaire de la bande 3D, indépendant de la hauteur de surface. |
| `bandOffsetVariation` | 0 m | Variation spatiale de ce décalage. |

Les couleurs provisoires suivent les 25 cases, avec des teintes de roche propres aux milieux. En mode automatique, les surfaces sous le niveau marin prennent progressivement la teinte du fond marin ; ce masque visuel utilise leur altitude réelle, distincte de la hauteur macro qui sélectionne le biome, pour éviter de recalculer un bruit continental par fragment. Elles facilitent la lecture du prototype ; les matériaux définitifs et le triplanar seront une étape distincte.

Les tests et leur exécution sont détaillés dans [Tests/Terrain/README.md](../Tests/Terrain/README.md). Les contrôles GPU s’exécutent sur les shaders de production. Les mesures faites sur un pilote logiciel ne permettent pas de conclure sur le débit de la carte graphique du joueur.
