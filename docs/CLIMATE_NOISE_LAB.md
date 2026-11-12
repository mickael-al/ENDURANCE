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

## Répartition des biomes dans l'image

La fenêtre **Résultat matrice** affiche l'image et un tableau des 25 biomes :

- **Surface %** : somme des poids du biome, divisée par le nombre de pixels.
  Un pixel mélangé à 70 % / 30 % contribue dans ces proportions aux deux biomes.
  Le total est voisin de 100 % (arrondi d'affichage et accumulation Q24).
- **Dominant %** : proportion de pixels où ce biome possède le poids le plus
  élevé. Une égalité favorise le plus petit ID. Le total vaut 100 % avant arrondi.

Les biomes absents sont affichés en rouge, ceux présents uniquement dans les
mélanges en ambre. Le résumé indique combien de biomes sont présents et absents.
Un pourcentage positif inférieur à 0.001 % est affiché **<0.001**, et non zéro.
Le survol du nom indique le nombre de pixels concernés, même si leur poids est
trop petit pour contribuer à l'accumulation Q24. Avec une matrice discrète,
les colonnes Surface et Dominant coïncident.

Ces statistiques portent sur **les pixels de la zone actuellement affichée**,
après contraste/gamma et attraction. Une absence sur cet aperçu ne signifie
pas que le biome est impossible dans tout le monde ; déplacer la zone, changer
son étendue ou sa résolution permet d'étendre la vérification.

L'image et le tableau sont côte à côte dans une fenêtre large, puis empilés
dans une fenêtre étroite. Le tableau possède son propre défilement et un en-tête
fixe. Les paramètres climatiques ne sont pas modifiés par cette mesure.

Le compute existant réutilise directement les IDs et poids de la coloration :
aucun nouveau bruit, aucune lecture des couleurs RGBA8 et aucune passe de calcul
supplémentaire. Des compteurs partagés par groupe de 64 pixels réduisent les
atomiques globales. Un SSBO de 416 octets contient les surfaces en Q24 sur deux
mots uint32 (pour éviter les débordements), les comptes dominants et la présence.
Il inclut l'ID Marine réservé, mais le tableau liste les biomes de la matrice 2D.
Le code de mesure est isolé dans `ClimateNoiseLab/matrix_statistics.glsl`.

Les statistiques sont recalculées à chaque génération de l'aperçu. Une fence
est interrogée sans attente ; les 416 octets ne sont lus qu'une fois le calcul
terminé. Si une nouvelle image remplace une génération encore en cours, ses
anciens pourcentages sont écartés. Les frames sans nouvelle génération ne
recomptent pas les pixels.

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
- attraction continue vers les cinq niveaux réels de la matrice ;
- matrice discrète ou interpolation quintique.

L'ordre est :

```text
source -> contraste/gamma -> attraction -> matrice
```

Le mode **Niveau matrice le plus proche** conserve une attraction bornée par le
déplacement maximal. Le changement de cible est désormais progressif autour
des milieux entre niveaux : il ne réintroduit plus les sauts de `round()` dans
un climat continu. Hors de cette bande, la cible reste le niveau le plus proche.
Un déplacement maximal nul laisse les valeurs inchangées.

Les presets **Neural Pattern -> humidité** et **Neural Pattern -> température**
placent directement le pattern entraîné sur l'axe souhaité et conservent Simplex
sur l'autre axe.

## Autres sources

Les bruits classiques restent disponibles : Perlin, Simplex, Value, bruit blanc,
Voronoï, valeur de cellule, variantes fBm et Patchwork de régions.

## Sommes x5 expérimentales

Deux sources supplémentaires sont disponibles indépendamment pour l'humidité
et la température :

- **Somme x5 - Perlin (exp.)** ;
- **Somme x5 - fBm Simplex (exp.)**.

Le code de ces expériences et de l'attraction du laboratoire est isolé dans
`Asset/Shader/ClimateNoiseLab/climate_experiments.glsl`. Il réutilise les bruits
de `Noise/` sans ajouter de recette expérimentale à cette bibliothèque ni
modifier les shaders du terrain.

Chaque canal évalue cinq bruits de même fréquence avec cinq seeds distinctes,
dérivées de la seed de cet axe. Avec **Seuil transition H/T = 0**, chaque bruit
strictement supérieur à zéro ajoute `1/5` au résultat ; un bruit nul ou négatif
n'ajoute rien :

```glsl
resultat = (positifs_sur_les_cinq_bruits) / 5.0;
```

Cela produit **six niveaux**, `0, 0.2, 0.4, 0.6, 0.8, 1`, et des frontières
franches. La distribution des niveaux n'est pas forcée à être uniforme. Pour la
version fBm, le seuil s'applique au résultat de chaque fBm complet, après le
mélange de ses octaves. Les réglages Octaves, Lacunarité et Gain restent actifs.
Avec zéro octave et un seuil de transition nul, la somme fBm vaut zéro.

Les réglages **Seuil transition H** et **Seuil transition T** sont indépendants,
sauvegardés avec le composant de scène, et valent `0.1` par défaut. Avec un seuil
positif `s`, chaque bruit contribue `0.2 * smoothstep(-s, s, bruit)`. Ainsi `0.1`
crée une transition entre `-0.1` et `+0.1`, avant l'addition des cinq bruits.
Hors de ces bandes, les contributions et les plateaux restent identiques au
mode binaire. Un bruit exactement nul contribue alors `0.1` : cinq bruits nuls
(notamment un fBm à zéro octave) donnent `0.5` avant contraste/gamma.

Cette largeur est exprimée en valeurs de bruit, pas en pixels ni en mètres.
Le champ ne dépend donc pas de la résolution d'aperçu ; la largeur en mètres
varie avec la pente locale du bruit et la fréquence choisie. Aucun voisinage
n'est échantillonné : toujours cinq bruits par canal, sans passe ni texture
supplémentaire. Le changement de seuil est un uniform et ne recompile pas les
shaders. Les autres sources de bruit ignorent ce réglage.

`Climate frequency` et les multiplicateurs H/T contrôlent la taille des motifs.
Le remappage Smoothstep est ignoré car la somme est déjà dans [0,1]. Les
transformations contraste/gamma et l'attraction restent disponibles ensuite.
Les boutons **Test somme binaire x5 Perlin** et **Test somme binaire x5 fBm Simplex**
placent la même méthode sur les deux axes, remettent les seuils de transition à
zéro et contraste/gamma à 1,
mettent l'apprentissage neuronal en pause, désactivent l'attraction et génèrent
l'aperçu. Les seeds des deux axes restent
distinctes. La matrice de biomes conserve ses cinq niveaux et ses réglages ;
elle n'est pas redimensionnée pour cette expérience.

**Preset climat x5 adouci** reprend les réglages du test retenu : fBm Simplex
x5 sur les deux axes, cinq octaves, lacunarité 2, gain 0.5, fréquence climatique
0.0005 et multiplicateurs H/T à 0.1, contraste 2, gamma H à 1.5 et T à 2,
attraction au niveau de matrice le plus proche avec déplacement maximal 0.032,
matrice lissée. Les deux seuils de transition sont initialisés à 0.1 et restent
réglables. La seed et le cadrage ne sont pas modifiés.

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
Ils contrôlent aussi les transitions progressives, l'indépendance des réglages
H/T, la conservation des plateaux, le retour exact au mode binaire et la
continuité de l'attraction autour des anciennes coupures.
La répartition est comparée à un oracle CPU sur les mélanges, les 25 biomes,
les IDs répétés, les contributions minuscules et les groupes de bord incomplets.
Les tests couvrent aussi les retenues des compteurs Q24, leur remise à zéro et
le remplacement d'une génération avant sa lecture asynchrone.
Les tests du panneau exercent les buffers réels et les transitions de chargement
et de réinitialisation sans lancer la scène complète.
La variante `--fast-math` vérifie aussi le rejet des modèles contenant des NaN
avec les optimisations flottantes utilisées par la compilation Release.
