# Ciel et atmosphère — état de reprise

## État actuel

Correction intégrée et validée après le retour visuel sur `3b57f79`.
Le quadrillage a été reproduit avec les vrais shaders du terrain, le VBO
compacté, le G-buffer puis la composition du moteur. Le filtre qui extrapole
un plan récepteur produit environ **53 % de faux pixels d’ombre** sur les
bosses triangulées, avec des marges de 0, 4 et 16 km. Les anciens succès sur
plans analytiques ne prouvaient donc pas sa justesse sur le terrain réel.

La nouvelle version remplace cette extrapolation par quatre comparaisons PCF
avec interpolation de visibilité, un biais exprimé en texels par lumière et
une normale géométrique capturée sur la primitive. Cette normale est encodée
dans `gPosition.w`, déjà en RGBA32F ; aucun attachement G-buffer n’est ajouté.
La marge de casters a une base de **4 km**, extensible par cascade d’après les
bounds des maillages publiés. Les valeurs utilisateur chargées restent respectées.

**Validation consolidée : les 19 groupes passent le 18 novembre 2026**, avec
sortie 0 de `run_all.py`. Le terrain réel couvre 18 combinaisons de relief,
soleil et marge : **438 732 évaluations de récepteurs sans faux auto-ombrage**,
avec conservation des occultants résolus. Le harnais du service conserve ses
18 groupes internes. La correction est organisée en deux commits : surfaces
puis couverture adaptative. Aucune validation visuelle de l’éditeur Windows
ni mesure sur GPU matériel n’est revendiquée.

Les cinq paliers fonctionnels et les correctifs de continuité antérieurs sont
implémentés. La branche d’intégration est `feature/atmosphere-biome-sky`, issue
de `feature/terrain-noise-library` (base GitHub
`2ab58174c1a33f8b1b795b466075fd618424e933`, arbre
`bc47314734e8bfd1b3f915e53b6a08b4e9d99238`). Ne pas repartir de l’ancien `master`.

1. **Palettes et interfaces** : 25 biomes et Marine, JSON linéaire HDR,
   transition indépendante du framerate, API moteur sans identifiants de biomes.
2. **Ciel et IBL** : ensembles HDR complets, irradiance E/pi, préfiltrage GGX,
   BRDF conservée, reprise de l’état affiché et disques analytiques à l’écran.
3. **Atmosphère** : transmittance, diffusion multiple approximée, SkyView et
   perspective aérienne à la profondeur propre des opaques et transparents.
4. **Brouillard et rayons** : atlas climatique mondial, milieux lisses ou
   bruités, ombres de terrain, couverture des casters ; ancien fog suspendu.
