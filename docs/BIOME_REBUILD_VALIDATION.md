# Reconstruction des 25 biomes — mesures et vérification

Révision du 13 novembre 2026. Les 50 recettes terrestres précédentes sont remplacées ; le climat, Marine et les réglages de la scène sont conservés. La description des formes, des unités et les sources de conception figurent dans [BIOMES.md](BIOMES.md). Les [fiches initiales](BIOME_TOPOGRAPHY_PROPOSAL.md) décrivent l'intention ; leur catalogue n'est pas une liste de fonctionnalités toutes réalisées.

## Ce qui a été exécuté

Les sondes exécutent les fonctions GLSL de production sur **OpenGL 4.5, Mesa llvmpipe**, puis lisent leurs résultats pour les diagnostics. Le renderer est logiciel : ces mesures ne constituent pas un benchmark du GPU Windows du jeu. Aucun compteur de mesure, readback ni buffer de diagnostic n'est ajouté au moteur.

| Vérification | Résultat |
| --- | --- |
| Paramètres CPU, limites numériques, tables et sélection LOD | Réussi ; compilation C++17 avec `-O3 -ffast-math`. La garde de domaine suit les cinq octaves macro déjà présentes sur la branche. |
| Compilation et pipeline réel | 52 programmes de biome compilés/liés, 26 IDs forcés, lots multi-seed/multi-LOD, halos et commandes indirectes contrôlés. |
| Poids locaux | 13 308 densités comparées à un oracle indépendant ; doublons, huit contributeurs, mer et poids positifs <0,1 % inclus. |
| Matrice climatique | 25 nœuds exacts, transitions 2D/3D et littoral continus ; 25/25 biomes dominants présents dans le relevé sur quatre seeds. |
| Nouveaux sites sculptés | Les 25 variantes hautes ciblées ajoutent du solide et contiennent de l'air relié à l'extérieur sous un toit. Deux autres variantes par biome sont également mesurées : 75 sites au total, densité finie et support de bande contrôlé à la tolérance flottante du test. Une variante sans cavité reste valide. |
| Sondes contre pipeline | 99 825 valeurs près des trois variantes de chaque biome concordent avec l'accumulation réelle ; 9 375 échantillons partagés entre deux LOD sont identiques bit à bit. |
| Épaisseur et contrôles | 24 combinaisons épaisseur/échelle, bandes négatives/nulles, contrôle `.w` indépendant et rejets par tuile/Y vérifiés. |
| Grandes coordonnées | 41 154 densités identiques aux lancements exhaustifs à Y=±2²⁴, LOD 0/6/12. Ce test porte sur les rejets du pipeline, pas sur la finesse d'un paysage à des coordonnées extrêmes. |
| Maillage | Plan, sphère, normales, champ vide, chunk 64³, six faces Transvoxel, raccord d'angle et 91 644 arêtes du stress ambigu vérifiés. |
| Régénération et cache | Code backend C++ exécuté : 61 binaires rechargés sans compilation quand les sources sont identiques ; une recette modifiée ne recompile que son programme. Une modification de bruit 2D ou 3D reste limitée à sa dimension. |
| Diagnostics de shader | Fichier/ligne d'origine, include manquant et correction après échec vérifiés. |

Le partage de placement est séparé des bibliothèques de bruit. `biome_landforms_common.glsl` ne dépend que du hash commun ; les volumes n'incluent pas la bibliothèque 2D. Les recettes ne créent aucun programme par combinaison de biomes. Les colonnes, le champ FP32, les caches et les buffers de production gardent leur format et leur nombre.

## Échantillonnage et échelle du personnage

Fixture issue des réglages actuels de la scène : seed 1337, amplitude macro 1000, fréquence macro 0,000015, cinq octaves, `detailScale=0,35`, hauteur de base 0, mer −100 et transition marine 64. Chaque biome est forcé séparément pour lire sa forme sans mélange climatique. La validation de matrice vérifie les mélanges à part.

Pour chaque biome, le test examine 144 cellules d'implantation et cible un site proche dont la variante est >0,80 afin d'inclure les sculptures les plus rares. **Il ne s'agit pas d'un échantillon aléatoire représentatif de leur fréquence.** Une variante basse et une variante proche de 0,50 sont aussi évaluées en volume, puis comparées au pipeline et aux LOD. La seed suivante est échantillonnée pour vérifier que le champ change. Les empreintes SHA-256 des sources GLSL développées sont conservées dans le JSON ; une reprise partielle refuse les mesures de sources périmées.

