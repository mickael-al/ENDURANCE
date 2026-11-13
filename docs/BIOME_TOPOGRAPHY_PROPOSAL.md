# ENDURANCE — Fiches de formes et de traversée des biomes

Proposition du 13 novembre 2026, à discuter avant implémentation. **Aucun biome ni shader n'est modifié par ce document.** Les 25 biomes terrestres et le fond marin conservent leurs identifiants. La répartition climatique validée et les derniers réglages de scène sont conservés. La glace polaire sert de référence déjà presque aboutie, selon le retour du joueur-développeur.

Périmètre : uniquement le terrain solide, sa silhouette et ses passages. Pas de végétation, animaux, accessoires, ponts ou bâtiments ; pas de matériaux définitifs, rendu d'eau, météo ou impacts dynamiques. Un rocher proposé ici est une masse intégrée au champ du terrain, pas un objet à disperser. Les noms de forêt ou de mangrove désignent leur futur milieu ; leurs sols doivent déjà être reconnaissables seuls.

## 1. Direction générale

Un biome doit proposer **plusieurs paysages possibles**, sans les empiler partout. Une occurrence peut être une plaine avec quelques massifs ; une autre, un bassin bordé de falaises ; une troisième, une succession de larges rampes. Elles partagent une même famille de silhouettes.

Le climat ne dicte pas la géologie : les associations ci-dessous sont une direction artistique pour cette planète. Les inspirations naturelles servent à donner une logique aux formes ; les proportions et certaines déformations peuvent devenir fantastiques.

### Échelles de composition

Les dimensions sont des points de départ pour les essais, en mètres monde après les facteurs d'échelle. Elles ne sont ni des limites techniques déjà garanties, ni des valeurs à appliquer uniformément aux 26 biomes.

| Niveau | Emprise horizontale indicative | Rôle | Moyen privilégié |
| --- | --- | --- | --- |
| Paysage | 2–8 km | Massif, vaste bassin, grande vallée, plaine ou plateau | Macrorelief et grandes formes 2D cohérentes |
| Relief de traversée | 150–1 200 m | Collines, dunes, terrasses, selles, contreforts | Profils 2D explicites, courbes et bruits peu nombreux |
| Formes locales | 5–60 m, en groupes espacés | Dalles, lèvres, bosses, renfoncements, rochers soudés | Faible relief 2D ; 3D seulement pour les volumes utiles |
| Site remarquable | Environ 100–700 m ; certains plus grands | Une silhouette mémorable, plusieurs choix de passage | Masse 2D + sculpture 3D localisée |

Un site remarquable n'est pas une quatrième couche de bruit couvrant toute la carte. Il apparaît à quelques endroits. Première hypothèse à tester : plusieurs kilomètres entre grandes rencontres, avec des variations et des absences ; une zone de quelques centaines de mètres peut alors être plus travaillée. Ces espacements dépendent de la vitesse réelle du joueur.

### Préserver la vitesse

- Réserver des surfaces **connectées** pour courir : un pourcentage de terrain plat ne suffit pas si une falaise ou une fissure ferme toute la région.
- Viser d'abord de larges couloirs naturels, de l'ordre de 80–200 m dans les zones de déplacement rapide, davantage dans les grands déserts. Les passages de 10–30 m deviennent des raccourcis ou des endroits ponctuels, pas le gabarit de toute la jungle.
- Dessiner les longues pentes, selles et rampes avant les falaises. Les ruptures fortes se placent surtout sur les flancs, avec un passage au pied ou un accès progressif.
- Mesurer les rencontres en temps : à titre d'exemple, à 50 m/s, 100 m passent en deux secondes et 1 km en vingt secondes. La vitesse du jeu n'est pas supposée fixée à cette valeur.
- Dimensionner les virages avec la vitesse et les capacités du contrôleur : pour un virage à accélération latérale constante, le rayon suit `R = v² / a`. Une pente arrondie n'est pas automatiquement agréable à parcourir.
- Garder un parcours courant praticable sans grappin obligatoire. Le grappin ouvre une trajectoire haute, un raccourci ou une prise d'élan facultative.
- Donner à voir les ruptures avant qu'elles arrivent : pas de puits caché juste derrière chaque sommet, ni d'atterrissage couvert de petites pointes. La météo pourra compliquer ces espaces plus tard.

### Diversité et rareté

Chaque fiche est un catalogue de possibilités. Une sous-région n'active que deux ou trois familles compatibles. Les variantes décrites sont des compositions, pas de nouveaux IDs de biomes ou une nouvelle matrice climatique.

La sélection régionale change lentement ; les paramètres continus se raccordent progressivement. Les formes spectaculaires disposent d'ancres stables, dérivées de la seed et des coordonnées monde, avec un espacement contrôlé. Une ancre choisit aussi une composition : masse solitaire, groupe asymétrique, forme couchée sur un versant, relief entourant une dépression, etc.

