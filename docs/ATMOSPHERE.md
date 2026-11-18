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
| Intervalle d’environnement | 0,20 s | Delai minimal entre transactions IBL ; une transaction peut occuper plusieurs images. |
| Fondu d’environnement | 0,20 s | Transition entre ensembles complets de ressources. |
| Distance atmospherique | 32 768 m | Profondeur couverte par le volume de perspective aerienne et les milieux locaux. |
| Atlas de brouillard | 128 × 128 en Ultra | Echantillonnage climatique spatial ; ses proprietes sont interpolees entre les noeuds. |
| Etendue demandee de l’atlas | 81 920 m | Le composant impose aussi un minimum de 2,5 fois la distance atmospherique. |
| Correction artistique | 0,38 | Controle initial par biome ; 0 conserve la radiance physique. |
| Nuages visibles en Ultra | 320 × 180 | Contribution nuageuse courante reconstruite sous le ciel et les disques a pleine resolution. |
| Travail IBL par image | 16 unites maximum | Tuiles initiales de 32 × 32 pixels ; plafond distinct du budget temporel estime. |
| Echelle meteorologique | 48 000 m | Modulation regionale de la couverture, distincte des formes nuageuses de 6 000 m. |

La scene Terrain affecte explicitement sa lumiere directionnelle d’index 0 au
soleil et porte la distance atmospherique a **49 152 m** pour couvrir sa
distance de terrain existante de **40 096 m**. Son atlas agrandit donc
automatiquement son domaine a 122 880 m. Ces valeurs sont des reglages
initiaux, pas un benchmark de performance.
`targetGpuMilliseconds` pilote un budget de soumission souple : les mesures GPU
differees estiment le travail IBL admissible apres les autres phases. Le plafond
`iblMaxWorkUnitsPerFrame` borne separement le nombre d’unites soumises. Une unite
minimale assure la progression. Le GPU ne pouvant pas interrompre une commande
deja soumise, la valeur en millisecondes ne constitue pas une garantie de temps
d’image ; aucune attente GPU n’est ajoutee pour l’imposer.

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

Au premier demarrage, la palette artistique affichee reste fixe pendant la
construction de la premiere capture IBL. Les nouvelles cibles continuent
d’etre acceptees selon le seuil, mais leur fondu ne progresse pas en arriere-plan.
La premiere publication reprend donc exactement cette palette visible, puis
le fondu part de cet etat vers la derniere cible acceptee. Une duree nulle
reste autorisee ; elle ne libere pas la palette pendant la premiere capture.
Annuler cette capture pour un changement de qualite ou un rechargement de
shader conserve le meme etat visible jusqu’a une publication complete.

La liberation puis recreation des ressources GPU du meme service conserve
son horloge, le deplacement cumule des nuages et sa derniere palette affichee.
La premiere capture recreee repart de cette palette avant de reprendre une
eventuelle cible en attente. Un nouvel objet `Atmosphere` commence avec un
deplacement nul. Cette continuite en memoire ne constitue pas une sauvegarde
du temps ou du deplacement dans le JSON de scene.

Les dependances physiques sont distinctes des changements de palette :

| Ressource | Principales dependances |
| --- | --- |
| Transmittance | Rayons de la planete/atmosphere, coefficients et profils de Rayleigh/Mie/ozone. |
| Diffusion multiple | Atmosphere et albedo du sol ; indexee par altitude et angle solaire. |
| SkyView | Altitude camera, directions/couleurs des sources, parametres physiques et correction artistique. La rotation de la camera ne change pas un ciel couvrant toutes les directions. |
| Environnement HDR et IBL | Ciel, palette, sources, nuages/vent/meteo, source externe et sa revision ; etat fige pour toute la transaction. |
| Nuages visibles | Camera et matrices courantes, deplacement de vent integre, sources, meteo et palette affichee ; cadence independante de l’IBL. |
| Volumes vus par la camera | Matrices de vue/projection, altitude/position, distance, ombres courantes, atmosphere et milieux locaux. |
| Bruit volumetrique des nuages | Seed et configuration de la texture procedurale. Le vent deplace l’echantillonnage, pas la texture generee. |
| Ombres des nuages | Couche nuageuse, couverture/densite, meme deplacement integre que la densite, sources et domaine spatial. |
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
des controles differents. Le rendu visible emploie sa propre vue des nuages depuis la camera actuelle.
Une passe reduite stocke la difference signee entre ciel nuageux et ciel clair.
Le dessin du ciel ajoute cette contribution au SkyView courant ; le disque
est evalue analytiquement a la resolution d’affichage. Le gradient clair et
la taille angulaire des disques ne dependent donc pas de la resolution de la
passe nuageuse. Le fondu entre captures IBL ne superpose plus deux positions
anciennes des nuages visibles. Une cubemap externe reste une source fournie
par l’appelant, avec son propre contenu.

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
La palette artistique publiee suit ce meme melange pour le ciel courant et
la diffusion dans les volumes ; une nouvelle transition repart de cet etat
reellement affiche. Le vent, la position et l’orientation de la camera ne
sont pas retardes par le fondu de l’IBL.

