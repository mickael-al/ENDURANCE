#ifndef ENDURANCE_COLD_FOREST_PIT_SETTINGS_GLSL
#define ENDURANCE_COLD_FOREST_PIT_SETTINGS_GLSL

// Physical world metres, independent of uDetailScale.
const float COLD_FOREST_PIT_SPACING = 10000.0;
const float COLD_FOREST_PIT_RADIUS = 10000.0;
// Fraction of the maximum safe depth allowed by the current volume band.
const float COLD_FOREST_PIT_DEPTH_SCALE = 1.0;
const float COLD_FOREST_PIT_BAND_MARGIN = 96.0;
const float COLD_FOREST_PIT_RIM_WIDTH = 2000.0;
const float COLD_FOREST_PIT_SPIRAL_DEPTH = 110.0;
const float COLD_FOREST_PIT_SPIRAL_TURNS = 2.0;
const float COLD_FOREST_PORE_SCALE = 720.0;
// No stone inside the first 70% of the radius; fill the outer 30% gradually.
const float COLD_FOREST_PIT_EMPTY_RADIUS = 0.70;
const float COLD_FOREST_PIT_ANCHOR_RADIUS = 1100.0;
const float COLD_FOREST_PIT_RIVER_FADE = 500.0;

#endif
