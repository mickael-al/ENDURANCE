# Climate Noise Lab

Ouvrir `ENDURANCE/Scene/ClimateNoiseLab.scene`, puis lancer **Play**. La scène ne
contient pas de géométrie. Le panneau ImGui affiche :

1. l'image de référence RG ;
2. la température prédite ;
3. l'humidité prédite ;
4. le climat brut, rouge = température et vert = humidité ;
5. le climat après transformation/attraction ;
6. le résultat de la matrice 5×5.

L'image est chargée directement depuis :

```text
Asset/Shader/ClimateNoiseLab/climate_reference.png
```

Convention :

```text
R = température
G = humidité
B = ignoré
```

## Sources climatiques

`Deux bruits indépendants` conserve Perlin, Simplex, Value, bruit blanc,
Voronoï, valeur de cellule, fBm et Patchwork.

`Neural Climate GPU` utilise un réseau fixe :

```text
12 entrées -> 8 -> 8 -> 8 -> 8 -> 2 sorties
```

Les quatre couches de largeur 8 exécutent exactement 32 `tanh`. Les 338 poids et
biais sont stockés dans un SSBO. La sortie est :

```text
x = humidité
y = température
```

Les 12 entrées combinent deux bandes Fourier par axe et quatre Perlin en
coordonnées non repliées. La partie Fourier apprend la carte de référence ; les
Perlin donnent une variation déterministe quand on dépasse une période.

## Entraînement génétique GPU

Aucun script Python n'est utilisé. Le bouton **Lancer / reprendre** déclenche un
compute shader qui entraîne 128 individus en parallèle.

À chaque génération :

1. une fenêtre 512×512 est choisie à un offset déterministe pseudo-aléatoire ;
2. une grille réglable de points est échantillonnée dans cette fenêtre ;
3. chaque réseau est comparé aux canaux RG de l'image ;
4. un tournoi choisit les parents ;
5. croisement, mutation, élitisme et quelques immigrants produisent la génération suivante.

Le coût se règle avec :

- `Grille d'échantillons` : 8 à 64 par axe ;
- `Générations par lot` : nombre de générations soumises avant de rafraîchir l'aperçu ;
- `Mutation initiale` et `Mutation minimale`.

Le panneau n'envoie un nouveau lot que lorsque la fence du précédent est
terminée. Il n'accumule donc pas une file GPU sans limite.

## Sauvegarde du modèle

Le meilleur individu peut être sauvegardé depuis le panneau dans :

```text
Asset/Shader/ClimateNoiseLab/neural_climate_model.bin
```

Ce fichier local est ignoré par Git. Il contient un en-tête court puis 338
`float`. Au prochain lancement, le Lab le recharge automatiquement s'il existe.
Le résultat courant est lu directement par le shader d'aperçu : aucune
recompilation et aucun export GLSL ne sont nécessaires.

## Domaine et transformations

Avec :

```text
Origine X/Z = 0
Étendue monde = Période image neuronale
```

l'aperçu couvre exactement une période de l'image apprise. Déplacer l'origine
permet de regarder l'extrapolation pilotée par les entrées Perlin.

Les transformations restent disponibles après le réseau :

- contraste séparé humidité/température ;
- gamma séparé humidité/température ;
- `attractNearFifths01` ;
- attraction vers les cinq niveaux de la matrice ;
- matrice discrète ou interpolation quintique.

Ordre :

```text
réseau -> contraste/gamma -> attraction -> matrice
```

## Fichiers principaux

```text
Asset/Shader/ClimateNoiseLab/neural_genetic_train.C.glsl
Asset/Shader/Noise/neural_climate_2d.glsl
ANTAGONIZ/Source/ProjectUser/ClimateNoiseLabPanel.cpp
```