Un quatrieme ensemble est reserve au travail en cours. Les captures et les
filtres parcourent les faces par tuiles ; la generation des mipmaps ordinaires
precede les convolutions. Chaque transaction conserve une copie GPU de ses
LUT, de son bruit et de sa source externe ainsi que ses parametres et son
instant de capture. Une modification continue est retenue pour la transaction
suivante, sans recommencer sans cesse celle qui est en cours. Les anciennes
ressources completes restent disponibles pendant une reconstruction. Les
compteurs exposent progression, revisions, unites soumises, cout estime et
empreinte des textures.

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
sans compter de nouveau l’air deja present dans le ciel courant. Cette
reference conserve l’auto-ombrage des nuages mais exclut les ombres du terrain.
La contribution finale les conserve : leur soustraction ne peut donc plus
annuler l’ombre d’une montagne sur un nuage.

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

## Coordonnees ecran et precision des ombres de surface

La composition atmospherique deferred utilise `gl_FragCoord.xy *
uAtmosphereInvResolution` pour les objets comme pour le ciel. Les passes
concernes dessinent dans un viewport d’origine `(0,0)` ayant les dimensions
du framebuffer. Le triangle historique de `ModelManager` conserve ses UV de
texture : leur Y dans `[-1,0]` fonctionne avec les textures G-buffer en
REPEAT, mais ne doit pas alimenter les volumes atmospheriques en CLAMP.
Les transparents utilisent la meme convention ecran, a leur propre profondeur.

Le filtre directionnel commun se trouve dans
`Asset/Shader/Shadow/directional_surface.glsl`. Il effectue **quatre lectures**
de profondeur et interpole les quatre comparaisons de visibilite (PCF
bilineaire). Il ne prolonge plus le plan d’un triangle sur les texels de ses
voisins : cette extrapolation produisait du quadrillage sur les facettes du
terrain, meme avec une marge de casters nulle.

La normale servant au biais est geometrique, independante de la normale de
shading et du microdetail. Les vrais fragment shaders opaque et terrain la
calculent avec les derivees de leur primitive, avant les branches ou le
discard. Ils la conservent dans le canal **deja existant** `gPosition.w`, avec
le contrat partage de `Geometry/gbuffer_geometry.glsl` :

| Valeur de `gPosition.w` | Signification |
| --- | --- |
| Strictement positive | Emission, valeur conservee. |
| `-2` | Ciel. |
| `0` ou ancien indicateur non encode | Surface historique ; repli sur les derivees deferred. |
| Entier de `-1048579` a `-4` | Normale geometrique en espace vue, encodage octaedrique de 10 bits par axe. |

Les tags sont exactement representables dans le RGBA32F et sont lus en
NEAREST. Le deferred decode la normale puis la tourne en espace monde. Les
transparents et l’eau utilisent les derivees de leur propre primitive. Il
n’y a ni nouvel attachment, ni texture supplementaire, ni passe de calcul.
Les shaders personnalises peuvent appeler `geGBufferGeometryTag` pour
beneficier du meme contrat ; les anciens producteurs restent compatibles.

Chaque lumiere directionnelle expose deux controles ImGui, en texels de la
cascade, independants de la portee Z :

| Controle | Defaut | Fonction |
| --- | --- | --- |
| `Shadow depth bias (texels)` | `0.5` | Decalage vers la lumiere. |
| `Shadow normal bias (texels)` | `1.5` | Decalage selon la normale geometrique, ajuste a l’inclinaison. |

Le facteur d’inclinaison utilise `max(sin(angle), sqrt(0.5))` : ce plancher
conservateur tient compte du filtre qui peut chevaucher des facettes de pentes
differentes, meme si la facette courante fait face au soleil.

