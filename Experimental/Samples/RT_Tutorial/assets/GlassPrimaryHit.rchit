#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#define CAST_SECONDARY_RAY
#include "structures.h"
#include "RayUtils.h"

hitAttributeEXT float2  hitAttribs;

layout(std140) uniform un_CubeAttribsCB { CubeAttribs  g_CubeAttribsCB; };


float3 LightAbsorption(float3 color, float depth, float absorption)
{
    float f = clamp(exp(-depth * (1.0 - absorption) * g_ConstantsCB.GlassAbsorptionScale), 0.0, 1.0);
    return color * f;
}

float3 BlendWithReflection(float3 srcColor, float3 reflectionColor, float factor)
{
    return mix(srcColor, reflectionColor * g_ConstantsCB.GlassReflectionColorMask.rgb, factor);
}

// http://www.pbr-book.org/3ed-2018/Reflection_Models/Specular_Reflection_and_Transmission.html
float Fresnel(float relIOR, float cosThetaI)
{
    cosThetaI = clamp(cosThetaI, -1.0, 1.0);
    if (cosThetaI < 0.0)
    {
        relIOR = 1.0 / relIOR;
        cosThetaI = -cosThetaI;
    }

    float sinThetaTSq = relIOR * relIOR * (1.0 - cosThetaI * cosThetaI);
    if (sinThetaTSq > 1.0)
        return 1.0;

    float cosThetaT = sqrt(1.0 - sinThetaTSq);

    float Rs = (relIOR * cosThetaI - cosThetaT) / (relIOR * cosThetaI + cosThetaT);
    float Rp = (relIOR * cosThetaT - cosThetaI) / (relIOR * cosThetaT + cosThetaI);

    return 0.5 * (Rs * Rs + Rp * Rp);
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
    const int    wavelen_idx = primaryPayload.WaveLengthIndex;
    const float  AirIOR      = 1.0;

    if (wavelen_idx < 0)
    {
        float3  AccumColor = float3(0.0, 0.0, 0.0);
        float3  AccumMask  = float3(0.0, 0.0, 0.0);

        RayDesc ray;
        ray.TMin = SMALL_OFFSET;
        ray.TMax = 100.0;
    
        const int step = int(MAX_DISPERS_SAMPLES / g_ConstantsCB.DispersionSampleCount);
        for (int i = 0; i < MAX_DISPERS_SAMPLES; i += step)
        {
            float3 norm         = normal;
            float  relIOR;
            float  glassIOR     = mix(g_ConstantsCB.GlassIndexOfRefraction.x, g_ConstantsCB.GlassIndexOfRefraction.y, g_ConstantsCB.DispersionSamples[i].a);
            float3 colorMask    = g_ConstantsCB.DispersionSamples[i].rgb; // RGB color for wavelength
            float3 rayDir       = gl_WorldRayDirectionEXT;
            float  absorption   = g_ConstantsCB.Absorption[i/4][i&3];

            if (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT)
            {
                relIOR = AirIOR / glassIOR;
                rayDir = refract(rayDir, norm, relIOR);
            }
            else if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT)
            {
                relIOR = glassIOR / AirIOR;
                norm   = -norm;
                rayDir = refract(rayDir, norm, relIOR);
            }
    
            float  fresnel  = Fresnel(relIOR, dot(gl_WorldRayDirectionEXT, -norm));
            float3 curColor = float3(0.0);
            float3 reflColor;

            // reflection
            {
                ray.Origin    = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT + norm * SMALL_OFFSET;
                ray.Direction = reflect(gl_WorldRayDirectionEXT, norm);

                PrimaryRayPayload reflPayload = CastPrimaryRay(ray, recursion + 1, i);
                reflColor = reflPayload.Color;

                if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT)
                {
                    reflColor = LightAbsorption(reflColor, reflPayload.Depth, absorption);
                }
            }

            // refraction
            if (fresnel < 1.0)
            {
                ray.Origin    = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
                ray.Direction = rayDir;

                PrimaryRayPayload nextPayload = CastPrimaryRay(ray, recursion + 1, i);
                curColor = nextPayload.Color;
                
                if (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT)
                {
                    curColor = LightAbsorption(curColor, nextPayload.Depth, absorption);
                }
            }
            
            curColor    = BlendWithReflection(curColor, reflColor, fresnel);
            AccumColor += curColor * colorMask;
            AccumMask  += colorMask;
        }
    
        resultColor = AccumColor / AccumMask;
    }
    else
    {
        float absorption = g_ConstantsCB.Absorption[wavelen_idx/4][wavelen_idx&3];

        RayDesc ray;
        ray.TMin = SMALL_OFFSET;
        ray.TMax = 100.0;

        float3 reflDir = gl_WorldRayDirectionEXT;
        float  relIOR;
      
        if (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT)
        {
            relIOR  = AirIOR / g_ConstantsCB.GlassIndexOfRefraction.x;
            reflDir = refract(reflDir, normal, relIOR);
        }
        else if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT)
        {
            relIOR  = g_ConstantsCB.GlassIndexOfRefraction.x / AirIOR;
            normal  = -normal;
            reflDir = refract(reflDir, normal, relIOR);
        }

        float  fresnel   = Fresnel(relIOR, dot(gl_WorldRayDirectionEXT, -normal));
        float3 reflColor = float3(0.0);

        // reflection
        if (fresnel > 0.0)
        {
            ray.Origin    = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT + normal * SMALL_OFFSET;
            ray.Direction = reflect(gl_WorldRayDirectionEXT, normal);

            PrimaryRayPayload reflPayload = CastPrimaryRay(ray, recursion + 1, wavelen_idx);
            reflColor = reflPayload.Color;
            
            if (gl_HitKindEXT == gl_HitKindBackFacingTriangleEXT)
            {
                reflColor = LightAbsorption(reflColor, reflPayload.Depth, absorption);
            }
        }
        
        // refraction
        if (fresnel < 1.0)
        {
            ray.Origin    = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
            ray.Direction = reflDir;

            PrimaryRayPayload nextPayload = CastPrimaryRay(ray, recursion + 1, wavelen_idx);
            resultColor = nextPayload.Color;

            if (gl_HitKindEXT == gl_HitKindFrontFacingTriangleEXT || recursion == 0)
            {
                resultColor = LightAbsorption(resultColor, nextPayload.Depth, absorption);
            }
        }
        
        resultColor = BlendWithReflection(resultColor, reflColor, fresnel);
    }

    primaryPayload.Color            = resultColor;
    primaryPayload.Depth            = gl_HitTEXT;
    primaryPayload.Recursion        = recursion;
    primaryPayload.WaveLengthIndex  = wavelen_idx;
}
