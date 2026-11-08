// Terrain signatures for the 5x5 temperature/humidity matrix, plus seafloor.
// Each recipe is evaluated once per XZ column, never once per density sample.
// X: relative height (m); Y: extra 3D band thickness (m);
// Z: biome band offset (m); W: landmark influence [0,1].
// The caller applies detailScale to XYZ and blends recipes continuously.
#ifndef ENDURANCE_BIOME_RELIEF_GLSL
#define ENDURANCE_BIOME_RELIEF_GLSL

float fieldSurfaceFbm(vec2 p, uint salt)
{
    // Three bounded octaves. Small-scale visual grain belongs in the material.
    return fieldNoise2(p, salt) * 0.57142857
        + fieldNoise2(p * 2.03 + vec2(19.7, -37.1), salt + 1u) * 0.28571429
        + fieldNoise2(p * 4.11 + vec2(-43.2, 11.8), salt + 2u) * 0.14285714;
}

vec2 fieldSurfaceRotate(vec2 p, vec2 direction)
{
    return vec2(dot(p, direction), dot(p, vec2(-direction.y, direction.x)));
}

float fieldSurfaceMound(float distanceToSite, float radius)
{
    return 1.0 - smoothstep(0.0, radius, distanceToSite);
}

float fieldSurfaceMesa(float distanceToSite, float radius, float crown)
{
    return 1.0 - smoothstep(radius * crown, radius, distanceToSite);
}

float fieldSurfaceLandmark(vec2 p, float spacing, uint salt, float metric,
                           float radius, float crown, float occupancy)
{
    vec2 site = fieldCellular2Data(p / spacing, salt, metric);
    // Site metadata costs no extra noise sample. A disabled site leaves an
    // uninterrupted floor instead of a small obstacle. Compact support ends
    // before the nearest-site boundary, so changing site IDs cannot make a seam.
    float enabled = step(1.0 - occupancy, site.y);
    float strength = 0.65 + 0.35 * clamp((site.y - (1.0 - occupancy)) / occupancy, 0.0, 1.0);
    // Different footprints and crowns keep rows of sites from becoming rows
    // of identical cylinders. The contour cannot expand beyond the original
    // radius: 0.96*r + 0.04*r = r, preserving its spacing bound and continuity.
    float radiusVariation = 0.72 + 0.24 * fract(site.y * 23.17);
    float crownVariation = clamp(crown * (0.65 + 0.65 * fract(site.y * 41.73)), 0.0, 0.85);
    float contour = fieldNoise2(p * (3.0 / spacing), salt ^ 0xa511e9b3u) * (0.04 * radius);
    return fieldSurfaceMesa(site.x + contour, radius * radiusVariation, crownVariation) * enabled * strength;
}

float fieldSurfaceTerrace(float height, float stepHeight, float softness)
{
    // Step size is expressed directly in metres, including negative heights.
    // The broad transition region keeps a usable ramp between level shelves.
    float cell = height / stepHeight;
    float halfWidth = softness * 0.5;
    return (floor(cell) + smoothstep(0.5 - halfWidth, 0.5 + halfWidth, fract(cell))) * stepHeight;
}

