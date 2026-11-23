# Musique d'exploration ENDURANCE

La scène `ENDURANCE/Scene/Terrain.scene` utilise maintenant
`EnduranceMusicComponent`. Il sélectionne les morceaux et confie leur lecture
à `AudioManager`, sur le bus musique de `SoundManager`. Aucune passe GPU ni
nouvelle interrogation du climat n'est ajoutée.

## Installation des morceaux

Par défaut, placer les trois dossiers sous `ENDURANCE/Ressources/Sound` :

```text
ENDURANCE/Ressources/Sound/Elderweald/...
ENDURANCE/Ressources/Sound/Otherworlds/...
ENDURANCE/Ressources/Sound/Ancient Horrors/...
```

Les sous-dossiers sont parcourus au démarrage de la scène et au rechargement
manuel uniquement. Le chemin racine est modifiable dans l'inspecteur du
composant. La casse et les espaces du nom des albums sont ignorés.
Les autres dossiers audio ne sont pas intégrés à la sélection.

Le chargeur accepte les **WAV PCM 8, 16 ou 24 bits, mono ou stéréo**.
Les 33 fichiers Elderweald livrés en 24 bits sont convertis vers du PCM 16 bits
avec dither au chargement, sur le worker audio. La fréquence, la durée et les
canaux sont conservés ; les fichiers source ne sont pas modifiés. La conversion
utilise un bloc d'entrée de 24 Kio et le tampon de sortie uniquement.
Les fichiers OGG/MP3/FLAC et les WAV float/32 bits sont signalés dans
`Music catalog warnings`, pas envoyés à un décodeur incompatible.
L'indexeur vérifie seulement les en-têtes WAV, sans charger leur contenu audio.
Les dossiers absents ne font pas planter la
scène : l'inspecteur indique le problème et propose `Reload music catalog`.

## Familles et identité musicale

| Famille | Intention et exemples de biomes |
| --- | --- |
| `Cold` | Glaces, toundras, forêts froides |
| `Verdant` | Prairies, forêts, jungles |
| `Vast` | Déserts, plateaux, steppes, grandes étendues |
| `Mystery` | Marécages, formations étranges, lieux volcaniques |
| `Ocean` | Mer, mangrove |
| `Lunar` | Lune et légende, uniquement sur événement explicite |

Les associations des **26 biomes** se trouvent dans `biomeFamilies()` dans
`ANTAGONIZ/Source/ProjectUser/EnduranceMusicCatalog.cpp`. Plusieurs familles
peuvent convenir à un même biome ; le tirage favorise les correspondances.

Les **77 morceaux actuellement fournis** disposent d'affectations explicites
dans le manifeste : 57 fichiers d'exploration, 8 de découverte et 12 lunaires.
La [répartition détaillée](BIOME_MUSIC_CATALOG.md) donne les familles, les
groupes de variantes et la matrice des 26 biomes. Il s'agit d'une direction
artistique fondée sur les titres, à affiner à l'écoute.

Pour de nouveaux morceaux sans entrée explicite, les mots des titres servent
de première classification. Sans mot reconnu,
Elderweald reçoit `Verdant | Mystery`, Otherworlds reçoit
`Cold | Vast | Mystery | Ocean`. C'est un classement initial, à affiner à
l'écoute. **Ancient Horrors reste exclusivement narratif/lunaire**, même si
un override tente de le rendre disponible en exploration. Ni la nuit ni la
simple présence de la lune ne déclenchent cet album.

## Ajustements par morceau

`ENDURANCE/Ressources/Sound/music_catalog.json` contient les affectations
explicites qui remplacent le classement automatique. Les nouvelles musiques
non renseignées restent importées automatiquement ; une liste vide utilise
uniquement ce classement de secours.
Exemple avec un nom fictif, à remplacer par le chemin affiché dans l'inspecteur :

```json
{
  "version": 1,
  "tracks": [
    {
      "id": "Otherworlds/WAV/Example.wav",
      "enabled": true,
      "group": "example-composition",
      "families": ["Cold", "Mystery"],
      "role": "exploration",
      "gain": 0.85
    }
  ]
}
```

Seul `id` est obligatoire ; les autres champs conservent leur valeur automatique
s'ils sont absents. `id` est relatif à `musicRoot`. `enabled: false` exclut un
morceau sans supprimer le fichier. `gain` va de 0 à 1 et multiplie le volume du
composant et celui du bus musique. `role` accepte `exploration`, `discovery` et
`lunar` ; une piste `discovery` ou `lunar` n'entre pas dans la rotation ordinaire.