**Séparer trois réglages** : distance entre sites, taille du site et fréquence du détail à l'intérieur. Réduire la fréquence d'un bruit agrandit les formes ; cela ne garantit pas qu'elles apparaîtront moins souvent ni qu'un espace libre les séparera.

## 2. Fiches des biomes

Dans les tableaux, « rare » propose des alternatives : elles ne doivent pas toutes se trouver dans la même région. Les grands détails restent épais et lisibles. Les petits motifs sont atténués dans les espaces de circulation.

### 0 — Glace polaire / `PolarIce`

**Identité :** grandes surfaces glacées courbes, creux lisibles et ruptures isolées. **Conserver la recette validée comme référence.**

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Étendues ouvertes, grandes dépressions gelées, fronts de falaise discontinus | Garder les masques et formes de la recette actuelle ; doser leur occurrence régionale |
| Traversée | Bassins de tailles variées, longues remontées, lèvres arrondies | Voronoï déformé et profils de bord continus ; préserver les sorties des bassins |
| Locale | Faibles bourrelets et renfoncements dans les flancs | Réutiliser le relief fin et la 3D déjà présents, sans densifier partout |
| Rare | Très grand bassin bordé d'un front brisé ; autre région dominée par une longue nappe soulevée | Composer les familles existantes sur un site plutôt qu'ajouter immédiatement une nouvelle recette |

**Variantes :** plaines de glace / pays de cuvettes / bord de plateau. **Traversée :** grandes prises d'élan ; les crevasses et murs ne doivent pas enfermer les bassins. Toute nouveauté reste optionnelle après inventaire de ce qui fonctionne déjà.

### 1 — Toundra gelée / `FrozenTundra`

**Identité :** un sol gonflé par grandes plaques, différent de la glace en cuvettes.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Plaines bombées et dépressions très ouvertes | Bruit lent aplati au centre des plaines |
| Traversée | Dômes isolés, bourrelets et grands polygones peu creusés | Bosses elliptiques espacées ; Voronoï uniquement pour les joints peu profonds |
| Locale | Lèvres de plaques et petits bombements décentrés | Profils doux aux bordures, rugosité réduite sur les sommets praticables |
| Rare | Pingo monumental ouvert sur un côté ; grand capot de sol incliné | Dôme 2D puis découpe 3D latérale ; dalle épaisse inclinée sur un site distinct |

**Variantes :** plaques presque planes / plaines à dômes / dépressions cerclées. **Traversée :** conserver de larges selles entre dômes ; aucune fissure obligatoire à chaque polygone.

### 2 — Toundra / `Tundra`

**Identité :** versants doux disposés en longues langues et replats, avec peu de verticalité.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Collines basses et vallons ouverts | Bruit anisotrope à grande longueur d'onde |
| Traversée | Lobes de versant, selles larges, terrasses arrondies | Profils allongés décalés et paliers adoucis |
| Locale | Affleurements couchés et petites lèvres rocheuses | Volumes elliptiques fondus dans le versant, sans tapis de blocs |
| Rare | Cirque très ouvert ; crête couchée dont une extrémité se relève | Bassin radial ouvert en 2D ; masse 3D épaisse courbée sur un site rare |

**Variantes :** collines / versants en langues / cirques. **Traversée :** les replats s'enchaînent par des pentes, jamais par une série de petites marches.

### 3 — Taïga / `Taiga`

**Identité :** paysage allongé, « peigné », guidant naturellement de longues courbes.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Vallées longitudinales entre reliefs parallèles irréguliers | Domaine étiré et orientation lente partagée |
| Traversée | Cordons sinueux, dos de terrain et cols entre vallées | Distance à des axes courbes, avec interruptions larges |
| Locale | Affleurements oblongs sur les flancs | Ellipsoïdes allongés cohérents avec l'orientation régionale |
| Rare | Longue arête couchée ; paroi courbe isolée qui se termine en rampe | Volume étiré et déformé, amplitude qui décroît aux extrémités |

**Variantes :** vallées / cordons bas / arêtes couchées. **Traversée :** ménager des traversées latérales entre vallées pour éviter des rails infranchissables.

### 4 — Forêt boréale marécageuse / `BorealBog`

**Identité :** cuvettes organiques entourant de très larges îlots bas, sans morcellement excessif.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Chapelets de bassins et grandes zones presque planes | Cellulaire déformé à faible amplitude, bassins regroupés |
| Traversée | Dômes aplatis, cols bas et longs cordons de sol | Unions de bosses larges avec selles explicites |
| Locale | Petites corniches de rive, dos rocheux polis | Déformations de berge localisées ; roche lisse au sommet |
| Rare | Îlot minéral massif évidé sur un flanc ; longue masse affleurante autour d'un bassin | Volume bas très étiré, cavité exclusivement latérale |

