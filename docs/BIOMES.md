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
| 0 — Glace polaire | Nappes orientées, sillons, paliers et pics en diamant dispersés | Chambres basales, entailles angulaires et couches |
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

## Composition et coût GPU

`Asset/Shader/ProceduralTerrain/biome_relief.glsl` contient les recettes 2D individuelles. Elles combinent des bruits de gradient, des crêtes, des profils orientés, des cellules et des paliers dont la largeur de transition est réglée. Les métriques cellulaires euclidienne, Manhattan et maximum produisent des masses rondes, en diamant ou carrées ; elles ne nécessitent pas un algorithme Diamond-Square récursif.

`biome_volume.glsl` contient les recettes 3D individuelles. Leurs paramètres sont mélangés dans la passe de colonnes, puis évalués avec des bruits 3D communs. Il n’y a pas huit générateurs de bruit complets exécutés par voxel à la frontière de huit cases. Les fréquences restent fixées dans le repère mondial : interpoler une fréquence puis la multiplier par une position mondiale éloignée ferait varier trop rapidement la phase du bruit.

Une colonne occupe **64 octets**, contre 32 auparavant. Pour un chunk de 64³ cellules avec halo, cela ajoute **143 648 octets par travail GPU**, soit environ 140 KiB. Le champ reste en `float32`, et les colonnes sont partagées par les quatre invocations Y d’un groupe. Cette dépense évite de refaire les mélanges de biomes et les bruits de relief à chaque voxel. Les sommets restent à 12 octets et les champs ne sont pas rapatriés vers le CPU.

Les transitions climatiques sont adoucies avant l’interpolation. La seed, les positions mondiales et les fréquences ne dépendent pas du LOD. Une modification de ces recettes change le monde produit par rapport à la version précédente : reproduire un monde demande la seed, les paramètres **et la même version du générateur**.

## Espaces de déplacement

Les objectifs fournis sont interprétés en mètres : environ **10 m au minimum visé dans les régions denses**, **100 m dans les grandes régions ouvertes**, et davantage dans les déserts. Les grandes formations utilisent un espacement et un déplacement de centre bornés, indépendants du bruit qui sculpte leur profil.

Ces valeurs sont des objectifs de composition. Elles ne garantissent pas encore qu’un personnage peut parcourir partout le monde à sa vitesse maximale : il faudra aussi vérifier les pentes, les transitions entre biomes, les ouvertures 3D, les collisions, la portée du grappin et la géométrie de la capsule. Une fréquence de bruit ne constitue pas à elle seule une largeur minimale de passage.

## Atlas de diagnostic

Ces images sont calculées à partir des shaders de production, avec la seed **1337**, les paramètres par défaut et chaque biome forcé séparément.

- [Atlas des hauteurs](images/biome_height_atlas.jpg) : 1 536 × 1 536 m par case, échantillonnage de 8 m. La palette et l’éclairage sont normalisés par case pour lire les formes ; ce ne sont pas les matériaux du jeu.
- [Coupes du champ 3D](images/biome_density_sections.png) : noir = solide, blanc = vide. Chaque coupe passe par le point culminant échantillonné de sa case ; son Z est indiqué. Les échelles verticales diffèrent entre cases.

Les hauteurs montrent le relief avant sculpture 3D ; les coupes montrent les évidements réellement présents dans le champ. Ces diagnostics ne remplacent pas un parcours avec la caméra et les collisions du jeu. En vue aérienne, une trame cellulaire reste perceptible dans certaines jungles denses forcées : l’implantation des formations est un réglage artistique à poursuivre après le premier essai en jeu.

## Essayer chaque biome

Ouvrir `ENDURANCE/Scene/Terrain.scene`, puis lancer Play. Dans l’interface du composant, **« Biome a tester »** permet de choisir les matrices automatiques ou un biome forcé. **« Regenerer avec ces reglages »** reconstruit le terrain avec les paramètres actuels.

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
