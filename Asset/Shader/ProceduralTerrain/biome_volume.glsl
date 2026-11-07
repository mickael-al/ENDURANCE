#ifndef ENDURANCE_BIOME_VOLUME_GLSL
#define ENDURANCE_BIOME_VOLUME_GLSL

// Optional shared recipe helpers for the independent density functions below.
// These parameters are no longer blended to construct the production field.
// A biome may replace its density function with any bounded 3D construction.
struct BiomeVolume
{
    vec4 volume; // additional offset, displacement metres, carving scale metres, feather
    vec4 shape;  // isotropic / horizontal / vertical noise weights, cave radius
    vec4 style;  // tubes vs sheets, angular cross-section, roughness, strata
};

BiomeVolume volumeRecipe(float offset, float displacement, float carving, float feather,
                         vec3 weights, float radius, vec4 style)
{
    BiomeVolume result;
    result.volume = vec4(offset,displacement,carving,feather);
    result.shape = vec4(weights,radius);
    result.style = style;
    return result;
}

// Polar activity keeps only extremes of a slow 2D field. Evaluated once per
// column, before biome blending: quiet columns bypass all three volume noises.
// Raising START reduces occupied areas; END controls the soft activation ramp.
const float POLAR_VOLUME_SPACING = 224.0;
const float POLAR_VOLUME_START = 0.32;
const float POLAR_VOLUME_END = 0.52;

float polarVolumeActivity(vec2 p)
{
    float n = abs(fieldNoise2(p / max(POLAR_VOLUME_SPACING,4.0), 105u));
    float start = clamp(POLAR_VOLUME_START,0.0,0.999);
    float end = max(start+0.001,clamp(POLAR_VOLUME_END,0.0,1.0));
    float t = clamp((n-start)/(end-start),0.0,1.0);
    return t*t*(3.0-2.0*t);
}

// Ice: shallow recesses and small buried chambers in rare active sectors.
BiomeVolume polarIceVolume() { return volumeRecipe(-24,4,84,.65,vec3(.20,.70,.10),.09,vec4(.90,.45,.15,.20)); }
BiomeVolume frozenTundraVolume() { return volumeRecipe(-20,3,72,.40,vec3(.35,.50,.15),.10,vec4(.85,.70,.15,.30)); }
BiomeVolume tundraVolume() { return volumeRecipe(-30,2,52,.55,vec3(.65,.15,.20),.075,vec4(.95,.10,.10,.10)); }
BiomeVolume taigaVolume() { return volumeRecipe(-30,6,90,.40,vec3(.35,.25,.40),.13,vec4(.80,.20,.30,.10)); }
BiomeVolume borealBogVolume() { return volumeRecipe(-18,2,40,.65,vec3(.15,.70,.15),.09,vec4(.95,.00,.15,.30)); }

// Cold rock/forest: jointed masses, caves in valley walls and restrained sinks.
BiomeVolume rockyTundraVolume() { return volumeRecipe(-36,10,110,.35,vec3(.50,.15,.35),.12,vec4(.75,.85,.50,.60)); }
BiomeVolume coldSteppeVolume() { return volumeRecipe(-38,3,56,.60,vec3(.70,.10,.20),.08,vec4(.95,.20,.15,.10)); }
BiomeVolume borealForestVolume() { return volumeRecipe(-36,7,92,.40,vec3(.45,.15,.40),.13,vec4(.80,.25,.35,.15)); }
BiomeVolume coldForestVolume() { return volumeRecipe(-42,9,116,.40,vec3(.30,.40,.30),.15,vec4(.70,.20,.45,.40)); }
BiomeVolume coldMarshVolume() { return volumeRecipe(-20,2,44,.65,vec3(.20,.65,.15),.08,vec4(.90,.05,.10,.25)); }

