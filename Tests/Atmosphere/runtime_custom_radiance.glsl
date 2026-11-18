// Deliberately simple, so the expected response is independent of the renderer.
vec3 geCustomizeRadiance(vec3 radiance,vec3 direction)
{
    return radiance*0.5;
}
