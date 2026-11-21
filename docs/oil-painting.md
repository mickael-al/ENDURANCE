# Filtre de peinture à l’huile

Dans **Post Process → Oil Painting**, activer **Active**, puis essayer :

- **Brush radius (pixels) : 2** ;
- **Strength : 0,65** pour conserver une partie des détails, ou **1** pour voir l’effet complet.

Le filtre reste désactivé par défaut. Les anciennes scènes conservent leur rayon enregistré, souvent 1. Le nouveau paramètre `oilp_strength` est sauvegardé avec les paramètres de post-traitement ; son défaut est 0,65. Aucun asset de scène n’est modifié par cette correction.

## Algorithme et intégration

Le principe d’origine est conservé : classer les voisins dans un histogramme de 24 luminances, puis moyenner les couleurs de la classe la plus représentée. Le voisinage est maintenant un disque. Les égalités favorisent la luminance la plus proche du pixel central plutôt que systématiquement la classe la plus sombre. L’intensité mélange ce résultat avec l’image initiale.

Les indices de l’histogramme sont bornés, y compris pour une entrée HDR ou négative. Le shader utilise `texelFetch`, borne ses coordonnées aux bords de l’image et conserve l’alpha. Les anciens appels `texture2D`, le code mort et les uniformes inutilisés ont été retirés.

Ordre : **bloom/tone mapping → profondeur de champ éventuelle → peinture → teinte éventuelle → interface**. Le calcul de luminance est donc fait sur les couleurs après tone mapping. L’effet n’altère ni les matériaux, ni l’éclairage, ni les buffers du terrain. Il ne constitue pas un antialiasing temporel : le choix discret d’une classe peut encore changer en mouvement, et un grand rayon efface des détails fins.

## Coût

| Rayon | Lectures de couleur par pixel |
| --- | ---: |
| 0 | Passe évitée |
| 1 | 5 |
| 2 | 13 |
| 3 | 29 |
| 4 | 49 |

Les cinq variantes fixes sont compilées à l’initialisation ; les variantes de peinture utilisent le cache binaire existant. Changer le rayon sélectionne un programme déjà prêt. Il n’y a pas de test de rayon par voisin dans le shader. L’histogramme et la sélection de sa classe dominante restent un travail par pixel, dont le coût dépend du GPU.

Une seule passe de peinture, à pleine résolution. Plus de copie complète de l’image ni de texture privée au filtre. La chaîne réutilise ses deux cibles RGBA16F alternées, déjà employées par la profondeur de champ et la teinte. Si aucun de ces effets n’a encore été utilisé, leur première activation alloue ces cibles : environ **31,6 Mio au total en 1920 × 1080**. Elles restent réutilisables jusqu’à leur destruction ou leur redimensionnement.

Effet désactivé, rayon nul ou intensité nulle : **aucun draw de peinture ni copie d’image**. Les programmes déjà initialisés et les cibles déjà allouées restent en mémoire. Le gain en FPS sur GTX 1660 n’a pas été mesuré.

## Code et validation

- Shader : `Asset/Shader/oilpainting.fs.glsl`.
- Module et interface : `ANTAGONIZ/Source/Rendering/Shader/OilPainting.cpp`.
- Ordonnancement : `ANTAGONIZ/Source/Rendering/Shader/PostProcess.cpp`.
- Paramètres : `ModulePP.hpp` et sérialisation `ProjectData.hpp`.

Vérifications du 21 novembre 2026, sous OpenGL/Mesa llvmpipe :

```sh
python3 Tests/Rendering/check_oil_painting.py
python3 Tests/Rendering/check_bloom.py
python3 Tests/Atmosphere/check_render_integration.py
```

57 comparaisons de l’image calculée par le vrai module avec une référence CPU : rayons 0–4, dépassement du rayon, intensités 0/0,65/1 et valeur non finie, entrées HDR/négatives, conservation de l’alpha, résolutions impaires et 1 × 1. La chaîne réelle est exercée dans 24 cas (huit combinaisons profondeur de champ/peinture/teinte à trois tailles), avec profondeur de champ à rayon nul pour vérifier précisément les pixels et le routage. Contrôles des passes évitées, absence de lecture de la cible en cours d’écriture, cache de programmes borné et libération des ressources.

Les 15 comparaisons de régression du bloom passent aussi. Les interfaces C++ sont compilées avec les véritables headers du moteur. Les services de fenêtre, d’éditeur et de caméra sont remplacés dans le banc OpenGL ; le brouillard y est désactivé. Le test ne remplace pas un essai artistique en jeu ni un build complet sous MSVC.
