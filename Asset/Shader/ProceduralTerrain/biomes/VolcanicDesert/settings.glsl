#ifndef ENDURANCE_VOLCANIC_DESERT_SETTINGS_GLSL
#define ENDURANCE_VOLCANIC_DESERT_SETTINGS_GLSL

// Horizontal world metres. Heights are metres at uDetailScale = 0.35.
// One randomly offset candidate per cell, including neighbouring cells.
// Keep 1.5 * maximum radius below the cell size for the 3x3 search bounds.
const float VOLCANIC_CELL_SIZE = 10000.0;
const vec2 VOLCANIC_RADIUS_RANGE = vec2(1800.0, 2800.0);
const vec2 VOLCANIC_HEIGHT_RANGE = vec2(900.0, 1700.0);
const vec2 VOLCANIC_CRATER_RADIUS_RANGE = vec2(0.20, 0.30);
const vec2 VOLCANIC_CRATER_FLOOR_RANGE = vec2(0.22, 0.40);
const float VOLCANIC_REFERENCE_SCALE = 0.35;

#endif