float fieldSurfaceDune(vec2 p, vec2 direction, float wavelength, uint salt)
{
    vec2 oriented = fieldSurfaceRotate(p, direction);
    // A slow phase warp bends a dune train without scattering unrelated bumps.
    float warp = fieldNoise2(p * (1.0 / (wavelength * 3.0)), salt) * 0.32;
    float phase = fract(oriented.x / wavelength + warp);
    // Long windward ramp, shorter lee slope, C1 continuity at each join.
    return phase < 0.76 ? smoothstep(0.0, 0.76, phase)
                        : 1.0 - smoothstep(0.76, 1.0, phase);
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

//x — hauteur	Ajoutée à la hauteur générale du terrain, après multiplication par detailScale.
//y — épaisseur	Ajoutée à bandThickness, après multiplication par detailScale.C’est une épaisseur totale, pas une demi - épaisseur.
//z — décalage	Déplace la bande 3D par rapport à la surface.Négatif = enfouissement, positif = déplacement vers le haut.Il s’ajoute aux décalages du profil 3D et des réglages globaux.
//w — masque rocheux	Valeur entre 0 et 1, initialement prévue pour renforcer la sculpture 3D sur les formations rocheuses. 0 ne désactive pas la 3D.

const float POLAR_BASIN_SPACING = 64.0;
const float POLAR_BASIN_DEPTH = 14.0;
const float POLAR_BASIN_FLOOR = 0.06;
const float POLAR_BASIN_WARP = 0.65;
const float POLAR_ROLL_AMPLITUDE = 18.0;
const float POLAR_FINE_AMPLITUDE = 1.5;

vec4 fieldReliefPolarIce(vec2 p)
{
    /*vec2 q = fieldSurfaceRotate(p, vec2(0.8, 0.6));
    float spacing = max(POLAR_BASIN_SPACING, 4.0);
    vec2 warp = vec2(fieldNoise2(q / (spacing * 3.0), 100u),
                     fieldNoise2(q / (spacing * 3.0), 101u));
    vec2 sites = fieldCellular2Distances(q / spacing + warp * POLAR_BASIN_WARP, 102u);
    // At a cell boundary F1 == F2. The remap has zero slope there, so the
    // depressed floors meet at rounded rims instead of sharp Voronoi ridges.
    float bowl = smoothstep(clamp(POLAR_BASIN_FLOOR,0.0,0.95),1.0,
                            clamp(sites.x / max(sites.y,0.0001),0.0,1.0));
    float h = (bowl - 1.0) * POLAR_BASIN_DEPTH;
    h += fieldNoise2(p * 0.0006, 103u) * POLAR_ROLL_AMPLITUDE;
    h += fieldNoise2(p * 0.04, 104u) * POLAR_FINE_AMPLITUDE;
    return vec4(h, 32.0, -40.0, 0.0);*/
    return vec4(0, 32, -40.0, 0.0);
}

vec4 fieldReliefFrozenTundra(vec2 p)
{
    float polygon = fieldCellular2(p / 96.0, 110u, 2.0);
    float raisedRim = smoothstep(0.24, 0.34, polygon) *
                      (1.0 - smoothstep(0.40, 0.48, polygon));
    float pingo = fieldSurfaceLandmark(p, 256.0, 111u, 0.0, 0.21, 0.0, 0.70);
    float h = fieldSurfaceFbm(p * 0.0015, 112u) * 38.0;
    h += raisedRim * 8.0 - smoothstep(0.1, 0.32, polygon) * 12.0 + pingo * 125.0;
    return vec4(h, 64.0, -36.0, pingo);
}

vec4 fieldReliefTundra(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.6, -0.8));
    float h = fieldSurfaceFbm(q * vec2(0.0011, 0.0025), 120u) * 95.0;
    float tor = fieldSurfaceLandmark(p, 384.0, 123u, 0.0, 0.20, 0.0, 0.60);
    h += tor * 150.0 + fieldNoise2(q * vec2(0.002, 0.006), 124u) * 10.0;
    return vec4(h, 72.0, -44.0, tor);
}

vec4 fieldReliefTaiga(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, -0.6));
    float valley = 1.0 - smoothstep(0.04, 0.26,
        abs(fieldNoise2(q * vec2(0.0006, 0.0020), 130u)));
    float esker = fieldRidge2(q * vec2(0.0014, 0.0050), 131u);
    esker *= esker;
    esker *= esker;
    float shield = fieldSurfaceLandmark(p, 384.0, 132u, 0.0, 0.22, 0.0, 0.68);
    float h = fieldNoise2(p * 0.0012, 133u) * 75.0 - valley * 72.0;
    h += esker * 35.0 + shield * 250.0;
    return vec4(h, 136.0, -68.0, shield);
}

vec4 fieldReliefBorealWetland(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.6, 0.8));
    float basin = fieldSurfaceFbm(p * 0.0018, 140u) * 24.0 - 28.0;
    float rib = fieldRidge2(q * vec2(0.0008, 0.007), 143u);
    float island = fieldSurfaceLandmark(p, 384.0, 144u, 0.0, 0.21, 0.0, 0.58);
    float h = fieldSurfaceTerrace(basin, 10.0, 0.80) + rib * rib * 15.0 + island * 145.0;
    return vec4(h, 80.0, -42.0, island);
}

// Cold row ------------------------------------------------------------------
vec4 fieldReliefRockyTundra(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, 0.6));
    float ridge = fieldRidge2(q * vec2(0.0011, 0.0022), 150u);
    float block = fieldSurfaceLandmark(p, 320.0, 151u, 2.0, 0.15, 0.42, 0.72);
    float h = fieldSurfaceTerrace((ridge * ridge - 0.45) * 170.0, 30.0, 0.68);
    h += block * (420.0 + 40.0 * fieldNoise2(p * 0.002, 152u));
    h += fieldNoise2(p * 0.006, 153u) * 10.0;
    return vec4(h, 200.0, -56.0, block);
}

