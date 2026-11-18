# Validation du ciel et de l'atmosphère

La passe de correction du 18 novembre 2026 passe les **12 groupes** de la
suite consolidee. Le harnais C++ du service comprend **18 groupes internes**,
en plus du test du vrai composant ENDURANCE.

Les deux derniers correctifs de continuite sont inclus dans cette validation :
palette visible maintenue pendant la premiere capture IBL et vent nuageux
integre en deplacement. Les nouvelles regressions controlent aussi les
images produites, au-dela des valeurs de l’API et du bloc uniforme.

## Tests reproductibles

Depuis la racine du dépôt :

```bash
python3 Tests/Atmosphere/run_all.py
```

Ou, pour isoler une catégorie :

```bash
python3 Tests/Atmosphere/check_cpu.py
python3 Tests/Atmosphere/check_shaders.py
python3 Tests/Atmosphere/check_common.py
python3 Tests/Atmosphere/check_luts.py
python3 Tests/Atmosphere/check_ibl.py
python3 Tests/Atmosphere/check_volumes.py
python3 Tests/Atmosphere/check_sampling.py
python3 Tests/Atmosphere/check_sky.py
python3 Tests/Atmosphere/check_game_climate.py
python3 Tests/Atmosphere/check_game_component.py
python3 Tests/Atmosphere/check_revision.py
python3 Tests/Atmosphere/check_runtime.py
```

Le test CPU utilise le vrai `AtmosphereState.hpp`, C++17 et le GLM embarqué.
Les tests GPU compilent et exécutent les vrais shaders GLSL, avec leurs includes,
dans le contexte EGL sans fenêtre déjà employé par les tests du terrain.
Ils n'ont besoin d'aucun paquet Python externe. OpenGL desktop 4.5 et EGL sont
nécessaires ; une erreur de création du contexte est un échec de disponibilité,
pas un succès silencieux.

Le test d'intégration C++ lie les véritables `Atmosphere.cpp`,
`AtmosphereSettings.cpp`, `ShaderCompilation.cpp` et `ShaderBinaryCache.cpp`
dans une bibliothèque temporaire chargée dans le même contexte EGL. Seuls
l'appartenance à l'éditeur et les objets caméra/lumière sont remplacés par
de petits objets de test. Les matrices, les appels OpenGL, les compilations,
les images, les textures et le code de publication sont ceux de production.
Ce test demande également une bibliothèque GLEW 2.2 et un compilateur C++17 ;
il réutilise les en-têtes embarqués dans `ThirdParty` et n'exige pas les
en-têtes système EGL.

Les textures de sortie sont préremplies de NaN. Les dimensions impaires
exercent les groupes de calcul partiels. Les lectures GPU bloquantes servent
exclusivement aux assertions des tests, jamais au chemin de rendu du jeu.

## Vérifications automatisées

