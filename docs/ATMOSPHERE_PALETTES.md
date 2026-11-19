# Palettes de ciel et brouillard ENDURANCE

Proposition initiale : les teintes restent sobres, avec des horizons lisibles sur les grandes distances et des zeniths plus profonds. La correction artistique par defaut est de **38 %** ; le transport atmospherique reste calcule physiquement. Les valeurs sont des choix artistiques, a ajuster dans la scene de test.

## Matrice climatique

X = humidite croissante ; Y = temperature croissante. Les IDs, la matrice et les poids sont ceux de `ProceduralTerrainBiomes.hpp` et `fieldSurfaceWeight`. Le fond marin est ajoute avec le poids cotier du terrain, pas selon l’altitude Y de la camera.

| Temperature \ Humidite | 0 — sec | 1 | 2 | 3 | 4 — humide |
|---|---|---|---|---|---|
| 0 — polaire | Glace polaire | Toundra gelee | Toundra | Taiga | Foret boreale marecageuse |
| 1 — froid | Toundra rocheuse | Steppe froide | Foret boreale | Foret froide | Marecage froid |
| 2 — tempere | Plaine seche | Prairie | Foret temperee | Foret temperee humide | Marais tempere |
| 3 — chaud | Desert de roche | Savane seche | Savane | Jungle seche | Jungle tropicale |
| 4 — tres chaud | Desert volcanique | Desert rouge | Plateau aride | Jungle brulante | Mangrove tropicale |

## Trois couleurs par biome

Les codes hexadecimaux ci-dessous sont des apercus **sRGB**. Le fichier JSON contient leur conversion **lineaire**, utilisee dans les shaders HDR. `exposure` multiplie les couleurs sans ecretage ; il est possible de saisir des valeurs lineaires superieures a 1 dans ImGui.

| ID | Biome | Horizon | Elevation intermediaire | Zenith | Intention |
|---:|---|---|---|---|---|
| 0 | PolarIce — Glace polaire | `#C5DBDC` | `#789FAF` | `#263C5B` | Air blanc bleute, zenith profond |
| 1 | FrozenTundra — Toundra gelee | `#C7D2CF` | `#83979F` | `#39495E` | Gris froid, glace et ciel acier |
| 2 | Tundra — Toundra | `#C4C7B6` | `#8C9C97` | `#3E575F` | Lichen et gris vert discret |
| 3 | Taiga — Taiga | `#B4C7C1` | `#6C9699` | `#294A5D` | Brume froide entre les coniferes |
| 4 | BorealBog — Foret boreale marecageuse | `#ACBEB6` | `#72918D` | `#304D51` | Nappes vert gris basses |
| 5 | RockyTundra — Toundra rocheuse | `#D0CEC4` | `#8D9BA8` | `#3B4F6B` | Horizon mineral, ciel cobalt assourdi |
| 6 | ColdSteppe — Steppe froide | `#CECABC` | `#8F9BA5` | `#3C536C` | Lumiere seche et herbes pales |
| 7 | BorealForest — Foret boreale | `#B7C9BC` | `#70998F` | `#28535D` | Lointains sauge, ombres bleues |
| 8 | ColdForest — Foret froide | `#BBC9C8` | `#73979F` | `#2B4C64` | Cyan grise et grands creux obscurs |
| 9 | ColdMarsh — Marecage froid | `#AFBFB9` | `#718D89` | `#324C53` | Voiles froids au ras du sol |
| 10 | DryPlain — Plaine seche | `#D5C9AD` | `#9BA6A6` | `#43667B` | Paille doree, ciel bleu gris |
| 11 | Meadow — Prairie | `#D1D4BD` | `#8CAFAD` | `#386C7F` | Horizon lumineux et teinte fraiche |
| 12 | TemperateForest — Foret temperee | `#C0CEB9` | `#7BA69D` | `#315F71` | Vert discret, zenith petrolier |
| 13 | TemperateRainforest — Foret temperee humide | `#B5C7BC` | `#74A29D` | `#2B5D6B` | Humidite douce sur les grands reliefs |
| 14 | TemperateMarsh — Marais tempere | `#BEC7AF` | `#839F8F` | `#3B615E` | Brume sauge et clairieres |
| 15 | RockDesert — Desert de roche | `#DAC5A6` | `#ABAFA6` | `#516D83` | Pierre chaude, ciel froid et net |
| 16 | DrySavanna — Savane seche | `#D7C39D` | `#A4B09C` | `#496D78` | Or poussiereux et olive desature |
| 17 | Savanna — Savane | `#D5CBA6` | `#9EB4A5` | `#3F7180` | Soleil chaud, lointains turquoise assourdis |
| 18 | DryJungle — Jungle seche | `#CCC6A6` | `#8CA898` | `#3A655F` | Ocre doux et vert sombre |
| 19 | TropicalJungle — Jungle tropicale | `#BFD1BB` | `#7DB4AD` | `#286F7C` | Vapeur claire, bleu vert profond |
| 20 | VolcanicDesert — Desert volcanique | `#C6A796` | `#867782` | `#363447` | Cendre chaude et zenith ardoise violet |
| 21 | RedDesert — Desert rouge | `#DDB28C` | `#B69B8A` | `#53667C` | Poussiere cuivre et bleu mineral |
| 22 | AridPlateau — Plateau aride | `#DBCAAC` | `#A2B1B2` | `#426A86` | Horizon ivoire, grande transparence |
| 23 | BurningJungle — Jungle brulante | `#D2B899` | `#9B9F85` | `#425C59` | Air ambre, canopee olive sombre |
| 24 | TropicalMangrove — Mangrove tropicale | `#C5CDB0` | `#85AAA0` | `#32686D` | Brume chaude verdatre et eaux sombres |
| 25 | Marine — Fond marin / large | `#BED7D9` | `#70A7B8` | `#245479` | Horizon marin et bleu oceanique |

