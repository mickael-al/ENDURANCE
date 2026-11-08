# Les biomes d’ENDURANCE

La matrice reprend les **25 cases de l’image de référence**, avec une recette de relief 2D et une recette volumique 3D pour chacune. Un 26e biome décrit le fond marin. Les noms de forêt, de prairie ou de mangrove désignent ici le terrain destiné à accueillir ces milieux ; la végétation sera un système séparé.

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
| 0 — Glace polaire | Cuvettes de Voronoï déformées, fonds doux et rebords arrondis, petites ondulations | Petits renfoncements et chambres enterrées dans les rares secteurs où le bruit d’activité dépasse son seuil |
| 1 — Toundra gelée | Sol polygonal bas et dômes isolés | Petites cavités angulaires enterrées |
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

Pour isoler cette recette pendant Play, choisir **Glace polaire** dans « Biome a tester ». Modifier les fichiers GLSL ci-dessous, les enregistrer, puis cliquer sur **Regenerer avec ces reglages**. Le bouton relit les shaders de terrain et leurs includes depuis le disque, réutilise leurs binaires si le contenu est inchangé, puis reconstruit les chunks. Les programmes sans binaire correspondant sont recompilés ; changer uniquement les réglages de scène conserve la même clé de cache. Il reste disponible après une erreur de compilation : corriger le fichier et réessayer. Le message fournit une table des fichiers GLSL et des identifiants de source associés aux numéros de ligne du compilateur. Les chemins sont ceux de `Asset/Shader/ProceduralTerrain`, à côté du dossier `Bin`.

Les constantes `POLAR_*` regroupent les principaux réglages :

| Fichier / réglage | Défaut | Effet |
| --- | ---: | --- |
| `biome_relief.glsl` — `POLAR_BASIN_SPACING` | 64 m | Espacement caractéristique des cuvettes. |
| `POLAR_BASIN_DEPTH` | 24 | Profondeur avant `detailScale`, soit 8,4 m à 0,35. |
| `POLAR_BASIN_FLOOR` | 0,06 | Part aplatie au fond du profil normalisé. |
| `POLAR_BASIN_WARP` | 0,65 | Déformation lente des positions de Voronoï. |
| `POLAR_ROLL_AMPLITUDE` / `POLAR_FINE_AMPLITUDE` | 18 / 1,5 | Ondulation large et relief fin, avant `detailScale`. |
| `biome_volume.glsl` — `POLAR_VOLUME_SPACING` | 224 m | Échelle des secteurs où le champ 3D peut intervenir. |
| `POLAR_VOLUME_START` | 0,32 | Seuil du bruit absolu ; l’augmenter réduit la présence du champ 3D. |
| `POLAR_VOLUME_END` | 0,52 | Fin de la rampe d’activation ; conserver une valeur supérieure au seuil. |

Le profil utilise le rapport des distances au carré aux deux sites de Voronoï les plus proches, avec une pente nulle aux rebords des cellules. Les distances restent continues quand le site le plus proche change. Le macrorelief global de la scène est toujours ajouté : mettre temporairement `heightAmplitude` à zéro permet d’inspecter les seules cuvettes.

`fieldReliefPolarIce()` renvoie quatre valeurs, avant application de `detailScale` :

| Composante | Rôle |
| --- | --- |
| `.x` | Hauteur ajoutée au macrorelief. |
| `.y` | Épaisseur totale supplémentaire de la bande 3D. |
| `.z` | Décalage vertical supplémentaire de cette bande. |
| `.w` | Masque de roche de 0 à 1, modulant les amplitudes du helper volumique commun ; 0 ne désactive pas la 3D. |

La fonction 3D à éditer librement est désormais **`fieldVolumePolarIce(vec3 p, FieldColumn column, inout FieldVolumeNoise cache)`**, dans `biome_volume.glsl`. `polarIceVolume()` conserve les paramètres de la recette commune employée par défaut ; modifier uniquement cette fonction suffit pour régler son décalage, sa déformation, son creusement et son rayon. Remplacer le corps de `fieldVolumePolarIce()` permet d’utiliser une autre construction 3D, sans modifier les autres biomes.

Dans la recette polaire actuelle, le masque de rareté s’applique aux amplitudes **et au rayon des cavités** avant le calcul de sa densité : réduire seulement une amplitude positive ne suffit pas à supprimer les valeurs négatives d’un champ creusant. Les secteurs inactifs renvoient directement `column.terrain.x - p.y` sans demander les trois bruits 3D communs. L’activité est précalculée en 2D pour les colonnes concernées. Les sommets compressés restent inchangés.

