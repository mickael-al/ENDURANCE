# Lune d'ENDURANCE

La scène `ENDURANCE/Scene/Terrain.scene` active une lune proche, de **20° de
diamètre apparent** à l'origine du monde. Son centre est à 1 000 km, au-dessus
de l'horizon à 25°. Sa position reste fixe dans le monde : les déplacements
du joueur produisent une parallaxe. Le rayon angulaire et la distance sont des
choix artistiques, pas les valeurs de la Lune terrestre.

Le modèle exporté `ENDURANCE/Ressources/Model/Moon/Baked/moon.glb` est désormais
utilisé en jeu : corps creusé, parois intérieures et fragments détachés.

![Ancien atlas sphérique : phases et occultation atmosphérique](images/moon-validation.png)

L’illustration ci-dessus documente l’ancien atlas de secours, pas le nouveau maillage.

## Réglages

Dans les paramètres de l'atmosphère :

| Paramètre | Effet |
| --- | --- |
| `moonSurface` | Active le corps lunaire détaillé ; utilise le GLB lorsqu’il est chargé, sinon la sphère de secours. |
| `moonModelFile` | Modèle GLB du jeu ; vide pour sélectionner explicitement l’ancien atlas. |
| `moonMeshResolution` | Résolution du cache de géométrie, 1024 par défaut. |
| `Moon angular radius (degrees)` | Rayon apparent à l'origine ; valeur de scène : 10°. |
| `Moon centre distance (m)` | Distance depuis `(0, seaLevel, 0)` ; la taille physique est déduite du rayon angulaire. |
| `Moon surface rotation (degrees)` | Tourne le modèle entier, fragments compris, autour de son axe Y. |
| `Legacy sphere relief strength` | Contrôle de l’ancien atlas sphérique ; le GLB utilise ses propres normales. |
| `Moon disk color` / `Moon disk HDR intensity` | Teinte et gain de la lumière réfléchie, également pris en compte pour l'éclairage. |
| `Moon illumination multiplier` | Ajustement artistique de la lumière directe et de la diffusion atmosphérique ; 1 conserve la calibration de la sphère. |
| `Optional moon light` | Source directionnelle associée ; la scène sélectionne `Moon`, index 1. |

Tourner l'objet **Moon** déplace l'astre. Les angles de secours `Moon fallback
azimuth/elevation` ne servent que lorsqu'aucune lumière lunaire n'est associée.
Le déplacement orbital automatique n'est pas implémenté. La phase évolue lorsque
la direction du soleil ou de la lune change.

La lumière de la lune est calculée depuis celle du soleil, l'albédo moyen de
la surface, la taille angulaire et la phase lambertienne. Les valeurs génériques
de couleur/intensité de l'objet `Moon` ne remplacent pas ce calcul ; utiliser les
contrôles de l'atmosphère ci-dessus. La nouvelle lune n'émet pas de lumière.

Les ombres terrain de **Moon** sont désactivées dans la scène par défaut pour
éviter un second jeu de cascades. Activer sa case **Shadow** utilise le système
existant, y compris son interaction avec les volumes atmosphériques. Sans ces
ombres, cette lumière directe n'est pas occultée par les reliefs. Les nuages
atténuent déjà sa lumière, indépendamment de cette case.

## Rendu du modèle et caches

Le jeu charge le GLB via `EnduranceMoon::loadMoonMesh` et transmet des données
validées à `Atmosphere::setMoonMesh`. Le moteur possède les buffers et textures
GPU ; il ne connaît ni le chemin de l’asset ni les recettes du laboratoire.
Le modèle publié contient **49 412 sommets, 97 600 triangles et 47 parties**,
avec un atlas sRGB de 2048 × 1024. Les indices des parties sont regroupés pour
rendre l’ensemble en un appel par passe. Le rayon initial du corps reste 1 ;
les fragments élargissent les bornes sans réduire la taille apparente de la lune.

- Une vue perspective centrée sur l’astre rasterise réellement le maillage dans
  une texture **RGBA16F**. RGB contient la réponse à un soleil d’intensité unitaire
  et A la couverture géométrique. Les cavités, les espaces entre fragments et
  les faces obscures viennent de la géométrie, sans intersection avec une sphère
  de remplacement.
- Le cache de vue ne dépend pas de la rotation de la caméra, de son champ de
  vision ou du temps. Un déplacement suffisant de l’observateur, un changement
  d’orientation du modèle ou de direction solaire déclenche un nouveau rendu.
  Les petites translations s’accumulent par rapport à la dernière vue calculée.
