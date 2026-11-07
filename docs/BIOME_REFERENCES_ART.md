# Références artistiques et déplacement dans les biomes

Recherche du 7 novembre 2026 pour ENDURANCE. Ce document décrit des références et des propositions de composition ; il ne constitue pas un inventaire de fonctions déjà implémentées. Les formes, métriques et dimensions proposées ci-dessous sont des choix pour ENDURANCE, pas des spécifications techniques attribuées aux œuvres citées. Aucun asset de ces œuvres n'est importé.

## Références primaires

| Référence | Ce que la source établit | Application proposée à ENDURANCE |
| --- | --- | --- |
| [Journey — thatgamecompany](https://thatgamecompany.com/journey/) | Le studio présente un vaste désert, une montagne lointaine comme destination, et le déplacement entre sable et vestiges. | Une silhouette dominante visible de loin ; de longues étendues simples font ressortir les formations rares. Réserver les accidents du terrain aux endroits où ils composent un passage ou une découverte. |
| [Sword of the Sea — Giant Squid](https://giantsquidstudios.com/Sword-of-the-Sea-Announcement) | Le studio associe explicitement terrain ondulant, accumulation de vitesse, grands sauts et formes proches d'un skatepark. | Construire les collines comme des supports de mouvement : bassins larges, crêtes arrondies, rampes progressives et pieds de colonnes raccordés au sol. Les terrasses peuvent avoir des flancs abrupts tout en conservant des accès courbes. |
| [Sonic Frontiers — entretien de l'équipe, SEGA](https://www.sega.co.jp/en/recruit/episode/episode15/) | Les développeurs expliquent avoir réintroduit le mouvement rapide dans une version trop centrée sur les puzzles. Ils décrivent aussi la recherche de points remarquables et la nécessité des essais manette en main. | Évaluer chaque biome avec le futur contrôleur rapide : la variété visuelle ne suffit pas. Distinguer espaces d'accélération, obstacles lisibles et appuis de grappin. Faire varier les silhouettes sur une grande distance, au lieu d'ajouter continuellement de petits obstacles. |
| [Shadow of the Colossus — Mark Skelton, directeur artistique de Bluepoint](https://blog.playstation.com/2018/01/16/shadow-of-the-colossus-photo-mode-revealed/) | La présentation du directeur artistique met en avant les panoramas, les images de développement et le contrôle de la composition et de la lumière. | Réserver de grands volumes vides devant des falaises, arches ou aiguilles. Tester les silhouettes en niveaux de gris et en vue lointaine avant les matériaux. |
| [Shadow of the Colossus — entretien Bluepoint sur la reconstruction](https://blog.playstation.com/archive/2018/01/12/how-shadow-of-the-colossus-ps4-studio-bluepoint-became-masters-of-the-remaster/) | Le directeur artistique donne l'exemple d'une cascade dont ils ont rétabli une alimentation géographiquement cohérente. | Même les reliefs fantastiques gagnent à avoir une logique : strates continues, vallées reliées, érosion concentrée, cavités dans une même couche. |
| [Princesse Mononoké — Studio Ghibli](https://www.ghibli.jp/works/mononoke/) | Fiche officielle du film et galerie de référence du studio. | Référence artistique retenue pour les régions humides : profondeur, espaces abrités et grandes masses anciennes. Traduire cette intention en bassins et falaises ; les arbres et les racines restent des éléments distincts du terrain. |
| [Nausicaä de la vallée du vent — Studio Ghibli](https://www.ghibli.jp/works/nausicaa/) | Fiche officielle du film et galerie de référence du studio. | Référence retenue pour une nature étrangère, des traversées aériennes et des contrastes entre ouvertures et enceintes rocheuses. Employer des parois hautes et des aiguilles espacées au-dessus de sols praticables. |
| [Le Château dans le ciel — Studio Ghibli](https://www.ghibli.jp/works/laputa/) | Fiche officielle du film et galerie de référence du studio. | Référence retenue pour l'échelle verticale et les silhouettes impossibles. Préférer d'abord de grands tabliers de roche, piliers et arches reliés au terrain ; des îles flottantes demanderaient une règle volumique supplémentaire, explicite. |

Ces références guident des choix de formes. Elles ne prouvent pas qu'une fréquence de bruit particulière produit le mouvement, la composition ou l'émotion des œuvres concernées.

## Spectre de formes pour les 25 cases

La matrice conserve les noms de la capture fournie : température en lignes, humidité en colonnes. Les échelles ci-dessous sont des objectifs de composition. « Dense » décrit ici la densité de masses du terrain ; les arbres ne sont pas générés par ce champ.

| Biome | Support 2D et silhouette | Intervention 3D proposée | Lecture du déplacement |
| --- | --- | --- | --- |
| Glace polaire | Grandes nappes courbes, bourrelets isolés et rebords de plateau. | Voûtes sous la nappe et entailles rares dans les bourrelets. | Très longues prises d'élan ; franchissement annoncé par un rebord visible. |
| Toundra gelée | Grandes dalles polygonales, terrasses peu hautes, dépressions arrondies. | Fractures regroupées et passages sous quelques dalles. | Éviter la fissure à chaque cellule ; conserver de larges dalles continues. |
| Toundra | Collines basses, selles larges et buttes isolées. | Quelques abris et tunnels courts dans les buttes. | Ondulations d'environ 100–180 m, rares appuis verticaux. |
| Taïga | Vallées larges encaissées et crêtes arrondies. | Surplombs localisés aux flancs des vallées. | Couloir naturel de mouvement, ouvertures latérales pour le grappin. |
| Forêt boréale marécageuse | Bassins, îlots bas et cordons de terrain. | Berges creusées, passages couverts peu fréquents. | Alternance de sols bas ouverts et appuis hauts ponctuels. |
| Toundra rocheuse | Dos rocheux angulaires et vallons larges ; pieds de relief adoucis. | Fentes et fenêtres au sein des masses principales. | Silhouettes cassées, trajectoires basses souples. |
| Steppe froide | Houle très ample, longs flancs convexes, crêtes peu nombreuses. | Porches rares sous les crêtes. | Grande vitesse soutenue, longue visibilité. |
| Forêt boréale | Cordons arrondis et relief glaciaire allongé. | Cavités dans les berges et arches occasionnelles. | Longues courbes et changements progressifs de hauteur. |
| Forêt froide | Amphithéâtres, gradins larges et contreforts. | Balcons rocheux et tunnels de flanc. | Plusieurs hauteurs accessibles avec le grappin, bassins libres en dessous. |
| Marécage froid | Cuvettes connectées, îlots et plateformes basses. | Sous-cavages restreints aux berges. | Traversées lisibles ; le relief ne doit pas devenir un tapis de petites bosses. |
| Plaine sèche | Grandes ondulations douces, quelques rebords de roche. | Petites familles de porches, espacées à l'échelle du paysage. | Segment d'accélération d'environ 100 m ou davantage. |
| Prairie | Collines continues, larges vallées et terrasses très douces. | Arches rares dans les grandes crêtes. | Un biome de respiration et de vitesse, à silhouette immédiatement lisible. |
| Forêt tempérée | Collines à banquettes, vallons plus serrés et promontoires. | Abris et passages dans les couches rocheuses. | Terrasses praticables reliées par rampes ; relief dense mais discontinu. |
| Forêt tempérée humide | Masses rocheuses épaisses, vallées profondes et plateformes. | Porches généreux, arches et grandes sous-cavités. | Parcours inférieur rapide et parcours supérieur au grappin. |
| Marais tempéré | Plaine basse, bassins et cordons à bords nets. | Érosion sous les berges et passages rares. | Des espaces ouverts entre les cordons ; pas de labyrinthe aléatoire de murs. |
| Désert de roche | Mesas, énormes colonnes isolées et pieds arrondis. | Fenêtres et arches dans certaines colonnes ou mesas. | Grandes lignes d'élan de 150–400 m ; colonnes comme objectifs de grappin. |
| Savane sèche | Plaines ondulantes et groupes de kopjes espacés. | Abris dans les groupes rocheux. | Sol ouvert et relief concentré en îlots ; courbes rapides autour des masses. |
| Savane | Houle large, plateaux doux et rares monolithes. | Tunnels et porches au pied de grands reliefs. | Environ 100 m entre les ruptures majeures ; silhouettes moins sèches que le désert rocheux. |
| Jungle sèche | Gradins de grès, longues crêtes et vallons ouverts. | Strates sous-cavées, passages traversants. | Les marches marquent le paysage, les rampes portent la trajectoire rapide. |
| Jungle tropicale | Tours karstiques et grands creux arrondis. | Arches, cavernes ouvertes et balcons à plusieurs hauteurs. | Région dense : couloirs de l'ordre de 15–40 m, avec bassins plus vastes pour reprendre de la vitesse. |
| Désert volcanique | Plaques de lave, caldeiras larges et gradins plus géométriques. | Tubes de lave et ouvertures dans des couches déterminées. | Planchers lisses et rebords forts ; les structures en blocs restent groupées. |
| Désert rouge | Dunes orientées, mesas tabulaires et aiguilles rares. | Grandes arches dans les bancs rocheux. | Dunes comme rampes ; vastes espaces de sable entre les points d'appui. |
| Plateau aride | Grandes terrasses, canyons et tabliers rocheux. | Surplombs stratifiés et ponts naturels. | Distinguer le chemin haut, le fond du canyon et leurs connexions. |
| Jungle brûlante | Bassins volcaniques, parois épaisses et contreforts arrondis. | Évents, porches et voûtes érodées groupées. | Variante fantastique des régions chaudes humides : masses impressionnantes, fonds de bassin praticables. |
| Mangrove tropicale | Terrasses côtières basses, îlots et chenaux larges. | Berges sous-cavées et arches littorales rares. | Canaux de circulation lisibles ; les futures racines géantes relèvent d'un système de végétation. |

Sous le seuil marin, employer une famille cohérente de plateau continental, talus, monts sous-marins et canyons. Cette famille décrit le fond solide. Une surface d'eau, ses vagues et son rendu constituent une autre couche du moteur.

## Composition procédurale et coût

Le chapitre de Ryan Geiss, [Generating Complex Procedural Terrains Using the GPU, GPU Gems 3](https://developer.nvidia.com/gpugems/gpugems3/part-i-geometry/chapter-1-generating-complex-procedural-terrains-using-gpu), expose une densité volumique, l'accumulation d'octaves, la déformation du domaine, les strates et les influences contrôlées. Il montre notamment que la déformation à grande échelle peut produire arches et tunnels, et qu'une influence explicite peut réserver une zone plate. Son ancien pipeline de rendu ne constitue pas une recommandation de remplacer le backend compute actuel.

Proposition pour ENDURANCE : préparer une grande forme par colonne, lui ajouter un relief secondaire mesuré, puis concentrer le travail 3D dans une bande choisie. Une crête, une terrasse ou une colonne identifiable doit déjà exister dans la forme principale. Les cavités modifient cette logique au lieu d'effacer partout le support roulant. Partager les primitives entre recettes permet de réutiliser des échantillons de bruit ; le nombre de noms de biomes ne doit pas multiplier automatiquement le nombre d'octaves par voxel.

Le caractère angulaire peut provenir d'une transformation du bruit : terrasses, puissance d'une crête, distance cellulaire en losange ou en carré. Une fréquence supplémentaire ne remplace pas la définition d'une silhouette. Pour les strates, un palier adouci conserve une transition mesurable ; un `step` discontinu peut être réservé à un masque ou à un choix de matériau plutôt qu'à une marche sous le joueur.

## Ce que les distances ne garantissent pas encore

Une longueur d'onde de 100 m ne garantit pas un trajet libre de 100 m. Deux reliefs, un mélange de biomes ou une cavité peuvent créer un passage plus étroit. Une garantie de 10 m libres dans les régions denses demande soit un masque de corridor explicite, soit un contrôle de dégagement sur le résultat généré, avec prise en compte de la taille et de la trajectoire du joueur.

Pour cette étape, mesurer au minimum la distribution des pentes et la largeur des passages sur plusieurs seeds. Ensuite, tester accélération, lancement et balancement avec le contrôleur réel. Les points de grappin devront eux aussi être choisis sur une géométrie stable et adaptée ; leur présence ne découle pas automatiquement d'une belle colonne.