**Variantes :** grands bassins / archipel terrestre connecté / cordons. **Traversée :** garder un réseau de sol continu ; ne pas compter sur une future nage pour rendre le terrain franchissable.

### 5 — Toundra rocheuse / `RockyTundra`

**Identité :** dalles inclinées, silhouettes cassées et grands plans minéraux.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Massifs anguleux entre vastes replats | Crêtes orientées et grands plans de coupe |
| Traversée | Rampes de dalles, banquettes et contreforts en éventail | Profils asymétriques avec pied adouci ; aucune marche sur toute la largeur |
| Locale | Petits groupes de tors soudés et fractures de flanc | Volumes anguleux regroupés, fractures courtes hors trajectoire |
| Rare | Immense dalle penchée ; deux lames divergentes laissant un passage large | Boîtes ou prismes arrondis, orientation oblique et emprise 3D bornée |

**Variantes :** dalles / arêtes / bassins rocheux. **Traversée :** silhouette agressive en hauteur, pieds de relief fluides ; éviter le chaos d'éboulis partout.

### 6 — Steppe froide / `ColdSteppe`

**Identité :** très longues vagues asymétriques et horizons dégagés.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Houle de terrain de grande longueur d'onde | Bruit fortement étiré, orientations lentement variables |
| Traversée | Longues descentes, talus obliques et rampes latérales | Profil doux d'un côté, rupture localisée sur l'autre |
| Locale | Épaules rocheuses, petites entailles sur les talus | Masque de pente puis quelques coupes latérales |
| Rare | Escarpement courbe très long ; crête terminée par un bec rocheux | Bande courbe à hauteur progressive ; surplomb seulement à une extrémité |

**Variantes :** vagues / plaine inclinée / bord d'escarpement. **Traversée :** longs segments de vitesse ; les escarpements doivent se terminer ou proposer plusieurs cols.

### 7 — Forêt boréale / `BorealForest`

**Identité :** dos rocheux polis et coupoles massives, moins linéaires que la taïga.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Grandes coupoles séparées de vallons larges | Bosses anisotropes de plusieurs tailles, implantation espacée |
| Traversée | Selles, rampes convexes et épaules de roche | Unions douces et profils de raccord aux pieds |
| Locale | Amas arrondis soudés à la masse principale | Quelques ellipsoïdes, déformation limitée aux flancs |
| Rare | Coupole géante fendue latéralement ; masse couchée à grand surplomb bas | Union volumique épaisse, soustraction d'une fente courte ou d'une alcôve |

**Variantes :** dômes solitaires / groupes de coupoles / vallons polis. **Traversée :** sommets et selles roulants ; les fissures ne coupent pas systématiquement le dessus.

### 8 — Forêt froide / `ColdForest`

**Identité :** grandes vallées en U, amphithéâtres et flancs puissants.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Vallées ouvertes au fond, massifs élevés sur les côtés | Distance à un axe courbe et profil en U |
| Traversée | Éventails de pente, replats hauts, larges cols | Rampes de raccord aux parois et terrasses peu nombreuses |
| Locale | Nervures de contrefort et balcons épais | Relief orienté sur les flancs ; petits surplombs localisés |
| Rare | Cirque monumental ouvert ; épaule suspendue au-dessus d'une grande terrasse | Demi-bassin déformé ; ajout de volume épais et excavation latérale |

**Variantes :** vallée / cirque / hauts replats. **Traversée :** fond rapide continu et accès aux hauteurs ; aucun cul-de-sac systématique au fond des cirques.

### 9 — Marécage froid / `ColdMarsh`

**Identité :** immenses dépressions plates bordées de faibles gradins, plutôt que des îlots nombreux.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Larges fonds presque horizontaux | Aplatissement local de bassins déformés |
| Traversée | Anciennes rives concentriques et banquettes discontinues | Terrasses radiales douces avec ouvertures franchissables |
| Locale | Lèvres de rive, bosses isolées sur les marges | Masque de bord ; presque aucun petit obstacle au centre |
| Rare | Grand anneau de terrain rompu ; banquette relevée en croissant | Couronne 2D ouverte, quelques lèvres 3D en porte-à-faux |

**Variantes :** bassin plat / rives étagées / anneau rompu. **Traversée :** plusieurs sorties à chaque bassin ; l'anneau ne doit pas devenir une enceinte.

### 10 — Plaine sèche / `DryPlain`

