# Les biomes d’ENDURANCE

Les **25 recettes terrestres** décrites ici construisent le terrain par une fonction 2D et une fonction 3D indépendantes pour chaque biome. Le fond marin, ID 25, conserve sa recette antérieure. Cette révision remplace les formes terrestres ; elle conserve la répartition climatique, les identifiants, le pipeline d’accumulation, les LOD et le cache de programmes.

Le périmètre est le terrain solide : silhouettes, pentes, cols, grottes et surplombs. Les noms « forêt », « prairie » ou « mangrove » désignent un futur milieu ; ils n’ajoutent ni végétation, ni eau, ni objets. Les [fiches de conception](BIOME_TOPOGRAPHY_PROPOSAL.md) restent une référence d’intention. Le tableau ci-dessous décrit le code effectivement présent, pas toutes les possibilités de ces fiches.

## Matrice et fichiers à modifier

Les lignes de la matrice vont du très froid au très chaud ; les colonnes, du très sec au très humide. Les IDs se lisent de gauche à droite puis de haut en bas. `BiomeMatrix2D` et `BiomeMatrix3D`, dans [ProceduralTerrainBiomes.hpp](../ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainBiomes.hpp), font autorité. L’axe vertical de la matrice 3D dépend de la **hauteur macro par rapport au rivage**, pas de l’altitude du voxel. Une cavité ne devient donc pas marine simplement parce qu’elle descend sous `seaLevel`.

Chaque dossier [biomes/&lt;Nom&gt;](../Asset/Shader/ProceduralTerrain/biomes/) contient :

| Fichier | Fonction éditable | Responsabilité |
| --- | --- | --- |
| `relief.C.glsl` | `vec4 fieldRelief(vec2 p)` | Relief, épaisseur et décalage de la bande 3D, contrôle local. |
| `volume.C.glsl` | `float fieldVolume(vec3 p, FieldColumn column)` | Contribution signée à la densité : ajouter du solide ou en retirer. |

Les noms de fonction sont identiques entre biomes car chaque fichier constitue son propre programme compute. Conserver `FIELD_BIOME_ID`, les includes de `field_batch_common.glsl` / `field_weights.glsl`, et l’include final d’accumulation. Les 25 recettes terrestres n’utilisent plus un tableau commun de paramètres volumiques ; les constantes `FIELD_VOLUME`, `FIELD_SHAPE` et `FIELD_STYLE` restantes appartiennent uniquement au fond marin inchangé.

La première révision des sept biomes froids et les fréquences **RKM** sont archivées dans [COLD_BIOME_REVISION.md](COLD_BIOME_REVISION.md). La [seconde révision : largeur et placement](COLD_BIOME_SPACING_REVISION.md) documente les quatre premiers biomes. Les réglages polaires ultérieurs et leurs parois volumétriques sont décrits dans [POLAR_ICE_RIFT_3D.md](POLAR_ICE_RIFT_3D.md). Les hauteurs ci-dessous sont relatives au relief local, à `detailScale=0,35`.

## Formes présentes

Le macrorelief global s’ajoute toujours au relief propre au biome. Les sites remarquables apparaissent par endroits ; les sculptures de la dernière colonne ne recouvrent pas toute leur région.

