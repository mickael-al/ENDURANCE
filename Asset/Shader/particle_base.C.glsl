#version 450

#define GROUP_SIZE 256
#define MAX_DIM_GROUPS 256
#define MAX_DIM_THREADS (GROUP_SIZE * MAX_DIM_GROUPS)
#define MAX_DIM_THREADS_THREADS (MAX_DIM_THREADS * MAX_DIM_GROUPS)

layout(local_size_x = GROUP_SIZE, local_size_y = 1, local_size_z = 1) in;

layout(std430, binding = 6) buffer ParticleData
{
    float data[];
} particle;

layout(std430, binding = 7) buffer InstanceGroup
{
    uint data[];
} instance;

/*
uniform uint u_offset;   // offset des particules
uniform uint u_offsetSettings;   // offset des particulesGroups
uniform uint u_count;    // nombre de particules
uniform float u_dt;      // delta time
*/

uniform uvec3 u_instance;
uniform float u_dt;

vec3 ReadPosition(uint u_offset, uint id)
{
    uint bid = u_offset + id * 8u;
    return vec3(
        particle.data[bid + 0],
        particle.data[bid + 1],
        particle.data[bid + 2]
    );
}

void WritePosition(uint u_offset, uint id, vec3 p)
{
    uint bid = u_offset + id * 8u;
    particle.data[bid + 0] = p.x;
    particle.data[bid + 1] = p.y;
    particle.data[bid + 2] = p.z;
}

vec4 ReadColor(uint u_offset, uint id)
{
    uint base = u_offset + id * 8u;

    uint packed1 = floatBitsToUint(particle.data[base + 3]);
    uint packed2 = floatBitsToUint(particle.data[base + 4]);

    float r = float(packed1 & 0xFFFFu) / 65535.0;
    float g = float((packed1 >> 16) & 0xFFFFu) / 65535.0;
    float b = float(packed2 & 0xFFFFu) / 65535.0;
    float a = float((packed2 >> 16) & 0xFFFFu) / 65535.0;

    return vec4(r, g, b, a) * 64.0; // si ta range est 0..64
}

void WriteColor(uint u_offset, uint id, vec4 color)
{
    uint base = u_offset + id * 8u;

    uint r = uint(clamp(color.r / 64.0, 0.0, 1.0) * 65535.0);
    uint g = uint(clamp(color.g / 64.0, 0.0, 1.0) * 65535.0);
    uint b = uint(clamp(color.b / 64.0, 0.0, 1.0) * 65535.0);
    uint a = uint(clamp(color.a / 64.0, 0.0, 1.0) * 65535.0);

    uint packed1 = (r) | (g << 16);
    uint packed2 = (b) | (a << 16);

    particle.data[base + 3] = uintBitsToFloat(packed1);
    particle.data[base + 4] = uintBitsToFloat(packed2);
}

float ReadTime(uint u_offset, uint id)
{
    uint bid = u_offset + id * 8u;
    return particle.data[bid + 5];
}

void WriteTime(uint u_offset, uint id, float t)
{
    uint bid = u_offset + id * 8u;
    particle.data[bid + 5] = t;
}

void ReadScaleVelocity(uint u_offset, uint id, out float scale, out vec3 vel)
{
    uint bid = u_offset + id * 8u;

    uint packed1 = floatBitsToUint(particle.data[bid + 6]);
    uint packed2 = floatBitsToUint(particle.data[bid + 7]);

    uint scale16 = packed1 & 0xFFFFu;
    scale = (float(scale16) / 65535.0) * 64.0;

    uint vx16 = packed1 >> 16;
    uint vy16 = packed2 & 0xFFFFu;
    uint vz16 = packed2 >> 16;

    vel.x = (float(vx16) / 32767.5) - 1.0;
    vel.y = (float(vy16) / 32767.5) - 1.0;
    vel.z = (float(vz16) / 32767.5) - 1.0;

    vel *= 256.0;
}

