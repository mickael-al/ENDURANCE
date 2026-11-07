// Temperature rows, humidity columns: exact ordering of the design matrix.
#ifndef ENDURANCE_BIOME_MATRIX_GLSL
#define ENDURANCE_BIOME_MATRIX_GLSL
const uint FIELD_BIOME_COUNT = 26u;
const uint FIELD_MARINE_BIOME = 25u;
const uint FIELD_BIOME_2D[25] = uint[25](
    0u,1u,2u,3u,4u,
    5u,6u,7u,8u,9u,
    10u,11u,12u,13u,14u,
    15u,16u,17u,18u,19u,
    20u,21u,22u,23u,24u
);

// Five altitude slices. The lowest slice is exclusively marine. Land slices
// keep the same climate identity, so the 2D and 3D recipes remain paired.
// Future altitude-specific variants can replace IDs here without changing ABI.
const uint FIELD_BIOME_3D[125] = uint[125](
    25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,25u,
    0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u,17u,18u,19u,20u,21u,22u,23u,24u,
    0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u,17u,18u,19u,20u,21u,22u,23u,24u,
    0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u,17u,18u,19u,20u,21u,22u,23u,24u,
    0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u,17u,18u,19u,20u,21u,22u,23u,24u
);
#endif