Les variantes courantes (`Loop`, `Full`, `Short`, `Alt`, etc.) d'une composition
partagent automatiquement un groupe d'historique. Un `group` explicite permet
de corriger les noms particuliers. Les copies identiques par nom dans plusieurs
dossiers d'export sont dédoublonnées. Le tirage choisit d'abord une composition,
puis une variante, afin qu'une composition ayant beaucoup de versions ne
monopolise pas la lecture.

## Transitions, silences et répétition

- Le biome doit rester stable **12 secondes** avant de devenir la nouvelle
  référence. Un échantillon de climat invalide ou trop éloigné de la caméra
  interdit de commencer une nouvelle musique d'exploration.
- Un morceau compatible continue jusqu'à sa fin. Un changement incompatible
  attend au moins **30 secondes de lecture**, puis utilise un fondu.
- Fondus successifs de **4 secondes**, sans superposer deux compositions.
  Le fondu d'entrée commence à la lecture effective, après le chargement.
- Une respiration aléatoire de **20 à 60 secondes** suit les morceaux ordinaires.
  Mettre les deux valeurs à zéro pour une écoute continue, à l'exception des
  fondus et du temps de chargement. Les événements prioritaires peuvent passer
  avant cette respiration.
- Les **5 dernières compositions** sont évitées. Si le choix est trop réduit,
  la moins récemment entendue redevient disponible ; la même version n'est
  pas choisie immédiatement lorsqu'une alternative existe.
- La pause du moteur suspend lecture, transitions et décisions. Désactiver
  le composant arrête et libère la piste sans effacer l'historique.

Ces valeurs sont réglables dans l'inspecteur et sérialisées dans la scène.
`Reload music catalog` relit les fichiers et remet à zéro l'historique de la
session, les échecs de chargement et les délais des événements.

## Découvertes et événements lunaires

Appeler depuis le gameplay, sur le thread principal :

```cpp
if (auto* music = EnduranceMusicComponent::active())
{
    music->requestDiscovery("stele/northern-ridge");
    // Ailleurs, sur un véritable événement narratif :
    music->requestLunarEvent("moon/first-fragment");
}
```

La priorité par défaut est 50 pour une découverte et 100 pour la lune.
Une priorité supérieure peut interrompre un morceau de moindre priorité avec
un fondu. Une seule demande est en attente ; une demande plus importante peut
la remplacer. Les demandes expirent après 120 secondes pour éviter de jouer
une découverte longtemps après avoir quitté le lieu.

Par défaut, une clé d'événement ne joue qu'une fois **par session de scène**.
Pour un événement répétable, passer `once = false` ; son délai par défaut est
180 secondes. La clé n'est consommée qu'après le début réel de la lecture,
pas lors de la mise en attente. Cet état n'est pas encore un système de
sauvegarde des découvertes entre sessions.

Les pistes dédiées aux découvertes sont `Oathstone`, `Opened Door`,
`AriaOfAwakening`, `AwakenTheLight`, `Enshrinement`, `FinalSummon` et sa variante,
ainsi que `Resolve`. Elles ne jouent pas en exploration ordinaire.
Sans piste dédiée `discovery`, une découverte utilise une musique d'exploration
de mystère/grandes étendues ou adaptée au biome. Un événement lunaire ne prend
jamais cette solution de repli : il faut une piste `lunar` disponible.
Les boutons de prévisualisation de l'inspecteur permettent de tester les deux
cas sans attendre l'implémentation des événements de gameplay.

## Coût et cycle de vie

`EnduranceMusicDirector` est indépendant du moteur et testable sans OpenGL.
Il réutilise le biome dominant de l'atmosphère ; les allocations/recherches de
morceaux ont lieu aux changements de lecture, pas à chaque image.

`AudioManager` charge sur son worker et libère la piste précédente après son
fondu. Ce n'est **pas du streaming audio** : un morceau est chargé entièrement
en mémoire, mais les morceaux déjà terminés ne restent pas tous en cache.
Un fichier disparu/invalide est exclu pour la session, avec un délai avant une
autre tentative. Un arrêt annule aussi les chargements en attente ; une réponse
ancienne ne peut pas redémarrer le son ou remplacer une demande plus récente.

Validation sans périphérique audio (le dernier test lie le vrai directeur au
vrai `AudioManager`, avec seulement le périphérique et l'horloge simulés) :

```sh
bash Tests/run-music-catalog.sh
bash Tests/run-music-director.sh
bash Tests/run-audio-manager-lifecycle.sh
bash Tests/run-music-playback.sh
bash Tests/run-wav-loader.sh
# Optionnel : décoder également les 77 fichiers du jeu, un à la fois.
bash Tests/run-wav-loader.sh ENDURANCE/Ressources/Sound
```

L'écoute des transitions et l'équilibrage artistique restent à valider dans le
jeu avec les morceaux définitifs. Les tests ne mesurent pas les performances
sur la machine du joueur.