vec4 fieldReliefColdSteppe(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.6, 0.8));
    float h = fieldSurfaceFbm(q * vec2(0.0009, 0.002), 160u) * 72.0;
    float mesa = fieldSurfaceLandmark(p, 512.0, 163u, 0.0, 0.21, 0.36, 0.50);
    h = mix(h, fieldSurfaceTerrace(h, 26.0, 0.85), 0.42);
    h += mesa * 190.0;
    return vec4(h, 88.0, -50.0, mesa);
}

vec4 fieldReliefBorealForest(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, -0.6));
    float drumlin = fieldNoise2(q * vec2(0.0010, 0.0041), 170u);
    float shield = fieldSurfaceLandmark(p, 384.0, 171u, 0.0, 0.22, 0.0, 0.65);
    float h = fieldSurfaceFbm(p * 0.0013, 172u) * 65.0 + drumlin * 32.0;
    h += shield * 270.0;
    return vec4(h, 170.0, -76.0, shield);
}

vec4 fieldReliefColdForest(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.6, 0.8));
    float valley = 1.0 - smoothstep(0.05, 0.28,
        abs(fieldNoise2(q * vec2(0.00055, 0.0017), 180u)));
    float buttress = fieldSurfaceLandmark(p, 384.0, 181u, 1.0, 0.22, 0.12, 0.68);
    float h = (1.0 - valley) * 145.0 - valley * 120.0;
    h += fieldNoise2(p * 0.0025, 182u) * 35.0 + buttress * 360.0;
    return vec4(h, 240.0, -96.0, buttress);
}

vec4 fieldReliefColdMarsh(vec2 p)
{
    float lake = fieldCellular2(p / 256.0, 190u, 0.0);
    float basin = 1.0 - smoothstep(0.12, 0.31, lake);
    float pingo = fieldSurfaceLandmark(p, 512.0, 191u, 0.0, 0.18, 0.0, 0.55);
    float h = fieldSurfaceFbm(p * 0.0014, 192u) * 18.0 - basin * 42.0;
    h += pingo * 110.0;
    return vec4(h, 72.0, -40.0, pingo);
}

// Temperate row -------------------------------------------------------------
vec4 fieldReliefDryPlain(vec2 p)
{
    float h = fieldNoise2(p * 0.0009, 200u) * 44.0 + fieldNoise2(p * 0.003, 201u) * 10.0;
    float butte = fieldSurfaceLandmark(p, 512.0, 202u, 0.0, 0.20, 0.45, 0.52);
    h += butte * (360.0 + fieldNoise2(p * 0.0011, 203u) * 65.0);
    return vec4(h, 136.0, -60.0, butte);
}

vec4 fieldReliefPrairie(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, 0.6));
    float waves = fieldSurfaceFbm(q * vec2(0.0007, 0.0015), 210u) * 96.0;
    float knoll = fieldSurfaceLandmark(p, 512.0, 213u, 0.0, 0.22, 0.0, 0.55);
    float h = waves + knoll * 125.0;
    h += fieldNoise2(q * vec2(0.0012, 0.004), 214u) * 10.0;
    return vec4(h, 96.0, -52.0, knoll);
}

vec4 fieldReliefTemperateForest(vec2 p)
{
    float hills = fieldSurfaceFbm(p * 0.0016, 220u) * 115.0;
    float tor = fieldSurfaceLandmark(p, 384.0, 223u, 0.0, 0.22, 0.0, 0.65);
    float h = hills + tor * (270.0 + 45.0 * fieldNoise2(p * 0.001, 224u));
    return vec4(h, 220.0, -80.0, tor);
}

vec4 fieldReliefHumidTemperateForest(vec2 p)
{
    float valley = fieldSurfaceFbm(p * 0.0014, 230u) * 62.0;
    float tower = fieldSurfaceLandmark(p, 256.0, 233u, 0.0, 0.15, 0.32, 0.75);
    float benches = fieldSurfaceTerrace(valley, 30.0, 0.65);
    float h = mix(valley, benches, 0.55) + tower * (480.0 + 55.0 * fieldNoise2(p * 0.001, 234u));
    return vec4(h, 300.0, -110.0, tower);
}