- Les ombres du relief lunaire utilisent une carte locale **D24 2048 × 2048**.
  Elle dépend de la géométrie, de sa rotation et de la direction solaire :
  déplacer seulement l’observateur ne la reconstruit pas.
- La couleur et l’intensité solaires ainsi que la teinte lunaire sont appliquées
  lors de la composition. Leur changement seul ne nécessite pas de redessiner
  le modèle. La couverture reste opaque même sur une face sans lumière.
- Le ciel lit le cache, puis applique les mêmes atténuations atmosphériques et
  nuageuses qu’avant. Le passage des nuages ne redessine pas le maillage lunaire.
- Chaque capture IBL fige l’image et sa projection, en réutilisant la copie tant
  que le cache lunaire n’a pas changé. Ses faces restent cohérentes
  si le soleil change pendant la génération répartie sur plusieurs images.
  Les réflexions GGX contiennent la lune ; avec une lumière lunaire associée,
  la contribution diffuse reste exclue de l’IBL pour éviter de la compter deux fois.
- `GE_ATM_MOON_MESH` spécialise les shaders de ciel/capture. Les shaders de volume
  et la sélection soleil le jour / lune la nuit ne prennent pas une seconde
  passe d’éclairage.

Le coût du maillage est donc ponctuel ; le coût permanent est l’échantillonnage
du cache dans la région de l’astre. Ce système utilise de la mémoire GPU pour
éviter de retraiter environ 98 000 triangles à chaque image. Il n’est pas gratuit :
une animation continue de la direction solaire peut demander des rafraîchissements
fréquents. Les compteurs `moonImageUpdates`, `moonShadowUpdates` et
`moonMeshGpuBytes` permettent de contrôler le travail et la mémoire réellement
alloués. Les mesures locales ne remplacent pas un profilage sur GTX 1660.

Le cache utilise 512, 1024 ou 2048 pixels de côté selon le réglage. Sa projection
doit englober les fragments : l’observateur doit rester hors de leur sphère
englobante. Les 10° de rayon actuels conviennent au modèle publié ; une taille
exagérée peut rendre cette projection impossible, ce qui masque le corps et
affiche une erreur plutôt que de conserver une image incorrecte.

L’éclairage directionnel du terrain reste une approximation lambertienne à
partir de l’albédo moyen et de la phase du corps. Les fragments et cavités sont
rendus exactement dans la vue mise en cache, mais ne créent pas chacun une
nouvelle lumière. Une superposition avec le soleil masque son disque ; l’ombre
d’une éclipse sur le terrain n’est pas simulée.

## Chargement et remplacement de l’asset

Le composant utilise `moonModelFile`, par défaut
`../ENDURANCE/Ressources/Model/Moon/Baked/moon.glb`. Le chargement se fait une
fois à l’activation, au changement de chemin ou avec **Lunar model → Reload lunar
asset**. Aucune lecture de fichier ni génération procédurale n’est effectuée par
image. Le décodeur PNG du chargeur est privé : les réglages de retournement des
textures du reste du moteur ne peuvent pas modifier cet atlas.

Un remplacement invalide garde le dernier modèle valide et affiche son erreur.
Si aucun modèle n’a pu être chargé, `moonSurfaceFile` fournit l’ancien atlas
sphérique de secours. Un chemin de modèle vide sélectionne explicitement ce
secours. L’atlas historique n’est pas conservé en VRAM lorsque le GLB est utilisé.
La désactivation de la scène libère les deux ressources.

Pour modifier le modèle, utiliser `ENDURANCE/Scene/MoonGeneratorLab.scene`,
exporter le GLB, puis recharger l’asset : voir [le laboratoire lunaire](moon-lab.md).
Les bruits, les cavités et la simplification du maillage restent précalculés
hors jeu en C++. Aucun bruit 3D ni raymarch de géométrie n’est ajouté au rendu.

L’ancien `Tools/bake_moon.cpp` reste disponible pour régénérer `moon.surface`.
Son format `EMOON001` contient dimensions, albédo moyen et RGBA8
(normale objet RGB, albédo A). Il ne modifie pas la silhouette sphérique :
ce comportement ne concerne que le secours analytique.

## Validation

```sh
python Tests/Atmosphere/check_shaders.py
python Tests/Atmosphere/check_runtime.py
python Tests/Atmosphere/check_game_component.py
python Tests/Atmosphere/check_moon_mesh_runtime.py
python Tests/Atmosphere/check_moon.py --preview docs/images/moon-validation.png
python Tests/Atmosphere/check_revision.py --group visible
python Tests/Atmosphere/benchmark_moon_clouds.py
python Tests/Sea/check_render.py
```