**Identité :** grands espaces presque nus, traversés de traces de relief très longues.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Plaines inclinées, grands éventails et cuvettes ouvertes | Bruit lent de faible amplitude et éventails radiaux incomplets |
| Traversée | Chenaux secs peu profonds, talus qui émergent progressivement | Distance à des courbes avec section large et peu incisée |
| Locale | Petites lèvres et plaques basses aux bords des chenaux | Profils de rive continus ; détails retirés des lignes de passage |
| Rare | Long rocher affleurant comme une quille ; butte solitaire sous-cavée sur un côté | Volume très allongé enfoui progressivement ; évidement latéral ciblé |

**Variantes :** plaine / éventails / lit ancien. **Traversée :** préserver de longues lignes de vue et de course ; le site remarquable se contourne largement.

### 11 — Prairie / `Meadow`

**Identité :** enchaînement de collines rondes et de selles ; le relief lui-même porte le plaisir du déplacement.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Grandes collines et vallées souples | Somme contrôlée de bosses et dépressions larges |
| Traversée | Rampes, cuvettes ouvertes et longues crêtes courbes | Profils à courbure continue, cols maintenus entre bosses |
| Locale | Quelques affleurements couchés sur les côtés | Volumes très aplatis, sans couverture généralisée de pierres |
| Rare | Amphithéâtre de collines ; crête minérale retournée sur un seul flanc | Grandes bosses disposées autour d'un creux ouvert ; petite intervention 3D localisée |

**Variantes :** collines rondes / grandes combes / amphithéâtres. **Traversée :** mettre l'effort sur les courbes et les réceptions, pas sur le nombre de cavités.

### 12 — Forêt tempérée / `TemperateForest`

**Identité :** paysage plissé, avec côtes asymétriques et couches épaisses.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Longues côtes, revers rocheux et vallons obliques | Relief orienté avec profil asymétrique |
| Traversée | Banquettes, marches très larges et rampes en biais | Terrasses adoucies, masquées sur des bandes de montée |
| Locale | Nez de couche et alcôves dans les revers | Coupes 3D peu profondes limitées à quelques strates |
| Rare | Falaise courbe à grands abris ; promontoire en couches débordantes | Masse stratifiée 2D puis épaississement et sous-cavage local |

**Variantes :** côtes longues / vallons / promontoires. **Traversée :** les paliers restent reliés ; les faces abruptes composent le paysage sans barrer chaque vallée.

### 13 — Forêt tempérée humide / `TemperateRainforest`

**Identité :** vallées généreuses sous de lourdes parois arrondies et surplombantes.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Bassins larges encadrés de masses rocheuses épaisses | Vallées courbes, profils de paroi et plateaux localisés |
| Traversée | Terrasses profondes, contreforts arrondis et entrées de vallon | Gradins peu nombreux, longues rampes de raccord |
| Locale | Encoches, abris ouverts et lèvres de paroi | Évidements latéraux espacés, jamais des trous répartis sur tout le fond |
| Rare | Amphithéâtre surplombant une terrasse ; énorme auvent rocheux décentré | Union de volumes épais et soustraction d'une alcôve largement ouverte |

**Variantes :** vallées / terrasses / amphithéâtres. **Traversée :** passage rapide au fond, itinéraires facultatifs en hauteur ; les monuments doivent donner de l'espace sous eux.

### 14 — Marais tempéré / `TemperateMarsh`

**Identité :** grandes courbes de méandres, levées continues et anciens bras.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Plaine organisée par quelques méandres larges | Axes courbes continus et profil de plaine ; réseau conçu, pas bruit en toile d'araignée |
| Traversée | Levées, bras abandonnés et îlots allongés | Profils transversaux de berge, dépressions peu profondes |
| Locale | Petits surplombs de berge et langues de terrain | Découpes limitées aux rives extérieures |
| Rare | Méandre fossile à hautes berges courbes ; vaste gradin alluvial échancré | Exagérer un seul méandre, sous-caver quelques flancs et ménager ses accès |

**Variantes :** grandes boucles / levées ouvertes / bras anciens. **Traversée :** les chenaux ordinaires se franchissent sans imposer des sauts ; les rives restent connectées par le terrain.

### 15 — Désert de roche / `RockDesert`

**Identité :** longues lames érodées et masses couchées, avec de grands intervalles libres.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Plaines de roche et massifs directionnels discontinus | Masques régionaux séparant plaine et groupes de lames |
| Traversée | Dorsales effilées, couloirs larges et bases arrondies | Bruit anisotrope et plans épaissis ; pieds adoucis |
| Locale | Rochers couchés, becs latéraux et strates visibles en silhouette | Prismes arrondis déformés dans une direction commune |
| Rare | Monolithe courbé géant ; paire de grandes lames divergentes | Capsules ou prismes épais avec centre décalé selon la hauteur |

**Variantes :** désert ouvert / lames / massifs couchés. **Traversée :** les formations offrent des appuis latéraux au grappin ; elles ne remplissent pas tous les couloirs.

