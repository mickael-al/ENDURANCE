# Revision de la PR #4 : correctifs et validation

Ce document conserve le plan de correction demande et les preuves disponibles.
La validation GPU de jeu et celle de l’editeur restent distinctes des tests numeriques executes ici.

## Correctifs implementes

| Point | Correction | Verification attendue |
| --- | --- | --- |
| 1. Ombres du terrain dans les nuages | La reference nuageuse soustraite pendant la composition conserve l’auto-ombrage des nuages, sans reprendre l’occultation par le terrain appliquee a la contribution courante. | Une montagne doit assombrir les nuages et le ciel concerne ; la soustraction de la reference ne doit pas annuler cette ombre. |
| 2. Visibilite solaire sans nuages et au ras de l’horizon | Un milieu nuageux absent fournit une visibilite neutre de 1. Les calculs de rayons et de carte restent finis et continus lorsque l’elevation du soleil approche zero. | Couverture ou densite nulle, soleil bas/rasant/sous l’horizon : absence d’ombre nuageuse artificielle et de discontinuites numeriques. |
| 3. Vent du brouillard ENDURANCE | Le composant integre un vent XYZ en metres par seconde au temps de rendu et decale uniquement le domaine du vrai bruit 3D. L’atlas climatique et le profil d’altitude restent fixes dans le monde. | Animation camera immobile, changement de vent sans saut, vent nul immobile, climat fixe et sauvegarde des reglages. |
| 4. Nuages visibles a la position courante | La contribution visible utilise la camera et le temps courants, avec une correction nuageuse signee a resolution reduite. La capture IBL conserve une cadence independante. | Deplacements rapides et rotation : absence de double position des nuages ou de retard lie a l’intervalle de capture IBL. |
| 5. Construction IBL repartie sur plusieurs images | Les faces, mipmaps ordinaires, irradiance et niveaux GGX sont construits par tuiles de 32 pixels. Une transaction garde ses parametres, LUT, bruit et source externe figes dans un ensemble de travail distinct. Publication d’ensembles complets uniquement. | Aucune face ni mipmap partielle publiee ; progression malgre les changements continus de camera/vent. Budget estime par mesures GPU asynchrones et plafond initial de 16 unites de travail par image. |
| 6. Cout des effets desactives | Les passes inutiles sont ignorees ; des ressources neutres de taille 1 × 1 × 1 remplacent les volumes de coefficients desactives. L’extinction nuageuse utilise un canal R32F. | Identites de composition preservees avec chaque effet desactive, pas de lecture de ressources non initialisees ; allocations et dispatches reduits. |
| 7. Formes et ouvertures des nuages | Une modulation meteorologique a grande echelle controle la couverture et les ouvertures ; trois profils verticaux proposent stratus, cumulus et tours. Reglages exposes dans ImGui et sauvegardes. | Presence d’ouvertures coherentes, profils visiblement distincts et aller-retour des nouveaux reglages de scene. |

## Complements de continuite

Deux corrections supplementaires apres l’audit de `eb84b0f` sont integrees,
relues et couvertes par les regressions dediees ci-dessous.

- **Premiere capture et palette visible** : la palette affichee reste fixe
  jusqu’a la premiere publication complete. Les cibles acceptees pendant la
  capture sont memorisees, puis le fondu reprend depuis l’etat publie vers la
  derniere cible. La duree nulle et une premiere capture annulee/recreee ne
  doivent pas provoquer un aller-retour vers une ancienne couleur.
- **Continuite du vent des nuages** : la vitesse XZ est integree en double
  precision. Un deplacement commun alimente meteo, densite, details et
  ombres ; chaque capture en conserve sa propre valeur figee. Changer ou
  stopper le vent conserve la formation a sa position courante. Une derniere
  mise a jour IBL reste possible apres l’arret pour rejoindre cette position.

Une recreation des ressources GPU du meme service conserve l’horloge,
le deplacement et la derniere palette affichee. Le redemarrage de sa capture
reprend depuis cet etat. Un nouvel objet `Atmosphere` commence a zero ; les
reglages de scene sauvegardent la vitesse, pas cet etat d’execution.

## Rendu complet : composition et correction des ombres

La correction des UV après `802d046` reste conservée : les compositions du
ciel et des opaques reçoivent des coordonnées dérivées de `gl_FragCoord`.
Les UV négatifs du triangle historique ne sont plus fournis aux volumes CLAMP.
Le contrôle des 18 dessins ciel/opaques reste séparé du contrôle des ombres.

### Pourquoi les anciens tests de plans ne suffisaient pas

Après le retour visuel sur `3b57f79`, le quadrillage est reproduit avec les
vrais shaders terrain, le VBO compacté, le G-buffer et le deferred du moteur.
Le filtre précédent extrapole un plan récepteur à chaque texel voisin ; sur
les bosses triangulées, il donne environ **53 % de faux pixels d’ombre**, avec
les marges de 0, 4 et 16 km. Un plan analytique continu n’exerçait ni les
changements de facette ni les dérivées traversant ces frontières dans le G-buffer.
Réduire uniquement la marge de profondeur ne corrige pas ce défaut.

