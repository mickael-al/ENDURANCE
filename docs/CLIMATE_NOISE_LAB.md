# Climate Noise Lab

Ouvrir `ENDURANCE/Scene/ClimateNoiseLab.scene`, puis lancer **Play**. La scène ne
contient pas de géométrie : `ClimateNoiseLabComponent` enregistre un espace de
travail ImGui docké et un compute shader produit cinq textures :

1. température en niveaux de gris ;
2. humidité en niveaux de gris ;
3. climat brut, rouge = température et vert = humidité ;
4. le même climat après les transformations et l'attraction sélectionnée ;
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
évaluée, puis tournoi, croisement, mutations, élitisme et immigrants construisent
la génération suivante.

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
référence afin d'éviter de reprendre un apprentissage incompatible.

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
