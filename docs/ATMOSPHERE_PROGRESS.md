# Ciel et atmosphère — état de reprise

## État actuel

Les cinq paliers fonctionnels sont implémentés. La branche d'intégration est
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

Les extinctions et sources sont additionnées avant l'intégration. Les quatre
volumes de coefficients sont en RGBA32F ; les sorties intégrées, les cubemaps et
l'IBL sont en HDR. L'éclairage direct et l'IBL ne comptent pas deux fois les disques.
La saturation de l'atmosphère visible est appliquée après la composition ; celle
de l'IBL est indépendante. Les ombres utilisent la profondeur des vraies cascades,
jamais une estimation à partir de la profondeur écran.

## Validation et prochaine action

Les tests CPU, shaders OpenGL et service C++ de production fonctionnent sur
EGL/Mesa llvmpipe. Commande consolidée :

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
