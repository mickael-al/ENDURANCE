#include "EnduranceTerrainMaterialPanel.hpp"
#include "EnduranceTerrainMaterials.hpp"
#include "imgui-cmake/Header/imgui.h"
#include <algorithm>
#include <cmath>

namespace
{
    bool boundedSlider(const char* label, float* value, float low, float high)
    {
        return ImGui::SliderFloat(label, value, low, high, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    }
    bool boundedDrag(const char* label, float* value, float speed, float low, float high, const char* format)
    {
        return ImGui::DragFloat(label, value, speed, low, high, format, ImGuiSliderFlags_AlwaysClamp);
    }
    float toDisplay(float v)
    {
        return v <= 0.0031308f ? 12.92f * v : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    }
    float toLinear(float v)
    {
        return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    }

    bool paletteStop(const char* label, ProceduralTerrain::TerrainMaterialVector& stop)
    {
        ImGui::PushID(label);
        float display[3] = {toDisplay(stop[0]), toDisplay(stop[1]), toDisplay(stop[2])};
        bool changed = ImGui::ColorEdit3(label, display);
        if (changed)
            for (unsigned int i = 0; i < 3; ++i) stop[i] = toLinear(std::clamp(display[i], 0.0f, 1.0f));
        changed |= boundedSlider("Rugosite", &stop[3], 0.04f, 1.0f);
        ImGui::PopID();
        return changed;
    }

    bool materialEditor(const char* label, ProceduralTerrain::TerrainMaterial& m)
    {
        if (!ImGui::TreeNode(label)) return false;
        bool changed = paletteStop("Couleur basse", m.colorLow);
        changed |= paletteStop("Couleur intermediaire", m.colorMid);
        changed |= paletteStop("Couleur haute", m.colorHigh);
        ImGui::TextDisabled("Couleurs affichees en sRGB, melange et eclairage en lineaire.");
        changed |= boundedSlider("Position couleur intermediaire", &m.ramp[0], 0.05f, 0.95f);
        changed |= boundedSlider("Douceur des transitions", &m.ramp[1], 0.0f, 1.0f);
        changed |= boundedSlider("Contraste de palette", &m.ramp[2], 0.0f, 8.0f);
        changed |= boundedSlider("Decalage de palette", &m.ramp[3], -1.0f, 1.0f);
        changed |= boundedSlider("Metallic", &m.response[0], 0.0f, 1.0f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 pour les sols, roches, neige et glace. Les reflets viennent aussi de la rugosite.");
        changed |= boundedSlider("AO de la matiere", &m.response[1], 0.0f, 1.0f);
        changed |= boundedSlider("Force du relief", &m.response[2], 0.0f, 4.0f);
        changed |= boundedSlider("Variation de rugosite", &m.response[3], 0.0f, 1.0f);
        ImGui::TextUnformatted("Contributions au motif (poids relatifs)");
        changed |= boundedSlider("Grandes variations", &m.pattern[0], 0.0f, 4.0f);
        changed |= boundedSlider("Plaques", &m.pattern[1], 0.0f, 4.0f);
        changed |= boundedSlider("Strates", &m.pattern[2], 0.0f, 4.0f);
        changed |= boundedSlider("Rides", &m.pattern[3], 0.0f, 4.0f);
        if (ImGui::TreeNode("Details fins"))
        {
            changed |= boundedSlider("Grains brillants", &m.fineSurface[0], 0.0f, 1.0f);
            changed |= boundedSlider("Rugosite des grains", &m.fineSurface[1], 0.04f, 1.0f);
            changed |= boundedSlider("Assombrissement des taches", &m.fineSurface[2], 0.0f, 1.0f);
            changed |= boundedSlider("Baisse de rugosite des taches", &m.fineSurface[3], 0.0f, 1.0f);
            ImGui::TextDisabled("0.10 = 10 %% plus sombre. Les grains reagissent aux lumieres et aux reflets.");
            ImGui::TreePop();
        }
        ImGui::TreePop();
        return changed;
    }

    bool surfaceEditor(const char* label, ProceduralTerrain::TerrainSurface& s,
        std::size_t biome, bool wall)
    {
        if (!ImGui::TreeNode(label)) return false;
        ImGui::TextWrapped("Principal : %s", Endurance::terrainMaterialLabel(biome, wall, false));
        ImGui::TextWrapped("Secondaire : %s", Endurance::terrainMaterialLabel(biome, wall, true));
        bool changed = materialEditor("Materiau principal", s.base);
        changed |= materialEditor("Materiau secondaire", s.accent);
        changed |= boundedSlider("Seuil du materiau secondaire", &s.blend[0], 0.0f, 1.0f);
        changed |= boundedSlider("Transition du masque", &s.blend[1], 0.0f, 1.0f);
        changed |= boundedSlider("Masque grandes variations", &s.blend[2], 0.0f, 4.0f);
        changed |= boundedSlider("Masque plaques", &s.blend[3], 0.0f, 4.0f);
        changed |= boundedDrag("Espacement strates (m)", &s.detail[0], 0.25f, 0.01f, 100000.0f, "%.2f");
        changed |= boundedDrag("Espacement rides (m)", &s.detail[1], 0.05f, 0.01f, 100000.0f, "%.2f");
        changed |= ImGui::SliderAngle("Orientation", &s.detail[2], -180.0f, 180.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
        changed |= boundedDrag("Deformation des motifs (m)", &s.detail[3], 0.1f, 0.0f, 10000.0f, "%.2f");
        ImGui::Separator();
        ImGui::TextUnformatted("Relief de surface, independant des couleurs");
        static const char* families[] = {"Grain mineral", "Rides du vent", "Strates rocheuses",
            "Fissures", "Fibres organiques", "Sediment humide", "Veines de glace", "Lave cordee"};
        int family = static_cast<int>(s.normal[0]);
        if (ImGui::Combo("Famille de normale", &family, families, 8))
        {
            s.normal[0] = static_cast<float>(family);
            changed = true;
        }
        ImGui::BeginDisabled(family == 0);
        changed |= boundedDrag("Taille du motif de normale (m)", &s.normal[1],
            0.02f, 0.01f, 10000.0f, "%.3f");
        changed |= boundedSlider("Deformation du relief", &s.normal[2], 0.0f, 2.0f);
        ImGui::EndDisabled();
        if (family == 0)
            ImGui::TextDisabled("Grain mineral : regler la taille du grain et les octaves du biome.");
        changed |= boundedSlider("Part du grain fin", &s.normal[3], 0.0f, 1.0f);
        ImGui::TreePop();
        return changed;
    }
}

bool Endurance::drawTerrainMaterialEditor(ProceduralTerrain::TerrainAppearance& savedAppearance,
    float& savedMicroDetailStrength, float& savedMicroDetailScale, int& selectedBiome)
{
    if (!ImGui::CollapsingHeader("Materiaux proceduraux / palettes")) return false;
    // Only the open editor copies this small table. Invalid typed input must
    // never replace saved data, including when the scene is stopped.
    auto appearance = savedAppearance;
    float microDetailStrength = savedMicroDetailStrength;
    float microDetailScale = savedMicroDetailScale;
    ImGui::TextWrapped("Reglages appliques en direct, sans regenerer le terrain. Sauvegarder la scene pour les conserver.");
    bool changed = boundedDrag("Echelle grandes variations (m)", &appearance.macroPeriod,
        4.0f, 1.0f, 100000.0f, "%.1f");
    changed |= boundedDrag("Echelle des plaques (m)", &appearance.patchPeriod,
        0.5f, 0.1f, 100000.0f, "%.1f");
    changed |= boundedSlider("Force globale du relief", &microDetailStrength, 0.0f, 2.0f);
    float grainFrequencyMultiplier = microDetailScale * 0.5f;
    if (boundedDrag("Multiplicateur frequence du grain", &grainFrequencyMultiplier,
        0.025f, 0.00005f, 500000.0f, "%.3f"))
    {
        microDetailScale = grainFrequencyMultiplier * 2.0f;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("1 conserve la taille propre a chaque biome. 2 divise cette taille par deux.");
    static const char* views[] = {"Materiau complet", "Palette", "Sol / paroi",
        "Materiau secondaire", "Rugosite", "Metallic / AO", "Normale"};
    changed |= ImGui::Combo("Vue de controle", &appearance.debugView, views, 7);
    selectedBiome = std::clamp(selectedBiome, 0, static_cast<int>(ProceduralTerrain::BiomeCount) - 1);
    if (ImGui::BeginCombo("Palette du biome", ProceduralTerrain::BiomeNames[selectedBiome]))
    {
        for (int i = 0; i < static_cast<int>(ProceduralTerrain::BiomeCount); ++i)
            if (ImGui::Selectable(ProceduralTerrain::BiomeNames[i], i == selectedBiome)) selectedBiome = i;
        ImGui::EndCombo();
    }
    ImGui::TextWrapped("%s", Endurance::terrainMaterialIntent(static_cast<std::size_t>(selectedBiome)));
    ImGui::TextDisabled("La selection edite la palette ; Biome a tester change la generation.");
    if (ImGui::Button("Restaurer les materiaux de ce biome"))
    {
        appearance.biomes[selectedBiome] = Endurance::makeTerrainAppearance().biomes[selectedBiome];
        changed = true;
    }
    if (ImGui::Button("Restaurer tous les prereglages de materiaux"))
    {
        appearance = Endurance::makeTerrainAppearance();
        microDetailStrength = 0.15f;
        microDetailScale = 2.0f;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remplace les palettes, motifs et reglages globaux des 26 biomes par les valeurs du jeu.");
    auto& biome = appearance.biomes[selectedBiome];
    changed |= boundedDrag("Multiplicateur taille des plaques", &biome.spectrum[0],
        0.05f, 0.05f, 64.0f, "%.2f");
    changed |= boundedDrag("Taille du grain du biome (m)", &biome.spectrum[1],
        0.005f, 0.01f, 1000.0f, "%.3f");
    changed |= boundedSlider("Octaves du grain", &biome.spectrum[2], 1.0f, 3.0f);
    changed |= boundedSlider("Gain des octaves", &biome.spectrum[3], 0.0f, 0.5f);
    ImGui::TextDisabled("Les octaves trop fines pour un pixel sont filtrees automatiquement.");
    changed |= surfaceEditor("Sol", biome.ground, selectedBiome, false);
    changed |= surfaceEditor("Parois et plafonds", biome.wall, selectedBiome, true);
    changed |= boundedSlider("Debut transition sol / paroi", &biome.slope[0], 0.0f,
        (std::max)(0.0f, biome.slope[1] - 0.001f));
    changed |= boundedSlider("Fin transition sol / paroi", &biome.slope[1],
        (std::min)(1.0f, biome.slope[0] + 0.001f), 1.0f);
    changed |= boundedSlider("Irregularite transition sol / paroi", &biome.slope[2], 0.0f, 1.0f);
    changed |= boundedSlider("Force du relief du biome", &biome.slope[3], 0.0f, 4.0f);
    if (changed)
    {
        if (!ProceduralTerrain::validTerrainAppearance(appearance)
            || !ProceduralTerrain::terrainMaterialRange(microDetailStrength, 0.0f, 16.0f)
            || !ProceduralTerrain::terrainMaterialRange(microDetailScale, 0.0001f, 1000000.0f))
        {
            ImGui::TextWrapped("Valeur invalide : les reglages precedents sont conserves.");
            return false;
        }
        savedAppearance = appearance;
        savedMicroDetailStrength = microDetailStrength;
        savedMicroDetailScale = microDetailScale;
    }
    return changed;
}
