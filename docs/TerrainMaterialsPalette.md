# ENDURANCE terrain art direction

The game palette lives in
`ANTAGONIZ/Source/ProjectUser/EnduranceTerrainMaterials.cpp`. The renderer's
generic ABI and validation live in
`ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainMaterials.hpp`.

Every biome has **four independently editable materials**: ground base, ground
accent, wall base and wall accent. Each material has three explicit color stops
with independent roughness, plus normal strength, roughness variation, palette
shape and procedural-pattern weights. There are 104 materials and 312 color
stops, including the marine seabed.

## Color and lighting contract

- Hex swatches in the game source are authored in **display sRGB**. They are
  converted once on the CPU to linear reflectance before upload. Saved runtime
  material values and GPU values are linear; do not decode them a second time.
- The palettes describe matter, not baked sunlight, shadows, fog or emission.
  Directional light, environment lighting, atmosphere and SSAO remain responsible
  for those effects.
- These natural materials start with metallic `0` and material AO `1`. Wet peat,
  exposed ice and damp stone vary through roughness and normal response, not
  metallic. The generic material supports other values when an authored surface
  actually needs them.
- Large regions carry the color composition. Medium stripes or ripples identify
  the surface. Fine detail mostly modulates the normal and roughness. Bright
  random albedo speckles are deliberately absent.
- Pale deposits are accents with masks; they are not a universal gray material
  substituted for every steep face. The wall palette belongs to the biome too.

## Biome recipes

The table follows the authoritative `BiomeNames` order. Accent names describe
the material pattern, not additional geometry or vegetation meshes.

| ID | Biome | Ground base / accent | Wall base / accent | Main visual intent |
|---:|---|---|---|---|
| 0 | PolarIce | Windblown snow / exposed ice | Deep ice / frost veins | Ivory snow, blue ice, restrained turquoise veins |
| 1 | FrozenTundra | Frozen sediment / thin snow | Cold schist / mineral frost | Taupe deposits below slate-colored frosted massifs |
| 2 | Tundra | Dry humus / lichen | Tundra rock / pale oxidation | Olive and ochre vegetation against violet-gray stone |
| 3 | Taiga | Dark moss / dry needles | Pillar stone / pale deposits | Deep greens, brown litter and muted warm strata |
| 4 | BorealBog | Wet peat / pale moss | Wet rock / cold lichen | Quiet peat, desaturated moss, damp blue-green walls |
| 5 | RockyTundra | Gray gravel / bronze lichen | Dark mineral rock / quartz veins | Cold gravel and graphite cliffs with sparse light veins |
| 6 | ColdSteppe | Ash-colored grass / cold sand | Gray sandstone / cream layers | Dry muted gold below blue-gray stone and arches |
| 7 | BorealForest | Boreal moss / russet litter | Plateau rock / limestone strata | Cold forest greens and warm gray stepped rock |
| 8 | ColdForest | Cold soil / ground vegetation | Chasm schist / clay veins | Damp earth and cool blue chasm walls |
| 9 | ColdMarsh | Cold mud / island moss | Satin rock / pale crust | Blue mud and large subdued green mineral formations |
| 10 | DryPlain | Ochre earth / pale dust | Plain rock / cream sediment | Broad calm ochre regions and warm sedimentary stone |
| 11 | Meadow | Meadow grass / exposed soil | Meadow stone / gold lichen | Golden greens with small patches of earth |
| 12 | TemperateForest | Temperate moss / amber litter | Forest rock / soft lichen | Moss greens, amber litter and warm gray stone |
| 13 | TemperateRainforest | Wet humus / bright moss | Ravine rock / vegetation patina | Deep humid greens with restrained yellow-green accents |
| 14 | TemperateMarsh | Olive mud / reed cover | Damp clay / dry deposits | Olive mud, gold vegetation and worn damp walls |
| 15 | RockDesert | Copper gravel / gold sand | Desert sandstone / ivory beds | Copper sandstone and pale sedimentary bands |
| 16 | DrySavanna | Blond grass / baked soil | Brown rock / limestone dust | Dry gold, baked earth and muted brown stone |
| 17 | Savanna | Olive grass / red soil | Savanna stone / pale layers | Golden olive regions broken by red earth |
| 18 | DryJungle | Olive cover / caramel litter | Warm stone / pale patina | Deep olive and caramel with weathered warm rock |
| 19 | TropicalJungle | Tropical humus / jade moss | Tropical stone / cream deposits | Dark humid greens, jade accents and damp stone |
| 20 | VolcanicDesert | Charcoal basalt / cold ash | Volcano basalt / ferric oxides | Readable charcoal, blue-gray ash and sparse warm oxides |
| 21 | RedDesert | Red sand / amber sand | Red sandstone / cream sandstone | Terracotta dunes and broad amber variation |
| 22 | AridPlateau | Plateau dust / golden crust | Plateau sandstone / pale beds | Pale dust and large sedimentary cliff strata |
| 23 | BurningJungle | Bronze ground cover / warm ash | Dark stone / copper patina | Dark bronze vegetation and matte warm mineral accents |
| 24 | TropicalMangrove | Mangrove mud / salt deposits | Jade rock / lichen patina | Green-brown mud, ivory deposits and muted wet jade |
| 25 | Marine | Marine sand / dark sediment | Marine rock / calcareous deposits | Desaturated seabed sand and green slate; no baked depth fog |

## Editing guidance

Keep the base palette close enough to read as one material. Use the accent
material when the actual surface changes, such as snow to exposed ice or peat
to dry moss. Masks control placement and blend width; palette contrast controls
variation inside the material. Those are different controls.

Shared macro and patch fields keep the computational cost bounded. Strata and
ripples have per-surface metre scales, direction and distortion. The palette
softness can move from continuous gradients to discrete bands; screen-space
filtering still applies at band boundaries.

The authored game defaults use a 384 m macro field and a 96 m patch field.
Most primary wall recipes give strata only a small part of the palette weight;
sedimentary deserts keep a stronger stratification. Wall periods range from
43 to 155 m, with distortion around 40–48% of that period. Accent masks are
deliberately broad and sparse, so the result retains quiet areas instead of
covering the terrain in small high-contrast spots.

Use the terrain material preview views and compare under direct light, shadow,
and the existing environment illumination. Roughness changes are easiest to
judge at grazing angles. A successful biome should remain readable at distance
after fine normal detail disappears.
