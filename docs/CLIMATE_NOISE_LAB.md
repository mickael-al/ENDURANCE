# Climate Noise Lab

Ouvrir `ENDURANCE/Scene/ClimateNoiseLab.scene`, puis lancer **Play**. La scène ne
contient pas de géométrie : `ClimateNoiseLabComponent` enregistre un espace de
travail ImGui docké et un compute shader produit cinq textures :

1. température en niveaux de gris ;
2. humidité en niveaux de gris ;
3. climat après contraste/gamma, rouge = température et vert = humidité ;
4. le même climat après l'attraction sélectionnée ;
5. le résultat de la matrice 5×5 avec la palette provisoire du terrain.

L'image `Asset/Shader/ClimateNoiseLab/climate_reference.png` est affichée dans un
sixième panneau de référence.

## Neural Pattern GPU

L'image de référence décrit **un seul pattern scalaire**. Le canal utilisé pour
l'apprentissage se choisit dans l'interface :

- rouge ;
- vert ;
- bleu ;
- luminance.

Le réseau ne produit donc plus simultanément humidité et température. Il devient
un bruit ordinaire nommé `Neural Pattern GPU`, sélectionnable indépendamment dans
les menus **Humidité** et **Température**. On peut par exemple utiliser :

```text
Humidité    = Neural Pattern GPU
Température = Simplex
```

ou l'inverse. Si les deux axes utilisent Neural Pattern avec les mêmes réglages,
ils reçoivent le même pattern appris.

La structure du réseau est fixe :

```text
12 entrées -> 8 tanh -> 8 tanh -> 8 tanh -> 8 tanh -> 1 sortie
```

Cela représente exactement 32 activations `tanh` et 329 poids/biais. Les entrées
combinent deux bandes Fourier par axe et quatre Perlin évalués dans des
coordonnées non repliées. Le domaine de l'image occupe `Période Neural Pattern`
mètres ; les Fourier se répètent sur cette période et les Perlin font varier le
résultat au-delà.

## Entraînement génétique GPU

L'entraînement est entièrement intégré à l'éditeur, sans Python :

1. choisir le canal de référence ;
2. cliquer sur **Réinitialiser depuis zéro** ;
3. cliquer sur **Lancer / reprendre** ;
4. observer la génération et la meilleure erreur ;
5. mettre en pause ou sauvegarder le meilleur pattern.

À chaque génération, 128 individus sont comparés à une fenêtre 512×512 choisie
avec un offset pseudo-aléatoire dans l'image. Une grille réglable de points est
évaluée pour les tournois, croisements et mutations de la génération suivante.

Le meilleur individu de cette fenêtre est ensuite comparé au meilleur modèle
conservé sur **une grille fixe de 32×32 points répartis dans l'image entière**.
Les points sont décalés de façon déterministe dans chaque cellule. Ce score de
validation ne dépend ni de la fenêtre aléatoire, ni de la génération, ni du
réglage de la grille d'apprentissage. L'interface affiche ce score global
échantillonné, pas une erreur exhaustive sur tous les pixels.

Cela évite qu'un individu excellent sur une zone uniforme soit considéré à tort
comme le meilleur modèle de l'image entière. Deux élites sont conservées : le
meilleur modèle global et le gagnant de la fenêtre courante. La validation reste
sur le GPU et ajoute deux évaluations de réseaux, sans lecture CPU des poids à
chaque génération.

**Réinitialiser depuis zéro** efface réellement la population et le modèle,
même si un lot GPU était encore en cours. **Pause** laisse le lot déjà soumis se
terminer ; **Lancer / reprendre** conserve la population. Un rechargement réussi
de l'image arrête et réinitialise l'apprentissage. Un fichier image illisible
conserve la référence précédente et affiche une erreur.

Le shader d'entraînement est :

```text
Asset/Shader/ClimateNoiseLab/neural_genetic_train.C.glsl
```

La fonction réseau partagée entre entraînement et aperçu est :

```text
Asset/Shader/Noise/neural_pattern_2d.glsl
```

Le meilleur modèle peut être sauvegardé localement dans :

```text
Asset/Shader/ClimateNoiseLab/neural_pattern_model.bin
```

Ce fichier est ignoré par Git. Le modèle sauvegarde aussi la seed et le canal de
référence. Son chargement restaure ces réglages et les poids, mais son ancien
score est invalidé : il sera remesuré à la prochaine génération sur la référence
actuelle. Les fichiers tronqués, les poids non finis ou hors de la plage de
mutation [-3,3] et les scores invalides sont refusés avant de remplacer le modèle
en mémoire. Recharger les shaders invalide aussi le score affiché sans effacer
les poids. Il faut reprendre l'apprentissage pour mesurer et sauvegarder à
nouveau le modèle.

La sauvegarde contient le meilleur réseau, pas toute la population génétique :
la reprise reconstruit une population avec ce candidat comme élite. Elle ne
reproduit donc pas exactement la suite d'un entraînement interrompu.

L'image est chargée sans retournement vertical, et la référence comme les
aperçus utilisent la même orientation. Les images non carrées restent apprises
en coordonnées normalisées [0,1]² ; la visualisation actuelle est carrée.

## Transformations communes

Après le bruit choisi, les mêmes traitements fonctionnent pour toutes les
sources, y compris Neural Pattern :

- contraste séparé humidité/température ;
- gamma séparé humidité/température ;
- `attractNearFifths01` ;
- attraction vers les cinq niveaux réels de la matrice ;
- matrice discrète ou interpolation quintique.

L'ordre est :

```text
source -> contraste/gamma -> attraction -> matrice
```

Les presets **Neural Pattern -> humidité** et **Neural Pattern -> température**
placent directement le pattern entraîné sur l'axe souhaité et conservent Simplex
sur l'autre axe.

## Autres sources

Les bruits classiques restent disponibles : Perlin, Simplex, Value, bruit blanc,
Voronoï, valeur de cellule, variantes fBm et Patchwork de régions.

## Vérifications

Depuis la racine du dépôt, sous Linux avec OpenGL 4.5/EGL disponible :

```sh
python3 Tests/ClimateNoiseLab/check_gpu.py
python3 Tests/ClimateNoiseLab/check_panel.py
python3 Tests/ClimateNoiseLab/check_panel.py --fast-math
```

Le test du panneau nécessite les compilateurs C/C++17 et les en-têtes de
développement X11 pour construire le GLEW fourni. Un objet GLEW déjà compilé avec
`-fPIC` peut être réutilisé avec `--glew-object /chemin/glew.o`.

Les tests GPU vérifient notamment les scores face à un calcul CPU indépendant,
le cas d'une fenêtre facile qui ne doit pas verrouiller le modèle, la
reproductibilité et l'accord entre les coordonnées d'apprentissage et d'aperçu.
Les tests du panneau exercent les buffers réels et les transitions de chargement
et de réinitialisation sans lancer la scène complète.
La variante `--fast-math` vérifie aussi le rejet des modèles contenant des NaN
avec les optimisations flottantes utilisées par la compilation Release.
