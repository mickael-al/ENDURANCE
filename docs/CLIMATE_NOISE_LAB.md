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

ou l'inverse. En génération normale, les deux axes réutilisent les mêmes poids
mais reçoivent des seeds différentes, dérivées de la seed du monde. Un décalage
de phase déterministe varie aussi les entrées Fourier : les deux cartes peuvent
ainsi différer même si le réseau a appris à ignorer les entrées Perlin.

**Voir le pattern appris sans variation** permet de comparer directement le
réseau à l'image de référence, avec les entrées utilisées pendant l'apprentissage.
Cette option est désactivée par défaut ; lorsqu'elle est activée, deux axes
Neural avec les mêmes réglages affichent volontairement le même pattern.

La structure du réseau est fixe :

```text
12 entrées -> 8 tanh -> 8 tanh -> 8 tanh -> 8 tanh -> 1 sortie
```

Cela représente exactement 32 activations `tanh` et 329 poids/biais. Les entrées
combinent deux bandes Fourier par axe et quatre Perlin évalués dans des
coordonnées non repliées. `Période Neural Pattern` donne l'échelle en mètres à
`Climate frequency = 0.0005`, avec les multiplicateurs d'axe à 1. La fréquence
globale pilote aussi Neural : la doubler divise la taille des motifs par deux.
À 0, la sortie devient constante dans l'espace. Les Fourier sont périodiques,
tandis que les Perlin font varier le résultat au-delà du premier domaine.

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
les poids. La sauvegarde reste disponible immédiatement pour un modèle chargé
ou entraîné, même lorsque son score doit être recalculé. Un pattern qui vient
d'être réinitialisé à zéro sans entraînement ne remplace pas une sauvegarde.

Le format v3 distingue un score mesuré d'un score inconnu. Les fichiers v2
restent lisibles, sans modifier leurs poids. Une écriture réussie affiche le
chemin absolu du fichier dans le panneau. Le remplacement passe par un fichier
temporaire entièrement écrit et fermé, afin de conserver la sauvegarde
précédente en cas d'échec. Le chargement demande un rafraîchissement de l'aperçu
même si la mise à jour automatique est désactivée.

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

## Sommes binaires expérimentales

Deux sources supplémentaires sont disponibles indépendamment pour l'humidité
et la température :

- **Somme binaire x5 - Perlin (exp.)** ;
- **Somme binaire x5 - fBm Simplex (exp.)**.

Chaque canal évalue cinq bruits de même fréquence avec cinq seeds distinctes,
dérivées de la seed de cet axe. Chaque bruit strictement supérieur à zéro ajoute
`1/5` au résultat ; un bruit nul ou négatif n'ajoute rien :

```glsl
resultat = (positifs_sur_les_cinq_bruits) / 5.0;
```

Cela produit **six niveaux**, `0, 0.2, 0.4, 0.6, 0.8, 1`, et des frontières
franches. La distribution des niveaux n'est pas forcée à être uniforme. Pour la
version fBm, le seuil s'applique au résultat de chaque fBm complet, après le
mélange de ses octaves. Les réglages Octaves, Lacunarité et Gain restent actifs.
Avec zéro octave, la somme fBm vaut zéro.

`Climate frequency` et les multiplicateurs H/T contrôlent la taille des motifs.
Le remappage Smoothstep est ignoré car la somme est déjà dans [0,1]. Les
transformations contraste/gamma et l'attraction restent disponibles ensuite.
Les boutons **Test somme binaire x5 Perlin** et **Test somme binaire x5 fBm Simplex**
placent la même méthode sur les deux axes, remettent contraste/gamma à 1,
mettent l'apprentissage neuronal en pause, désactivent l'attraction et génèrent
l'aperçu. Les seeds des deux axes restent
distinctes. La matrice de biomes conserve ses cinq niveaux et ses réglages ;
elle n'est pas redimensionnée pour cette expérience.

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
Ils comparent aussi les deux sommes binaires avec un calcul CPU indépendant,
vérifient les six niveaux, le seuil strict à zéro et l'effet des seeds/fréquences.
Les tests du panneau exercent les buffers réels et les transitions de chargement
et de réinitialisation sans lancer la scène complète.
La variante `--fast-math` vérifie aussi le rejet des modèles contenant des NaN
avec les optimisations flottantes utilisées par la compilation Release.
