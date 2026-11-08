# Les biomes d’ENDURANCE

La matrice reprend les **25 cases de l’image de référence**, avec une fonction de relief 2D et un jeu de paramètres volumiques 3D pour chacune. Un 26e biome décrit le fond marin. Les noms de forêt, de prairie ou de mangrove désignent ici le terrain destiné à accueillir ces milieux ; la végétation sera un système séparé.

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
| 0 — Glace polaire | Distance au carré au site de Voronoï le plus proche, domaine déformé et ondulations superposées | Petites cavités angulaires enterrées |
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

Le relief 2D reste dans **`fieldReliefPolarIce()`**, dans `biome_relief.glsl`. Les derniers réglages conservés sont :

| Réglage | Valeur | Effet |
| --- | ---: | --- |
| `POLAR_BASIN_SPACING` | 64 m | Échelle des cellules du Voronoï. |
| `POLAR_BASIN_WARP` | 1,618 | Déformation lente des coordonnées du Voronoï. |
| `POLAR_ROLL_AMPLITUDE` | 18 | Amplitude des grandes ondulations avant `detailScale`. |
| `POLAR_FINE_AMPLITUDE` | 2,5 | Amplitude du bruit fin avant `detailScale`. |

Le code actif prend la distance au carré au site le plus proche (`sites.x`), multipliée par 10, puis ajoute les deux bruits. `POLAR_BASIN_DEPTH` (14) et `POLAR_BASIN_FLOOR` (0,06) ne servent qu’à l’ancienne formule laissée en commentaire ; les modifier ne change pas le profil actif. Le macrorelief global de la scène s’ajoute toujours au résultat.

`fieldReliefPolarIce()` renvoie actuellement `vec4(h, 241, -40, 0)` :

| Composante | Rôle |
| --- | --- |
| `.x` | Hauteur ajoutée au macrorelief, multipliée par `detailScale`. |
| `.y` | Épaisseur **totale** de la bande 3D, multipliée par `detailScale`. Zéro désactive la bande locale. |
| `.z` | Décalage vertical supplémentaire de cette bande, combiné au décalage volumique avant `detailScale`. |
| `.w` | Masque de roche entre 0 et 1, modulant les amplitudes du champ commun ; 0 ne désactive pas la 3D. |

L’épaisseur n’a plus de base ni de variation globale ajoutée : `épaisseur = relief.y × detailScale`, après interpolation des biomes. Les 26 valeurs `.y` sont maintenant des épaisseurs complètes, bornées entre 0 et 576 avant multiplication. Avec `detailScale = 0,35`, la glace polaire possède donc une bande de 84,35 m ; mettre son `.y` à zéro la ferme complètement lorsqu’elle est le seul biome contributeur. À une transition, les épaisseurs des biomes voisins se mélangent normalement.

Les nouvelles valeurs ont été ajustées pour approcher l’ancienne épaisseur moyenne avec les réglages par défaut (base 64 m, variation 0–16 m, `detailScale = 0,35`). Cette conversion est inscrite dans chaque valeur littérale du biome ; aucune addition cachée n’est conservée. L’ancien bruit global de variation disparaît, donc la géométrie près des bords de bande peut légèrement changer. Pour une épaisseur variable, écrire directement une expression de bruit dans le `.y` du biome ; ses zones à zéro restent sans 3D.

Les réglages 3D se trouvent à la **ligne de biome 0** de `FIELD_BIOME_VOLUME`, dans `biome_volume.glsl` :

```glsl
vec4[3](vec4(-20, 3, 72, .40),  // volume
        vec4(.35, .50, .15, .10), // shape
        vec4(.85, .70, .15, .30)) // style
```

Ces valeurs reprennent la dernière recette polaire. Il n’y a plus de fonction `fieldVolumePolarIce()` ni de masque d’activité polaire ajouté à ses paramètres. Pour modifier seulement la glace, éditer cette ligne ; pour modifier le calcul 3D de tous les biomes, éditer `fieldDensityAtPrepared()` dans le même fichier.

## Paramètres 3D communs

`FIELD_BIOME_VOLUME[26][3]` est un tableau à deux dimensions de `vec4` : une ligne par identifiant de biome, trois groupes de quatre valeurs par ligne. Cela représente **312 floats, soit 1 248 octets de données logiques** intégrées au shader. Le stockage effectif des constantes dans le programme compilé dépend du pilote ; aucun nouveau SSBO n’est ajouté.

| Groupe | Composante | Signification avant préparation de la colonne |
| --- | --- | --- |
| `volume` (`[0]`) | `.x` | Décalage vertical supplémentaire en mètres avant `detailScale`. Négatif : bande plus enfouie. |
| | `.y` | Amplitude de déformation du champ de hauteur, avant `detailScale` et le masque de roche. |
| | `.z` | Échelle de densité du creusement, avant `detailScale` et le masque de roche. Ce n’est pas l’espacement des grottes. |
| | `.w` | Fraction de la demi-épaisseur utilisée pour adoucir les bords de la bande, normalement entre 0 et 1. |
| `shape` (`[1]`) | `.x` | Poids du premier bruit 3D, isotrope. |
| | `.y` | Poids du deuxième bruit, variant plus vite verticalement. |
| | `.z` | Poids du troisième bruit, variant plus vite horizontalement. |
| | `.w` | Seuil de rayon des cavités dans l’espace des valeurs du bruit ; ce n’est pas un rayon en mètres. |
| `style` (`[2]`) | `.x` | Mélange de cavités en feuillets (0) vers des tubes (1). |
| | `.y` | Section des tubes : arrondie (0), en diamant (0,5), carrée (1). |
| | `.z` | Réintroduit une part du premier bruit dans la déformation de surface, entre 0 et 1. |
| | `.w` | Intensité de la modulation du rayon par les strates, entre 0 et 1. |

