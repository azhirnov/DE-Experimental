#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "structures.fxh"

layout(location=SHADOW_RAY_INDEX)  rayPayloadInEXT ShadowRayPayload   shadowPayload;

void main()
{
	shadowPayload.Shading = 1.0;
}
