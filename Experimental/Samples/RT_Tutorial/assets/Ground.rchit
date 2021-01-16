#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#define CAST_SHADOW_RAY
#include "structures.h"
#include "RayUtils.h"

hitAttributeEXT float2  hitAttribs;

layout(std140) uniform un_CubeAttribsCB { CubeAttribs g_CubeAttribsCB; };

uniform sampler2D  g_GroundTexture;


void main()
{
    float3 barycentrics = float3(1.0 - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);
    
    uint3 primitive = g_CubeAttribsCB.Primitives[gl_PrimitiveID].xyz;
    
    float2 uv = g_CubeAttribsCB.UVs[primitive.x].xy * barycentrics.x +
                g_CubeAttribsCB.UVs[primitive.y].xy * barycentrics.y +
                g_CubeAttribsCB.UVs[primitive.z].xy * barycentrics.z;
    uv *= 32.0; // tiling

    primaryPayload.Color = textureLod(g_GroundTexture, uv, 0).rgb;
    primaryPayload.Depth = gl_HitTEXT;

    float3  origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    float3  normal = float3(0.0, -1.0, 0.0);

    LightingPass(primaryPayload.Color, origin, normal, primaryPayload.Recursion + 1);
}
