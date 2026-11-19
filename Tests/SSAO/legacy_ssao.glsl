// Frozen SSAO from ENDURANCE 05a0a5a975976faf438622856d4c71f1e77b3384.
// Original defects deliberately retained as a negative control and timing witness.
// The harness supplies aligned kernel data to avoid undefined buffer reads.
float SSAO(vec3 viewPos, vec3 normal)
{
	ivec2 ts = textureSize(gPosition,0);
	vec3 randomVec = normalize(texture(texNoise, v_UV * vec2(float(ts.x) / 4.0, float(ts.y) / 4.0)).xyz);

	vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
	vec3 bitangent = cross(normal, tangent);
	mat3 TBN = mat3(tangent, bitangent, normal);
	float occlusion = 0.0;
	float radius = 0.5;
	float bias = 0.025;

	for (int i = 0; i < 64; ++i)
	{
		vec3 samplePos = TBN * ubssao.samples[i];
		samplePos = viewPos + samplePos * radius;
		vec4 offset = vec4(samplePos, 1.0);
		offset = ubc.proj * offset;    // from view to clip-space
		offset.xyz /= offset.w;               // perspective divide
		offset.xyz = offset.xyz * 0.5 + 0.5;
		float sampleDepth = texture(gPosition, offset.xy).r;
		occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0);
		float rangeCheck = smoothstep(0.0, 1.0, radius / abs(viewPos.z - sampleDepth));
		occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
	}
	return 1.0 - (occlusion / 64);
}
