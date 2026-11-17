// Cold forest: metre-based plan-view dimensions; existing ground relief
// amplitudes remain raw heights and therefore follow the scene detailScale.
#ifndef ENDURANCE_COLD_FOREST_GROUND_SETTINGS_GLSL
#define ENDURANCE_COLD_FOREST_GROUND_SETTINGS_GLSL

// Whole 20-km pits need large uninterrupted regions. A 24-km wavelength
// admitted no complete pits in 264196 sampled 50-km cells across four seeds.
const float COLD_FOREST_REGION_SCALE = 120000.0;
// Calibrated to ~30% dominant coverage (mask >= .5), preserving the .4-wide
// smooth transition. Reproduce with check_cold_forest_regions.py.
const float COLD_FOREST_REGION_LOW = 0.042469;
const float COLD_FOREST_REGION_HIGH = 0.442469;

const float COLD_FOREST_RIVER_SPACING = 50000.0;
const float COLD_FOREST_RIVER_FLOOR_HALF_WIDTH = 100.0;
const float COLD_FOREST_RIVER_BANK_HALF_WIDTH = 650.0;
const float COLD_FOREST_RIVER_MEANDER = 600.0;
const float COLD_FOREST_RIVER_MEANDER_SCALE = 12000.0;

#endif
