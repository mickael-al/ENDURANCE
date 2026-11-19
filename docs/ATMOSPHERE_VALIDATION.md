# Validation du ciel et de l'atmosphère

## État de la passe courante

La passe du 19 novembre 2026 ajoute la stabilisation des cascades, le PCF
continu des god rays, les ombres des projecteurs et les caches de profondeur
et de coefficients volumétriques. Les **23 groupes passent** avec sortie 0 de
`run_all.py` et le résultat `PASS all 23 atmosphere validation groups`, sur
Mesa/llvmpipe. Aucun gain en millisecondes sur GPU matériel n'est revendiqué.
Le fonctionnement et les limites du cache sont décrits dans [SHADOWS.md](SHADOWS.md).

Le contrôle de bornes inclut aussi les en-têtes publics avec les macros Windows
`min`/`max` définies après STL/GLM. Il reproduit la collision de
`numeric_limits<float>::max()` et des appels de `AtmosphereState.hpp`, vérifie
leur protection et préserve les macros de l'appelant. Ce test de préprocesseur
sur Linux ne constitue pas une compilation complète de l'éditeur sous MSVC.

Les nouveaux contrôles vérifient notamment les bords XY superposés de quatre
cascades, leurs trois transitions, une vraie carte de projecteur rasterisée,
la réutilisation de 64 images de terrain immobile et les invalidations après
publication, retrait de caster, animation inconnue ou remplacement d'atlas.
Le test runtime vérifie aussi les images diffusées, et pas seulement les
compteurs, après un changement du contenu des ombres.

### Historique de la correction des surfaces

Après le retour visuel sur `3b57f79`, la régression des ombres est reproduite
sur la vraie géométrie triangulée du terrain. Le succès historique des
15 groupes, fondé notamment sur des plans analytiques, ne démontrait pas
l’absence de quadrillage sur ce cas.

La suite étendue à **19 groupes passait le 18 novembre 2026**, avec sortie 0 de
`run_all.py` et le résultat final `PASS all 19 atmosphere validation groups`.
Elle inclut le terrain réel, la persistance des biais, la couverture adaptative
et l’encodage de la normale géométrique. Le harnais C++ du service conserve
ses contrôles internes, en plus du test du vrai composant ENDURANCE.
Aucun lancement de l’éditeur Windows ni mesure sur GPU matériel n’est
revendiqué.

Les correctifs de continuité restent présents : palette visible maintenue
pendant la première capture IBL et vent nuageux intégré en déplacement.
Leurs régressions sont conservées dans les sections correspondantes et ont
été exécutées de nouveau dans cette consolidation.

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
python3 Tests/Atmosphere/check_screen_composition.py
python3 Tests/Atmosphere/check_surface_shadows.py
python3 Tests/Atmosphere/check_position_precision.py
python3 Tests/Atmosphere/check_terrain_shadow.py
python3 Tests/Atmosphere/check_light_persistence.py
python3 Tests/Atmosphere/check_shadow_bounds.py
python3 Tests/Atmosphere/check_gbuffer_geometry.py
python3 Tests/Atmosphere/check_shadow_filtering.py
python3 Tests/Atmosphere/check_shadow_cache.py
python3 Tests/Atmosphere/check_spot_shadows.py
python3 Tests/Atmosphere/check_render_integration.py
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
| `check_shadow_filtering.py` | PCF comparé à un oracle bilinéaire indépendant ; vrais blocs opaque/transparence/eau ; trois transitions de cascades ; continuité des god rays quand plusieurs bords XY coïncident ; couches invalides et désactivation. |
| `check_shadow_cache.py` | Vrai LightManager en compilation normale et fast-math : réutilisation au repos, invalidation du contenu même à bornes identiques, matrices, casters inconnus, retraits, redémarrages, atlas remplacé, compteur de couches et publication après rendu complet. Changement des clips point/spot sans recalcul sur simple translation caméra. |
| `check_spot_shadows.py` | Douze cas d'occultant rasterisé dans une carte perspective ; limites de couverture ; vraie composition différée à cônes de 90° et 1° ; caméra confondue avec la lumière ; dépassement numérique du cosinus de Fresnel. |
| `check_render_integration.py` | Compilation des vrais renderer, lumières, atmosphère et terrain avec les en-têtes de production, sans doubles de matériaux, modèles ou services moteur. |
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
| `check_screen_composition.py` | Triangle indexe extrait de ModelManager, vrais forward.vs/fs, G-buffer NEAREST/REPEAT, volumes LINEAR/CLAMP. Ciel et opaques a trois resolutions et angles 0/+72/-68 degres, bords et desactivation ; ancien caller UV volontairement restaure en memoire comme controle negatif. La vraie branche directionnelle compare plan libre, carte vide et occultant sous camera pivotee. |
| `check_surface_shadows.py` | Helper de production, depth32/NEAREST et oracle CPU indépendant du PCF bilinéaire. Seize configurations testées avec/sans occultant, pentes 0–85°, axes signés, Y inversé, résolutions 32–2048, cartes rectangulaires, profondeur jusqu’à 100 km ; incertitude XYZ, dégénérescences, biais nul, gardes et bords. Contacts fins uniquement avec une empreinte permettant de les résoudre. |
| `check_position_precision.py` | Vrais shaders opaques écrivant 16F puis 32F, reconstruction deferred et incertitude XYZ extraite du vrai caller ; depth32/NEAREST et filtre réel. Plans à 100 m, 1 km, 10 km et 40 km, occultants résolus de 2/4 m, allocations initiale et après resize en 32F. |
| `check_terrain_shadow.py` | Chaîne réelle VBO terrain compacté → shaders d’ombre → G-buffer → deferred. Contrôle négatif figé de l’ancien filtre, 18 combinaisons de relief/soleil/marge, 438 732 évaluations sans faux auto-ombrage ; occultants réels, oracle géométrique indépendant et invariance au microdétail de matériau. |
| `check_light_persistence.py` | Vrais SceneData/LightData et json_struct : anciennes entrées sans biais, champs partiels, zéro explicite, plusieurs directionnelles distinctes, autres types de lumières, aller-retour et rechargement. |
| `check_shadow_bounds.py` | Vrais Lights/LightManager avec services GL/caméra/GUI simulés ; fitting conservateur et cache en compilation normale puis -ffast-math, setter réel et bloc de 80 octets. Le vrai scheduler terrain vérifie publication à caméra immobile, bounds de tous les chunks publiés, repos, reprise et arrêt. |
| `check_gbuffer_geometry.py` | Normales octaédriques transportées dans l’alpha RGBA32F, amplitude des dérivées, cas dégénérés/invalides, tags réservés et limites. Vrai opaque avec émission, discard et normal map ; liaison des variantes opaques statique, skinned et cloth. |

