#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#define CAST_SECONDARY_RAY
#include "structures.fxh"
#include "RayUtils.fxh"

hitAttributeEXT ProceduralGeomIntersectionAttribs  hitAttribs;

void main()
{
    float3 normal    = normalize(hitAttribs.Normal * float3x3(gl_ObjectToWorld3x4EXT));
    float3 rayDir    = reflect(gl_WorldRayDirectionEXT, normal);
    uint   recursion = primaryPayload.Recursion;

    RayDesc ray;
    ray.Origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT + normal * SMALL_OFFSET;
    ray.TMin   = 0.0;
    ray.TMax   = 100.0;

    float3    color    = float3(0.0, 0.0, 0.0);
    const int ReflBlur = primaryPayload.Recursion > 1 ? 1 : g_ConstantsCB.SphereReflectionBlur;
    for (int j = 0; j < ReflBlur; ++j)
    {
        float2 offset = float2(g_ConstantsCB.DiscPoints[j / 2][(j % 2) * 2], g_ConstantsCB.DiscPoints[j / 2][(j % 2) * 2 + 1]);
        ray.Direction = DirectionWithinCone(rayDir, offset * 0.01);
        color += CastPrimaryRay(ray, recursion + 1).Color;
    }

    color /= float(ReflBlur);
    color *= g_ConstantsCB.SphereReflectionColorMask;

    primaryPayload.Color     = color;
    primaryPayload.Depth     = gl_HitTEXT;
    primaryPayload.Recursion = recursion;
}