uint EncodeVelocity16(float v)
{
    float n = clamp(v / 256.0, -1.0, 1.0);
    return uint((n + 1.0) * 32767.5);
}

void WriteScaleVelocity(uint u_offset, uint id, float scale, vec3 vel)
{
    uint bid = u_offset + id * 8u;
    uint scale16 = uint(clamp(scale / 64.0, 0.0, 1.0) * 65535.0);

    uint vx16 = EncodeVelocity16(vel.x);
    uint vy16 = EncodeVelocity16(vel.y);
    uint vz16 = EncodeVelocity16(vel.z);

    uint packed1 = (scale16) | (vx16 << 16);
    uint packed2 = (vy16) | (vz16 << 16);

    particle.data[bid + 6] = uintBitsToFloat(packed1);
    particle.data[bid + 7] = uintBitsToFloat(packed2);
}

void WriteVelocityOnly(uint u_offset,uint id, vec3 vel)
{
    uint bid = u_offset + id * 8u;
    uint packed1 = floatBitsToUint(particle.data[bid + 6]);

    uint scale16 = packed1 & 0xFFFFu;

    uint vx16 = EncodeVelocity16(vel.x);
    uint vy16 = EncodeVelocity16(vel.y);
    uint vz16 = EncodeVelocity16(vel.z);

    packed1 = scale16 | (vx16 << 16);
    uint packed2 = (vy16) | (vz16 << 16);

    particle.data[bid + 6] = uintBitsToFloat(packed1);
    particle.data[bid + 7] = uintBitsToFloat(packed2);
}

float hash(uint x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return float(x) * (1.0 / 4294967295.0);
}

float rand(inout uint seed)
{
    seed += (seed << 10u);
    seed ^= (seed >> 6u);
    seed += (seed << 3u);
    seed ^= (seed >> 11u);
    seed += (seed << 15u);
    return hash(seed);
}

vec3 randomDirectionInCone(inout uint seed, vec3 axis, float angleDeg)
{
    axis = normalize(axis);
    float angleRad = radians(angleDeg);

    float cosMax = cos(angleRad);

    float u = rand(seed);
    float v = rand(seed);

    float cosTheta = mix(1.0, cosMax, u);
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
    float phi = 6.28318530718 * v;

    vec3 localDir = vec3(
        cos(phi) * sinTheta,
        sin(phi) * sinTheta,
        cosTheta
    );

    vec3 up = abs(axis.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, axis));
    vec3 bitangent = cross(axis, tangent);

    return tangent * localDir.x +
        bitangent * localDir.y +
        axis * localDir.z;
}   

vec3 RotateAroundLocalY(vec3 dir, float angleDeg)
{
    if (dot(dir, dir) < 1.0e-8)
        return vec3(1.0, 0.0, 0.0);
    vec3 forward = normalize(dir);
    const vec3 ref = vec3(1.0, 0.0, 0.0);
    vec3 tangent = cross(forward, ref);
    if (dot(tangent, tangent) < 1.0e-8)
        tangent = cross(forward, vec3(0.0, 1.0, 0.0));
    tangent = normalize(tangent);
    vec3 localY = normalize(cross(tangent, forward));
    
    vec3 X = tangent;
    vec3 Y = localY; 
    vec3 Z = forward;

    float a = radians(angleDeg);
    float c = cos(a);
    float s = sin(a);

    return normalize(X * c + Z * s);
}

vec3 WorldOrbitDisplacement(vec3 launchVelocity, vec3 acceleration,
    float age, float dt, float angularSpeed, float radius)
{
    float omega = radians(angularSpeed);
    float angle = omega * age;
    float previousAngle = omega * (age - dt);
    // Difference of two points on the same birth-centered orbit. No live target
    // or emitter position: moving the character cannot drag existing particles.
    vec3 orbitalStep = radius * vec3(cos(angle) - cos(previousAngle),
        0.0, sin(angle) - sin(previousAngle));
    return launchVelocity * dt + acceleration * (age - 0.5 * dt) * dt + orbitalStep;
}

