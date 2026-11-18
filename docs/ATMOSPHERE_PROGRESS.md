# Ciel et atmosphère — état de reprise

## État actuel

Les cinq paliers fonctionnels, les sept points de la passe de correction et les deux derniers correctifs de continuite sont implementes. La branche d'intégration est
`feature/atmosphere-biome-sky`, issue de `feature/terrain-noise-library`
(commit GitHub de base `2ab58174c1a33f8b1b795b466075fd618424e933`, arbre
`bc47314734e8bfd1b3f915e53b6a08b4e9d99238`). Ne pas repartir de l'ancien `master`.

1. **Palettes et interfaces** : 25 biomes et Marine, JSON linéaire HDR, état de transition indépendant du framerate, API moteur sans identifiants de biomes.
2. **Ciel et IBL** : ensembles HDR complets, irradiance E/pi, préfiltrage GGX, BRDF conservée, fondu partagé avec reprise de l'état affiché et disques analytiques à la résolution d'écran.
3. **Atmosphère** : transmittance, diffusion multiple approximée, SkyView, coefficients volumétriques et perspective aérienne à la profondeur propre des opaques/transparents.
4. **Brouillard et rayons** : atlas climatique mondial, milieux lisses/bruités/absents, ombres de terrain, marge de casters et biais exprimé en mètres ; ancien fog suspendu.
5. **Nuages et optimisation** : Perlin–Worley périodique, ombres solaires/lunaires, vent, mipmaps du bruit, profils Low/Medium/High/**Ultra par défaut**, budgets et mesures GPU séparées.

## Où reprendre

- Ouvrir `ENDURANCE/Scene/Terrain.scene`, puis Play.
- Inspecter `Procedural terrain` → `EnduranceAtmosphereComponent`.
- Cette scène assigne le soleil d'index 0, laisse la lune absente et utilise une
  portée atmosphérique de **49 152 m** ; sa génération de terrain existante est conservée.
- Matrice : `ENDURANCE/Ressources/Atmosphere/biome_palettes.json`.
- API et utilisation : `docs/ATMOSPHERE.md`.
- Parcours de validation et preuves : `docs/ATMOSPHERE_VALIDATION.md`.

## Décisions à préserver

Le classificateur climatique est partagé avec le terrain, vérifié identique
sur 4 356 échantillons. Les résultats de palette passent par trois buffers et
ne sont lus qu'après une fence signalée, sans attente. Le brouillard est
échantillonné dans le monde ; son atlas réutilise la recette exacte aux nœuds
puis interpole les propriétés entre eux. Il couvre les régions voisines.

Les références de seuil sont les derniers états effectivement acceptés ou
calculés ; les captures animées des nuages ne réinitialisent pas le seuil de
SkyView. Les publications font progresser l'ancien fondu avant de capturer une
nouvelle cible. Le dépassement de la durée du fondu publie bien sa dernière image.

Pendant la premiere capture IBL, conserver la palette visible et memoriser
la derniere cible acceptee sans avancer son fondu. La premiere publication
relance ce fondu depuis l’etat affiche, y compris si sa duree est nulle ou si
la capture a ete annulee puis reconstruite.

Le vent nuageux est integre en double precision dans un deplacement commun
a la meteo, a la densite et aux ombres. L’offset est fige par capture, tandis
que la vue courante utilise sa valeur actuelle. Ne pas revenir a une formule
vitesse courante × temps total. Une recreation GPU du meme service conserve
l’horloge, l’offset et la derniere palette visible ; un nouvel objet repart
avec un deplacement nul.

Les extinctions et sources sont additionnées avant l'intégration. Les coefficients colores sont en RGBA32F, l’extinction nuageuse en R32F ; les sorties intégrées, les cubemaps et
l'IBL sont en HDR. L'éclairage direct et l'IBL ne comptent pas deux fois les disques.
La saturation de l'atmosphère visible est appliquée après la composition ; celle
de l'IBL est indépendante. Les ombres utilisent la profondeur des vraies cascades,
jamais une estimation à partir de la profondeur écran.

## Validation et prochaine action

Les douze groupes de tests CPU, shaders OpenGL, service C++ de production
et composant ENDURANCE passent sur EGL/Mesa llvmpipe. La passe de correction
a aussi verifie les trois unites C++ modifiees avec les vrais en-tetes. Commande consolidée :

```bash
python3 Tests/Atmosphere/run_all.py
```

La vérification syntaxique porte sur les sept unités C++ modifiées, avec les
vrais en-têtes. Le harnais runtime emploie les vrais shaders, cache, sérialisation
et service atmosphérique, avec des objets caméra/lumière/éditeur de test.

**À vérifier sur la machine de jeu** : compilation et lancement de l'éditeur
Windows, apparence des silhouettes en déplacement rapide, ombres de grandes
montagnes hors écran mais générées, reflets et transparents, sauvegarde/rechargement
dans l'éditeur, et coût Ultra sur la carte graphique cible. Les tests logiciels ne
remplacent pas cette validation visuelle et de performance. Le détail des budgets
et des limites se trouve dans `ATMOSPHERE_VALIDATION.md`.

## Passe de correction de la PR #4

Les sept points de l’audit sont suivis dans `ATMOSPHERE_REVISION.md` :
reference nuageuse sans ombres terrain, soleil rasant, vent du vrai brouillard
ENDURANCE, nuages visibles a la camera courante, IBL par transactions et
tuiles, ressources conditionnelles/R32F, couverture regionale et profils.
La suite consolidee des douze groupes passe. Les regressions du ciel compose,
du soleil rasant, du brouillard reel, des snapshots, des budgets et des
ressources conditionnelles sont couvertes. Conserver les snapshots GPU d’une capture et ne jamais
reutiliser son set de travail pendant un fondu public.

## Complements de continuite valides

La palette reste stable pendant la premiere capture, puis rejoint la derniere
cible. Les regressions verifient l’API, le bloc uniforme et les pixels HDR,
avec atmosphere physique, ciel gradient et duree nulle.

Le vent nuageux conserve les formations a 137 secondes lors d’un arret ou
d’un changement de vitesse. Les tests couvrent densite, vue, ombres, partage
avec la meteo, snapshot IBL et publication de la position finale apres arret.
Les **18 groupes internes** du runtime et les **12 groupes** de la suite
consolidee passent. Preuves : `ATMOSPHERE_VALIDATION.md` et
`ATMOSPHERE_REVISION.md`.