| ID — Nom / dossier | Relief 2D | Sculpture 3D localisée |
| --- | --- | --- |
| **0 — Glace polaire** / `PolarIce` | Cuvettes élargies ; déchirures de 700–900 m, courbure et zéro à trois branches propres à chaque site ; massifs élargis, réglage utilisateur 450–1580 m soumis au plafond global de relief. | Cavités larges et rares en Perlin 3D dans les flancs des massifs ; nappes et tunnels espacés ; parois de faille avec surplombs et renfoncements, bruit étalé ×2,5 ; petites grottes sous le fond. |
| **1 — Toundra gelée** / `FrozenTundra` | Plaines élargies et pingos ; croupes glaciaires de 420–560 m avec cirque de flanc, largeur ×2,5 et espacement ×4. | Pingos ouverts conservés ; petites chambres souterraines avec entrée. |
| **2 — Toundra** / `Tundra` | Grandes ondulations et demi-cirques ; massifs de 800 m à épaules décalées et arêtes courbes, largeur ×5 et espacement ×6. | Ancienne crête abritée ; rochers de 20 m indépendants. |
| **3 — Taïga** / `Taiga` | Collines irrégulières élargies ; longues crêtes asymétriques de 700 m, largeur ×3 et espacement ×4 ; lits fossiles, berges et terrasses. | Arête oblongue et renfoncement antérieurs conservés. |
| **4 — Forêt boréale marécageuse** / `BorealBog` | Bassins et îlots conservés ; cuvettes de lac de 7–14 m. | Surplombs amplifiés et nouvelles cavités Perlin 3D accessibles. |
| **5 — Toundra rocheuse** / `RockyTundra` | Hauteur du relief antérieur multipliée par deux. | Dalles antérieures conservées ; nouvelles grottes ouvertes. |
| **6 — Steppe froide** / `ColdSteppe` | Ondulations conservées ; montagnes à strates de 620 m et failles abruptes de 800 m de profondeur. | Épaule sous-cavée antérieure conservée. |
| **7 — Forêt boréale** / `BorealForest` | Grandes coupoles, vallons et dôme monumental ponctuel. | Deux masses polies soudées, silhouette en dos de roche, fente latérale. |
| **8 — Forêt froide** / `ColdForest` | Fonds de vallée en U, crêtes sur les parois, cirque en croissant à large ouverture. | Épaule haute et balcon épais, alcôve orientée vers l’intérieur du site. |
| **9 — Marécage froid** / `ColdMarsh` | Bassins aplatis, niveaux de berge doux, couronne rompue par deux sorties. | Deux lèvres de berge basses et longues, petit sous-cavage sur l’une d’elles. |
| **10 — Plaine sèche** / `DryPlain` | Plaine alluviale, chenaux secs larges et peu incisés, levées et buttes rares. | Quille allongée ou butte arrondie, une alcôve sur le flanc. |
| **11 — Prairie** / `Meadow` | Collines rondes, grandes ondulations et amphithéâtre ouvert, de proportions variables. | Affleurement couché ou courbé plus haut, renfoncement latéral ; centre ouvert. |
| **12 — Forêt tempérée** / `TemperateForest` | Cuestas asymétriques, longues côtes, selles et paliers traversés par des rampes. | Promontoire en couches épaisses, débord et hauteur variables, abri latéral. |
| **13 — Forêt tempérée humide** / `TemperateRainforest` | Vallées courbes continues, épaules à crêtes, banquettes et amphithéâtres. | Grand auvent arrondi sur des contreforts décentrés, alcôve largement ouverte. |
| **14 — Marais tempéré** / `TemperateMarsh` | Méandres peu profonds, levées et grandes plaines calmes, croissant alluvial rare. | Berge fossile courbe, lèvre en surplomb et sous-cavage peu profond ; un arc reste ouvert. |
| **15 — Désert de roche** / `RockDesert` | Sol ouvert et groupes rares de lames orientées, profils asymétriques et gradins. | Selon la variante, grande lame penchée et courbée ; ouverture latérale plus rare. |
| **16 — Savane sèche** / `DrySavanna` | Plaine ondulée, groupes compacts de trois dômes inégaux et pieds adoucis. | Selon la variante, masses arrondies soudées en kopje ; porche occasionnel. |
| **17 — Savane** / `Savanna` | Massifs doubles isolés, selles larges, épaules et crêtes combinées à deux échelles. | Couronne rocheuse asymétrique sur contrefort, avec alcôve, sur une partie des sites. |
| **18 — Jungle sèche** / `DryJungle` | Tables stratifiées isolées, sommet irrégulier et secteur de rampe, vallées ouvertes. | Dalle inclinée portée par un pied rocheux sur le bord de table ; cavité facultative. |
| **19 — Jungle tropicale** / `TropicalJungle` | Groupes de tours karstiques inégales, piédestal et bassin continu entre les sites. | Tours épaissies et penchées sur un site sélectionné, grand portail occasionnel. |
| **20 — Désert volcanique** / `VolcanicDesert` | Nappes à gradins, langues de coulée, bouclier ou caldera avec une brèche. | Coulée lobée surélevée, traversée par un tube horizontal courbe. |
| **21 — Désert rouge** / `RedDesert` | Dunes composées et asymétriques, phase déformée, zone de sable abaissée au pied des sites. | Grande arche de grès ou fenêtre plus haute, pied et strates épaisses. |
| **22 — Plateau aride** / `AridPlateau` | Mesas en couches, plis, vallées et larges selles traversant les tables. | Contrefort portant une dalle débordante, évidée latéralement en alcôve. |
| **23 — Jungle brûlante** / `BurningJungle` | Massifs à crêtes modulés par de grandes régions calmes, lignes de relief déformées. | Deux contreforts minéraux tournant avec la hauteur, nervures et galerie locale. |
| **24 — Mangrove tropicale** / `TropicalMangrove` | Chenaux et branches peu profonds, levées basses et terrasse côtière ponctuelle. | Nappe minérale basse avec lèvre et alcôve de rive ; pas de racines générées. |
| **25 — Fond marin** / `Marine` — inchangé | Plateaux négatifs, canyons orientés et reliefs sous-marins isolés. | Recette antérieure de cavités et strates modulées par le relief marin. |

