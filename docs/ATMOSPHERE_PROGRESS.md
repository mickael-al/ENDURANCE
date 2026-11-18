# Ciel et atmosphère — état de reprise

## Base de travail

- Branche d'intégration : `feature/atmosphere-biome-sky`.
- Base utilisateur : `feature/terrain-noise-library`, commit GitHub `2ab58174c1a33f8b1b795b466075fd618424e933`.
- Arbre de base vérifié : `bc47314734e8bfd1b3f915e53b6a08b4e9d99238`. La copie locale initiale porte le même arbre sous le commit `a14d15c`.
- Ne pas repartir de `master` : la bibliothèque Noise, les recettes récentes et les réglages volcaniques sont sur la branche de développement.

## Étapes

1. **Palettes et interfaces** : matrice des 25 biomes et fond marin, données JSON linéaires, interface moteur sans noms de biomes, transition indépendante du framerate. Implémenté.
2. **Ciel et IBL** : cubemaps HDR cohérentes, irradiance, préfiltrage GGX, BRDF partagée, sources explicites et état affiché commun. Implémenté ; ce palier affiche le gradient artistique, avec les milieux physiques désactivés par défaut.
3. **Atmosphère** : LUT de transmittance et diffusion multiple, SkyView, perspective aérienne à la profondeur des fragments. Implémenté et activé. Transmittance et diffusion multiple ont des invalidations séparées. Le brouillard et les ombres volumétriques sont ajoutés au palier 4, puis les nuages au palier 5.
4. **Brouillard et rayons** : atlas de propriétés spatiales GPU, brouillard lisse/bruité/absent, ombres terrain y compris hors écran dans les cascades, extinction et diffusion composées ensemble. Implémenté et activé ; les nuages restent désactivés dans ce palier.
5. **Nuages et optimisation** : bruit Perlin–Worley, lumière atténuée, profils qualité Ultra par défaut, temps GPU séparés, validation. À intégrer.

## Décisions conservées

- La recette climatique du terrain reste l'unique référence. Évaluation GPU du point caméra ; lecture CPU différée seulement après signalement d'une fence, sans attente.
- Le climat complet coûte trop cher pour chaque pas volumétrique. Les propriétés locales du brouillard sont mises en cache sur une carte GPU en coordonnées mondiales ; interpolation spatiale documentée.
- Les coefficients physiques, de brouillard et de nuages sont additionnés avant l'intégration. Les transparents échantillonnent la diffusion et la transmittance à leur propre profondeur.
- Le disque direct reste visible ; l'IBL est filtrée sans ce disque pour éviter le double comptage avec les lumières directionnelles.
- Pas d'accumulation temporelle dans cette première version : pas d'historique à réinitialiser lors des téléportations, des changements de seed ou des occultations.

## Validation à compléter

Palier 2 : transitions C++, classification climatique, HDR et filtrage IBL vérifiés par les tests dédiés. Les fonctions de support GLSL sont déjà présentes car le chargeur développe les includes avant le préprocesseur ; les passes physiques seront activées au palier suivant.


Tests OpenGL réels possibles avec EGL/llvmpipe dans cet environnement. Les temps de ce rendu logiciel ne doivent pas être présentés comme des performances sur une carte graphique de jeu. Compilation C++ et essais visuels de la scène Windows restent à distinguer des tests isolés des shaders.