Les poids `shape.xyz` décrivent un mélange, mais ne sont pas normalisés automatiquement. Les amplitudes `volume.yz` et le rayon `shape.w` doivent rester non négatifs. Les fréquences et les seeds des trois bruits sont communes, dans l’évaluateur 3D. Changer leur fréquence change à la fois l’échelle des formes et leur espacement ; les douze paramètres ne décrivent pas des centres de grottes indépendants.

Pour désactiver la sculpture d’un biome, mettre `volume.y` et `volume.z` à zéro. Son relief 2D reste actif. À une frontière avec un biome actif, l’interpolation des paramètres réintroduit progressivement la sculpture.

## Composition et coût GPU

Les 26 fonctions 2D de `biome_relief.glsl` restent indépendantes. Elles combinent les bruits de gradient, cellules euclidiennes ou angulaires, profils orientés et paliers. `fieldSurfaceRecipes()` regroupe les identifiants de poids positif avant de les appeler : au maximum quatre coins climatiques et une contribution marine. Ce chemin conserve son `switch` 2D.

Le chemin 3D utilise **un seul calcul de densité par voxel**. `fieldVolumeParameters()` lit les paramètres dans le tableau et les interpole avec les poids trilinéaires locaux. `fieldPrepareVolume()` prépare ensuite le centre et la demi-épaisseur de bande, les amplitudes et la distance d’adoucissement. La passe `field_density.C.glsl` fait cette préparation une fois par colonne XZ de son groupe, puis la partage entre les quatre invocations Y. `fieldDensityAtPrepared()` évalue le champ commun avec ces paramètres. Hors de la bande ou quand les deux amplitudes sont nulles, il évite les trois bruits 3D.

Les poids dépendent toujours de l’humidité, de la température et de la hauteur macro **à cette colonne**. Ils ne deviennent pas un pourcentage constant par chunk. Les fonctions 3D individuelles, leur grand `switch` et leur cache de bruits paresseux ont disparu. Le gain structurel vient du calcul volumique unique et de la préparation partagée ; un accès de tableau n’est pas, à lui seul, une garantie de meilleur débit sur chaque GPU.

**Le mélange porte désormais sur les paramètres, puis la densité est calculée.** L’évaluateur étant non linéaire, les formes des zones de transition peuvent différer de l’ancien mélange des densités. Les paramètres des 26 biomes isolés sont conservés ; de petits écarts d’arrondi restent possibles. La seed, les positions mondiales et les fréquences ne dépendent pas du LOD. Reproduire un monde demande la seed, les réglages et la même version du générateur.

Une colonne persistante reste à **48 octets**, soit 215 472 octets pour 67² colonnes. Son ancien mot d’activité polaire est réservé à zéro. La préparation partagée conserve **1 792 octets par groupe** : 32 colonnes × (un `vec2` + trois `vec4`). Aucun buffer de paramètres par voxel ou par chunk n’est ajouté. La table CPU des cellules reste à 1 KiB ; les densités restent en `float32`, les compteurs de meshing à 16 octets par travail et les sommets à 12 octets.

Les bruits restent séparés en includes 2D et 3D. Chaque passe garde son programme générique ; aucun déplacement dans le monde ne lance de compilation. Les services de compilation parallèle du moteur restent disponibles, mais le terrain ne les utilise pas. Les six programmes compute passent par `Ge::ComputeShader`, les pipelines de dessin et d’ombres par `Ge::GraphiquePipeline`, avec leur cache binaire persistant commun.

**Regenerer avec ces reglages** relit les sources et includes, recharge les binaires correspondants ou recompile les programmes modifiés, puis reconstruit les chunks. Éditer `FIELD_BIOME_VOLUME` ne change que la dépendance de la passe de densité ; éditer `biome_relief.glsl` ne concerne que les colonnes. Les fichiers communs concernent les deux. Modifier seulement la seed ou les uniforms conserve la clé de cache. Une erreur GLSL conserve son journal et les fichiers/lignes correspondants ; corriger puis régénérer permet de réessayer. Les détails du cache sont décrits dans [PROCEDURAL_TERRAIN.md](PROCEDURAL_TERRAIN.md#programmes-et-rechargement-des-shaders).

## Contrat du champ 3D

Le champ est positif dans le solide, négatif dans le vide ; zéro définit la surface. Ce n’est pas une distance signée. L’évaluateur commun revient continûment à `hauteur - p.y` hors de la bande. `fieldPrepareVolume()` borne la somme du décalage de relief et de `volume.x` à **[-160 ; 160] × `uDetailScale`**, puis ajoute le décalage global. L’épaisseur totale reste celle calculée par la passe 2D.

Modifier l’évaluateur commun doit conserver ces bornes, ou adapter ensemble le rejet de bande et `fieldHeightBounds()` côté C++. Les bruits utilisent le repère mondial et la seed commune ; ils ne doivent pas dépendre de l’origine du chunk, du LOD ou du temps. Les tests de raccords, de champs finis et d’interpolation décrits dans `Tests/Terrain/README.md` accompagnent toute modification des paramètres ou de leurs bornes.

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

Le chemin générique est le seul chemin de génération. Le bouton de régénération recharge les fonctions 2D, la table de paramètres 3D et l’évaluateur commun, sans option de spécialisation ni cache de variantes à régler.

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
