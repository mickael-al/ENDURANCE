# Ciel, atmosphere et brouillard d’ENDURANCE

## Utilisation

Le composant **`EnduranceAtmosphereComponent`** relie le terrain d’ENDURANCE au
service generique `Ge::Atmosphere`. La scene `ENDURANCE/Scene/Terrain.scene`
le contient deja sur l’objet **Procedural terrain** : ouvrir cette scene et
lancer Play. Dans une autre scene, ajouter le composant a un objet. Son
inspecteur regroupe les palettes, l’affectation du soleil
et de la lune, les reglages atmospheriques, les nuages et les mesures GPU.
Un seul composant ENDURANCE doit piloter le ciel d’une scene.
Pendant son execution, une source explicitement choisie conserve son identite
si une autre lumiere est supprimee ; son index sauvegarde est reajuste. Supprimer
la source choisie la desaffecte au lieu de selectionner la lumiere suivante.

Le prereglage initial est **Ultra**. Les trois couleurs de chaque biome sont
proposees dans [la matrice des palettes](ATMOSPHERE_PALETTES.md). Le fichier
`ENDURANCE/Ressources/Atmosphere/biome_palettes.json` est la configuration
artistique initiale : 25 biomes climatiques et le fond marin. Les couleurs du
fichier sont **lineaires**, pas des valeurs sRGB a utiliser directement dans
un selecteur hexadecimal.

Le composant charge ce fichier lorsque la scene ne contient pas encore de
palettes. Les modifications de l’inspecteur sont ensuite enregistrees dans les
donnees du composant avec la scene. Le bouton **Reload biome palette asset**
remplace explicitement ces palettes par celles du fichier. Le bouton
**Reload climate / fog shaders** recompile les shaders de selection climatique
et l’atlas de brouillard. Les ressources GPU ne sont jamais serialisees.

### Reglages de depart

| Reglage | Valeur initiale | Effet |
| --- | ---: | --- |
| Qualite | Ultra | Resolutions et nombres d’echantillons eleves ; chaque budget reste remplacable. |
| Deplacement climatique | 0,1 m | Compare le XZ actuel a la derniere position soumise au calcul climatique. |
| Seuil de palette | 0,01 | Ecart maximal d’un canal ou d’un parametre par rapport a la derniere cible acceptee. |
| Fondu de palette | 1 s | Interpolation depuis l’etat actuellement affiche, mise a jour au temps de rendu. |
| Intervalle d’environnement | 0,20 s | Cadence de reconstruction du ciel cubique et de son IBL lorsqu’une actualisation est necessaire. |
| Fondu d’environnement | 0,20 s | Transition entre ensembles complets de ressources. |
| Distance atmospherique | 32 768 m | Profondeur couverte par le volume de perspective aerienne et les milieux locaux. |
| Atlas de brouillard | 128 × 128 en Ultra | Echantillonnage climatique spatial ; ses proprietes sont interpolees entre les noeuds. |
| Etendue demandee de l’atlas | 81 920 m | Le composant impose aussi un minimum de 2,5 fois la distance atmospherique. |
| Correction artistique | 0,38 | Controle initial par biome ; 0 conserve la radiance physique. |

La scene Terrain affecte explicitement sa lumiere directionnelle d’index 0 au
soleil et porte la distance atmospherique a **49 152 m** pour couvrir sa
distance de terrain existante de **40 096 m**. Son atlas agrandit donc
automatiquement son domaine a 122 880 m. Ces valeurs sont des reglages
initiaux, pas un benchmark de performance.
`targetGpuMilliseconds` est une cible affichee pour la mesure : elle ne garantit
pas une duree et n’effectue aucune attente GPU pour forcer cette duree.

## Responsabilites

