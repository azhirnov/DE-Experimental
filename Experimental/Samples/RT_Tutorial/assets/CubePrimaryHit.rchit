#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#define CAST_SHADOW_RAY
#include "structures.fxh"
#include "RayUtils.fxh"

hitAttributeEXT float2  hitAttribs;

layout(std140) uniform un_CubeAttribsCB { CubeAttribs g_CubeAttribsCB; };

uniform sampler2D  g_Texture[NUM_TEXTURES];


void main()
{
    float3 barycentrics = float3(1.0 - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);

    uint3 primitive = g_CubeAttribsCB.Primitives[gl_PrimitiveID].xyz;

    float2 uv = g_CubeAttribsCB.UVs[primitive.x].xy * barycentrics.x +
                g_CubeAttribsCB.UVs[primitive.y].xy * barycentrics.y +
                g_CubeAttribsCB.UVs[primitive.z].xy * barycentrics.z;

    float3 normal = g_CubeAttribsCB.Normals[primitive.x].xyz * barycentrics.x +
                    g_CubeAttribsCB.Normals[primitive.y].xyz * barycentrics.y +
                    g_CubeAttribsCB.Normals[primitive.z].xyz * barycentrics.z;
    normal        = normalize(normal * float3x3(gl_ObjectToWorld3x4EXT));

    primaryPayload.Color = textureLod(g_Texture[gl_InstanceCustomIndexEXT], uv, 0).rgb;
    primaryPayload.Depth = gl_HitTEXT;
    
    float3 rayOrigin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    LightingPass(primaryPayload.Color, rayOrigin, normal, primaryPayload.Recursion + 1);
}
