
float  Attenuation (const float3 attenuation, const float dist)
{
    return clamp( 1.0f / (attenuation.x + attenuation.y * dist + attenuation.z * dist * dist), 0.0f, 1.0f );
}

float3  DiffuseLighting (const float3 color, const float3 norm, const float3 lightDir)
{
    float NdotL = max( 0.0f, dot( norm, lightDir ));
    return color * NdotL;
}


layout(std430) readonly buffer un_Primitives
{
    PrimitiveAttribs g_Primitives[];
};

layout(std430) readonly buffer un_Indices
{
    uint g_Indices[];
};

layout(std430) readonly buffer un_PrimitiveOffsets
{
    uint Offsets[];
} g_InstancePrimitiveOffsets [NUM_OBJECTS];

layout(std430) readonly buffer un_VertexAttribs
{
    VertexAttribs g_VertexAttribs[];
};
    
layout(std430) readonly buffer un_MaterialAttribs
{
    MaterialAttribs g_MaterialAttribs[];
};

uniform sampler2DArray g_MaterialColorMaps[NUM_TEXTURES];


float3 TriangleHitAttribsToBaricentrics (const float2 hitAttribs)
{
    return float3(1.0f - hitAttribs.x - hitAttribs.y, hitAttribs.x, hitAttribs.y);
}


float2 BaryLerp (const float2 a, const float2 b, const float2 c, const float3 barycentrics)
{
    return a * barycentrics.x + b * barycentrics.y + c * barycentrics.z;
}

float3 BaryLerp (const float3 a, const float3 b, const float3 c, const float3 barycentrics)
{
    return a * barycentrics.x + b * barycentrics.y + c * barycentrics.z;
}

struct IntermMaterial
{
    float4  color;
    float3  normal;
    float2  uv0;
};

IntermMaterial  ReadMaterial (const float2 hitAttribs)
{
    const float3     barycentrics = TriangleHitAttribsToBaricentrics(hitAttribs);
    const uint       primOffset   = g_InstancePrimitiveOffsets[gl_InstanceID].Offsets[gl_GeometryIndexEXT];
    const uint       firstIndex   = (primOffset + gl_PrimitiveID) * 3;
    PrimitiveAttribs primitive    = g_Primitives[primOffset + gl_PrimitiveID];
    VertexAttribs    attr0        = g_VertexAttribs[g_Indices[ firstIndex + 0 ]];
    VertexAttribs    attr1        = g_VertexAttribs[g_Indices[ firstIndex + 1 ]];
    VertexAttribs    attr2        = g_VertexAttribs[g_Indices[ firstIndex + 2 ]];
    float2           uv0          = BaryLerp(float2(attr0.u0, attr0.v0), float2(attr1.u0, attr1.v0), float2(attr2.u0, attr2.v0), barycentrics);
    float3           normal       = BaryLerp(float3(attr0.normX, attr0.normY, attr0.normZ), float3(attr1.normX, attr1.normY, attr1.normZ), float3(attr2.normX, attr2.normY, attr2.normZ), barycentrics);
    uint             matId        = (primitive.ObjectIDAndMaterialID >> 16);

    IntermMaterial result;
    result.color   = textureLod(g_MaterialColorMaps[nonuniformEXT(matId)], float3(uv0, 0.0), 0.0);
    result.normal  = normal; // TODO: mul with matrix
    result.uv0     = uv0;

    return result;
}

