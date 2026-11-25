// CPU-only tests of production catalogue/selection. No renderer substitutes,
// random sampling, GL context or frame-time assumptions are needed here.
#include "EnduranceBiomeEffectsState.hpp"
#include "EnduranceBiomeParticles.hpp"
#include "EnduranceEnvironmentMedium.hpp"
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
glm::vec2 productionParticleWind(glm::vec2 base, double seconds, float variation, float period);
EnduranceEnvironmentMediumState productionEnvironmentMedium(
    const std::array<float, E::EffectCount>& strengths, glm::vec2 windOffset);
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
        require(p.count > 0 || s.features() != 0 || d.medium.extinction > 0 || d.id == E::EffectId::WindStreaks,
                label + "Effect has no visual implementation");
        const auto& medium = d.medium;
        require(finite(medium.extinction) && medium.extinction >= 0,
                label + "Invalid medium extinction");
        finiteVector(medium.albedo, label + "Invalid medium albedo");
        for (unsigned axis = 0; axis < 3; ++axis)
            require(medium.albedo[axis] >= 0 && medium.albedo[axis] <= 1,
                    label + "Scattering albedo must not create energy");
        require(finite(medium.baseHeight) && finite(medium.heightScale) && medium.heightScale > 0
                && finite(medium.noiseAmount) && medium.noiseAmount >= 0 && medium.noiseAmount <= 1
                && finite(medium.noiseScale) && medium.noiseScale > 0,
                label + "Invalid medium height/noise controls");
        if (d.activeEvent) { ++activeCount; activeIds |= E::effectBit(d.id); }
        else { ++passiveCount; passiveIds |= E::effectBit(d.id); }
        for (float value : {s.heat, s.frost, s.wet, s.condensation, s.dust, s.flash})
            require(finite(value) && value >= 0.0f && value <= 1.0f, label + "Invalid screen channel");
        finiteVector(s.tint, label + "Invalid screen tint");
        require(finite(s.scale) && s.scale > 0 && finite(s.speed) && s.speed >= 0, label + "Invalid screen motion");
        require(static_cast<unsigned>(p.shape) < static_cast<unsigned>(E::ParticleShape::Count), label + "Invalid sprite shape");
        require(p.count <= 262144, label + "Unbounded single-effect base particle pool");
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
            << s.heat << ',' << s.frost << ',' << s.wet << ',' << s.condensation << ',' << s.dust << ',' << s.flash
            << ',' << medium.extinction << ',' << medium.baseHeight << ',' << medium.heightScale << ','
            << medium.noiseAmount << ',' << medium.noiseScale << ',' << medium.groundLayer;
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
        require(particles <= 131072, "Biome passive particles exceed ambient budget");
        std::vector<unsigned> eventCounts;
        for (auto id : maskIds(m.active)) eventCounts.push_back(definitions[index(id)].particles.count);
        std::sort(eventCounts.rbegin(), eventCounts.rend());
        unsigned peak = particles;
        for (unsigned i = 0; i < (std::min)(2u, static_cast<unsigned>(eventCounts.size())); ++i)
            peak += eventCounts[i];
        require(peak * 4 <= EnduranceBiomeParticles::MaxLiveParticles,
                "A legal biome plus two events is silently truncated at supported density4");
    }
    require(assignedPassive == passiveIds && assignedActive == activeIds, "Some of the 40 effects are unreachable from the biome matrix");
    std::cout << "PASS catalogue: 40 distinct finite visual recipes, 28 passive/12 events, all 26 biome mappings; max ambient particles "
        << maxParticles << '\n';
}

