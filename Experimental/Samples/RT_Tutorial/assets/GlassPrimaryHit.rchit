#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#define CAST_SECONDARY_RAY
#include "structures.fxh"
#include "RayUtils.fxh"

hitAttributeEXT float2  hitAttribs;

layout(std140) uniform un_CubeAttribsCB { CubeAttribs  g_CubeAttribsCB; };


float3 LightAbsoption(float3 color, float depth)
{
    float factor = clamp(pow(depth * g_ConstantsCB.GlassOpticalDepth, 1.8) + 0.5, 0.0, 1.0);
    return mix(color, color * g_ConstantsCB.GlassMaterialColor.rgb, factor);
}

float3 BlendWithReflection(float3 srcColor, float3 reflectionColor)
{
    return mix(srcColor, reflectionColor * g_ConstantsCB.GlassReflectionColorMask.rgb, 0.3);
}


void main()
{
    float3 barycentrics = float3(1.0 - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);
    uint3  primitive    = g_CubeAttribsCB.Primitives[gl_PrimitiveID].xyz;
    
    float3 normal = g_CubeAttribsCB.Normals[primitive.x].xyz * barycentrics.x +
                    g_CubeAttribsCB.Normals[primitive.y].xyz * barycentrics.y +
                    g_CubeAttribsCB.Normals[primitive.z].xyz * barycentrics.z;
    normal        = normalize(normal * float3x3(gl_ObjectToWorld3x4EXT));

    float3       resultColor = float3(0.0);
    const uint   recursion   = primaryPayload.Recursion;
    const float  AirIOR      = 1.0;

    if (g_ConstantsCB.GlassEnableInterference > 0 && recursion < 2)
    {
        float3  AccumColor = float3(0.0, 0.0, 0.0);
        float3  AccumMask  = float3(0.0, 0.0, 0.0);

        RayDesc ray;
        ray.TMin = SMALL_OFFSET;
        ray.TMax = 100.0;
    
        const int step = int(MAX_INTERF_SAMPLES / g_ConstantsCB.InterferenceSampleCount);
        for (int i = 0; i < MAX_INTERF_SAMPLES; i += step)
        {
            float3 norm = normal;
            float3 color;

            float  GlassIOR = mix(g_ConstantsCB.GlassIndexOfRefraction.x, g_ConstantsCB.GlassIndexOfRefraction.y, g_ConstantsCB.InterferenceSamples[i].a);

            if (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT)
            {
                ray.Direction = refract(gl_WorldRayDirectionEXT, norm, AirIOR / GlassIOR);
            }
            else if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT)
            {
                norm = -norm;
                ray.Direction = refract(gl_WorldRayDirectionEXT, norm, GlassIOR / AirIOR);
            }
    
            // Total internal reflection.
            if (dot(ray.Direction, float3(1.0)) == 0.0)
            {
                ray.Origin    = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT + norm * SMALL_OFFSET;
                ray.Direction = reflect(gl_WorldRayDirectionEXT, norm);

                PrimaryRayPayload reflPayload = CastPrimaryRay(ray, recursion + 1);
                color = reflPayload.Color * g_ConstantsCB.InterferenceSamples[i].rgb;

                if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT)
                {
                    color = LightAbsoption(color, reflPayload.Depth);
                }
            }
            else
            {
                ray.Origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

                PrimaryRayPayload nextPayload = CastPrimaryRay(ray, recursion + 1);
                color = nextPayload.Color * g_ConstantsCB.InterferenceSamples[i].rgb;
                
                if (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT)
                {
                    color = LightAbsoption(color, nextPayload.Depth);
                }
            }

            AccumColor += color;
            AccumMask  += g_ConstantsCB.InterferenceSamples[i].rgb;
        }
    
        resultColor = AccumColor / AccumMask;
        
        if (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT)
        {
            if (recursion < 1)
            {
                ray.Origin    = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT + normal * SMALL_OFFSET;
                ray.Direction = reflect(gl_WorldRayDirectionEXT, normal);

                PrimaryRayPayload reflPayload = CastPrimaryRay(ray, recursion + 1);
                resultColor = BlendWithReflection(resultColor, reflPayload.Color);
            }
        }
    }
    else
    {
        RayDesc ray;
        ray.Direction = gl_WorldRayDirectionEXT;
        ray.TMin      = SMALL_OFFSET;
        ray.TMax      = 100.0;
      
        if (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT)
        {
            ray.Direction = refract(ray.Direction, normal, AirIOR / g_ConstantsCB.GlassIndexOfRefraction.x);
        }
        else if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT)
        {
            normal = -normal;
            ray.Direction = refract(ray.Direction, normal, g_ConstantsCB.GlassIndexOfRefraction.x / AirIOR);
        }

        // Total internal reflection.
        if (dot(ray.Direction, float3(1.0)) == 0.0)
        {
            ray.Origin    = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT + normal * SMALL_OFFSET;
            ray.Direction = reflect(gl_WorldRayDirectionEXT, normal);

            PrimaryRayPayload reflPayload = CastPrimaryRay(ray, recursion + 1);
            resultColor = reflPayload.Color;
            
            if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT)
            {
                resultColor = LightAbsoption(resultColor, reflPayload.Depth);
            }
        }
        else
        {
            ray.Origin = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;

            PrimaryRayPayload nextPayload = CastPrimaryRay(ray, recursion + 1);

            resultColor = nextPayload.Color;

            if (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT)
            {
                resultColor = LightAbsoption(resultColor, nextPayload.Depth);

                if (recursion < 1)
                {
                    ray.Direction = reflect(gl_WorldRayDirectionEXT, normal);
                    PrimaryRayPayload reflPayload = CastPrimaryRay(ray, recursion + 1);
                    resultColor = BlendWithReflection(resultColor, reflPayload.Color);
                }
            }
        }
    }

    primaryPayload.Color     = resultColor;
    primaryPayload.Depth     = gl_HitTEXT;
    primaryPayload.Recursion = recursion;
}