Ces formes sont des interprétations artistiques. Le relief à crêtes n’est pas une simulation d’érosion ; les chenaux ne constituent pas un réseau hydrologique calculé. Les variantes, tailles, orientations et absences réduisent la répétition, sans garantir des monuments tous uniques.

## Contrat 2D et unités

`p` est la position **XZ mondiale en mètres**, indépendante du chunk et du LOD. La seed reste déterministe. La sortie de `fieldRelief()` est bornée par `fieldClampRelief()` :

| Composante | Plage brute | Utilisation |
| --- | --- | --- |
| `.x` | −4096 à 4096 | Hauteur ajoutée au macrorelief, multipliée par `detailScale`. |
| `.y` | 0 à 8192 | Épaisseur **totale** de la bande 3D, multipliée par `detailScale`. Aucune épaisseur artistique de base n’est ajoutée. |
| `.z` | −160 à 160 | Décalage vertical de recette, multiplié par `detailScale`, puis associé au décalage global en mètres. |
| `.w` | 0 à 1 | Contrôle propre à la recette. Les nouveaux biomes terrestres y exposent le masque de site ; le pipeline ne l’applique pas secrètement à la densité. |

Avec `detailScale = 0,35`, une hauteur brute de 100 ajoute **35 m** et une bande de 576 couvre **201,6 m au total**. La gamme étendue autorise jusqu’à **2867,2 m**, utilisée localement près des parois polaires. Le stockage de 32 octets conserve la précision antérieure pour les bandes ≤576 grâce à un indicateur de gamme séparé. Les dimensions XZ ne sont pas multipliées par ce réglage. Modifier l’épaisseur de bande change où la 3D est autorisée ; cela ne change ni la fréquence de son bruit ni l’espacement des sites.

L’accumulation interpole les sorties 2D, puis la finalisation applique l’échelle et compacte les contrôles. `column.terrain.x` contient ensuite la hauteur mondiale finale ; `.w`, l’épaisseur totale en mètres. `column.blend.y` contient le décalage global en mètres, `.z` le décalage brut de recette, et `.w` le contrôle supplémentaire interpolé. Une bande nulle en biome isolé ferme la 3D ; à une transition, les bandes voisines contribuent également.

## Outils communs, compositions indépendantes

Les helpers fournissent des opérations, pas une recette imposée. [biome_landforms_common.glsl](../Asset/Shader/ProceduralTerrain/biome_landforms_common.glsl) contient `LandSite` et son placement par hash ; [biome_landforms_2d.glsl](../Asset/Shader/ProceduralTerrain/biome_landforms_2d.glsl) contient les compositions de relief. Le helper 3D inclut directement le placement commun : il ne charge pas la bibliothèque de bruits 2D.

