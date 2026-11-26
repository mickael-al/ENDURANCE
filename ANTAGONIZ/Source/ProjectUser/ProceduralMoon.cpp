#include "ProceduralMoon.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace EnduranceMoon
{
namespace
{
constexpr float Pi = 3.14159265358979323846f;
constexpr float Tau = 2.f * Pi;
constexpr float TargetAlbedo = 0.2084f;

struct Vec3 { float x, y, z; };
Vec3 operator+(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x-b.x, a.y-b.y, a.z-b.z}; }
Vec3 operator*(Vec3 a, float b) { return {a.x*b, a.y*b, a.z*b}; }
float dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x};
}
Vec3 normalized(Vec3 a)
{
    return a * (1.f / std::sqrt((std::max)(dot(a, a), 1.e-20f)));
}
float clamp(float v, float a, float b) { return (std::max)(a, (std::min)(v, b)); }
float smooth(float a, float b, float value)
{
    const float t = clamp((value-a)/(b-a), 0.f, 1.f);
    return t*t*(3.f-2.f*t);
}
// std::isfinite can be compiled away under the engine's -ffast-math flags.
bool finite(float value)
{
    static_assert(sizeof(float) == sizeof(std::uint32_t), "32-bit float required");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}
float bounded(float value, float fallback, float low, float high)
{
    return clamp(finite(value) ? value : fallback, low, high);
}
bool cancelled(const std::atomic_bool* cancel)
{
    return cancel && cancel->load(std::memory_order_relaxed);
}

// Fixed integer RNG rather than standard distributions: reproducible across
// standard libraries and independent of any engine random state.
struct Random
{
    std::uint32_t state;
    explicit Random(std::uint32_t seed) : state(seed) {}
    std::uint32_t next()
    {
        state += 0x9e3779b9u;
        std::uint32_t n = state;
        n = (n ^ (n >> 16u)) * 0x85ebca6bu;
        n = (n ^ (n >> 13u)) * 0xc2b2ae35u;
        return n ^ (n >> 16u);
    }
    float unit() { return static_cast<float>(next() >> 8u) * (1.f/16777216.f); }
    float range(float lo, float hi) { return lo + (hi-lo)*unit(); }
    Vec3 sphere()
    {
        const float y = range(-1.f, 1.f);
        const float a = range(-Pi, Pi);
        const float r = std::sqrt((std::max)(0.f, 1.f-y*y));
        return {r*std::cos(a), y, r*std::sin(a)};
    }
};

