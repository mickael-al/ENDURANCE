#include "Physics/ClothSimulation.hpp"
#include "Physics/PreparedClothContacts.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace
{
    void require(bool condition, const std::string& message)
    { if (!condition) throw std::runtime_error(message); }
    bool finite(const glm::vec3& v)
    { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
    struct Mesh
    {
        std::vector<glm::vec3> positions;
        std::vector<std::uint32_t> triangles;
        std::vector<float> pins;
    };
    Mesh curtain(unsigned width = 9, unsigned height = 13)
    {
        Mesh mesh;
        for (unsigned row = 0; row < height; ++row)
            for (unsigned column = 0; column < width; ++column)
            {
                const float x = float(column) / float(width - 1) - 0.5f;
                const float y = 1.8f - float(row) / float(height - 1) * 1.3f;
                // Small authored folds must remain part of the rest geometry.
                const float z = 0.035f * std::sin(x * 12.0f);
                mesh.positions.emplace_back(x, y, z);
                mesh.pins.push_back(row == 0 ? 1.0f : row == 1 ? 0.70f : row == 2 ? 0.20f : 0.0f);
            }
        for (unsigned row = 0; row + 1 < height; ++row)
            for (unsigned column = 0; column + 1 < width; ++column)
            {
                const std::uint32_t a = row * width + column, b = a + 1;
                const std::uint32_t c = a + width, d = c + 1;
                mesh.triangles.insert(mesh.triangles.end(), {a, c, b, b, c, d});
            }
        return mesh;
    }
    float maximumStretch(const ClothTopology& topology, const std::vector<glm::vec3>& positions, float scale = 1.0f)
    {
        float maximum = 0.0f;
        const auto& triangles = topology.triangles();
        for (std::size_t j = 0; j < triangles.size(); j += 3)
            for (unsigned k = 0; k < 3; ++k)
            {
                const auto a = triangles[j + k], b = triangles[j + (k + 1) % 3];
                const float rest = glm::length(topology.restPositions()[a] - topology.restPositions()[b]) * scale;
                maximum = (std::max)(maximum, std::abs(glm::length(positions[a] - positions[b]) / rest - 1.0f));
            }
        return maximum;
    }
    void seamWelding()
    {
        Mesh mesh;
        mesh.positions = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
        mesh.triangles = {0, 1, 2, 3, 4, 5};
        mesh.pins = {1, .3f, 0, .8f, 0, 0};
        const auto topology = ClothTopology::build(mesh.positions, mesh.triangles, mesh.pins);
        require(topology->particleCount() == 4 && topology->triangles().size() == 6, "UV seams must weld without deleting adjacent triangles");
        require(topology->renderToParticle()[1] == topology->renderToParticle()[3] &&
            topology->renderToParticle()[2] == topology->renderToParticle()[5], "Seam map must preserve both render vertices");
        require(topology->pinWeights()[topology->renderToParticle()[1]] == .8f, "Seam attachment must use strongest weight");
        ClothBuildSettings settings;
        settings.seamWeldDistance = -1;
        require(ClothTopology::build(mesh.positions, mesh.triangles, mesh.pins, settings)->particleCount() == 6,
            "Welding must be explicitly disableable");
        mesh.triangles.push_back(0);
        bool rejected = false;
        try { ClothTopology::build(mesh.positions, mesh.triangles, mesh.pins); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Malformed topology must be rejected");
    }
    void zeroPinsAndPause()
    {
        const auto mesh = curtain();
        const auto topology = ClothTopology::build(mesh.positions, mesh.triangles, mesh.pins);
        ClothSimulation reference(topology), independent(topology);
        reference.reset(topology->restPositions());
        independent.reset(topology->restPositions());
        auto poisoned = topology->restPositions();
        for (std::size_t i = 0; i < poisoned.size(); ++i)
            if (topology->pinWeights()[i] == 0) poisoned[i] = glm::vec3(std::numeric_limits<float>::quiet_NaN());
        for (unsigned frame = 0; frame < 120; ++frame)
        {
            reference.advance(1.0f / 60.0f, topology->restPositions(), {});
            independent.advance(1.0f / 60.0f, poisoned, {});
        }
        for (std::size_t i = 0; i < poisoned.size(); ++i)
        {
            require(reference.positions()[i] == independent.positions()[i], "Zero pins must never sample the skin pose");
            require(finite(reference.positions()[i]), "Gravity and folds must remain finite");
            if (topology->pinWeights()[i] == 1) require(reference.positions()[i] == topology->restPositions()[i], "Hard pins must remain exact");
        }
        const auto positions = reference.positions(), velocities = reference.velocities();
        auto changed = topology->restPositions();
        for (auto& p : changed) p += glm::vec3(100.0f);
        reference.advance(0, changed, {{{100, 0, 0}, {100, 3, 0}, 2, .5f}});
        reference.advance(-1, changed, {});
        reference.advance(std::numeric_limits<float>::quiet_NaN(), changed, {});
        require(reference.positions() == positions && reference.velocities() == velocities, "Pause and invalid delta must freeze positions and velocities");
        require(maximumStretch(*topology, positions) < .06f, "Hanging cape must retain cloth edge lengths");
        const auto& drawn = reference.renderPositions(changed);
        for (std::size_t i = 0; i < drawn.size(); ++i)
            if (topology->pinWeights()[i] == 1) require(drawn[i] == changed[i], "Drawing must put upper anchors on the current skeleton");
    }
    std::vector<glm::vec3> runMovingCurtain(const std::shared_ptr<const ClothTopology>& topology, unsigned fps)
    {
        ClothSimulation simulation(topology);
        simulation.reset(topology->restPositions());
        auto targets = topology->restPositions();
        for (unsigned frame = 1; frame <= fps * 2; ++frame)
        {
            const float time = float(frame) / float(fps);
            for (std::size_t i = 0; i < targets.size(); ++i)
                targets[i] = topology->restPositions()[i] + glm::vec3(.2f * time,
                    .04f * std::sin(time * 3.0f), .1f * std::sin(time * 2.0f));
            simulation.advance(1.0f / float(fps), targets, {});
        }
        require(maximumStretch(*topology, simulation.positions()) < .08f, "Moving anchors must not tear the mesh");
        return simulation.positions();
    }
    void frameRatesAndCatchUp()
    {
        const auto mesh = curtain();
        const auto topology = ClothTopology::build(mesh.positions, mesh.triangles, mesh.pins);
        const auto a = runMovingCurtain(topology, 30), b = runMovingCurtain(topology, 60), c = runMovingCurtain(topology, 120);
        float maximumDifference = 0;
        for (std::size_t i = 0; i < a.size(); ++i)
            maximumDifference = (std::max)({maximumDifference, glm::length(a[i] - c[i]), glm::length(b[i] - c[i])});
        require(maximumDifference < .025f, "Fixed-step result must be stable across 30/60/120 FPS");
        ClothSettings settings;
        settings.maxSubsteps = 3;
        ClothSimulation bounded(topology, settings);
        bounded.reset(topology->restPositions());
        bounded.advance(500.0f, topology->restPositions(), {});
        require(bounded.lastSubsteps() == 3, "Long frames must have bounded catch-up");
        bounded.advance(1.0f / 120, topology->restPositions(), {});
        require(bounded.lastSubsteps() == 1, "Dropped time must not accumulate a backlog");
        std::cout << "  maximum FPS difference: " << maximumDifference << " m\n";
    }
    void changingFixedFrequency()
    {
        const auto mesh = curtain(5, 7);
        const auto topology = ClothTopology::build(mesh.positions, mesh.triangles, mesh.pins);
        ClothSimulation changed(topology), unchanged(topology);
        changed.reset(topology->restPositions());
        unchanged.reset(topology->restPositions());
        for (unsigned frame = 0; frame < 30; ++frame)
        {
            changed.advance(1.0f / 120.0f, topology->restPositions(), {});
            unchanged.advance(1.0f / 120.0f, topology->restPositions(), {});
        }
        const auto positions = changed.positions(), velocities = changed.velocities();
        for (const float invalid : {0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity()}) changed.setFixedSeconds(invalid);
        require(changed.fixedSeconds() == 1.0f / 120.0f &&
            changed.positions() == positions && changed.velocities() == velocities,
            "Invalid fixed frequencies must leave the live simulation unchanged");
        changed.setFixedSeconds(1.0f);
        require(changed.fixedSeconds() == 1.0f / 30.0f, "Fixed frequency must be bounded below by 30 Hz");
        changed.setFixedSeconds(.00001f);
        require(changed.fixedSeconds() == 1.0f / 480.0f, "Fixed frequency must be bounded above by 480 Hz");
        require(changed.positions() == positions && changed.velocities() == velocities,
            "Changing fixed frequency must preserve physical position and momentum");
        changed.setFixedSeconds(1.0f / 120.0f);
        for (unsigned frame = 0; frame < 30; ++frame)
        {
            changed.advance(1.0f / 120.0f, topology->restPositions(), {});
            unchanged.advance(1.0f / 120.0f, topology->restPositions(), {});
        }
        require(changed.positions() == unchanged.positions() && changed.velocities() == unchanged.velocities(),
            "Restoring the frequency must restore every cached coefficient without resetting the cape");

        // A normal remainder at 30 Hz can exceed the new catch-up window at
        // 480 Hz. The setter must retain elapsed time; advance owns the bounded
        // substeps and the eventual discarded backlog.
        const std::vector<glm::vec3> rest{{0, 1, 0}, {1, 1, 0}, {0, 1, 1}};
        const auto freeTopology = ClothTopology::build(rest, {0, 1, 2}, {0, 0, 0});
        ClothSettings settings;
        settings.fixedSeconds = 1.0f / 30.0f;
        settings.maxSubsteps = 2;
        settings.selfCollision = settings.triangleContacts = settings.tethers = false;
        ClothSimulation fractional(freeTopology, settings);
        fractional.reset(rest);
        fractional.advance(settings.fixedSeconds, rest, {});
        const float remainder = settings.fixedSeconds * .6f;
        fractional.advance(remainder, rest, {});
        require(fractional.lastSubsteps() == 0, "Frequency-change fixture must retain a fractional step");
        const auto beforePositions = fractional.positions(), beforeVelocities = fractional.velocities();
        fractional.setFixedSeconds(1.0f / 480.0f);
        require(fractional.positions() == beforePositions && fractional.velocities() == beforeVelocities,
            "Frequency change with a large fractional remainder must not integrate or reset");
        const float nextDelta = .000001f, fixed = fractional.fixedSeconds();
        fractional.advance(nextDelta, rest, {});
        require(fractional.lastSubsteps() == 2, "New frequency must keep the configured catch-up bound");
        const float retained = std::fmod(remainder + nextDelta - 2.0f * fixed, fixed);
        const auto drawn = fractional.renderPositions(rest);
        for (std::size_t i = 0; i < drawn.size(); ++i)
            require(glm::length(drawn[i] - (fractional.positions()[i] + fractional.velocities()[i] * retained)) < .0000001f,
                "The frequency setter must preserve elapsed fractional time until advance drops its bounded backlog");
    }
    void cachedConstraintInvalidation()
    {
        const auto mesh = curtain(5, 7);
        const auto topology = ClothTopology::build(mesh.positions, mesh.triangles, mesh.pins);
        ClothSettings settings;
        settings.selfCollision = false;
        ClothSimulation reused(topology, settings);
        for (const float scale : {.5f, 3.0f})
            for (const bool mirrored : {false, true})
                for (const float fixed : {1.0f / 60.0f, 1.0f / 240.0f})
                    for (const bool changeAfterReset : {false, true})
                {
                    auto targets = topology->restPositions();
                    for (auto& point : targets)
                    {
                        point *= scale;
                        if (mirrored) point.x = -point.x;
                        point += glm::vec3(.3f, .2f, -.1f);
                    }
                    // Test reset invalidation by itself as well as a live
                    // cadence change after reset; the latter must not mask
                    // a reset that accidentally kept the old coefficients.
                    reused.setFixedSeconds(changeAfterReset ? 1.0f / 30.0f : fixed);
                    reused.reset(targets, scale, mirrored);
                    if (changeAfterReset) reused.setFixedSeconds(fixed);
                    settings.fixedSeconds = fixed;
                    ClothSimulation fresh(topology, settings);
                    fresh.reset(targets, scale, mirrored);
                    for (unsigned frame = 1; frame <= 24; ++frame)
                    {
                        auto moved = targets;
                        const glm::vec3 movement(.015f * float(frame), .02f * std::sin(float(frame)), 0);
                        for (auto& point : moved) point += movement;
                        reused.advance(1.0f / 60.0f, moved, {});
                        fresh.advance(1.0f / 60.0f, moved, {});
                        require(reused.positions() == fresh.positions() && reused.velocities() == fresh.velocities(),
                            "Reused cached constraints must match a fresh instance after scale, mirror and frequency changes");
                    }
                }
    }
    float foldAngle(const std::vector<glm::vec3>& p)
    {
        const auto e = glm::normalize(p[1] - p[0]);
        const auto n1 = glm::normalize(glm::cross(p[1] - p[0], p[2] - p[0]));
        const auto n2 = glm::normalize(glm::cross(p[3] - p[0], p[1] - p[0]));
        return std::atan2(glm::dot(glm::cross(n1, n2), e), glm::dot(n1, n2));
    }
    void signedRestFolds()
    {
        for (const float angle : {-.6f, .6f})
        {
            std::vector<glm::vec3> rest{{-.5f, 0, 0}, {.5f, 0, 0}, {0, .7f, 0},
                {0, -.7f * std::cos(angle), -.7f * std::sin(angle)}};
            const auto topology = ClothTopology::build(rest, {0, 1, 2, 1, 0, 3}, {1, 1, 0, 0});
            ClothSettings settings;
            settings.gravity = glm::vec3(0);
            settings.bendCompliance = 0;
            settings.selfCollision = false;
            settings.damping = 8;
            ClothSimulation simulation(topology, settings);
            auto flattened = rest;
            flattened[3] = {0, -.7f, 0};
            simulation.reset(flattened);
            for (unsigned frame = 0; frame < 120; ++frame) simulation.advance(1.0f / 120, flattened, {});
            require(std::abs(foldAngle(simulation.positions()) - foldAngle(rest)) < .015f,
                "Dihedral constraint must restore signed authored folds rather than flatten or mirror them");
            require(maximumStretch(*topology, simulation.positions()) < .015f, "Bending must not stretch triangle edges");
            auto mirrored = rest;
            for (auto& p : mirrored) p.x = -p.x;
            ClothSimulation reflection(topology, settings);
            reflection.reset(mirrored, 1.0f, true);
            reflection.advance(1.0f / 60, mirrored, {});
            for (std::size_t i = 0; i < mirrored.size(); ++i)
                require(glm::length(reflection.positions()[i] - mirrored[i]) < .00001f,
                    "Reflected actors must preserve the mirrored signed rest folds");
        }
        const std::vector<glm::vec3> rest{{-.5f, 0, 0}, {.5f, 0, 0}, {0, .7f, 0},
            {0, -.7f * std::cos(.6f), -.7f * std::sin(.6f)}};
        const auto topology = ClothTopology::build(rest, {0, 1, 2, 1, 0, 3}, {1, 1, 0, 0});
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = false;
        auto flat = rest;
        flat[3] = {0, -.7f, 0};
        ClothSimulation flexible(topology, settings), small(topology, settings);
        flexible.reset(flat);
        flexible.advance(1.0f / 120, flat, {});
        auto quarter = flat;
        for (auto& p : quarter) p *= .25f;
        small.reset(quarter, .25f);
        small.advance(1.0f / 120, quarter, {});
        const float response = std::abs(foldAngle(flexible.positions()));
        require(response > .001f && response < .2f,
            "Default bending must gently restore authored folds instead of creating a rigid mantle");
        require(std::abs(foldAngle(flexible.positions()) - foldAngle(small.positions())) < .0001f,
            "Angular spring softness must remain unchanged after actor rescaling");
    }
    void collisions()
    {
        const std::vector<glm::vec3> rest{{0, -.02f, 0}, {0, .02f, 0}, {0, 0, .03f}};
        const auto topology = ClothTopology::build(rest, {0, 1, 2}, {0, 0, 0});
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = false;
        settings.collisionThickness = .005f;
        ClothSimulation clear(topology, settings), distant(topology, settings);
        clear.reset(rest);
        distant.reset(rest);
        for (unsigned frame = 0; frame < 10; ++frame)
        {
            clear.advance(1.0f / 60, rest, {});
            distant.advance(1.0f / 60, rest,
                {{{20, -20, 30}, {20, 20, 30}, 4, .8f}, {{-30, 4, -20}, {-20, 4, -20}, 2, .5f}});
        }
        require(clear.positions() == distant.positions(), "Swept broadphase must reject distant colliders without changing free cloth");
        ClothSimulation moving(topology, settings);
        moving.reset(rest);
        moving.advance(1.0f / 120, rest, {{{-2, -.2f, 0}, {-2, .2f, 0}, .15f, 0}});
        moving.advance(1.0f / 120, rest, {{{2, -.2f, 0}, {2, .2f, 0}, .15f, 0}});
        for (const auto& p : moving.positions()) require(p.x > 2.1f, "Fast moving body capsule must not tunnel through cloth");
        settings.gravity = {10000, 0, 0};
        ClothSimulation crossing(topology, settings);
        crossing.reset(rest);
        crossing.advance(1.0f / 120, rest, {{{.35f, -.2f, 0}, {.35f, .2f, 0}, .05f, .5f}});
        for (const auto& p : crossing.positions())
        {
            require(p.x < .41f, "Fast cloth point must not tunnel through a stationary capsule");
            const float distance = glm::length(glm::vec2(p.x - .35f, p.z));
            require(distance >= .05499f, "Swept cloth points must finish outside capsule thickness");
        }
        settings.gravity = glm::vec3(0);
        ClothSimulation overlapping(topology, settings);
        overlapping.reset(rest);
        const std::vector<ClothCapsule> overlappingCapsules{
            {{-.05f, -.2f, 0}, {-.05f, .2f, 0}, .1f, .25f},
            {{.05f, -.2f, 0}, {.05f, .2f, 0}, .1f, .25f}};
        overlapping.advance(1.0f / 120, rest, overlappingCapsules);
        for (const auto& p : overlapping.positions())
            for (const auto& capsule : overlappingCapsules)
                require(glm::length(glm::vec2(p.x - capsule.a.x, p.z)) >= .10498f,
                    "Final cloth must remain outside all overlapping body capsules");
        settings.gravity = {0, -9.81f, 0};
        ClothSimulation ground(topology, settings);
        auto raised = rest;
        for (auto& p : raised) p.y += 1;
        ground.reset(raised);
        for (unsigned frame = 0; frame < 120; ++frame)
            ground.advance(1.0f / 120, raised, {}, {{{0, 2, 0}, 0, .5f}});
        for (const auto& p : ground.positions()) require(p.y >= .00499f, "Hem must stay above a normalized collision plane");
    }
    void handSphereProjection()
    {
        constexpr float radius = .20f, thickness = .005f;
        const std::vector<ClothCapsule> hand{{{0, 0, 0}, {0, 0, 0}, radius, 0,
            ClothContactResponse::SeparationOnly}};
        const glm::vec3 exterior(.4f, .03f, -.02f);
        require(projectOutsideClothCapsules(exterior, {0, 0, 1}, hand, thickness) == exterior,
            "A hand sphere must leave exterior cloth points unchanged");
        for (const glm::vec3 start : {glm::vec3(0), glm::vec3(.04f, .01f, 0)})
        {
            const auto projected = projectOutsideClothCapsules(start, {0, 0, 1}, hand, thickness);
            require(finite(projected) && glm::length(projected) >= radius + thickness,
                "Interior points, including the exact hand centre, need a finite exterior projection");
            require(glm::length(projected - start) < .21f,
                "A hand sphere projection must remain local instead of applying an arbitrary teleport");
        }
        const std::vector<glm::vec3> rest{{0, 0, 0}, {.02f, .01f, 0}, {0, .02f, .01f}};
        const auto topology = ClothTopology::build(rest, {0, 1, 2}, {0, 0, 0});
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = settings.triangleContacts = settings.tethers = false;
        settings.collisionThickness = thickness;
        ClothSimulation simulation(topology, settings);
        simulation.reset(rest);
        simulation.advance(settings.fixedSeconds, rest, hand);
        for (const auto& point : simulation.positions())
            require(finite(point) && glm::length(point) >= radius + thickness - .00001f,
                "Physical reference points must end outside the static hand sphere");

        const std::vector<glm::vec3> anchoredRest{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        const auto anchored = ClothTopology::build(anchoredRest, {0, 1, 2}, {1, 0, 0});
        settings.triangleContacts = true;
        ClothSimulation pinned(anchored, settings);
        pinned.reset(anchoredRest);
        for (unsigned frame = 0; frame < 20; ++frame)
            pinned.advance(settings.fixedSeconds, anchoredRest, hand);
        require(pinned.positions() == anchoredRest,
            "An enlarged hand sphere must not move a hard shoulder attachment or launch its free neighbours");
    }
    void handSphereSweeps()
    {
        const std::vector<glm::vec3> rest{{0, -.02f, 0}, {0, .02f, 0}, {0, 0, .03f}};
        const auto topology = ClothTopology::build(rest, {0, 1, 2}, {0, 0, 0});
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = settings.triangleContacts = settings.tethers = false;
        settings.collisionThickness = .005f;
        ClothSimulation moving(topology, settings);
        moving.reset(rest);
        moving.advance(settings.fixedSeconds, rest, {{{-2, 0, 0}, {-2, 0, 0}, .15f, 0}});
        moving.advance(settings.fixedSeconds, rest, {{{2, 0, 0}, {2, 0, 0}, .15f, 0}});
        for (const auto& point : moving.positions())
            require(finite(point) && point.x > 2.1f && glm::length(point - glm::vec3(2, 0, 0)) >= .15499f,
                "A fast hand sphere crossing the entire cape sample must not tunnel through it");

        settings.gravity = {10000, 0, 0};
        // Keep all rays close to the sphere centre: a glancing contact may
        // legitimately slide around its side and finish beyond the centre.
        const std::vector<glm::vec3> crossingRest{{0, 0, 0}, {0, .002f, 0}, {0, 0, .002f}};
        const auto crossingTopology = ClothTopology::build(crossingRest, {0, 1, 2}, {0, 0, 0});
        ClothSimulation crossing(crossingTopology, settings);
        crossing.reset(crossingRest);
        crossing.advance(settings.fixedSeconds, crossingRest, {{{.35f, 0, 0}, {.35f, 0, 0}, .05f, 0}});
        for (const auto& point : crossing.positions())
            require(finite(point) && point.x < .35f && glm::length(point - glm::vec3(.35f, 0, 0)) >= .05499f,
                "A fast cloth point must stay on the entry side of a stationary hand sphere");

        settings.gravity = glm::vec3(0);
        auto offset = rest;
        for (auto& point : offset) point.x += .12f;
        ClothSimulation growing(topology, settings);
        growing.reset(offset);
        growing.advance(settings.fixedSeconds, offset, {{{0, 0, 0}, {0, 0, 0}, .05f, 0}});
        growing.advance(settings.fixedSeconds, offset, {{{0, 0, 0}, {0, 0, 0}, .25f, 0}});
        for (const auto& point : growing.positions())
            require(finite(point) && glm::length(point) >= .25499f,
                "Enabling the larger combat radius must safely resolve points inside its newly expanded volume");

        ClothSimulation shrinking(topology, settings);
        shrinking.reset(rest);
        shrinking.advance(settings.fixedSeconds, rest, {{{-1, 0, 0}, {-1, 0, 0}, .35f, 0}});
        shrinking.advance(settings.fixedSeconds, rest, {{{1, 0, 0}, {1, 0, 0}, .05f, 0}});
        for (const auto& point : shrinking.positions())
            require(finite(point) && point.x > 1.04f && glm::length(point - glm::vec3(1, 0, 0)) >= .05499f,
                "A rapidly shrinking moving hand sphere must retain continuous collision detection");

        const std::vector<glm::vec3> boundary{{.205f, 0, 0}, {.205f, .01f, 0}, {.205f, 0, .01f}};
        const auto boundaryTopology = ClothTopology::build(boundary, {0, 1, 2}, {0, 0, 0});
        for (const float tangentGravity : {0.0f, 60.0f})
        {
            settings.gravity = {0, tangentGravity, 0};
            ClothSimulation shrinkBoundary(boundaryTopology, settings), freeBoundary(boundaryTopology, settings);
            shrinkBoundary.reset(boundary);
            freeBoundary.reset(boundary);
            shrinkBoundary.advance(settings.fixedSeconds * .5f, boundary, {{{0, 0, 0}, {0, 0, 0}, .2f, 0}});
            freeBoundary.advance(settings.fixedSeconds * .5f, boundary, {});
            shrinkBoundary.advance(settings.fixedSeconds * .5f, boundary, {{{0, 0, 0}, {0, 0, 0}, .1f, 0}});
            freeBoundary.advance(settings.fixedSeconds * .5f, boundary, {});
            for (std::size_t point = 0; point < boundary.size(); ++point)
                require(glm::length(shrinkBoundary.positions()[point] - freeBoundary.positions()[point]) < .00001f,
                    "Shrinking the hand helper must not pull touching cloth inward or remove its tangent travel");
        }

        // When centre-relative speed exactly equals radius growth the swept
        // quadratic is linear. The entering root must stay finite and valid.
        const std::vector<glm::vec3> linearRest{{2, 0, 0}, {2, .002f, 0}, {2, 0, .002f}};
        const auto linearTopology = ClothTopology::build(linearRest, {0, 1, 2}, {0, 0, 0});
        settings.gravity = {-14400, 0, 0};
        settings.damping = 0;
        settings.collisionThickness = 0;
        ClothSimulation linear(linearTopology, settings);
        linear.reset(linearRest);
        linear.advance(settings.fixedSeconds * .5f, linearRest, {{{0, 0, 0}, {0, 0, 0}, 1, 0}});
        linear.advance(settings.fixedSeconds * .5f, linearRest, {{{0, 0, 0}, {0, 0, 0}, 2, 0}});
        for (const auto& point : linear.positions())
            require(finite(point) && glm::length(point) >= 1.99998f && point.x > 1.9f,
                "The linear swept-sphere equation must not miss an expanding helper contact");
    }
    void handSphereOverlapsAndScale()
    {
        const std::vector<glm::vec3> rest{{0, 0, 0}, {.02f, .01f, 0}, {0, .02f, .01f}};
        const auto topology = ClothTopology::build(rest, {0, 1, 2}, {0, 0, 0});
        const std::vector<ClothCapsule> colliders{
            {{0, -.3f, -.08f}, {0, .3f, -.08f}, .16f, .25f},
            {{-.10f, 0, .02f}, {-.10f, 0, .02f}, .18f, 0, ClothContactResponse::SeparationOnly},
            {{.10f, 0, .02f}, {.10f, 0, .02f}, .18f, 0, ClothContactResponse::SeparationOnly}};
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = settings.triangleContacts = settings.tethers = false;
        settings.collisionThickness = .005f;
        ClothSimulation reference(topology, settings);
        reference.reset(rest);
        reference.advance(settings.fixedSeconds, rest, colliders);
        for (const auto& point : reference.positions())
        {
            require(finite(point) && glm::length(point) < .4f,
                "Two hand spheres overlapping the torso must escape locally without cycling");
            require(projectOutsideClothCapsules(point, {0, 0, 1}, colliders, .00498f) == point,
                "Resolving hand overlap must not place cloth back inside the torso or the other hand");
        }
        for (const float scale : {.25f, 3.0f})
        {
            auto scaledRest = rest;
            auto scaledColliders = colliders;
            for (auto& point : scaledRest) point *= scale;
            for (auto& collider : scaledColliders)
            {
                collider.a *= scale;
                collider.b *= scale;
                collider.radius *= scale;
            }
            ClothSimulation scaled(topology, settings);
            scaled.reset(scaledRest, scale);
            scaled.advance(settings.fixedSeconds, scaledRest, scaledColliders);
            for (std::size_t point = 0; point < rest.size(); ++point)
                require(finite(scaled.positions()[point]) &&
                    glm::length(scaled.positions()[point] / scale - reference.positions()[point]) < .0002f,
                    "Combat hand separation must follow actor scale without changing its relative clearance");
        }
    }
    void handSphereSeparationVelocity()
    {
        const std::vector<glm::vec3> rest{{0, -.01f, 0}, {0, .01f, 0}, {0, 0, .01f}};
        const auto topology = ClothTopology::build(rest, {0, 1, 2}, {0, 0, 0});
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = settings.triangleContacts = settings.tethers = false;
        settings.collisionThickness = .005f;
        ClothSimulation physical(topology, settings), separation(topology, settings);
        physical.reset(rest);
        separation.reset(rest);
        std::vector<ClothCapsule> hand{{{-2, 0, 0}, {-2, 0, 0}, .15f, 0}};
        auto guard = hand;
        guard[0].response = ClothContactResponse::SeparationOnly;
        physical.advance(settings.fixedSeconds, rest, hand);
        separation.advance(settings.fixedSeconds, rest, guard);
        hand[0].a = hand[0].b = guard[0].a = guard[0].b = {2, 0, 0};
        physical.advance(settings.fixedSeconds, rest, hand);
        separation.advance(settings.fixedSeconds, rest, guard);
        const auto separated = separation.positions();
        for (std::size_t point = 0; point < rest.size(); ++point)
        {
            require(separated[point].x > 2.1f,
                "A non-grabbing hand must still separate cloth along its swept trajectory");
            require(physical.velocities()[point].x > 10.0f && std::abs(separation.velocities()[point].x) < .3f,
                "Only physical contacts may inject the hand's outward normal speed into the cape");
        }
        separation.advance(settings.fixedSeconds, rest, {});
        for (std::size_t point = 0; point < rest.size(); ++point)
            require(finite(separation.positions()[point]) &&
                std::abs(separation.positions()[point].x - separated[point].x) < .005f,
                "Removing the combat hand must not leave a residual normal launch in the cape");

        const std::vector<glm::vec3> slidingRest{{.199f, -.005f, 0}, {.199f, .005f, 0}, {.199f, 0, .01f}};
        const auto slidingTopology = ClothTopology::build(slidingRest, {0, 1, 2}, {0, 0, 0});
        settings.gravity = {0, 60, 0};
        ClothSimulation sliding(slidingTopology, settings), gripping(slidingTopology, settings);
        sliding.reset(slidingRest);
        gripping.reset(slidingRest);
        guard = {{{0, 0, 0}, {0, 0, 0}, .20f, 0, ClothContactResponse::SeparationOnly}};
        sliding.advance(settings.fixedSeconds, slidingRest, guard);
        guard[0].friction = 1;
        guard[0].response = ClothContactResponse::Physical;
        gripping.advance(settings.fixedSeconds, slidingRest, guard);
        float slidingTangent = 0, grippingTangent = 0;
        for (std::size_t point = 0; point < slidingRest.size(); ++point)
        {
            slidingTangent += sliding.velocities()[point].y;
            grippingTangent += gripping.velocities()[point].y;
        }
        require(slidingTangent > 1.0f && grippingTangent < slidingTangent * .5f,
            "Zero-friction hand separation must retain tangential cloth movement instead of grabbing it");
    }
    void handSphereModeChanges()
    {
        const std::vector<glm::vec3> rest{{-.3f, 0, .10f}, {.3f, 0, .10f}, {0, .04f, .10f}};
        const auto topology = ClothTopology::build(rest, {0, 1, 2}, {.4f, .4f, .4f});
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = settings.triangleContacts = settings.tethers = false;
        settings.collisionThickness = .005f;
        settings.damping = 20;
        settings.maxSpeed = 5;
        ClothSimulation simulation(topology, settings);
        simulation.reset(rest);
        // Preserve collider order and count just like the post-pose actor
        // snapshot: torso first, then left and right palm slots.
        std::vector<ClothCapsule> body{{{20, -1, 0}, {20, 1, 0}, .2f, .25f},
            {{-.3f, -.025f, 0}, {-.3f, .025f, 0}, .03f, .25f},
            {{.3f, -.025f, 0}, {.3f, .025f, 0}, .03f, .25f}};
        for (unsigned frame = 0; frame < 12; ++frame)
        {
            const bool combat = frame % 2 != 0;
            for (std::size_t palm = 1; palm < body.size(); ++palm)
            {
                const float x = palm == 1 ? -.3f : .3f;
                body[palm].a = {x, combat ? 0.0f : -.025f, 0};
                body[palm].b = {x, combat ? 0.0f : .025f, 0};
                body[palm].radius = combat ? .18f : .03f;
                body[palm].friction = combat ? 0.0f : .25f;
                body[palm].response = combat ? ClothContactResponse::SeparationOnly : ClothContactResponse::Physical;
            }
            // A fractional snapshot tests interpolation of the mode change
            // before the subsequent fixed step consumes its new geometry.
            simulation.advance(settings.fixedSeconds * .5f, rest, body);
            simulation.advance(settings.fixedSeconds * .5f, rest, body);
            for (const auto& point : simulation.positions())
                require(finite(point) && glm::length(point) < .8f,
                    "Combat sphere switches must not sweep between unrelated collider slots or launch the cape");
            if (combat)
                for (std::size_t palm = 1; palm < body.size(); ++palm)
                    for (const auto& point : simulation.positions())
                        require(glm::length(point - body[palm].a) >= body[palm].radius + .00498f,
                            "The enabled combat palm radius must be respected after interpolation");
        }
    }
    void softPinsAndReset()
    {
        const std::vector<glm::vec3> rest{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = false;
        const auto soft = ClothTopology::build(rest, {0, 1, 2}, {.2f, .2f, .2f});
        const auto strong = ClothTopology::build(rest, {0, 1, 2}, {.8f, .8f, .8f});
        ClothSimulation weak(soft, settings), attached(strong, settings);
        weak.reset(rest);
        attached.reset(rest);
        auto targets = rest;
        for (auto& p : targets) p.x += .3f;
        weak.advance(1.0f / 120, targets, {});
        attached.advance(1.0f / 120, targets, {});
        require(attached.positions()[0].x > weak.positions()[0].x + .1f,
            "Larger pin weights must produce physically stronger attachment");
        require(weak.positions()[0].x > 0 && weak.positions()[0].x < .29f, "Partial pins must be compliant rather than exact skin lerps");
        ClothSimulation scaledWeak(soft, settings);
        auto quarterRest = rest, quarterTargets = targets;
        for (auto& p : quarterRest) p *= .25f;
        for (auto& p : quarterTargets) p *= .25f;
        scaledWeak.reset(quarterRest, .25f);
        scaledWeak.advance(1.0f / 120, quarterTargets, {});
        require(std::abs(scaledWeak.positions()[0].x / .25f - weak.positions()[0].x) < .0001f,
            "Soft attachment weight must retain its strength after actor rescaling");
        auto scaled = rest;
        for (auto& p : scaled) p = p * 2.0f + glm::vec3(8, 3, -5);
        attached.reset(scaled, 2);
        require(attached.positions() == scaled && attached.uniformScale() == 2, "Teleport/rescale reset must initialize the new pose");
        for (const auto& v : attached.velocities()) require(v == glm::vec3(0), "Reset must clear physical momentum");
        attached.advance(1.0f / 60, scaled, {});
        require(maximumStretch(*strong, attached.positions(), 2) < .0001f, "Uniform rescale must also update constraint rest lengths");
    }
    void instancesAndDenseMesh()
    {
        const auto mesh = curtain(37, 49);
        const auto topology = ClothTopology::build(mesh.positions, mesh.triangles, mesh.pins);
        ClothSimulation a(topology), b(topology);
        a.reset(topology->restPositions());
        auto displaced = topology->restPositions();
        for (auto& p : displaced) p += glm::vec3(5, 0, 0);
        b.reset(displaced);
        const auto start = std::chrono::steady_clock::now();
        for (unsigned frame = 0; frame < 60; ++frame)
        {
            a.advance(1.0f / 60, topology->restPositions(), {});
            b.advance(1.0f / 60, displaced, {});
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        for (std::size_t i = 0; i < displaced.size(); ++i)
            require(glm::length((b.positions()[i] - a.positions()[i]) - glm::vec3(5, 0, 0)) < .015f,
                "Instances sharing topology must keep independent world positions");
        std::cout << "  dense mesh: " << topology->particleCount() << " particles, " <<
            seconds / 120.0 * 1000.0 << " ms per 60-Hz update\n";
    }
    void proxyEmbedding()
    {
        const auto mesh = curtain(41, 53);
        ClothBuildSettings settings;
        settings.maxParticles = 700;
        const auto topology = ClothTopology::build(mesh.positions, mesh.triangles, mesh.pins, settings);
        require(topology->particleCount() <= 700, "Dense cloth must obey the requested physics proxy budget");
        require(topology->renderEmbedding().size() == mesh.positions.size(), "Proxy must embed every fine render vertex");
        std::vector<glm::vec3> mean(topology->particleCount(), glm::vec3(0));
        std::vector<unsigned> count(topology->particleCount(), 0);
        float largestError = 0;
        for (std::size_t i = 0; i < mesh.positions.size(); ++i)
        {
            const auto mapped = topology->renderToParticle()[i];
            mean[mapped] += mesh.positions[i];
            ++count[mapped];
            if (mesh.pins[i] == 0) require(topology->pinWeights()[mapped] == 0,
                "Proxy must never give fully physical render points skin attachment");
            const auto& e = topology->renderEmbedding()[i];
            const auto& a = topology->restPositions()[e.particles[0]];
            const auto& b = topology->restPositions()[e.particles[1]];
            const auto& c = topology->restPositions()[e.particles[2]];
            const auto tangent = glm::normalize(b - a);
            const auto normal = glm::normalize(glm::cross(b - a, c - a));
            const auto bitangent = glm::cross(normal, tangent);
            const auto reconstructed = a * e.barycentric.x + b * e.barycentric.y + c * e.barycentric.z +
                tangent * e.frameOffset.x + bitangent * e.frameOffset.y + normal * e.frameOffset.z;
            largestError = (std::max)(largestError, glm::length(reconstructed - mesh.positions[i]));
        }
        for (std::size_t i = 0; i < mean.size(); ++i)
            require(glm::length(mean[i] / float(count[i]) - topology->restPositions()[i]) < .00001f,
                "Averaged render skin targets must equal the proxy bind positions");
        require(largestError < .00001f, "Proxy embedding must preserve the fine authored rest surface exactly");
        std::cout << "  proxy: " << mesh.positions.size() << " render vertices -> " <<
            topology->particleCount() << " particles, rest error " << largestError << " m\n";
    }
    void selfCollision()
    {
        const std::vector<glm::vec3> rest{{0, 0, -.004f}, {1, 0, -.004f}, {0, 1, -.004f},
            {0, 0, .004f}, {1, 0, .004f}, {0, 1, .004f}};
        const auto topology = ClothTopology::build(rest, {0, 1, 2, 3, 4, 5}, {0, 0, 0, 0, 0, 0});
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.collisionThickness = .02f;
        ClothSimulation simulation(topology, settings);
        simulation.reset(rest);
        simulation.advance(1.0f / 120, rest, {});
        require(simulation.positions() == rest,
            "Self collision must preserve a thin authored closed-shell thickness at rest");
        auto collapsed = rest;
        for (unsigned i = 0; i < 3; ++i) collapsed[i + 3] = collapsed[i];
        simulation.reset(collapsed);
        simulation.advance(1.0f / 120, rest, {});
        for (unsigned i = 0; i < 3; ++i)
            require(glm::length(simulation.positions()[i] - simulation.positions()[i + 3]) > .00719f,
                "Collapsed thin cloth layers must separate without inflating beyond authored thickness");
        require(maximumStretch(*topology, simulation.positions()) < .001f,
            "Separating cloth layers must retain their in-plane dimensions");
    }
    void capsuleUnionEscape()
    {
        const auto distance = [](const glm::vec3& p, const ClothCapsule& c)
        {
            const glm::dvec3 axis = glm::dvec3(c.b) - glm::dvec3(c.a);
            const double length2 = glm::dot(axis, axis);
            const double t = length2 > 0 ? std::clamp(glm::dot(glm::dvec3(p) - glm::dvec3(c.a), axis) /
                length2, 0.0, 1.0) : 0.0;
            return glm::length(glm::dvec3(p) - glm::dvec3(c.a) - axis * t);
        };
        const auto outside = [&distance](const glm::vec3& p, const std::vector<ClothCapsule>& capsules, float thickness)
        {
            require(finite(p), "Capsule union escape must remain finite");
            for (const auto& c : capsules)
                require(distance(p, c) >= double(c.radius) + double(thickness),
                    "Union escape must be exterior to every overlapping capsule");
        };
        const std::vector<ClothCapsule> cycle{{{-.15f, 0, 0}, {-.15f, 0, 0}, .25f, .25f},
            {{.15f, 0, 0}, {.15f, 0, 0}, .25f, .25f}, {{0, .15f, 0}, {0, .15f, 0}, .25f, .25f}};
        const auto p = projectOutsideClothCapsules({0, .02f, 0}, {0, 0, 1}, cycle, .003f);
        outside(p, cycle, .003f);
        require(glm::length(p - glm::vec3(0, .02f, 0)) < .27f, "Overlap escape must choose a nearby exterior");
        require(projectOutsideClothCapsules(p, {0, 0, 1}, cycle, .003f) == p,
            "Collision polish must be idempotent on an exterior point");
        const std::vector<ClothCapsule> foot{{{-.2f, .06f, 0}, {.2f, .06f, 0}, .12f, .25f},
            {{0, .08f, -.15f}, {0, .08f, .15f}, .12f, .25f}};
        const auto ground = projectOutsideClothCapsules({0, -.01f, 0}, {0, -1, 0}, foot, .018f,
            {{{0, 1, 0}, 0, .25f}});
        outside(ground, foot, .018f);
        require(ground.y >= .018f, "Capsule escape must also respect collision planes");
        const std::vector<ClothCapsule> separated{{{0, 0, 0}, {0, 0, 0}, .203f, .25f},
            {{.407f, 0, 0}, {.407f, 0, 0}, .203f, .25f}};
        const auto gap = projectOutsideClothCapsules({0, 0, 0}, {1, 0, 0}, separated, 0);
        outside(gap, separated, 0);
        require(gap.x >= .203f && gap.x < .204f, "Ray escape must stop in a real gap rather than cross an unrelated solid");
        const glm::vec3 unchanged(.01f, .02f, .03f);
        require(projectOutsideClothCapsules(unchanged, {0, 0, 0}, {{{0, 0, 0}, {0, 0, 0}, 0, 0}}, .2f) == unchanged,
            "Zero-radius disabled capsules must stay disabled when thickness is positive");
        std::mt19937 random(930128);
        std::uniform_real_distribution<float> coordinate(-.25f, .25f), radius(.08f, .30f);
        for (unsigned trial = 0; trial < 200; ++trial)
        {
            std::vector<ClothCapsule> capsules;
            for (unsigned j = 0; j < 8; ++j)
            {
                const glm::vec3 a(coordinate(random), coordinate(random), coordinate(random));
                capsules.push_back({a, a + glm::vec3(coordinate(random), coordinate(random), coordinate(random)), radius(random), .25f});
            }
            const glm::vec3 start(coordinate(random), coordinate(random), coordinate(random));
            const auto escaped = projectOutsideClothCapsules(start, {0, 1, 0}, capsules, .018f);
            outside(escaped, capsules, .018f);
            require(glm::length(escaped - start) < 1.0f, "Random overlap escape must remain local");
        }
    }
    void preparedContactReuse()
    {
        constexpr float thickness = .018f;
        const std::vector<ClothCapsule> first{{{-.15f, 0, 0}, {-.15f, 0, 0}, .25f, .25f},
            {{.15f, 0, 0}, {.15f, 0, 0}, .25f, .25f}};
        const std::vector<ClothCapsule> second{{{0, .08f, -.15f}, {0, .08f, .15f}, .12f, .25f}};
        const std::vector<ClothPlane> ground{{{0, 3, 0}, .15f, .25f}};
        PreparedClothContacts reused, fresh;
        reused.prepare(first, thickness, ground);
        for (unsigned generation = 0; generation < 6; ++generation)
        {
            const auto& capsules = generation % 2 ? first : second;
            // Updating capsules alone must retain the prepared ground plane.
            reused.prepareCapsules(capsules, thickness);
            fresh.prepare(capsules, thickness, ground);
            for (unsigned point = 0; point < 24; ++point)
            {
                const glm::vec3 start(float(point % 3) * .07f - .07f,
                    float(point % 5) * .02f - .02f, float(point % 4) * .04f - .06f);
                const auto result = reused.projectOutside(start, {0, -1, 0});
                require(result == fresh.projectOutside(start, {0, -1, 0}),
                    "Prepared capsule updates must not retain stale ray coefficients or scratch intervals");
                require(finite(result) && result.y >= .05f + thickness,
                    "Prepared contact escape must respect non-unit ground planes");
                for (const auto& capsule : capsules)
                {
                    const glm::dvec3 axis = glm::dvec3(capsule.b) - glm::dvec3(capsule.a);
                    const double length2 = glm::dot(axis, axis);
                    const double t = length2 > 0 ? std::clamp(glm::dot(glm::dvec3(result) -
                        glm::dvec3(capsule.a), axis) / length2, 0.0, 1.0) : 0.0;
                    require(glm::length(glm::dvec3(result) - glm::dvec3(capsule.a) - axis * t) >=
                        double(capsule.radius) + double(thickness),
                        "Prepared exact escape must be outside all capsules");
                }
                auto surface = start, referenceSurface = start;
                reused.projectSurface(surface, {0, 0, 1});
                fresh.projectSurface(referenceSurface, {0, 0, 1});
                require(surface == referenceSurface && finite(surface),
                    "Prepared fine-surface polish must remain deterministic across reuse");
            }
        }
        reused.preparePlanes({}, thickness);
        fresh.prepare(first, thickness);
        require(reused.projectOutside({0, 0, 0}, {0, -1, 0}) ==
            fresh.projectOutside({0, 0, 0}, {0, -1, 0}),
            "Removing planes must clear cached halfspaces while retaining current capsules");

        const float invalid = std::numeric_limits<float>::quiet_NaN();
        auto filtered = first;
        filtered.push_back({{invalid, 0, 0}, {0, 0, 0}, .2f, .25f});
        filtered.push_back({{0, 0, 0}, {0, 0, 0}, invalid, .25f});
        filtered.push_back({{0, 0, 0}, {0, 0, 0}, 0, .25f});
        auto filteredPlanes = ground;
        filteredPlanes.push_back({{0, 0, 0}, 2, .25f});
        filteredPlanes.push_back({{0, invalid, 0}, 0, .25f});
        filteredPlanes.push_back({{0, 1, 0}, invalid, .25f});
        reused.prepare(filtered, thickness, filteredPlanes);
        fresh.prepare(first, thickness, ground);
        const glm::vec3 start(0, -.03f, 0);
        require(reused.projectOutside(start, {0, 0, 1}) == fresh.projectOutside(start, {0, 0, 1}),
            "Disabled and invalid colliders must not poison prepared contact tables");
        auto surface = start, referenceSurface = start;
        reused.projectSurface(surface, {0, 0, 1});
        fresh.projectSurface(referenceSurface, {0, 0, 1});
        require(surface == referenceSurface, "Fine surface projection must ignore invalid collider entries");
        reused.prepare({}, 0, {});
        require(reused.projectOutside(start, {0, 0, 1}) == start,
            "Preparing an empty contact snapshot must discard every prior collider");
    }
    void triangleInteriorContact()
    {
        // No vertex overlaps the sphere, but the face passes through its
        // centre. A vertex-only collider completely misses this configuration.
        const std::vector<glm::vec3> rest{{-1, 0, -1}, {1, 0, -1}, {0, 0, 1}};
        const auto topology = ClothTopology::build(rest, {0, 1, 2}, {0, 0, 0});
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = false;
        settings.collisionThickness = .005f;
        settings.triangleContacts = false;
        const std::vector<ClothCapsule> sphere{{{0, 0, 0}, {0, 0, 0}, .15f, .25f}};
        ClothSimulation verticesOnly(topology, settings);
        verticesOnly.reset(rest);
        verticesOnly.advance(1.0f / 120, rest, sphere);
        require(verticesOnly.positions() == rest, "Fixture must expose a genuine triangle-interior collision miss");
        settings.triangleContacts = true;
        ClothSimulation faces(topology, settings);
        faces.reset(rest);
        faces.advance(1.0f / 120, rest, sphere);
        const auto& positions = faces.positions();
        const auto normal = glm::normalize(glm::cross(positions[1] - positions[0], positions[2] - positions[0]));
        require(std::abs(glm::dot(normal, positions[0])) > .1548f,
            "Barycentric virtual contacts must keep triangle interiors outside body capsules");
        require(maximumStretch(*topology, positions) < .015f,
            "Triangle contact must distribute its correction without tearing the fabric");
        // An authored kinematic anchor may overlap a conservative body
        // capsule. Moving the other vertices cannot make that face exterior.
        // The impossible virtual constraint must leave free neighbours alone.
        const std::vector<glm::vec3> anchoredRest{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        const std::vector<ClothCapsule> anchorOverlap{{{.10f, .10f, 0}, {.10f, .10f, 0}, .20f, .25f}};
        const auto anchoredTopology = ClothTopology::build(anchoredRest, {0, 1, 2}, {1, 0, 0});
        ClothSimulation anchored(anchoredTopology, settings);
        anchored.reset(anchoredRest);
        for (unsigned step = 0; step < 120; ++step)
            anchored.advance(1.0f / 120, anchoredRest, anchorOverlap);
        require(anchored.positions() == anchoredRest,
            "An interior fixed anchor must not launch movable triangle neighbours");

        // Both fixed endpoints are outside, but their unmovable edge crosses
        // the capsule. This is equally infeasible despite a movable centroid.
        const std::vector<glm::vec3> edgeRest{{-1, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        const auto edgeTopology = ClothTopology::build(edgeRest, {0, 1, 2}, {1, 1, 0});
        ClothSimulation fixedEdge(edgeTopology, settings);
        fixedEdge.reset(edgeRest);
        for (unsigned step = 0; step < 120; ++step)
            fixedEdge.advance(1.0f / 120, edgeRest, anchorOverlap);
        require(fixedEdge.positions() == edgeRest,
            "An intersecting fixed edge must not launch its movable third vertex");
    }
    void currentRenderSample()
    {
        const std::vector<glm::vec3> rest{{.5f, 0, 0}, {.5f, 1, 0}, {.5f, 0, 1}};
        const auto topology = ClothTopology::build(rest, {0, 1, 2}, {0, 0, 0});
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = false;
        settings.triangleContacts = false;
        settings.tethers = false;
        settings.collisionThickness = .005f;
        ClothSimulation simulation(topology, settings);
        simulation.reset(rest);
        std::vector<ClothCapsule> body{{{0, 0, 0}, {0, 0, 0}, .4f, .25f}};
        simulation.advance(settings.fixedSeconds, rest, body);
        body[0].a = body[0].b = {.2f, 0, 0};
        simulation.advance(settings.fixedSeconds, rest, body);
        require(glm::length(rest[0] - body[0].a) < body[0].radius,
            "Previous sample must lie inside the newly moved body collider");
        require(glm::length(simulation.positions()[0] - body[0].a) > body[0].radius + .0049f,
            "Current physical sample must be exterior to the moving collider");
        require(simulation.renderPositions(rest) == simulation.positions(),
            "Drawing at an exact fixed step must use the current safe sample");

        const auto positions = simulation.positions(), velocities = simulation.velocities();
        const float remainder = settings.fixedSeconds * .4f;
        simulation.advance(remainder, rest, body);
        require(simulation.lastSubsteps() == 0, "Fractional draw fixture must retain a nonzero accumulator");
        const auto predicted = simulation.renderPositions(rest);
        for (std::size_t i = 0; i < predicted.size(); ++i)
        {
            require(predicted[i] == positions[i] + velocities[i] * remainder,
                "Display prediction must use only the remaining fixed-step time");
            require(glm::length(predicted[i] - positions[i]) <= settings.maxSpeed * settings.fixedSeconds + .0001f,
                "Display prediction must be bounded to one fixed substep");
        }
        auto unrelated = rest;
        for (auto& point : unrelated) point = glm::vec3(std::numeric_limits<float>::quiet_NaN());
        require(simulation.renderPositions(unrelated) == predicted,
            "Unpinned display prediction must never sample animated skin targets");
        simulation.advance(0, unrelated, {});
        simulation.advance(-1, unrelated, {});
        require(simulation.renderPositions(unrelated) == predicted &&
            simulation.positions() == positions && simulation.velocities() == velocities,
            "Pause must freeze even a fractional-step predicted display exactly");

        const auto anchored = ClothTopology::build(rest, {0, 1, 2}, {0, 1, 0});
        ClothSimulation pinned(anchored, settings);
        pinned.reset(rest);
        pinned.advance(remainder, rest, {});
        auto currentPose = rest;
        currentPose[1] += glm::vec3(.5f, .1f, .2f);
        require(pinned.renderPositions(currentPose)[1] == currentPose[1],
            "Hard display anchors must follow the current skeleton exactly");
    }
    void materialTethers()
    {
        auto mesh = curtain(2, 30);
        for (std::size_t i = 2; i < mesh.pins.size(); ++i) mesh.pins[i] = 0;
        const auto topology = ClothTopology::build(mesh.positions, mesh.triangles, mesh.pins);
        ClothSettings settings;
        settings.gravity = glm::vec3(0);
        settings.selfCollision = false;
        settings.iterations = 1;
        ClothSimulation simulation(topology, settings);
        simulation.reset(topology->restPositions());
        auto moved = topology->restPositions();
        for (auto& p : moved) p.x += 1;
        // Skin data for free points is intentionally unrelated to the cloth.
        for (std::size_t i = 2; i < moved.size(); ++i) moved[i] = glm::vec3(1000);
        simulation.advance(1.0f / 120, moved, {});
        for (std::size_t i = 2; i < moved.size(); ++i)
        {
            const auto anchor = i % 2;
            const float materialLength = glm::length(topology->restPositions()[i] - topology->restPositions()[anchor]);
            require(glm::length(simulation.positions()[i] - simulation.positions()[anchor]) <=
                materialLength * settings.tetherSlack + .0001f,
                "Geodesic tethers must bound hanging-cloth stretch even with a small iteration budget");
        }
    }
}

int main()
{
    const std::array<std::pair<const char*, void(*)()>, 21> tests{{
        {"seam welding and invalid topology", seamWelding},
        {"zero pins, exact anchors, pause and rest lengths", zeroPinsAndPause},
        {"fixed-step frame rates and bounded catch-up", frameRatesAndCatchUp},
        {"live fixed frequency and preserved fractional time", changingFixedFrequency},
        {"cached constraints after cadence, scale and mirror changes", cachedConstraintInvalidation},
        {"signed authored folds", signedRestFolds},
        {"capsule CCD and collision planes", collisions},
        {"combat hand sphere projection and pinned anchors", handSphereProjection},
        {"moving hand sphere, cloth-point CCD and radius growth", handSphereSweeps},
        {"hand and torso overlap with actor-scale invariance", handSphereOverlapsAndScale},
        {"non-grabbing hands and zero-friction tangential slide", handSphereSeparationVelocity},
        {"fixed-order combat palm mode transitions", handSphereModeChanges},
        {"compliant attachment, teleport and rescale", softPinsAndReset},
        {"shared topology, independent instances and dense mesh", instancesAndDenseMesh},
        {"bounded proxy and exact fine mesh embedding", proxyEmbedding},
        {"cloth layer self collision", selfCollision},
        {"robust overlapping capsule union and plane escape", capsuleUnionEscape},
        {"prepared contact reuse, invalid entries and non-unit planes", preparedContactReuse},
        {"barycentric triangle-interior body contacts", triangleInteriorContact},
        {"material geodesic anchor tethers", materialTethers},
        {"current render sample and bounded prediction", currentRenderSample}
    }};
    try
    {
        for (const auto& test : tests)
        {
            test.second();
            std::cout << "PASS " << test.first << '\n';
        }
    }
    catch (const std::exception& failure)
    {
        std::cerr << "FAIL " << failure.what() << '\n';
        return 1;
    }
    return 0;
}