void extendedArtRecipes()
{
    // These are the reviewed artwork requirements relative to revision
    // 1d227f0. Population is deliberately NOT forced upward for every effect:
    // wider leaves and lighter spores must remain cheaper than dense storms.
    const auto& definitions = E::effectDefinitions();
    require(definitions[index(E::EffectId::WindStreaks)].particles.count == 0,
            "Wind ribbons must not allocate a generic particle group");
    const std::pair<E::EffectId, unsigned> rainCounts[] = {
        {E::EffectId::Rain, 30720}, {E::EffectId::HeavyRain, 61440},
        {E::EffectId::Thunderstorm, 46080}, {E::EffectId::Sleet, 35840}};
    for (const auto& entry : rainCounts)
    {
        const auto& rain = definitions[index(entry.first)].particles;
        require(rain.count == 4 * entry.second, "Rain populations must be quadrupled");
        require(rain.shape == E::ParticleShape::Streak && !(rain.group.flags & Ge::VelOrientation),
                "Rain must use its vertical rendering family without generic velocity rotation");
    }
    const auto& sand = definitions[index(E::EffectId::SandGust)].particles;
    require(sand.count == 2 * 30720, "Sand gust must double the previous population");
    require(near(sand.group.scaleStart, .007f) && near(sand.group.scaleEnd, .028f),
            "Sand gust sprite dimensions must retain the doubled visibility setting");
    require(near(sand.group.colorGradient[1].a, .32f * .75f),
            "Sand gust opacity must fall by 25 percent");
    struct ReachRequirement { E::EffectId id; unsigned count; float oldXZ; float oldY; };
    for (const auto& expected : std::array<ReachRequirement, 3>{{
        {E::EffectId::WindLeaves, 1920, 220, 7},
        {E::EffectId::Petals, 2080, 190, 5},
        {E::EffectId::DrySeeds, 3840, 220, 3}}})
    {
        const auto& p = definitions[index(expected.id)].particles;
        const auto& g = p.group;
        require(p.count == expected.count, "Widening leaf/petal/seed volumes increased population");
        const glm::vec3 extent = .5f * (g.emitScaleMax - g.emitScaleMin);
        require(extent.x > expected.oldXZ && extent.z > expected.oldXZ && extent.y >= expected.oldY,
                "Leaf/petal/seed volume was not widened in world space");
    }
    const auto& bloom = definitions[index(E::EffectId::SporeBloom)].particles;
    const auto& spores = definitions[index(E::EffectId::GlowingSpores)].particles;
    require(bloom.count < 9600 && bloom.group.colorGradient[1].a < .65f
        && bloom.group.colorGradient[2].a < .55f, "Spore bloom was not made sparser and more transparent");
    require(spores.count < 1280 && spores.group.colorGradient[1].a < .70f,
            "Passive glowing spores remain too dense/opaque");
    const auto& mist = definitions[index(E::EffectId::GroundMist)];
    require(mist.particles.count == 0 && mist.screen.features() == 0
        && mist.medium.extinction > 0 && mist.medium.groundLayer,
        "Ground mist must use only the existing volumetric ground medium");
    for (auto id : {E::EffectId::Sandstorm, E::EffectId::Blizzard, E::EffectId::AshStorm})
    {
        const auto& storm = definitions[index(id)];
        require(storm.medium.extinction > 0 && !storm.medium.groundLayer && storm.screen.dust == 0,
                "Storm visibility must use real depth-dependent media without a second flat dust veil");
    }
    require(E::findEffect("cold_breath") == nullptr, "First-person breath remains in environment catalogue");
    const auto* haze = E::findEffect("ice_haze");
    require(haze && haze->particles.group.emitScaleMax.x > 50
        && haze->particles.group.emitScaleMax.z > 50,
        "Breath replacement is still a mouth-sized emitter");
    const auto& snow = definitions[index(E::EffectId::LightSnow)].particles;
    const auto& pollen = definitions[index(E::EffectId::WindPollen)].particles;
    require(snow.shape != pollen.shape, "Snow and pollen still use the same sprite kind");
    require((snow.group.flags & Ge::RotateSprite) && std::abs(snow.group.rotateSprite) > .05f,
            "Snow lacks visible sprite rotation");
    require(snow.group.velocityMax <= 1.0f && snow.windScale < .5f,
            "Gentle snowfall is still driven like a windstorm");
    std::cout << "PASS artwork: doubled softer sand, wider constant-budget leaves/seeds, sparser spores, real ground/storm media, rotating snow\n";
}