5. **Nuages et optimisation** : Perlin–Worley, ombres soleil/lune, déplacement
   intégré, niveaux Low/Medium/High/**Ultra par défaut**, budgets et mesures.

## Révision à tester

La PR d’intégration est [ENDURANCE #4](https://github.com/mickael-al/ENDURANCE/pull/4),
sur `feature/atmosphere-biome-sky`. Le commit de correction des surfaces est
[`2f78d5e`](https://github.com/mickael-al/ENDURANCE/commit/2f78d5e531a8d65f53f90acadd38d5abe2d86d0d).
Le commit qui suit ajoute la couverture adaptative et ce compte rendu consolidé.
Le miroir local du premier commit est `3544790817a4de5a8646f8bc42238857663e0f61` ;
les deux arbres sont identiques (`53a14ac6a256fd161600375539a06903a58787ce`).

## Où reprendre

- Ouvrir `ENDURANCE/Scene/Terrain.scene`, puis Play.
- Inspecter `Procedural terrain` → `EnduranceAtmosphereComponent`.
- Cette scène assigne le soleil d’index 0 et aucune lune. Sa portée
  atmosphérique vaut **49 152 m**, celle du terrain **40 096 m**.
- Elle ne sauvegarde aucune marge de casters explicite et hérite donc de la
  base de 4 km. Un caster chargé pertinent peut demander une extension supérieure.
- Matrice : `ENDURANCE/Ressources/Atmosphere/biome_palettes.json`.
- API et limites : `docs/ATMOSPHERE.md`.
- Preuves et commandes : `docs/ATMOSPHERE_VALIDATION.md`.
- Historique des corrections : `docs/ATMOSPHERE_REVISION.md`.

## Décisions à préserver

Le classificateur climatique est partagé avec le terrain. Les résultats de
palette passent par trois buffers et ne sont lus qu’après une fence signalée,
sans attente. Le brouillard est échantillonné dans le monde ; son atlas couvre
les régions voisines et son vent ne déplace pas la sélection climatique.

Les références de seuil restent les derniers états acceptés ou calculés.
Les captures animées ne réinitialisent pas le seuil de SkyView. Un fondu
interrompu reprend depuis l’état affiché et publie sa dernière image même
lorsqu’un grand delta-temps dépasse sa durée.

Pendant la première capture IBL, conserver la palette visible et la dernière
cible acceptée sans avancer le fondu. La première publication relance la
transition depuis cet état, même après annulation ou avec une durée nulle.

Le vent nuageux est intégré en double dans un déplacement commun à la météo,
la densité et les ombres. Chaque capture conserve son offset figé ; la vue
courante utilise le déplacement actuel. Ne pas revenir à vitesse courante ×
temps total. Une recréation GPU du même service conserve horloge, déplacement
et palette ; un nouvel objet recommence avec un déplacement nul.

Les coefficients colorés sont en RGBA32F, l’extinction nuageuse en R32F et les
sorties intégrées/cubemaps/IBL restent HDR. Éclairage direct et IBL ne comptent
pas deux fois les disques. La saturation visible intervient après composition ;
celle de l’IBL est indépendante.

Pour les ombres de surface, conserver séparément la normale de primitive et
la normale de matériau. Le contrat `gPosition.w` réserve `-2` au ciel, les
valeurs positives à l’émission, `0` au repli historique et les entiers `<= -4`
à la normale encodée. Le sampling NEAREST et l’absence de blending de ce
G-buffer sont nécessaires. La normale est calculée avant discard ou branches.

La couverture des casters utilise toutes les bounds publiées, y compris hors
caméra, puis finalise les matrices après les `prepareRenderer()`. Un nouveau
terrain publié doit invalider le culling d’ombres dans la même image, même
caméra immobile. Le plancher utilisateur protège les renderers sans bounds ;
il ne garantit pas leur couverture au-delà de cette distance.

## Validation et prochaine action

Commande de consolidation, **PASS : 19 groupes, sortie 0** :

```bash
python3 Tests/Atmosphere/run_all.py
```

La consolidation inclut les vrais `Lights.cpp`/`LightManager.cpp` en
compilation normale et `-ffast-math`, ainsi que six publications stationnaires
du vrai scheduler terrain. Les cinq unités `Lights.cpp`, `LightManager.cpp`,
`ProceduralGeneration.cpp`, `RenderingEngine.cpp` et `Editor.cpp` passent le
contrôle syntaxique avec les vrais en-têtes. Le terrain vérifie aussi
252 triangles indépendamment du décodeur de normales et 24 576 tags RGBA32F.
Les détails chiffrés sont dans `ATMOSPHERE_VALIDATION.md`.

**À effectuer sur la machine de jeu** : compilation et lancement de l’éditeur
Windows ; retour dans la scène montrant le quadrillage ; silhouettes, contact
des ombres, montagnes hors écran mais chargées, reflets, transparents et
sauvegarde/rechargement. Mesurer Ultra sur la carte cible après réchauffement.
Les tests EGL/Mesa llvmpipe ne constituent ni cette validation visuelle ni un
benchmark matériel. Aucun contact de 50 mm à 40 km n’est garanti : les tests
courants distinguent précision numérique et empreinte du texel d’ombre.
