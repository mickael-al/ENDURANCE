#ifndef ENDURANCE_BIOME_VOLUME_GLSL
#define ENDURANCE_BIOME_VOLUME_GLSL

// Shared lazily within one voxel: recipes using this basis pay for three
// noises once, even at a boundary. A custom biome can ignore this cache and use
// fieldCellular3 or arbitrary other deterministic 3D functions instead.
struct FieldVolumeNoise { vec3 values; bool ready; };

vec3 fieldSharedVolumeNoise(vec3 p,inout FieldVolumeNoise cache)
{
    if(!cache.ready)
    {
        cache.values=vec3(fieldNoise3(p*0.0125,0x299f31d0u),
            fieldNoise3(p*vec3(0.00625,0.020,0.00625),0x082efa98u),
            fieldNoise3(p*vec3(0.018,0.005,0.018),0xec4e6c89u));
        cache.ready=true;
    }
    return cache.values;
}

// Density contract: positive solid, negative air, world metres of density.
// Return the base height field outside the finite band; keep the combined
// relative band offset within [-160,160]*uDetailScale for CPU culling bounds.
// Optional common construction; each biome supplies its parameters directly.
// volume: additional offset, displacement metres, carving scale metres, feather.
// shape: isotropic / horizontal / vertical noise weights, cave radius.
// style: tubes vs sheets, angular cross-section, roughness, strata.
float fieldVolumeFromRecipe(vec3 p,FieldColumn column,vec4 volume,vec4 shape,vec4 style,
                           inout FieldVolumeNoise cache)
{
    float base=column.terrain.x-p.y;
    float halfThickness=column.terrain.w*0.5;
    volume.yz*=uDetailScale;
    volume.y*=mix(0.55,1.0,column.blend.w);
    volume.z*=mix(0.85,1.0,column.blend.w);
    if(halfThickness<=0.0001 || volume.y+volume.z<=0.0) return base;
    float offset=column.blend.y+clamp(column.blend.z+volume.x,-160.0,160.0)*uDetailScale;
    float edgeDistance=halfThickness-abs(p.y-(column.terrain.x+offset));
    if(edgeDistance<=0.0) return base;
    float influence=smoothstep(0.0,max(0.0001,halfThickness*volume.w),edgeDistance);
    vec3 n=fieldSharedVolumeNoise(p,cache);
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


//float base = column.terrain.x - p.y;

float fieldVolumePolarIce(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p, column, vec4(-20, 3, 72, .40),
        vec4(.35, .50, .15, .10), vec4(.85, .70, .15, .30), cache);
}

float fieldVolumeFrozenTundra(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-20,3,72,.40),
        vec4(.35,.50,.15,.10),vec4(.85,.70,.15,.30),cache);
}

float fieldVolumeTundra(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-30,2,52,.55),
        vec4(.65,.15,.20,.075),vec4(.95,.10,.10,.10),cache);
}

float fieldVolumeTaiga(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-30,6,90,.40),
        vec4(.35,.25,.40,.13),vec4(.80,.20,.30,.10),cache);
}

float fieldVolumeBorealBog(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-18,2,40,.65),
        vec4(.15,.70,.15,.09),vec4(.95,.00,.15,.30),cache);
}

// Cold rock/forest: jointed masses, caves in valley walls and restrained sinks.
float fieldVolumeRockyTundra(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-36,10,110,.35),
        vec4(.50,.15,.35,.12),vec4(.75,.85,.50,.60),cache);
}

float fieldVolumeColdSteppe(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-38,3,56,.60),
        vec4(.70,.10,.20,.08),vec4(.95,.20,.15,.10),cache);
}

float fieldVolumeBorealForest(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-36,7,92,.40),
        vec4(.45,.15,.40,.13),vec4(.80,.25,.35,.15),cache);
}

float fieldVolumeColdForest(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-42,9,116,.40),
        vec4(.30,.40,.30,.15),vec4(.70,.20,.45,.40),cache);
}

float fieldVolumeColdMarsh(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-20,2,44,.65),
        vec4(.20,.65,.15,.08),vec4(.90,.05,.10,.25),cache);
}

// Open plains keep most volume below ground; wet forests expose stronger karst.
float fieldVolumeDryPlain(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-56,2,48,.70),
        vec4(.75,.10,.15,.07),vec4(1.00,.25,.10,.10),cache);
}

float fieldVolumeMeadow(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-50,3,55,.65),
        vec4(.70,.10,.20,.075),vec4(.95,.05,.15,.05),cache);
}

float fieldVolumeTemperateForest(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-34,9,108,.40),
        vec4(.35,.20,.45,.14),vec4(.80,.20,.40,.25),cache);
}

