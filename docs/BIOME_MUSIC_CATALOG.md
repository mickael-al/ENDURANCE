# Répartition des musiques d’ENDURANCE

Le catalogue `ENDURANCE/Ressources/Sound/music_catalog.json` affecte explicitement les **77 fichiers** ajoutés au projet, soit **64 compositions** après regroupement des variantes. Ce classement artistique repose sur les titres, sans prétendre à une analyse ou une écoute des morceaux. Les niveaux restent à 1.0 par piste ; le volume global du composant reste à 0.65. L’équilibrage à l’oreille pourra modifier `gain` ensuite.

| Usage | Fichiers | Compositions | Déclenchement |
| --- | ---: | ---: | --- |
| exploration | 57 | 45 | Biome dominant stable |
| discovery | 8 | 7 | `requestDiscovery()` |
| lunar | 12 | 12 | `requestLunarEvent()` |

## Familles

| Famille | Intention | Exemples |
| --- | --- | --- |
| `Cold` | Froid, solitude, fragilité | Winter’s Flame, Frostbloom, The Pale Forest, ValkyrieTears |
| `Verdant` | Végétation, floraison, vie discrète | Dogwood Petals, Elderweald, QuietGarden, Sea of Grass |
| `Vast` | Immensité, traversée, paysages minéraux | Yonder, Fernweh, Paper Stars, AshenOne |
| `Mystery` | Étrangeté, profondeur, ruines | A Fractured Mirror, Wyrmroot, SkyRuins, The Hollow King |
| `Ocean` | Mer, eaux, rives et horizons | Dance of the Moonfish, SirenSong, RiverofSunAndMoon |
| `Lunar` | Dislocation de la lune et ancienne légende | Tout Ancient Horrors, exclusivement sur événement |

`Sea of Grass` appartient aux prairies et aux grandes étendues, pas à l’océan. `Moonflowers` et `Dance of the Moonfish` restent des morceaux d’exploration : le mot « moon » dans un titre ne déclenche pas la narration lunaire.

## Matrice des 26 biomes

Les familles sont celles de `biomeFamilies()` dans `EnduranceMusicCatalog.cpp`. Une correspondance rend une composition éligible ; plusieurs correspondances renforcent son poids. Le tirage se fait par composition puis par variante, avec exclusion des cinq dernières compositions. Les nombres ci-dessous comptent les compositions d’exploration distinctes, pas les fichiers alternatifs.

| ID | Biome | Familles | Compositions éligibles |
| ---: | --- | --- | ---: |
| 0 | PolarIce | Cold, Vast | 28 |
| 1 | FrozenTundra | Cold, Vast | 28 |
| 2 | Tundra | Cold, Vast | 28 |
| 3 | Taiga | Cold, Verdant | 31 |
| 4 | BorealBog | Cold, Verdant, Mystery | 42 |
| 5 | RockyTundra | Cold, Vast | 28 |
| 6 | ColdSteppe | Cold, Vast | 28 |
| 7 | BorealForest | Cold, Verdant | 31 |
| 8 | ColdForest | Cold, Verdant, Mystery | 42 |
| 9 | ColdMarsh | Cold, Mystery | 35 |
| 10 | DryPlain | Vast | 18 |
| 11 | Meadow | Verdant, Vast | 34 |
| 12 | TemperateForest | Verdant | 18 |
| 13 | TemperateRainforest | Verdant, Mystery | 33 |
| 14 | TemperateMarsh | Verdant, Mystery | 33 |
| 15 | RockDesert | Vast | 18 |
| 16 | DrySavanna | Vast, Verdant | 34 |
| 17 | Savanna | Verdant, Vast | 34 |
| 18 | DryJungle | Verdant, Mystery | 33 |
| 19 | TropicalJungle | Verdant, Mystery | 33 |
| 20 | VolcanicDesert | Vast, Mystery | 33 |
| 21 | RedDesert | Vast, Mystery | 33 |
| 22 | AridPlateau | Vast | 18 |
| 23 | BurningJungle | Verdant, Mystery | 33 |
| 24 | TropicalMangrove | Verdant, Ocean, Mystery | 38 |
| 25 | Marine | Ocean, Vast | 25 |

## Affectations par composition

Les noms ci-dessous correspondent exactement aux fichiers, sans extension. Une ligne contenant plusieurs versions représente **un seul groupe d’historique**. Les chemins inhabituels existants (`FinalSummon(Ver.wav`, `It's Following .wav`) sont conservés. `Path of Silence - Variant` et `Path of Sorrow` restent deux compositions : leurs titres seuls ne permettent pas d’affirmer qu’il s’agit de variantes du même morceau.