// Open plains keep most volume below ground; wet forests expose stronger karst.
BiomeVolume dryPlainVolume() { return volumeRecipe(-56,2,48,.70,vec3(.75,.10,.15),.07,vec4(1.00,.25,.10,.10)); }
BiomeVolume meadowVolume() { return volumeRecipe(-50,3,55,.65,vec3(.70,.10,.20),.075,vec4(.95,.05,.15,.05)); }
BiomeVolume temperateForestVolume() { return volumeRecipe(-34,9,108,.40,vec3(.35,.20,.45),.14,vec4(.80,.20,.40,.25)); }
BiomeVolume temperateRainforestVolume() { return volumeRecipe(-40,12,138,.35,vec3(.25,.25,.50),.17,vec4(.70,.15,.50,.25)); }
BiomeVolume temperateMarshVolume() { return volumeRecipe(-24,3,58,.65,vec3(.25,.60,.15),.10,vec4(.85,.10,.20,.40)); }

// Desert mesas/karst towers begin in the height map. Subtraction opens arches,
// overhangs and shelter recesses while leaving their large silhouettes readable.
BiomeVolume rockDesertVolume() { return volumeRecipe(-44,14,160,.30,vec3(.40,.40,.20),.19,vec4(.45,.65,.35,.80)); }
BiomeVolume drySavannaVolume() { return volumeRecipe(-42,6,95,.50,vec3(.60,.10,.30),.11,vec4(.85,.40,.25,.25)); }
BiomeVolume savannaVolume() { return volumeRecipe(-40,8,105,.45,vec3(.55,.15,.30),.12,vec4(.80,.20,.30,.15)); }
BiomeVolume dryJungleVolume() { return volumeRecipe(-42,12,148,.35,vec3(.25,.15,.60),.17,vec4(.65,.55,.40,.50)); }
BiomeVolume tropicalJungleVolume() { return volumeRecipe(-52,16,176,.30,vec3(.20,.15,.65),.20,vec4(.65,.15,.60,.15)); }

// Hot terrain: lava tubes, layered sandstone shelters and broad bank undercuts.
BiomeVolume volcanicDesertVolume() { return volumeRecipe(-68,18,180,.30,vec3(.30,.45,.25),.15,vec4(.95,.75,.65,.75)); }
BiomeVolume redDesertVolume() { return volumeRecipe(-44,12,156,.30,vec3(.30,.50,.20),.18,vec4(.50,.50,.30,.90)); }
BiomeVolume aridPlateauVolume() { return volumeRecipe(-56,10,168,.30,vec3(.15,.70,.15),.20,vec4(.35,.70,.25,1.00)); }
BiomeVolume burningJungleVolume() { return volumeRecipe(-64,18,180,.30,vec3(.25,.30,.45),.19,vec4(.75,.45,.70,.65)); }
BiomeVolume tropicalMangroveVolume() { return volumeRecipe(-18,3,52,.70,vec3(.15,.75,.10),.10,vec4(.90,.10,.15,.35)); }
BiomeVolume marineVolume() { return volumeRecipe(-44,12,148,.40,vec3(.35,.40,.25),.16,vec4(.60,.30,.45,.70)); }

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
float fieldVolumeFromRecipe(vec3 p,FieldColumn column,BiomeVolume recipe,inout FieldVolumeNoise cache)
{
    float base=column.terrain.x-p.y;
    float halfThickness=column.terrain.w*0.5;
    recipe.volume.yz*=uDetailScale;
    recipe.volume.y*=mix(0.55,1.0,column.blend.w);
    recipe.volume.z*=mix(0.85,1.0,column.blend.w);
    if(halfThickness<=0.0001 || recipe.volume.y+recipe.volume.z<=0.0) return base;
    float offset=column.blend.y+clamp(column.blend.z+recipe.volume.x,-160.0,160.0)*uDetailScale;
    float edgeDistance=halfThickness-abs(p.y-(column.terrain.x+offset));
    if(edgeDistance<=0.0) return base;
    float influence=smoothstep(0.0,max(0.0001,halfThickness*recipe.volume.w),edgeDistance);
    vec3 n=fieldSharedVolumeNoise(p,cache);
    float a=dot(recipe.shape.xyz,n);
    float displacement=mix(a,0.6*a+0.4*n.x,recipe.style.z)*recipe.volume.y;
    float density=base+influence*displacement;
    if(recipe.volume.z<=0.0) return density;
    float b=0.75*n.y-0.65*n.z;
    vec2 d=abs(vec2(a,b));
    float tube=mix(length(d),(d.x+d.y)*0.75,clamp(recipe.style.y*2.0,0.0,1.0));
    tube=mix(tube,max(d.x,d.y),clamp(recipe.style.y*2.0-1.0,0.0,1.0));
    float cavity=mix(abs(a),tube,recipe.style.x);
    float layer=abs(2.0*fract(p.y*0.025+0.08*n.x)-1.0);
    float layerWidth=mix(0.55,1.35,smoothstep(0.2,0.8,layer));
    float radius=recipe.shape.w*mix(1.0,layerWidth,recipe.style.w);
    float cavityDensity=(cavity-radius)*recipe.volume.z;
    return mix(density,min(density,cavityDensity),influence);
}

