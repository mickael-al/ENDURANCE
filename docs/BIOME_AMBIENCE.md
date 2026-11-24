# Ambiances des biomes

Les 17 WAV de `ENDURANCE/Ressources/Sound/Ambiant` sont raccordés à la scène
existante via `EnduranceMusicComponent`. Aucun ajout manuel à `Terrain.scene`
ni nouveau calcul de climat n'est nécessaire. Le composant transmet le même
échantillon valide de biome aux deux lecteurs et utilise le même délai de
stabilité, `biomeHoldSeconds` (12 secondes par défaut).

La musique conserve ses règles : morceau compatible, durée minimale, événements
prioritaires et respirations. L'ambiance ne s'arrête pas pendant ces respirations
ni lorsqu'une musique lunaire intervient. Le son reste relatif à l'auditeur,
sans distance d'atténuation ni lecture de la géométrie.

## Répartition

Les correspondances et gains par biome se modifient dans `biomeProfiles()` de
`ANTAGONIZ/Source/ProjectUser/EnduranceAmbience.cpp`. Les noms exacts des fichiers
et le traitement éventuel du raccord de boucle sont dans `recordings()`.

| Biomes | Fond continu | Détails espacés |
| --- | --- | --- |
| PolarIce, RockyTundra | Vent de montagne | Craquements de glace |
| FrozenTundra | Vent irrégulier | Craquements de glace |
| Tundra, ColdSteppe, DryPlain | Vent irrégulier | Aucun |
| Taiga, BorealForest, TemperateForest, DryJungle | Forêt | Craquements d'arbres |
| BorealBog, ColdMarsh, TemperateMarsh | Ambiance alpine avec insectes | Eau discrète |
| ColdForest, TemperateRainforest | Forêt | Eau discrète |
| Meadow, Savanna | Ambiance alpine avec insectes | Aucun |
| DrySavanna | Vent irrégulier | Insectes |
| TropicalJungle | Forêt | Insectes |
| RockDesert, RedDesert | Vent désertique | Aucun |
| AridPlateau | Vent de montagne | Aucun |
| VolcanicDesert, BurningJungle | Lave et vent | Grondement volcanique |
| TropicalMangrove | Littoral | Insectes |
| Marine | Littoral | Aucun |

Les gains varient entre les biomes partageant un fichier. Traverser deux biomes
qui utilisent le même fond modifie seulement le gain, sans rechargement ni retour
au début de l'enregistrement. Les détails sont des lectures uniques, espacées
par un intervalle aléatoire après leur fin ; les longues prises de forêt sont
conservées entières. Les sons d'eau sont ici une couleur sonore de biome, pas
une simulation de rivière localisée.

## Fondus et cycle de vie

- Fondu croisé de **4 secondes** entre les fonds. L'ancien reste audible pendant
  le chargement asynchrone du suivant ; le fondu démarre à sa lecture effective.
- Une nouvelle cible en cours de transition réutilise les voix existantes. Une
  troisième cible attend une place libre ; aucune accumulation de voix.
- Une lecture devenue obsolète avant son démarrage est annulée par le jeton de
  chargement d'`AudioManager`. Un retour vers l'ancien biome inverse le fondu
  depuis son niveau actuel.
- Un échantillon de climat indisponible depuis plus de deux secondes retire
  l'ancien fond en fondu ; aucune nouvelle ambiance n'est choisie sur un
  échantillon périmé après une téléportation.
- La pause du moteur suspend les sons, délais et fondus. L'arrêt de la scène
  annule les demandes, rejoint le worker puis libère les sources et buffers.
- Un fichier absent/invalide est exclu pour la session. Un fond manquant tente
  le vent irrégulier comme repli ; aucun nouvel essai à chaque image. Le bouton
  existant de rechargement du catalogue réinitialise aussi ces exclusions.

## Météo

`EnduranceBiomeEffectsComponent::effectStrength()` expose en lecture seule les
enveloppes des effets déjà actifs. Aucun événement météo n'est créé par l'audio.
La désactivation du rendu de particules ne coupe pas à elle seule le son de
l'événement ; désactiver le composant d'effets le retire.