void independentParticleWind()
{
    const glm::vec2 base(3, 1);
    const float speed = glm::length(base);
    auto previous = productionParticleWind(base, 0, 1, 24);
    bool rotated = false, changedStrength = false;
    for (unsigned i = 0; i < 3600; ++i)
    {
        const double seconds = i / 60.0;
        const auto wind = productionParticleWind(base, seconds, 1, 24);
        const float length = glm::length(wind);
        require(finite(wind.x) && finite(wind.y), "Animated wind became nonfinite");
        require(length >= speed * .5199f && length <= speed * 1.4801f,
                "Animated wind exceeded its bounded gust range");
        require(glm::length(wind - previous) < .025f * speed, "Wind jumps between adjacent frames");
        require(wind == productionParticleWind(base, seconds, 1, 24), "Wind depends on evaluation history");
        require(productionParticleWind(base, seconds, 0, 24) == base, "Zero variation changes base wind");
        require(productionParticleWind(glm::vec2(0), seconds, 1, 24) == glm::vec2(0),
                "Zero base wind produces unexpected gusts");
        rotated |= std::abs(wind.x * base.y - wind.y * base.x) > .2f;
        changedStrength |= std::abs(length - speed) > .2f;
        previous = wind;
    }
    require(rotated && changedStrength, "Wind does not vary both direction and strength");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (float value : {nan, inf, -inf})
    for (unsigned field = 0; field < 5; ++field)
    {
        auto input = base;
        double seconds = 3;
        float variation = 1, period = 24;
        if (field < 2) input[field] = value;
        if (field == 2) seconds = value;
        if (field == 3) variation = value;
        if (field == 4) period = value;
        const auto wind = productionParticleWind(input, seconds, variation, period);
        require(finite(wind.x) && finite(wind.y), "Invalid wind setting escaped input sanitization");
    }
    std::cout << "PASS independent particle wind: smooth heading/gusts, bounded speed, zero/static controls, deterministic time and invalid inputs\n";
}

