# Préparation CPU et génération GPU du terrain

Cette passe conserve les formes des biomes, les paramètres de scène et les
budgets de génération. Elle corrige l'attente entre groupes, soumet plus tôt
les émissions de maillage et évite une seconde recherche des mêmes voisins
pendant la sélection LOD. Référence : `30082ee50564d92a8ac1ddbf0563ee97ac608cd5`.

## Anticiper le groupe suivant

Auparavant, `ProceduralGeneration::update()` attendait la génération et la
publication complète du groupe courant avant de demander le plan CPU suivant.
Le résultat ne pouvait être consommé qu'à une mise à jour ultérieure. Les jobs
GPU retombaient donc à zéro à chaque frontière de groupe, même avec du terrain
restant à construire.

Le thread de planification existant prépare maintenant **un groupe d'avance**
pendant le calcul GPU du groupe courant. Il travaille sur une copie immuable
de la couverture prévue, associée à la révision de publication attendue.
Après publication, le moteur adopte ce plan et soumet ses chunks dans la même
mise à jour s'il est prêt. Le GPU conserve un seul groupe actif, avec les
limites habituelles par image et de travaux simultanés.

Les groupes restent publiés atomiquement pour préserver les raccords LOD.
Un échec invalide les plans anticipés, même si une future tentative réutilise
le même numéro de révision. Un déplacement significatif rejette la préparation
spéculative devenue obsolète. Les demandes ordinaires continuent cependant à
progresser pendant un déplacement continu, afin de ne pas affamer la génération.
Les opérations sans nouveau mesh sont limitées à quatre passages par mise à jour.

La planification utilisait déjà un thread persistant et les collisions deux
workers. Cette passe fait mieux se chevaucher leur travail avec celui du GPU ;
elle n'ajoute pas de threads supplémentaires ni d'appels OpenGL dans le worker.

## Soumettre les émissions sans attendre un nouveau chunk

Après le comptage GPU, `GpuMesher::poll()` peut lancer l'émission des sommets.
Auparavant, ces nouvelles commandes attendaient un prochain envoi explicite ou
un flush du rendu. Un flush conditionnel en fin de `poll()` les soumet maintenant
immédiatement. Plusieurs émissions partagent le même flush. Aucun `glFinish`
ni attente bloquante n'est ajouté ; une interrogation inactive ne fait aucun
flush supplémentaire.

## Réutiliser les recherches de voisins LOD

La sélection vérifiait les six voisins de chaque feuille pour équilibrer
l'octree, puis répétait les mêmes recherches pour construire les masques de
raccord. Le premier passage calcule désormais aussi ces masques temporaires.
Ils sont appliqués uniquement lorsque l'équilibrage est terminé ; aucun masque
intermédiaire n'est transmis aux enfants issus d'une subdivision.

Sur cinq essais alternant l'ordre avant/après, les médianes sont **299,43 ms
avant et 172,30 ms après pour 96 sélections**, soit environ **42,5 % de temps CPU
en moins pour cette étape**. Les 183 632 feuilles produites par essai, avec leurs
origines, LOD, masques et ordre, sont identiques octet par octet. Ce résultat
n'est pas une réduction de 42,5 % du temps total de génération ni une mesure FPS.

## Lire les compteurs

| Compteur | Signification |
| --- | --- |
| `Queue (current group)` | Chunks du groupe courant qui ne sont pas encore soumis ; pas tout le terrain restant. |
| `GPU jobs` | Chunks soumis en attente de fin ; pas la profondeur de file matérielle ni l'occupation GPU. |
| `CPU plan` | Une demande ou un résultat CPU reste en attente de consommation. |
| `next group: ready` | Un plan anticipé est conservé jusqu'à sa révision de publication. |
| `Planning waits` | Mises à jour sans job GPU ni groupe actif, en attente du CPU. |
| `immediate handoffs` | Mises à jour publiant un groupe puis soumettant le suivant. |

Un zéro reste possible au démarrage, si le CPU n'a pas encore terminé, pendant
des opérations de retrait sans nouveau mesh, ou après convergence. Le but est
d'enlever l'attente imposée entre deux groupes dont le suivant est déjà prêt.

## Vérification et limites

Le test du scheduler compile le vrai code de mise à jour et le vrai worker CPU,
avec un backend GPU de test dont la latence est contrôlée. Sur le chargement
initial de 292 chunks en 15 publications, l'ancien scheduler présentait 16
mises à jour sans job GPU alors que la génération était inachevée, dont 15 aux
publications ; le nouveau en présente zéro dans cet essai et effectue 14 relais
dans la même mise à jour. Le scénario passe de 476 à 460 mises à jour simulées.
Ce test vérifie aussi les mouvements de caméra, l'annulation après échec, la
reprise, les budgets, la stabilité au repos et la géométrie des raccords publiés.
Il ne mesure pas le temps GPU matériel.

La suite du backend réel passe sous OpenGL/Mesa : deux émissions sont soumises
par un flush, 64 cycles inactifs ne produisent aucun flush supplémentaire et les
retraits partagent leur flush. Le test démontre d'abord l'échec de l'ancien code.
Les tests CPU, streaming et planificateur passent également. Les fichiers du
générateur et de son interface compilent avec les vrais en-têtes du moteur.

Le jeu complet et le pilote Windows ne sont pas exécutés dans cet environnement.
Le gain total doit être mesuré dans la même scène, avec les mêmes paramètres,
après chauffage du cache de shaders, à l'aide de `Coverage generation` et des
temps GPU par étape. **Recompiler le moteur** pour utiliser cette passe.

- [Mesures de sélection LOD](terrain-optimization/lod_selection.json)
- [Comparaison du scheduler](terrain-optimization/scheduler.json)
- [Commandes de validation](../Tests/Terrain/README.md)
