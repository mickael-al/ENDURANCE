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
        V roughNormal, V pattern, float softness = 0.95f, float contrast = 1.08f,
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
                surface(material(0xC6D3D5,0xD5DEDC,0xE8ECE4, {{.87f,.89f,.92f,.55f}}, {{0.8f,0.f,0.f,0.2f}}, 0.95f,1.060f,.025f),
                        material(0x7498A4,0x8DADB5,0xA9C3C8, {{.27f,.34f,.42f,.30f}}, {{0.85f,0.1f,0.05f,0.f}}, 0.95f,1.067f,.015f),
                        {{0.7f,0.18f,0.97f,0.03f}}, {{48.f,2.6f,0.38f,1.1f}}),
                surface(material(0x526F84,0x65869A,0x7C9AAA, {{.30f,.37f,.47f,.55f}}, {{0.65f,0.15f,0.2f,0.f}}, 0.82f,1.095f,.02f),
                        material(0x9FBFC5,0xB4CFD1,0xCDDDD9, {{.54f,.64f,.77f,.45f}}, {{0.6f,0.1f,0.3f,0.f}}, 0.82f,1.109f,.02f),
                        {{0.705f,0.16f,0.6f,0.4f}}, {{115.f,3.f,0.22f,55.2f}}), {{.20f,.58f,.09f,.9f}})},
            // 1 FrozenTundra: cold mineral massifs, warm glacial sediment in gaps.
            {"Neige mince et sediments taupe sous des massifs ardoise givres.",
             {{"Sediment gele", "Neige mince", "Schiste froid", "Givre mineral"}},
             biome(
                surface(material(0x858B83,0x979D90,0xA9AD9F, {{.84f,.88f,.91f,.8f}}, {{0.75f,0.25f,0.f,0.f}}),
                        material(0xBDCACC,0xCCD6D2,0xDDE2DB, {{.87f,.91f,.94f,.5f}}, {{0.9f,0.1f,0.f,0.f}}, 0.95f),
                        {{0.63f,0.21f,0.87f,0.13f}}, {{28.f,1.5f,1.05f,3.f}}),
                surface(material(0x60737F,0x738691,0x8A99A1, {{.78f,.85f,.91f,1.05f}}, {{0.68f,0.2f,0.12f,0.f}}, 0.82f,1.102f),
                        material(0xA7B8BC,0xB9C9CA,0xCDD8D5, {{.75f,.84f,.9f,.65f}}, {{0.75f,0.1f,0.15f,0.f}}, 0.82f),
                        {{0.665f,0.19f,0.8f,0.2f}}, {{68.f,1.5f,0.8f,27.2f}}), {{.22f,.61f,.1f,1.f}})},
            // 2 Tundra: olive lichen and ochre humus, violet-gray mineral cliffs.
            {"Lichen olive et humus ocre, roches gris violace sans saturation excessive.",
             {{"Humus sec", "Lichen", "Roche de toundra", "Oxydation claire"}},
             biome(
                surface(material(0x7A6F51,0x8A7E5D,0x9C906D, {{.94f,.92f,.9f,.85f}}, {{0.72f,0.28f,0.f,0.f}}),
                        material(0x6D7553,0x7D835D,0x8D926A, {{.94f,.96f,.97f,.65f}}, {{0.55f,0.45f,0.f,0.f}}, 0.82f,1.109f),
                        {{0.55f,0.18f,0.67f,0.33f}}, {{18.f,1.2f,0.65f,2.f}}),
                surface(material(0x65656B,0x76767A,0x88888A, {{.82f,.88f,.93f,1.15f}}, {{0.64f,0.2f,0.16f,0.f}}, 0.82f,1.095f),
                        material(0x88806B,0x99917A,0xAAA28B, {{.86f,.91f,.94f,.9f}}, {{0.5f,0.25f,0.25f,0.f}}, 0.82f),
                        {{0.685f,0.17f,0.65f,0.35f}}, {{54.f,1.4f,-0.5f,21.6f}}))},
            // 3 Taiga: broad dark-green floor; pillars retain muted warm barklike strata.
            {"Sous-bois vert profond, aiguilles brunes et colonnes minerales aux strates chaudes.",
             {{"Mousse sombre", "Aiguilles seches", "Pierre des colonnes", "Depots pales"}},
             biome(
                surface(material(0x44553E,0x50624A,0x607154, {{.91f,.95f,.97f,.75f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x5A553B,0x686143,0x796E4E, {{.89f,.93f,.96f,.85f}}, {{0.55f,0.45f,0.f,0.f}}, 0.82f),
                        {{0.66f,0.18f,0.77f,0.23f}}, {{25.f,1.2f,0.3f,3.f}}),
                surface(material(0x626B67,0x717C74,0x839085, {{.78f,.85f,.91f,1.1f}}, {{0.73f,0.15f,0.12f,0.f}}, 0.82f,1.109f),
                        material(0x7B806A,0x8B9078,0x9DA18A, {{.83f,.88f,.94f,.8f}}, {{0.6f,0.15f,0.25f,0.f}}, 0.82f),
                        {{0.675f,0.18f,0.65f,0.35f}}, {{82.f,1.2f,1.3f,32.8f}}))},
            // 4 BorealBog: quiet damp ground and desaturated peat rather than shiny foliage.
            {"Tourbe froide, mousses sourdes et parois humides bleu vert.",
             {{"Tourbe humide", "Mousse sourde", "Roche humide", "Lichen froid"}},
             biome(
                surface(material(0x425044,0x48584B,0x505F52, {{0.66f,0.70f,0.75f,.65f}}, {{0.7f,0.3f,0.f,0.f}}, 0.95f,1.067f,.035f),
                        material(0x495940,0x536348,0x5E6D51, {{0.78f,0.82f,0.86f,.6f}}, {{0.55f,0.45f,0.f,0.f}}, 0.82f),
                        {{0.69f,0.18f,0.67f,0.33f}}, {{16.f,1.4f,1.8f,4.f}}),
                surface(material(0x506967,0x587170,0x617A78, {{0.62f,0.67f,0.72f,.9f}}, {{0.75f,0.2f,0.05f,0.f}}, 0.85f),
                        material(0x5B7065,0x657A6F,0x6F8478, {{0.77f,0.82f,0.87f,.65f}}, {{0.59f,0.35f,0.06f,0.f}}, 0.82f),
                        {{0.685f,0.18f,0.45f,0.55f}}, {{92.f,1.4f,-0.7f,44.2f}}))},
            // 5 RockyTundra.
            {"Graviers froids, quartz clair et falaises de graphite stratifie.",
             {{"Gravier gris", "Lichen bronze", "Graphite mineral", "Filons de quartz"}},
             biome(
                surface(material(0x758085,0x859092,0x97A1A0, {{.88f,.93f,.96f,1.1f}}, {{0.75f,0.2f,0.05f,0.f}}),
                        material(0x7C8068,0x8C9076,0x9DA187, {{.91f,.95f,.97f,.7f}}, {{0.45f,0.55f,0.f,0.f}}, 0.82f),
                        {{0.73f,0.18f,0.82f,0.18f}}, {{14.f,1.3f,-0.2f,3.f}}),
                surface(material(0x536473,0x637587,0x758898, {{.76f,.84f,.91f,1.25f}}, {{0.6f,0.2f,0.2f,0.f}}, 0.82f,1.109f),
                        material(0x8E9D9F,0xA2AFB0,0xB8C2C0, {{.45f,.55f,.7f,.8f}}, {{0.5f,0.1f,0.4f,0.f}}, 0.82f,1.123f,.025f),
                        {{0.735f,0.16f,0.5f,0.5f}}, {{43.f,1.3f,0.7f,17.2f}}))},
            // 6 ColdSteppe.
            {"Herbes seches cendrees, sable froid et arches de gres bleu gris.",
             {{"Herbe cendree", "Sable froid", "Gres gris", "Couches creme"}},
             biome(
                surface(material(0x847D61,0x948D6F,0xA59F80, {{.92f,.95f,.97f,.7f}}, {{0.75f,0.15f,0.f,0.1f}}),
                        material(0x969988,0xA5AA97,0xB8BBA6, {{.83f,.88f,.93f,.6f}}, {{0.6f,0.1f,0.f,0.3f}}, 0.95f),
                        {{0.67f,0.2f,0.87f,0.13f}}, {{32.f,2.3f,-0.8f,1.5f}}),
                surface(material(0x6E808E,0x7E919E,0x92A3AD, {{.8f,.87f,.92f,1.05f}}, {{0.67f,0.15f,0.18f,0.f}}, 0.82f,1.109f),
                        material(0x969B90,0xA7ADA0,0xB9BFB1, {{.84f,.9f,.95f,.8f}}, {{0.49f,0.15f,0.36f,0.f}}, 0.82f),
                        {{0.675f,0.16f,0.65f,0.35f}}, {{125.f,2.f,0.4f,50.f}}))},
            // 7 BorealForest.
            {"Mousse de foret froide et plateaux rocheux brun gris aux bandes minerales.",
             {{"Mousse boreale", "Litiere rousse", "Roche des plateaux", "Strates calcaires"}},
             biome(
                surface(material(0x526447,0x60744F,0x71845B, {{.91f,.95f,.97f,.75f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x6D684B,0x7C7555,0x8D8563, {{.9f,.93f,.96f,.8f}}, {{0.6f,0.4f,0.f,0.f}}, 0.82f),
                        {{0.69f,0.19f,0.82f,0.18f}}, {{22.f,1.4f,0.5f,3.f}}),
                surface(material(0x6D7567,0x7D8575,0x8F9686, {{.8f,.87f,.93f,1.05f}}, {{0.63f,0.15f,0.22f,0.f}}, 0.82f),
                        material(0x919783,0xA2A792,0xB4B7A3, {{.79f,.86f,.93f,.8f}}, {{0.5f,0.15f,0.35f,0.f}}, 0.82f,1.102f),
                        {{0.665f,0.16f,0.6f,0.4f}}, {{76.f,1.4f,-0.35f,30.4f}}))},
            // 8 ColdForest.
            {"Terre humide et fougeres sourdes, falaises bleutees dans les gouffres.",
             {{"Terre froide", "Couvert vegetal", "Schiste du gouffre", "Veines argileuses"}},
             biome(
                surface(material(0x4D5A47,0x5B6951,0x6B795D, {{.62f,.74f,.85f,.8f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x576B4D,0x657A57,0x758B64, {{.89f,.94f,.97f,.65f}}, {{0.55f,0.45f,0.f,0.f}}, 0.82f),
                        {{0.61f,0.21f,0.67f,0.33f}}, {{20.f,1.6f,0.9f,3.f}}),
                surface(material(0x4E6972,0x5D7B85,0x718E96, {{.53f,.65f,.8f,1.05f}}, {{0.7f,0.2f,0.1f,0.f}}, 0.82f),
                        material(0x7A877D,0x8C998C,0xA1AA9E, {{.76f,.86f,.93f,.8f}}, {{0.58f,0.2f,0.22f,0.f}}, 0.82f),
                        {{0.685f,0.17f,0.7f,0.3f}}, {{145.f,1.6f,-0.65f,69.6f}}))},
            // 9 ColdMarsh.
            {"Vase bleue froide, ilots de mousse et formations minerales verdatres satinees.",
             {{"Vase froide", "Mousse des ilots", "Roche satinee", "Croute minerale"}},
             biome(
                surface(material(0x506660,0x587168,0x627A70, {{0.62f,0.67f,0.73f,.6f}}, {{0.75f,0.25f,0.f,0.f}}, 0.95f,1.074f,.025f),
                        material(0x5A705B,0x647B65,0x6F866F, {{0.78f,0.82f,0.86f,.65f}}, {{0.5f,0.5f,0.f,0.f}}, 0.82f),
                        {{0.7f,0.18f,0.67f,0.33f}}, {{24.f,1.7f,1.6f,4.f}}),
                surface(material(0x5C8177,0x668B81,0x72958B, {{0.60f,0.65f,0.72f,.85f}}, {{0.7f,0.25f,0.05f,0.f}}, 0.90f),
                        material(0x718C7D,0x7D9788,0x88A392, {{0.73f,0.78f,0.84f,.7f}}, {{0.65f,0.25f,0.1f,0.f}}, 0.82f),
                        {{0.685f,0.17f,0.65f,0.35f}}, {{155.f,1.7f,0.4f,74.4f}}))},
            // 10 DryPlain.
            {"Terres ocres sourdes, poussiere ambree et roche sedimentaire chaude.",
             {{"Terre ocre", "Poussiere ambree", "Roche de plaine", "Sediments ocres"}},
             biome(
                surface(material(0x926F43,0x9D7D50,0xAA8A5C, {{.9f,.94f,.97f,.75f}}, {{0.8f,0.15f,0.f,0.05f}}),
                        material(0xA18A60,0xAD976C,0xBBA67B, {{.91f,.95f,.98f,.55f}}, {{0.75f,0.1f,0.f,0.15f}}, 0.95f),
                        {{0.715f,0.22f,0.92f,0.08f}}, {{25.f,2.f,-0.6f,1.3f}}),
                surface(material(0x76634B,0x846F55,0x947E61, {{.83f,.89f,.95f,1.05f}}, {{0.56f,0.2f,0.24f,0.f}}, 0.82f),
                        material(0x988361,0xA6916E,0xB6A17D, {{.85f,.92f,.96f,.7f}}, {{0.52f,0.1f,0.38f,0.f}}, 0.82f),
                        {{0.700f,0.20f,0.7f,0.3f}}, {{63.f,2.f,0.2f,25.2f}}))},
            // 11 Meadow.
            {"Prairie olive profonde, terre herbeuse et pierre gris vert.",
             {{"Herbe de prairie", "Terre herbeuse", "Pierre de prairie", "Lichen olive"}},
             biome(
                surface(material(0x3C502B,0x495E33,0x566C3D, {{.91f,.95f,.97f,.65f}}, {{0.85f,0.15f,0.f,0.f}}, 0.95f),
                        material(0x59603D,0x646846,0x727351, {{.88f,.93f,.97f,.8f}}, {{0.55f,0.45f,0.f,0.f}}, 0.82f),
                        {{0.745f,0.20f,0.77f,0.23f}}, {{17.f,1.3f,0.55f,2.f}}),
                surface(material(0x565C50,0x646B5D,0x747C6B, {{.81f,.88f,.94f,1.f}}, {{0.71f,0.2f,0.09f,0.f}}, 0.82f),
                        material(0x70785F,0x7C846B,0x899177, {{.9f,.94f,.97f,.65f}}, {{0.47f,0.45f,0.08f,0.f}}, 0.82f),
                        {{0.700f,0.20f,0.6f,0.4f}}, {{48.f,1.3f,-0.3f,19.2f}}))},
            // 12 TemperateForest.
            {"Sous-bois vert mousse sombre, litiere humifiee et roches gris chauds.",
             {{"Mousse temperee", "Litiere humifiee", "Roche forestiere", "Lichen doux"}},
             biome(
                surface(material(0x2A3D29,0x334A2F,0x3E5736, {{.92f,.95f,.97f,.7f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x41462E,0x4A5034,0x565B3D, {{.88f,.93f,.96f,.8f}}, {{0.6f,0.4f,0.f,0.f}}, 0.82f),
                        {{0.665f,0.20f,0.77f,0.23f}}, {{24.f,1.5f,0.8f,3.f}}),
                surface(material(0x3D483D,0x495547,0x566252, {{.77f,.85f,.92f,1.05f}}, {{0.7f,0.2f,0.1f,0.f}}, 0.82f),
                        material(0x596249,0x646E53,0x707B5D, {{.86f,.92f,.96f,.65f}}, {{0.54f,0.4f,0.06f,0.f}}, 0.82f),
                        {{0.700f,0.20f,0.6f,0.4f}}, {{84.f,1.5f,-0.7f,33.6f}}))},
            // 13 TemperateRainforest.
            {"Verts humides profonds et roche bleu vert, avec une mousse de teinte voisine.",
             {{"Humus humide", "Mousse humide", "Roche des ravines", "Patine vegetale"}},
             biome(
                surface(material(0x244032,0x2E4B3A,0x385843, {{0.64f,0.69f,0.75f,.75f}}, {{0.75f,0.25f,0.f,0.f}}),
                        material(0x3C533A,0x465F41,0x536C4C, {{0.78f,0.83f,0.88f,.6f}}, {{0.55f,0.45f,0.f,0.f}}, 0.82f),
                        {{0.685f,0.20f,0.72f,0.28f}}, {{22.f,1.6f,1.2f,3.f}}),
                surface(material(0x2D4640,0x36534B,0x416056, {{0.60f,0.66f,0.73f,.9f}}, {{0.68f,0.25f,0.07f,0.f}}, 0.85f),
                        material(0x455D48,0x506A52,0x5C765D, {{0.76f,0.81f,0.87f,.7f}}, {{0.44f,0.5f,0.06f,0.f}}, 0.82f),
                        {{0.660f,0.20f,0.5f,0.5f}}, {{118.f,1.6f,0.35f,56.6f}}))},
            // 14 TemperateMarsh.
            {"Vase olive sombre et roseaux sourds, parois usees et depots argileux.",
             {{"Vase olive", "Couvert de roseaux", "Argile humide", "Depots secs"}},
             biome(
                surface(material(0x41472E,0x4C5336,0x59603F, {{.35f,.46f,.64f,.6f}}, {{0.7f,0.3f,0.f,0.f}}, 0.95f,1.081f,.025f),
                        material(0x57603B,0x626B42,0x6F774C, {{.87f,.93f,.97f,.6f}}, {{0.6f,0.4f,0.f,0.f}}, 0.82f),
                        {{0.695f,0.20f,0.77f,0.23f}}, {{18.f,1.8f,1.5f,4.f}}),
                surface(material(0x474F3B,0x545C45,0x626A50, {{.43f,.57f,.75f,.85f}}, {{0.61f,0.25f,0.14f,0.f}}, 0.82f),
                        material(0x62674A,0x6E7354,0x7C8160, {{.8f,.88f,.95f,.7f}}, {{0.56f,0.2f,0.24f,0.f}}, 0.82f),
                        {{0.710f,0.20f,0.7f,0.3f}}, {{66.f,1.8f,-0.5f,26.4f}}))},
            // 15 RockDesert.
            {"Gres cuivre sombre, sable chaud et lits sedimentaires ambres.",
             {{"Gravier cuivre", "Sable cuivre", "Gres du desert", "Lits ambres"}},
             biome(
                surface(material(0x71533F,0x80604A,0x906E57, {{.86f,.91f,.96f,1.f}}, {{0.75f,0.2f,0.05f,0.f}}),
                        material(0x856A4D,0x937957,0xA28765, {{.85f,.91f,.95f,.6f}}, {{0.65f,0.05f,0.f,0.3f}}, 0.95f),
                        {{0.685f,0.20f,0.87f,0.13f}}, {{16.f,1.8f,-0.45f,1.5f}}),
                surface(material(0x6B493B,0x7B5644,0x8B654F, {{.79f,.86f,.93f,1.15f}}, {{0.51f,0.15f,0.34f,0.f}}, 0.82f,1.102f),
                        material(0x8A6C54,0x987B62,0xA78B72, {{.83f,.9f,.95f,.8f}}, {{0.48f,0.1f,0.42f,0.f}}, 0.82f,1.109f),
                        {{0.690f,0.20f,0.6f,0.4f}}, {{47.f,1.8f,0.25f,18.8f}}))},
            // 16 DrySavanna.
            {"Herbes paille sourdes, terre brulee et roche brune dans une palette seche.",
             {{"Herbe paille", "Terre brulee", "Roche brune", "Poussiere ocre"}},
             biome(
                surface(material(0x80713F,0x8E7E48,0x9D8D54, {{.91f,.95f,.98f,.65f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x776043,0x856D4D,0x947B59, {{.88f,.93f,.97f,.8f}}, {{0.6f,0.4f,0.f,0.f}}, 0.82f),
                        {{0.675f,0.20f,0.82f,0.18f}}, {{22.f,1.7f,0.65f,2.5f}}),
                surface(material(0x6D5B40,0x7B684A,0x8B7656, {{.82f,.88f,.94f,1.f}}, {{0.6f,0.2f,0.2f,0.f}}, 0.82f),
                        material(0x887A55,0x968863,0xA69871, {{.87f,.92f,.96f,.7f}}, {{0.55f,0.15f,0.3f,0.f}}, 0.82f),
                        {{0.710f,0.20f,0.7f,0.3f}}, {{69.f,1.7f,-0.35f,27.6f}}))},
            // 17 Savanna.
            {"Vert olive dore et terre ferrugineuse sourde, falaises aux couches ocres.",
             {{"Herbe olive", "Terre ferrugineuse", "Roche de savane", "Couches ocres"}},
             biome(
                surface(material(0x5C6339,0x687040,0x777D4A, {{.92f,.95f,.98f,.65f}}, {{0.85f,0.15f,0.f,0.f}}),
                        material(0x76543B,0x825F43,0x906D50, {{.87f,.92f,.96f,.8f}}, {{0.6f,0.4f,0.f,0.f}}, 0.82f),
                        {{0.685f,0.20f,0.77f,0.23f}}, {{24.f,1.5f,0.9f,3.f}}),
                surface(material(0x665B42,0x75694C,0x857858, {{.8f,.87f,.94f,1.f}}, {{0.62f,0.2f,0.18f,0.f}}, 0.82f),
                        material(0x827750,0x91845D,0xA1946B, {{.85f,.91f,.96f,.7f}}, {{0.5f,0.2f,0.3f,0.f}}, 0.82f),
                        {{0.700f,0.20f,0.6f,0.4f}}, {{82.f,1.5f,0.5f,32.8f}}))},
            // 18 DryJungle.
            {"Vegetation olive profonde et litiere seche, roche chaude patinee.",
             {{"Couvert olive", "Litiere seche", "Roche chaude", "Patine olive"}},
             biome(
                surface(material(0x3B462A,0x475331,0x546039, {{.91f,.95f,.98f,.75f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x51432B,0x5D4E32,0x6B5B3B, {{.86f,.92f,.96f,.85f}}, {{0.55f,0.45f,0.f,0.f}}, 0.82f),
                        {{0.685f,0.20f,0.77f,0.23f}}, {{20.f,1.4f,1.1f,3.f}}),
                surface(material(0x4C4D37,0x595A40,0x67684B, {{.76f,.84f,.92f,1.05f}}, {{0.67f,0.2f,0.13f,0.f}}, 0.82f),
                        material(0x606346,0x6D7050,0x7C7D5B, {{.85f,.92f,.96f,.7f}}, {{0.52f,0.4f,0.08f,0.f}}, 0.82f),
                        {{0.680f,0.20f,0.55f,0.45f}}, {{94.f,1.4f,-0.6f,37.6f}}))},
            // 19 TropicalJungle.
            {"Humus vert sombre, mousses jade et roche humide avec une patine vegetale.",
             {{"Humus tropical", "Mousse jade", "Roche tropicale", "Patine vegetale"}},
             biome(
                surface(material(0x2B4433,0x34523B,0x406147, {{0.65f,0.70f,0.76f,.75f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x405E3A,0x4D6B43,0x5A784F, {{0.78f,0.83f,0.88f,.6f}}, {{0.55f,0.45f,0.f,0.f}}, 0.82f),
                        {{0.665f,0.20f,0.72f,0.28f}}, {{26.f,1.6f,1.4f,4.f}}),
                surface(material(0x33493F,0x3E574B,0x4B6557, {{0.60f,0.66f,0.73f,.95f}}, {{0.62f,0.3f,0.08f,0.f}}, 0.82f),
                        material(0x526751,0x60765D,0x6F856B, {{0.76f,0.82f,0.88f,.7f}}, {{0.6f,0.3f,0.1f,0.f}}, 0.82f),
                        {{0.720f,0.20f,0.65f,0.35f}}, {{125.f,1.6f,0.7f,60.f}}))},
            // 20 VolcanicDesert: dark still has readable albedo; no fake metallic basalt.
            {"Basalte charbon, cendres bleu gris et rares oxydes chauds sans emission artificielle.",
             {{"Basalte charbon", "Cendre froide", "Basalte des volcans", "Oxydes ferriques"}},
             biome(
                surface(material(0x252A31,0x30363E,0x3D444D, {{.73f,.82f,.91f,.95f}}, {{0.825f,0.15f,0.025f,0.f}}, 0.95f,1.095f),
                        material(0x454A4F,0x51575B,0x5E6466, {{.91f,.95f,.98f,.6f}}, {{0.8f,0.2f,0.f,0.f}}, 0.95f),
                        {{0.735f,0.21f,0.92f,0.08f}}, {{20.f,1.8f,0.4f,4.f}}),
                surface(material(0x292D34,0x343A42,0x414851, {{.59f,.72f,.87f,1.2f}}, {{0.65f,0.2f,0.15f,0.f}}, 0.82f,1.109f),
                        material(0x554036,0x624A3E,0x705749, {{.81f,.89f,.95f,.9f}}, {{0.5f,0.25f,0.25f,0.f}}, 0.82f),
                        {{0.760f,0.20f,0.7f,0.3f}}, {{72.f,1.8f,-0.45f,28.8f}}))},
            // 21 RedDesert: the strongest warm palette, with a quiet fine-scale response.
            {"Dunes terre cuite, sable ambre et gres rouge aux bandes cuivre sourd.",
             {{"Sable rouge", "Sable ambre", "Gres rouge", "Gres ambre"}},
             biome(
                surface(material(0x914C36,0xA45B3D,0xB76D49, {{.84f,.89f,.94f,.65f}}, {{0.7f,0.f,0.f,0.3f}}, 0.95f,1.074f,.035f),
                        material(0xA1633E,0xB27448,0xC38556, {{.84f,.91f,.95f,.55f}}, {{0.8f,0.f,0.f,0.2f}}, 0.95f),
                        {{0.705f,0.24f,0.97f,0.03f}}, {{18.f,2.2f,-0.7f,1.6f}}),
                surface(material(0x814333,0x934F3A,0xA65E45, {{.81f,.88f,.94f,1.05f}}, {{0.52f,0.15f,0.33f,0.f}}, 0.82f,1.102f),
                        material(0xA26B4B,0xB17A58,0xC18B66, {{.84f,.91f,.96f,.75f}}, {{0.48f,0.1f,0.42f,0.f}}, 0.82f),
                        {{0.690f,0.20f,0.6f,0.4f}}, {{52.f,2.2f,0.3f,20.8f}}))},
            // 22 AridPlateau.
            {"Poussiere de gres ocre, croutes brunes et falaises aux strates larges.",
             {{"Poussiere du plateau", "Croute ocre", "Gres du plateau", "Bancs ambres"}},
             biome(
                surface(material(0x8B7859,0x9C8867,0xAD9977, {{.9f,.94f,.97f,.65f}}, {{0.8f,0.1f,0.f,0.1f}}, 0.95f),
                        material(0x8D7046,0x9C7E50,0xAC8E5E, {{.85f,.91f,.96f,.85f}}, {{0.6f,0.35f,0.05f,0.f}}, 0.82f),
                        {{0.725f,0.20f,0.82f,0.18f}}, {{32.f,2.5f,0.2f,2.f}}),
                surface(material(0x806B4D,0x917A59,0xA28B68, {{.8f,.88f,.94f,1.1f}}, {{0.49f,0.15f,0.36f,0.f}}, 0.82f,1.109f),
                        material(0xA18F6E,0xB1A07E,0xC0AF8E, {{.85f,.92f,.97f,.75f}}, {{0.46f,0.1f,0.44f,0.f}}, 0.82f,1.109f),
                        {{0.680f,0.20f,0.65f,0.35f}}, {{125.f,2.5f,-0.25f,50.f}}))},
            // 23 BurningJungle.
            {"Sous-bois bronze sombre, cendres chaudes et roche patinee de cuivre mat.",
             {{"Sous-bois bronze", "Cendre chaude", "Roche sombre", "Patine cuivre"}},
             biome(
                surface(material(0x373D2B,0x434A32,0x51573B, {{.85f,.91f,.96f,.8f}}, {{0.8f,0.2f,0.f,0.f}}),
                        material(0x5A4B36,0x67553D,0x746246, {{.91f,.95f,.98f,.7f}}, {{0.7f,0.3f,0.f,0.f}}, 0.82f),
                        {{0.695f,0.20f,0.82f,0.18f}}, {{24.f,1.6f,1.2f,4.f}}),
                surface(material(0x403D30,0x4C4838,0x5A5442, {{.72f,.82f,.91f,1.1f}}, {{0.61f,0.25f,0.14f,0.f}}, 0.82f),
                        material(0x65503A,0x735C42,0x82694D, {{.8f,.88f,.95f,.85f}}, {{0.48f,0.25f,0.27f,0.f}}, 0.82f),
                        {{0.690f,0.20f,0.65f,0.35f}}, {{88.f,1.6f,0.5f,35.2f}}))},
            // 24 TropicalMangrove.
            {"Vase vert brun sombre, sediments secs et roche humide couleur jade sourd.",
             {{"Vase de mangrove", "Sediments secs", "Roche jade", "Patine de lichen"}},
             biome(
                surface(material(0x394B36,0x3F533D,0x485C46, {{0.62f,0.68f,0.74f,.55f}}, {{0.75f,0.25f,0.f,0.f}}, 0.95f,1.074f,.025f),
                        material(0x45563C,0x4E5F43,0x58694B, {{0.76f,0.81f,0.86f,.5f}}, {{0.6f,0.4f,0.f,0.f}}, 0.82f),
                        {{0.775f,0.20f,0.82f,0.18f}}, {{21.f,1.8f,1.7f,4.f}}),
                surface(material(0x345242,0x3A5A49,0x426351, {{0.60f,0.66f,0.73f,.9f}}, {{0.69f,0.25f,0.06f,0.f}}, 0.82f),
                        material(0x425E46,0x4C6950,0x57745B, {{0.76f,0.81f,0.87f,.65f}}, {{0.49f,0.45f,0.06f,0.f}}, 0.82f),
                        {{0.690f,0.20f,0.55f,0.45f}}, {{110.f,1.8f,-0.6f,52.8f}}))},
            // 25 Marine: seabed material, never a water surface or baked depth fog.
            {"Sable sous-marin desature et roche vert ardoise, sans brume peinte dans la couleur.",
             {{"Sable marin", "Sediments sombres", "Roche marine", "Depots calcaires"}},
             biome(
                surface(material(0x69776C,0x79877A,0x8B9889, {{.81f,.87f,.93f,.55f}}, {{0.7f,0.f,0.f,0.3f}}, 0.95f,1.067f,.025f),
                        material(0x495F54,0x566C5F,0x647A6C, {{.65f,.76f,.88f,.7f}}, {{0.75f,0.25f,0.f,0.f}}, 0.90f),
                        {{0.685f,0.21f,0.87f,0.13f}}, {{28.f,2.1f,0.6f,1.8f}}),
                surface(material(0x3B524D,0x48605A,0x576F66, {{.55f,.69f,.83f,1.f}}, {{0.67f,0.2f,0.13f,0.f}}, 0.82f),
                        material(0x738678,0x839787,0x94A897, {{.69f,.81f,.92f,.65f}}, {{0.48f,0.3f,0.22f,0.f}}, 0.82f),
                        {{0.720f,0.20f,0.6f,0.4f}}, {{93.f,2.1f,-0.4f,37.2f}}))}
        }};
        return data;
    }

    struct SurfaceProfile
    {
        V groundNormal;
        V wallNormal;
        V spectrum;
    };

    // The feature normal is independent of the albedo ramp and its broad masks.
    // normal = family, feature period (m), warp fraction, fine-grain mix.
    // spectrum = patch-period multiplier, fine period (m), octaves, octave gain.
    // Family 0 uses the fine period directly; its feature-period slot is unused.
    // 0 grain, 1 wind ribs, 2 bedding, 3 fractures, 4 fibres, 5 wet folds,
    // 6 ice veins, 7 ropy lava. Modest gains prevent successive octaves from
    // contributing equally strong slopes. These are artistic material motifs,
    // not a second terrain-height field or a geological simulation.
    constexpr std::array<SurfaceProfile, BiomeCount> profiles{{
        //                         Ground normal            Wall normal              Spectrum
        /*  0 PolarIce */          {{{1.f,1.8f,.65f,.15f}},  {{6.f,4.5f,.35f,.015f}}, {{1.65f,.065f,2.f,.30f}}},
        /*  1 FrozenTundra */      {{{0.f,.18f,.40f,.50f}},  {{2.f,1.7f,.32f,.18f}},  {{1.18f,.110f,2.f,.32f}}},
        /*  2 Tundra */            {{{4.f,.28f,.65f,.45f}},  {{3.f,.85f,.40f,.20f}},  {{.84f,.120f,2.f,.34f}}},
        /*  3 Taiga */             {{{4.f,.17f,.85f,.35f}},  {{3.f,2.4f,.30f,.12f}},  {{1.42f,.140f,2.f,.31f}}},
        /*  4 BorealBog */         {{{5.f,.45f,.80f,.08f}},  {{5.f,.90f,.30f,.05f}},  {{.68f,.120f,1.f,.30f}}},
        /*  5 RockyTundra */       {{{0.f,.22f,.40f,.60f}},  {{3.f,1.15f,.22f,.50f}}, {{1.06f,.180f,3.f,.28f}}},
        /*  6 ColdSteppe */        {{{1.f,.42f,.85f,.30f}},  {{2.f,2.6f,.52f,.14f}},  {{2.15f,.090f,2.f,.32f}}},
        /*  7 BorealForest */      {{{4.f,.23f,.75f,.40f}},  {{2.f,1.45f,.40f,.12f}}, {{.93f,.110f,2.f,.30f}}},
        /*  8 ColdForest */        {{{4.f,.32f,.50f,.30f}},  {{3.f,2.15f,.45f,.13f}}, {{1.27f,.160f,2.f,.33f}}},
        /*  9 ColdMarsh */         {{{5.f,.75f,.65f,.07f}},  {{5.f,1.4f,.50f,.06f}},  {{.74f,.140f,1.f,.30f}}},
        /* 10 DryPlain */          {{{0.f,.15f,.20f,.50f}},  {{2.f,1.1f,.35f,.20f}},  {{2.30f,.070f,2.f,.30f}}},
        /* 11 Meadow */            {{{4.f,.12f,.70f,.32f}},  {{3.f,.95f,.30f,.20f}},  {{.88f,.090f,2.f,.32f}}},
        /* 12 TemperateForest */   {{{4.f,.19f,.65f,.38f}},  {{3.f,1.35f,.50f,.18f}}, {{1.12f,.130f,2.f,.30f}}},
        /* 13 TemperateRainforest*/{{{4.f,.14f,.95f,.30f}},  {{5.f,.50f,.65f,.05f}},  {{.62f,.120f,2.f,.28f}}},
        /* 14 TemperateMarsh */    {{{5.f,.36f,.55f,.06f}},  {{2.f,.90f,.60f,.07f}},  {{1.48f,.110f,1.f,.30f}}},
        /* 15 RockDesert */        {{{0.f,.32f,.30f,.62f}},  {{2.f,1.6f,.25f,.40f}},  {{1.95f,.160f,3.f,.30f}}},
        /* 16 DrySavanna */        {{{4.f,.33f,.90f,.42f}},  {{2.f,1.8f,.40f,.18f}},  {{2.65f,.170f,2.f,.31f}}},
        /* 17 Savanna */           {{{4.f,.22f,.65f,.40f}},  {{3.f,1.4f,.35f,.20f}},  {{1.75f,.150f,2.f,.34f}}},
        /* 18 DryJungle */         {{{4.f,.27f,.85f,.50f}},  {{3.f,1.1f,.55f,.16f}},  {{1.08f,.180f,2.f,.30f}}},
        /* 19 TropicalJungle */    {{{4.f,.16f,.95f,.32f}},  {{5.f,.65f,.75f,.06f}},  {{.79f,.105f,2.f,.29f}}},
        /* 20 VolcanicDesert */    {{{7.f,.80f,.45f,.15f}},  {{3.f,.75f,.25f,.55f}},  {{.91f,.130f,3.f,.28f}}},
        /* 21 RedDesert */         {{{1.f,.38f,.85f,.30f}},  {{2.f,1.25f,.40f,.20f}}, {{3.20f,.035f,2.f,.32f}}},
        /* 22 AridPlateau */       {{{3.f,.65f,.40f,.38f}},  {{2.f,2.8f,.18f,.15f}},  {{2.85f,.120f,2.f,.30f}}},
        /* 23 BurningJungle */     {{{0.f,.18f,.30f,.55f}},  {{7.f,.55f,.80f,.15f}},  {{1.33f,.100f,2.f,.31f}}},
        /* 24 TropicalMangrove */  {{{5.f,.55f,.95f,.04f}},  {{5.f,1.05f,.55f,.05f}}, {{.57f,.150f,1.f,.30f}}},
        /* 25 Marine */            {{{1.f,.24f,.50f,.25f}},  {{2.f,.70f,.40f,.10f}},  {{2.45f,.050f,2.f,.30f}}}
    }};
}

ProceduralTerrain::TerrainAppearance makeTerrainAppearance()
{
    ProceduralTerrain::TerrainAppearance appearance;
    // Per-biome multipliers vary this reference period from 55 to 307 m.
    // Fine normal periods and spectra remain independent of color-mask scales.
    appearance.patchPeriod = 96.0f;
    const auto& authored = recipes();
    for (std::size_t i = 0; i < authored.size(); ++i)
    {
        appearance.biomes[i] = authored[i].appearance;
        appearance.biomes[i].ground.normal = profiles[i].groundNormal;
        appearance.biomes[i].wall.normal = profiles[i].wallNormal;
        appearance.biomes[i].spectrum = profiles[i].spectrum;
    }
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