| Partie | Proprietaire | Fichiers principaux |
| --- | --- | --- |
| Noms, IDs et correspondance climatique | Jeu | `ProceduralTerrainBiomes.hpp`, `field_climate.glsl`, `field_weights.glsl` |
| Couleurs et proprietes du brouillard par biome | Jeu | `biome_palettes.json`, `EnduranceAtmosphere.hpp/.cpp` |
| Selection au XZ de la camera et atlas de brouillard | Jeu | `ENDURANCE/Ressources/Shader/Atmosphere/` |
| Parametres physiques, LUT, integration des milieux | Moteur | `Atmosphere.hpp/.cpp`, `Asset/Shader/Atmosphere/` |
| Cubemaps HDR, convolution diffuse, prefiltrage GGX | Moteur | `environment.C.glsl`, `filter.C.glsl`, `brdf.C.glsl` |
| Composition sur opaques et transparents | Moteur | `RenderingEngine.cpp`, `Atmosphere/sample.glsl`, shaders de materiaux |
| Sauvegarde des choix propres a une scene | Composant de jeu + service generique | JSON du composant ; `serializeSettings` / `loadSettings` du service |

Le moteur ne charge pas la matrice de biomes et n’utilise aucun nom de biome.
Le jeu lui soumet un `AtmospherePalette` de couleurs et de parametres. Les
extensions possibles sont explicites :

- `setPaletteTarget` fournit une nouvelle cible artistique ; le moteur gere le
  seuil et le fondu.
- `setGameFogShader` fournit une fonction GLSL
  `vec4 geGameFog(vec3 worldPosition)` : RGB = coefficient de diffusion
  `sigma_s`, A = extinction `sigma_t`, en metres inverses.
- `setGameFogBinding` permet au jeu de lier ses textures et ses uniforms lors
  des passes concernees. Les textures de brouillard restent sur GPU.
- `setRadianceShader` fournit `vec3 geCustomizeRadiance(vec3 radiance,
  vec3 direction)` ; le meme hook est utilise pour le ciel et les milieux
  participants. Une personnalisation arbitraire doit conserver des valeurs
  finies, positives et raisonnables dans le domaine HDR.
- `setExternalEnvironment` accepte une cubemap HDR appartenant a l’appelant,
  accompagnee d’une revision. Cette source doit exclure les disques directs
  des sources deja representees par des lumieres directionnelles.
- `setSun` et `setMoon` affectent explicitement les lumieres atmospheriques.

La duree de vie des textures et des callbacks fournis doit couvrir leurs
utilisations par le moteur. Le composant ENDURANCE les retire avant de liberer
ses ressources. Les operations OpenGL restent sur le thread de rendu.

## Climat partage et echanges CPU/GPU

`fieldClimateColumn` est partage par le classificateur du terrain et les deux
calculs du jeu : palette de camera et atlas de brouillard. Il appelle exactement
la recette `Noise/climate_2d.glsl`, avec la seed du generateur actif. Les
parametres de macro-hauteur et de rivage proviennent de
`ProceduralGeneration::getConfig().field`. Les matrices compactees sont
produites par `makeBiomeCellTable`, puis les poids sont calcules par
`fieldSurfaceWeight`.

L’interpolation comprend les quatre voisins climatiques et, lorsque le terrain
le demande, le melange avec le biome marin. Elle ne depend ni du chunk courant,
ni du LOD, ni de la hauteur de la camera. La projection XZ sert exclusivement
a cette selection. La position Y complete reste utilisee pour les densites,
la transmittance et la perspective aerienne.

La recette climatique actuelle est relativement lourde : cinq couches,
huit octaves et deux axes. Un lancement de calcul ponctuel n’est donc pas
suppose gratuit. L’implementation conserve la recette GPU du terrain et utilise
un petit resultat de **64 octets**. Trois emplacements permettent de recueillir
les resultats differes :

1. Soumettre le calcul uniquement apres franchissement du seuil de deplacement
   ou modification de la seed/des parametres/des palettes.
2. Placer une fence apres l’ecriture du resultat.
3. Interroger cette fence avec un timeout nul dans les images suivantes.
4. Lire uniquement un resultat dont la fin GPU a deja ete constatee ; conserver
   le dernier ciel complet tant que les resultats sont en vol.
5. Rejeter les resultats d’une ancienne revision et ne pas appliquer une
   sequence plus ancienne que la derniere deja recueillie.