vec4 fieldReliefTemperateMarsh(vec2 p)
{
    float channels = fieldNoise2(p * vec2(0.0025, 0.0045), 240u);
    float h = fieldSurfaceTerrace(fieldNoise2(p * 0.0011, 241u) * 18.0, 7.0, 0.86);
    h -= (1.0 - smoothstep(0.025, 0.13, abs(channels))) * 20.0;
    float island = fieldSurfaceLandmark(p, 384.0, 242u, 0.0, 0.22, 0.0, 0.60);
    h += island * 80.0;
    return vec4(h, 64.0, -38.0, island);
}

// Warm row ------------------------------------------------------------------
vec4 fieldReliefRockDesert(vec2 p)
{
    float floorWaves = fieldSurfaceDune(p, vec2(0.8, 0.6), 420.0, 250u) * 62.0;
    float mesa = fieldSurfaceLandmark(p, 640.0, 251u, 0.0, 0.22, 0.58, 0.64);
    float layers = fieldSurfaceTerrace(mesa * 570.0, 54.0, 0.42);
    float h = floorWaves + layers + fieldNoise2(p * 0.002, 252u) * 8.0;
    return vec4(h, 300.0, -90.0, mesa);
}

vec4 fieldReliefDrySavanna(vec2 p)
{
    float pediment = fieldSurfaceFbm(p * 0.0011, 260u) * 60.0;
    float inselberg = fieldSurfaceLandmark(p, 512.0, 263u, 0.0, 0.22, 0.0, 0.55);
    float h = mix(pediment, fieldSurfaceTerrace(pediment, 32.0, 0.80), 0.35);
    h += inselberg * (470.0 + 55.0 * fieldNoise2(p * 0.001, 264u));
    return vec4(h, 180.0, -72.0, inselberg);
}

vec4 fieldReliefSavanna(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.6, -0.8));
    float floorWaves = fieldSurfaceFbm(q * vec2(0.0009, 0.0016), 270u) * 85.0;
    float kopje = fieldSurfaceLandmark(p, 384.0, 273u, 0.0, 0.20, 0.0, 0.60);
    float h = floorWaves + kopje * (330.0 + 45.0 * fieldNoise2(p * 0.0015, 274u));
    return vec4(h, 184.0, -74.0, kopje);
}

vec4 fieldReliefDryJungle(vec2 p)
{
    float floorWaves = fieldSurfaceFbm(p * 0.0013, 280u) * 55.0;
    float tower = fieldSurfaceLandmark(p, 128.0, 283u, 1.0, 0.22, 0.18, 0.88);
    float h = fieldSurfaceTerrace(floorWaves, 22.0, 0.78);
    h += tower * (480.0 + 75.0 * fieldNoise2(p * 0.002, 284u));
    return vec4(h, 300.0, -100.0, tower);
}

vec4 fieldReliefTropicalJungle(vec2 p)
{
    float floorWaves = fieldSurfaceFbm(p * 0.0010, 290u) * 65.0;
    float tower = fieldSurfaceLandmark(p, 128.0, 293u, 0.0, 0.22, 0.36, 0.90);
    float h = floorWaves + tower * (560.0 + 65.0 * fieldNoise2(p * 0.0017, 294u));
    return vec4(h, 320.0, -112.0, tower);
}

// Hottest row ---------------------------------------------------------------
vec4 fieldReliefVolcanicDesert(vec2 p)
{
    float d = fieldCellular2(p / 768.0, 300u, 0.0);
    float shield = fieldSurfaceMound(d, 0.43);
    float caldera = 1.0 - smoothstep(0.055, 0.12, d);
    float flow = fieldNoise2(p * vec2(0.0015, 0.005), 301u) * 30.0;
    float basalt = fieldSurfaceLandmark(p, 512.0, 302u, 2.0, 0.14, 0.55, 0.60);
    float h = fieldSurfaceTerrace(shield * 460.0 - caldera * 240.0 + flow, 28.0, 0.70);
    h += basalt * 110.0;
    return vec4(h, 320.0, -120.0, max(shield, basalt));
}

vec4 fieldReliefRedDesert(vec2 p)
{
    float dune = fieldSurfaceDune(p, vec2(0.6, 0.8), 360.0, 310u);
    float secondary = fieldNoise2(p * vec2(0.0017, 0.0041), 311u);
    float monument = fieldSurfaceLandmark(p, 768.0, 312u, 1.0, 0.22, 0.34, 0.52);
    float h = dune * 150.0 + secondary * 13.0;
    h += fieldSurfaceTerrace(monument * 440.0, 48.0, 0.40);
    return vec4(h, 230.0, -78.0, monument);
}