Les tests utilisent de petites résolutions et des budgets réduits pour vérifier
les invariants. Ils ne remplacent pas les préréglages de qualité du moteur,
dont la valeur initiale est **Ultra**.

## Correction courante : terrain triangulé et normale géométrique

### Diagnostic qui remplace la conclusion des anciens plans

Le nouveau contrôle utilise le VBO compacté et les shaders terrain de
production, puis le G-buffer et la composition réelle. Sur les bosses
triangulées, le filtre précédent produit environ **53 % de faux pixels
ombrés**, avec des marges de casters de 0, 4 et 16 km. Les résultats sur un
plan unique ne couvraient ni les changements de facette ni les dérivées
traversant ces frontières dans le deferred. Réduire la marge seule ne résout
pas cette régression.

Les affirmations précédentes « quadrillage résolu par le plan récepteur » et
« contacts de 50/100 mm conservés jusqu’à 40 km » sont **supersédées**. La
nouvelle validation distingue la précision des positions, la taille mondiale
du texel, le déplacement géométrique du biais et l’occultation réelle.

Le nouveau filtre compare quatre profondeurs puis interpole la visibilité.
Il utilise une normale de primitive, capturée avant discard/branches, et des
biais profondeur/normale en texels par directionnelle. La normale encodée sur
20 bits octaédriques occupe l’alpha déjà disponible de `gPosition` RGBA32F.
L’émission positive, le marqueur ciel `-2` et le repli historique `0` sont
conservés ; les entiers `<= -4` désignent le nouvel encodage.

La régression du terrain réel et les contrôles ci-dessous passent dans la
même consolidation, avec les biais par défaut `0,5 / 1,5` et le plancher
angulaire `0,70710678`. La validation porte sur ces scènes numériques et les
conditions décrites ; l’observation dans le jeu reste à effectuer.

### Résultats de la consolidation

| Contrôle | Résultat disponible pour cette passe |
| --- | --- |
| Composition écran | **PASS** : 18 dessins ciel/opaques et vraie branche directionnelle. |
| Filtre de surface | **PASS** : 16 configurations avec/sans obstacle et contrôles complémentaires, soit 51 évaluations GPU. |
| Précision des positions | **PASS** : aucun faux auto-ombrage 32F, occultants résolus de 2/4 m conservés jusqu’à 40 km. |
| Encodage de normale | **PASS** : 33 388 normales, contrat d’alpha et vrais producteurs opaques. |
| Persistance et API des biais | **PASS** : SceneData/json_struct ; setter réel, clamp et entrées non finies en -ffast-math. |
| Couverture des casters | **PASS** : projection indépendante de 2 304 coins, vrais Lights/LightManager et six publications stationnaires du vrai terrain. |
| Terrain réel | **PASS** : 18 combinaisons, 438 732 évaluations sans faux auto-ombrage ; vrais occultants conservés et ancien défaut reproduit par le contrôle négatif. |
| Suite consolidée à 19 groupes | **PASS**, sortie 0 le 18 novembre 2026. |