Ce choix implique une petite latence de resultat, visible dans l’inspecteur.
Il evite une lecture bloquante immediatement apres le dispatch. Les compteurs
indiquent la duree GPU du calcul, sa latence CPU/GPU et les images ou les trois
emplacements etaient occupes. Ils permettent de comparer une eventuelle
implementation CPU strictement equivalente sur le materiel cible.

Un changement de reglages du terrain encore **non applique** ne modifie pas le
ciel : la source de verite est le generateur en fonctionnement. Appliquer les
reglages du terrain pour qu’il fournisse le nouvel etat commun. Si la recette
GLSL elle-meme est modifiee, recharger aussi les shaders du composant
atmospherique pour utiliser la meme version compilee.

## Transitions et invalidation

Le seuil de mouvement est compare a la derniere position soumise. Des
translations inferieures a 0,1 m ne remettent donc pas continuellement la
reference a zero. De meme, une palette refusee parce qu’elle est trop proche
ne remplace pas la derniere cible acceptee. Les petites differences successives
peuvent s’accumuler jusqu’au seuil.

Une nouvelle cible part de la couleur actuellement affichee. Elle ne reprend
pas le point de depart d’un ancien fondu. Le temps accumule utilise les secondes
reelles transmises au rendu ; l’interpolation conserve des points de depart et
d’arrivee fixes. Elle progresse lorsque la camera s’arrete et lorsque les
comportements de jeu sont suspendus par la pause de l’editeur. La preparation
s’effectue apres la mise a jour de la camera active.

Les dependances physiques sont distinctes des changements de palette :

| Ressource | Principales dependances |
| --- | --- |
| Transmittance | Rayons de la planete/atmosphere, coefficients et profils de Rayleigh/Mie/ozone. |
| Diffusion multiple | Atmosphere et albedo du sol ; indexee par altitude et angle solaire. |
| SkyView | Altitude camera, directions/couleurs des sources, parametres physiques et correction artistique. La rotation de la camera ne change pas un ciel couvrant toutes les directions. |
| Environnement HDR et IBL | Ciel, palette affichee, sources, nuages/vent, source externe et sa revision. |
| Volumes vus par la camera | Matrices de vue/projection, altitude/position, distance, ombres courantes, atmosphere et milieux locaux. |
| Bruit volumetrique des nuages | Seed et configuration de la texture procedurale. Le vent deplace l’echantillonnage, pas la texture generee. |
| Ombres des nuages | Couche nuageuse, couverture/densite, vent/temps, sources et domaine spatial. |
| Atlas de brouillard ENDURANCE | Seed, parametres de rivage, palettes/proprietes des milieux et deplacement hors de la zone de garde. |

Les nuages et leurs ombres continuent d’evoluer avec le temps lorsque la camera
est immobile. La cadence de reconstruction de la cubemap reste configurable
pour limiter le cout de l’IBL animee.

## Cubemap HDR et eclairage indirect

La capture produit les six faces du ciel visible et une source d’eclairage
indirect distincte, sans les disques directs du soleil et de la lune. Cette
separation evite d’ajouter leur energie directe une seconde fois par l’IBL.
Le halo atmospherique autour des sources reste dans l’environnement lumineux.
L’intensite visuelle d’un disque et celle de la lumiere directionnelle sont
des controles differents. Le rendu visible dispose aussi d’une version du
ciel sans disque : le disque est evalue analytiquement a la resolution
d’affichage pour conserver sa taille angulaire aux petites resolutions de
cubemap, sans ajouter une seconde copie de son energie.

L’irradiance diffuse et les reflections speculaires sont conservees, meme avec
un gradient doux. La convolution diffuse utilise un echantillonnage du
hemisphere pondere par le cosinus et stocke `E / pi`. Le prefiltrage speculaire
utilise une distribution GGX et plusieurs niveaux de rugosite. Les mipmaps
ordinaires de la source fournissent une reduction de frequence pendant ce
filtrage ; elles ne remplacent pas les niveaux GGX.