## Composition et coût GPU

`Asset/Shader/ProceduralTerrain/biome_relief.glsl` contient les recettes 2D individuelles. Elles combinent des bruits de gradient, des crêtes, des profils orientés, des cellules et des paliers dont la largeur de transition est réglée. Les métriques cellulaires euclidienne, Manhattan et maximum produisent des masses rondes, en diamant ou carrées ; elles ne nécessitent pas un algorithme Diamond-Square récursif.

`biome_volume.glsl` contient une fonction **`fieldVolume*` indépendante par biome**. Chacune reçoit la position mondiale, la colonne déjà préparée et un cache de bruits facultatif. Elle renvoie une densité complète, positive dans le solide et négative dans le vide ; zéro définit la surface. Le champ n’est pas une distance signée. La production mélange désormais les **densités obtenues**, et non les paramètres de `BiomeVolume` : les zones de transition peuvent donc changer d’apparence par rapport à l’ancienne méthode. Le dispatcher `fieldBiomeVolume(biome, p, column, cache)` appelle ces fonctions par un `switch`. Les anciens helpers d’inspection des paramètres, dont la surcharge `fieldBiomeVolume(biome)` à un seul argument, restent uniquement dans `Tests/Terrain/biome_profile_probe.glsl`.

Les fonctions actuelles peuvent utiliser `fieldVolumeFromRecipe()` et `fieldSharedVolumeNoise()` pour conserver la base commune. Le cache local au voxel calcule ses trois bruits seulement à la première demande, puis les partage entre recettes. Ce cache suppose la même position mondiale `p` pour tous ses utilisateurs ; des bruits sur des coordonnées déformées doivent être calculés séparément. Un biome peut ignorer ce helper, combiner d’autres bruits ou appeler `fieldCellular3(p, salt)` pour un Voronoï 3D ; `fieldCellular3Data()` fournit également une valeur stable de cellule. Ce Voronoï n’alloue aucune texture et n’est évalué que lorsqu’une recette l’appelle. Les coûts propres aux recettes et les mélanges de plusieurs fonctions restent à mesurer sur le GPU cible.

La génération utilise **un programme générique par passe**, commun à tous les biomes. `field_columns.C.glsl` calcule le climat, les fractions de matrice et le relief dans une seule passe 2D, halo inclus. `field_density.C.glsl` utilise ensuite ces colonnes pour mélanger les densités 3D. Les poids restent **locaux GPU**, jamais un pourcentage constant par chunk ; les poids nuls évitent les appels aux recettes et les identifiants répétés sont regroupés. Les fonctions de chaque biome restent indépendantes et librement modifiables.

Le parcours ne teste plus les 26 biomes à chaque point. En 2D, un masque local de 32 bits rassemble les quatre coins de poids positif et la contribution marine : au maximum cinq recettes distinctes. `findLSB` sélectionne le prochain identifiant, puis `mask &= mask - 1u` retire son bit ; `fieldBiomeRelief()` fournit le `switch` et le bornage de chaque recette. En 3D, `makeBiomeCellTable()` précalcule le masque des huit coins dans le quatrième mot existant de chaque cellule. La passe de colonnes le copie dans `column.detail.w`, ou utilise un seul bit pour le biome forcé. Les voxels parcourent ces candidats et vérifient leur poids local avant d’appeler leur recette. Aucun atomic, buffer supplémentaire ou retour GPU–CPU n’est nécessaire ; le cache de bruits reste partagé entre les appels du même voxel.

Les champs et poids de génération n’utilisent plus `precise` : les floats restent sur 32 bits, mais le compilateur peut fusionner ou réassocier des calculs. De petits écarts d’arrondi par rapport à l’ancienne version sont possibles. Les qualifications des calculs de meshing et de position du rendu ne sont pas modifiées. Le parcours réduit les recherches de poids ; son gain de temps dépend de la compilation du `switch` et de la divergence sur la carte cible.

Le terrain ne crée plus de variantes par combinaison de biomes, de masque à relire sur le CPU ni de worker de compilation pendant le streaming. Les services moteur `Ge::BackgroundShaderCompiler`, `Ge::ParallelShaderCompiler` et `Ge::SharedShaderContext` restent disponibles pour d’autres usages. La séparation des passes 2D et 3D conserve le partage des colonnes entre voxels ; il ne s’agit pas d’un shader monolithique recalculant le relief pour chaque échantillon 3D.

