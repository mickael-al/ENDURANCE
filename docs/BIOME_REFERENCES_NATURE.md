# Références naturelles pour les biomes d’ENDURANCE

Recherche effectuée le 7 novembre 2026. Ce document distingue les observations géomorphologiques sourcées des propositions de génération et de parcours. Les propositions ne prétendent pas simuler l’érosion, ni décrire une propriété obligatoire de chaque biome terrestre.

La matrice température × humidité décrit d’abord un climat et un écosystème. Elle ne suffit pas à déterminer le sous-sol : une forêt peut pousser sur du granite, du calcaire ou du basalte. Pour donner une silhouette reconnaissable aux 25 cases, ENDURANCE peut leur associer une famille de relief dominante, tout en conservant le mélange entre régions. Les arbres, racines et autres éléments de végétation ne sont pas des formes à fabriquer avec le champ de terrain.

## Douze références primaires

| ID | Référence | Formes retenues pour la conception |
| --- | --- | --- |
| N1 | [NPS — Permafrost](https://www.nps.gov/im/arcn/permafrost.htm) | Sols polygonaux, collines à cœur de glace, dépressions et affaissements liés au dégel. Ces formes donnent une autre lecture du froid que des montagnes uniformément couvertes de neige. |
| N2 | [NPS — Glacier Landforms](https://www.nps.gov/articles/series.htm?id=F6B94C83-1DD8-B71B-0B7447F900839E48) | Vallées en U, cirques, arêtes, moraines, collines allongées et cordons de sédiments. Alternance utile entre fonds roulants et parois d’accroche. |
| N3 | [NPS — Arid and Semi-arid Region Landforms](https://www.nps.gov/subjects/geology/arid-landforms.htm) | Plateaux tabulaires, buttes, aiguilles, canyons, inselbergs, cônes alluviaux et bassins plats. Le désert n’est donc pas limité aux dunes. |
| N4 | [NPS — Dune Types, Great Sand Dunes](https://www.nps.gov/grsa/learn/nature/dune-types.htm) | Crêtes liées à des vents opposés, dunes en étoile et dunes en croissant. L’orientation et le profil de la crête comptent autant que la variation de hauteur. |
| N5 | [UNESCO — Wulingyuan](https://whc.unesco.org/en/list/640/) | Grands piliers de grès, gorges et ponts naturels. Référence de verticalité et de lecture à longue distance, sans exiger une origine fantastique. |
| N6 | [UNESCO — South China Karst](https://whc.unesco.org/en/list/1248) | Tours, cônes, pinacles, grandes dépressions, grottes et ponts naturels. Référence pour les reliefs humides très découpés et les volumes traversables. |
| N7 | [NPS — Columnar Jointing](https://www.nps.gov/subjects/volcanoes/columnar-jointing.htm) | Fractures prismatiques dues au refroidissement ; colonnes souvent hexagonales, mais pas exclusivement. Référence pour des parois minérales anguleuses. |
| N8 | [NPS — Lava Flow Forms](https://www.nps.gov/articles/000/lava-flow-forms.htm) | Contraste entre nappes lisses ou cordées, surfaces fragmentées, chenaux et tubes. Les détails agressifs peuvent rester visuels, tandis que de grandes nappes servent au déplacement. |
| N9 | [NPS — River Systems and Fluvial Landforms](https://www.nps.gov/subjects/geology/fluvial-landforms.htm) | Plaines alluviales, terrasses, levées, méandres et chenaux tressés. Référence pour des passages longs et des changements de niveau lisibles. |
| N10 | [NOAA — Estuarine Habitats](https://oceanservice.noaa.gov/education/tutorial_estuaries/est06_habitats.html) | Vasières, chenaux de marée, bassins, berges, marais et mangroves. Le relief d’un marais peut être subtil ; ses différences d’altitude n’ont pas besoin de devenir des murs. |
| N11 | [NPS — Mammoth Hot Springs, Lower Terraces](https://www.nps.gov/places/000/mammoth-hot-springs-lower-terraces.htm) | Dépôts de travertin en terrasses, alimentés par des fractures. Référence à agrandir et simplifier pour des marches minérales monumentales. |
| N12 | [NOAA Ocean Exploration — Canyons and Seamounts](https://oceanexplorer.noaa.gov/expedition-feature/okeanos-ex1905-background-canyons-seamounts/) | Talus continental entaillé de canyons, reliefs sous-marins et transitions entre plateau peu profond et grands fonds. La mer n’implique pas un fond plat. |

## Vocabulaire procédural proposé

Les combinaisons suivantes sont des propositions d’implémentation originales inspirées par les formes ci-dessus, pas des algorithmes extraits de ces sources.

- **Relief large** : une ou deux octaves 2D, éventuellement étirées suivant une direction ; elles définissent les pentes qui portent le mouvement.
- **Crêtes** : transformation `1 - abs(noise)` avec contrôle du profil. Une direction dominante et une déformation lente évitent l’aspect de bosses sans organisation.
- **Cellules** : distance Voronoï et différence des deux premières distances pour séparer noyaux et frontières. Les métriques euclidienne, Manhattan et maximum donnent respectivement des formes rondes, en diamant ou carrées. Elles servent de briques géométriques ; ce choix ne nécessite pas le générateur récursif Diamond-Square.
- **Terrasses** : quantification de la hauteur avec une largeur de transition contrôlée. Des paliers peuvent être francs sur les monuments, mais les voies rapides gardent des rampes. La fréquence du bruit ne garantit pas à elle seule une largeur minimale de palier.
- **Découpe 3D** : masques de cavernes, chenaux souterrains ou entailles latérales appliqués dans la bande verticale du biome. Les profils horizontaux et les paramètres de bande sont précalculables par colonne.
- **Monuments** : piliers, tables ou voûtes limités à une enveloppe de placement. Leur espacement est indépendant de la turbulence qui sculpte leur surface.

## Déclinaison des 25 cases

Les distances sont des cibles de conception en mètres, à vérifier avec le contrôleur et le grappin. Elles décrivent les grands passages et motifs, pas un maillage de micro-obstacles. La taille des régions climatiques doit rester sensiblement plus grande que celle des motifs.

| Case de la matrice | Fonction de relief 2D proposée | Fonction volumique 3D proposée | Lecture et parcours visés | Références |
| --- | --- | --- | --- | --- |
| Glace polaire | Grandes nappes ondulées et crêtes orientées ; peu de détail géométrique | Fissures profondes rares et grandes voûtes sous une dalle épaisse ; relief cristallin réservé aux bordures | Horizons de 300–800 m, rampes longues ; masses de glace comme points d’accroche | N1, N2 |
| Toundra gelée | Réseau polygonal très atténué et dômes isolés | Entailles entre dalles, creux sous quelques dômes ; majorité du sol pleine | Circulation entre motifs de 60–120 m ; éviter un quadrillage de tranchées permanentes | N1 |
| Toundra | Collines larges et bassins peu profonds, alternance douce de deux bruits | Petits renfoncements au pied de rares affleurements ; très faible porosité | Espaces de 100–200 m et pentes de relance douces | N1, N2 |
| Taïga | Collines allongées suivant un axe et vallées larges | Arches rares dans des verrous rocheux ; bande enterrée sous les dos arrondis | Axes de course de 80–160 m, belvédères latéraux ; arbres ajoutés séparément | N2 |
| Forêt boréale marécageuse | Bassins cellulaires bas et cordons minéraux sinueux | Sous-cavages peu profonds dans les cordons ; pas de cavernes systématiques sous chaque bassin | Passages minéraux de 16–32 m entre grandes cuvettes | N1, N2, N10 |
| Toundra rocheuse | Crêtes anguleuses, cirques et larges fonds en U | Diaclases et grandes ouvertures latérales ponctuelles dans les arêtes | Fonds roulants de 40–100 m, parois hautes pour le grappin | N2 |
| Steppe froide | Ondulation anisotrope très large et tables basses peu nombreuses | Découpe faible ; quelques abris dans les talus | Champs libres de 120–240 m avec ondulations de 150–400 m | N2, N3 |
| Forêt boréale | Dos rocheux arrondis, chapelet de bassins et cordons de relief | Cavités isolées sous les reliefs, entrées près des vallées | Clairières géologiques de 40–100 m, séquences de bosses plutôt que murs | N2 |
| Forêt froide | Vallées encaissées, arêtes et terrasses rocheuses froides | Cavernes dans les flancs ; ponts rares au-dessus des vallées | Couloirs de 20–50 m, ouvertures plus vastes entre les chaînes | N2, N6 |
| Marécage froid | Dépressions de thermokarst et îlots faiblement surélevés | Bords de bassins affaissés et renfoncements très localisés | Sol bas peu accidenté, cheminements de 16–32 m ; les creux ne sont pas tous des trous profonds | N1, N10 |
| Plaine sèche | Longues ondulations à faible amplitude, chenaux secs peu profonds | Gros blocs isolés faiblement évidés ; presque aucun creusement sous la plaine | Espaces de 150–300 m, visibilité et vitesse | N3, N9 |
| Prairie | Collines souples, rubans de vallée et terrasses alluviales basses | Rares ponts dans les bordures rocheuses des vallées | Grandes courbes de 100–200 m ; pentes servant de tremplins naturels | N9 |
| Forêt tempérée | Collines mêlées à des paliers irréguliers, vallées continues | Grottes et surplombs dans les escarpements, plateaux supérieurs conservés | Passages de 24–64 m, alternance roulage et balancement | N9, N6 |
| Forêt tempérée humide | Grands plateaux incisés, gradins minéraux et bassins | Grandes arches et cavités sous les plateaux ; bande plus épaisse et plus basse | Passages denses de 16–40 m, clairières de 80–160 m | N6, N11 |
| Marais tempéré | Plaines très basses, méandres et levées douces | Berges légèrement évidées ; densité pleine ailleurs | Chenaux et berges de 20–50 m ; éviter de convertir la végétation en piliers de terrain | N9, N10 |
| Désert de roche | Grandes nappes de sable ou de graviers, cellules de mesas et inselbergs | Colosses évidés latéralement, arches et abris dans les grands monuments | Espaces de 200–500 m ; monuments de 80–250 m de haut comme ancres | N3, N5 |
| Savane sèche | Ondulations sèches, cônes alluviaux simplifiés et buttes rares | Sous-cavages localisés dans les buttes ; terrain de liaison plein | Espaces de 120–240 m et grandes pentes convexes | N3 |
| Savane | Grandes collines avec groupes de dômes rocheux isolés | Fentes et passages entre quelques dômes ; porosité modérée et localisée | Espaces de 100–200 m, groupes d’accroches espacés | N3, N9 |
| Jungle sèche | Tables rocheuses découpées et bassins secs, cassures orientées | Arches, tunnels horizontaux et entailles dans les tables | Passages de 24–60 m ; alternance de plateaux rapides et gorges | N3, N6 |
| Jungle tropicale | Tours et cônes karstiques, dépressions larges entre massifs | Cavernes hautes, ponts naturels et perforations sous les tours | Zones denses de 12–32 m, grandes vallées de 80–160 m ; forte verticalité | N5, N6 |
| Désert volcanique | Nappes de lave inclinées, gradins prismatiques et grands cratères rares | Tubes de lave, fenêtres de toit et colonnes pleines | Champs de 120–300 m, tubes traversables ; éboulis fins laissés au matériau | N7, N8 |
| Désert rouge | Dunes orientées combinées à des cellules rocheuses, mesas stratifiées | Hoodoos, grandes arches et étranglements sous les tables | Sable libre de 200–500 m, monuments lisibles à distance | N3, N4 |
| Plateau aride | Paliers larges et profondes incisions peu nombreuses ; formes parfois carrées ou en diamant | Rebords surplombants et ponts massifs au-dessus des incisions | Surfaces libres de 100–250 m ; accès par rampes dédiées entre les étages | N3 |
| Jungle brûlante | Hauts gradins minéraux et bassins inspirés du travertin, mêlés à des reliefs volcaniques | Cheminées et salles sous les gradins ; grandes voûtes ponctuelles | Passages de 16–40 m et terrasses de 80–160 m ; interprétation fantastique assumée | N7, N8, N11 |
| Mangrove tropicale | Côte basse, îlots et chenaux de marée, faible amplitude au niveau marin | Berges sous-cavées et rares arches littorales ; aucune imitation des racines | Chenaux de 20–60 m, grandes ouvertures côtières de 100–300 m | N10 |

## Famille marine et limite d’altitude

Sous un niveau marin configurable, la famille marine peut progressivement remplacer la sélection terrestre dans la matrice 3D : plateau côtier peu accidenté, talus entaillé et reliefs abyssaux plus larges. L’origine du masque doit être la hauteur de référence du terrain ou un champ de continentalité stable ; le mélanger progressivement autour du seuil évite de créer une coupure de densité horizontale. Une cavité terrestre profonde n’a pas nécessairement besoin de se transformer en fond marin simplement parce que son échantillon Y est bas.

Proposition de recette : relief 2D large négatif, terrasses de plateau continental, masque de canyons orienté et quelques montagnes isolées ; la 3D creuse les flancs et crée des alcôves, avec une bande placée sous le fond local. Cible de motifs : 150–600 m. La géométrie du fond est distincte d’une surface d’eau, de son rendu et d’éventuelles règles de nage. Référence : N12.

## Préserver réellement les espaces de déplacement

Un bruit lisse ou une basse fréquence ne garantit pas 10 m entre deux obstacles : les seuils peuvent produire une fente arbitrairement fine. Pour les familles à monuments, on peut borner leur placement et leur enveloppe.

Soit `S` la distance minimale entre centres des cellules de placement, `R` le rayon horizontal maximal d’un monument et `J` le déplacement maximal de son centre, déformation de domaine comprise. Une borne conservatrice entre deux enveloppes est `S - 2R - 2J`.

| Famille de parcours | S | R | J | Espace garanti entre enveloppes |
| --- | ---: | ---: | ---: | ---: |
| Dense | 48 m | 12 m | 6 m | 12 m |
| Ouverte | 200 m | 32 m | 16 m | 104 m |
| Désert monumental | 384 m | 48 m | 32 m | 224 m |

Cette borne suppose que `J` borne la norme du déplacement, et que tout ajout de matière demeure dans l’enveloppe. Elle concerne ces obstacles, pas les falaises du relief de base ni les objets ajoutés ultérieurement. Pour un chemin praticable complet, il faut aussi réserver des vallées ou corridors dont la hauteur de base est continue, limiter leur pente et préserver leur largeur au moment de la composition volumique. La connectivité finale se vérifie sur la géométrie et les collisions.

Les déformations agressives, terrasses franches et sculptures fantastiques doivent surtout occuper les bords et monuments. Les grandes voies de déplacement gardent des courbes continues : cette séparation permet un paysage anguleux et monumental sans imposer au joueur un escalier de petites collisions à grande vitesse.