Les anciennes conclusions sur un quadrillage résolu et sur des contacts de
50/100 mm à 40 km sont donc **supersédées** par les contrôles du terrain et des
occultants résolus décrits dans `ATMOSPHERE_VALIDATION.md`. Le succès historique
de 15 groupes ne vaut pas validation de la nouvelle passe.

### Version validée par la suite consolidée

- Quatre profondeurs sont comparées séparément, puis leurs visibilités sont
  interpolées. Le filtre ne prolonge plus un plan à travers les facettes.
- La normale géométrique est calculée sur la primitive, avant les branches
  et le discard, puis encodée sur 20 bits octaédriques dans `gPosition.w`.
  Le deferred la distingue de la normale de matériau. Les variantes forward
  disposent directement de leurs dérivées de primitive.
- `gPosition` reste RGBA32F/NEAREST. Les entiers négatifs encodés sont exacts ;
  ciel `-2`, émission positive et repli historique `0` gardent leurs rôles.
  Ce codage ne crée aucun attachement et n’ajoute aucun octet par pixel.
- Chaque directionnelle expose ses biais profondeur/normale en texels
  (défauts `0,5 / 1,5`, plage `[0,8]`, zéro explicite conservé). Leur stockage
  réutilise la fin du bloc lumière de 80 octets et les valeurs sont persistées.
- La marge de casters prend une base de **4 km** et s’étend par cascade selon
  les AABB des maillages réellement publiés. Elle ne réduit pas le plancher
  utilisateur. Le cache est révisé après la préparation des renderers,
  sans readback, sans nouvelle texture et sans modification de la résolution XY.

Le RGBA32F introduit lors de la passe précédente reste nécessaire aux grandes
coordonnées : son surcoût historique par rapport au 16F est de 8 octets par
pixel, soit 15,82 Mio à 1080p. Le nouvel encodage réutilise cet espace existant.
Le déplacement géométrique dépend de la taille mondiale des texels ; il peut
masquer des contacts trop petits pour la cascade. La marge numérique est
contrôlée séparément, sans promettre une précision de contact universelle.

**État courant : les 19 groupes de validation passent le 18 novembre 2026**,
avec sortie 0 de `run_all.py`. La chaîne du terrain réel couvre 18 combinaisons
et 438 732 évaluations de récepteurs sans faux auto-ombrage ; les occultants
résolus restent présents. Le contrôle négatif conserve l’ancien défaut.
Les deux étapes sont intégrées sur la branche de la PR #4. Aucune validation
dans l’éditeur Windows ni mesure de performances sur GPU matériel n’est revendiquée.

## Brouillard du jeu : implementation et contrat

`EnduranceAtmosphereComponent::fogWind` contient le vent XYZ, avec une valeur
initiale de `(4, 0, 1)` m/s. Son controle ImGui est **Fog wind XYZ (m/s)**. Le
champ est inclus dans le veritable enregistrement et la serialisation du
composant ; une ancienne scene qui ne le contient pas conserve les valeurs
par defaut.

`prepareRenderer` accumule le deplacement en double precision depuis
`ptrClass::time->getDeltaTime()`. Le hook recoit ce deplacement en metres par
`uGameFogWindOffset`. Il evalue le bruit en
`worldPosition - uGameFogWindOffset`, tout en calculant les UV climatiques et
l’attenuation verticale depuis `worldPosition` inchange.

L’integration de la vitesse conserve la position courante du bruit lorsqu’on
modifie le vent. Une vitesse nulle conserve cette position. Les deltas de
temps negatifs ou non finis sont ignores ; les axes de vent non finis sont
traites comme nuls et les vitesses sont bornees a ±100 m/s par axe. Le
deplacement continue pendant une pause des comportements et pendant une
disparition temporaire du generateur de terrain. Cette animation ne demande
aucune reconstruction de l’atlas climatique.

## Validation : état de reprise

La consolidation du 18 novembre 2026 couvre les 19 groupes ci-dessous,
y compris les régressions atmosphère/continuité des passes précédentes.
Le test du service C++ comprend toujours 18 groupes internes. Le contrôle
syntaxique avec les vrais en-têtes est distinct de l’exécution des tests EGL.

