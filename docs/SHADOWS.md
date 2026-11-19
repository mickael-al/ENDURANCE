# Ombres : couverture, filtrage et réutilisation

Le soleil utilise quatre cascades. Les surfaces et les milieux volumétriques
lisent la même texture de profondeur ; leurs biais restent distincts.

## Couverture des cascades

`Lights::mapMemoryShadow` ajuste une sphère par intervalle de profondeur
caméra. Le rayon est calculé en espace caméra, en double précision, pour ne
pas varier à cause de la translation. Le centre XY en espace lumière est
aligné sur les texels. Une réserve de trois texels protège la couverture
après cet alignement et avec le filtre/biais par défaut.

La cascade suivante couvre aussi les derniers 10 % de l'intervalle précédent.
Les shaders mélangent les deux visibilités dans cette zone seulement. La
fraction CPU doit rester identique à `GE_SHADOW_CASCADE_BLEND_FRACTION` dans
`Asset/Shader/Shadow/shadow_filter.glsl`. Les splits publiés restent les
limites nominales, sans chevauchement.

La marge de casters de 4 km est un minimum explicite. Les bornes de tous les
chunks publiés, y compris hors champ caméra, peuvent étendre la profondeur
de chaque cascade indépendamment. Cette extension ne change pas sa taille
XY. Les renderers sans fournisseur de bornes restent couverts par la marge
explicite. Les bornes sont collectées après `prepareRenderer`, donc une
publication de terrain à caméra immobile est prise en compte immédiatement.

## Comparaison des profondeurs

`geShadowPCF` compare quatre profondeurs brutes puis interpole les quatre
visibilités. La texture reste en `NEAREST` ; interpoler ses profondeurs avant
comparaison inventerait des occultants. Un échantillon hors carte est éclairé.
Il n'y a ni texture supplémentaire ni lecture GPU vers CPU dans ce chemin.

- Surfaces : normale géométrique de primitive et biais par lumière en texels,
  avec garde numérique adaptée à la précision de position.
- God rays : biais de 0,25 m, sans normale de surface. Au bord XY d'une carte,
  le dernier texel se fond vers une cascade couvrante ou vers la lumière libre.
- Projecteurs : une seule carte en perspective couvre le cône. Le biais suit
  la taille mondiale du texel à la profondeur du récepteur et le déplacement
  homogène modifie à la fois Z et W. Les chemins opaque, transparent et eau
  appliquent cette ombre.

Le chemin habituel effectue quatre lectures de profondeur. Une seconde carte
est échantillonnée dans les zones de transition ; si plusieurs bords XY se
superposent, le volume poursuit le fondu à travers les cascades concernées.
Le biais de surface
reste un compromis : une ombre de contact plus petite que l'empreinte des
texels n'est pas garantie, surtout dans les cascades éloignées.

## Cache des cartes

`ShadowMapCache` conserve les profondeurs par couche lorsque la matrice et
les sources d'ombres versionnées n'ont pas changé. Toute réallocation de
l'atlas invalide les couches. La révision de contenu n'est publiée qu'après
la soumission de toutes les couches nécessaires.

Un `CustomRenderer::shadowContentRevision()` non nul promet que toute la
géométrie et tout l'état de matériau affectant les profondeurs sont inchangés
tant que le jeton reste identique. Le jeton doit changer après une publication,
un retrait, une modification de shader pertinente ou un redémarrage. Les
jetons de publication du terrain satisfont ce contrat. Une révision de bornes
seule n'autorise pas implicitement la mise en cache.

**Repli conservateur :** les modèles ordinaires, leurs buffers animés et les
callbacks historiques n'exposent pas encore ce contrat. Quand ils dessinent
des casters, les cartes sont recalculées. Un renderer inconnu renvoie zéro et
déclenche aussi le recalcul. L'optimisation bénéficie donc actuellement aux
scènes dont les casters sont tous explicitement versionnés, notamment le
terrain seul ; elle ne promet pas de supprimer les passes d'une scène animée.

`LightManager::getShadowRedrawnLayers()` et `getShadowReusedLayers()` indiquent
les décisions de la dernière passe. Ce sont des compteurs de couches, pas
des durées GPU. `getShadowContentRevision()` vaut zéro tant que les cartes ne
sont pas prêtes, et change lorsqu'une passe ou réallocation les renouvelle.

## Atmosphère

Le cache volumétrique utilise la révision effective des cartes. Il distingue
les coefficients de l'atmosphère, ceux des nuages et l'intégration finale.
Un changement de brouillard local peut ainsi réutiliser les coefficients,
tandis qu'un mouvement de caméra, un changement d'ombres ou de nuages relance
les passes affectées. Les hooks de brouillard inconnus continuent d'être
évalués à chaque image ; les hooks de radiance restent conservateurs.

## Validation

Exécuter `python3 Tests/Atmosphere/run_all.py`. La suite couvre les shaders de
production sur EGL, la géométrie réelle du terrain, les cas de contact, le
filtrage subtexel, les transitions de cascades, les cônes des projecteurs et
le cycle d'invalidation des caches. Un contrôle complémentaire compile les
interfaces C++ de production sans doubles de services. Les essais CPU des
matrices et du cache sont aussi exécutés avec `-ffast-math`.

Ces contrôles ne remplacent pas les mesures sur le GPU cible. En jeu, vérifier
les petits contacts, les déplacements lents, les passages de splits, les
montagnes publiées à l'arrêt, les personnages animés et les transitions météo.