| API | Effet |
| --- | --- |
| `landFbm(p, salt)` | Simplex signé, trois octaves, lacunarité 2,03 et gain 0,48. Les coordonnées sont déjà mises à l’échelle par l’appelant. |
| `landRidges(p, salt)` | Trois octaves à crêtes, mises au carré ; la crête précédente module le détail suivant. |
| `landWarp(p, wavelength, amplitude, salt)` | Déformation XZ par deux Simplex ; longueur d’onde et amplitude exprimées en mètres. |
| `landTerrace(height, stepHeight, softness)` | Paliers continus ; `stepHeight` est une hauteur brute. De 0,05 à 0,5, `softness` élargit les raccords ; au-delà, elle mélange progressivement vers la pente initiale, retrouvée à 1. |
| `landSite(p, spacing, salt, occupancy)` | Site stable : centre mondial, coordonnées locales XZ tournées `q`, rayon, masque compact et variante. |
| `landRkmSite(p, rkm, salt)` | Une population indépendante, intervalle nominal `1000/rkm` mètres par cellule ; une trajectoire ne rencontre pas nécessairement chaque site. |
| `landSparseSite(p, spacing, footprintScale, salt, occupancy)` | Placement tourné et déformé, largeur indépendante ; `center` est dans le domaine déformé, jamais une position mondiale. |
| `landPlacementValue(p, salt)` | Bruit scalaire lisse entre 0 et 1, partagé entre relief et volume sans dépendance de bibliothèque 2D/3D. |
| `landMountain(site, peakMetres, salt)` | Profil pointu, contreforts ridged et fBm déformé ; sommet en mètres à `detailScale=0,35`. |
| `landBand(site, rawThickness)` | Bande nulle hors site, ouverture progressive à son bord, épaisseur brute bornée à 576. |

Les implantations initiales utilisent des cellules de **1,6 à 2,9 km**, avec des cellules vides. Les populations utilisant `landRkmSite` emploient des cellules de **0,5 à 4 km**, toutes candidates, avec des sels indépendants. Les montagnes des IDs 0–3 et les failles/nappes polaires utilisent désormais `landSparseSite`, décrit ci-dessous. `occupancy` est une probabilité de présence par cellule, pas le pourcentage de sol sculpté. Le rayon varie de 20 à 28 % de `spacing` et le centre bouge au maximum de 16 % de cette taille par axe. Le support reste donc à l’intérieur de sa cellule, avec une marge minimale de 6 % par bord. Les sites d’une même implantation ne se recouvrent pas ; des biomes différents peuvent néanmoins contribuer ensemble aux transitions.

Le masque vaut 1 jusqu’à 64 % du rayon, puis descend continûment à zéro. La grille sélectionne le candidat mais ne crée pas de marche de terrain à sa frontière. Aucun parcours de cellules voisines ni buffer de sites n’est nécessaire. **Conserver exactement les mêmes arguments de `landSite()` dans les deux fichiers du biome**, et distinguer l’espacement, le rayon de la forme et la fréquence du bruit sculptant sa surface.

Pour `landSparseSite`, l’espacement de 6 à 20 km est nominal dans un domaine tourné par seed puis déformé par deux échelles de bruit lisse. La largeur se règle séparément par `footprintScale` : rayon = `spacing × mix(0,20, 0,28, variant) × footprintScale`. Le décalage des centres augmente en tenant compte de ce rayon, avec une marge de 2,5 % de cellule ; 20 % des sites de montagnes restent vides. Le masque reste compact et continu. La déformation ne se replie pas (borne de la perturbation jacobienne < 0,891), mais modifie les distances physiques : les multiplicateurs ne garantissent pas un minimum centre-à-centre au mètre près. `center` appartient au domaine déformé ; utiliser directement `q` pour sculpter, sans traiter `center` comme une position mondiale. Relief et volume doivent appeler exactement la même population. Les recettes anciennes et leurs placements restent inchangés.

