#pragma once

#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

struct BoneTransform
{
    glm::vec3 position;
    glm::quat rotation;
};

struct LegDefinition
{
    std::vector<float> lengths;
    std::vector<glm::vec3> restDirections;   // équivalent StartDirectionSucc
    std::vector<glm::vec3> restPositions;    // pose initiale (optionnel mais utile)

    bool initialized = false;
};

struct LegInput
{
    glm::vec3 rootPosition;
    glm::vec3 targetPosition;
    glm::vec3 polePosition;
    bool usePole = false;
};

class ProceduralIK
{
public:
    // Analytic shoulder/elbow/wrist specialization, starting from the animated
    // pose. Preserve segment lengths, guide the elbow with a pole, no allocation.
    static bool SolveTwoBone(const std::array<glm::vec3, 3>& joints,
        const glm::vec3& target, const glm::vec3& pole,
        std::array<glm::vec3, 3>& result)
    {
        const auto finite = [](const glm::vec3& p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        };
        if (!finite(joints[0]) || !finite(joints[1]) || !finite(joints[2]) ||
            !finite(target) || !finite(pole)) return false;
        const float upper = glm::length(joints[1] - joints[0]);
        const float lower = glm::length(joints[2] - joints[1]);
        if (!std::isfinite(upper + lower) || upper < 1e-5f || lower < 1e-5f)
            return false;
        glm::vec3 direction = target - joints[0];
        const float requestedDistance = glm::length(direction);
        if (!std::isfinite(requestedDistance)) return false;
        if (requestedDistance < 1e-6f) direction = joints[2] - joints[0];
        if (glm::length2(direction) < 1e-12f) direction = joints[1] - joints[0];
        direction = glm::normalize(direction);
        const float margin = (std::min)(upper, lower) * 0.0001f;
        const float distance = glm::clamp(requestedDistance,
            std::abs(upper - lower) + margin, upper + lower - margin);
        glm::vec3 bend = pole - joints[0];
        bend -= direction * glm::dot(bend, direction);
        if (glm::length2(bend) < 1e-10f)
        {
            bend = joints[1] - joints[0];
            bend -= direction * glm::dot(bend, direction);
        }
        if (glm::length2(bend) < 1e-10f)
        {
            const glm::vec3 axis = std::abs(direction.y) < 0.9f
                ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            bend = axis - direction * glm::dot(axis, direction);
        }
        bend = glm::normalize(bend);
        const float along = (upper * upper - lower * lower + distance * distance) /
            (2.0f * distance);
        const float height = std::sqrt((std::max)(0.0f, upper * upper - along * along));
        result = {{joints[0], joints[0] + direction * along + bend * height,
            joints[0] + direction * distance}};
        return finite(result[1]) && finite(result[2]);
    }

    int maxIterations = 12;
    float tolerance = 0.001f;
    float snapBackStrength = 1.0f;

public:
    glm::vec3 SafeNormalize(const glm::vec3& v)
    {
        float len2 = glm::length2(v);
        if (len2 < 1e-8f)
        {
            return glm::vec3(0.0f);
        }
        return v / std::sqrt(len2);
    }
    std::vector<BoneTransform> SolveLeg(LegDefinition& def, const LegInput& input)
    {
        const int n = def.lengths.size();
        if (n < 1)
        {
            return {};
        }

        const int count = n + 1;

        std::vector<glm::vec3> positions(count);
        
        if (!def.initialized)
        {
            def.restDirections.resize(count);
            def.restPositions.resize(count);

            def.restPositions[0] = input.rootPosition;

            for (int i = 0; i < n; i++)
            {
                def.restPositions[i + 1] = def.restPositions[i] + glm::vec3(0, def.lengths[i], 0);
                def.restDirections[i] = def.restPositions[i + 1] - def.restPositions[i];
            }

            def.restDirections[n] = def.restDirections[n - 1];

            def.initialized = true;
        }
        
        positions = def.restPositions;
        positions[0] = input.rootPosition;

        float completeLength = 0.0f;
        for (float l : def.lengths)
        {
            completeLength += l;
        }

        glm::vec3 target = input.targetPosition;
        
        if (glm::length2(target - positions[0]) >= completeLength * completeLength)
        {
            glm::vec3 dir = SafeNormalize(target - positions[0]);

            for (int i = 1; i < count; i++)
            {
                positions[i] = positions[i - 1] + dir * def.lengths[i - 1];
            }
        }
        else
        {            
            for (int i = 0; i < count - 1; i++)
            {
                glm::vec3 desired = positions[i] + def.restDirections[i];
                positions[i + 1] = glm::mix(positions[i + 1], desired, snapBackStrength);
            }
            
            for (int iteration = 0; iteration < maxIterations; iteration++)
            {                
                for (int i = count - 1; i > 0; i--)
                {
                    if (i == count - 1)
                    {
                        positions[i] = target;
                    }
                    else
                    {
                        glm::vec3 dir = SafeNormalize(positions[i] - positions[i + 1]);
                        positions[i] = positions[i + 1] + dir * def.lengths[i];
                    }
                }
                
                for (int i = 1; i < count; i++)
                {
                    glm::vec3 dir = SafeNormalize(positions[i] - positions[i - 1]);
                    positions[i] = positions[i - 1] + dir * def.lengths[i - 1];
                }
                
                if (glm::length2(positions[count - 1] - target) < tolerance * tolerance)
                {
                    break;
                }
            }
        }
        
        if (input.usePole)
        {
            glm::vec3 pole = input.polePosition;

            for (int i = 1; i < count - 1; i++)
            {
                glm::vec3 prev = positions[i - 1];
                glm::vec3 next = positions[i + 1];

                glm::vec3 planeNormal = SafeNormalize(next - prev);

                auto projectOnPlane = [&](const glm::vec3& p)
                {
                    glm::vec3 v = p - prev;
                    return p - glm::dot(v, planeNormal) * planeNormal;
                };

                glm::vec3 projectedPole = projectOnPlane(pole);
                glm::vec3 projectedBone = projectOnPlane(positions[i]);

                glm::vec3 a = SafeNormalize(projectedBone - prev);
                glm::vec3 b = SafeNormalize(projectedPole - prev);

                float angle = acos(glm::clamp(glm::dot(a, b), -1.0f, 1.0f));

                glm::vec3 cross = glm::cross(a, b);
                if (glm::dot(cross, planeNormal) < 0.0f)
                {
                    angle = -angle;
                }

                glm::quat rot = glm::angleAxis(angle, planeNormal);

                positions[i] = prev + rot * (positions[i] - prev);
            }
        }
        
        std::vector<BoneTransform> result(count);

        for (int i = 0; i < count; i++)
        {
            result[i].position = positions[i];

            if (i < count - 1)
            {
                glm::vec3 from = def.restDirections[i];
                glm::vec3 to = positions[i + 1] - positions[i];

                if (glm::length2(from) > 0.0f && glm::length2(to) > 0.0f)
                {
                    result[i].rotation = glm::rotation(SafeNormalize(from), SafeNormalize(to));
                }
                else
                {
                    result[i].rotation = glm::quat(1, 0, 0, 0);
                }
            }
            else
            {
                result[i].rotation = glm::quat(1, 0, 0, 0);
            }
        }

        return result;
    }
};

/*
ProceduralIK solver;

LegDefinition legDef;
legDef.lengths = {1.0f, 0.8f, 0.6f}; // 3 segments

LegInput input;
input.rootPosition = glm::vec3(0, 0, 0);
input.targetPosition = glm::vec3(1, -1, 2);

auto bones = solver.SolveLeg(legDef, input);
*/