vec4 fieldReliefAridPlateau(vec2 p)
{
    float mesa = fieldSurfaceLandmark(p, 512.0, 320u, 2.0, 0.15, 0.62, 0.68);
    float floorWaves = fieldSurfaceFbm(p * 0.0010, 321u) * 45.0;
    float h = floorWaves + fieldSurfaceTerrace(mesa * 540.0, 60.0, 0.32);
    return vec4(h, 280.0, -90.0, mesa);
}

vec4 fieldReliefBurningJungle(vec2 p)
{
    vec2 q = fieldSurfaceRotate(p, vec2(0.8, -0.6));
    float flow = fieldSurfaceFbm(q * vec2(0.0008, 0.0028), 330u) * 76.0;
    float basalt = fieldSurfaceLandmark(p, 128.0, 333u, 2.0, 0.15, 0.40, 0.82);
    float h = fieldSurfaceTerrace(flow, 32.0, 0.62);
    h += basalt * (520.0 + 55.0 * fieldNoise2(p * 0.0015, 334u));
    return vec4(h, 320.0, -108.0, basalt);
}

vec4 fieldReliefTropicalMangrove(vec2 p)
{
    float tide = fieldNoise2(p * 0.0011, 340u) * 20.0;
    float channel = 1.0 - smoothstep(0.025, 0.12,
        abs(fieldNoise2(p * vec2(0.0024, 0.0046), 341u)));
    float island = fieldSurfaceLandmark(p, 384.0, 342u, 0.0, 0.21, 0.0, 0.55);
    float h = fieldSurfaceTerrace(tide, 6.0, 0.88) - channel * 25.0 + island * 100.0;
    return vec4(h, 104.0, -52.0, island);
}

vec4 fieldReliefMarine(vec2 p)
{
    float shelf = fieldSurfaceTerrace(fieldNoise2(p * 0.0007, 350u) * 85.0, 42.0, 0.80);
    float canyon = 1.0 - smoothstep(0.035, 0.20,
        abs(fieldNoise2(p * vec2(0.0005, 0.0017), 351u)));
    float seamount = fieldSurfaceLandmark(p, 768.0, 352u, 0.0, 0.22, 0.0, 0.50);
    float h = -235.0 + shelf - canyon * 145.0 + seamount * 275.0;
    return vec4(h, 280.0, -96.0, seamount);
}

vec4 fieldBiomeRelief(uint biome, vec2 p)
{
    vec4 result;
    switch (biome)
    {
        case 0u: result = fieldReliefPolarIce(p); break;
        case 1u: result = fieldReliefFrozenTundra(p); break;
        case 2u: result = fieldReliefTundra(p); break;
        case 3u: result = fieldReliefTaiga(p); break;
        case 4u: result = fieldReliefBorealWetland(p); break;
        case 5u: result = fieldReliefRockyTundra(p); break;
        case 6u: result = fieldReliefColdSteppe(p); break;
        case 7u: result = fieldReliefBorealForest(p); break;
        case 8u: result = fieldReliefColdForest(p); break;
        case 9u: result = fieldReliefColdMarsh(p); break;
        case 10u: result = fieldReliefDryPlain(p); break;
        case 11u: result = fieldReliefPrairie(p); break;
        case 12u: result = fieldReliefTemperateForest(p); break;
        case 13u: result = fieldReliefHumidTemperateForest(p); break;
        case 14u: result = fieldReliefTemperateMarsh(p); break;
        case 15u: result = fieldReliefRockDesert(p); break;
        case 16u: result = fieldReliefDrySavanna(p); break;
        case 17u: result = fieldReliefSavanna(p); break;
        case 18u: result = fieldReliefDryJungle(p); break;
        case 19u: result = fieldReliefTropicalJungle(p); break;
        case 20u: result = fieldReliefVolcanicDesert(p); break;
        case 21u: result = fieldReliefRedDesert(p); break;
        case 22u: result = fieldReliefAridPlateau(p); break;
        case 23u: result = fieldReliefBurningJungle(p); break;
        case 24u: result = fieldReliefTropicalMangrove(p); break;
        default: result = fieldReliefMarine(p); break;
    }
    return clamp(result, vec4(-640.0, 0.0, -160.0, 0.0),
                         vec4(640.0, 320.0, 160.0, 1.0));
}

#endif
