# ENDURANCE terrain art direction

The game palette lives in
`ANTAGONIZ/Source/ProjectUser/EnduranceTerrainMaterials.cpp`. The renderer's
generic ABI and validation live in
`ANTAGONIZ/Header/Miscellaneous/ProceduralTerrainMaterials.hpp`.

Every biome has **four independently editable materials**: ground base, ground
accent, wall base and wall accent. Each material has three explicit color stops
with independent roughness, plus normal strength, roughness variation, palette
shape and procedural-pattern weights. There are 104 materials and 312 color
stops, including the marine seabed. Ground and wall have independent normal
families; each biome has an independently authored patch scale and fine-noise
spectrum. Color masks do not displace the surface normal.

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
| 4 | BorealBog | Wet peat / subdued moss | Wet rock / cold lichen | Quiet peat, desaturated moss, damp blue-green walls |
| 5 | RockyTundra | Gray gravel / bronze lichen | Dark mineral rock / quartz veins | Cold gravel and graphite cliffs with sparse light veins |
| 6 | ColdSteppe | Ash-colored grass / cold sand | Gray sandstone / cream layers | Dry muted gold below blue-gray stone and arches |
| 7 | BorealForest | Boreal moss / russet litter | Plateau rock / limestone strata | Cold forest greens and warm gray stepped rock |
| 8 | ColdForest | Cold soil / ground vegetation | Chasm schist / clay veins | Damp earth and cool blue chasm walls |
| 9 | ColdMarsh | Cold mud / island moss | Satin rock / mineral crust | Blue mud and large subdued green mineral formations |
| 10 | DryPlain | Ochre earth / amber dust | Plain rock / ochre sediment | Broad subdued ochre regions and warm sedimentary stone |
| 11 | Meadow | Olive grass / grassy soil | Gray-green stone / olive lichen | Deep meadow greens with restrained earth accents |
| 12 | TemperateForest | Dark moss / humified litter | Forest rock / soft lichen | Dark moss greens, close olive-brown litter and warm gray stone |
| 13 | TemperateRainforest | Wet humus / damp moss | Ravine rock / vegetation patina | Deep humid greens with nearby green accents |
| 14 | TemperateMarsh | Olive mud / reed cover | Damp clay / dry deposits | Dark olive mud, subdued reeds and worn damp walls |
| 15 | RockDesert | Copper gravel / copper sand | Desert sandstone / amber beds | Dark copper sandstone and restrained warm beds |
| 16 | DrySavanna | Straw grass / baked soil | Brown rock / ochre dust | Muted straw gold, baked earth and brown stone |
| 17 | Savanna | Olive grass / ferric soil | Savanna stone / ochre layers | Golden olive and nearby subdued ferric earth |
| 18 | DryJungle | Olive cover / dry litter | Warm stone / olive patina | Deep olive and brown litter with weathered warm rock |
| 19 | TropicalJungle | Tropical humus / jade moss | Tropical stone / vegetation patina | Dark humid greens, jade accents and damp stone |
| 20 | VolcanicDesert | Charcoal basalt / cold ash | Volcano basalt / ferric oxides | Readable charcoal, blue-gray ash and sparse warm oxides |
| 21 | RedDesert | Red sand / amber sand | Red sandstone / amber sandstone | Rich terracotta dunes and subdued copper-amber beds |
| 22 | AridPlateau | Plateau dust / ochre crust | Plateau sandstone / amber beds | Earthy sandstone dust and large sedimentary cliff strata |
| 23 | BurningJungle | Bronze ground cover / warm ash | Dark stone / copper patina | Dark bronze vegetation and matte warm mineral accents |
| 24 | TropicalMangrove | Mangrove mud / dry sediment | Jade rock / lichen patina | Dark green-brown mud, close dry sediment and muted wet jade |
| 25 | Marine | Marine sand / dark sediment | Marine rock / calcareous deposits | Desaturated seabed sand and green slate; no baked depth fog |

## Editing guidance

Keep the base palette close enough to read as one material. Use the accent
material when the actual surface changes, such as snow to exposed ice or peat
to dry moss. Masks control placement and blend width; palette contrast controls
variation inside the material. Those are different controls.

## Surface profiles and scales

The shared macro field provides broad composition. Patch periods are multiplied
per biome, and the fine field has its own period, octave count and gain. A stable
biome-specific domain prevents unrelated materials from tracing the same patch
boundary. Normal families are deliberately separate from palette weights and
large color strata; changing color contrast must not emboss the whole surface.

These are stylized matter cues, not simulations of every geological process:

| ID | Biome | Ground normal: period | Wall normal: period | Patch period | Fine period / octaves |
|---:|---|---|---|---:|---|
| 0 | PolarIce | Wind ribs: 1.8 m | Ice veins: 4.5 m | 158 m | 6.5 cm / 2 |
| 1 | FrozenTundra | Frozen grain: 11 cm | Schist bedding: 1.7 m | 113 m | 11 cm / 2 |
| 2 | Tundra | Organic fibres: 28 cm | Fractures: 85 cm | 81 m | 12 cm / 2 |
| 3 | Taiga | Litter fibres: 17 cm | Mineral fractures: 2.4 m | 136 m | 14 cm / 2 |
| 4 | BorealBog | Wet folds: 45 cm | Damp stone folds: 90 cm | 65 m | 12 cm / 1 |
| 5 | RockyTundra | Mineral grain: 18 cm | Fractures: 1.15 m | 102 m | 18 cm / 3 |
| 6 | ColdSteppe | Wind ribs: 42 cm | Bedding: 2.6 m | 206 m | 9 cm / 2 |
| 7 | BorealForest | Moss/litter fibres: 23 cm | Bedding: 1.45 m | 89 m | 11 cm / 2 |
| 8 | ColdForest | Humus fibres: 32 cm | Damp fractures: 2.15 m | 122 m | 16 cm / 2 |
| 9 | ColdMarsh | Wet folds: 75 cm | Polished folds: 1.4 m | 71 m | 14 cm / 1 |
| 10 | DryPlain | Fine soil grain: 7 cm | Bedding: 1.1 m | 221 m | 7 cm / 2 |
| 11 | Meadow | Grass/soil fibres: 12 cm | Fractures: 95 cm | 84 m | 9 cm / 2 |
| 12 | TemperateForest | Litter fibres: 19 cm | Fractures: 1.35 m | 108 m | 13 cm / 2 |
| 13 | TemperateRainforest | Humus fibres: 14 cm | Damp folds: 50 cm | 60 m | 12 cm / 2 |
| 14 | TemperateMarsh | Wet folds: 36 cm | Clay bedding: 90 cm | 142 m | 11 cm / 1 |
| 15 | RockDesert | Gravel: 16 cm | Sandstone beds: 1.6 m | 187 m | 16 cm / 3 |
| 16 | DrySavanna | Straw fibres: 33 cm | Bedding: 1.8 m | 254 m | 17 cm / 2 |
| 17 | Savanna | Dry fibres: 22 cm | Fractures: 1.4 m | 168 m | 15 cm / 2 |
| 18 | DryJungle | Litter fibres: 27 cm | Fractures: 1.1 m | 104 m | 18 cm / 2 |
| 19 | TropicalJungle | Humus fibres: 16 cm | Damp folds: 65 cm | 76 m | 10.5 cm / 2 |
| 20 | VolcanicDesert | Ropy lava: 80 cm | Basalt fractures: 75 cm | 87 m | 13 cm / 3 |
| 21 | RedDesert | Sand ripples: 38 cm | Sandstone beds: 1.25 m | 307 m | 3.5 cm / 2 |
| 22 | AridPlateau | Shallow crust fractures: 65 cm | Wide bedding: 2.8 m | 274 m | 12 cm / 2 |
| 23 | BurningJungle | Cinder grain: 10 cm | Ropy cooled rock: 55 cm | 128 m | 10 cm / 2 |
| 24 | TropicalMangrove | Mud folds: 55 cm | Wet stone folds: 1.05 m | 55 m | 15 cm / 1 |
| 25 | Marine | Current ripples: 24 cm | Subdued bedding: 70 cm | 235 m | 5 cm / 2 |

The table's patch periods assume the game reference period of 96 m. The grain
family uses the fine period directly; its separate feature-period field is
unused. Other normal families combine the listed feature and fine scales. Normal
orientation uses the surface's existing direction control, independently for
ground and wall. Feature period and fine period are different controls: a broad
ice vein can remain polished while rough gravel contains several small scales.
Normal amplitude remains intentionally shallow; no large color noise is added
as a normal-height field. Three fine octaves are reserved for gravel/basalt;
most surfaces use two, and wet sediment uses one. Gains of 0.28–0.34 suppress
high-frequency slope accumulation. Pixel-footprint filtering removes unresolved
detail rather than displaying a noisy checker or shimmering distant surface.

## Revised palette contrast

The warm and temperate presets use darker **reflectance**, not reduced light
intensity or an exposure workaround. Across their four material midpoints,
linear luminance is now roughly 29–61% of the first material revision. Cold
biomes mostly retain their previous midpoint luminance, and polar snow keeps its
bright role. The boreal bog's formerly pale moss and cold marsh's bright crust were
deliberately subdued; their average material midpoint luminance is about 63%
and 80% of the first revision respectively.

Most low/mid/high ramps now stay within one hue family. Ground accents share
neighboring earth/vegetation hues, and warm wall accents use ochre, copper or
olive instead of a universal cream highlight. The mean low-to-high linear
luminance span is about 0.04–0.13 for warm biomes, down from about 0.25–0.31.
This preserves material variation while letting sunlight, fog and terrain
silhouette establish the large contrasts. No complementary-color rule is
applied mechanically; adjacent hues provide the intended harmony.

`BorealBog`, `ColdMarsh`, `TemperateRainforest`, `TropicalJungle` and
`TropicalMangrove` use a restrained satin response: roughness is generally 0.60–0.76 for the wet base and 0.73–0.88
for its drier accent. The stops vary gradually. Close roughness ranges avoid reinforcing the broad
material mask with abrupt wet/dry specular changes. For the bog, cold marsh and
mangrove, the final albedo pass also brings ground and wall accent midpoints to
roughly 1.2–1.4 times their corresponding base's linear luminance. Their narrowed
color ramps remove the pale islands while retaining the green/blue wet-ground
identity. Neither adjustment uses an exposure or baked-lighting correction.

Use the terrain material preview views and compare under direct light, shadow,
and the existing environment illumination. Roughness changes are easiest to
judge at grazing angles. A successful biome should remain readable at distance
after fine normal detail disappears.
