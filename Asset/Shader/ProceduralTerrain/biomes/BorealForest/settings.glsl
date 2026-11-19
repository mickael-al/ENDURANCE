#ifndef ENDURANCE_BOREAL_FOREST_SETTINGS_GLSL
#define ENDURANCE_BOREAL_FOREST_SETTINGS_GLSL

// World metres. Four independent normalized simplex fBm fields, five octaves.
const float BOREAL_MOUNTAIN_SCALE = 24000.0;
const int BOREAL_MOUNTAIN_OCTAVES = 5;
const float BOREAL_MOUNTAIN_THRESHOLD = 0.70;
// Raw relief units: each enabled layer adds 600 m at detailScale = .35.
const float BOREAL_LAYER_HEIGHT = 600.0 / 0.35;
const float BOREAL_MAX_MOUNTAIN_HEIGHT = 4.0 * BOREAL_LAYER_HEIGHT;

// World metres. Three Perlin 2D projections bend the 3D Voronoi domain.
const float BOREAL_CAVITY_SCALE = 1600.0;
const float BOREAL_CAVITY_WARP_SCALE = 600.0;
const float BOREAL_CAVITY_WARP_STRENGTH = 0.65;
// Surviving stone from the original ground to the top of the full layer stack.
const float BOREAL_SOLID_BASE = 0.80;
const float BOREAL_SOLID_TOP = 0.50;

#endif
