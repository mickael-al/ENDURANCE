# Quatre biomes froids : largeur, espacement et silhouettes

> Bilan de la passe du 13 novembre. Les réglages polaires ultérieurs de l’utilisateur et la sculpture 3D des parois sont décrits dans [POLAR_ICE_RIFT_3D.md](POLAR_ICE_RIFT_3D.md).

Seconde passe du 13 novembre 2026, après le retour en jeu sur `e1c6f52`. La première passe atteignait les hauteurs demandées mais ses montagnes trop étroites et trop rapprochées révélaient la grille de placement. Cette correction concerne uniquement Glace polaire, Toundra gelée, Toundra et Taïga. Les autres recettes ne changent pas.

## Réglages actuels

Les dimensions sont exprimées en mètres. Les hauteurs sont relatives au sol local, à `detailScale=0,35`, dans un biome isolé. Les largeurs sont les diamètres nominaux des supports dans le domaine de placement, pas les largeurs à mi-hauteur. La déformation des coordonnées fait varier les dimensions et distances physiques.

| Formation | Espacement nominal avant → après | Largeur nominale avant → après | Hauteur / profondeur |
| --- | --- | --- | --- |
| Massif polaire | 4 → 20 km (×5) | 1,6–2,24 → 6,4–8,96 km (×4) | 450–580 m conservés |
| Montagne gelée | 2 → 8 km (×4) | 0,8–1,12 → 2–2,8 km (×2,5) | 420–560 m conservés |
| Massif de toundra | 1 → 6 km (×6) | 0,4–0,56 → 2–2,8 km (×5) | 800 m conservés |
| Crête de taïga | 2 → 8 km (×4) | 0,8–1,12 → 2,4–3,36 km (×3) | 700 m conservés |
| Faille polaire | 2 → 8 km (×4) | Support longitudinal conservé, ouverture étroite et variable | 230–330 → 700–900 m de profondeur |
| Nappe / tunnel polaire | 2,4 → 9,6 km (×4) | Support conservé | Enveloppe verticale conservée |

L’interprétation de « espacer de 5 » est un espacement multiplié par cinq. Ces intervalles sont des paramètres nominaux de population, pas des minima centre-à-centre stricts ni une garantie d’en rencontrer sur un trajet. Les montagnes et failles gardent 80 % de candidats ; les nappes polaires 65 %. Les grottes gelées, rochers de toundra et rivières de taïga conservent leurs populations précédentes.

## Placement et formes

`landSparseSite` sépare la taille des formations de leur espacement. Il tourne le domaine par une orientation dépendant de la seed, le déforme à deux échelles de bruit lisse, puis décale fortement les centres. Les cellules vides et les empreintes compactes évitent une rangée complète de montagnes. La marge de support de 2,5 % de cellule et la déformation sans repli préservent la continuité entre cellules. La seed reste déterministe et indépendante du chunk ou du LOD.

La glace polaire utilise une crête glaciaire courbe. La toundra gelée reçoit une croupe arrondie et un cirque sur un flanc. La toundra a des épaules décalées, des arêtes et des creux latéraux. La taïga utilise une longue crête aux pentes dissymétriques, avec contreforts. Les ondulations du sol sont élargies horizontalement d’environ ×3 à ×4, sans hausse générale de leur amplitude ; les cordons cosinus périodiques de la taïga sont retirés.

Les failles polaires ont un contour bruité, une largeur variable, des extrémités effilées et une branche secondaire. `PolarIce/shape.glsl` partage leur tracé entre le relief et les petites grottes au fond. Les nappes 3D passent d’une dalle rectangulaire à un corps elliptique à rive irrégulière, tout en conservant un toit large. Les grandes profondeurs sont creusées en 2D ; la bande 3D reste limitée aux petites grottes locales.

Pour éditer ensuite un biome : `spacing` règle la population, `footprintScale` la largeur, le profil local la silhouette et son amplitude la hauteur. `LandSite.center` est une ancre **dans le domaine déformé** ; utiliser `q` pour la sculpture, sans traiter cette ancre comme une position mondiale. Le détail des unités reste dans [BIOMES.md](BIOMES.md).

## Validation

Les sondes exécutent les recettes GLSL réelles sous OpenGL 4.5 / Mesa llvmpipe. Elles contrôlent la distribution en vue du dessus, les profils, les bornes et le raccord entre relief et volume. Les figures sont des diagnostics sans textures ni végétation, et non un rendu du jeu. Elles ne valident pas les collisions du personnage ni le coût sur le GPU Windows.

Les treize formations des sept biomes froids passent les sondes ciblées, les contrôles de cavités et les comparaisons avec le pipeline/LOD ; les quatre sites primaires modifiés passent également leur validation. Le diagnostic climatique des 26 biomes fonctionne avec les nouvelles ancres déformées. Le contrôle général des 26 recettes passe : valeurs finies, plages de bandes, déterminisme, transitions climatiques et marines, concordance entre sondes isolées et accumulation réelle, poids locaux et échantillons LOD partagés. Le rejet conservateur reste identique à l’évaluation exhaustive sur 41 154 densités aux grandes coordonnées. Les petites grottes et nappes polaires ont aussi été échantillonnées en volume : cavités ouvertes sous plafond et absence de fuite significative hors bande.

Les fichiers `cold_revision.json` et `cold_profiles.png` restent le bilan historique de la première passe, clairement identifié dans [COLD_BIOME_REVISION.md](COLD_BIOME_REVISION.md).

### Mesures ciblées de la seconde passe

Comparaison avec les shaders de `e1c6f52`, seed 1337. Chaque largeur est mesurée sur une formation isolée en conservant le sol et les autres populations identiques, au seuil de 10 % de la hauteur du pic. Le diamètre équivalent vient de la surface au-dessus de ce seuil ; ce n’est pas une largeur dans une direction particulière. Les distances viennent de 100 cellules par population et mesurent le voisin actif le plus proche.

| Biome | Largeur effective mesurée avant → après | Rapport | Distance médiane au voisin avant → après | Pic isolé |
| --- | --- | --- | --- | --- |
| Glace polaire | 1,395 → 5,476 km | ×3,93 | 3,491 → 17,045 km | 563,6 m |
| Toundra gelée | 0,730 → 1,854 km | ×2,54 | 1,748 → 6,072 km | 491,8 m |
| Toundra | 0,333 → 1,662 km | ×4,99 | 0,872 → 5,089 km | 800,0 m |
| Taïga | 0,738 → 1,987 km | ×2,69 | 1,776 → 6,817 km | 700,0 m |

La faille polaire ciblée passe de 238,7 à 717,3 m de profondeur. Les fronts sont testés aux véritables frontières du domaine déformé, retrouvées par inversion numérique dans les sondes uniquement. Les supports y sont nuls et les différences de hauteur convergent lorsqu’on rapproche les points de part et d’autre. Ce test ne remplace pas une vérification des silhouettes en jeu.

- [Vue du dessus et profils avant/après](biome-validation/cold_spacing_atlas.png)
- [Contour et profondeur de la faille polaire](biome-validation/polar_rift_contour.png)
- [Mesures détaillées](biome-validation/cold_spacing_revision.json)

```sh
EGL_PLATFORM=surfaceless python3 Tests/Terrain/check_cold_biome_spacing.py --reference-shaders /chemin/vers/e1c6f52/Asset/Shader/ProceduralTerrain --output /tmp/endurance-cold-spacing
```
