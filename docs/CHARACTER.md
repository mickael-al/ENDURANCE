# ENDURANCE — personnage, étape 1

La scène `ENDURANCE/Scene/Terrain.scene` contient un empty **Character Manager**
avec `EnduranceCharacterComponent`. En mode Play, il crée le personnage, son
squelette partagé, les matériaux et la caméra. Stop libère les objets et rejoint
les tâches en cours. Aucun déplacement ni contrôleur de collision du terrain
n'est activé par ce composant.

## Utilisation

- Bouton droit maintenu : tourner le pivot de caméra.
- Molette : avancer/reculer la caméra dans l'espace local du pivot.
- Inspecteur du Character Manager : position, caméra, force du vent, activation
  des cheveux et des vêtements, réinitialisation des simulations.
- La taille et le nombre de particules sont des paramètres de construction :
  arrêter puis relancer Play après leur modification.

Le point de départ est fixe en `(0,0,0)`, pieds à cette hauteur. Le composant ne
cherche pas le sol : selon le terrain généré, modifier **Character position**
dans l'inspecteur pour déplacer la présentation sans activer les collisions.
Ce changement explicite dans l'inspecteur réinitialise la physique secondaire.

`ENDURANCE/Scene/CharacterPreview.scene` fournit aussi une présentation isolée,
avec une vue de trois quarts face, le ciel et son éclairage IBL, sans terrain ni
nuages, pour régler le personnage.

## Import et rendu

Le FBX livré contient 15 meshes pondérés, 128 os et aucune animation. Son corps
mesure environ 1,597 m hors cheveux ; la transformation du FBX et les bounds de
peau servent à le normaliser à 1,70 m, pieds à Y=0. Aucun facteur d'échelle
arbitraire n'est appliqué aux vertices ou aux poses de liaison.

Les noms de matériaux et les chemins de textures accompagnent désormais chaque
sous-mesh importé. La politique du jeu identifie les parties par noms de
matériaux et de meshes, jamais par leur position dans le fichier. Le mesh `Body`
est la chevelure arrière ; `Body.001` est le corps. Les textures des quatre
vêtements sont résolues vers `dress`, `belt`, `pants` et `ring` ; les autres
utilisent le nom de fichier des références FBX, sans dépendre du chemin Windows
de l'auteur.

Le jade dense est opaque : Fresnel/GGX diélectrique, reflets IBL, ombres et
diffusion interne approchée dans le pipeline forward existant. Pas de réfraction
plein écran ni de rendu supplémentaire de transmission. Les textures du visage
et leurs découpes alpha restent utilisées. Les détails des yeux ne projettent
pas d'ombres sur les couches du visage. Les tissus utilisent le pipeline différé
avec normal maps et rugosité élevée. Le shader jade ne contribue actuellement
pas au G-buffer utilisé par le SSAO.

La caméra réserve sa première cascade d'ombres aux 16 premiers mètres afin de
conserver des ombres détaillées sur le personnage, tout en gardant une portée
de vue de 100 km. Les autres caméras conservent leur répartition habituelle.

## Simulation CPU

Un `ThreadPool` existant, limité à deux workers, exécute des tâches courtes de
cheveux, robe et corde. Il n'y a qu'une tâche active par simulation, avec entrées
copiées et résultats complets ; aucune opération OpenGL sur les workers. Le
thread de rendu publie les dernières géométries et poses terminées sans attendre
chaque frame. Le chargement initial construit les proxies une seule fois.

- Cheveux : chaînes `J_Sec_Hair*`, racines attachées à la tête, ressorts amortis,
  longueurs conservées, limites angulaires et capsules de tête/cou/haut du torse.
- Robe : haut fixé, transition progressive autour de la ceinture, bas libre.
- Corde : tours de taille et nœud attachés, extrémités pendantes libérées.
- Pantalon et bandeau : squelette seul.

La position de la ceinture est déduite de ses parties larges, pour ne pas
confondre les glands très détaillés avec les tours de taille. Les masques sont
continus. Le solveur de tissu et sa reconstruction existants sont réutilisés,
avec 720 particules demandées pour la robe et 240 pour la corde. Les collisions
du corps utilisent des capsules simples ; il n'y a pas encore de collision
mutuelle précise entre robe et corde, ni de collision avec l'environnement.
Les capsules du bassin et des cuisses enveloppent aussi le pantalon ample pour
éviter qu'il traverse la robe. La résistance à la flexion de la robe est réglée
pour préserver ses pans superposés, avec une base libre de réagir au vent.

Le vent utilise la vitesse d'air des effets de biome existants, avec un vent de
repli réglable si ces effets sont absents. Les nuages conservent leur vitesse
atmosphérique indépendante. La traînée dépend de la vitesse relative de l'air,
de la surface exposée et d'une accélération bornée.

## Suite : locomotion procédurale et hybride

`setBasePose()` accepte une pose globale de squelette issue du procédural ou de
l'IK résolu par l'appelant. Si une animation est jouée sur le squelette, sa pose évaluée devient la
base. La publication secondaire via `applyProceduralGlobalPose()` conserve les
états d'animation et les cibles IK, contrairement à l'ancienne publication
exclusive destinée au ragdoll.

Le branchement de l'IK natif reste l'étape suivante : ses noms d'os devront être
adaptés au rig `J_Bip_*`, et son évaluation devra fonctionner sans clip actif.
Cette étape ne produit pas encore les déplacements ni les appuis du personnage.

`setRootPosition(position, teleport)` distingue déplacement et vraie
téléportation. Les déplacements rapides ne déclenchent pas automatiquement un
reset du tissu. Lors de l'étape locomotion, vérifier le retard des snapshots de
tissu sur le haut du corps et les collisions durant les accélérations/rotations
brutales ; la présente étape valide d'abord le personnage immobile au vent.

## Vérification

```sh
bash Tests/run-character-import.sh
bash Tests/run-character-asset.sh
bash Tests/run-character-pose.sh
bash Tests/run-cloth-simulation.sh
bash Tests/run-cloth-async.sh
bash Tests/run-memory-pool.sh
bash Tests/run-shadow-cascade-splits.sh
EGL_PLATFORM=surfaceless python3 Tests/Character/check_shaders.py
```

`Tests/Character/run_engine_smoke.py` lie une scène de test aux objets compilés
du moteur, en remplaçant seulement son point d'entrée. Il vérifie les shaders et
les matériaux réels, les résultats des simulations, puis Stop/Start, et produit
des vues de face et de dos. Un build Ninja hors réseau et un affichage OpenGL
avec les extensions du moteur sont nécessaires ; `--xvfb` permet d'utiliser un
serveur X virtuel. Les temps sous rendu logiciel ne constituent pas une mesure
de performance sur la machine du joueur.

L'option `--atmosphere` ajoute le composant de ciel réel et attend une capture IBL
publiée avant les images. Les options `--no-cloth`, `--no-shadows` et
`--no-normal-maps` permettent d'isoler un problème de drapé ou d'éclairage.