| Test | Ce qu'il vérifie |
| --- | --- |
| `check_cpu.py` | Fondu interrompu depuis la couleur affichée ; convergence sans nouvel événement caméra ; référence du seuil conservée après les petits écarts ; changements de paramètres du gradient ; forçage et durée nulle ; valeurs HDR ; indépendance aux subdivisions du temps ; rejet des delta-times négatifs, infinis et NaN. |
| `check_shaders.py` | Compilation et liaison des 15 programmes/variantes de production présents dans `Asset/Shader/Atmosphere`, avec leurs dépendances réelles, et des sept associations matériaux/vertex : deferred, forward, transparence, eau, géométrie skinned et cloth. |
| `check_common.py` | Intégration homogène comparée à une solution analytique indépendante ; transmittance de Beer-Lambert ; limite sans extinction ; densités décroissant avec l'altitude ; radiance HDR ; conservation de l'obscurité par la correction artistique ; aller-retour de la projection du ciel ; masque du disque fini au centre, au bord et à l'extérieur, même avec un rayon de 1e-5 radian. |
| `check_luts.py` | LUT intégralement écrites ; transmittance RGB dans `[0,1]` ; variation avec l'altitude, y compris depuis l'extérieur de l'atmosphère ; extinction doublée donnant `T²` ; vide donnant `T=1` ; absence de source donnant un ciel noir fini ; lune seule ; linéarité de l'irradiance ; SkyView affectée par l'altitude et indépendante de la rotation de la vue. |
| `check_ibl.py` | Invariance d'une radiance constante HDR sous convolution diffuse et GGX ; six faces et tous les mips écrits ; élargissement d'une source angulaire par la rugosité ; fondu HDR exact ; disque visible exclu de la source IBL ; LUT BRDF bornée ; ciel HDR non modifié dans le G-buffer avant la composition finale. |
| `check_volumes.py` | Coefficients colores RGBA32F, extinction nuageuse R32F et resultats RGBA16F écrits et finis ; intégration monotone ; vraie lecture d'une texture d'ombres de terrain, même sans nuages ; brouillard absorbant comparé à la solution analytique ; modes generiques absent et lisse ; nuages deterministes selon la seed ; couverture nulle ; ombres de nuages ; vent continu à caméra immobile ; aucune source. |
| `check_sampling.py` | Fonction de composition commune au deferred et aux transparents : profondeur propre du fragment, identité à distance nulle, borne au dernier plan du volume, composition du brouillard devant le ciel et désactivation complète. Saturation appliquée au ciel après composition de tous les milieux ; sur les objets, seule la diffusion est modifiée, sans changer la réflectance ni la transmittance spectrale. |
| `check_sky.py` | Variante physique complète de génération des cubemaps : HDR, disque solaire, nuages visibles avec une couverture de 0,42, identité à couverture nulle, animation sans déplacement, IBL sans énergie directe du disque et absence de sources. |
| `check_game_climate.py` | 26 palettes forcées, concordance des poids avec le terrain à des positions/seed connues, brouillard de jeu absent/lisse/bruité, altitude et ancrage de l'atlas dans le monde. |
| `check_game_component.py` | Execute le vrai composant ENDURANCE et son hook : vent XYZ persistant, bruit anime a camera fixe avec atlas inchange, invariance 4 s / 4 × 1 s, changement de vent sans saut, vitesse nulle, deltas invalides et reprise apres reconstruction du terrain. Aucun mode GLSL artificiel. |
| `check_revision.py` | Ombres de terrain sur les nuages jusque dans le ciel compose, reference non occultee et extinction inchangees ; milieu nuageux absent neutre aux angles rasants ; continuite du rayon et de sa carte autour de l’horizon ; meteo regionale, ouvertures et profils verticaux ; vrai dessin du ciel courant independant d’une cubemap figee. |
| `check_runtime.py` | Exécution du C++ de production : allocations, formats d'images, tailles de dispatch, six faces et cinq mips publiés, bindings matériaux sans programme courant, dessin HDR, invalidation séparée des LUT, fondu à faible framerate avec rebakes continus, parallaxe des nuages sans vent, sources optionnelles, désactivation/réactivation, timers asynchrones, destruction et redémarrage. Sérialisation JSON exacte avec HDR/vecteurs, rejet atomique des entrées invalides, personnalisation de radiance cohérente entre IBL et diffusion, conservation de l'état complet après échec de compilation, brouillard de jeu ajoute au milieu generique. Ordonnanceur multi-images : limites de travail, gel des LUT/bruit/cubemap externe, revisions regroupees sans famine, publication complete, resize/reload avec ancienne IBL conservee, allocation R32F et absence de dispatchs pour les effets coupes. Source externe visible des la premiere image et hook de brouillard anime sans callback. |

Les tests utilisent de petites résolutions et des budgets réduits pour vérifier
les invariants. Ils ne remplacent pas les préréglages de qualité du moteur,
dont la valeur initiale est **Ultra**.

## Regressions de la passe de correction

`check_revision.py` execute la composition finale, et non seulement la source
nuageuse intermediaire. Son milieu isole donne un assombrissement maximal de
4,8008 en radiance HDR lorsque la shadow map du terrain occulte le soleil ;
la reference non occultee et la transmittance restent identiques.

Dans le milieu homogene de controle au ras de l’horizon, la carte et le ray
marching sont compares a `exp(-1e-6 × 64000) = 0,938005`, avec une tolerance
de 0,002. Les cas densite nulle, couverture nulle et nuages desactives donnent
une visibilite de 1. Le helper de materiau ne requiert aucun sampler de bruit
supplementaire. Le test de meteo controle les ouvertures, les trois profils
et l’advection, puis le vrai `sky.fs.glsl` est confronte au ray marching
courant en conservant la cubemap d’eclairage figee.

