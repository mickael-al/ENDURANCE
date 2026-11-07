# Steam et réseau dans ENDURANCE

Le socle multijoueur est intégré au moteur pour préparer la suite du développement. Il fournit des sessions et un transport de messages ; il ne synchronise pas encore les personnages, la physique, le monde ou la progression d'ENDURANCE, et n'ajoute pas de menu multijoueur.

## Provenance

Les composants sont repris de la branche la plus récente de Parasite au moment de l'import, le 7 novembre 2026 : [`agent/skybox-bellgrave-port`, commit `675a941b88c6c88f08a7f8a5ae2277b211fcef11`](https://github.com/mickael-al/Parasite/tree/675a941b88c6c88f08a7f8a5ae2277b211fcef11). Cette branche inclut les changements de `master` et de `agent/steam-release-network-fixes`, ainsi que les dernières métadonnées de langue des salons.

- `ANTAGONIZ/Header/Network/` et `Source/Network/` : transport, sessions, sérialisation RPC, langues des salons, arguments d'invitation Steam et contrôle d'intégrité optionnel.
- `ThirdParty/steamworks_sdk/` : en-têtes publics et bibliothèques d'import/exécution Windows et Linux issus de cette même révision. Les octets des fichiers copiés ont été vérifiés contre leurs empreintes Git.
- `ThirdParty/hv/` : sources officielles de [libhv v1.3.3, commit `b7928e930710df51c055fc2345f6871eeee04530`](https://github.com/ithewei/libhv/tree/b7928e930710df51c055fc2345f6871eeee04530). Parasite ne contenait pas les sources ni les bibliothèques compilées nécessaires ; le sous-ensemble TCP de libhv est donc compilé ici depuis les sources. Les 51 en-têtes communs à cette sélection et à Parasite sont identiques. Les modules HTTP/WebSocket optionnels ne sont pas utilisés.

Les règles de Parasite, ses succès, son interface et ses états de joueur vivant/fantôme ne sont pas importés. `NetworkText.hpp` conserve uniquement les messages réseau en français et en anglais. Le moteur issu de BellGrave et les scènes/ressources existantes d'ENDURANCE sont conservés.

## Fonctionnalités disponibles

| Composant | Fonction |
| --- | --- |
| `OnlineSessionManager` | Création/recherche/connexion aux salons Steam, noms, langues, capacité, salons privés, mots de passe, invitations et départ de session. |
| Migration d'hôte Steam | Reconstitution du transport avec le nouveau propriétaire du salon ; le futur jeu devra restaurer lui-même son état partagé. |
| Connexion IP directe | Serveur/client TCP via libhv ; disponible même si Steam n'est pas lancé. |
| `NetworkManager` | Envoi fiable, envoi non fiable Steam avec repli TCP, suivi des pairs, contrôle des émetteurs et admission. |
| `FunctionRegistry` | Sérialisation des appels et vérification des messages reçus avant leur exécution. |
| `ResourceIntegritySession` | Échange et comparaison d'une empreinte de ressources, sur activation explicite du futur jeu. |

Steam utilise les clés de salon `endurance_game`, `endurance_version` et `endurance_language`, la valeur de jeu `endurance` et un protocole d'admission propre à ENDURANCE. Même avec l'AppID de test partagé, les salons Parasite ne sont pas compatibles. Le transport IP direct doit relier des versions compatibles d'ENDURANCE.

## Compilation et AppID

Le réseau est activé par défaut :

```bash
cmake -S . -B build/Debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/Debug
```

| Option CMake | Valeur par défaut | Utilisation |
| --- | --- | --- |
| `ENDURANCE_ENABLE_NETWORK` | `ON` | Active le code réseau, Steamworks et libhv. `OFF` compile l'éditeur sans ces dépendances. |
| `ENDURANCE_STEAM_APP_ID` | `480` | AppID de test Spacewar. À remplacer par l'AppID propre à ENDURANCE lorsqu'il sera disponible. |
| `ENDURANCE_DEPLOY_STEAM_APP_ID` | `ON` | Écrit `Bin/steam_appid.txt` pour Debug et RelWithDebInfo. |

La bibliothèque Steam d'exécution est copiée à côté de l'exécutable. Le fichier `steam_appid.txt` de développement est supprimé lors d'une compilation Release ou sans réseau. Une version distribuée par Steam utilise son contexte de lancement Steam ; les tests hors Steam doivent utiliser une configuration de développement adaptée.

Pour compiler sans réseau :

```bash
cmake -S . -B build/Offline -DCMAKE_BUILD_TYPE=Debug -DENDURANCE_ENABLE_NETWORK=OFF
cmake --build build/Offline
```

L'AppID de Parasite n'est pas réutilisé. Les essais réels de salons/invitations nécessitent un client Steam lancé et les comptes/machines de test appropriés. Les connexions directes à travers Internet nécessitent un port TCP joignable ; ce mode ne fournit pas de relais NAT.

## Raccordement au futur gameplay

Le moteur crée `onlineSessionManager`, initialise Steam avant la fenêtre OpenGL, exécute les callbacks pendant sa boucle et ferme les connexions à l'arrêt. L'absence de Steam ne bloque pas le démarrage. Le point d'entrée traite les arguments d'invitation Steam et les transmet au gestionnaire.

Dans un composant compilé avec le réseau :

```cpp
#if ENDURANCE_ENABLE_NETWORK
auto* sessions = Ge::Engine::getPtrClass().onlineSessionManager;
sessions->setMinimumPlayers(2); // Valeur par défaut, configurable.

// Sur l'hôte, à la demande de la future interface :
sessions->hostDirect(27015);
// Ou : sessions->createSteamLobby("Expédition", false, "", 8, "fr");

// Sur une autre instance :
// sessions->joinDirect("127.0.0.1", 27015);
#endif
```

Utiliser `getState()`, `getLastError()`, `getLobbyList()` et `getPlayers()` pour construire l'interface. Les invitations sont exposées par `consumeLobbyInvite()` et `consumeLobbyPasswordRequest()`. Le lancement d'une partie est un événement via `startGame()`/`consumeLaunchRequest()` : le gestionnaire ne charge pas de scène de jeu à lui seul.

Le gameplay enregistrera ses RPC sur `getNetworkManager()`, avec `registerClientToServerFunction()` ou `registerServerToClientFunction()` suivant l'autorité attendue. Leur exécution arrive lors de la mise à jour du réseau sur le thread principal. Une migration d'hôte recrée le gestionnaire de transport : ne pas conserver son pointeur indéfiniment ; utiliser `getInstanceId()` pour détecter un remplacement et réenregistrer les callbacks.

Le contrôle des ressources reste désactivé tant qu'ENDURANCE n'a pas de collecteur propre. Pour l'utiliser, appeler `beginResourceIntegrityCheck()` après la création/connexion de la session sur chaque participant, puis `completeResourceIntegrityCheck(digest, error)` avec l'empreinte calculée. Une erreur ou une annulation après activation maintient le blocage jusqu'au départ de session ; cela ne doit pas être utilisé comme un interrupteur de contournement.

## Vérifications

Les tests réseau sans client Steam se lancent avec :

```bash
python3 Tests/Network/check_all.py
```

Ils exécutent le code de transport et de session avec des interfaces Steam/TCP contrôlées, et vérifient notamment les messages malformés, les permissions RPC, les callbacks tardifs, les invitations, l'isolation des salons, l'intégrité optionnelle et la migration d'hôte. Ils ne remplacent pas une partie Steam entre deux machines.

Validation de cet import :

- Compilations Linux Debug complètes avec `ENDURANCE_ENABLE_NETWORK=ON` et `OFF` réussies.
- `python3 Tests/Network/check_all.py --sanitize --sdk-check` réussi : 31 scénarios de session, transport, RPC, 20 cas d'arguments d'invitation, langues et 655 vérifications d'empreintes. AddressSanitizer et UndefinedBehaviorSanitizer activés ; compilation du transport et des sessions contre les véritables en-têtes Steam.
- Connexion TCP réelle sur `127.0.0.1` avec libhv : connexion, attribution d'identifiant, appel client vers serveur, réponse ciblée, déconnexion/reconnexion et fermeture propre validés, sans client Steam.

La compilation Windows, l'affichage de l'overlay et les échanges Steam entre deux comptes/machines restent à vérifier dans l'environnement correspondant.