## Construction du volume

[biome_landforms_3d.glsl](../Asset/Shader/ProceduralTerrain/biome_landforms_3d.glsl) ajoute les primitives et le contrat de bande. `landVolumeEnabled(column)` rejette les modes analytiques, un `detailScale` nul et une bande fermée. Les recettes quittent aussi immédiatement les sites de masque nul ; plusieurs biomes chauds réservent leur 3D à certaines variantes.

`landVolumeLocal(p, column, site)` renvoie un repère volontairement mixte : X et Z restent les coordonnées locales du site **en mètres de son domaine de placement** (mondial ou déformé), tandis que Y est une hauteur **brute**, relative au terrain et au centre de bande :

```glsl
rawY = (p.y - column.terrain.x - column.blend.y) / uDetailScale
       - clamp(column.blend.z, -160.0, 160.0);
```

Une demi-hauteur Y de 40 produit donc environ 14 m à l’échelle 0,35 ; un rayon X de 40 reste 40 m. La géométrie suit la hauteur locale du terrain. Cette construction ne fournit pas une distance euclidienne mondiale exacte.

`landEllipsoid`, `landBox` et `landCapsule` donnent un champ **positif à l’extérieur** de leur forme. Le terrain utilise la convention inverse : **positif = solide**, **négatif = vide**. `landUnion(a, b, k)` est un maximum adouci pour deux densités solides. Exemple de composition :

```glsl
float base = column.terrain.x - p.y;
float target = max(base, -solidShape * uDetailScale); // ajout
target = min(target, cavityShape * uDetailScale);    // creusement
return landDelta(p, column, target, site.mask);
```

`fieldVolume()` renvoie uniquement le **delta** : l’accumulateur écrit déjà la base une fois, puis ajoute `poidsLocal * delta` pour chaque biome actif. `landDelta()` applique explicitement le masque du site, adoucit la contribution entre 76 et 98 % de la demi-bande, puis revient à zéro. Il n’interprète pas `column.blend.w`.

Les nouvelles recettes terrestres utilisent un décalage de recette nul ; `bandOffset` reste utilisable. Toute modification doit garder la densité de base hors de l’enveloppe définie par `fieldVolumeEnvelope()` : décalage brut dans ±160, épaisseur totale au maximum 8192 (le helper classique `landBand` reste limité à 576), avec l’échelle et le décalage global du générateur. Agrandir une forme au-delà de ces bornes demande de modifier aussi les rejets GPU et les bornes CPU ; augmenter seulement son rayon Y peut la tronquer.

## Accumulation, compilation et coût

Les poids restent locaux à chaque colonne XZ, selon humidité, température et hauteur macro. Ils ne deviennent pas un ratio constant par chunk. Le GPU construit les listes de travaux par biome ; chaque programme traite ses chunks par dispatch indirect. Les résultats 2D, puis les deltas 3D, sont accumulés dans l’ordre des IDs avec des barrières entre passes, sans atomiques flottants ni champ dense supplémentaire par biome.

