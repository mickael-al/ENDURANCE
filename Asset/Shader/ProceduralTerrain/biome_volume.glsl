#ifndef ENDURANCE_BIOME_VOLUME_GLSL
#define ENDURANCE_BIOME_VOLUME_GLSL

// Every biome has its own volume recipe, paired with its 2D relief recipe.
// Recipes are blended once per XZ column. Their three noise fields have fixed
// world-space phases: interpolating a frequency and multiplying it by world
// position would distort biome boundaries increasingly far from the origin.
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

BiomeVolume fieldBiomeVolume(uint biome)
{
    if(biome==0u) return polarIceVolume();
    if(biome==1u) return frozenTundraVolume();
    if(biome==2u) return tundraVolume();
    if(biome==3u) return taigaVolume();
    if(biome==4u) return borealBogVolume();
    if(biome==5u) return rockyTundraVolume();
    if(biome==6u) return coldSteppeVolume();
    if(biome==7u) return borealForestVolume();
    if(biome==8u) return coldForestVolume();
    if(biome==9u) return coldMarshVolume();
    if(biome==10u) return dryPlainVolume();
    if(biome==11u) return meadowVolume();
    if(biome==12u) return temperateForestVolume();
    if(biome==13u) return temperateRainforestVolume();
    if(biome==14u) return temperateMarshVolume();
    if(biome==15u) return rockDesertVolume();
    if(biome==16u) return drySavannaVolume();
    if(biome==17u) return savannaVolume();
    if(biome==18u) return dryJungleVolume();
    if(biome==19u) return tropicalJungleVolume();
    if(biome==20u) return volcanicDesertVolume();
    if(biome==21u) return redDesertVolume();
    if(biome==22u) return aridPlateauVolume();
    if(biome==23u) return burningJungleVolume();
    if(biome==24u) return tropicalMangroveVolume();
    return marineVolume();
}

BiomeVolume fieldBiomeVolumeAt(uint biome, vec2 p)
{
    BiomeVolume result = fieldBiomeVolume(biome);
    if(biome==0u)
    {
        float activity = polarVolumeActivity(p);
        result.volume.yz *= activity;
        // Scaling carving strength alone does not remove its negative sign.
        // Also close the cavity radius smoothly towards inactive columns.
        result.shape.w *= activity;
    }
    return result;
}

#endif