Ils sont bornes dans `[0,8]`, sauvegardes dans `LightData`, charges et copies
avec la lumiere. Les anciennes scenes obtiennent les valeurs par defaut.
Les champs utilisent le padding du bloc de lumiere : son stride reste de
80 octets. Un biais plus fort peut detacher une ombre ou supprimer un detail
plus petit que l’empreinte d’un texel ; ces controles ne remplacent pas une
resolution de cascade suffisante.

Le biais numerique ajoute 5 mm, une borne de l’incertitude de position XYZ
projetee selon la lumiere et un plancher d’arrondi de profondeur. Il reste
separe du decalage geometrique en texels. Les indices, projections et points
hors carte sont controles ; les echantillons hors carte restent eclaires.

Les positions vue de `gPosition` sont en **RGBA32F** a la creation et au
redimensionnement, depuis la correction precedente. Le passage precedent
de 16F a 32F ajoutait 8 octets par pixel pour cet attachment : **+15,82 Mio a
1920 x 1080**, **+28,125 Mio a 2560 x 1440**, **+63,28 Mio a 3840 x 2160**.
L’encodage geometrique de cette passe n’ajoute aucun octet a ce stockage.
Les couleurs HDR conservent leur format.

References : [coordonnees de fragment OpenGL](https://wikis.khronos.org/opengl/Fragment_Shader/Defined_Inputs),
[limites de la compensation par plan recepteur](https://learn.microsoft.com/windows/win32/dxtecharts/cascaded-shadow-maps)
et [biais des ombres dans Unity HDRP](https://docs.unity.cn/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Shadows-in-HDRP.html).
La validation du vrai terrain, des facettes et des petits occultants se trouve
dans `ATMOSPHERE_VALIDATION.md`.

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
facultativement, un bruit 3D de la bibliotheque du projet. Le vent de
brouillard `fogWind` XYZ, initialement `(4, 0, 1)` m/s, deplace uniquement ce
bruit. Le composant integre sa vitesse au temps rendu, meme camera immobile.
Changer la vitesse conserve la position courante de la structure ; regler les
trois axes a zero l’immobilise. L’atlas climatique et le profil d’altitude
restent fixes. Le vent est modifiable dans ImGui et sauvegarde avec le
composant, sans provoquer une regeneration de l’atlas.

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

La marge de casters explicite est un **plancher de 4 000 m par defaut**, et
non un plafond ou une augmentation du format de profondeur. Chaque cascade
peut etendre son volume vers la lumiere a partir des AABB des maillages
publies qui recouvrent sa projection XY. Les splits, la resolution des
textures et leur format ne changent pas. La construction en double de la
base et de la matrice evite qu’une extension purement Z fasse varier XY par
soustraction de grandes positions.

`CustomRenderer` fournit deux hooks optionnels, revision et ajout des bounds.
Le terrain expose tous ses chunks publies non vides, y compris hors champ,
et non seulement ceux du frustum camera. `LightManager` conserve ces bounds
en cache et finalise les matrices apres tous les `prepareRenderer`. Une
publication stationnaire invalide donc aussi le culling de la passe d’ombres
courante. Le calcul reste CPU, sans lecture GPU ; il parcourt les bounds par
cascade lors d’une actualisation de matrice. Il ne ralloue pas les textures.

Les renderers sans bounds restent couverts par le plancher explicite ; un
caster de ce type situe au-dela demande une marge adaptee. Une ancienne
scene sauvegardee a 16 km conserve ce reglage. La scene `Terrain.scene`,
dont la distance de terrain est 40 096 m, ne sauvegardait pas de marge et
herite du defaut de 4 km. Des chunks charges peuvent encore necessiter une
extension superieure a 4 km, voire a 16 km, selon la cascade et le soleil.

En dehors de la couverture reelle d’une shadow map, le shader traite le point
comme eclaire ; il ne repete pas son pixel de bord. Une montagne hors ecran
peut projeter une ombre si sa geometrie residente est dessinee dans les
cascades. Augmenter la marge ne cree pas les chunks absents : la distance de
streaming reste a dimensionner avec celle du rendu et des ombres.

Les nuages utilisent une texture 3D procedurale combinant Perlin et Worley,
avec erosion des details, profils verticaux et deplacement du domaine par le
vent. Les fonctions de bruit deja presentes dans le projet sont reutilisees.
Les controles comprennent couverture, densite, base de couche, epaisseur,
echelle des grandes formes, details, seed et vent. L’extinction solaire des
nuages influence aussi l’eclairage direct des objets et les rayons lumineux.
La lumiere a l’interieur des nuages utilise un ray marching vers la source.
Un milieu nuageux absent fournit toujours une transmission de 1. Les rayons
rasants sont bornes depuis leur origine, et la projection de leur carte
d’ombre est regularisee et fondue aux bords de son domaine. Il n’existe plus
de seuil d’elevation qui eteint brutalement le soleil. La carte reste une
approximation spatiale : le ray marching explicite eclaire l’interieur des nuages.

La carte meteorologique regionale est une tranche 2D du bruit pregenere,
echantillonnee a une echelle independante. Cela reutilise son stockage, sa
seed et ses mipmaps. `cloudWeatherScale`, `cloudWeatherStrength` et
`cloudWeatherContrast` reglent les grandes ouvertures. La couverture globale
0 reste entierement vide et la couverture 1 ne produit pas d’ouvertures
regionales. `cloudProfileMix` melange stratus, cumulus et tours ;
`cloudProfileVariation` permet de faire varier ce melange dans le monde.
Les controles sont exposes dans ImGui et serialises avec les reglages moteur.

Le vent nuageux est une vitesse XZ en metres par seconde. Le service integre
cette vitesse en double precision, puis fournit un seul deplacement en
metres dans le bloc uniforme commun. La meteo regionale, les grandes formes,
les details, l’auto-ombrage et la carte d’ombres echantillonnent tous ce meme
domaine deplace. Modifier la vitesse ou l’inverser ne repositionne pas la
formation courante ; un vent nul la conserve a sa derniere position.

Chaque capture IBL fige ce deplacement avec ses autres parametres et ses
textures, pendant que la vue courante continue d’utiliser la position actuelle.
Si le vent s’arrete pendant la capture, une actualisation ulterieure peut
encore etre necessaire pour publier la position finale. Les caches suivent
le deplacement effectivement utilise. Lorsque les nuages sont desactives,
leur deplacement ne provoque pas de nouveau calcul des volumes statiques.

La vue courante ajoute du travail par image : en Ultra, 320 × 180 pixels,
40 pas primaires et 8 pas vers une source representent jusqu’a 18,4 millions
d’evaluations de densite sur les rayons lumineux, par source, avant les
echantillons primaires. Les zones vides et la terminaison par extinction
reduisent ce pire cas. La resolution de vue et les pas lumineux sont donc
des budgets utiles a mesurer sur la carte cible ; etaler l’IBL ne borne pas
a lui seul le cout de ce rendu courant.

Le modele est une couche procedurale configurable. Il ne simule pas encore une
simulation meteorologique dynamique, des couches nuageuses independantes superposees ou un
transport spectral complet de diffusion multiple dans les gouttelettes.

## Allocation et passes conditionnelles

Les coefficients physiques et nuageux inactifs utilisent des textures neutres
1 × 1 × 1 ; les shaders ne les lisent pas avec les coordonnees d’un volume
complet. Les passes correspondantes sont ignorees. L’extinction nuageuse,
identique sur RGB, utilise R32F. Les coefficients colores gardent RGBA32F
pour ne pas perdre les faibles valeurs en metres inverses.

Pour les six volumes 3D principaux, en Ultra 160 × 90 × 96 avec tous les
milieux actifs, le stockage theorique des texels passe de **105,47 Mio a 89,65 Mio**, soit
**15,82 Mio de moins**. Ce chiffre exclut les cubemaps, les LUT et les copies
privees des captures ; les compteurs de textures permettent de suivre leur
cout total. Il ne constitue pas une mesure de gain de temps GPU.

## Validation et limites de la livraison

Les commandes, preuves et parcours visuels sont maintenus dans
[ATMOSPHERE_VALIDATION.md](ATMOSPHERE_VALIDATION.md). Les tests supplementaires
specifiques au jeu se lancent avec :

```bash
python3 Tests/Atmosphere/check_game_climate.py
python3 Tests/Atmosphere/check_game_component.py
python3 Tests/Atmosphere/check_revision.py
```

Ils executent les shaders de production et verifient les 26 palettes forcees,
la correspondance des poids avec le classificateur terrain, les regions
voisines, l’attenuation avec la hauteur, les modes absent/lisse/bruite et le
recentrage de l’atlas. Le fichier de palettes a aussi ete charge et reserialise
avec le vrai parseur JSON C++ du moteur. La persistance et le vent des proprietes du
composant sont verifies en executant le vrai C++ avec son mecanisme REGISTER ;
un cycle complet sauvegarde/rechargement
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