Cette révision conserve le catalogue fixe des **26 programmes 2D et 26 programmes 3D**, les passes communes et le cache binaire persistant. Elle ne réintroduit aucune compilation par combinaison de biomes ou pendant un déplacement. Le service moteur de compilation parallèle prépare les programmes manquants au démarrage ou à la régénération ; son parallélisme dépend du pilote. Le détail est dans [PROCEDURAL_TERRAIN.md](PROCEDURAL_TERRAIN.md#programmes-et-rechargement-des-shaders).

Éditer une recette invalide son programme seulement ; éditer un include commun invalide ses dépendants. La bibliothèque de bruits 2D concerne la classification et les 26 reliefs ; celle de bruits 3D concerne les 26 volumes. Les hashes communs concernent les deux. Modifier la seed ou un uniform conserve la clé de code. Les nouvelles formes n’ajoutent ni stockage de sites ni canal de densité. Leur coût dépend des bruits réellement évalués, de la couverture des bandes, du mélange des biomes et du nombre de triangles ; une diminution des évaluations hors site ne constitue pas à elle seule une mesure de débit sur la carte cible.

## Réglage et validation

1. Ouvrir `ENDURANCE/Scene/Terrain.scene`, lancer Play et sélectionner le biome dans **« Biome a tester »**.
2. Modifier sa fonction 2D ou 3D et enregistrer le fichier. Conserver les unités et l’enveloppe ci-dessus.
3. Cliquer sur **« Regenerer avec ces reglages »** : les sources et includes sont relus, les binaires valides réutilisés et les programmes modifiés reconstruits.
4. Vérifier plusieurs sites et seeds, puis les raccords avec les biomes voisins en mode automatique. Contrôler aussi les LOD éloignés : une ouverture fine peut disparaître quand la taille de cellule augmente.

Le personnage de référence mesure **1,8 m**. Les pentes longues, fonds de vallée, sorties des cirques et grandes zones sans sculpture servent le déplacement rapide. Les objectifs de conception restent des couloirs principaux de l’ordre de 80–200 m, avec des passages locaux plus serrés et de grands espaces désertiques. Les dimensions de cellules de placement ne garantissent pas ces largeurs praticables : le macrorelief et le mélange des biomes peuvent les modifier.

Les sondes GPU vérifient les valeurs finies, la seed, la continuité, les ajouts/retraits de solide et l’absence de contribution hors bande. Elles mesurent aussi pente et connexité sur une grille de hauteurs. **Ce sont des indicateurs**, pas une preuve de traversabilité du maillage 3D : il reste à éprouver la capsule, les collisions, la visibilité des obstacles, les réceptions, les virages à vitesse réelle et le grappin en jeu. Une pente mesurée à 32 m de pas peut masquer un détail plus fin.

Le protocole, les images et les limites des mesures de cette révision sont regroupés dans [BIOME_REBUILD_VALIDATION.md](BIOME_REBUILD_VALIDATION.md). Les commandes générales restent dans [Tests/Terrain/README.md](../Tests/Terrain/README.md).

## Sources de conception

Les sources naturelles guident les silhouettes ; les recettes sont des constructions originales adaptées à la traversée, sans simulation géologique complète.

- [NPS — Permafrost](https://www.nps.gov/im/arcn/permafrost.htm) : sols polygonaux et pingos, repris comme formes basses et dômes.
- [NPS — Outwash Plains and Eskers](https://www.nps.gov/articles/outwashplainsandeskers.htm) : plaines et cordons sinueux, inspirant les régions froides.
- [NPS — Geology of Mesa Verde](https://www.nps.gov/meve/learn/nature/geology.htm) : cuestas et couches rocheuses pour les profils asymétriques.
- [NPS — River Systems and Fluvial Landforms](https://www.nps.gov/subjects/geology/fluvial-landforms.htm) et [Meandering Stream](https://www.nps.gov/articles/meandering-stream.htm) : plaines, levées, méandres et berges.
- [NPS — Aeolian Landforms](https://www.nps.gov/subjects/geology/aeolian-landforms.htm) : dunes asymétriques et yardangs orientés.
- [NPS — Lava Flow Forms](https://www.nps.gov/articles/000/lava-flow-forms.htm) : nappes, lobes et tubes volcaniques.
- [National Museum of the Philippines — Geology of Karst Forests](https://www.nationalmuseum.gov.ph/2022/10/06/geology-of-karst-forests/) : tours isolées, falaises et cavités.
- [Red Blob Games — Making maps with noise functions](https://www.redblobgames.com/maps/terrain-from-noise/) : combinaison de fréquences, redistribution, crêtes et terrasses.
- [Ryan Geiss / NVIDIA — Generating Complex Procedural Terrains Using the GPU](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu) : densité implicite, surfaces et volumes impossibles à décrire par une seule carte de hauteur.

Les [références naturelles complémentaires](BIOME_REFERENCES_NATURE.md) et les [références artistiques](BIOME_REFERENCES_ART.md) conservent le contexte de recherche initial.
