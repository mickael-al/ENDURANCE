#ifndef ENDURANCE_STEPPE_TORUS_SETTINGS_GLSL
#define ENDURANCE_STEPPE_TORUS_SETTINGS_GLSL

// World metres, independent of uDetailScale. Major radius is measured to the
// centre of the tube: outside diameter 2100 m, central opening 1900 m.
const float STEPPE_TORUS_RADIUS = 1000.0;
const float STEPPE_TORUS_TUBE_RADIUS = 50.0;
// Fraction of the tube's vertical diameter below the local terrain surface.
const float STEPPE_TORUS_BURIED_FRACTION = 0.25;
// One candidate per 10 x 10 km cell, BEFORE excluding mountain regions.
const float STEPPE_TORUS_SPACING = 10000.0;
const float STEPPE_TORUS_PORE_SCALE = 160.0;

#endif
