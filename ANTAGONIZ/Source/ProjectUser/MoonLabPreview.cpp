#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "MoonLabPreview.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace EnduranceMoon
{
namespace
{
constexpr float pi=3.14159265358979323846f;
struct Vector { float x,y,z; };
struct Sample { Vector normal; float albedo; };

bool valid(const Surface& surface)
{
    return surface.width>=2 && surface.width<=8192 && surface.height==surface.width/2 &&
        surface.pixels.size()==static_cast<std::size_t>(surface.width)*surface.height*4;
}
bool finite(float value)
{
    std::uint32_t bits=0;
    std::memcpy(&bits,&value,sizeof(bits));
    return (bits&0x7f800000u)!=0x7f800000u;
}
float angle(float value)
{ return finite(value)?std::fmod(value,360.0f)*pi/180.0f:0.0f; }
float dot(Vector a,Vector b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vector normalize(Vector value,Vector fallback)
{
    const float length=std::sqrt(dot(value,value));
    if(length<1e-6f) return fallback;
    return {value.x/length,value.y/length,value.z/length};
}
float smooth(float low,float high,float value)
{
    const float t=std::clamp((value-low)/(high-low),0.0f,1.0f);
    return t*t*(3.0f-2.0f*t);
}
float srgb(float value)
{
    value=std::clamp(value,0.0f,1.0f);
    return value<=0.0031308f?value*12.92f:1.055f*std::pow(value,1.0f/2.4f)-0.055f;
}
std::uint8_t byte(float value)
{ return static_cast<std::uint8_t>(std::clamp(value,0.0f,1.0f)*255.0f+0.5f); }

// Match GL_REPEAT on longitude and GL_CLAMP_TO_EDGE on latitude, including
// texel-center addressing. Atlas row zero is the south pole in the runtime.
Sample sample(const Surface& surface,float u,float v)
{
    const float fx=(u-std::floor(u))*surface.width-0.5f;
    const float fy=std::clamp(v*surface.height-0.5f,0.0f,float(surface.height-1));
    const int left=static_cast<int>(std::floor(fx));
    const int bottom=static_cast<int>(std::floor(fy));
    const int x0=(left+surface.width)%surface.width, x1=(x0+1)%surface.width;
    const int y1=(std::min)(bottom+1,surface.height-1);
    const float tx=fx-std::floor(fx), ty=fy-bottom;
    float channels[4]{};
    for(int c=0;c<4;++c)
    {
        const auto at=[&](int x,int y) {
            return float(surface.pixels[(static_cast<std::size_t>(y)*surface.width+x)*4+c])/255.0f;
        };
        const float a=at(x0,bottom)*(1.0f-tx)+at(x1,bottom)*tx;
        const float b=at(x0,y1)*(1.0f-tx)+at(x1,y1)*tx;
        channels[c]=a*(1.0f-ty)+b*ty;
    }
    return {{channels[0]*2.0f-1.0f,channels[1]*2.0f-1.0f,channels[2]*2.0f-1.0f},channels[3]};
}
}

PreviewImage renderSphere(const Surface& surface,float yawDegrees,float sunDegrees,
    int resolution,const std::atomic_bool* cancel)
{
    if(!valid(surface) || (cancel && cancel->load())) return {};
    resolution=std::clamp(resolution,64,1024);
    PreviewImage image;
    image.width=image.height=resolution;
    image.pixels.resize(static_cast<std::size_t>(resolution)*resolution*4);
    const float yaw=angle(yawDegrees), phase=angle(sunDegrees);
    const float c=std::cos(yaw), s=std::sin(yaw);
    const Vector light=normalize({std::sin(phase),0.22f,std::cos(phase)},{0,0,1});
    const float edge=1.0f/(resolution*0.455f);
    for(int row=0;row<resolution;++row)
    {
        if(cancel && cancel->load()) return {};
        for(int column=0;column<resolution;++column)
        {
            const float x=(column+0.5f-resolution*0.5f)/(resolution*0.455f);
            const float y=(resolution*0.5f-row-0.5f)/(resolution*0.455f);
            const float radius=std::sqrt(x*x+y*y);
            float value=0.0f;
            float coverage=0.0f;
            if(radius<=1.0f)
            {
                const Vector geometric{x,y,std::sqrt((std::max)(0.0f,1.0f-radius*radius))};
                const Vector local{c*x-s*geometric.z,y,s*x+c*geometric.z};
                const float u=std::atan2(local.z,local.x)/(2.0f*pi)+0.5f;
                const float v=std::asin(std::clamp(local.y,-1.0f,1.0f))/pi+0.5f;
                const auto surfaceSample=sample(surface,u,v);
                const auto n=surfaceSample.normal;
                const Vector mapped{c*n.x+s*n.z,n.y,-s*n.x+c*n.z};
                const float radial=dot(mapped,geometric);
                // Match the default atmosphere normal strength (0.65), while
                // keeping a positive geometric component at steep fractures.
                constexpr float normalStrength=0.65f;
                const float correction=(std::max)(radial,0.10f)-normalStrength*radial;
                const Vector normal=normalize({normalStrength*mapped.x+geometric.x*correction,
                    normalStrength*mapped.y+geometric.y*correction,
                    normalStrength*mapped.z+geometric.z*correction},geometric);
                const float day=smooth(-0.015f,0.04f,dot(geometric,light));
                const float diffuse=(std::max)(0.0f,dot(normal,light))*day;
                // Fixed editor exposure; shading follows the runtime's normal
                // reconstruction and geometric terminator, without atmosphere.
                value=srgb(surfaceSample.albedo*diffuse*2.2f);
                coverage=1.0f-smooth(1.0f-edge,1.0f,radius);
            }
            auto* pixel=&image.pixels[(static_cast<std::size_t>(row)*resolution+column)*4];
            pixel[0]=byte(value*coverage+(9.0f/255.0f)*(1.0f-coverage));
            pixel[1]=byte(value*coverage+(12.0f/255.0f)*(1.0f-coverage));
            pixel[2]=byte(value*coverage+(20.0f/255.0f)*(1.0f-coverage));
            pixel[3]=255;
        }
    }
    return image;
}

PreviewImage renderAtlas(const Surface& surface,bool normals)
{
    if(!valid(surface)) return {};
    PreviewImage image;
    image.width=(std::min)(surface.width,1024);
    image.height=image.width/2;
    image.pixels.resize(static_cast<std::size_t>(image.width)*image.height*4);
    for(int row=0;row<image.height;++row)
        for(int column=0;column<image.width;++column)
        {
            const auto surfaceSample=sample(surface,(column+0.5f)/image.width,
                1.0f-(row+0.5f)/image.height);
            auto* pixel=&image.pixels[(static_cast<std::size_t>(row)*image.width+column)*4];
            if(normals)
            {
                pixel[0]=byte(surfaceSample.normal.x*0.5f+0.5f);
                pixel[1]=byte(surfaceSample.normal.y*0.5f+0.5f);
                pixel[2]=byte(surfaceSample.normal.z*0.5f+0.5f);
            }
            else pixel[0]=pixel[1]=pixel[2]=byte(srgb(surfaceSample.albedo));
            pixel[3]=255;
        }
    return image;
}
}