### 16 — Savane sèche / `DrySavanna`

**Identité :** plaines ouvertes ponctuées de groupes rocheux compacts et arrondis.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Faibles ondulations autour de sites minéraux isolés | Fond 2D simple, implantation espacée de groupes |
| Traversée | Petites collines rocheuses, rampes épaisses et selles | Bosses de tailles hiérarchisées, raccords au pied |
| Locale | Blocs soudés et lèvres lisses | Union de quelques ellipsoïdes plutôt qu'une multitude de pierres |
| Rare | Amas monumental asymétrique ; grande masse arrondie couchée sur deux contreforts | Composition 3D de quelques volumes, ouverture latérale généreuse |

**Variantes :** plaine / groupes bas / amas monumentaux. **Traversée :** on doit pouvoir contourner un groupe sans casser sa vitesse, ou choisir d'en monter les rampes.

### 17 — Savane / `Savanna`

**Identité :** montagnes isolées à base très large, distinctes des petits groupes de la savane sèche.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Grandes ondulations et massifs solitaires | Bosses de grande portée sur un fond doucement vallonné |
| Traversée | Contreforts, longues pentes et immenses selles | Branches radiales irrégulières et cols explicites |
| Locale | Épaules rocheuses et falaises sur certains revers | Masque de flanc ; détails bas ailleurs |
| Rare | Montagne double autour d'une selle ; grand massif érodé en croissant | Deux masses 2D raccordées, excavation 3D limitée à une face remarquable |

**Variantes :** plaines vallonnées / massif solitaire / massifs doubles. **Traversée :** faire le tour, gravir une longue pente ou passer par la selle ; aucun escalier de petites buttes.

### 18 — Jungle sèche / `DryJungle`

**Identité :** pavements et plateaux bas découpés en grandes dalles, avec quelques murs isolés.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Tables basses et vallons largement ouverts | Cellulaire de grande taille transformé en plateaux |
| Traversée | Pavements, larges gradins et dépressions fermées peu profondes | Paliers adoucis, rampes dans les raccords |
| Locale | Lèvres de dalle et fentes courtes sur les côtés | Prismes épais ; soustractions latérales peu nombreuses |
| Rare | Plateau évidé sous un bord ; grande dalle relevée en coque | Masse 2D et volume incliné, plafond robuste sans perforer partout le dessus |

**Variantes :** pavements / tables / dalles relevées. **Traversée :** le pavement reste une surface, pas un champ de petits blocs à escalader.

### 19 — Jungle tropicale / `TropicalJungle`

**Identité :** grands massifs verticaux regroupés, séparés de vrais espaces de circulation.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Plaines de bassin entre groupes de tours épaisses | Sites de massifs espacés ; relief faible entre sites |
| Traversée | Contreforts, cols et plateformes latérales | Bases élargies et rampes ; tours de hauteurs hiérarchisées |
| Locale | Parois concaves, alcôves et petites épaules de roche | Bruit 3D modulé sur les flancs, avec peu de perforations |
| Rare | Massif traversé d'une immense ouverture ; groupe de tours partiellement fusionnées autour d'une cour | Union de volumes épais puis grande découpe ciblée |

**Variantes :** bassin ouvert / tours regroupées / massif perforé. **Traversée :** corridors larges entre groupes ; le slalom serré reste un choix local. L'ouverture du massif se distingue des arches fines du désert rouge.

### 20 — Désert volcanique / `VolcanicDesert`

**Identité :** grandes pentes en bouclier et nappes de roche lobées, sans imposer de lave active.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Boucliers aplatis, caldeiras ouvertes et plaines de coulée | Profils radiaux déformés et nappes dirigées |
| Traversée | Lobes, bourrelets longs et larges gradins | Profils de bord épais ; ruptures interrompues par des rampes |
| Locale | Dalles soulevées, rides figées et bouches latérales | Déformations de basse amplitude ; courtes cavités sous les nappes |
| Rare | Cratère asymétrique rompu ; grand tube ouvert sur un flanc | Anneau 2D incomplet ; volume tubulaire à entrée et sortie explicites |

**Variantes :** coulées lisses / bouclier / bord de caldera. **Traversée :** ne pas transformer la surface en mâchefer injouable ; les ouvertures sont regroupées et visibles.

### 21 — Désert rouge / `RedDesert`

**Identité :** immenses étendues de dunes et rares monuments de grès. Les grandes arches élancées deviennent sa signature rare.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Mers de sable, larges combes et affleurements très espacés | Domaines à orientation lente ; séparer régions sableuses et sites rocheux |
| Traversée | Dunes asymétriques, croissants et longues crêtes ondulantes | Profils directionnels et lobes ; varier les organisations, pas seulement la phase d'une sinusoïde |
| Locale | Lèvres douces, petites langues de grès au bord des sites | Détail géométrique faible sur les trajectoires ; rochers soudés aux sites |
| Rare | Grande arche de grès ; ailleurs, paroi isolée percée ou longue flèche couchée | Masse stratifiée et soustraction 3D ; une composition choisie par site |