float fieldVolumeTemperateRainforest(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-40,12,138,.35),
        vec4(.25,.25,.50,.17),vec4(.70,.15,.50,.25),cache);
}

float fieldVolumeTemperateMarsh(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-24,3,58,.65),
        vec4(.25,.60,.15,.10),vec4(.85,.10,.20,.40),cache);
}

// Desert mesas/karst towers begin in the height map. Subtraction opens arches,
// overhangs and shelter recesses while leaving their large silhouettes readable.
float fieldVolumeRockDesert(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-44,14,160,.30),
        vec4(.40,.40,.20,.19),vec4(.45,.65,.35,.80),cache);
}

float fieldVolumeDrySavanna(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-42,6,95,.50),
        vec4(.60,.10,.30,.11),vec4(.85,.40,.25,.25),cache);
}

float fieldVolumeSavanna(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-40,8,105,.45),
        vec4(.55,.15,.30,.12),vec4(.80,.20,.30,.15),cache);
}

float fieldVolumeDryJungle(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-42,12,148,.35),
        vec4(.25,.15,.60,.17),vec4(.65,.55,.40,.50),cache);
}

float fieldVolumeTropicalJungle(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-52,16,176,.30),
        vec4(.20,.15,.65,.20),vec4(.65,.15,.60,.15),cache);
}

// Hot terrain: lava tubes, layered sandstone shelters and broad bank undercuts.
float fieldVolumeVolcanicDesert(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-68,18,180,.30),
        vec4(.30,.45,.25,.15),vec4(.95,.75,.65,.75),cache);
}

float fieldVolumeRedDesert(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-44,12,156,.30),
        vec4(.30,.50,.20,.18),vec4(.50,.50,.30,.90),cache);
}

float fieldVolumeAridPlateau(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-56,10,168,.30),
        vec4(.15,.70,.15,.20),vec4(.35,.70,.25,1.00),cache);
}

float fieldVolumeBurningJungle(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-64,18,180,.30),
        vec4(.25,.30,.45,.19),vec4(.75,.45,.70,.65),cache);
}

float fieldVolumeTropicalMangrove(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-18,3,52,.70),
        vec4(.15,.75,.10,.10),vec4(.90,.10,.15,.35),cache);
}

float fieldVolumeMarine(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,vec4(-44,12,148,.40),
        vec4(.35,.40,.25,.16),vec4(.60,.30,.45,.70),cache);
}

// One generic dispatcher keeps every biome independently editable. The
// caller visits each contributing ID once and shares its lazy noise cache.
float fieldBiomeVolume(uint biome,vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    switch(biome)
    {
        case 0u: return fieldVolumePolarIce(p,column,cache);
        case 1u: return fieldVolumeFrozenTundra(p,column,cache);
        case 2u: return fieldVolumeTundra(p,column,cache);
        case 3u: return fieldVolumeTaiga(p,column,cache);
        case 4u: return fieldVolumeBorealBog(p,column,cache);
        case 5u: return fieldVolumeRockyTundra(p,column,cache);
        case 6u: return fieldVolumeColdSteppe(p,column,cache);
        case 7u: return fieldVolumeBorealForest(p,column,cache);
        case 8u: return fieldVolumeColdForest(p,column,cache);
        case 9u: return fieldVolumeColdMarsh(p,column,cache);
        case 10u: return fieldVolumeDryPlain(p,column,cache);
        case 11u: return fieldVolumeMeadow(p,column,cache);
        case 12u: return fieldVolumeTemperateForest(p,column,cache);
        case 13u: return fieldVolumeTemperateRainforest(p,column,cache);
        case 14u: return fieldVolumeTemperateMarsh(p,column,cache);
        case 15u: return fieldVolumeRockDesert(p,column,cache);
        case 16u: return fieldVolumeDrySavanna(p,column,cache);
        case 17u: return fieldVolumeSavanna(p,column,cache);
        case 18u: return fieldVolumeDryJungle(p,column,cache);
        case 19u: return fieldVolumeTropicalJungle(p,column,cache);
        case 20u: return fieldVolumeVolcanicDesert(p,column,cache);
        case 21u: return fieldVolumeRedDesert(p,column,cache);
        case 22u: return fieldVolumeAridPlateau(p,column,cache);
        case 23u: return fieldVolumeBurningJungle(p,column,cache);
        case 24u: return fieldVolumeTropicalMangrove(p,column,cache);
        case 25u: return fieldVolumeMarine(p,column,cache);
        default: return column.terrain.x-p.y;
    }
}

#endif
