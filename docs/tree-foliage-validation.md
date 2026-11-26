# Feuillage mince et réglages des arbres — 21 novembre 2026

Base avant modification : `1f7785d66e061fb2a5b9100d499274d38065987f`.
Environnement de validation : OpenGL/EGL Mesa llvmpipe 25.0.7,
LLVM 19.1.7, Linux. Aucun résultat de FPS GTX 1660 n'est déduit de ces tests.

## Changements

- Amplitude du vent 0,015 → 0,03 ; vitesse, bruit et portée de l'animation conservés.
- Portée de rendu et de publication 10 240 → 15 360 m. Comparaison JSON : aucun
  autre champ du catalogue modifié (densités, masques et prototypes identiques).
- Suppression de l'arrondi global des normales de couronne ; normales de touffes
  exportées conservées, normales de flexion du vent toujours appliquées.
- Matériau mince dans le shader différé existant : diffuse élargie, transmission
  teintée, irradiance des deux côtés. Bois toujours opaque. Réglage initial 0,65.
- Métadonnée signée dans l'alpha RGBA16F déjà alloué ; aucun nouveau buffer,
  aucune passe supplémentaire. Les cibles des bancs utilisent maintenant aussi
  RGBA16F, au lieu de RGBA8 qui aurait écrasé cette métadonnée.

## Vérifications

| Commande | Résultat |
|---|---|
| `python Tests/Trees/check_runtime.py --captures /tmp/endurance-foliage-captures` | Réussi : vrai renderer et bake, LOD0/1/3, tag signé, normales/albédo/profondeur inchangés, masque couronne/tronc, vent et ombres synchronisés, caches et état GL |
| `python Tests/Trees/check_runtime.py --fast-math --captures /tmp/endurance-foliage-captures` | Réussi, incluant les contrôles ajoutés sur les bakes conifère et arbre mort, géométrie et imposteurs |
| `python Tests/Trees/check_lighting.py --baseline /tmp/endurance-foliage-before.fs.glsl --captures /tmp/endurance-foliage-captures` | Réussi avec le shader différé réel et l'ancien shader extrait de la base ci-dessus |
| `python Tests/Atmosphere/check_render_integration.py` | Interfaces moteur réelles : compilation syntaxique réussie ; avertissements préexistants de macro GLFW |
| `python Tests/Atmosphere/check_spot_shadows.py` | Ombres de spots rasterisées et intégration différée réussies |
| `python Tests/Atmosphere/check_screen_composition.py` | Composition du ciel/atmosphère et ombres directionnelles réussies |
| `python Tests/SSAO/check_gpu.py` | SSAO et intégration différée réussis |
| `git -c core.whitespace=cr-at-eol diff --check` | Réussi ; CRLF du shader existant conservé |

Le test différé compare les matériaux opaques avec l'ancienne version et avec
`GE_FOLIAGE_LIGHTING=0` : écart maximal observé **0** pour trois types de lumières,
trois alphas non négatifs et deux états de composition atmosphérique. Le
contre-jour atteint les feuilles et est complètement bloqué par une carte
d'ombre occultante. L'absence de lumière produit zéro. L'IBL constant conserve
son énergie ; une irradiance différente derrière les feuilles est bien reçue.
Les incidences rasantes, le contre-jour exactement opposé et une transmission
approchant zéro restent finis et continus, y compris avec une normale arrière.

Les images optionnelles sont rendues à partir des G-buffers réels par le shader
différé de production, avec lumière et exposition fixes, sans auto-ombres de
scène. `opaque_before` est un témoin opaque avec les mêmes normales de touffe,
pas une capture de l'ancien arrondi global. Ces images ne valident pas à elles
seules le résultat dans la scène interactive de l'utilisateur.

## Coût et limites

Une lecture d'irradiance supplémentaire par pixel de feuillage et de l'ALU par
lumière ; aucun coût de génération des formes de terrain ajouté. La plus grande
portée peut publier/dessiner plus d'instances, même si la densité est conservée.
Les imposteurs utilisent la hauteur de couronne faute de masque de matériau
exporté : le bois inclus dans la couronne peut recevoir la réponse des feuilles.
Le modèle est artistique et ne simule pas l'épaisseur ni la diffusion multiple.

Le build MSVC complet et l'aspect/performance en jeu sur la GTX 1660 restent à
valider. Les réglages déjà sérialisés dans une scène restent prioritaires :
vérifier **15 360 m / vent 0,03 / transmission 0,65**, puis régénérer.
Voir [le fonctionnement détaillé et les sources](tree-rendering.md).
