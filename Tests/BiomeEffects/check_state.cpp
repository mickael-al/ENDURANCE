// CPU-only tests of production catalogue/selection. No renderer substitutes,
// random sampling, GL context or frame-time assumptions are needed here.
#include "EnduranceBiomeEffectsState.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace E = EnduranceEnvironment;
namespace {
void require(bool value, const std::string& message)
{
    if (!value) throw std::runtime_error(message);
}
bool finite(float value) { return std::isfinite(value); }
void finiteVector(glm::vec3 value, const std::string& label)
{
    require(finite(value.x) && finite(value.y) && finite(value.z), label);
}
bool near(float a, float b, float tolerance = 0.00002f) { return std::abs(a - b) <= tolerance; }
std::size_t index(E::EffectId id) { return static_cast<std::size_t>(id); }
E::EffectMask liveMask(const E::EffectsState& state)
{
    E::EffectMask mask = 0;
    for (std::size_t i = 0; i < E::EffectCount; ++i)
    {
        const float value = state.strengths()[i];
        require(finite(value) && value >= 0.0f && value <= 1.0f, "Strength is outside finite [0,1]");
        if (value != 0.0f) mask |= E::effectBit(static_cast<E::EffectId>(i));
    }
    return mask;
}
void settle(E::EffectsState& state, float seconds = 20.0f)
{
    const unsigned steps = static_cast<unsigned>(std::ceil(seconds * 20.0f));
    for (unsigned i = 0; i < steps; ++i) state.update(0.05f);
}
std::vector<E::EffectId> maskIds(E::EffectMask mask)
{
    std::vector<E::EffectId> result;
    for (std::size_t i = 0; i < E::EffectCount; ++i)
        if (mask & E::effectBit(static_cast<E::EffectId>(i))) result.push_back(static_cast<E::EffectId>(i));
    return result;
}

void catalogue()
{
    const auto& definitions = E::effectDefinitions();
    const auto& matrix = E::biomeEffectMatrix();
    require(definitions.size() == 40 && matrix.size() == 26, "Catalogue shape changed");
    std::set<std::string> keys, recipes;
    unsigned passiveCount = 0, activeCount = 0;
    E::EffectMask passiveIds = 0, activeIds = 0;
    for (std::size_t i = 0; i < definitions.size(); ++i)
    {
        const auto& d = definitions[i];
        const auto& p = d.particles;
        const auto& g = p.group;
        const auto& s = d.screen;
        const std::string label = "Effect " + std::to_string(i) + ": ";
        require(index(d.id) == i, label + "ID does not match lookup index");
        require(d.key && *d.key && d.name && *d.name, label + "Missing stable key/display name");
        require(keys.insert(d.key).second, label + "Duplicate key");
        require(E::findEffect(d.key) == &d, label + "Key lookup returns a different recipe");
        require(finite(d.fadeIn) && finite(d.fadeOut) && d.fadeIn >= .05f && d.fadeOut >= .05f,
                label + "Invalid fade times");
        require(p.count > 0 || s.features() != 0, label + "Effect has no visual implementation");
        if (d.activeEvent) { ++activeCount; activeIds |= E::effectBit(d.id); }
        else { ++passiveCount; passiveIds |= E::effectBit(d.id); }
        for (float value : {s.heat, s.frost, s.wet, s.condensation, s.dust, s.flash})
            require(finite(value) && value >= 0.0f && value <= 1.0f, label + "Invalid screen channel");
        finiteVector(s.tint, label + "Invalid screen tint");
        require(finite(s.scale) && s.scale > 0 && finite(s.speed) && s.speed >= 0, label + "Invalid screen motion");
        require(static_cast<unsigned>(p.shape) < static_cast<unsigned>(E::ParticleShape::Count), label + "Invalid sprite shape");
        require(p.count <= 32768, label + "Unbounded single-effect particle pool");
        if (p.count)
        {
            require(finite(g.lifeTimeMin) && finite(g.lifeTimeMax) && g.lifeTimeMin > 0
                && g.lifeTimeMax >= g.lifeTimeMin, label + "Invalid lifetime");
            require(g.nbColor > 0 && g.nbColor <= 10, label + "Invalid color count");
            bool visible = false;
            for (unsigned color = 0; color < g.nbColor; ++color)
            {
                const auto c = g.colorGradient[color];
                require(finite(c.r) && finite(c.g) && finite(c.b) && finite(c.a), label + "Nonfinite gradient");
                require(c.r >= 0 && c.g >= 0 && c.b >= 0 && c.a >= 0 && c.a <= 1, label + "Invalid gradient range");
                visible |= c.a > 0;
            }
            require(visible, label + "All particle opacity values are zero");
            finiteVector(g.emitPosition, label + "Invalid emitter offset");
            finiteVector(g.emitDirection, label + "Invalid emitter direction");
            finiteVector(g.emitScaleMin, label + "Invalid emission minimum");
            finiteVector(g.emitScaleMax, label + "Invalid emission maximum");
            finiteVector(g.gravity, label + "Invalid gravity");
            finiteVector(g.externalForce, label + "Invalid wind force");
            for (unsigned axis = 0; axis < 3; ++axis)
                require(g.emitScaleMax[axis] >= g.emitScaleMin[axis], label + "Inverted emission volume");
            require(g.velocityMax >= g.velocityMin && finite(g.velocityMax) && finite(g.velocityMin), label + "Invalid speed interval");
            require(g.scaleStart >= 0 && g.scaleEnd >= 0 && finite(g.scaleStart) && finite(g.scaleEnd), label + "Invalid size");
            require(finite(p.aspect) && p.aspect > 0 && finite(p.emission) && p.emission >= 0, label + "Invalid sprite properties");
            require(finite(p.turbulence) && finite(p.turbulenceScale) && p.turbulenceScale > 0
                && finite(p.pulse) && finite(p.windScale), label + "Invalid animation controls");
        }
        std::ostringstream signature;
        signature << p.count << ',' << static_cast<unsigned>(p.shape) << ',' << p.aspect << ',' << p.emission << ','
            << p.turbulence << ',' << p.turbulenceScale << ',' << p.pulse << ',' << p.windScale << ','
            << g.lifeTimeMin << ',' << g.lifeTimeMax << ',' << g.velocityMin << ',' << g.velocityMax << ','
            << g.scaleStart << ',' << g.scaleEnd << ',' << g.flags << ','
            << s.heat << ',' << s.frost << ',' << s.wet << ',' << s.condensation << ',' << s.dust << ',' << s.flash;
        for (unsigned c = 0; p.count && c < g.nbColor; ++c)
            for (unsigned a = 0; a < 4; ++a) signature << ',' << g.colorGradient[c][a];
        require(recipes.insert(signature.str()).second, label + "Another effect has the same visual/motion recipe");
    }
    require(passiveCount == 28 && activeCount == 12, "Expected 28 passive and 12 explicit events");
    require(E::findEffect(nullptr) == nullptr && E::findEffect("") == nullptr
        && E::findEffect("not_an_effect") == nullptr, "Unknown key lookup must be safe");
    require(E::effectBit(E::EffectId::Count) == 0 && E::effectBit(static_cast<E::EffectId>(255)) == 0,
            "Invalid ID must not shift outside effect mask");
    E::EffectMask assignedPassive = 0, assignedActive = 0;
    unsigned maxParticles = 0;
    for (std::size_t b = 0; b < matrix.size(); ++b)
    {
        const auto& m = matrix[b];
        require((m.passive & ~passiveIds) == 0 && (m.active & ~activeIds) == 0,
                "Biome mixes passive/event categories or references invalid bits");
        require(m.passive != 0 && m.active != 0, "Biome has no passive or eligible event assignment");
        assignedPassive |= m.passive; assignedActive |= m.active;
        unsigned particles = 0;
        for (auto id : maskIds(m.passive)) particles += definitions[index(id)].particles.count;
        maxParticles = (std::max)(particles, maxParticles);
        require(particles <= 4096, "Biome passive particles exceed conservative ambient budget");
    }
    require(assignedPassive == passiveIds && assignedActive == activeIds, "Some of the 40 effects are unreachable from the biome matrix");
    std::cout << "PASS catalogue: 40 distinct finite visual recipes, 28 passive/12 events, all 26 biome mappings; max ambient particles "
        << maxParticles << '\n';
}

void passiveSelection()
{
    E::EffectsState state;
    require(state.biome() == -1 && liveMask(state) == 0, "Cold state should be fully dormant");
    settle(state, 100.0f);
    require(liveMask(state) == 0 && state.activeEventCount() == 0, "Idle state started an effect");
    for (unsigned b = 0; b < ProceduralTerrain::BiomeCount; ++b)
    {
        state.setBiome(static_cast<int>(b));
        settle(state);
        require(state.biome() == static_cast<int>(b), "Biome selection lost ID");
        require(liveMask(state) == E::biomeEffectMatrix()[b].passive,
                "After transition, effects survive outside the dominant biome");
        for (auto id : maskIds(E::biomeEffectMatrix()[b].passive))
            require(state.strengths()[index(id)] == 1.0f, "Passive effect did not reach full strength");
        state.update(3600.0f);
        require(state.activeEventCount() == 0 && liveMask(state) == E::biomeEffectMatrix()[b].passive,
                "Time or biome entry automatically triggered active weather");
    }
    for (int invalid : {-1, -999, 26, 999})
    {
        state.setBiome(invalid); settle(state);
        require(state.biome() == -1 && liveMask(state) == 0 && state.activeEventCount() == 0,
                "Missing/invalid biome did not become completely dormant");
    }
    std::cout << "PASS only dominant passive effects, all biome transitions, no timer-triggered events, exact-zero fade cleanup\n";
}

void eventLifecycle()
{
    const auto& definitions = E::effectDefinitions();
    const auto& matrix = E::biomeEffectMatrix();
    for (unsigned b = 0; b < matrix.size(); ++b)
    for (const auto& d : definitions)
    {
        E::EffectsState state; state.setBiome(static_cast<int>(b));
        const bool eligible = d.activeEvent && (matrix[b].active & E::effectBit(d.id)) != 0;
        require(state.triggerEvent(d.id, 12.0f, .6f) == eligible, "Event eligibility disagrees with biome assignment");
        if (!eligible) { require(state.activeEventCount() == 0, "Rejected event modified active count"); continue; }
        require(state.activeEventCount() == 1 && near(state.remaining(d.id), 12.0f), "Event did not start");
        settle(state, 5.0f);
        require(near(state.strengths()[index(d.id)], .6f), "Event intensity/fade-in did not settle");
        settle(state, 20.0f);
        require(state.remaining(d.id) == 0 && state.activeEventCount() == 0
            && state.strengths()[index(d.id)] == 0, "Expired event did not release its visual strength");
        require(state.triggerEvent(d.id, 100.0f), "Event could not restart");
        settle(state, 5.0f);
        state.stopEvent(d.id); settle(state);
        require(state.activeEventCount() == 0 && state.strengths()[index(d.id)] == 0, "Manual stop did not fade to zero");
        require(state.triggerEvent(d.id, 100.0f), "Stopped event could not restart");
        state.setBiome(-1); settle(state);
        require(liveMask(state) == 0 && state.activeEventCount() == 0, "Lost biome retained weather");
    }
    E::EffectsState state;
    unsigned biome = 0;
    for (; biome < matrix.size(); ++biome) if (maskIds(matrix[biome].active).size() >= 3) break;
    require(biome < matrix.size(), "No biome can exercise concurrent-event budget");
    state.setBiome(static_cast<int>(biome));
    const auto allowed = maskIds(matrix[biome].active);
    require(state.triggerEvent(allowed[0], 20) && state.triggerEvent(allowed[1], 20), "Two-event budget rejected legal events");
    require(!state.triggerEvent(allowed[2], 20), "Third concurrent event exceeded budget");
    require(state.triggerEvent(allowed[0], 42, .25f) && near(state.remaining(allowed[0]), 42), "Retargeting running event consumed a new slot");
    state.stopEvent(allowed[1]);
    require(state.triggerEvent(allowed[2], 20), "Stopped event did not free event slot");
    state.stopEvents(); settle(state);
    require(state.activeEventCount() == 0 && liveMask(state) == matrix[biome].passive, "stopEvents affected passives or retained events");
    require(state.triggerEvent(allowed[0], 10000, 4), "Finite oversized settings should clamp safely");
    require(state.remaining(allowed[0]) == 600, "Unbounded event lifetime");
    settle(state, 5);
    require(state.strengths()[index(allowed[0])] == 1, "Unbounded event intensity");
    std::cout << "PASS exhaustive event/biome eligibility, lifetime/stop/restart, two-event cap, retargeting and clamps\n";
}

void previewsAndInputs()
{
    E::EffectsState state;
    state.setBiome(12); settle(state);
    for (const auto& d : E::effectDefinitions())
    {
        require(state.previewEffect(d.id, 10.0f), "Valid explicit preview was rejected");
        settle(state, 6.0f);
        require(state.previewing() && liveMask(state) == E::effectBit(d.id)
            && state.strengths()[index(d.id)] == 1.0f, std::string("Preview does not isolate selected effect: ") + d.key);
        settle(state, 20.0f);
        require(!state.previewing() && liveMask(state) == E::biomeEffectMatrix()[12].passive,
                "Expired preview failed to return to normal passive set");
    }
    require(state.previewEffect(E::EffectId::Sandstorm, 10), "Preview of off-biome event must be explicitly available to editor");
    state.stopPreview(); settle(state);
    require(liveMask(state) == E::biomeEffectMatrix()[12].passive, "Manual preview stop retained preview");
    const auto baseline = state.strengths();
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto inf = std::numeric_limits<float>::infinity();
    for (float dt : {0.0f, -1.0f, nan, inf}) state.update(dt);
    require(state.strengths() == baseline, "Invalid delta time changed state");
    for (float seconds : {0.0f, -1.0f, nan, inf})
    {
        require(!state.previewEffect(E::EffectId::Fireflies, seconds), "Invalid preview duration accepted");
        require(!state.triggerEvent(E::EffectId::Rain, seconds), "Invalid event duration accepted");
    }
    for (float strength : {0.0f, -1.0f, nan, inf})
        require(!state.triggerEvent(E::EffectId::Rain, 10, strength), "Invalid event intensity accepted");
    for (auto id : {E::EffectId::Count, static_cast<E::EffectId>(255)})
    {
        require(!state.triggerEvent(id, 10) && !state.previewEffect(id, 10), "Invalid ID accepted");
        require(state.remaining(id) == 0, "Invalid ID read outside event array");
        state.stopEvent(id);
    }
    state.clear();
    require(state.biome() == -1 && state.activeEventCount() == 0 && !state.previewing() && liveMask(state) == 0,
            "clear did not reset all state");
    std::cout << "PASS all 40 explicit previews, bounded return, malformed inputs, invalid IDs and reset\n";
}

void timestep()
{
    E::EffectsState coarse, fine;
    for (int biome : {0, 20, 12, 25, -1})
    {
        coarse.setBiome(biome); fine.setBiome(biome);
        for (int i = 0; i < 10; ++i) coarse.update(.1f);
        for (int i = 0; i < 100; ++i) fine.update(.01f);
        for (std::size_t i = 0; i < E::EffectCount; ++i)
            require(near(coarse.strengths()[i], fine.strengths()[i]), "Passive fade depends on frame rate");
    }
    for (const auto& d : E::effectDefinitions())
    {
        E::EffectsState previewCoarse, previewFine;
        previewCoarse.setBiome(12); previewFine.setBiome(12);
        require(previewCoarse.previewEffect(d.id, .3f) && previewFine.previewEffect(d.id, .3f), "Timestep preview setup failed");
        previewCoarse.update(10.0f);
        for (int i = 0; i < 1000; ++i) previewFine.update(.01f);
        require(!previewCoarse.previewing() && !previewFine.previewing(), "Expired preview remained active after long frame");
        for (std::size_t i = 0; i < E::EffectCount; ++i)
            require(near(previewCoarse.strengths()[i], previewFine.strengths()[i], .0001f),
                    "Preview expiry/fade differs between single long frame and short steps");
        if (!d.activeEvent) continue;
        unsigned biome = 0;
        while (biome < ProceduralTerrain::BiomeCount && (E::biomeEffectMatrix()[biome].active & E::effectBit(d.id)) == 0) ++biome;
        require(biome < ProceduralTerrain::BiomeCount, "Timestep event has no eligible biome");
        E::EffectsState eventCoarse, eventFine;
        eventCoarse.setBiome(static_cast<int>(biome)); eventFine.setBiome(static_cast<int>(biome));
        require(eventCoarse.triggerEvent(d.id, .3f) && eventFine.triggerEvent(d.id, .3f), "Timestep event setup failed");
        eventCoarse.update(10.0f);
        for (int i = 0; i < 1000; ++i) eventFine.update(.01f);
        require(eventCoarse.activeEventCount() == 0 && eventFine.activeEventCount() == 0, "Expired event remained active after long frame");
        for (std::size_t i = 0; i < E::EffectCount; ++i)
            require(near(eventCoarse.strengths()[i], eventFine.strengths()[i], .0001f),
                    "Event expiry/fade differs between single long frame and short steps");
    }
    std::cout << "PASS frame-rate-independent passive fades and event/preview expiry, including 10-second frames\n";
}
}

int main()
{
    try { catalogue(); passiveSelection(); eventLifecycle(); previewsAndInputs(); timestep(); }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    std::cout << "PASS biome effects production catalogue/state\n";
}
