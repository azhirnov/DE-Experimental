#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "structures.h"

layout(location=SHADOW_RAY_INDEX)  rayPayloadInEXT ShadowRayPayload   shadowPayload;

void main()
{
	shadowPayload.Shading = 1.0;
}