## Reglages et sauvegarde

- `middleElevation = 0.25` : direction verticale du point intermediaire (environ 14,5 degres au-dessus de l’horizon).
- `gradientPower = 1.0` : deformation du gradient, distincte de la position intermediaire.
- `strength = 0.38` : intensite de la correction artistique appliquee par le moteur au ciel, a l’IBL et a la radiance atmospherique.
- `exposure = 1.0` : multiplicateur HDR des trois couleurs.
- `fogDensity` : coefficient d’extinction local, en m^-1 ; **0 desactive le brouillard local** du biome.
- `fogAlbedo` : part de lumiere diffusee, comprise entre 0 et 1.
- `fogBaseHeight` : altitude de base, en metres au-dessus du niveau marin du terrain.
- `fogHeightScale` : hauteur caracteristique de decroissance exponentielle, en metres.
- `fogNoiseAmount = 0` donne un voile lisse ; une valeur positive active une structure 3D.
- `fogNoiseScale` : frequence spatiale de cette structure (cycles/metre).

La configuration initiale se trouve dans `ENDURANCE/Ressources/Atmosphere/biome_palettes.json`. Le composant charge ce fichier si la scene ne contient pas encore de palettes ; les modifications ImGui sont ensuite enregistrees avec les donnees du composant dans la scene. Le bouton de rechargement recharge explicitement la matrice du fichier.

## Coherence climatique et cout

`fieldClimateColumn` est une seule implementation partagee par le classificateur du terrain, l’echantillon de palette et l’atlas de brouillard. La recette `Noise/climate_2d.glsl`, la seed, la macro-hauteur, le melange cotier et les poids quintiques ne sont pas reimplementes sur CPU. Le composant lit les parametres **du generateur en cours d’execution** : des reglages du terrain encore non appliques ne modifient pas le ciel.

La palette au XZ de la camera est evaluee dans un petit compute shader. Trois emplacements de resultat permettent de recueillir un resultat seulement apres signalement de sa fence GPU ; aucune attente n’est faite sur un resultat en vol. Les compteurs montrent les requetes, resultats et latences pour mesurer ce choix. Le seuil de deplacement se compare a la derniere position soumise, donc les petites translations s’accumulent. Les transitions de couleur et leur seuil sont geres par le moteur.

Le brouillard evalue le climat sur un atlas en **coordonnees du monde**, avec la meme fonction exacte aux noeuds. Entre les noeuds, ses proprietes sont interpolees : c’est une reduction de resolution volontaire du brouillard, pas un changement de la recette du terrain. Les cellules restent ancrees dans le monde pendant les deplacements. Une camera en region claire voit donc la nappe du biome voisin. La frequence d’actualisation de l’atlas depend d’un changement de la seed/des reglages/des palettes ou du franchissement de la zone de garde de l’atlas. Son etendue doit couvrir la distance du ray marching ; l’interface expose l’etendue et la resolution.

L’altitude Y de la camera ne selectionne jamais le biome ; elle reste disponible pour les LUT atmospheriques et l’integration du brouillard.

Le vent de brouillard global du composant, `fogWind` XYZ (metres/seconde),
anime seulement la structure 3D a l’interieur de ces regions. Il est distinct
du vent des nuages. Le climat, les frontieres du brouillard et son profil
d’altitude restent fixes ; changer le vent ne reconstruit pas l’atlas.