Le biome forcé et la sélection automatique préparent des identifiants et des poids différents, puis passent par le même appel à `fieldSurfaceRecipes()`. Cela évite deux points d’appel susceptibles de dupliquer les 26 recettes 2D lors de leur intégration par le compilateur. La sonde d’interpolation `fieldProbeBlendRelief()` reste uniquement dans `Tests/Terrain/biome_profile_probe.glsl` ; les recettes individuelles restent libres. Ce changement ne garantit pas une réduction donnée du nombre d’instructions ou de registres sur tous les pilotes.

Les six programmes compute de génération passent par `Ge::ComputeShader`, qui utilise le même service `ShaderCompilation` que `Ge::GraphiquePipeline`. Le terrain active le cache binaire persistant de ces classes pour ses six programmes de calcul et ses deux pipelines de dessin/ombres. Les sources, includes et defines sont toujours relus à l’initialisation : leur contenu développé et l’identité du pilote participent à la clé SHA-256. Si le cache correspond, le moteur charge le programme lié ; sinon, il recompile les sources puis sauvegarde le binaire. Un binaire corrompu ou refusé par le pilote déclenche une compilation normale, sans bloquer le générateur pour un simple problème de cache.

**Regenerer avec ces reglages** suit la même procédure et reconstruit toujours les chunks. Une modification de seed ou d’uniforms conserve la même clé ; une modification d’include change celle des programmes qui le contiennent et demande une compilation si le binaire correspondant manque. Les includes de biomes sont partagés par les colonnes et la densité : leur modification peut recompiler ces deux passes. Une erreur GLSL conserve le journal complet et les correspondances de fichiers/lignes, sans réutiliser silencieusement un ancien binaire. Le bouton permet de corriger puis de réessayer. Le chargement et les compilations nécessaires restent synchrones ; aucun déplacement dans le monde ne déclenche de compilation. Les emplacements de cache et les détails de repli sont décrits dans [PROCEDURAL_TERRAIN.md](PROCEDURAL_TERRAIN.md#programmes-et-rechargement-des-shaders).

Une colonne occupe **48 octets**, contre 64 dans la version à paramètres mélangés. Pour 67² colonnes, cela représente 215 472 octets et économise 71 824 octets par travail GPU. Le champ reste en `float32`, et les colonnes ainsi que les fractions climatiques sont partagées par les quatre invocations Y d’un groupe. La table de cellules ajoute 1 KiB pour le générateur ; les compteurs de meshing occupent 16 octets par travail. Le climat intermédiaire reste local à la passe de colonnes et n’est plus écrit puis relu dans un buffer entre deux passes. Les sommets restent à 12 octets ; ni champs denses ni poids locaux ne sont rapatriés vers le CPU.

Les transitions climatiques sont adoucies avant l’interpolation. La seed, les positions mondiales et les fréquences ne dépendent pas du LOD. Une modification de ces recettes change le monde produit par rapport à la version précédente : reproduire un monde demande la seed, les paramètres **et la même version du générateur**.

## Contrat d’une fonction 3D personnalisée

La liberté de calcul conserve les bornes nécessaires au streaming. Une fonction `fieldVolume*` doit renvoyer le champ de base `column.terrain.x - p.y` hors de sa bande, avec un raccord continu. L’épaisseur totale est `column.terrain.w`. Le décalage global est `column.blend.y` ; le décalage relatif combinant le relief et la recette doit rester dans **[-160 ; 160] × `uDetailScale`**. `fieldVolumeFromRecipe()` borne automatiquement cette somme. Une implémentation entièrement personnalisée doit respecter le même contrat ; sinon, adapter ensemble le rejet anticipé de `fieldDensityAtPrepared()` et `fieldHeightBounds()` côté C++ avant d’élargir son domaine.

Conserver une échelle de densité cohérente avec les autres biomes pour que leur mélange reste prévisible. Définir les bruits dans le repère mondial avec la seed commune ; ne pas dépendre de l’origine du chunk, de son LOD ou du temps. Une région sans détail 3D peut simplement renvoyer le champ de base. Les tests de raccords et de champs finis décrits dans `Tests/Terrain/README.md` doivent accompagner une modification des recettes ou de leurs bornes.

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

Le chemin générique est le seul chemin de génération. Le bouton de régénération recharge toujours les fonctions 2D et 3D de chaque biome, sans option de spécialisation ni cache de variantes à régler.

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
