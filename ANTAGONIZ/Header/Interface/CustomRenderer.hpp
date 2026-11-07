#ifndef __CUSTOM_RENDERER__
#define __CUSTOM_RENDERER__

// Called inside the engine's opaque/depth passes. Implementations must restore
// every OpenGL binding and render state they change; the framebuffer and viewport
// already belong to the current pass. Do not register/unregister during drawing.
class CustomRenderer
{
public:
	virtual ~CustomRenderer() = default;
	// Once per frame, after the active camera has moved and before any render pass.
	virtual void prepareRenderer() {}
	// SSBO 0 contains UniformBufferCamera.
	virtual void drawRenderer() = 0;
	// SSBO 0 contains the engine's StructShadow array. Optional for older renderers.
	virtual void drawShadowRenderer(unsigned int /*shadowIndex*/) {}
};

#endif // __CUSTOM_RENDERER__