La LUT BRDF integree est une ressource generique persistante. Elle n’est pas
reconstruite lorsque la camera change de biome. Les environnements sont
construits dans des ressources intermediaires ; leurs six faces, irradiance
et niveaux speculaires doivent etre completes avant publication. Le fondu
concerne un ensemble coherent de ces ressources, avec les memes coefficients.
La palette artistique publiee suit ce meme melange pour la diffusion dans les
volumes ; une nouvelle transition repart de cet etat reellement affiche.

Le domaine reste lineaire et HDR jusqu’aux passes bloom et tone mapping.
Deux reglages de saturation permettent d’ajuster separement l’atmosphere
visible et l’eclairage indirect sans devoir refaire la matrice de palettes.
La saturation du ciel s’applique apres la composition du ciel et des volumes,
ainsi qu’a la lumiere diffusee devant les objets ; elle ne desature ni leur
couleur de surface ni la transmittance. L’IBL conserve son propre reglage.

## Atmosphere, perspective aerienne et transparence

L’atmosphere utilise un modele RGB spherique : diffusion de Rayleigh,
diffusion/absorption de Mie, absorption par l’ozone et sol lambertien. La
transmittance est calculee avec la loi de Beer-Lambert. La diffusion simple
et une approximation de diffusion multiple isotrope alimentent le ciel et
le milieu entre la camera et les objets. C’est une adaptation temps reel
inspiree des references de Hillaire et Bruneton, pas une reproduction integrale
d’un simulateur spectral.

Les volumes vus par la camera stockent la radiance accumulee `S` et la
transmittance `T`. La composition d’une surface est :

```text
radiance_finale = radiance_surface * T + S
```

L’atmosphere, le brouillard local et les nuages contribuent aux coefficients
d’extinction et de diffusion **avant** l’integration de chaque segment. Il ne
s’agit pas de trois voiles appliques successivement a l’image. Le ciel utilise
une reference atmospherique pour ajouter les effets locaux et les occultations
sans compter de nouveau l’air deja present dans sa cubemap.

La composition opaque dispose de la position/profondeur de la surface.
Les materiaux transparents utilisent leur **propre** position de fragment
via `geAtmComposite`, avant leur melange alpha. Ils ne doivent pas utiliser la
profondeur de l’objet opaque derriere eux. Un shader transparent personnalise
qui contourne les helpers du moteur doit appeler la meme interface pour
recevoir la perspective aerienne.

Les anciennes passes de fog doivent etre ignorees lorsque ce systeme est
actif. Elles restent utilisables dans les scenes qui ne l’activent pas.
Aucune reconstruction temporelle n’est requise : les volumes sont integres
avec une resolution configurable et interroges a la profondeur du fragment.
Cela evite les trainees dues a un historique reprojete, sans supprimer le
besoin de verifier les silhouettes et les objets fins a faible resolution.

## Brouillard dans les biomes voisins

Le climat n’est pas recalcule a chaque pas de ray marching. Le jeu genere un
atlas GPU de proprietes du milieu sur un domaine **ancre dans le monde**.
Chaque noeud utilise exactement le meme climat et les memes poids que le
terrain. Les proprietes sont interpolees entre les noeuds. L’extinction nulle
represente un biome sans brouillard local.

L’atlas contient les coefficients de diffusion/extinction et les controles de
hauteur et de bruit. Les controles sont ponderes par la densite : un voisin
sans brouillard ne deplace pas artificiellement l’altitude d’une nappe humide.
Le calcul final applique une decroissance exponentielle verticale et,
facultativement, un bruit 3D de la bibliotheque du projet.

Une camera dans un desert peut donc voir la nappe d’un marais voisin. La nappe
ne suit pas le biome choisi pour le ciel ni la position de la camera. L’origine
de l’atlas se decale par multiples de sa grille ; les noeuds qui restent dans
son domaine conservent les memes valeurs. Les tests couvrent ce recentrage.