**Variantes :** dunes longues / dunes en croissants / désert de grès. **Traversée :** les dunes font accélérer et se propulser ; aucune bordure cassante à chaque crête.

### 22 — Plateau aride / `AridPlateau`

**Identité :** très grandes tables praticables et vallées encaissées entre leurs escarpements.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Plateaux, mesas étendues et grands canyons | Hauteur terrassée et axes de vallée cohérents |
| Traversée | Paliers larges, éperons et rampes le long des parois | Profils de rampe réservés avant la sculpture des faces |
| Locale | Corniches épaisses, alcôves et strates débordantes | 3D limitée à certaines couches et portions de falaise |
| Rare | Mesa divisée par une grande fracture ; plateau terminé par un immense bec surplombant | Fracture à emprise explicite, ou volume débordant localisé |

**Variantes :** tables ouvertes / réseau de canyons / mesas isolées. **Traversée :** relier les étages hauts et bas ; une surface plane isolée au sommet ne compte pas comme accessible.

### 23 — Jungle brûlante / `BurningJungle`

**Identité :** fantaisie minérale de masses torsadées et de contreforts courbes, pas de racines géantes déguisées en terrain.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Bassins vastes et massifs peu nombreux aux flancs tournants | Grandes masses 2D avec domaine lentement déformé |
| Traversée | Rampes hélicoïdales partielles et contreforts obliques | Profils courbes ouverts, sans escalier en spirale obligatoire |
| Locale | Nervures épaisses et replis rocheux | Déformation bornée de volumes, détails limités à certains flancs |
| Rare | Massif qui se replie sur lui-même ; deux contreforts minéraux entrelacés | Quelques capsules épaisses déformées et soudées à leur socle |

**Variantes :** bassins / contreforts torsadés / massif replié. **Traversée :** conserver les fonds simples ; la torsion doit se lire de loin, pas désorienter chaque mètre du sol.

### 24 — Mangrove tropicale / `TropicalMangrove`

**Identité :** grands éventails côtiers et bancs allongés, plus ramifiés que les méandres du marais tempéré.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Éventails plats et chenaux ramifiés peu profonds | Réseau d'axes à grande échelle et profil côtier |
| Traversée | Bancs larges, langues de terre et îlots minéraux rares | Profils de banc continus, selles basses conservées |
| Locale | Lèvres de berge, petites découpes et dalles littorales | Évidements 3D courts sur les côtés, détails très faibles sur les bancs |
| Rare | Grand éventail soulevé ; promontoire littoral largement sous-cavé | Relèvement local 2D puis découpe de son bord côté mer |

**Variantes :** éventails / bancs longitudinaux / rivage soulevé. **Traversée :** connections terrestres ou gués topographiques larges ; ne pas rendre l'ensemble dépendant d'un futur système d'eau.

### 25 — Fond marin / `Marine`

**Identité :** grands espaces négatifs, gradins continentaux et reliefs isolés. La pratique sous l'eau dépendra du futur mode de déplacement.

| Échelle | Formes retenues | Mise en œuvre courte |
| --- | --- | --- |
| Paysage | Plateaux sous-marins, talus et grandes plaines profondes | Profils de profondeur progressifs, domaines régionaux larges |
| Traversée | Canyons ouverts, monts isolés et grandes banquettes | Axes de vallée et bosses espacées ; fonds peu accidentés |
| Locale | Dalles, lèvres et renfoncements dans les parois | Détails surtout sur les flancs, faible couverture de cavités |
| Rare | Paroi abyssale à grande alcôve ; mont isolé partiellement éventré | Masse 2D principale puis excavation 3D latérale limitée |

**Variantes :** plateau / canyon / monts sous-marins. **Traversée :** lisibilité et grands passages d'abord ; ne pas promettre que le personnage courant à terre pourra utiliser ces reliefs sous l'eau.

## 3. Traduction dans le générateur actuel

### Une organisation commune, des formes propres à chaque biome

1. Préparer en 2D la composition locale : grand relief, type de région, sites proches et espaces de passage.
2. Employer la 2D pour la masse des montagnes, dunes, vallées, terrasses et rampes. Les fissures et arches ne remplacent pas une bonne silhouette de base.
3. Activer la 3D pour les ajouts de matière, parois inclinées, surplombs et évidements impossibles avec une seule hauteur.
4. Raccorder le site au terrain courant sur une couronne progressive. Limiter les changements rapides aux parties volontairement abruptes.