- Paysage : fenêtre de **4 096 × 4 096 m**, grille 129², pas 32 m, macrorelief inclus.
- Volume du site : **81 × 129 × 81** échantillons ; dimensions mondiales adaptées au rayon et à la bande. Le JSON indique le pas de chaque axe.
- Variantes basse et intermédiaire : **41 × 65 × 41** échantillons chacune ; absence de sculpture autorisée lorsque la recette la prévoit.
- Détail : fenêtre 144 × 144m, pas horizontal 1,5 m, 129 niveaux Y.
- Repère de personnage : **1,80m**, sans agrandissement, avec la même échelle physique sur X, Y et Z.

Sur les 25 fenêtres, **87,1 à 100 %** des échantillons de hauteur ont une pente estimée <30°. La plus grande composante connexe correspondante couvre **85,3 à 100 %** de la fenêtre et rejoint ses quatre bords. Ce sont des indicateurs 2D, pas une garantie de passage dans la géométrie finale. Les cavités peuvent se trouver dans une masse ajoutée au-dessus du sol : une mesure limitée aux trous sous la hauteur initiale les manquerait.

La dernière colonne du tableau est le **plus grand dégagement vertical observé sous un toit ouvert** sur le site ciblé. Ce n'est ni une hauteur minimale de tous les passages, ni la largeur du passage, ni une validation d'une capsule en mouvement. Le pas de mesure peut masquer de petits obstacles.

| Biome | Cellule d'implantation (km) | Pentes 2D <30° | Dégagement sous toit échantillonné |
| --- | --- | --- | --- |
| 0 — PolarIce | 2.40 | 99.8 % | 17.5 m |
| 1 — FrozenTundra | 2.20 | 99.5 % | 19.9 m |
| 2 — Tundra | 2.60 | 100.0 % | 21.3 m |
| 3 — Taiga | 2.80 | 99.1 % | 19.9 m |
| 4 — BorealBog | 2.50 | 100.0 % | 12.2 m |
| 5 — RockyTundra | 2.10 | 93.9 % | 37.4 m |
| 6 — ColdSteppe | 2.70 | 100.0 % | 18.3 m |
| 7 — BorealForest | 1.85 | 97.9 % | 23.3 m |
| 8 — ColdForest | 2.40 | 93.8 % | 30.4 m |
| 9 — ColdMarsh | 2.30 | 100.0 % | 7.1 m |
| 10 — DryPlain | 2.60 | 100.0 % | 21.0 m |
| 11 — Meadow | 2.40 | 99.6 % | 18.8 m |
| 12 — TemperateForest | 2.05 | 100.0 % | 36.3 m |
| 13 — TemperateRainforest | 2.20 | 99.2 % | 47.4 m |
| 14 — TemperateMarsh | 2.50 | 100.0 % | 10.5 m |
| 15 — RockDesert | 2.10 | 97.2 % | 68.3 m |
| 16 — DrySavanna | 1.80 | 98.4 % | 40.3 m |
| 17 — Savanna | 2.80 | 99.8 % | 20.1 m |
| 18 — DryJungle | 2.20 | 96.9 % | 18.1 m |
| 19 — TropicalJungle | 1.60 | 96.3 % | 28.3 m |
| 20 — VolcanicDesert | 2.60 | 97.5 % | 13.8 m |
| 21 — RedDesert | 2.90 | 98.8 % | 35.4 m |
| 22 — AridPlateau | 2.70 | 87.1 % | 29.5 m |
| 23 — BurningJungle | 2.50 | 98.8 % | 50.3 m |
| 24 — TropicalMangrove | 2.60 | 100.0 % | 9.2 m |

Le détail brut des coordonnées, seeds, tailles de grille, changements de signe, composantes connexes et différences aux frontières est enregistré dans [metrics.json](biome-validation/metrics.json).

## Images mesurées

Les images sont reconstruites depuis les **densités GLSL réellement calculées**, sans textures, végétation ou génération d'image artistique. L'extraction hors ligne par tétraèdres sert à afficher ces données ; le meshing Marching Cubes/Transvoxel du jeu est testé séparément. L'éclairage gris et la grille de diagnostic ne représentent pas le rendu final.

