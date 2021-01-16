#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "structures.h"

layout(location = PRIMARY_RAY_INDEX) rayPayloadInEXT ShadowPayload  payload;

void main ()
{
    payload.Depth = 1.0;
}
