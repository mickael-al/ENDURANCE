// Raw recipe height before uDetailScale; mirror in ProceduralTerrainField.hpp.
// Height is stored as FP32. Narrow bands retain their original packed precision;
// a metadata flag selects the extended range only for deep volumetric sculptures.
#ifndef ENDURANCE_FIELD_LIMITS_GLSL
#define ENDURANCE_FIELD_LIMITS_GLSL
const float FIELD_MAX_RELIEF_HEIGHT=10240.0;
const float FIELD_LEGACY_VOLUME_THICKNESS=576.0;
const float FIELD_MAX_VOLUME_THICKNESS=8192.0;
#endif