Dans chaque biome, les primitives pourront être partagées — bassin, profil de dune, terrasse douce, ellipsoïde, prisme, tube courbe — mais la composition, les proportions et l'implantation doivent rester spécifiques. Ajouter six bruits à toutes les recettes ne crée pas six paysages différents.

### Contrat 3D à respecter

Le champ actuel est positif dans le solide. Il initialise `Dbase = hauteur - y`, puis additionne les deltas pondérés des biomes. Une fonction `fieldVolume` retourne donc `Dcible - Dbase` ; le poids est déjà appliqué par le pipeline. Elle ne doit pas rajouter une seconde fois la densité de base.

Pour une distance signée conventionnelle `s`, positive à l'extérieur de la forme, les constructions conceptuelles sont : union de matière `max(Dbase, -s)` ; excavation `min(Dbase, s)`. Ce sont des combinaisons de champs pour l'isosurface, pas la garantie que le résultat reste une distance signée exacte. Le mélange entre biomes peut modifier ou amincir ces formes.

La 3D reste évaluée dans une **enveloppe autour du terrain**. Actuellement, la sortie 2D est bornée à une hauteur relative de ±640, une épaisseur totale entre 0 et 576 et un offset entre −160 et +160, avant les facteurs d'échelle concernés. L'épaisseur totale n'est pas une demi-épaisseur. La quantification des contrôles et les bornes CPU participent aussi à ce contrat.

Une tour penchée ou un auvent doit ouvrir la bande sur **toute son empreinte XZ**, pas seulement sous son pied. La marge doit couvrir déformation, raccord et quantification ; la contribution doit s'éteindre avant la limite de son enveloppe. Une bande nulle désactive la 3D locale.

Les montagnes très imposantes doivent porter leur masse dans le relief 2D, puis recevoir une sculpture locale. Si les dimensions souhaitées dépassent le contrat, adapter ensemble les clamps, le stockage, l'enveloppe GPU et les bornes CPU ; ne pas simplement augmenter un coefficient. Les ordres de grandeur artistiques de ces fiches ne constituent pas l'autorisation de dépasser silencieusement ces bornes.

Les grandes formes fragiles sont préférentiellement implantées là où leur biome domine. Aux frontières, utiliser des formes basses compatibles et vérifier la connectivité ; une interpolation de densités ne garantit pas qu'une arche garde son ouverture. La bande héritée du mélange 2D doit toujours couvrir la contribution 3D pondérée.

### Rareté et coût GPU

- Utiliser des ancres déterministes mondiales. Une grille de placement est un outil d'implantation de formes, pas une nouvelle segmentation climatique. Un simple candidat par cellule ne garantit pas la distance minimale aux voisins : prévoir une règle d'exclusion locale déterministe.
- Borner le rayon d'un site, sa déformation et le nombre de cellules voisines à consulter. Un site chevauchant deux chunks doit être trouvé des deux côtés ; aucun hasard dépendant du chunk, de l'ordre de génération ou du LOD.
- Rejeter d'abord une emprise absente ou lointaine, puis seulement évaluer les déformations 3D coûteuses. Un bruit rare mais calculé partout n'est pas gratuit.
- Préserver les données préparées par colonne et le filtrage par tuiles déjà présents. Choisir entre recalcul léger d'une ancre et stockage supplémentaire après mesure ; ne pas ajouter d'emblée un gros tableau de paramètres par voxel.
- Conserver les shaders indépendants par biome et leurs binaires. Les variantes régionales ne nécessitent pas automatiquement de nouvelles combinaisons de shaders : une sélection déterministe de quelques familles suffit comme premier essai.
- Ne pas élargir toute la bande 3D d'un biome pour un seul monument. L'ouvrir localement sur les emprises utiles ; laisser la 3D éteinte là où la surface simple suffit.
- Distinguer relief de jeu et détail visuel : rides et grain très fins pourront passer dans les normales plus tard. Une normal map ne remplace ni une silhouette ni une collision.
- Les formations restent fixes dans le monde. Les sections fines peuvent disparaître dans un LOD grossier : tester leur représentation aux espacements réellement utilisés, épaissir les formes majeures ou conserver localement un LOD suffisant. Ne pas redimensionner les monuments avec la distance.

Les réseaux de vallées et chenaux proposés sont d'abord des constructions géométriques contrôlées, pas une simulation d'érosion ni une preuve de drainage réaliste. Les connexions et profils doivent être vérifiés explicitement.

## 4. Ordre de réalisation et critères de validation

