#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "structures.fxh"

layout(location=PRIMARY_RAY_INDEX) rayPayloadInEXT PrimaryRayPayload  primaryPayload;

const float3 Pallete[] = {
    float3(0.32, 0.00, 0.92),
    float3(0.00, 0.22, 0.90),
    float3(0.02, 0.67, 0.98),
    float3(0.41, 0.79, 1.00),
    float3(0.78, 1.00, 1.00),
    float3(1.00, 1.00, 1.00)
};

void main()
{
    float  factor  = clamp((-gl_WorldRayDirectionEXT.y + 0.5) / 1.5 * 4.0, 0.0, 4.0);
    int    idx     = int(floor(factor));
           factor -= float(idx);
    float3 color  = mix(Pallete[idx], Pallete[idx+1], factor);

    primaryPayload.Color = color;
    primaryPayload.Depth = gl_RayTmaxEXT;
}