L’ancien test qui injectait un mode generique de brouillard GLSL 2, non expose
par le C++, a ete retire avec ce chemin inutilise. L’animation du brouillard
est maintenant verifiee par les deux vrais chemins de jeu, shader et composant.

### Complements de continuite : resultats

| Cas dedie | Contrat verifie par la nouvelle regression | Etat |
| --- | --- | --- |
| Premiere capture IBL | Palette visible fixe, deux cibles acceptees pendant la capture, reprise depuis la premiere publication sans marche arriere et convergence vers la derniere cible. | **PASS** : physique avec fondu, gradient avec fondu, physique avec duree nulle ; API, UBO et pixels HDR. |
| Vent nuageux integre | Meme deplacement pour meteo/densite/ombres, changement et arret de vitesse sans saut, valeur figee pour une capture, convergence IBL vers la position finale apres arret. | **PASS** : arret et reprise a 137 s, nouvelle direction, champs GPU reels, reference IBL figee, residu de 0,125 m apres un aller-retour de 2^24 m. |
| Caches et cycle de vie | Le vent ne doit pas recalculer les volumes statiques lorsque les nuages sont desactives. | **PASS** : compteur aerialDispatches inchange ; regression existante release/restart toujours valide. La conservation de l’horloge, du deplacement et de la palette lors de recreation GPU est un contrat relu dans le code. |

Les fixtures GLSL fournissent un deplacement deja integre ; le test C++
verifie l’integration reelle des vitesses par le service. A deplacement fixe,
changer uniquement l’horloge a 137 s puis 1 000 000 s laisse meteo, densite
et cartes d’ombres soleil/lune identiques. Deplacer observations et centre
de carte du meme vecteur retrouve les champs initiaux, ce qui verifie leur
transport commun. Les lectures GPU utilisees pour ces assertions sont
limitees aux tests et ne sont pas ajoutees au pipeline de production.

## Exécution dans l'environnement de développement

Première exécution : 18 novembre 2026, Linux, OpenGL 4.5 Core, Mesa
25.0.7, llvmpipe LLVM 19.1.7. Les programmes de production, dont la variante
physique de génération de cubemap, ont été compilés puis exécutés par ces tests.
Le journal de validation final de la branche doit préciser la compilation de
l'éditeur et les observations qui demandent encore le matériel de jeu.

Le renderer llvmpipe est logiciel. Ses durées ne prédisent pas les temps GPU
sur la carte graphique de jeu et ne constituent pas un benchmark Ultra.
L'environnement initial dispose de `g++` et de bibliothèques OpenGL/EGL,
mais pas de CMake ou Ninja dans le `PATH`. Les contrôles autonomes n'exigent
pas une reconstruction complète de l'éditeur.

Les sept unités `Atmosphere.cpp`, `AtmosphereSettings.cpp`,
`RenderingEngine.cpp`, `FrameBuffer.cpp`, `PostProcess.cpp`, `Lights.cpp` et
`EnduranceAtmosphere.cpp` ont aussi passé `g++ -std=c++17 -fsyntax-only` avec
les véritables en-têtes du moteur et les dépendances embarquées. Les options
de cette vérification sont `GLEW_NO_GLU`, `GLFW_INCLUDE_NONE`,
`ENDURANCE_ENABLE_NETWORK=0` et `ANTAGONIZ_UNIX`. Cette vérification porte
sur l'intégration des déclarations et du code C++, sans constituer une
liaison ni une exécution de l'éditeur complet.

Un panorama de diagnostic peut être produit avec Pillow en option :

```bash
python3 Tests/Atmosphere/check_sky.py --preview /tmp/atmosphere_diagnostic.png
```

Il compare ciel clair, couverture de 0,42 et même météo après 137 secondes à
position caméra constante. Il visualise les cubemaps réellement calculées ;
il n'est pas une capture de l'éditeur, n'inclut pas le terrain et emploie de
petits budgets, un disque agrandi pour le test et un tone mapping de diagnostic.

## Parcours visuel à effectuer sur le GPU de jeu

La validation visuelle porte sur le jeu rendu et demande une session de
l'éditeur. Ne pas déduire son succès des tests de compilation.

