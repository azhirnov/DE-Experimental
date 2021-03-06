#version 460
#extension GL_EXT_ray_tracing : require
#extension GL_GOOGLE_include_directive : require

#include "structures.fxh"

layout(location = PRIMARY_RAY_INDEX) rayPayloadEXT PrimaryPayload  payload;

#define PRIMARY_RAY_CAST
#include "Lighting.fxh"

layout(rgba16f) uniform image2D  g_ColorBuffer;
layout(r32f)    uniform image2D  g_DepthBuffer;

void main ()
{
    float2  uv        = float2(gl_LaunchIDEXT.xy) / float2(gl_LaunchSizeEXT.xy - 1);
    float3  origin    = g_CameraAttribs.Position.xyz;
    float3  direction = normalize(mix(mix(g_CameraAttribs.FrustumRayLB, g_CameraAttribs.FrustumRayRB, uv.x),
                                      mix(g_CameraAttribs.FrustumRayLT, g_CameraAttribs.FrustumRayRT, uv.x), uv.y)).xyz;

    traceRayEXT(g_TLAS,                       // acceleration structure
                gl_RayFlagsNoneEXT,           // rayFlags
                0xFF,                         // cullMask
                PRIMARY_RAY_INDEX,            // sbtRecordOffset
                0,                            // sbtRecordStride
                PRIMARY_RAY_INDEX,            // missIndex
                origin,                       // ray origin
                g_CameraAttribs.ClipPlanes.x, // ray min range
                direction,                    // ray direction
                g_CameraAttribs.ClipPlanes.y, // ray max range
                PRIMARY_RAY_INDEX);           // payload location

    imageStore(g_ColorBuffer, int2(gl_LaunchIDEXT), float4(payload.Color, 1.0));
    imageStore(g_DepthBuffer, int2(gl_LaunchIDEXT), vec4((payload.Depth - g_CameraAttribs.ClipPlanes.x) / g_CameraAttribs.ClipPlanes.y));
}