| Verification | Etat |
| --- | --- |
| `python3 Tests/Atmosphere/check_game_climate.py` | **PASS** : vrai hook du jeu, animation a position fixe, transport de structure, modes absent/lisse et profil Y conserves, atlas fixe et biome sec restant sec. |
| `python3 Tests/Atmosphere/check_game_component.py` | **PASS avec l’ordonnanceur multi-images** : sources C++ du composant et du moteur compilees sans modification, vrais uniforms/atlas/transmission, JSON, integration temporelle et reintegration du hook anime avec parametres moteur inchanges. |
| Syntaxe C++17 avec les vrais en-tetes du projet | **PASS** : Atmosphere.cpp, AtmosphereSettings.cpp et EnduranceAtmosphere.cpp. |
| `python3 Tests/Atmosphere/check_revision.py` | **PASS** : ombres conservees dans le ciel compose, milieu vide neutre au soleil rasant, loi analytique en milieu homogene, couverture regionale/profils, vrai ciel courant independant du cube IBL, deplacement commun meteo/densite/ombres independant de l’horloge. |
| Ordonnanceur C++ : cas de capture, budget, ressources et publication | **PASS** : snapshots stables, aucune publication partielle, revisions regroupees, mouvement continu sans famine, resize/reload conservant le dernier complet, cible temporelle influencant la soumission et plafond strict. |
| Suite consolidée `python3 Tests/Atmosphere/run_all.py` | **PASS : 19 groupes, sortie 0**, avec la reproduction et la correction du terrain réel. |
| Interfaces generiques supplementaires | **PASS** : cubemap externe visible au demarrage, IBL precedente conservee jusqu’a publication, fog anime par le temps sans callback. |
| Palette pendant la premiere capture, physique active et duree nulle | **PASS** : API, UBO et pixels HDR stables avant publication ; deux cibles successives pendant la capture ; aucune marche arriere et convergence vers la derniere cible. Cas physique avec fondu, gradient avec fondu, physique avec duree nulle. |
| Offset nuageux commun et changement/arret de vent | **PASS** : arret, changement de direction et reprise a 137 s, images de densite/vue/ombres sans saut, IBL au deplacement fige, publication de la position finale, precision double et cache aerien sans nuages. Le contrat de recreation GPU a egalement ete relu. |
| Composition et surface directionnelle | **PASS** : 18 dessins ciel/opaques ; helper à quatre comparaisons, 16 configurations avec/sans occultant et contrôles supplémentaires. Les contacts fins sont vérifiés avec des texels de 1 mm. |
| Positions à grande distance | **PASS** : vrai opaque/G-buffer32F, plan libre sans fausse ombre et occultants résolus de 2/4 m conservés jusqu’à 40 km. Les anciennes assertions de contact 50 mm sont retirées. |
| Normale dans `gPosition.w` | **PASS** : 33 388 normales, erreur vectorielle maximale 0,0041474, marqueurs réservés, émission/discard, normal map et liaison opaque/skinned/cloth. |
| Persistance des biais | **PASS** : vrais SceneData/json_struct, scènes anciennes, champs partiels, zéro et lumières distinctes ; clone/load/save relus. |
| Couverture adaptative | **PASS** : vrais Lights/LightManager, cache de publication, plancher, XY inchangés, absence d’allocation GPU ; six publications du vrai terrain à caméra immobile. |
| Terrain réel compacté → ombre → G-buffer → deferred | **PASS** : 18 combinaisons, 438 732 évaluations sans faux auto-ombrage, visibilité minimale 1 ; vrais occultants conservés. Contrôle indépendant de 252 triangles et 24 576 tags ; ancien helper figé toujours fautif. |
| Syntaxe de la couverture et de l’éditeur avec vrais en-têtes | **PASS** : Lights.cpp, LightManager.cpp, ProceduralGeneration.cpp, RenderingEngine.cpp et Editor.cpp. |
| Vérification visuelle dans l’éditeur Windows et mesures sur GPU cible | **NON EFFECTUÉES** : à réaliser sur la machine de jeu. |

Le test du composant attend `stats().environmentReady` pendant au plus 300
preparations de rendu avec `dt = 0`. Il ne fait donc pas avancer le vent pour
masquer le temps d’initialisation et ne suppose pas une taille fixe du bloc
uniforme atmospherique. Il verifie notamment l’egalite entre quatre secondes
en une image et quatre images d’une seconde, la continuite lors d’un changement
de vent et l’immobilite du volume avec un vent nul.
Un cas specifique avance ensuite le vrai composant sans nouvelle preparation
du service : a camera, parametres atmospheriques et atlas identiques, son hook
anime doit encore provoquer une integration et modifier la transmission. Ce
cas protege le contrat du fournisseur de brouillard contre une reutilisation
incorrecte du cache volumetrique du moteur.

Les tests numeriques sont executes sous OpenGL 4.5 Mesa/llvmpipe. Les fixtures
du test C++ remplacent les proprietaires de scene, les entrees camera/horloge,
l’interface terrain et ImGui ; elles conservent les sources de production du
composant, du service atmospherique et du compilateur GLSL. Cette validation
ne mesure pas les performances sur une carte graphique de jeu.

## Interfaces generiques controlees pendant la relecture

Une cubemap externe assignee avant la premiere capture est dessinee depuis
la source complete fournie par l’appelant, avec sa correction artistique.
L’IBL reste desactivee jusqu’a la premiere publication complete. Le ciel ne
passe plus par une texture non allouee pendant cette initialisation.

Un shader de brouillard peut animer son milieu a partir du temps du bloc
atmospherique sans avoir besoin d’un callback de liaison. La presence de ce
shader suffit donc a empecher une reutilisation abusive du volume statique.
Le chemin ENDURANCE conserve en plus son callback d’offset de vent.

Les snapshots de capture sont reutilises lorsque leur format, leurs dimensions
et leurs mipmaps ne changent pas. Les compteurs de stockage incluent les
ensembles publies, les ressources courantes et les copies privees, afin de ne
pas confondre l’economie des seuls coefficients 3D avec la memoire totale.
