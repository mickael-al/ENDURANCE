#include "EnduranceTerrainMaterials.hpp"
#include <array>
#include <cmath>
#include <cstdint>

namespace Endurance
{
namespace
{
    using namespace ProceduralTerrain;
    using V = TerrainMaterialVector;

    // Hex swatches are authored in display sRGB, then converted ONCE to linear
    // reflectance. The shader's final G-buffer encoding and the deferred decode
    // cancel, so these linear swatches are not decoded a second time.
    float linearChannel(std::uint32_t channel)
    {
        const float s = static_cast<float>(channel) / 255.0f;
        return s <= 0.04045f ? s / 12.92f : std::pow((s + 0.055f) / 1.055f, 2.4f);
    }
    V swatch(std::uint32_t rgb, float roughness)
    {
        return {{linearChannel((rgb >> 16u) & 255u), linearChannel((rgb >> 8u) & 255u),
                 linearChannel(rgb & 255u), roughness}};
    }
    TerrainMaterial material(std::uint32_t low, std::uint32_t mid, std::uint32_t high,
        V roughNormal, V pattern, float softness = 0.90f, float contrast = 1.20f,
        float roughVariation = 0.04f)
    {
        TerrainMaterial m;
        m.colorLow = swatch(low, roughNormal[0]);
        m.colorMid = swatch(mid, roughNormal[1]);
        m.colorHigh = swatch(high, roughNormal[2]);
        m.response = {{0.0f, 1.0f, roughNormal[3], roughVariation}};
        m.ramp = {{0.5f, softness, contrast, 0.0f}};
        m.pattern = pattern;
        return m;
    }
    TerrainSurface surface(TerrainMaterial base, TerrainMaterial accent, V blend, V detail)
    {
        TerrainSurface s;
        s.base = base; s.accent = accent; s.blend = blend; s.detail = detail;
        return s;
    }
    TerrainBiomeAppearance biome(TerrainSurface ground, TerrainSurface wall,
        V slope = {{0.28f, 0.65f, 0.07f, 1.0f}})
    {
        TerrainBiomeAppearance b;
        b.ground = ground; b.wall = wall; b.slope = slope;
        return b;
    }
    struct Recipe
    {
        const char* intent;
        std::array<const char*, 4> labels;
        TerrainBiomeAppearance appearance;
    };