vec4 interpolateColors(uint id,uint count, float t)
{
    float scaled = t * float(count - 1u);
    uint idxA = uint(floor(scaled));
    uint idxB = min(idxA + 1u, count - 1u);    
    idxA = (idxA * 4) + id;
    idxB = (idxB * 4) + id;

    float localT = fract(scaled);
    return mix(vec4(particle.data[idxA], particle.data[idxA+1], particle.data[idxA+2], particle.data[idxA+3]),
    vec4(particle.data[idxB], particle.data[idxB + 1], particle.data[idxB + 2], particle.data[idxB + 3]), localT);
}

const uint FLAG_ATTACH_TO_EMIT_POINT = 1u << 0;
const uint FLAG_BOX_EMIT = 1u << 1;
const uint FLAG_FOLLOW_TARGET = 1u << 2;
const uint FLAG_ROTATE = 1u << 3;
const uint FLAG_VEL_ORTIENTATION = 1u << 4;
const uint FLAG_AVOID_TARGET = 1u << 5;
const uint FLAG_NOT_MOVE = 1u << 9;
const uint FLAG_SCALE_RNG = 1u << 10;
const uint FLAG_BURST = 1u << 11;
const uint FLAG_ORBIT_TARGET = 1u << 12;
const uint FLAG_ORBIT_WORLD = 1u << 13;
const uint FLAG_CYLINDER_EMIT = 1u << 14;
const uint FLAG_LOCAL_EMIT_TRANSFORM = 1u << 15;
const float BURST_VALUE = -30000;

