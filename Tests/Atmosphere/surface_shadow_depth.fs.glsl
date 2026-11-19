#version 430 core
// Rasterize an inclined receiver and an independently raised occluder into the
// engine's real DEPTH_COMPONENT32 / NEAREST shadow-map format.
in vec2 TexCoords;
uniform vec3 plane; // dz/du, dz/dv, center depth
uniform vec4 obstaclePixels;
uniform float obstacleGap;
void main()
{
    float depth=plane.z+dot(plane.xy,TexCoords-0.5);
    if(all(greaterThanEqual(gl_FragCoord.xy,obstaclePixels.xy)) &&
       all(lessThan(gl_FragCoord.xy,obstaclePixels.zw))) depth-=obstacleGap;
    if(depth<0.0 || depth>1.0) discard;
    gl_FragDepth=depth;
}
