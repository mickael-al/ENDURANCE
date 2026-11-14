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

## Climat validé pour le terrain — 12 novembre 2026

La recette retenue est **Somme x5 - fBm Simplex** sur les deux canaux. Les valeurs
ci-dessous sont celles de la capture validée, et servent maintenant de valeurs
par défaut à la scène du laboratoire et au bouton **Preset climat terrain valide**.

| Réglage | Humidité | Température |
| --- | --- | --- |
| Fréquence climatique | 0.0005 | 0.0005 |
| Multiplicateur spatial | 0.1 | 0.1 |
| Fréquence effective | 0.00005 | 0.00005 |
| Nombre de bruits / seeds | 5 | 5 |
| Octaves par fBm | 8 | 8 |
| Lacunarité | 2 | 2 |
| Gain | 0.5 | 0.5 |
| Seuil de transition | 0.08 | 0.08 |
| Contraste | 1.5 | 1.5 |
| Gamma | 1.5 | 2 |
| Attraction | Niveau de matrice le plus proche, continu | Idem |
| Déplacement maximal | 0.0468 | 0.0468 |
| Interpolation de matrice | Quintique lissée | Idem |

Le Smoothstep global affiché à `[-1, +1]` n'intervient pas dans cette source.
Chaque fBm complet contribue `smoothstep(-0.08, 0.08, valeur) / 5`, puis on
applique contraste, gamma et attraction. La seed reste celle du monde, avec des
salts distincts pour l'humidité et la température. Aucun bruit aléatoire temporel
ni aucune texture d'aperçu n'intervient dans le terrain.

La version de production est dans **`Asset/Shader/Noise/climate_2d.glsl`** :

```glsl
vec2 noiseClimate2D(vec2 world, uint seed); // x=humidité, y=température
```

Les réglages sont des `#define ENDURANCE_CLIMATE_*` regroupés en tête de fichier,
compilés dans le shader. Le terrain n'a plus de paramètre `climateFrequency` à
transmettre : les anciennes clés des scènes sont ignorées au chargement.
Modifier les defines puis utiliser **Regenerer avec ces reglages** recharge le
code ; le cache binaire tient compte du contenu des includes.

Les deux canaux et les cinq couches sont évalués ensemble. À chaque octave,
la cellule Simplex, les distances et les atténuations sont calculées une fois
pour les dix champs. Les hash/gradients gardent leurs seeds distinctes. Les
puissances fixes utilisent `x*sqrt(x)` pour le gamma 1.5 et `x*x` pour le gamma 2.
Le climat complet est calculé une seule fois par colonne XZ, puis réutilisé par
la génération volumique. Il n'ajoute aucun transfert CPU, buffer ou dispatch.
Le relief, la hauteur de base et la sélection marine par hauteur restent des
étapes distinctes du climat.

## Référence ajustable du laboratoire

Le laboratoire conserve la référence ajustable dans
`ClimateNoiseLab/climate_experiments.glsl` pour comparer les changements futurs.
**L'expérience x5 Perlin est supprimée** (sélecteur, bouton et branche GLSL).
Le Simplex x5 garde son ID sauvegardé `13` ; l'ancien ID `12` est migré vers `13`.
Les autres bruits simples et l'outil Neural restent disponibles pour les tests.

Les seuils H/T peuvent toujours être modifiés indépendamment dans le laboratoire.
`0` reproduit le mode binaire strict : chaque fBm positif ajoute `0.2`, et on
obtient les six niveaux `0, 0.2, 0.4, 0.6, 0.8, 1`. Un seuil positif `s` produit
une transition de `-s` à `+s` avant la somme. Un bruit nul contribue alors `0.1` ;
cinq bruits nuls donnent `0.5` avant les transformations de sortie.

Cette largeur s'exprime en valeurs de bruit, pas en pixels ou en mètres. Le champ
reste indépendant de la résolution d'aperçu ; sa largeur physique varie avec la
pente du bruit. Le bouton **Test somme binaire x5 fBm Simplex** remet les seuils
à zéro, contraste/gamma à 1 et l'attraction à zéro pour isoler cette expérience.
Le preset validé rétablit toutes les valeurs du tableau sans modifier la seed
ni le cadrage. Les changements du laboratoire n'affectent pas automatiquement
les constantes du terrain.

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
Ils comparent aussi la somme binaire Simplex avec un calcul CPU indépendant,
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

La recette fixe et le climat réellement lu par les colonnes du terrain sont
comparés en float32 à la référence ajustable du laboratoire, avec les réglages
validés, plusieurs seeds et des positions positives/négatives.