- [Atlas des 25 paysages de 4km](biome-validation/landscapes.png).
- [Atlas des 25 sites sculptés complets](biome-validation/landforms.png).
- [Coupes des cavités et surplombs](biome-validation/sections.png).
- [Détails à l'échelle du personnage](biome-validation/closeups.png).

## Limites à éprouver dans le jeu

Les recettes réservent des régions calmes et des sites espacés de 1,6 à 2,9 km, mais elles ne résolvent pas un chemin praticable garanti. Le macrorelief, les mélanges et le LOD peuvent modifier un passage. Les volumes suivent la hauteur de chaque colonne : ce sont des formations géologiques déformées avec le terrain, pas des objets rigides à altitude unique. Les variantes changent taille, direction et silhouette, mais leur vocabulaire de formes reste fini.

Le prochain test dans ENDURANCE doit couvrir la capsule de 1,80 m, les collisions, les pentes à vitesse réelle, les réceptions et le grappin. Vérifier plusieurs seeds et plusieurs transitions, puis les ouvertures aux LOD éloignés. La scène utilise un rayon de sélection de **40,096 km** (`viewDistance`, directement utilisé comme rayon) ; elle n'a pas été parcourue intégralement dans ces tests. Le [relevé du climat réel](BIOME_WORLD_SCALE.md) mesure séparément les superficies et les ancres des formations dans ce disque. La sensation de course demande une appréciation en jeu ; les données présentées ne permettent pas d'affirmer que chaque biome est artistiquement final.

## Reproduire

Depuis la racine du dépôt, avec les dépendances décrites dans [Tests/Terrain/README.md](../Tests/Terrain/README.md) :

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_biomes.py
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_biome_landforms.py --output /tmp/endurance-biome-rebuild
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_biome_world_scale.py
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_biome_pipeline.py
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_band_control.py
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_field_optimizations.py
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_gpu.py
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_polar.py
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_reload.py --glew-object /path/to/GLEW
```

Les métriques sont reproductibles pour un pilote et un état de source donnés ; le contrat de seed est testé sur ce pilote, sans promettre des sorties binaires identiques entre fabricants de GPU.

## Reprise par lots — 13 novembre 2026

La refonte des 25 biomes est sauvegardée dans le commit `c28bd31`, branche
`feature/terrain-noise-library` (PR #3). La reprise n'a trouvé aucune recette
manquante : une revue des 50 fonctions et des dépendances a confirmé les
appariements 2D/3D et les 25 empreintes des mesures enregistrées.

La campagne des sites sculptés a été relancée en cinq lots indépendants, sous
OpenGL 4.5 / Mesa llvmpipe, avec la seed 1337 et les paramètres décrits plus haut.

| Lot | IDs | Biomes | Résultat |
| --- | --- | --- | --- |
| 1 | 0–4 | PolarIce à BorealBog | 5/5 réussis |
| 2 | 5–9 | RockyTundra à ColdMarsh | 5/5 réussis, dont une interruption et reprise |
| 3 | 10–14 | DryPlain à TemperateMarsh | 5/5 réussis |
| 4 | 15–19 | RockDesert à TropicalJungle | 5/5 réussis |
| 5 | 20–24 | VolcanicDesert à TropicalMangrove | 5/5 réussis |

Les 25 enregistrements obtenus sont identiques aux mesures de `metrics.json`.
Le script écrit désormais un checkpoint après chaque biome, avec remplacement
atomique des échantillons puis du JSON. `--resume` reprend uniquement les biomes
manquants. Les [commandes par lots](../Tests/Terrain/README.md#rebuilt-landforms-real-density-and-player-scale)
séparent les mesures et le rendu des planches.

Une interruption forcée après le sixième biome a conservé les six mesures
terminées ; la reprise du lot 2 a réutilisé le biome 5 et calculé seulement les
quatre suivants. Une reprise complète réutilise les 25 résultats sans ouvrir
OpenGL. Le remplacement ciblé `--merge --biomes 0` préserve les 24 autres mesures.
Six tests de régression sans GPU couvrent également l'échec d'écriture atomique,
les archives corrompues et les données périmées (seed, sources, paramètres et
renderer). Une mesure d'un ancien pilote n'est pas mélangée avec de nouvelles
mesures d'un autre renderer.

La prochaine étape de validation reste le parcours dans le jeu sur la machine
cible : capsule de 1,80 m, collisions, grappin, transitions et performances.
Les checkpoints facilitent la reprise du travail ; ils ne modifient pas les
limites de conversation de ChatGPT Work.