Le cout de reconstruction de l’atlas est mesure separement de son utilisation
dans les volumes. Le budget automatique vaut 32² en Low, 64² en Medium, 96² en
High et 128² en Ultra. Un remplacement manuel est arrondi au multiple de 16
suivant pour conserver des decalages de grille entiers. A l’etendue initiale,
les noeuds Ultra sont espaces de 640 m ; le bruit local 3D est evalue a la
position du pas de ray marching et conserve ses details plus fins. Sa resolution peut etre reduite si les changements de milieu
restent assez lents dans le monde. L’etendue effective couvre le domaine du
ray marching, avec une marge pour les deplacements avant le prochain recentrage.

## Rayons lumineux et nuages

Les god rays proviennent de l’occultation de la lumiere diffusee dans les
volumes. La visibilite de chaque source est evaluee a partir des cascades
d’ombres du moteur et de l’attenuation nuageuse. Les nuages ne sont pas une
condition pour obtenir des rayons : une montagne peut masquer le soleil dans
une atmosphere claire ou dans le brouillard local.

La portee des cascades et la marge accordee aux obstacles sont exposees. En
dehors de la couverture reelle d’une shadow map, le shader ne repete pas son
pixel de bord ; il traite explicitement le point comme eclaire. Une montagne
hors ecran peut produire une ombre si sa geometrie residente est rendue dans
les cascades. Augmenter la marge d’ombres ne cree pas les chunks qui n’ont pas
ete generes : la distance de streaming du terrain reste a dimensionner avec
celle du rendu et des ombres.

Les nuages utilisent une texture 3D procedurale combinant Perlin et Worley,
avec erosion des details, profil vertical et deplacement du domaine par le
vent. Les fonctions de bruit deja presentes dans le projet sont reutilisees.
Les controles comprennent couverture, densite, base de couche, epaisseur,
echelle des grandes formes, details, seed et vent. L’extinction solaire des
nuages influence aussi l’eclairage direct des objets et les rayons lumineux.
La lumiere a l’interieur des nuages utilise un ray marching vers la source.

Le modele est une couche procedurale configurable. Il ne simule pas encore une
meteorologie dynamique, des types de nuages multiples independants ou un
transport spectral complet de diffusion multiple dans les gouttelettes.

## Validation et limites de la livraison

Les commandes, preuves et parcours visuels sont maintenus dans
[ATMOSPHERE_VALIDATION.md](ATMOSPHERE_VALIDATION.md). Les tests supplementaires
specifiques au jeu se lancent avec :

```bash
python3 Tests/Atmosphere/check_game_climate.py
```

Ils executent les shaders de production et verifient les 26 palettes forcees,
la correspondance des poids avec le classificateur terrain, les regions
voisines, l’attenuation avec la hauteur, les modes absent/lisse/bruite et le
recentrage de l’atlas. Le fichier de palettes a aussi ete charge et reserialise
avec le vrai parseur JSON C++ du moteur. La persistance des proprietes du
composant a ete verifiee dans le code ; un cycle complet sauvegarde/rechargement
depuis l’editeur reste a valider manuellement.

La validation realisee ici utilise un contexte EGL Mesa/llvmpipe. Elle prouve
la compilation et les invariants numeriques testes, pas les performances Ultra
sur une carte graphique de jeu. La compilation de l’editeur complet et les
controles visuels doivent etre rapportes separement dans le suivi de la branche.
Les transitions de biome, les ascensions rapides, les materiaux metalliques,
les obstacles hors ecran et les transparences superposees restent les scenes
prioritaires pour la verification visuelle sur le materiel cible.

### References

- Sebastien Hillaire, *A Scalable and Production Ready Sky and Atmosphere
  Rendering Technique*, EGSR 2020 :
  <https://sebh.github.io/publications/egsr2020.pdf>.
- Eric Bruneton, fonctions et documentation de *Precomputed Atmospheric
  Scattering* :
  <https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html>.
- Guerrilla, *Nubis: Authoring Real-Time Volumetric Cloudscapes with the Decima
  Engine* :
  <https://www.guerrilla-games.com/read/nubis-authoring-real-time-volumetric-cloudscapes-with-the-decima-engine>.
- Google, *Filament*, modele d’eclairage indirect et environnement :
  <https://google.github.io/filament/main/filament.html>.