| Pack | Titres et variantes | Familles | Usage |
| --- | --- | --- | --- |
| Elderweald | `A Fractured Mirror` | Cold, Mystery | exploration |
| Elderweald | `A Lonely Firefly` | Verdant, Mystery | exploration |
| Elderweald | `Ash and Aether - Dulcimer`, `Ash and Aether Layered` | Vast, Mystery | exploration |
| Elderweald | `Cage of Briars`, `Cage of Briars - Variant` | Verdant, Mystery | exploration |
| Elderweald | `Dance of the Moonfish`, `Dance of the Moonfish - Variant` | Ocean, Mystery | exploration |
| Elderweald | `Dogwood Petals`, `Dogwood Petals - Variant` | Verdant | exploration |
| Elderweald | `Elderweald`, `Elderweald Layered` | Verdant, Mystery | exploration |
| Elderweald | `Frostbloom` | Cold, Verdant | exploration |
| Elderweald | `Moonflowers` | Verdant, Mystery | exploration |
| Elderweald | `Oathstone` | Mystery, Vast | discovery |
| Elderweald | `Opened Door` | Mystery | discovery |
| Elderweald | `Paper Stars` | Cold, Vast | exploration |
| Elderweald | `Path of Silence - Variant` | Cold, Vast, Mystery | exploration |
| Elderweald | `Path of Sorrow` | Cold, Vast | exploration |
| Elderweald | `Sea of Grass` | Verdant, Vast | exploration |
| Elderweald | `Starweft` | Cold, Mystery | exploration |
| Elderweald | `The Eld Rose`, `The Eld Rose - Layered`, `The Eld Rose - Variant` | Verdant | exploration |
| Elderweald | `The Hollow King`, `The Hollow King - Piano Solo` | Mystery | exploration |
| Elderweald | `The Pale Forest` | Cold, Verdant | exploration |
| Elderweald | `Welkin Child`, `Welkin Child - Variant` | Vast, Ocean | exploration |
| Elderweald | `Winter's Flame` | Cold | exploration |
| Elderweald | `Wyrmroot`, `Wyrmroot - Variant` | Verdant, Mystery | exploration |
| Elderweald | `Yonder` | Vast, Ocean | exploration |
| Otherworlds | `AriaOfAwakening` | Verdant, Vast | discovery |
| Otherworlds | `AshenOne` | Vast, Mystery | exploration |
| Otherworlds | `AshenWings` | Vast, Mystery | exploration |
| Otherworlds | `AuraLullaby` | Cold, Ocean | exploration |
| Otherworlds | `AwakenTheLight` | Vast | discovery |
| Otherworlds | `BalladOfSorcery` | Mystery | exploration |
| Otherworlds | `DanceOfCinders` | Vast, Mystery | exploration |
| Otherworlds | `DoomedWithATragicHeart` | Cold, Mystery | exploration |
| Otherworlds | `Elfheimr` | Verdant, Mystery | exploration |
| Otherworlds | `Enshrinement` | Mystery | discovery |
| Otherworlds | `EvenIfYouCantSeeMe`, `EvenIfYouCantSeeMe(Extended)` | Cold, Ocean | exploration |
| Otherworlds | `Fernweh` | Vast, Ocean | exploration |
| Otherworlds | `FinalSummon`, `FinalSummon(Ver` | Mystery, Vast | discovery |
| Otherworlds | `HolyMaiden` | Cold, Verdant | exploration |
| Otherworlds | `HymnOfEther` | Cold, Vast | exploration |
| Otherworlds | `KingdomofNihil` | Vast, Mystery | exploration |
| Otherworlds | `MaidenofLight` | Verdant, Vast | exploration |
| Otherworlds | `PagesOfAMemory` | Cold, Verdant | exploration |
| Otherworlds | `QuietGarden` | Verdant | exploration |
| Otherworlds | `Resolve` | Vast | discovery |
| Otherworlds | `RiverofSunAndMoon` | Ocean, Verdant | exploration |
| Otherworlds | `SirenSong` | Ocean, Mystery | exploration |
| Otherworlds | `SkyRuins` | Vast, Mystery | exploration |
| Otherworlds | `SlumberingGoddess` | Verdant, Mystery | exploration |
| Otherworlds | `SolNymphaea` | Ocean, Verdant | exploration |
| Otherworlds | `StormOfDespair` | Cold, Vast | exploration |
| Otherworlds | `ValkyrieTears` | Cold, Vast | exploration |
| Otherworlds | `WingsWhichCannotFlutter`, `WingsWhichCannotFlutter_Ver2` | Cold, Vast | exploration |
| Otherworlds | `reverie` | Ocean, Mystery | exploration |
| Ancient Horrors | `Anathema` | Lunar | lunar |
| Ancient Horrors | `Cinder and Rot` | Lunar | lunar |
| Ancient Horrors | `Cinderfall` | Lunar | lunar |
| Ancient Horrors | `Cthonic Cathedral` | Lunar | lunar |
| Ancient Horrors | `Decay and Rust` | Lunar | lunar |
| Ancient Horrors | `Go Down` | Lunar | lunar |
| Ancient Horrors | `It's Following ` | Lunar | lunar |
| Ancient Horrors | `Shudder` | Lunar | lunar |
| Ancient Horrors | `Stygian Lights` | Lunar | lunar |
| Ancient Horrors | `The Black Mirror` | Lunar | lunar |
| Ancient Horrors | `The Dark Corridor` | Lunar | lunar |
| Ancient Horrors | `The Otherside` | Lunar | lunar |

## Intégration

- `EnduranceMusicComponent` est déjà activé dans `Terrain.scene` ; aucun réglage du terrain n’est remplacé.
- Les WAV Elderweald 24 bits sont convertis au chargement vers du PCM 16 bits avec dither, sur le worker d’`AudioManager`. Les originaux restent intacts. Les autres packs utilisent directement leur PCM 16 bits.
- L’indexation ne lit que les en-têtes. Le décodeur ne conserve qu’un morceau pour la lecture ; la conversion 24 bits utilise un tampon d’entrée de 24 Kio et le tampon de sortie, pas une seconde copie intégrale.
- Fondus, respirations, stabilité du biome et historique sont déjà gérés par le directeur musical.
- Les événements prioritaires disposent de leurs pistes dédiées et des boutons de prévisualisation. Le gameplay doit appeler les fonctions aux découvertes et événements réels ; aucune menace lunaire ne se déclenche simplement parce que l’astre est visible.

Pour changer une affectation, éditer `families`, `role` ou `group` dans le JSON, puis utiliser **Reload music catalog** dans l’inspecteur. Les nouveaux fichiers non renseignés continuent à utiliser le classement automatique de secours. Un fichier retiré du projet doit également être retiré du manifeste, ou désactivé tant qu’il est encore présent.
