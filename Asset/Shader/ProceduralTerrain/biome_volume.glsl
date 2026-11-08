#ifndef ENDURANCE_BIOME_VOLUME_GLSL
#define ENDURANCE_BIOME_VOLUME_GLSL

// One row per biome ID, three vec4s / twelve float32 controls per row.
// [0] volume: additional band offset (m), displacement (m), carving scale (m), feather.
// [1] shape: isotropic / horizontal / vertical noise weights, cave radius.
// [2] style: tubes vs sheets, angular cross-section, roughness, strata.
// Offsets/amplitudes use uDetailScale; noise frequencies remain world-space.
// Parameters are blended locally BEFORE the one common density evaluation.
const vec4 FIELD_BIOME_VOLUME[26][3]=vec4[26][3](
    // 0: PolarIce
    vec4[3](vec4(-20,3,72,.40), vec4(.35,.50,.15,.10), vec4(.85,.70,.15,.30)),
    // 1: FrozenTundra
    vec4[3](vec4(-20,3,72,.40), vec4(.35,.50,.15,.10), vec4(.85,.70,.15,.30)),
    // 2: Tundra
    vec4[3](vec4(-30,2,52,.55), vec4(.65,.15,.20,.075), vec4(.95,.10,.10,.10)),
    // 3: Taiga
    vec4[3](vec4(-30,6,90,.40), vec4(.35,.25,.40,.13), vec4(.80,.20,.30,.10)),
    // 4: BorealBog
    vec4[3](vec4(-18,2,40,.65), vec4(.15,.70,.15,.09), vec4(.95,.00,.15,.30)),
    // 5: RockyTundra
    vec4[3](vec4(-36,10,110,.35), vec4(.50,.15,.35,.12), vec4(.75,.85,.50,.60)),
    // 6: ColdSteppe
    vec4[3](vec4(-38,3,56,.60), vec4(.70,.10,.20,.08), vec4(.95,.20,.15,.10)),
    // 7: BorealForest
    vec4[3](vec4(-36,7,92,.40), vec4(.45,.15,.40,.13), vec4(.80,.25,.35,.15)),
    // 8: ColdForest
    vec4[3](vec4(-42,9,116,.40), vec4(.30,.40,.30,.15), vec4(.70,.20,.45,.40)),
    // 9: ColdMarsh
    vec4[3](vec4(-20,2,44,.65), vec4(.20,.65,.15,.08), vec4(.90,.05,.10,.25)),
    // 10: DryPlain
    vec4[3](vec4(-56,2,48,.70), vec4(.75,.10,.15,.07), vec4(1.00,.25,.10,.10)),
    // 11: Meadow
    vec4[3](vec4(-50,3,55,.65), vec4(.70,.10,.20,.075), vec4(.95,.05,.15,.05)),
    // 12: TemperateForest
    vec4[3](vec4(-34,9,108,.40), vec4(.35,.20,.45,.14), vec4(.80,.20,.40,.25)),
    // 13: TemperateRainforest
    vec4[3](vec4(-40,12,138,.35), vec4(.25,.25,.50,.17), vec4(.70,.15,.50,.25)),
    // 14: TemperateMarsh
    vec4[3](vec4(-24,3,58,.65), vec4(.25,.60,.15,.10), vec4(.85,.10,.20,.40)),
    // 15: RockDesert
    vec4[3](vec4(-44,14,160,.30), vec4(.40,.40,.20,.19), vec4(.45,.65,.35,.80)),
    // 16: DrySavanna
    vec4[3](vec4(-42,6,95,.50), vec4(.60,.10,.30,.11), vec4(.85,.40,.25,.25)),
    // 17: Savanna
    vec4[3](vec4(-40,8,105,.45), vec4(.55,.15,.30,.12), vec4(.80,.20,.30,.15)),
    // 18: DryJungle
    vec4[3](vec4(-42,12,148,.35), vec4(.25,.15,.60,.17), vec4(.65,.55,.40,.50)),
    // 19: TropicalJungle
    vec4[3](vec4(-52,16,176,.30), vec4(.20,.15,.65,.20), vec4(.65,.15,.60,.15)),
    // 20: VolcanicDesert
    vec4[3](vec4(-68,18,180,.30), vec4(.30,.45,.25,.15), vec4(.95,.75,.65,.75)),
    // 21: RedDesert
    vec4[3](vec4(-44,12,156,.30), vec4(.30,.50,.20,.18), vec4(.50,.50,.30,.90)),
    // 22: AridPlateau
    vec4[3](vec4(-56,10,168,.30), vec4(.15,.70,.15,.20), vec4(.35,.70,.25,1.00)),
    // 23: BurningJungle
    vec4[3](vec4(-64,18,180,.30), vec4(.25,.30,.45,.19), vec4(.75,.45,.70,.65)),
    // 24: TropicalMangrove
    vec4[3](vec4(-18,3,52,.70), vec4(.15,.75,.10,.10), vec4(.90,.10,.15,.35)),
    // 25: Marine
    vec4[3](vec4(-44,12,148,.40), vec4(.35,.40,.25,.16), vec4(.60,.30,.45,.70))
);

// Prepared controls: terrain=(surface height, half band thickness),
// volume=(absolute band center, scaled displacement, scaled carving, feather distance).
// Preparation runs once per shared XZ column. Only this function runs per voxel.
// Positive density is solid, negative density is air; all distances are metres.
float fieldDensityAtPrepared(vec3 p,vec2 terrain,vec4 volume,vec4 shape,vec4 style)
{
    if(uFieldMode==1u) return uBaseHeight-p.y;
    if(uFieldMode==2u) return uSphereRadius-length(p-uSphereCenter);
    float base=terrain.x-p.y;
    if(terrain.y<=0.0001 || volume.y+volume.z<=0.0) return base;
    float edgeDistance=terrain.y-abs(p.y-volume.x);
    if(edgeDistance<=0.0) return base;
    float influence=smoothstep(0.0,volume.w,edgeDistance);
    vec3 n=vec3(fieldNoise3(p*0.0125,0x299f31d0u),
        fieldNoise3(p*vec3(0.00625,0.020,0.00625),0x082efa98u),
        fieldNoise3(p*vec3(0.018,0.005,0.018),0xec4e6c89u));
    float a=dot(shape.xyz,n);
    float displacement=mix(a,0.6*a+0.4*n.x,style.z)*volume.y;
    float density=base+influence*displacement;
    if(volume.z<=0.0) return density;
    float b=0.75*n.y-0.65*n.z;
    vec2 d=abs(vec2(a,b));
    float tube=mix(length(d),(d.x+d.y)*0.75,clamp(style.y*2.0,0.0,1.0));
    tube=mix(tube,max(d.x,d.y),clamp(style.y*2.0-1.0,0.0,1.0));
    float cavity=mix(abs(a),tube,style.x);
    float layer=abs(2.0*fract(p.y*0.025+0.08*n.x)-1.0);
    float layerWidth=mix(0.55,1.35,smoothstep(0.2,0.8,layer));
    float radius=shape.w*mix(1.0,layerWidth,style.w);
    float cavityDensity=(cavity-radius)*volume.z;
    return mix(density,min(density,cavityDensity),influence);
}

#endif
