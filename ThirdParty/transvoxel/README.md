# Transvoxel lookup tables

`Transvoxel.cpp` and `LICENSE` are copied from Eric Lengyel's official
[Transvoxel repository](https://github.com/EricLengyel/Transvoxel).
The source text is unchanged except for normalized LF line endings.

Retrieved November 7, 2026. Git blob identifiers:

- `Transvoxel.cpp`: `2c4ceb79a513e368dbf24ea2650947044f089d7c`
- `LICENSE`: `791c429c79b58efb0379353c78881f26114b39a4`

The algorithm and its derivation are described at
[transvoxel.org](https://transvoxel.org/). The tables are MIT licensed;
the complete license is retained next to the data.

`ProceduralTransvoxelTables.hpp` expands both regular and transition cases
into a 25 KiB GPU table. The expansion retains the official regular-cell
triangulation, which is needed for the transition topology to match.
It removes CPU-style vertex reuse codes and class indirections, and resolves
transition winding once during upload. No field or mesh data is generated on
the CPU by this conversion.

The GPU face frames, tangent projection and boundary-vertex rule were
cross-checked against the MIT-licensed
[Godot Voxel Transvoxel implementation](https://github.com/Zylann/godot_voxel/blob/e578eb4521c09c4673502def80b804d2d688676e/meshers/transvoxel/transvoxel.cpp)
and its
[transition shader](https://github.com/Zylann/godot_voxel/blob/e578eb4521c09c4673502def80b804d2d688676e/shaders/dev/transvoxel_minimal.gdshader).
`GODOT_VOXEL_LICENSE` retains that reference implementation's license.
The compute kernels, shared-memory tiling and exact coarse-coordinate vertex
packing are implemented for ENDURANCE's GPU pipeline.