| Événements visuels actifs | Son |
| --- | --- |
| Rain, HeavyRain, Sleet, Hail | Pluie, pondérée par l'intensité |
| Thunderstorm | Pluie et deux coups de tonnerre alternés, espacés |
| Blizzard | Grandes rafales de neige |
| Sandstorm, AshStorm | Vent violent avec débris |
| LunarDustFall, MeteorShower | Galactic Wind |

Une seule couche de fond météo, la plus intense, s'ajoute au fond du biome.
Le tonnerre remplace temporairement le détail naturel. Il suit l'événement
orage, avec une cadence sonore indépendante ; il n'est pas encore synchronisé
à chaque flash visuel individuel. Les prises pluie/vent servent aussi de
solutions provisoires pour grêle et cendres, faute d'enregistrements dédiés.

## Volumes sans interface

Valeurs publiques et sérialisées dans `EnduranceMusicComponent`, déclarées dans
`ANTAGONIZ/Header/ProjectUser/EnduranceMusic.hpp`. Les anciennes scènes utilisent
ces valeurs par défaut ; aucun contrôle ImGui supplémentaire n'est ajouté.

| Variable | Défaut | Rôle |
| --- | --- | --- |
| `volume` | `0.65f` | Volume musique existant |
| `ambienceVolume` | `0.55f` | Volume général des ambiances, détails et météo |
| `ambienceDetailVolume` | `0.45f` | Multiplicateur des détails naturels |
| `weatherVolume` | `0.85f` | Multiplicateur météo et tonnerre |
| `ambienceTransitionSeconds` | `4.0f` | Durée des fondus d'ambiance |
| `ambienceEnabled` | `true` | Activation indépendante de la musique |

Exemple pour un futur menu d'options :

```cpp
if (auto* audio = EnduranceMusicComponent::active())
{
    audio->volume = 0.50f;
    audio->ambienceVolume = 0.40f;
    audio->ambienceDetailVolume = 0.35f;
    audio->weatherVolume = 0.70f;
}
```

Le gain final d'une ambiance vaut `gain du profil × ambienceVolume × bus SFX`.
Pour un détail ou la météo s'ajoute son multiplicateur. La musique conserve son
bus Music et les gains du catalogue. Le gain général du listener du moteur
s'applique toujours aux deux. Les valeurs sont bornées entre 0 et 1 ; un volume
nul rend le bus silencieux sans recréer les sources. Pour libérer les sons et
arrêter les chargements, utiliser `ambienceEnabled = false`.

## Coût et vérifications

- Un worker audio supplémentaire pour l'ambiance, dormant hors chargements.
- **Cinq voix d'ambiance au maximum** : deux fonds de biome durant un fondu,
  deux fonds météo durant un fondu, un détail/tonnerre. En régime stable,
  généralement un fond, plus les couches réellement actives.
- Une demande annulée déjà en cours de décodage peut encore posséder un buffer
  temporaire, mais ne devient pas audible. Pas de cache de tous les fichiers.
- Aucun draw, shader, lecture GPU, recherche sur le terrain ou parcours de
  dossiers ajouté par image. Le chargement est asynchrone mais **pas streamé** :
  chaque WAV utilisé est décodé entièrement, y compris les prises de 96/192 kHz.
- Les enregistrements non conçus comme boucles reçoivent un raccord court
  tête/queue lors du chargement, en place sur le PCM décodé. Cela raccourcit la
  boucle du temps de raccord, sans réécrire le fichier ni traiter la musique.
  Les boucles fournies comme telles restent intactes. OpenAL boucle dès la
  première lecture, sans attendre l'update suivante.

```sh
bash Tests/run-ambience.sh
bash Tests/run-audio-manager-lifecycle.sh
bash Tests/run-music-playback.sh
bash Tests/run-music-catalog.sh
bash Tests/run-music-director.sh
bash Tests/run-wav-loader.sh
```

Les tests utilisent le vrai lecteur d'ambiance et le vrai `AudioManager`, avec
le périphérique audio simulé. Ils couvrent notamment stabilité des biomes,
chargement lent/obsolète/échoué, inversion et interruption de fondu, pause,
volumes, fin de détail, événements, nombre borné de sources et raccord PCM.
Le contrôle des en-têtes des 17 assets confirme des WAV PCM16/24 stéréo acceptés
par le décodeur actuel. L'équilibrage des prises et l'écoute des raccords restent
à valider dans le jeu ; aucune mesure de performances sur GTX 1660 n'est revendiquée.
