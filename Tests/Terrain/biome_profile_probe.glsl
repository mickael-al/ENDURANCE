// Test-only matrix and former parameter-blend diagnostics. Runtime evaluates
// independent density functions and never calls these helpers.

vec4 fieldProbeBlendRelief(vec2 p,vec2 climate)
{
    vec2 grid=clamp(climate,0.0,1.0)*4.0;
    uvec2 cell=min(uvec2(floor(grid)),uvec2(3u));
    vec2 f=fieldFade(grid-vec2(cell));
    uvec4 ids=fieldUnpackSurface(fieldBiomeCells[cell.x+4u*cell.y].x);
    return fieldSurfaceRecipes(p,ids,fieldQuadWeights(f),0.0);
}

// Frozen legacy parameters are only used for the former parameter-blend
// diagnostics. Production per-biome density functions are tested separately.
// Keep this type distinct so old shader snapshots can be compared as well.
struct ProbeBiomeVolume { vec4 volume; vec4 shape; vec4 style; };

ProbeBiomeVolume fieldBiomeVolume(uint biome)
{
    const ProbeBiomeVolume profiles[26]=ProbeBiomeVolume[26](
        ProbeBiomeVolume(vec4(-24,4,84,.65),vec4(vec3(.20,.70,.10),.09),vec4(.90,.45,.15,.20)),
        ProbeBiomeVolume(vec4(-20,3,72,.40),vec4(vec3(.35,.50,.15),.10),vec4(.85,.70,.15,.30)),
        ProbeBiomeVolume(vec4(-30,2,52,.55),vec4(vec3(.65,.15,.20),.075),vec4(.95,.10,.10,.10)),
        ProbeBiomeVolume(vec4(-30,6,90,.40),vec4(vec3(.35,.25,.40),.13),vec4(.80,.20,.30,.10)),
        ProbeBiomeVolume(vec4(-18,2,40,.65),vec4(vec3(.15,.70,.15),.09),vec4(.95,.00,.15,.30)),
        ProbeBiomeVolume(vec4(-36,10,110,.35),vec4(vec3(.50,.15,.35),.12),vec4(.75,.85,.50,.60)),
        ProbeBiomeVolume(vec4(-38,3,56,.60),vec4(vec3(.70,.10,.20),.08),vec4(.95,.20,.15,.10)),
        ProbeBiomeVolume(vec4(-36,7,92,.40),vec4(vec3(.45,.15,.40),.13),vec4(.80,.25,.35,.15)),
        ProbeBiomeVolume(vec4(-42,9,116,.40),vec4(vec3(.30,.40,.30),.15),vec4(.70,.20,.45,.40)),
        ProbeBiomeVolume(vec4(-20,2,44,.65),vec4(vec3(.20,.65,.15),.08),vec4(.90,.05,.10,.25)),
        ProbeBiomeVolume(vec4(-56,2,48,.70),vec4(vec3(.75,.10,.15),.07),vec4(1.00,.25,.10,.10)),
        ProbeBiomeVolume(vec4(-50,3,55,.65),vec4(vec3(.70,.10,.20),.075),vec4(.95,.05,.15,.05)),
        ProbeBiomeVolume(vec4(-34,9,108,.40),vec4(vec3(.35,.20,.45),.14),vec4(.80,.20,.40,.25)),
        ProbeBiomeVolume(vec4(-40,12,138,.35),vec4(vec3(.25,.25,.50),.17),vec4(.70,.15,.50,.25)),
        ProbeBiomeVolume(vec4(-24,3,58,.65),vec4(vec3(.25,.60,.15),.10),vec4(.85,.10,.20,.40)),
        ProbeBiomeVolume(vec4(-44,14,160,.30),vec4(vec3(.40,.40,.20),.19),vec4(.45,.65,.35,.80)),
        ProbeBiomeVolume(vec4(-42,6,95,.50),vec4(vec3(.60,.10,.30),.11),vec4(.85,.40,.25,.25)),
        ProbeBiomeVolume(vec4(-40,8,105,.45),vec4(vec3(.55,.15,.30),.12),vec4(.80,.20,.30,.15)),
        ProbeBiomeVolume(vec4(-42,12,148,.35),vec4(vec3(.25,.15,.60),.17),vec4(.65,.55,.40,.50)),
        ProbeBiomeVolume(vec4(-52,16,176,.30),vec4(vec3(.20,.15,.65),.20),vec4(.65,.15,.60,.15)),
        ProbeBiomeVolume(vec4(-68,18,180,.30),vec4(vec3(.30,.45,.25),.15),vec4(.95,.75,.65,.75)),
        ProbeBiomeVolume(vec4(-44,12,156,.30),vec4(vec3(.30,.50,.20),.18),vec4(.50,.50,.30,.90)),
        ProbeBiomeVolume(vec4(-56,10,168,.30),vec4(vec3(.15,.70,.15),.20),vec4(.35,.70,.25,1.00)),
        ProbeBiomeVolume(vec4(-64,18,180,.30),vec4(vec3(.25,.30,.45),.19),vec4(.75,.45,.70,.65)),
        ProbeBiomeVolume(vec4(-18,3,52,.70),vec4(vec3(.15,.75,.10),.10),vec4(.90,.10,.15,.35)),
        ProbeBiomeVolume(vec4(-44,12,148,.40),vec4(vec3(.35,.40,.25),.16),vec4(.60,.30,.45,.70)));
    return profiles[min(biome,25u)];
}

ProbeBiomeVolume fieldBiomeVolumeAt(uint biome, vec2 p)
{
    ProbeBiomeVolume result = fieldBiomeVolume(biome);
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

ProbeBiomeVolume fieldBlendVolume(vec3 biomeCoordinates,vec2 p)
{
    vec3 grid=clamp(biomeCoordinates,0.0,1.0)*4.0;
    uvec3 cell=min(uvec3(floor(grid)),uvec3(3u));
    vec3 f=fieldFade(grid-vec3(cell));
    uvec4 mapping=fieldBiomeCells[cell.x+4u*cell.y+16u*cell.z];
    FieldColumn column;
    column.terrain=vec4(0.0,biomeCoordinates.xy,0.0);
    column.blend=vec4(f.z,0,0,0);
    column.detail=uvec4(0u,mapping.yz,0u);
    FieldBiomeWeights weights=fieldVolumeWeights(column,f.xy);
    // This explicit coordinate probe is independent of the scene override.
    weights.low=fieldQuadWeights(f.xy)*(1.0-f.z);
    weights.high=fieldQuadWeights(f.xy)*f.z;
    ProbeBiomeVolume result;
    result.volume=vec4(0.0); result.shape=vec4(0.0); result.style=vec4(0.0);
    for(uint biome=0u;biome<FIELD_BIOME_COUNT;++biome)
    {
        float weight=fieldVolumeWeight(weights,biome);
        if(weight<=0.0) continue;
        ProbeBiomeVolume value=fieldBiomeVolumeAt(biome,p);
        result.volume+=value.volume*weight;
        result.shape+=value.shape*weight;
        result.style+=value.style*weight;
    }
    return result;
}