void environmentMediumLifecycle()
{
    const auto inactive = [](const EnduranceEnvironmentMediumState& medium) {
        return medium.layers[0].extinction == 0 && medium.layers[1].extinction == 0;
    };
    std::array<float, E::EffectCount> strengths{};
    require(inactive(productionEnvironmentMedium(strengths, {100, -80})),
            "Zero effects retain an environmental medium");
    const auto& definitions = E::effectDefinitions();
    const glm::vec2 wind(75, -30);
    for (const auto& effect : definitions)
    {
        strengths = {}; strengths[index(effect.id)] = .4f;
        const auto actual = productionEnvironmentMedium(strengths, wind);
        const auto& expected = effect.medium;
        const unsigned selected = expected.groundLayer ? 0 : 1;
        require(actual.layers[1-selected].extinction == 0,
                "Medium leaked into the wrong height layer");
        const auto& layer = actual.layers[selected];
        require(near(layer.extinction, expected.extinction * .4f, 1e-7f),
                "Medium opacity does not follow the same effect fade");
        if (expected.extinction > 0)
        {
            require(glm::length(layer.albedo - expected.albedo) < 1e-5f,
                    "Fading changed the medium's scattering albedo");
            require(near(layer.baseHeight, expected.baseHeight) && near(layer.heightScale, expected.heightScale, .001f)
                && layer.windOffset == wind, "Medium lost authored world height or particle-only wind transport");
        }
    }
    // Independent physical coefficient oracle: two simultaneous events mix
    // sigma_s and sigma_t, not their final screen colours or alpha overlays.
    strengths = {};
    strengths[index(E::EffectId::HeavyRain)] = .6f;
    strengths[index(E::EffectId::Thunderstorm)] = .3f;
    const auto mix = productionEnvironmentMedium(strengths, wind).layers[1];
    const auto& rain = definitions[index(E::EffectId::HeavyRain)].medium;
    const auto& thunder = definitions[index(E::EffectId::Thunderstorm)].medium;
    const float expectedT = rain.extinction * .6f + thunder.extinction * .3f;
    const glm::vec3 expectedS = rain.albedo * rain.extinction * .6f + thunder.albedo * thunder.extinction * .3f;
    require(near(mix.extinction, expectedT, 1e-7f)
        && glm::length(mix.albedo * mix.extinction - expectedS) < 1e-7f,
        "Concurrent weather does not conserve summed scattering/extinction coefficients");
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(), -1.0f})
    {
        strengths.fill(invalid);
        require(inactive(productionEnvironmentMedium(strengths, wind)), "Invalid effect strength escaped medium sanitization");
    }
    strengths = {}; strengths[index(E::EffectId::Sandstorm)] = 2;
    const auto bounded = productionEnvironmentMedium(strengths,
        {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}).layers[1];
    require(near(bounded.extinction, definitions[index(E::EffectId::Sandstorm)].medium.extinction)
        && bounded.windOffset == glm::vec2(0), "Medium failed intensity/wind input bounds");

    E::EffectsState state;
    state.setBiome(4); settle(state);
    require(productionEnvironmentMedium(state.strengths(), wind).layers[0].extinction > 0,
            "Ground fog does not follow dominant-biome selection");
    state.setBiome(-1); settle(state);
    require(inactive(productionEnvironmentMedium(state.strengths(), wind)), "Leaving biome retains ground fog");
    state.setBiome(15); settle(state);
    require(inactive(productionEnvironmentMedium(state.strengths(), wind)), "Sandstorm started without an explicit event");
    require(state.triggerEvent(E::EffectId::Sandstorm, 100), "Medium event fixture could not start");
    state.update(.1f);
    const float partial = productionEnvironmentMedium(state.strengths(), wind).layers[1].extinction;
    require(partial > 0 && partial < definitions[index(E::EffectId::Sandstorm)].medium.extinction,
            "Weather medium skips its entry fade");
    settle(state, 8);
    state.stopEvents(); settle(state);
    require(inactive(productionEnvironmentMedium(state.strengths(), wind)), "Stopped weather leaves residual fog");
    require(state.previewEffect(E::EffectId::GroundMist, 20), "Medium preview fixture failed");
    settle(state, 8);
    require(productionEnvironmentMedium(state.strengths(), wind).layers[0].extinction > 0, "Medium preview is invisible");
    state.clear();
    require(inactive(productionEnvironmentMedium(state.strengths(), wind)), "Reset retains preview medium");
    std::cout << "PASS medium lifecycle: physical mixing, shared fades, no automatic storms, world heights, finite inputs and exact-zero cleanup\n";
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
    require(state.previewEffect(E::EffectId::FrostRim), "Default frost preview rejected");
    settle(state, E::effectDefinitions()[index(E::EffectId::FrostRim)].fadeIn + 2.0f);
    require(state.previewing() && state.strengths()[index(E::EffectId::FrostRim)] == 1.0f,
            "Default preview ends before authored crystal growth can be inspected");
    state.stopPreview(); settle(state);
    for (const auto& d : E::effectDefinitions())
    {
        const float hold = (std::max)(6.0f, d.fadeIn + .1f);
        require(state.previewEffect(d.id, hold + 10.0f), "Valid explicit preview was rejected");
        settle(state, hold);
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
    try { catalogue(); extendedArtRecipes(); independentParticleWind(); environmentMediumLifecycle(); passiveSelection(); eventLifecycle(); previewsAndInputs(); timestep(); }
    catch (const std::exception& error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
    std::cout << "PASS biome effects production catalogue/state\n";
}
