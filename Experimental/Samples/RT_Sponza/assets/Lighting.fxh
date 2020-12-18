
uniform accelerationStructureEXT  g_TLAS;

#ifdef PRIMARY_RAY_CAST
    layout(std140) uniform un_CameraAttribs {
        CameraAttribs g_CameraAttribs;
    };
#endif // PRIMARY_RAY_CAST

#ifdef SHADOW_RAY_CAST
    layout(location = SHADOW_RAY_INDEX)  rayPayloadEXT ShadowPayload  shadowPayload;

    layout(std140) uniform un_LightAttribs {
        LightAttribs g_LightAttribs;
    };

    float3  CastShadow (const float3 origin, const float3 direction, const float tmax)
    {
        shadowPayload.Depth = 0.0;
        traceRayEXT(g_TLAS,                        // acceleration structure
                    gl_RayFlagsTerminateOnFirstHitEXT,
                    0xFF,                          // cullMask
                    SHADOW_RAY_INDEX,              // sbtRecordOffset
                    0,                             // sbtRecordStride
                    SHADOW_RAY_INDEX,              // missIndex
                    origin,                        // ray origin
                    0.0,                           // ray min range
                    direction,                     // ray direction
                    tmax,                          // ray max range
                    SHADOW_RAY_INDEX);             // payload location
        return float3(shadowPayload.Depth);
    }

    /*float3  CastShadow (const float3 lightPos, const float3 origin)
    {
        const float3 lightVec = lightPos - origin;
        const float3 lightDir = ; //normalize(lightVec);
        const float  tmax     = 1000.0; //length(lightVec);
    }*/

    float3  LightingPass (const float3 origin, const float3 normal)
    {
        float3 light = float3(0.0f);

        // directional light
        {
            float3 dir = normalize(float3(0.0, -1.0, 0.0));
            light += CastShadow(origin + dir * 0.001, dir, 10000.0);
        }

        //for (uint i = 0; i < g_LightAttribs.OmniLightCount.x; ++i)
        /*{
            const float3 lightPos  = -float3(27.0, 10.0, -2.0); // g_LightAttribs.OmniLights[i].Position.xyz;
            const float  lightDist = length(lightPos - origin);
            const float3 lightDir  = normalize(lightPos - origin);
            //const float  lightR    = g_LightAttribs.OmniLights[i].Position.w;
            float3       shading   = float3(0.0f);

            shading += CastShadow(lightPos, origin);
            light   += //DiffuseLighting(g_LightAttribs.OmniLights[i].Color.rgb, normal, lightDir) *
                       //Attenuation(g_LightAttribs.OmniLights[i].Attenuation.rgb, lightDist) *
                       shading;
        }*/
        return light;
    }
#endif // SHADOW_RAY_CAST