void main()
{
    uint id = uint(gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * MAX_DIM_THREADS +gl_GlobalInvocationID.z * MAX_DIM_THREADS_THREADS);
    uint instanceID = u_instance.y + (id / u_instance.x);
    id = id % u_instance.x;
    if (instanceID >= (u_instance.y+u_instance.z))
    {
        return;
    }
    uint u_offsetSettings = instance.data[instanceID];
    uint u_offset = instance.data[u_offsetSettings+2];
    u_offsetSettings = instance.data[u_offsetSettings];     

    uint seed = u_offsetSettings + id + u_offset;
    float rlt = rand(seed);
    float rro = rand(seed);
    float rsro = rand(seed);

    uint sid = u_offsetSettings;
    uint flags = floatBitsToUint(particle.data[sid + 9]);
    float lifetime = ReadTime(u_offset,id);
    /*if ((flags & FLAG_BURST) != 0u)
    {
        
    }*/
    if (lifetime < ((flags & FLAG_BURST) != 0u ? BURST_VALUE : 0.0f))
    {            
        float rv = rand(seed);
        float rs = rand(seed);

        WriteTime(u_offset,id, mix(particle.data[sid], particle.data[sid + 1], rlt));
        vec3 emitDirection = vec3(particle.data[sid + 5], particle.data[sid + 6], particle.data[sid + 7]);
        float spreadAngle = particle.data[sid + 8];   
        float scaleStart = particle.data[sid + 12];
        if ((flags & FLAG_SCALE_RNG) != 0u)
        {
            scaleStart = mix(scaleStart,particle.data[sid + 13], rs);
        }
        vec3 emitScaleMin = vec3(particle.data[sid + 14],particle.data[sid + 15], particle.data[sid + 16]);
        vec3 emitScaleMax = vec3(particle.data[sid + 17], particle.data[sid + 18], particle.data[sid + 19]);

        vec3 emitPosition;
        if ((flags & FLAG_ATTACH_TO_EMIT_POINT) != 0u)
        {
            emitPosition = vec3(0, 0, 0);
        }
        else
        {
            emitPosition = vec3(particle.data[sid + 2], particle.data[sid + 3], particle.data[sid + 4]);
        }
        if ((flags & (FLAG_BOX_EMIT | FLAG_CYLINDER_EMIT)) != 0u)
        {
            vec3 birthOffset;
            if ((flags & FLAG_CYLINDER_EMIT) != 0u)
            {
                // sqrt(U) distributes births uniformly across the elliptical disk.
                // Y is independent, so the complete cylinder volume is uniform.
                float radius = sqrt(rand(seed));
                float angle = 6.28318530718 * rand(seed);
                float height = rand(seed);
                vec3 center = (emitScaleMin + emitScaleMax) * 0.5;
                vec3 halfExtent = abs(emitScaleMax - emitScaleMin) * 0.5;
                birthOffset = center + vec3(cos(angle) * radius * halfExtent.x,
                    mix(-halfExtent.y, halfExtent.y, height),
                    sin(angle) * radius * halfExtent.z);
            }
            else
            {
                // Keep the legacy box random sequence and bounds unchanged.
                float x = mix(emitScaleMin.x, emitScaleMax.x, rand(seed));
                float y = mix(emitScaleMin.y, emitScaleMax.y, rand(seed));
                float z = mix(emitScaleMin.z, emitScaleMax.z, rand(seed));
                birthOffset = vec3(x, y, z);
            }
            if ((flags & FLAG_LOCAL_EMIT_TRANSFORM) != 0u)
            {
                // Only new groups requesting this flag read the appended settings.
                vec3 axisX = vec3(particle.data[sid + 84], particle.data[sid + 85], particle.data[sid + 86]);
                vec3 axisY = vec3(particle.data[sid + 87], particle.data[sid + 88], particle.data[sid + 89]);
                vec3 axisZ = vec3(particle.data[sid + 90], particle.data[sid + 91], particle.data[sid + 92]);
                birthOffset = axisX * birthOffset.x + axisY * birthOffset.y + axisZ * birthOffset.z;
            }
            emitPosition += birthOffset;
        }
        vec3 dir = randomDirectionInCone(seed, emitDirection, spreadAngle);
        vec3 vel = mix(particle.data[sid + 10], particle.data[sid + 11], rv) * dir;
        if ((flags & FLAG_NOT_MOVE) != 0u)
        {
            vel = dir;
        }

        WritePosition(u_offset,id, emitPosition);
        WriteScaleVelocity(u_offset,id, scaleStart, vel);
        WriteColor(u_offset,id, interpolateColors(sid + 37, floatBitsToUint(particle.data[sid + 36]), 0.0));

        return;
    }   
    
    lifetime -= u_dt; 
    WriteTime(u_offset,id, lifetime);
    
    float maxLifeTime = mix(particle.data[sid], particle.data[sid + 1], rlt);
    float lifeRatio = clamp(1.0 - (lifetime / maxLifeTime), 0.0, 1.0);
    
    float scale;
    vec3 vel;
    ReadScaleVelocity(u_offset,id, scale, vel);
    vec3 baseVel = vel;

    vec3 gravity = vec3(particle.data[sid + 20], particle.data[sid + 21], particle.data[sid + 22]);
    vec3 externalForce = vec3(particle.data[sid + 23], particle.data[sid + 24], particle.data[sid + 25]);
    float drag = particle.data[sid + 26];

    vel += (gravity + externalForce) * u_dt;
    vel *= (1.0 - (drag * u_dt));

    vec3 pos = ReadPosition(u_offset,id);
    if ((flags & FLAG_AVOID_TARGET) != 0u)
    {
        vec3 tempPos = pos;
        if ((flags & FLAG_ATTACH_TO_EMIT_POINT) != 0u)
        {
            tempPos += vec3(particle.data[sid + 2], particle.data[sid + 3], particle.data[sid + 4]);
        }
        vec3 target = vec3(particle.data[sid + 31], particle.data[sid + 32], particle.data[sid + 33]);
        float avoidStrength = particle.data[sid + 77];
        float avoidRadius = particle.data[sid + 78];
        float d = distance(target, tempPos);
        if (d < avoidRadius)
        {
            vel += normalize(tempPos - target) * (d / avoidRadius) * avoidStrength;
        }
    }
    if ((flags & FLAG_FOLLOW_TARGET) != 0u)
    {
        vec3 tempPos = pos;
        if ((flags & FLAG_ATTACH_TO_EMIT_POINT) != 0u)
        {
            tempPos += vec3(particle.data[sid + 2], particle.data[sid + 3], particle.data[sid + 4]);
        }
        float strengthFollow = particle.data[sid + 34];
        float radiusFollow = particle.data[sid + 35];
        vec3 target = vec3(particle.data[sid + 31], particle.data[sid + 32], particle.data[sid + 33]);
        if (distance(target, tempPos) < radiusFollow)
        {
            float speed = length(vel);
            vec3 newDir = normalize(mix(normalize(vel), normalize(target - tempPos), strengthFollow));
            vel = newDir * speed;
        }
    }

    if ((flags & FLAG_ORBIT_WORLD) != 0u)
    {
        float age = maxLifeTime - lifetime;
        float angularSpeed = mix(particle.data[sid + 27], particle.data[sid + 28], rro);
        float radius = mix(particle.data[sid + 29], particle.data[sid + 30], rsro);
        pos += WorldOrbitDisplacement(baseVel, gravity + externalForce,
            age, u_dt, angularSpeed, radius);
        // Preserve launch velocity; do not quantize accumulated gravity each frame.
        vel = baseVel;
    }
    else if ((flags & FLAG_ORBIT_TARGET) != 0u)
    {
        // Keep the launch velocity and integrate acceleration analytically.
        // Repacking a tiny gravity increment into 16-bit velocity each frame
        // otherwise makes these long, slow trails fall faster at high FPS.
        float age = maxLifeTime - lifetime;
        pos += baseVel * u_dt + (gravity + externalForce) * (age - 0.5 * u_dt) * u_dt;
        vel = baseVel;
        vec3 target = vec3(particle.data[sid + 31], particle.data[sid + 32], particle.data[sid + 33]);
        vec2 center = target.xz;
        if ((flags & FLAG_ATTACH_TO_EMIT_POINT) != 0u)
            center -= vec2(particle.data[sid + 2], particle.data[sid + 4]);
        float angle = radians(mix(particle.data[sid + 27], particle.data[sid + 28], rro)) * u_dt;
        float c = cos(angle);
        float s = sin(angle);
        vec2 radial = pos.xz - center;
        pos.xz = center + vec2(c * radial.x - s * radial.y, s * radial.x + c * radial.y);
    }
    else
    {
        pos += vel * u_dt;
    }
    if ((flags & FLAG_ROTATE) != 0u)
    {
        float scaleRotate = mix(particle.data[sid + 29], particle.data[sid + 30], rsro);
        vec3 rdir = RotateAroundLocalY(vel, mix(particle.data[sid + 27], particle.data[sid + 28], rro) * lifeRatio) * scaleRotate;
        pos += rdir * u_dt;
    }
    if ((flags & FLAG_NOT_MOVE) != 0u)
    {
        float newScale = mix(particle.data[sid + 12], particle.data[sid + 13], lifeRatio);
        WriteScaleVelocity(u_offset,id, newScale, baseVel);
    }
    else
    {
        WritePosition(u_offset,id, pos);
        if ((flags & FLAG_SCALE_RNG) != 0u)
        {
            WriteVelocityOnly(u_offset,id, vel);
        }
        else
        {
            float newScale = mix(particle.data[sid + 12], particle.data[sid + 13], lifeRatio);
            WriteScaleVelocity(u_offset,id, newScale, vel);
        }
    }
    WriteColor(u_offset,id, interpolateColors(sid + 37, floatBitsToUint(particle.data[sid + 36]), lifeRatio));
}
