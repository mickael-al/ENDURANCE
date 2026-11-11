# Climate Noise Lab

Ouvrir `ENDURANCE/Scene/ClimateNoiseLab.scene`, puis lancer **Play**. La scène ne
contient pas de géométrie : `ClimateNoiseLabComponent` enregistre un espace de
travail ImGui docké et un compute shader produit cinq textures :

1. température en niveaux de gris ;
2. humidité en niveaux de gris ;
3. climat brut, rouge = température et vert = humidité ;
4. le même climat après les transformations et l'attraction sélectionnée ;
5. le résultat de la matrice 5×5 avec la palette provisoire du terrain.

## Sources climatiques

`Deux bruits indépendants` conserve les sources Perlin, Simplex, Value, bruit
blanc, Voronoi, valeur de cellule, fBm et Patchwork.

`Neural Climate` évalue un réseau exporté qui produit simultanément :

```text
x = humidité
y = température
```

Le modèle est stocké dans :

```text
Asset/Shader/Noise/neural_climate_weights.glsl
```

Le fichier livré par défaut contient des poids neutres et renvoie 0,5/0,5. Le
trainer le remplace après apprentissage. Cliquer ensuite sur
**Recharger le shader / poids** sans fermer la scène.

Une image de référence occupe `Période image neuronale` mètres. Avec une origine
monde nulle et une étendue égale à cette période, la texture montre exactement
le domaine d'entraînement `[0,1]²`. Les entrées Fourier sont périodiques, tandis
que quatre features Perlin utilisent les coordonnées non repliées pour faire
varier le résultat hors de cette première image.

Guide d'entraînement :

```text
Tools/ClimateNeural/README.md
```

## Transformations communes

Les transformations suivantes fonctionnent sur les bruits classiques comme sur
le modèle neuronal :

- contraste séparé humidité/température ;
- gamma séparé humidité/température ;
- `attractNearFifths01` ;
- attraction vers les cinq niveaux réels de la matrice ;
- matrice discrète ou interpolation quintique.

L'ordre est :

```text
source -> contraste/gamma -> attraction -> matrice
```

## Patchwork de régions

Ce candidat construit une partition cellulaire déformée, des valeurs constantes
au centre des zones, un gradient près des frontières et un niveau secondaire de
petites régions. Il reste disponible pour comparer son résultat à l'approche
neuronale dans le même outil.

## Validation GPU

Le test headless compile le compute shader réel, exécute ses cinq images et
vérifie les sources scalaires, les canaux RG, les transformations, la matrice et
la source Neural Climate, y compris avec le modèle neutre :

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_climate_noise_lab.py
```
