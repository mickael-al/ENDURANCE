#ifndef ENDURANCE_TERRAIN_RENDER_OPTIONS_GLSL
#define ENDURANCE_TERRAIN_RENDER_OPTIONS_GLSL

// CPU-selected render variants. These controls change on editor actions, never
// per fragment. The production path contains no diagnostic uniform branches.
// DEBUG_TERRAIN_LOD is defined only for the LOD inspection variant.
#ifndef DEBUG_TERRAIN_MATERIAL
#define DEBUG_TERRAIN_MATERIAL 0
#endif
#ifndef TERRAIN_FIXED_BIOME
#define TERRAIN_FIXED_BIOME -1
#endif
#ifndef TERRAIN_MICRODETAIL_ENABLED
#define TERRAIN_MICRODETAIL_ENABLED 1
#endif
#ifndef TERRAIN_FINE_SURFACE_ENABLED
#define TERRAIN_FINE_SURFACE_ENABLED 1
#endif

#if DEBUG_TERRAIN_MATERIAL < 0 || DEBUG_TERRAIN_MATERIAL > 6
#error Invalid terrain material inspection variant
#endif
#if TERRAIN_FIXED_BIOME < -1 || TERRAIN_FIXED_BIOME > 25
#error Invalid fixed terrain biome variant
#endif

#endif