struct Noise
{
    std::array<int, 512> permutation{};
    std::array<Vec3, 6> offset{};
    explicit Noise(std::uint32_t seed)
    {
        Random rng(seed ^ 0x4d4f4f4eu);
        for (int i=0; i<256; ++i) permutation[static_cast<std::size_t>(i)] = i;
        for (int i=255; i>0; --i)
        {
            const int j = static_cast<int>(rng.next() % static_cast<unsigned>(i+1));
            std::swap(permutation[static_cast<std::size_t>(i)], permutation[static_cast<std::size_t>(j)]);
        }
        for (int i=0; i<256; ++i) permutation[static_cast<std::size_t>(i+256)] = permutation[static_cast<std::size_t>(i)];
        for (auto& p : offset) p = {rng.range(-50.f,50.f), rng.range(-50.f,50.f), rng.range(-50.f,50.f)};
    }
    float simplex(Vec3 p) const
    {
        static constexpr Vec3 gradients[12] = {
            {1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},
            {1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},
            {0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
        const float skew = (p.x+p.y+p.z)/3.f;
        const int i = static_cast<int>(std::floor(p.x+skew));
        const int j = static_cast<int>(std::floor(p.y+skew));
        const int k = static_cast<int>(std::floor(p.z+skew));
        const float unskew = static_cast<float>(i+j+k)/6.f;
        const Vec3 q = {p.x-static_cast<float>(i)+unskew,
                        p.y-static_cast<float>(j)+unskew,
                        p.z-static_cast<float>(k)+unskew};
        const int rx = (q.x>=q.y ? 1:0) + (q.x>=q.z ? 1:0);
        const int ry = (q.y> q.x ? 1:0) + (q.y>=q.z ? 1:0);
        const int rz = (q.z> q.x ? 1:0) + (q.z> q.y ? 1:0);
        float value = 0.f;
        for (int corner=0; corner<4; ++corner)
        {
            const int ox = rx>=3-corner ? 1:0;
            const int oy = ry>=3-corner ? 1:0;
            const int oz = rz>=3-corner ? 1:0;
            const float shift = static_cast<float>(corner)/6.f;
            const Vec3 delta = {q.x-static_cast<float>(ox)+shift,
                                q.y-static_cast<float>(oy)+shift,
                                q.z-static_cast<float>(oz)+shift};
            float support = .6f-dot(delta,delta);
            if (support<=0.f) continue;
            const int h = permutation[static_cast<std::size_t>(((i+ox)&255) +
                permutation[static_cast<std::size_t>(((j+oy)&255) +
                permutation[static_cast<std::size_t>((k+oz)&255)])])];
            support *= support;
            value += support*support*dot(gradients[h%12],delta);
        }
        return value*32.f;
    }
    float faultWarp(Vec3 p) const
    {
        return .7f*simplex(p*4.3f+offset[5]) + .3f*simplex(p*10.1f+offset[2]);
    }
};

struct Fbm
{
    std::array<float, 10> frequency{}, weight{};
    int count = 0;
    Fbm(int octaves, float footprint, float gain)
    {
        float amplitude = 1.f, frequencyValue = 1.f, total = 0.f;
        for (int octave=0; octave<octaves; ++octave)
        {
            const float w = amplitude*(1.f-smooth(.25f,.5f,footprint*frequencyValue));
            if (w<=0.f) break;
            frequency[static_cast<std::size_t>(count)] = frequencyValue;
            weight[static_cast<std::size_t>(count)] = w;
            total += w;
            ++count;
            frequencyValue *= 2.03f;
            amplitude *= gain;
        }
        for (int i=0; i<count; ++i) weight[static_cast<std::size_t>(i)] /= total;
    }
    float sample(Vec3 p, const Noise& noise) const
    {
        float value = 0.f;
        for (int i=0; i<count; ++i)
            value += weight[static_cast<std::size_t>(i)] * noise.simplex(p*frequency[static_cast<std::size_t>(i)]);
        return value;
    }
};

struct Fault
{
    Vec3 origin, along, normal;
    float begin, end, width, depth, phase, frequency, bend, warp, originWarp;
};
float faultBend(const Fault& f, float angle, float warp)
{
    return f.bend*(std::sin(angle*f.frequency+f.phase)-std::sin(f.phase)) + f.warp*(warp-f.originWarp);
}
std::vector<Fault> makeFaults(const Params& params, const Noise& noise)
{
    std::vector<Fault> faults;
    Random rng(params.seed ^ 0x4641554cu);
    for (int i=0; i<params.fractureCount; ++i)
    {
        Fault f{};
        f.origin = rng.sphere();
        f.normal = normalized(cross(f.origin, rng.sphere()));
        f.along = normalized(cross(f.normal, f.origin));
        f.begin = -rng.range(.65f, 1.5f);
        f.end = rng.range(.75f, 1.65f);
        // A few dominant fractures and finer secondary faults, rather than
        // equal Voronoi cells or a regular network of complete great circles.
        const float importance = i<3 ? 1.f : rng.range(.4f,.72f);
        f.width = params.fractureWidth*importance*rng.range(.85f,1.3f);
        f.depth = params.fractureDepth*importance*rng.range(.8f,1.25f);
        f.phase = rng.range(-Pi,Pi);
        f.frequency = rng.range(8.f,17.f);
        f.bend = rng.range(.006f,.018f);
        f.warp = rng.range(.035f,.065f);
        f.originWarp = noise.faultWarp(f.origin);
        faults.push_back(f);
        const int branches = i<3 ? 3:2;
        for (int branch=0; branch<branches; ++branch)
        {
            const float attach = rng.range(f.begin*.7f,f.end*.7f);
            Vec3 anchor = f.origin*std::cos(attach) + f.along*std::sin(attach);
            const Vec3 onCircle = anchor;
            for (int iteration=0; iteration<3; ++iteration)
                anchor = normalized(onCircle + f.normal*faultBend(f,attach,noise.faultWarp(anchor)));
            const Vec3 tangent = normalized(f.along*std::cos(attach)-f.origin*std::sin(attach));
            const float turn = rng.range(.3f,.8f)*(branch%2 ? -1.f:1.f);
            Fault b = f;
            b.origin = anchor;
            b.along = normalized(tangent*std::cos(turn)+f.normal*std::sin(turn));
            b.normal = normalized(cross(b.origin,b.along));
            b.along = normalized(cross(b.normal,b.origin));
            b.begin = -.02f; // Overlap the trunk so the join is continuous.
            b.end = rng.range(.25f,.85f);
            b.width *= rng.range(.32f,.55f);
            b.depth *= rng.range(.35f,.65f);
            b.bend *= .6f;
            b.warp *= .55f;
            b.originWarp = noise.faultWarp(anchor);
            faults.push_back(b);
        }
    }
    return faults;
}

std::uint8_t encode(float x)
{
    return static_cast<std::uint8_t>(clamp(x,0.f,1.f)*255.f+.5f);
}
void write32(std::ostream& stream, std::uint32_t value)
{
    const char bytes[4] = {static_cast<char>(value&255u), static_cast<char>((value>>8u)&255u),
                           static_cast<char>((value>>16u)&255u), static_cast<char>((value>>24u)&255u)};
    stream.write(bytes,4);
}
}

Params sanitize(Params p)
{
    p.width = (std::max)(64,(std::min)(4096,p.width));
    int power = 64;
    while (power*2<=p.width) power *= 2;
    p.width = power;
    p.macroScale = bounded(p.macroScale,2.4f,.25f,12.f);
    p.detailScale = bounded(p.detailScale,18.f,2.f,96.f);
    p.octaves = (std::max)(1,(std::min)(10,p.octaves));
    p.fractureCount = (std::max)(0,(std::min)(32,p.fractureCount));
    p.fractureWidth = bounded(p.fractureWidth,.018f,.001f,.06f);
    p.fractureDepth = bounded(p.fractureDepth,.025f,0.f,.05f);
    p.fractureStrength = bounded(p.fractureStrength,1.25f,0.f,3.f);
    return p;
}

Surface generate(const Params& input, const std::atomic_bool* cancel)
{
    if (cancelled(cancel)) return {};
    const Params p = sanitize(input);
    const int width = p.width, height = width/2;
    const std::size_t count = static_cast<std::size_t>(width)*static_cast<std::size_t>(height);
    const float texel = Tau/static_cast<float>(width);
    const Noise noise(p.seed);
    const Fbm macro(p.octaves,texel*p.macroScale,.52f);
    const Fbm micro(p.octaves,texel*p.detailScale,.60f);
    std::vector<float> cosine(static_cast<std::size_t>(width)), sine(static_cast<std::size_t>(width));
    std::vector<float> rowCos(static_cast<std::size_t>(height)), rowSin(static_cast<std::size_t>(height));
    for (int x=0; x<width; ++x)
    {
        const float longitude = ((static_cast<float>(x)+.5f)/static_cast<float>(width)-.5f)*Tau;
        cosine[static_cast<std::size_t>(x)] = std::cos(longitude);
        sine[static_cast<std::size_t>(x)] = std::sin(longitude);
    }
    for (int y=0; y<height; ++y)
    {
        const float latitude = ((static_cast<float>(y)+.5f)/static_cast<float>(height)-.5f)*Pi;
        rowCos[static_cast<std::size_t>(y)] = std::cos(latitude);
        rowSin[static_cast<std::size_t>(y)] = std::sin(latitude);
    }
    const auto sphere = [&](int x, int y) -> Vec3 {
        return {rowCos[static_cast<std::size_t>(y)]*cosine[static_cast<std::size_t>(x)],
                rowSin[static_cast<std::size_t>(y)],
                rowCos[static_cast<std::size_t>(y)]*sine[static_cast<std::size_t>(x)]};
    };
    std::vector<float> maria(count), detail(count), terrain(count), albedo(count);
    for (int y=0; y<height; ++y)
    {
        if (cancelled(cancel)) return {};
        for (int x=0; x<width; ++x)
        {
            const std::size_t index = static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(x);
            const Vec3 n = sphere(x,y);
            const Vec3 warp = {noise.simplex(n*.85f+noise.offset[0]),
                               noise.simplex(n*.85f+noise.offset[1]),
                               noise.simplex(n*.85f+noise.offset[2])};
            const float broad = macro.sample((n+warp*.18f)*p.macroScale+noise.offset[3],noise);
            const float fine = micro.sample(n*p.detailScale+noise.offset[4],noise);
            detail[index] = fine;
            const float m = smooth(-.09f,.11f,broad+.05f*fine);
            maria[index] = m;
            const float mineral = clamp(.5f+2.4f*fine,0.f,1.f);
            albedo[index] = (.28f+.18f*mineral)*(1.f-m) + (.055f+.035f*mineral)*m;
            terrain[index] = -.0018f*m + .00018f*fine;
        }
    }

    Random rng(p.seed);
    for (int impact=0; impact<1800; ++impact)
    {
        if (cancelled(cancel)) return {};
        const Vec3 center = rng.sphere();
        const float latitude = std::asin(clamp(center.y,-1.f,1.f));
        const float longitude = std::atan2(center.z,center.x);
        float radius = std::exp(rng.range(std::log(.004f),std::log(.085f)));
        const int rayCount = 11+static_cast<int>(rng.next()%18u);
        const float phase = rng.range(-Pi,Pi);
        const bool young = impact<24;
        if (young) radius = rng.range(.015f,.033f);
        const float extent = radius*(young ? 14.f : 1.85f);
        const float chord = 2.f*std::sin(radius*.5f);
        const float depth = radius*rng.range(.025f,.055f);
        const Vec3 east = {-std::sin(longitude),0.f,std::cos(longitude)};
        const Vec3 north = cross(center,east);
        const int firstRow = (std::max)(0,static_cast<int>(std::floor((latitude-extent+Pi*.5f)/texel-.5f)));
        const int lastRow = (std::min)(height-1,static_cast<int>(std::ceil((latitude+extent+Pi*.5f)/texel-.5f)));
        // The angular bounding box wraps explicitly in longitude; polar caps
        // cover whole rows. This avoids 1800 full-width texture evaluations.
        float longitudeExtent = Pi;
        if (std::abs(latitude)+extent<Pi*.5f)
            longitudeExtent = std::asin(clamp(std::sin(extent)/std::cos(latitude),0.f,1.f));
        int firstColumn = static_cast<int>(std::floor((longitude-longitudeExtent+Pi)/texel-.5f));
        const int endColumn = static_cast<int>(std::ceil((longitude+longitudeExtent+Pi)/texel-.5f));
        int columns = (std::min)(width,endColumn-firstColumn+1);
        if (columns>=width) { firstColumn=0; columns=width; }
        for (int y=firstRow; y<=lastRow; ++y)
        {
            if (cancelled(cancel)) return {};
            for (int column=0; column<columns; ++column)
            {
                const int x = (firstColumn+column+width)%width;
                const std::size_t index = static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(x);
                const Vec3 n = sphere(x,y), delta = n-center;
                const float r = std::sqrt(dot(delta,delta))/chord*(1.f+.10f*detail[index]);
                if (r>(young ? 13.5f : 1.8f)) continue;
                const float rimDistance = (r-1.f)/.13f;
                const float floor = -depth*(1.f-smooth(.16f,1.f,r));
                const float rim = depth*.30f*std::exp(-rimDistance*rimDistance);
                terrain[index] += (floor+rim)*(1.f-.65f*maria[index]);
                const float colorDistance = (r-1.04f)/.22f;
                albedo[index] += (young ? .14f:.055f)*std::exp(-colorDistance*colorDistance)
                    -.025f*(1.f-smooth(.45f,.95f,r));
                if (young)
                {
                    const float angle = std::atan2(dot(n,north),dot(n,east));
                    const float ray = (std::max)(0.f,std::sin(angle*static_cast<float>(rayCount)+phase+detail[index]*3.f));
                    const float ray2=ray*ray, ray4=ray2*ray2, ray8=ray4*ray4;
                    const float rays = ray8*ray4*ray2*smooth(1.f,1.8f,r)*(1.f-smooth(3.f,13.f,r))*std::exp(-r/5.f);
                    const float ejectaDistance = (r-1.3f)/.60f;
                    albedo[index] += .26f*rays+.05f*std::exp(-ejectaDistance*ejectaDistance);
                }
            }
        }
    }

    if (p.fractureCount>0 && p.fractureStrength>0.f)
    {
        const std::vector<Fault> faults = makeFaults(p,noise);
        for (int y=0; y<height; ++y)
        {
            if (cancelled(cancel)) return {};
            for (int x=0; x<width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(x);
                const Vec3 n = sphere(x,y);
                const float warp = noise.faultWarp(n);
                float deepest = 0.f, shoulder = 0.f, exposed = 0.f, floorMaterial = 0.f;
                for (const Fault& f : faults)
                {
                    const float distanceToPlane = dot(n,f.normal);
                    const float effectiveWidth = (std::max)(f.width,texel*.6f);
                    if (std::abs(distanceToPlane)>2.f*f.bend+2.f*f.warp+effectiveWidth*4.f) continue;
                    const float angle = std::atan2(dot(n,f.along),dot(n,f.origin));
                    if (angle<f.begin || angle>f.end) continue;
                    const float taper = smooth(f.begin,f.begin+.12f,angle)*(1.f-smooth(f.end-.22f,f.end,angle));
                    const float widthVariation = .8f+.22f*std::sin(angle*23.f+f.phase)+.18f*warp;
                    const float widthHere = effectiveWidth*widthVariation;
                    const float d = distanceToPlane-faultBend(f,angle,warp);
                    const float r = std::abs(d)/widthHere;
                    if (r>3.8f) continue;
                    const float trench = 1.f-smooth(.10f,1.35f,r);
                    const float antiAlias = (std::min)(1.f,f.width/effectiveWidth);
                    const float depthHere = f.depth*taper*antiAlias;
                    deepest = (std::max)(deepest,depthHere*trench);
                    // Unequal uplifted shoulders give visible grazing-light
                    // relief; these are geometric normals, never baked light.
                    const float lip = smooth(.65f,1.3f,r)*(1.f-smooth(1.3f,3.8f,r));
                    shoulder = (std::max)(shoulder,depthHere*.23f*lip*(d>0.f ? 1.f:.55f));
                    exposed = (std::max)(exposed,taper*lip*antiAlias);
                    floorMaterial = (std::max)(floorMaterial,taper*trench*antiAlias);
                }
                terrain[index] += p.fractureStrength*(shoulder-deepest);
                // Fresh minerals at the rim, dark subsurface rock in the
                // fissure. Illumination remains entirely in the runtime shader.
                albedo[index] += p.fractureStrength*(.055f*exposed-.075f*floorMaterial);
            }
        }
    }

    for (float& value : albedo) value=clamp(value,.035f,.60f);
    // Solve the bounded material scale instead of brightening the sky or IBL.
    // This keeps even heavily fractured/custom bakes at the existing mean.
    float low=0.f, high=16.f;
    for (int iteration=0; iteration<18; ++iteration)
    {
        if (cancelled(cancel)) return {};
        const float scale=(low+high)*.5f;
        double sum=0.0;
        for (const float value : albedo) sum += static_cast<double>(clamp(value*scale,.035f,.60f));
        if (sum/static_cast<double>(count)<static_cast<double>(TargetAlbedo)) low=scale;
        else high=scale;
    }
    const float scale=(low+high)*.5f;
    Surface surface;
    surface.width=width;
    surface.height=height;
    surface.pixels.resize(count*4u);
    std::uint64_t sum=0;
    for (int y=0; y<height; ++y)
    {
        if (cancelled(cancel)) return {};
        const int prevY=(std::max)(0,y-1), nextY=(std::min)(height-1,y+1);
        const float cosLat=rowCos[static_cast<std::size_t>(y)];
        const float sinLat=rowSin[static_cast<std::size_t>(y)];
        const float polarFade=smooth(.015f,.06f,cosLat);
        for (int x=0; x<width; ++x)
        {
            const std::size_t index=static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(x);
            const int previous=(x+width-1)%width, next=(x+1)%width;
            const float du=(terrain[static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(next)]-
                terrain[static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(previous)])/(2.f*texel);
            const float dv=(terrain[static_cast<std::size_t>(nextY)*static_cast<std::size_t>(width)+static_cast<std::size_t>(x)]-
                terrain[static_cast<std::size_t>(prevY)*static_cast<std::size_t>(width)+static_cast<std::size_t>(x)])/(static_cast<float>(nextY-prevY)*texel);
            const Vec3 east={-sine[static_cast<std::size_t>(x)],0.f,cosine[static_cast<std::size_t>(x)]};
            const Vec3 north={-sinLat*cosine[static_cast<std::size_t>(x)],cosLat,-sinLat*sine[static_cast<std::size_t>(x)]};
            const Vec3 normal=normalized(sphere(x,y)-east*(du/(std::max)(cosLat,.03f)*polarFade)-north*dv);
            surface.pixels[index*4u]=encode(normal.x*.5f+.5f);
            surface.pixels[index*4u+1u]=encode(normal.y*.5f+.5f);
            surface.pixels[index*4u+2u]=encode(normal.z*.5f+.5f);
            surface.pixels[index*4u+3u]=encode(clamp(albedo[index]*scale,.035f,.60f));
            sum += surface.pixels[index*4u+3u];
        }
    }
    surface.meanAlbedo=static_cast<float>(static_cast<double>(sum)/(255.0*static_cast<double>(count)));
    if (cancelled(cancel)) return {};
    return surface;
}

bool save(const Surface& surface, const std::string& path, std::string& error)
{
    error.clear();
    if (surface.width<64 || surface.width>4096 || (surface.width&(surface.width-1))!=0 ||
        surface.height!=surface.width/2 || !finite(surface.meanAlbedo) ||
        surface.meanAlbedo<0.f || surface.meanAlbedo>1.f ||
        surface.pixels.size()!=static_cast<std::size_t>(surface.width)*static_cast<std::size_t>(surface.height)*4u)
    {
        error="Invalid moon surface dimensions, mean albedo, or pixel count.";
        return false;
    }
    namespace fs=std::filesystem;
    fs::path temporaryDirectory;
    const auto cleanup = [&]() {
        if (!temporaryDirectory.empty()) { std::error_code ignored; fs::remove_all(temporaryDirectory,ignored); }
    };
    try
    {
        const fs::path destination=fs::u8path(path);
        if (destination.filename().empty()) { error="A moon surface filename is required."; return false; }
        const fs::path parent=destination.has_parent_path() ? destination.parent_path() : fs::path(".");
        std::error_code ec;
        fs::create_directories(parent,ec);
        if (ec) { error="Cannot create output directory: "+ec.message(); return false; }
        static std::atomic<std::uint64_t> sequence{0};
        const auto clock=std::chrono::steady_clock::now().time_since_epoch().count();
        for (int attempt=0; attempt<64; ++attempt)
        {
            const fs::path candidate=parent/(".moon-bake-"+std::to_string(clock)+"-"+
                std::to_string(sequence.fetch_add(1,std::memory_order_relaxed)));
            ec.clear();
            if (fs::create_directory(candidate,ec)) { temporaryDirectory=candidate; break; }
            if (ec && ec!=std::errc::file_exists) { error="Cannot create temporary output: "+ec.message(); return false; }
        }
        if (temporaryDirectory.empty()) { error="Cannot reserve a temporary moon output."; return false; }
        const fs::path temporaryFile=temporaryDirectory/"moon.surface";
        std::ofstream stream(temporaryFile,std::ios::binary|std::ios::trunc);
        if (!stream) { error="Cannot open temporary moon output."; cleanup(); return false; }
        stream.write("EMOON001",8);
        write32(stream,static_cast<std::uint32_t>(surface.width));
        write32(stream,static_cast<std::uint32_t>(surface.height));
        std::uint32_t meanBits=0;
        std::memcpy(&meanBits,&surface.meanAlbedo,sizeof(meanBits));
        write32(stream,meanBits);
        stream.write(reinterpret_cast<const char*>(surface.pixels.data()),static_cast<std::streamsize>(surface.pixels.size()));
        stream.flush();
        const bool written=stream.good();
        stream.close();
        if (!written || stream.fail()) { error="Failed to write complete moon surface."; cleanup(); return false; }
#if defined(_WIN32)
        if (!MoveFileExW(temporaryFile.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        {
            error="Cannot replace moon surface: "+std::system_category().message(static_cast<int>(GetLastError()));
            cleanup();
            return false;
        }
#else
        fs::rename(temporaryFile,destination,ec);
        if (ec) { error="Cannot replace moon surface: "+ec.message(); cleanup(); return false; }
#endif
        cleanup();
        return true;
    }
    catch (const std::exception& exception)
    {
        error="Cannot save moon surface: "+std::string(exception.what());
        cleanup();
        return false;
    }
}
}