    const std::array<Recipe, BiomeCount>& recipes()
    {
        // Each ID has four independently editable materials, each with three
        // explicit swatches. Similar neighboring climates share an art direction,
        // never a single global gray wall material. No sunlight is baked in.
        static const std::array<Recipe, BiomeCount> data{{
            // 0 PolarIce: luminous snow, deep blue exposed ice; sparse satin faces.
            {"Neige ivoire froide, glace bleue affleurante et grandes veines turquoise.",
             {{"Neige soufflee", "Glace affleurante", "Glace profonde", "Veines de givre"}},
             biome(
                surface(material(0xBCCCD0,0xD4DDDA,0xEBEDE1, {{.87f,.89f,.92f,.55f}}, {{0.8f,0.f,0.f,0.2f}}, .95f,1.15f,.025f),
                        material(0x527F91,0x81A7B0,0xB6D0D2, {{.27f,.34f,.42f,.30f}}, {{0.85f,0.1f,0.05f,0.f}}, .95f,1.167f,.015f),
                        {{0.7f,0.18f,0.97f,0.03f}}, {{48.f,2.6f,0.38f,1.1f}}),
                surface(material(0x3F627A,0x628697,0x90AEB7, {{.30f,.37f,.47f,.55f}}, {{0.65f,0.15f,0.2f,0.f}}, 0.8f,1.237f,.02f),
                        material(0x91BBC2,0xB5CFD0,0xD9E3DC, {{.54f,.64f,.77f,.45f}}, {{0.6f,0.1f,0.3f,0.f}}, .5f,1.272f,.02f),
                        {{0.705f,0.16f,0.6f,0.4f}}, {{115.f,3.f,0.22f,55.2f}}), {{.20f,.58f,.09f,.9f}})},
            // 1 FrozenTundra: cold mineral massifs, warm glacial sediment in gaps.
            {"Neige mince et sediments taupe sous des massifs ardoise givres.",
             {{"Sediment gele", "Neige mince", "Schiste froid", "Givre mineral"}},
             biome(
                surface(material(0x767F7E,0x989D91,0xB9B7A2, {{.84f,.88f,.91f,.8f}}, {{0.75f,0.25f,0.f,0.f}}),
                        material(0xB3C3C5,0xCCD4CD,0xE0E2D3, {{.87f,.91f,.94f,.5f}}, {{0.9f,0.1f,0.f,0.f}}, .95f),
                        {{0.63f,0.21f,0.87f,0.13f}}, {{28.f,1.5f,1.05f,3.f}}),
                surface(material(0x4D606B,0x71818A,0x96A09C, {{.78f,.85f,.91f,1.05f}}, {{0.68f,0.2f,0.12f,0.f}}, 0.7f,1.255f),
                        material(0x9CAEB4,0xBCC9C8,0xD9DED1, {{.75f,.84f,.9f,.65f}}, {{0.75f,0.1f,0.15f,0.f}}, .8f),
                        {{0.665f,0.19f,0.8f,0.2f}}, {{68.f,1.5f,0.8f,27.2f}}), {{.22f,.61f,.1f,1.f}})},
            // 2 Tundra: olive lichen and ochre humus, violet-gray mineral cliffs.
            {"Lichen olive et humus ocre, roches gris violace sans saturation excessive.",
             {{"Humus sec", "Lichen", "Roche de toundra", "Oxydation claire"}},
             biome(
                surface(material(0x665C46,0x8A7E59,0xAEA078, {{.94f,.92f,.9f,.85f}}, {{0.72f,0.28f,0.f,0.f}}),
                        material(0x53604A,0x79825B,0xA1A478, {{.94f,.96f,.97f,.65f}}, {{0.55f,0.45f,0.f,0.f}}, .7f,1.272f),
                        {{0.55f,0.18f,0.67f,0.33f}}, {{18.f,1.2f,0.65f,2.f}}),
                surface(material(0x53545C,0x777878,0x9C9B8E, {{.82f,.88f,.93f,1.15f}}, {{0.64f,0.2f,0.16f,0.f}}, 0.7f,1.237f),
                        material(0x847455,0xA69875,0xC3B99A, {{.86f,.91f,.94f,.9f}}, {{0.5f,0.25f,0.25f,0.f}}, .55f),
                        {{0.685f,0.17f,0.65f,0.35f}}, {{54.f,1.4f,-0.5f,21.6f}}))},
            // 3 Taiga: broad dark-green floor; pillars retain muted warm barklike strata.
            {"Sous-bois vert profond, aiguilles brunes et colonnes minerales aux strates chaudes.",
             {{"Mousse sombre", "Aiguilles seches", "Pierre des colonnes", "Depots pales"}},
             biome(
                surface(material(0x354B3D,0x50664A,0x77815A, {{.91f,.95f,.97f,.75f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x584836,0x796344,0x99835C, {{.89f,.93f,.96f,.85f}}, {{0.55f,0.45f,0.f,0.f}}, .8f),
                        {{0.66f,0.18f,0.77f,0.23f}}, {{25.f,1.2f,0.3f,3.f}}),
                surface(material(0x4B5050,0x6D7370,0x96998A, {{.78f,.85f,.91f,1.1f}}, {{0.73f,0.15f,0.12f,0.f}}, 0.7f,1.272f),
                        material(0x6C6855,0x8D8870,0xB1AE93, {{.83f,.88f,.94f,.8f}}, {{0.6f,0.15f,0.25f,0.f}}, .5f),
                        {{0.675f,0.18f,0.65f,0.35f}}, {{82.f,1.2f,1.3f,32.8f}}))},
            // 4 BorealBog: quiet damp ground and desaturated peat rather than shiny foliage.
            {"Tourbe froide, mousses sourdes et parois humides bleu vert.",
             {{"Tourbe humide", "Mousse pale", "Roche humide", "Lichen froid"}},
             biome(
                surface(material(0x303D38,0x48574A,0x68745D, {{.47f,.58f,.72f,.65f}}, {{0.7f,0.3f,0.f,0.f}}, .95f,1.167f,.035f),
                        material(0x70795E,0x939A72,0xB5B997, {{.86f,.93f,.96f,.6f}}, {{0.55f,0.45f,0.f,0.f}}, .8f),
                        {{0.69f,0.18f,0.67f,0.33f}}, {{16.f,1.4f,1.8f,4.f}}),
                surface(material(0x354B4E,0x526B69,0x758E82, {{.39f,.5f,.66f,.9f}}, {{0.75f,0.2f,0.05f,0.f}}, 0.85f),
                        material(0x6C7C6D,0x8E9C81,0xB3B99B, {{.83f,.9f,.95f,.65f}}, {{0.59f,0.35f,0.06f,0.f}}, .7f),
                        {{0.685f,0.18f,0.45f,0.55f}}, {{92.f,1.4f,-0.7f,44.2f}}))},
            // 5 RockyTundra.
            {"Graviers froids, quartz clair et falaises de graphite stratifie.",
             {{"Gravier gris", "Lichen bronze", "Graphite mineral", "Filons de quartz"}},
             biome(
                surface(material(0x626B70,0x848B8B,0xACADA2, {{.88f,.93f,.96f,1.1f}}, {{0.75f,0.2f,0.05f,0.f}}),
                        material(0x746B4C,0x948C65,0xB5AF88, {{.91f,.95f,.97f,.7f}}, {{0.45f,0.55f,0.f,0.f}}, .65f),
                        {{0.73f,0.18f,0.82f,0.18f}}, {{14.f,1.3f,-0.2f,3.f}}),
                surface(material(0x3F4A58,0x606D79,0x87939A, {{.76f,.84f,.91f,1.25f}}, {{0.6f,0.2f,0.2f,0.f}}, 0.7f,1.272f),
                        material(0x9EA7A7,0xBFC4BA,0xDADAC9, {{.45f,.55f,.7f,.8f}}, {{0.5f,0.1f,0.4f,0.f}}, .3f,1.307f,.025f),
                        {{0.735f,0.16f,0.5f,0.5f}}, {{43.f,1.3f,0.7f,17.2f}}))},
            // 6 ColdSteppe.
            {"Herbes seches cendrees, sable froid et arches de gres bleu gris.",
             {{"Herbe cendree", "Sable froid", "Gres gris", "Couches creme"}},
             biome(
                surface(material(0x766B50,0x968963,0xB8AC84, {{.92f,.95f,.97f,.7f}}, {{0.75f,0.15f,0.f,0.1f}}),
                        material(0x8A8C80,0xAAAB96,0xC9C6AA, {{.83f,.88f,.93f,.6f}}, {{0.6f,0.1f,0.f,0.3f}}, .95f),
                        {{0.67f,0.2f,0.87f,0.13f}}, {{32.f,2.3f,-0.8f,1.5f}}),
                surface(material(0x536675,0x788B93,0xA2ACA8, {{.8f,.87f,.92f,1.05f}}, {{0.67f,0.15f,0.18f,0.f}}, 0.7f,1.272f),
                        material(0x9A947C,0xBBB59B,0xD7D1B5, {{.84f,.9f,.95f,.8f}}, {{0.49f,0.15f,0.36f,0.f}}, .35f),
                        {{0.675f,0.16f,0.65f,0.35f}}, {{125.f,2.f,0.4f,50.f}}))},
            // 7 BorealForest.
            {"Mousse de foret froide et plateaux rocheux brun gris aux bandes minerales.",
             {{"Mousse boreale", "Litiere rousse", "Roche des plateaux", "Strates calcaires"}},
             biome(
                surface(material(0x3E553F,0x60764D,0x879566, {{.91f,.95f,.97f,.75f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x68513A,0x8A6D46,0xAF9267, {{.9f,.93f,.96f,.8f}}, {{0.6f,0.4f,0.f,0.f}}, .75f),
                        {{0.69f,0.19f,0.82f,0.18f}}, {{22.f,1.4f,0.5f,3.f}}),
                surface(material(0x5A5B55,0x7C8074,0xA5A798, {{.8f,.87f,.93f,1.05f}}, {{0.63f,0.15f,0.22f,0.f}}, 0.7f),
                        material(0x8F8E75,0xB3B295,0xD0CEB1, {{.79f,.86f,.93f,.8f}}, {{0.5f,0.15f,0.35f,0.f}}, .35f,1.255f),
                        {{0.665f,0.16f,0.6f,0.4f}}, {{76.f,1.4f,-0.35f,30.4f}}))},
            // 8 ColdForest.
            {"Terre humide et fougeres sourdes, falaises bleutees dans les gouffres.",
             {{"Terre froide", "Couvert vegetal", "Schiste du gouffre", "Veines argileuses"}},
             biome(
                surface(material(0x3D4840,0x5A6650,0x7E8768, {{.62f,.74f,.85f,.8f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x48634A,0x6C8257,0x95A374, {{.89f,.94f,.97f,.65f}}, {{0.55f,0.45f,0.f,0.f}}, .75f),
                        {{0.61f,0.21f,0.67f,0.33f}}, {{20.f,1.6f,0.9f,3.f}}),
                surface(material(0x3D535E,0x5D7680,0x879B9F, {{.53f,.65f,.8f,1.05f}}, {{0.7f,0.2f,0.1f,0.f}}, 0.7f),
                        material(0x777D73,0x9EA18D,0xC2C4AA, {{.76f,.86f,.93f,.8f}}, {{0.58f,0.2f,0.22f,0.f}}, .5f),
                        {{0.685f,0.17f,0.7f,0.3f}}, {{145.f,1.6f,-0.65f,69.6f}}))},
            // 9 ColdMarsh.
            {"Vase bleue froide, ilots de mousse et formations minerales verdatres satinees.",
             {{"Vase froide", "Mousse des ilots", "Roche satinee", "Croute claire"}},
             biome(
                surface(material(0x394A49,0x556864,0x7B8C7F, {{.34f,.44f,.62f,.6f}}, {{0.75f,0.25f,0.f,0.f}}, .95f,1.185f,.025f),
                        material(0x65784D,0x8A9865,0xAFB78D, {{.87f,.93f,.96f,.65f}}, {{0.5f,0.5f,0.f,0.f}}, .75f),
                        {{0.7f,0.18f,0.67f,0.33f}}, {{24.f,1.7f,1.6f,4.f}}),
                surface(material(0x405D5F,0x63827C,0x90A99A, {{.3f,.43f,.59f,.85f}}, {{0.7f,0.25f,0.05f,0.f}}, 0.9f),
                        material(0x7E9681,0xA5B59A,0xC7D0B4, {{.66f,.78f,.9f,.7f}}, {{0.65f,0.25f,0.1f,0.f}}, .65f),
                        {{0.685f,0.17f,0.65f,0.35f}}, {{155.f,1.7f,0.4f,74.4f}}))},
            // 10 DryPlain.
            {"Terres ocres calmes, poussiere pale et roche sedimentaire chaude.",
             {{"Terre ocre", "Poussiere claire", "Roche de plaine", "Sediments creme"}},
             biome(
                surface(material(0x96784D,0xBA9B68,0xD3BA88, {{.9f,.94f,.97f,.75f}}, {{0.8f,0.15f,0.f,0.05f}}),
                        material(0xB4A07A,0xD0BE96,0xE3D3AE, {{.91f,.95f,.98f,.55f}}, {{0.75f,0.1f,0.f,0.15f}}, .95f),
                        {{0.7f,0.22f,0.92f,0.08f}}, {{25.f,2.f,-0.6f,1.3f}}),
                surface(material(0x74654E,0x9A8968,0xBDAE88, {{.83f,.89f,.95f,1.05f}}, {{0.56f,0.2f,0.24f,0.f}}, 0.7f),
                        material(0xB0A07E,0xCEC0A0,0xE3D9BD, {{.85f,.92f,.96f,.7f}}, {{0.52f,0.1f,0.38f,0.f}}, .35f),
                        {{0.685f,0.17f,0.7f,0.3f}}, {{63.f,2.f,0.2f,25.2f}}))},
            // 11 Meadow.
            {"Prairie vert dore ponctuee de terre, pierre claire aux ombres naturelles.",
             {{"Herbe de prairie", "Terre exposee", "Pierre de prairie", "Lichen dore"}},
             biome(
                surface(material(0x5D7541,0x819751,0xA8B875, {{.91f,.95f,.97f,.65f}}, {{0.85f,0.15f,0.f,0.f}}, .95f),
                        material(0x8E754D,0xB09565,0xCDB48B, {{.88f,.93f,.97f,.8f}}, {{0.55f,0.45f,0.f,0.f}}, .75f),
                        {{0.73f,0.18f,0.77f,0.23f}}, {{17.f,1.3f,0.55f,2.f}}),
                surface(material(0x697269,0x90978A,0xB5BAA5, {{.81f,.88f,.94f,1.f}}, {{0.71f,0.2f,0.09f,0.f}}, 0.7f),
                        material(0x91946B,0xB2B58B,0xCFD0AC, {{.9f,.94f,.97f,.65f}}, {{0.47f,0.45f,0.08f,0.f}}, .6f),
                        {{0.685f,0.19f,0.6f,0.4f}}, {{48.f,1.3f,-0.3f,19.2f}}))},
            // 12 TemperateForest.
            {"Sous-bois vert mousse, litiere ambree et roches gris chauds.",
             {{"Mousse temperee", "Litiere ambree", "Roche forestiere", "Lichen doux"}},
             biome(
                surface(material(0x3D5A36,0x5F7944,0x8B9A61, {{.92f,.95f,.97f,.7f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x715438,0x987449,0xBB996B, {{.88f,.93f,.96f,.8f}}, {{0.6f,0.4f,0.f,0.f}}, .7f),
                        {{0.65f,0.2f,0.77f,0.23f}}, {{24.f,1.5f,0.8f,3.f}}),
                surface(material(0x596253,0x7D8770,0xA5AE91, {{.77f,.85f,.92f,1.05f}}, {{0.7f,0.2f,0.1f,0.f}}, 0.7f),
                        material(0x8D9471,0xB0B58F,0xD0D0AB, {{.86f,.92f,.96f,.65f}}, {{0.54f,0.4f,0.06f,0.f}}, .6f),
                        {{0.685f,0.17f,0.6f,0.4f}}, {{84.f,1.5f,-0.7f,33.6f}}))},
            // 13 TemperateRainforest.
            {"Vert profond humide et pierre bleu vert, avec des accents de mousse jaune sourd.",
             {{"Humus humide", "Mousse lumineuse", "Roche des ravines", "Patine vegetale"}},
             biome(
                surface(material(0x2F4738,0x48674A,0x6A8B5D, {{.48f,.63f,.78f,.75f}}, {{0.75f,0.25f,0.f,0.f}}),
                        material(0x617847,0x849B5B,0xACBA7D, {{.85f,.92f,.96f,.6f}}, {{0.55f,0.45f,0.f,0.f}}, .75f),
                        {{0.67f,0.18f,0.72f,0.28f}}, {{22.f,1.6f,1.2f,3.f}}),
                surface(material(0x345650,0x54796A,0x7DA18A, {{.35f,.49f,.65f,.9f}}, {{0.68f,0.25f,0.07f,0.f}}, 0.85f),
                        material(0x69865B,0x90A575,0xB7C29B, {{.75f,.86f,.93f,.7f}}, {{0.44f,0.5f,0.06f,0.f}}, .65f),
                        {{0.645f,0.18f,0.5f,0.5f}}, {{118.f,1.6f,0.35f,56.6f}}))},
            // 14 TemperateMarsh.
            {"Vase olive et roseaux dores, parois usees et depots humides.",
             {{"Vase olive", "Couvert de roseaux", "Argile humide", "Depots secs"}},
             biome(
                surface(material(0x4C5140,0x6B7152,0x90916C, {{.35f,.46f,.64f,.6f}}, {{0.7f,0.3f,0.f,0.f}}, .95f,1.202f,.025f),
                        material(0x7B854F,0xA0A564,0xC1C18C, {{.87f,.93f,.97f,.6f}}, {{0.6f,0.4f,0.f,0.f}}, .75f),
                        {{0.68f,0.18f,0.77f,0.23f}}, {{18.f,1.8f,1.5f,4.f}}),
                surface(material(0x5A6350,0x7D8668,0xA5AC8B, {{.43f,.57f,.75f,.85f}}, {{0.61f,0.25f,0.14f,0.f}}, 0.75f),
                        material(0x9A9672,0xBAB793,0xD5D1B1, {{.8f,.88f,.95f,.7f}}, {{0.56f,0.2f,0.24f,0.f}}, .55f),
                        {{0.695f,0.18f,0.7f,0.3f}}, {{66.f,1.8f,-0.5f,26.4f}}))},
            // 15 RockDesert.
            {"Gres cuivre, poussiere doree et lits sedimentaires couleur ivoire.",
             {{"Gravier cuivre", "Sable dore", "Gres du desert", "Lits ivoire"}},
             biome(
                surface(material(0x8C6045,0xB07D54,0xD0A173, {{.86f,.91f,.96f,1.f}}, {{0.75f,0.2f,0.05f,0.f}}),
                        material(0xB08B5C,0xD0AE77,0xE7C994, {{.85f,.91f,.95f,.6f}}, {{0.65f,0.05f,0.f,0.3f}}, .95f),
                        {{0.67f,0.2f,0.87f,0.13f}}, {{16.f,1.8f,-0.45f,1.5f}}),
                surface(material(0x80533F,0xA87552,0xCF9F77, {{.79f,.86f,.93f,1.15f}}, {{0.51f,0.15f,0.34f,0.f}}, 0.7f,1.255f),
                        material(0xAD9473,0xD0B899,0xE6D4B7, {{.83f,.9f,.95f,.8f}}, {{0.48f,0.1f,0.42f,0.f}}, .25f,1.272f),
                        {{0.675f,0.16f,0.6f,0.4f}}, {{47.f,1.8f,0.25f,18.8f}}))},
            // 16 DrySavanna.
            {"Herbes blondes, terre brulee et roche brune dans une palette seche.",
             {{"Herbe blonde", "Terre brulee", "Roche brune", "Poussiere calcaire"}},
             biome(
                surface(material(0x9A813F,0xBDA154,0xD9C17D, {{.91f,.95f,.98f,.65f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x80583B,0xA7784B,0xC99D70, {{.88f,.93f,.97f,.8f}}, {{0.6f,0.4f,0.f,0.f}}, .75f),
                        {{0.66f,0.2f,0.82f,0.18f}}, {{22.f,1.7f,0.65f,2.5f}}),
                surface(material(0x796247,0x9F8360,0xBFA581, {{.82f,.88f,.94f,1.f}}, {{0.6f,0.2f,0.2f,0.f}}, 0.7f),
                        material(0xB1A177,0xD0C097,0xE4D6B4, {{.87f,.92f,.96f,.7f}}, {{0.55f,0.15f,0.3f,0.f}}, .4f),
                        {{0.695f,0.17f,0.7f,0.3f}}, {{69.f,1.7f,-0.35f,27.6f}}))},
            // 17 Savanna.
            {"Vert olive dore et terre rouge, falaises chaudes avec des couches pales.",
             {{"Herbe olive", "Terre rouge", "Roche de savane", "Couches pales"}},
             biome(
                surface(material(0x788044,0x9BA151,0xBFC17C, {{.92f,.95f,.98f,.65f}}, {{0.85f,0.15f,0.f,0.f}}),
                        material(0x8D5137,0xB5744B,0xD29C71, {{.87f,.92f,.96f,.8f}}, {{0.6f,0.4f,0.f,0.f}}, .7f),
                        {{0.67f,0.19f,0.77f,0.23f}}, {{24.f,1.5f,0.9f,3.f}}),
                surface(material(0x76684A,0x9B8965,0xBDAB87, {{.8f,.87f,.94f,1.f}}, {{0.62f,0.2f,0.18f,0.f}}, 0.7f),
                        material(0xABA47A,0xCAC39B,0xE1D8B4, {{.85f,.91f,.96f,.7f}}, {{0.5f,0.2f,0.3f,0.f}}, .45f),
                        {{0.685f,0.17f,0.6f,0.4f}}, {{82.f,1.5f,0.5f,32.8f}}))},
            // 18 DryJungle.
            {"Vegetation olive profonde et litiere caramel, roche chaude patinee.",
             {{"Couvert olive", "Litiere caramel", "Roche chaude", "Patine claire"}},
             biome(
                surface(material(0x515F35,0x748047,0x9AA465, {{.91f,.95f,.98f,.75f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x795032,0xA07243,0xC39768, {{.86f,.92f,.96f,.85f}}, {{0.55f,0.45f,0.f,0.f}}, .7f),
                        {{0.67f,0.18f,0.77f,0.23f}}, {{20.f,1.4f,1.1f,3.f}}),
                surface(material(0x68644B,0x8B8662,0xB0A77E, {{.76f,.84f,.92f,1.05f}}, {{0.67f,0.2f,0.13f,0.f}}, 0.7f),
                        material(0x91936A,0xB5B48B,0xD2CCAC, {{.85f,.92f,.96f,.7f}}, {{0.52f,0.4f,0.08f,0.f}}, .55f),
                        {{0.665f,0.18f,0.55f,0.45f}}, {{94.f,1.4f,-0.6f,37.6f}}))},
            // 19 TropicalJungle.
            {"Humus vert sombre, mousses jade et roche humide avec des depots creme.",
             {{"Humus tropical", "Mousse jade", "Roche tropicale", "Depots creme"}},
             biome(
                surface(material(0x294634,0x406748,0x628956, {{.5f,.66f,.81f,.75f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x4F7542,0x759754,0xA1B47B, {{.86f,.92f,.97f,.6f}}, {{0.55f,0.45f,0.f,0.f}}, .7f),
                        {{0.65f,0.19f,0.72f,0.28f}}, {{26.f,1.6f,1.4f,4.f}}),
                surface(material(0x3E5750,0x5C7C67,0x84A081, {{.34f,.48f,.66f,.95f}}, {{0.62f,0.3f,0.08f,0.f}}, 0.8f),
                        material(0x91A07A,0xB5BE99,0xD4D7B9, {{.73f,.84f,.93f,.7f}}, {{0.6f,0.3f,0.1f,0.f}}, .6f),
                        {{0.705f,0.18f,0.65f,0.35f}}, {{125.f,1.6f,0.7f,60.f}}))},
            // 20 VolcanicDesert: dark still has readable albedo; no fake metallic basalt.
            {"Basalte charbon, cendres bleu gris et rares oxydes chauds sans emission artificielle.",
             {{"Basalte charbon", "Cendre froide", "Basalte des volcans", "Oxydes ferriques"}},
             biome(
                surface(material(0x252933,0x3C424C,0x59616A, {{.73f,.82f,.91f,.95f}}, {{0.825f,0.15f,0.025f,0.f}}, .95f,1.237f),
                        material(0x656B70,0x898D8B,0xAFADA1, {{.91f,.95f,.98f,.6f}}, {{0.8f,0.2f,0.f,0.f}}, .95f),
                        {{0.72f,0.21f,0.92f,0.08f}}, {{20.f,1.8f,0.4f,4.f}}),
                surface(material(0x292B34,0x434750,0x666C72, {{.59f,.72f,.87f,1.2f}}, {{0.65f,0.2f,0.15f,0.f}}, 0.7f,1.272f),
                        material(0x633C30,0x89583E,0xAE7D57, {{.81f,.89f,.95f,.9f}}, {{0.5f,0.25f,0.25f,0.f}}, .65f),
                        {{0.745f,0.17f,0.7f,0.3f}}, {{72.f,1.8f,-0.45f,28.8f}}))},
            // 21 RedDesert: the strongest warm palette, with a quiet fine-scale response.
            {"Dunes terre cuite, sable ambre et gres rouge aux bandes creme.",
             {{"Sable rouge", "Sable ambre", "Gres rouge", "Gres creme"}},
             biome(
                surface(material(0xA34F35,0xC87549,0xE3A273, {{.84f,.89f,.94f,.65f}}, {{0.7f,0.f,0.f,0.3f}}, .95f,1.185f,.035f),
                        material(0xBB824E,0xD8A66C,0xEAC695, {{.84f,.91f,.95f,.55f}}, {{0.8f,0.f,0.f,0.2f}}, .95f),
                        {{0.69f,0.24f,0.97f,0.03f}}, {{18.f,2.2f,-0.7f,1.6f}}),
                surface(material(0x8E4434,0xB76A49,0xD89B73, {{.81f,.88f,.94f,1.05f}}, {{0.52f,0.15f,0.33f,0.f}}, 0.7f,1.255f),
                        material(0xBEA07F,0xDCC1A1,0xEDD8BA, {{.84f,.91f,.96f,.75f}}, {{0.48f,0.1f,0.42f,0.f}}, .3f),
                        {{0.675f,0.16f,0.6f,0.4f}}, {{52.f,2.2f,0.3f,20.8f}}))},
            // 22 AridPlateau.
            {"Poussiere de gres pale, croûtes dorees et falaises aux strates larges.",
             {{"Poussiere du plateau", "Croute doree", "Gres du plateau", "Bancs clairs"}},
             biome(
                surface(material(0xAC956B,0xCAB38A,0xE1D0AB, {{.9f,.94f,.97f,.65f}}, {{0.8f,0.1f,0.f,0.1f}}, .95f),
                        material(0x94723F,0xB79355,0xD3B579, {{.85f,.91f,.96f,.85f}}, {{0.6f,0.35f,0.05f,0.f}}, .7f),
                        {{0.71f,0.19f,0.82f,0.18f}}, {{32.f,2.5f,0.2f,2.f}}),
                surface(material(0x8E7654,0xB39870,0xD3BA92, {{.8f,.88f,.94f,1.1f}}, {{0.49f,0.15f,0.36f,0.f}}, 0.7f,1.272f),
                        material(0xC0B18F,0xDACBAA,0xEDE0C2, {{.85f,.92f,.97f,.75f}}, {{0.46f,0.1f,0.44f,0.f}}, .25f,1.272f),
                        {{0.665f,0.16f,0.65f,0.35f}}, {{125.f,2.5f,-0.25f,50.f}}))},
            // 23 BurningJungle.
            {"Sous-bois bronze sombre, cendres chaudes et roche patinee de cuivre mat.",
             {{"Sous-bois bronze", "Cendre chaude", "Roche sombre", "Patine cuivre"}},
             biome(
                surface(material(0x414532,0x626444,0x88865B, {{.85f,.91f,.96f,.8f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x795B41,0xA07E56,0xC2A47D, {{.91f,.95f,.98f,.7f}}, {{0.7f,0.3f,0.f,0.f}}, .8f),
                        {{0.68f,0.19f,0.82f,0.18f}}, {{24.f,1.6f,1.2f,4.f}}),
                surface(material(0x484638,0x6C6550,0x938A6C, {{.72f,.82f,.91f,1.1f}}, {{0.61f,0.25f,0.14f,0.f}}, 0.7f),
                        material(0x815A3F,0xA87D53,0xCCA379, {{.8f,.88f,.95f,.85f}}, {{0.48f,0.25f,0.27f,0.f}}, .5f),
                        {{0.675f,0.17f,0.65f,0.35f}}, {{88.f,1.6f,0.5f,35.2f}}))},
            // 24 TropicalMangrove.
            {"Vase vert brun, depots de sel ivoire et roche humide couleur jade sourd.",
             {{"Vase de mangrove", "Depots de sel", "Roche jade", "Patine de lichen"}},
             biome(
                surface(material(0x3E5041,0x5D7051,0x81916B, {{.3f,.43f,.62f,.55f}}, {{0.75f,0.25f,0.f,0.f}}, .95f,1.185f,.025f),
                        material(0x9AA183,0xBEC1A3,0xDADAC0, {{.78f,.87f,.94f,.5f}}, {{0.6f,0.4f,0.f,0.f}}, .7f),
                        {{0.76f,0.18f,0.82f,0.18f}}, {{21.f,1.8f,1.7f,4.f}}),
                surface(material(0x3C5D51,0x5D826A,0x88A88A, {{.32f,.46f,.65f,.9f}}, {{0.69f,0.25f,0.06f,0.f}}, 0.8f),
                        material(0x879C6F,0xADBA8E,0xCCD0AE, {{.75f,.85f,.93f,.65f}}, {{0.49f,0.45f,0.06f,0.f}}, .6f),
                        {{0.675f,0.18f,0.55f,0.45f}}, {{110.f,1.8f,-0.6f,52.8f}}))},
            // 25 Marine: seabed material, never a water surface or baked depth fog.
            {"Sable sous-marin desature et roche vert ardoise, sans brume peinte dans la couleur.",
             {{"Sable marin", "Sediments sombres", "Roche marine", "Depots calcaires"}},
             biome(
                surface(material(0x7F8E85,0xA4AFA0,0xC8CDB6, {{.81f,.87f,.93f,.55f}}, {{0.7f,0.f,0.f,0.3f}}, .95f,1.167f,.025f),
                        material(0x4C6460,0x6D8376,0x93A48E, {{.65f,.76f,.88f,.7f}}, {{0.75f,0.25f,0.f,0.f}}, .9f),
                        {{0.67f,0.21f,0.87f,0.13f}}, {{28.f,2.1f,0.6f,1.8f}}),
                surface(material(0x3D5B5C,0x5E7A73,0x88A08C, {{.55f,.69f,.83f,1.f}}, {{0.67f,0.2f,0.13f,0.f}}, 0.75f),
                        material(0x8FABA3,0xB5C8B8,0xD3DCCD, {{.69f,.81f,.92f,.65f}}, {{0.48f,0.3f,0.22f,0.f}}, .6f),
                        {{0.705f,0.18f,0.6f,0.4f}}, {{93.f,2.1f,-0.4f,37.2f}}))}
        }};
        return data;
    }
}

ProceduralTerrain::TerrainAppearance makeTerrainAppearance()
{
    ProceduralTerrain::TerrainAppearance appearance;
    // A 96 m patch field leaves broad quiet surfaces between material accents.
    appearance.patchPeriod = 96.0f;
    const auto& authored = recipes();
    for (std::size_t i = 0; i < authored.size(); ++i)
        appearance.biomes[i] = authored[i].appearance;
    return appearance;
}

const char* terrainMaterialIntent(std::size_t biome) noexcept
{
    return biome < recipes().size() ? recipes()[biome].intent : "";
}

const char* terrainMaterialLabel(std::size_t biome, bool wall, bool accent) noexcept
{
    return biome < recipes().size()
        ? recipes()[biome].labels[(wall ? 2u : 0u) + (accent ? 1u : 0u)] : "";
}
}