// Independent editable functions, paired by ID with fieldRelief* functions.
// Each returns a complete scalar density. The caller blends density deltas
// against the common height field, and never evaluates a zero-weight biome.
float fieldVolumePolarIce(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    BiomeVolume recipe=polarIceVolume();
    float activity=uintBitsToFloat(column.detail.x);
    recipe.volume.yz*=activity;
    recipe.shape.w*=activity;
    return fieldVolumeFromRecipe(p,column,recipe,cache);
}

float fieldVolumeFrozenTundra(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,frozenTundraVolume(),cache);
}

float fieldVolumeTundra(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,tundraVolume(),cache);
}

float fieldVolumeTaiga(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,taigaVolume(),cache);
}

float fieldVolumeBorealBog(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,borealBogVolume(),cache);
}

float fieldVolumeRockyTundra(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,rockyTundraVolume(),cache);
}

float fieldVolumeColdSteppe(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,coldSteppeVolume(),cache);
}

float fieldVolumeBorealForest(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,borealForestVolume(),cache);
}

float fieldVolumeColdForest(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,coldForestVolume(),cache);
}

float fieldVolumeColdMarsh(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,coldMarshVolume(),cache);
}

float fieldVolumeDryPlain(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,dryPlainVolume(),cache);
}

float fieldVolumeMeadow(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,meadowVolume(),cache);
}

float fieldVolumeTemperateForest(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,temperateForestVolume(),cache);
}

float fieldVolumeTemperateRainforest(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,temperateRainforestVolume(),cache);
}

float fieldVolumeTemperateMarsh(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,temperateMarshVolume(),cache);
}

float fieldVolumeRockDesert(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,rockDesertVolume(),cache);
}

float fieldVolumeDrySavanna(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,drySavannaVolume(),cache);
}

float fieldVolumeSavanna(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,savannaVolume(),cache);
}

float fieldVolumeDryJungle(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,dryJungleVolume(),cache);
}

float fieldVolumeTropicalJungle(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,tropicalJungleVolume(),cache);
}

float fieldVolumeVolcanicDesert(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,volcanicDesertVolume(),cache);
}

float fieldVolumeRedDesert(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,redDesertVolume(),cache);
}

float fieldVolumeAridPlateau(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,aridPlateauVolume(),cache);
}

float fieldVolumeBurningJungle(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,burningJungleVolume(),cache);
}

float fieldVolumeTropicalMangrove(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,tropicalMangroveVolume(),cache);
}

float fieldVolumeMarine(vec3 p,FieldColumn column,inout FieldVolumeNoise cache)
{
    return fieldVolumeFromRecipe(p,column,marineVolume(),cache);
}

#endif