1. Conserver un échantillon de la glace polaire validée, avec seed et cadrage reproductibles, pour comparer les changements futurs.
2. Réaliser une **prairie** : référence de mouvement pur, grandes courbes et réceptions, presque sans 3D complexe.
3. Réaliser un **désert de roche** : référence de sites espacés, volumes ajoutés et silhouette penchée.
4. Réaliser une **jungle tropicale** : référence de région dense qui conserve des trajets ouverts.
5. Réaliser un **marais tempéré** : référence de relief bas connecté. Ces quatre cas révèlent les problèmes communs avant de multiplier les recettes.
6. Étendre ensuite par familles, en vérifiant les différences entre biomes voisins. Introduire les grands monuments fantastiques après validation de leurs emprises et de leurs LOD.

Pour chaque biome :

- Tester seul puis mélangé à ses voisins, sur plusieurs seeds et plusieurs régions éloignées. Vérifier qu'une nouvelle occurrence montre une autre composition, pas uniquement le même motif tourné.
- Comparer les silhouettes avec un matériau gris commun, à hauteur de joueur et à distance. La couleur, la végétation et la météo ne doivent pas être nécessaires pour distinguer les terrains.
- Contrôler un réseau de passages entre les côtés d'une région, avec largeur, pente, dégagement vertical et rayon de virage compatibles avec le contrôleur. Vérifier aussi les transitions entre régions et l'accès aux terrasses.
- Mesurer le temps entre ralentissements forcés, la longueur de parcours continu, les chutes sans sortie et les réceptions aveugles. Aucune cible de vitesse ou de pente définitive n'est fixée tant que le contrôleur n'est pas calibré.
- Examiner les portions au sol, en hauteur et sous les surplombs : une carte 2D de pentes ne suffit pas pour valider la traversabilité volumique.
- Vérifier raccords entre chunks, transitions de LOD, collision locale et conservation des grandes ouvertures à distance.
- Mesurer le coût sur trois cas : terrain courant, site remarquable et frontière de plusieurs biomes. Une rareté visuelle ne dispense pas de mesurer les pics de génération GPU.

L'objectif d'une première passe n'est pas que chaque biome ait autant de trous ou de monuments : c'est qu'il possède une façon reconnaissable de se traverser, de grandes respirations, et des rencontres que l'on n'a pas toutes vues après une seule visite.

## 5. Références et statut des propositions

Les sources suivantes documentent des familles de formes ; **les recettes, échelles, compositions fantastiques et règles de déplacement ci-dessus sont des propositions pour ENDURANCE**, pas des paramètres extraits de ces sources.

- [NPS — Glacier Landforms](https://www.nps.gov/articles/series.htm?id=F6B94C83-1DD8-B71B-0B7447F900839E48) : cirques, reliefs modelés par les glaciers, cordons et formes allongées ; inspirations des régions froides.
- [NPS — Permafrost](https://www.nps.gov/im/arcn/permafrost.htm) : sols polygonaux et pingos ; inspiration de la toundra gelée, avec un relief volontairement plus praticable.
- [NPS — Arid and Semi-arid Region Landforms](https://www.nps.gov/subjects/geology/arid-landforms.htm) et [USGS — Arches](https://www.usgs.gov/geology-and-ecology-of-national-parks/geology-arches-national-park) : plaines désertiques, escarpements et formes rocheuses érodées.
- [USGS — Types of Dunes](https://pubs.usgs.gov/gip/deserts/dunes/) : plusieurs organisations de dunes pour différencier les régions d'un même désert.
- [NPS — Karst Landscapes](https://www.nps.gov/subjects/caves/karst-landscapes.htm) : reliefs de dissolution et cavités ; inspiration des massifs perforés, sans généraliser les gouffres au sol.
- [NPS — Meandering Stream](https://www.nps.gov/articles/meandering-stream.htm) : méandres et bras abandonnés ; inspiration du marais tempéré.
- [NPS — Lava Flow Forms](https://www.nps.gov/articles/000/lava-flow-forms.htm) : nappes, chenaux et tubes ; inspiration des formes de roche volcanique figée.
- [NOAA — Deep-Sea Canyons, Continental Slopes, and Seamounts](https://sanctuaries.noaa.gov/visit/ecosystems/deep-sea.html) : grandes familles de relief du fond marin.
- [Ryan Geiss / NVIDIA — Generating Complex Procedural Terrains Using the GPU](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu) : principe d'un terrain décrit par un champ de densité. Le pipeline historique de ce chapitre n'est pas une proposition de remplacer le backend actuel.

Points du code consultés : `field_batch_classify.C.glsl`, `biome_relief_common.glsl`, `biome_volume_common.glsl`, `field_weights.glsl`, `field_volume_accumulate.glsl`, `field_storage.glsl`, `ProceduralTerrainField.hpp`, et les deux shaders de `biomes/PolarIce`. Les noms/IDs viennent de `ProceduralTerrainBiomes.hpp` et de l'inspecteur terrain. La proposition complète les anciens documents artistiques ; elle ne décrit pas des changements déjà implémentés.
