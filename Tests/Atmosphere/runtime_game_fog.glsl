// A uniform, purely absorbing game medium: sigma_s=0, sigma_t=2e-5 per metre.
vec4 geGameFog(vec3 worldPosition)
{
    return vec4(0.0,0.0,0.0,2e-5);
}
