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

## Validation : etat de reprise

| Verification | Etat |
| --- | --- |
| `python3 Tests/Atmosphere/check_game_climate.py` | **PASS** : vrai hook du jeu, animation a position fixe, transport de structure, modes absent/lisse et profil Y conserves, atlas fixe et biome sec restant sec. |
| `python3 Tests/Atmosphere/check_game_component.py` | **PASS avec l’ordonnanceur multi-images** : sources C++ du composant et du moteur compilees sans modification, vrais uniforms/atlas/transmission, JSON, integration temporelle et reintegration du hook anime avec parametres moteur inchanges. |
| Syntaxe C++17 avec les vrais en-tetes du projet | **PASS** : Atmosphere.cpp, AtmosphereSettings.cpp et EnduranceAtmosphere.cpp. |
| `python3 Tests/Atmosphere/check_revision.py` | **PASS** : ombres conservees dans le ciel compose, milieu vide neutre au soleil rasant, loi analytique en milieu homogene, couverture regionale/profils, vrai ciel courant independant du cube IBL. |
| Ordonnanceur C++ : cas de capture, budget, ressources et publication | **PASS** : snapshots stables, aucune publication partielle, revisions regroupees, mouvement continu sans famine, resize/reload conservant le dernier complet, cible temporelle influencant la soumission et plafond strict. |
| Suite consolidee `python3 Tests/Atmosphere/run_all.py` | **PASS : les 12 groupes**, le 18 novembre 2026, apres integration de tous les correctifs. |
| Interfaces generiques supplementaires | **PASS** : cubemap externe visible au demarrage, IBL precedente conservee jusqu’a publication, fog anime par le temps sans callback. |
| Verification visuelle dans l’editeur et mesures sur GPU cible | A effectuer sur la machine de jeu. |

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
