#ifndef __CUSTOM_RENDERER__
#define __CUSTOM_RENDERER__

#include "ShadowCasterBounds.hpp"

// Called inside the engine's opaque/depth passes. Implementations must restore
// every OpenGL binding and render state they change; the framebuffer and viewport
// already belong to the current pass. Do not register/unregister during drawing.
class CustomRenderer
{
public:
	virtual ~CustomRenderer() = default;
	// Once per frame, after the active camera has moved and before any render pass.
	virtual void prepareRenderer() {}
	// Optional CPU bounds of all published casters, including off-screen ones.
	// Return zero when unsupported; allocate a fresh token with
	// Ge::nextShadowCasterBoundsRevision() whenever the published set changes.
	// Tokens distinguish restarts and address reuse; no GPU readback is needed.
	virtual std::uint64_t shadowCasterBoundsRevision() const { return 0; }
	virtual void appendShadowCasterBounds(std::vector<Ge::ShadowCasterBounds>& /*output*/) const {}
	// SSBO 0 contains UniformBufferCamera.
	virtual void drawRenderer() = 0;
	// SSBO 0 contains the engine's StructShadow array. Optional for older renderers.
	virtual void drawShadowRenderer(unsigned int /*shadowIndex*/) {}
};

#endif // __CUSTOM_RENDERER__