### Terrain réel et contrôle négatif

Les reliefs de ces essais sont des maillages définis par le test, encodés dans
le format compacté du terrain. Ils passent dans les shaders de production,
mais ne constituent pas une capture de la scène complète ni une validation
de tous les reliefs produits par le générateur climatique.

Le test parcourt **18 combinaisons** : trois reliefs triangulés (bosses,
ruptures et pente monotone), deux élévations du soleil (90° et 60°), et trois
marges de casters (0, 4 et 16 km). Sur **438 732 évaluations de récepteurs**,
aucune visibilité ne descend sous le seuil de faux auto-ombrage `0,999` ;
la visibilité minimale mesurée est **1** et l’occlusion moyenne est **0**.
Le helper précédent, conservé tel quel pour le contrôle négatif, produit
encore environ **53–54 %** de faux pixels sur les bosses et **22 %** sur les
ruptures. Réduire sa marge de casters n’élimine pas ce défaut.

Le G-buffer transporte **24 576 tags réels**. L’oracle calcule indépendamment
la normale de **252 triangles** à partir des positions décodées du VBO ;
l’écart angulaire maximal avec la normale transportée vaut **0,155738°**.
Retirer uniquement ce tag et forcer le repli historique réintroduit cinq
faux pixels, soit **0,020435 %** dans le cas concerné. Faire varier le
microdétail du matériau modifie sa normale jusqu’à **0,311802**, tandis que
positions et tags restent identiques bit à bit et que la visibilité reste
strictement identique. Le biais ne dépend donc pas de la normal map.

Un véritable caster situé à l’altitude de 400 m conserve **36 récepteurs entièrement
ombrés**. Les occultants plus proches du sol vérifient aussi le déplacement
du bord et la largeur du filtre :

| Écart avec le plan | Taille mondiale du texel d’ombre | Occlusion intérieure | Visibilité extérieure | Erreur maximale du bord à 50 % | Largeur maximale de transition 90→10 % |
| --- | --- | --- | --- | --- | --- |
| 2 m | 1 m | 100 % | 1 | 0,387972 texel | 0,800079 texel |
| 4 m | 1 m | 100 % | 1 | 0,387990 texel | 0,800067 texel |
| 0,5 m | 0,125 m | 100 % | 1 | 0,399636 texel | 0,800288 texel |

Les deux dernières colonnes sont mesurées en texels projetés au sol. Ces
contrôles distinguent l’absence d’auto-ombrage, la présence d’une vraie ombre
et la transition du filtre ; ils ne garantissent pas la résolution d’un
contact plus petit que l’empreinte de la cascade.

### Composition du triangle réel

Les 18 cas ciel/opaques, à trois résolutions et avec des inclinaisons de
0, +72 et -68°, conservent une erreur HDR maximale de **4,4e-7**, bords compris.
Rétablir uniquement les anciens appels `v_UV` en mémoire produit des erreurs
de **0,1803** sur le ciel et **0,4351** sur les opaques. Ce contrôle négatif
vérifie l’incompatibilité des anciens UV avec les volumes CLAMP.

La vraie branche directionnelle est exercée avec une caméra à 8 m, une carte
de 256 × 256 et un occultant de 2 m : **149 pixels assombris, 1 812 clairs**,
assombrissement HDR maximal **1,836**. Le plan libre correspond à une carte
vide. Le contrôle de normale géométrique donne une erreur de **1,3e-6** ;
fournir volontairement le mauvais espace de normale donne une erreur de
**0,9272**. Ces mesures restent distinctes de la régression de terrain triangulé.

### Filtre directionnel et contacts résolus

`check_surface_shadows.py` exécute 16 configurations sans puis avec obstacle
(32 évaluations), deux cas de précision/profondeur, deux cas dégénérés,
un cas de biais nul, neuf gardes et cinq bords/coin : **51 évaluations GPU**.
L’erreur maximale par rapport à l’oracle indépendant de visibilité est
**8,93e-8**. Le test contrôle les poids bilinéaires, les couches de texture,
les taps hors carte et l’indépendance entre biais géométrique et profondeur Z.

Les contacts de **10 mm sur plan plat** et de **50 mm à 85°** sont préservés
avec des **texels de 1 mm**. Ce résultat ne promet pas les mêmes contacts
avec une cascade dont le texel couvre plusieurs mètres. Le déplacement du
biais géométrique reste proportionnel à cette empreinte, et la marge numérique
reste une contribution séparée.