1. Traverser une frontière climatique lentement, puis rapidement ; s'arrêter
   pendant le fondu. Changer à nouveau de cible avant la fin de la transition.
   Au premier demarrage, ralentir volontairement la capture IBL et changer de
   cible avant sa publication. Controler la palette avec atmosphere physique
   active, puis avec une duree de transition nulle.
2. Monter de la plaine au-dessus de la couche de nuages ; comparer l'horizon,
   les montagnes proches et lointaines, puis redescendre.
3. Observer des matériaux mats et métalliques avec plusieurs rugosités ;
   changer de biome et faire tourner le soleil pendant la capture IBL.
4. Tester un soleil seul, soleil et lune, lune seule, puis aucune source.
   Contrôler qu'aucune direction normalisée n'est construite à partir d'un
   vecteur nul et qu'aucun disque résiduel n'apparaît.
5. Observer un brouillard voisin depuis une zone claire, puis entrer dans la
   nappe. Comparer les modes absent, lisse et bruité.
6. Désactiver les nuages et vérifier que les montagnes produisent encore des
   ombres volumétriques. Tester aussi un obstacle hors écran et les limites
   de portée des cascades d'ombres.
7. Vérifier eau, verre, particules et plusieurs couches transparentes devant
   et derrière le brouillard. La profondeur utilisée doit être celle du
   fragment transparent, et les contributions ne doivent pas être appliquées
   une seconde fois à toute l'image.
8. Réduire la résolution des volumes ; observer les crêtes de montagnes et
   objets minces en rotation et translation rapides. L'absence d'historique
   temporel ne dispense pas de contrôler la stabilité spatiale.
9. Conserver une capture IBL volontairement lente : se deplacer et tourner
   pendant sa construction. Les nuages visibles doivent suivre immediatement
   la camera ; surveiller la progression de capture et la publication complete.
   Modifier soleil/seed/profil/qualite pendant la capture, puis verifier la
   convergence de la revision publiee.
10. Regler densite ou couverture des nuages a zero avec un soleil rasant, puis
    restaurer les nuages et traverser doucement l’horizon solaire. Observer
    les ombres du terrain sur les nuages, pas seulement sur les surfaces.
11. Modifier le vent du brouillard, le mettre a zero, puis recharger la scene.
    La nappe climatique doit rester au meme endroit et le bruit ne doit pas
    sauter lors du changement de vitesse.
12. Modifier puis inverser le vent des nuages, enfin le mettre a zero. La
    formation et ses ombres doivent conserver leur position au changement de
    vitesse. Conserver une capture lente pendant l’arret et verifier qu’elle
    converge ensuite vers la derniere position affichee.
13. Mesurer séparément LUT, ciel, IBL, perspective aérienne, brouillard et
   nuages avec les compteurs ImGui, après compilation et réchauffement.
   Relever résolution d'écran, matériel, préréglage et budgets personnalisés.

## Références techniques

- Sébastien Hillaire, **A Scalable and Production Ready Sky and Atmosphere
  Rendering Technique**, EGSR 2020 :
  <https://sebh.github.io/publications/egsr2020.pdf>.
  LUT de transmittance, ciel et perspective aérienne ; approximation de
  diffusion multiple. L'implémentation du dépôt est une adaptation propre,
  pas un portage intégral de la référence.
- Eric Bruneton, **Precomputed Atmospheric Scattering**, fonctions GLSL
  documentées :
  <https://ebruneton.github.io/precomputed_atmospheric_scattering/atmosphere/functions.glsl.html>.
  Transmittance, composition entre deux points, séparation éclairage direct
  et indirect.
- Guerrilla, **Nubis: Authoring Real-Time Volumetric Cloudscapes with the
  Decima Engine**, SIGGRAPH 2017 :
  <https://www.guerrilla-games.com/read/nubis-authoring-real-time-volumetric-cloudscapes-with-the-decima-engine>.
  Formes Perlin-Worley, profils verticaux, couverture et érosion.
- Google, **Filament**, éclairage indirect et préfiltrage des environnements :
  <https://google.github.io/filament/main/filament.html>.
  Distinction irradiance diffuse, préfiltrage spéculaire et intégration BRDF.
