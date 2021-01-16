
#ifdef VULKAN
#    define float2   vec2
#    define float3   vec3
#    define float4   vec4
#    define uint2    uvec2
#    define uint3    uvec3
#    define uint4    uvec4
#    define int2     ivec2
#    define int3     ivec3
#    define int4     ivec4
#    define float4x4 mat4x4
#endif

#define MAX_OMNI_lIGHTS 8


struct PrimaryPayload
{
    float3  Color;
    float   Depth;
};

struct ShadowPayload
{
    float  Depth;
};

struct OmniLight
{
    float4  Position;
    float4  Color;
    float4  Attenuation;
};

struct LightAttribs
{
    uint4      OmniLightCount;
    OmniLight  OmniLights [MAX_OMNI_lIGHTS];
};

struct CameraAttribs
{
    float4  Position;      // Camera world position
    float2  ClipPlanes;
    float   ShadowDistance;
    float   _padding0;
    float4  FrustumRayLT;
    float4  FrustumRayLB;
    float4  FrustumRayRT;
    float4  FrustumRayRB;
};

struct MaterialAttribs
{
    float4  DiffuseFactor;
    float4  SpecularFactor;
    float4  BaseColorFactor;
    float4  EmissiveFactor;
    float   MetallicFactor;
    float   RoughnessFactor;
    float2  _padding;
};

struct PrimitiveAttribs
{
    uint   ObjectIDAndMaterialID;   // objectID : 16, materialID : 16
};

struct VertexAttribs
{
    float  posX, posY, posZ;
    float  normX, normY, normZ;
    float  u0, v0;
    float  u1, v1;
};

struct BoxAttribs
{
    float4 min;
    float4 max;
};