### Précision du G-buffer et occultants à grande distance

Le vrai shader opaque écrit les positions dans les deux formats. À 1 km,
l’erreur mondiale maximale passe d’environ **0,50 m en 16F à 0,267 mm en 32F**.
À 40 km, celle du 32F atteint **0,007971 m**. Le 16F reste un contrôle négatif ;
les positions de production sont en RGBA32F à la création comme au resize.

| Distance du plan | Plan libre éclairé en 32F | Occultant de 2 m | Occultant de 4 m |
| --- | --- | --- | --- |
| 100 m | 100 % | 100 % occulté | 100 % occulté |
| 1 km | 100 % | 100 % occulté | 100 % occulté |
| 10 km | 100 % | 100 % occulté | 100 % occulté |
| 40 km | 100 % | 100 % occulté | 100 % occulté |

Ces occultants représentent huit et seize texels dans le fixture. Au stress
40 km, la marge purement numérique vaut **20,551 à 20,564 mm** ; le biais
proportionnel au texel est contrôlé séparément. Il ne faut plus lire cette
régression comme une garantie de contacts de 50 mm à cette distance.

Le passage au 32F, réalisé précédemment, ajoute 8 octets par pixel par rapport
au 16F (+15,82 Mio à 1080p). Le nouvel encodage de normale réutilise ce canal
existant : aucune cible ni bande passante d’attachement supplémentaire n’est
ajoutée. Les mesures présentes portent sur la justesse, pas sur un temps de trame.

### Encodage de la normale de primitive

`check_gbuffer_geometry.py` transporte **33 388 normales** dans une vraie
texture RGBA32F. L’erreur vectorielle maximale est **0,0041474**, sous la borne
0,005. Les cas couvrent axes et replis octaédriques, dérivées d’échelles
`1e-30` à `1e30`, dégénérescences et valeurs invalides. Le code ne doit pas
confondre valeurs fractionnaires, hors intervalle, NaN ou infini avec une normale.

La liaison des variantes opaque, skinned et cloth est vérifiée. Le vrai opaque
préserve l’émission positive et le discard ; la normale de primitive reste
indépendante de la normal map. Le G-buffer est NEAREST et n’est ni mélangé ni
résolu depuis un attachement multisample, ce qui préserve les entiers encodés.

### Couverture adaptative et persistance

La base par défaut est **4 000 m** ; elle peut être augmentée selon les bounds
chargées pertinentes pour chaque cascade. Le fitting ne réduit jamais le
plancher explicite : une scène qui sauvegarde 16 000 m conserve ce choix.
Les renderers sans bounds ne bénéficient que du plancher, et des géométries
situées au-delà peuvent nécessiter une valeur supérieure. Le terrain fourni
publie les AABB de tous ses maillages, y compris hors caméra.

La régression CPU lie les vrais Lights/LightManager et les exécute en
compilation normale puis `-O3 -ffast-math`. Elle vérifie **2 304 coins projetés
indépendamment, dans 288 configurations**, rejet XY/aval, contacts de bounds,
plancher manuel, matrices XY et splits inchangés, absence d’allocation GPU,
cache immobile et redémarrage à la même adresse. Le vrai scheduler terrain
publie ensuite **six révisions à caméra immobile** ; ses bounds restent
exactement celles des chunks publiés et sont invalidées à la reprise/à l’arrêt.

Le même test contrôle le vrai setter des biais : plage `[0,8]`, zéro conservé,
NaN/infini ramenés aux défauts, absence d’upload si la valeur bornée ne change
pas et publication dans le bloc de 80 octets. Le test de persistance utilise
les vrais SceneData/LightData et json_struct, y compris scènes anciennes,
champs partiels et plusieurs lumières distinctes. Clone, load et save de
l’éditeur ont été relus ; son exécution Windows reste à effectuer.

## Régressions atmosphère et continuité des passes précédentes

Les régressions de cette section proviennent des passes précédentes. Elles
passent de nouveau dans la consolidation courante des 19 groupes.

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

Les passes précédentes avaient vérifié la syntaxe des unités atmosphère,
G-buffer et post-traitement avec les vrais en-têtes. Pour la passe courante,
les cinq unités `Lights.cpp`, `LightManager.cpp`, `ProceduralGeneration.cpp`,
`RenderingEngine.cpp` et `Editor.cpp` passent `c++ -std=c++17 -fsyntax-only`,
avec `GLEW_NO_GLU`, `ENDURANCE_ENABLE_NETWORK=0` et les en-têtes du moteur/ThirdParty.
Ce contrôle vérifie l’intégration C++ ; il ne constitue ni une édition de
liens ni un lancement de l’éditeur Windows.

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
