# Climate Noise Lab

Ouvrir `ENDURANCE/Scene/ClimateNoiseLab.scene`, puis lancer **Play**. La scene ne
contient pas de geometrie : `ClimateNoiseLabComponent` enregistre un espace de
travail ImGui docke et un compute shader produit cinq textures :

1. temperature en niveaux de gris ;
2. humidite en niveaux de gris ;
3. climat brut, rouge = temperature et vert = humidite ;
4. le meme climat apres l'attraction selectionnee ;
5. le resultat de la matrice 5x5 avec la palette provisoire du terrain.

Les deux axes peuvent utiliser independamment Perlin, Simplex, Value, bruit
blanc, Voronoi, valeur de cellule, trois variantes fBm ou `Patchwork de regions`.
`Climate frequency` reste la frequence mondiale du climat ; `Etendue monde`
choisit la surface montree par la texture. Les multiplicateurs H/T permettent
une comparaison sans changer cette frequence commune.

## Patchwork de regions

Ce candidat reproduit directement l'effet utile d'un filtre de peinture a
l'huile, sans filtrer un voisinage de pixels. Il construit :

- une partition cellulaire deformee pour les grandes zones ;
- une valeur constante evaluee au centre de chaque zone ;
- un gradient seulement pres des frontieres ;
- un second niveau de petites regions, present sur environ 30 % des cellules.

Une tendance Simplex a basse frequence correle les tons des zones voisines. Les
frontieres sont partagees entre humidite et temperature quand leurs
multiplicateurs de frequence sont identiques, mais chaque region recoit deux
valeurs independantes. Cela evite que deux mosaïques distinctes se croisent et
recreent artificiellement de nombreuses petites zones.

Le preset `Patchwork regions` selectionne ce bruit sur les deux axes, conserve
`climateFrequency = 0.0005`, utilise le remappage complet `[-1,1] -> [0,1]` et
desactive l'attraction pour montrer le champ brut. Les constantes artistiques de
cette premiere version sont dans :

`Asset/Shader/Noise/noise_regions_2d.glsl`

## Attraction

Le mode `attractNearFifths01` est la fonction experimentale exacte : ses six
reperes sont 0, .2, .4, .6, .8 et 1, et chaque axe possede une cible fixe. Le
mode `Niveau matrice le plus proche` sert uniquement de comparaison avec les
cinq noeuds reels de la matrice : 0, .25, .5, .75 et 1.

Le shader peut etre modifie puis recharge depuis la barre de menu ou le panneau
de reglages. Une recompilation echouee conserve le dernier programme valide et
affiche le diagnostic. Les cinq textures sont generees en une seule passe et ne
sont mises a jour que lorsqu'un parametre change, sauf si la mise a jour
automatique est desactivee.

## Validation GPU

Le test headless compile exactement le compute shader de la scene, execute ses
cinq images avec la matrice C++ de production et verifie le codage des canaux,
le determinisme, l'activation de l'attraction, la vue finale et la presence de
zones constantes dans le bruit Patchwork :

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_climate_noise_lab.py
```