Les tests exécutent les shaders sur OpenGL/Mesa : phases, noir sans soleil,
rotation, parallaxe, occultation atmosphère/nuages, alpha de nuages vide,
cohérence ciel/reflets, chargement réel de l'asset, invalidation des captures,
désactivation et libération. Les régressions mer vérifient aussi l'opacité
profonde à 0.97 et l'absence de transparence vers un ciel sans fond marin.
L'aperçu ci-dessus est issu des shaders, pas d'une capture de la scène complète.
Ces vérifications ne mesurent pas le coût sur GTX 1660 ni la compilation MSVC.
Pour comparer un autre atlas sans remplacer celui du jeu :
`python Tests/Atmosphere/check_moon.py --surface chemin/moon.surface --size 641 --preview comparaison.png`.

Le vent des nuages passe de `(12, 4)` à `(48, 16)` m/s dans les valeurs par défaut
et dans la scène terrain. Le déplacement reste intégré dans le temps, sans
régénération du bruit 3D ni saut de phase lors d'un changement de vitesse.
Une vitesse quatre fois plus élevée ne multiplie pas le nombre de passes ni
d'échantillons ; toute vitesse non nulle actualise déjà les mêmes caches.

## Une seule lumière céleste : soleil le jour, lune la nuit

La direction vers le soleil définit la bascule, indépendamment de l'orientation
ou de l'altitude de la caméra :

- `sunDirection.y >= 0` : éclairage solaire uniquement.
- Soleil sous l'horizon, ou absent : éclairage lunaire uniquement, si la lune
  est au-dessus de l'horizon et possède une énergie non nulle.
- Deux astres sous l'horizon : aucun de ces deux éclairages directs.

La règle est centralisée dans `Asset/Shader/Atmosphere/atmosphere_common.glsl`
(`geAtmSunLightingEnabled` et `geAtmMoonLightingEnabled`). Elle s'applique au
ciel physique, aux nuages, à la perspective aérienne, au brouillard et aux god
rays. Les matériaux différés, transparents et marins ignorent la source inactive
avant les calculs PBR et les lectures d'ombre. Les autres lumières de la scène
restent indépendantes.

Le corps lunaire visible et ses reflets restent présents de jour. Les données
solaires brutes sont conservées pour éclairer cette surface distante, même
lorsque le soleil est sous notre horizon ; sa phase et ses taches restent donc
correctes la nuit. Le gain d'éclairage lunaire n'ajoute plus de lumière directe
au terrain pendant le jour.

Le rendu saute aussi les cascades d'ombre de la source inactive, si elles sont
configurées, sans réallocation ni modification des cases `Active` / `Shadow`
de la scène. Les couches sautées sont invalidées pour être recalculées à leur
réactivation. L'atmosphère désactivée rend le contrôle aux lumières normales.
Les ombres lunaires demeurent désactivées par défaut dans la scène.

Il s'agit d'une bascule au passage du centre solaire sous l'horizon, sans
superposition des deux éclairages. Aucun cycle horaire automatique n'est ajouté.
L'ancienne approximation de faible lumière lunaire en journée a été retirée :
le rayon d'ombrage détaillé ne s'exécute que pour la source choisie.

Le [banc reproductible](../Tests/Atmosphere/benchmark_moon_clouds.py) compare les
shaders actuels à une référence autorisant les deux sources ; ses
[résultats](performance/moon-cloud-lighting-night.json) sont mesurés sur
**Mesa llvmpipe**, avec de petits buffers et des dispatchs synchronisés :

| Passe | Deux sources, jour | Soleil seul | Deux sources, nuit | Lune seule |
| --- | ---: | ---: | ---: | ---: |
| Nuages visibles, 96 × 54 | 18.54 ms | 10.18 ms | 17.07 ms | 9.87 ms |
| Volume, 64 × 36 × 32 | 1.73 ms | 0.99 ms | 1.64 ms | 1.11 ms |
| Ombres des nuages, 192 × 192 | 9.97 ms | 5.22 ms | 9.80 ms | 4.74 ms |

Les sorties sont bit-identiques à la même référence avec seulement la lumière
choisie présente. Ces nombres ne sont **ni des temps d'image complète, ni des
mesures sur GTX 1660**. Le rapport précédent `moon-cloud-lighting.json` conserve
les mesures de l'ancienne approximation diurne, désormais remplacée.

Les tests runtime couvrent midi, l'horizon exact, la nuit, les deux astres sous
l'horizon, le soleil absent et la désactivation de l'atmosphère. Ils vérifient
les canaux réels de l'atlas, la sélection CPU/GPU, les matériaux et la reprise
des cascades après leur suspension.
