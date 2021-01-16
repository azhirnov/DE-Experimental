
uniform accelerationStructureEXT  g_TLAS;
layout(std140) uniform un_ConstantsCB { Constants g_ConstantsCB; };

#ifdef CAST_PRIMARY_RAY
layout(location=PRIMARY_RAY_INDEX) rayPayloadEXT   PrimaryRayPayload  primaryPayload;
#elif defined(CAST_SECONDARY_RAY)
layout(location=PRIMARY_RAY_INDEX) rayPayloadInEXT PrimaryRayPayload  primaryPayload;
#else
layout(location=PRIMARY_RAY_INDEX) rayPayloadInEXT PrimaryRayPayload  primaryPayload;
#endif

#ifdef CAST_SHADOW_RAY
layout(location=SHADOW_RAY_INDEX)  rayPayloadEXT   ShadowRayPayload   shadowPayload;
#else
layout(location=SHADOW_RAY_INDEX)  rayPayloadInEXT ShadowRayPayload   shadowPayload;
#endif


#if defined(CAST_PRIMARY_RAY) || defined(CAST_SECONDARY_RAY)
PrimaryRayPayload CastPrimaryRay(RayDesc ray, uint Recursion/* = 0*/, int WaveLengthIndex/* = -1*/)
{
    primaryPayload.Color            = float3(0, 0, 0);
    primaryPayload.Depth            = 0.0;
    primaryPayload.Recursion        = Recursion;
    primaryPayload.WaveLengthIndex  = WaveLengthIndex;

    if (Recursion > g_ConstantsCB.MaxRecursion)
    {
        primaryPayload.Color = float3(0.95, 0.18, 0.95);
        return primaryPayload;
    }
    traceRayEXT(g_TLAS,
                gl_RayFlagsNoneEXT,
                0xFF,
                PRIMARY_RAY_INDEX,
                HIT_GROUP_STRIDE,
                PRIMARY_RAY_INDEX,
                ray.Origin,
                ray.TMin,
                ray.Direction,
                ray.TMax,
                PRIMARY_RAY_INDEX);
    return primaryPayload;
}
#endif


#ifdef CAST_SHADOW_RAY
ShadowRayPayload CastShadow(RayDesc ray, uint Recursion)
{
    shadowPayload.Shading   = 0.0;
    shadowPayload.Recursion = Recursion;
    
    if (Recursion > g_ConstantsCB.MaxRecursion)
    {
        shadowPayload.Shading = 1.0;
        return shadowPayload;
    }
    traceRayEXT(g_TLAS,
                gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT | gl_RayFlagsTerminateOnFirstHitEXT,
                OPAQUE_GEOM_MASK,
                SHADOW_RAY_INDEX,
                HIT_GROUP_STRIDE,
                SHADOW_RAY_INDEX,
                ray.Origin,
                ray.TMin,
                ray.Direction,
                ray.TMax,
                SHADOW_RAY_INDEX);
    return shadowPayload;
}
#endif


void GetRayPerpendicular(float3 dir, out float3 outLeft, out float3 outUp)
{
    const float3 a    = abs(dir);
    const float2 c    = float2(1.0, 0.0);
    const float3 axis = a.x < a.y ? (a.x < a.z ? c.xyy : c.yyx) :
                                    (a.y < a.z ? c.xyx : c.yyx);
    outLeft = normalize(cross(dir, axis));
    outUp   = normalize(cross(dir, outLeft));
}

float3 DirectionWithinCone(float3 dir, float2 offset)
{
    float3 left, up;
    GetRayPerpendicular(dir, left, up);
    return normalize(dir + left * offset.x + up * offset.y);
}

#ifdef CAST_SHADOW_RAY
void LightingPass(inout float3 Color, float3 Pos, float3 Norm, uint Recursion)
{
    RayDesc ray;
    float3  col = float3(0.0);

    ray.Origin = Pos + Norm * SMALL_OFFSET;
    ray.TMin   = 0.0;

    for (int i = 0; i < NUM_LIGHTS; ++i)
    {
        ray.TMax = distance(g_ConstantsCB.LightPos[i].xyz, Pos) * 1.01;

        float3 rayDir = normalize(g_ConstantsCB.LightPos[i].xyz - Pos);
        float  NdotL   = max(0.0, dot(Norm, rayDir));

        if (NdotL > 0.0)
        {
            const int PCF     = Recursion > 1 ? min(1, g_ConstantsCB.ShadowPCF) : g_ConstantsCB.ShadowPCF;
            float     shading = 0.0;
            for (int j = 0; j < PCF; ++j)
            {
                float2 offset = float2(g_ConstantsCB.DiscPoints[j / 2][(j % 2) * 2], g_ConstantsCB.DiscPoints[j / 2][(j % 2) * 2 + 1]);
                ray.Direction = DirectionWithinCone(rayDir, offset * 0.002);
                shading       += clamp(CastShadow(ray, Recursion).Shading, 0.0, 1.0);
            }
            
            shading = PCF > 0 ? shading / float(PCF) : 1.0;

            col += Color * g_ConstantsCB.LightColor[i].rgb * NdotL * shading;
        }
    }
    Color = col * (1.0 / float(NUM_LIGHTS)) + g_ConstantsCB.AmbientColor.rgb;
}
#endif
