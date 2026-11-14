# FrozenTundra : chaînes montagneuses

La recette repart du nettoyage utilisateur `a60b334`. Les anciennes plaines, pingos et cavités ne sont pas réintroduits. Le biome est un relief de chaînes courbes ; sa contribution volumique est nulle.

## Réglages

Dans [relief.C.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/relief.C.glsl), la variable demandée reste à un :

```glsl
float mountainPresence = 1.0;
```

Elle multiplie l'ensemble du relief des chaînes. `0` le supprime, `0.5` réduit son amplitude de moitié et `1` conserve la hauteur complète. Elle peut être remplacée par un masque de la position monde `p` pour choisir les zones montagneuses sans déplacer les massifs. Le macrorelief commun au monde continue à s'appliquer.

Les autres paramètres se trouvent dans [chain.glsl](../Asset/Shader/ProceduralTerrain/biomes/FrozenTundra/chain.glsl) :

| Constante | Valeur | Signification |
| --- | --- | --- |
| `FROZEN_WIDTH_MIN/MAX` | 1000 / 6000 m | Largeur longitudinale nominale d'un massif. |
| `FROZEN_GAP_MIN/MAX` | −500 / +250 m | Écart signé entre empreintes principales successives : négatif = chevauchement. |
| `FROZEN_HEIGHT_MIN/MAX` | 1400 / 3200 m | Hauteur nominale des sommets, relative au macrorelief, à `detailScale=0.35`. Les grands massifs tendent à être plus hauts. |
| `FROZEN_CHAIN_SEPARATION` | 12000 m | Distance nominale entre les axes des chaînes voisines. |

Les largeurs et écarts sont mesurés sur l'axe longitudinal tourné. Les axes courbes, les contreforts et les cols font varier les contours visibles : un écart positif entre deux empreintes principales n'impose pas une bande de plaine nue entre elles. Les cols maintiennent la continuité de la chaîne. Les bornes de recherche des voisins sont validées pour ces paramètres ; les revoir si la largeur maximale augmente fortement ou si les axes sont beaucoup rapprochés.

## Construction

Chaque chaîne suit un axe déformé à deux grandes échelles. Quatre massifs de tailles variables occupent un bloc directement adressable ; des paires complémentaires garantissent une somme constante des largeurs et écarts. Les paramètres restent déterministes avec la seed, sans liste de sites sur CPU ni parcours depuis l'origine. Les blocs voisins sont évalués pour conserver les chevauchements et les cols aux frontières, y compris en coordonnées négatives.

Le relief combine une crête principale courbe, deux crêtes secondaires descendantes, des contreforts irréguliers, un cirque latéral et un bruit ridged à plusieurs octaves. Les extrémités se rétrécissent. Les massifs et les cols sont assemblés par maximum, sans addition qui gonflerait les zones de chevauchement.

Ce modèle est une construction procédurale dirigée artistiquement, pas une simulation d'érosion ou de tectonique.

## Limites et validation

Le plafond global CPU/GLSL passe de ±4096 à ±10240 unités brutes, soit ±3584 m à l'échelle 0,35, afin de ne pas écrêter les sommets. Les bornes CPU reflètent ce plafond. Cela libère aussi les reliefs d'autres biomes auparavant écrêtés, notamment les réglages polaires 900–3160 m déjà enregistrés par l'utilisateur. Leurs fichiers sont conservés à l'identique.

La scène sauvegardée de 40,096 km demande maintenant une limite de sélection de 3072 au lieu de 2048. Le contrôle CPU mesure au maximum 2668 feuilles sur six positions, dont deux caméras en altitude. Le budget de sommets GPU et le maximum de chunks résidents ne changent pas. Les réglages par défaut à courte distance passent encore avec 2048 feuilles.

- 8192 empreintes sur deux seeds : largeurs 1000,37–5999,63 m, écarts −499,93 à +249,93 m ; erreur maximale de raccord des écarts inférieure à 1,5 cm sur la zone testée.
- Recherche de voisins identique à une référence plus large ; présence 0/0,5/1, déterminisme et valeurs finies vérifiés.
- Sommets échantillonnés à 2804 et 2884 m sur les grilles de contrôle, sans plateau de clamp.
- Pour chaque seed, 1331 densités concordent avec le pipeline de production et 125 points partagés entre LOD sont identiques.
- Les 26 recettes, les transitions, les poids locaux et les tests CPU passent. Le contrat de stockage des bandes reste inchangé ; 18 cas de hauteur vérifient le nouveau plafond.
- Les onze formations froides isolées restantes passent leurs contrôles. Les anciens tests de sites `FrozenTundra` sont remplacés par le test de chaînes.

[Aperçu du relief réel GLSL](biome-validation/frozen_chains.png) · [Mesures](biome-validation/frozen_chains.json)

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_frozen_chains.py --output /tmp/frozen-chains
```

Les tests utilisent Mesa llvmpipe ; le rendu en jeu, les collisions et les performances sur le GPU Windows restent à apprécier. Recompiler le moteur puis régénérer le terrain, car les bornes de hauteur CPU ont changé.
