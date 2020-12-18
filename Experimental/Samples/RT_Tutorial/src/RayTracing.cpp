#include "RayTracing.hpp"
#include "MapHelper.hpp"
#include "GraphicsUtilities.h"
#include "TextureUtilities.h"
#include "ShaderMacroHelper.hpp"
#include "imgui.h"
#include "ImGuiUtils.hpp"
#include "AdvancedMath.hpp"
#include "PlatformMisc.hpp"

namespace Diligent
{

SampleBase* CreateSample()
{
    return new RayTracing();
}

RayTracing::RayTracing() :
    m_ShaderDebugger{SHADER_TRACE_DLL}
{
}

void RayTracing::CreateGraphicsPSO()
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;

    PSOCreateInfo.PSODesc.Name         = "Image blit PSO";
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = m_pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

    ShaderCreateInfo ShaderCI;
    ShaderCI.UseCombinedTextureSamplers = true;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
    ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

    RefCntAutoPtr<IShader> pVS, pPS;
    m_ShaderDebugger.CompileFromFile(&pVS, SHADER_TYPE_VERTEX, "ImageBlit.vsh", "Image blit VS");
    m_ShaderDebugger.CompileFromFile(&pPS, SHADER_TYPE_PIXEL, "ImageBlit.psh", "Image blit PS");

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    SamplerDesc SamLinearClampDesc{
        FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
        TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP};

    ImmutableSamplerDesc ImmutableSamplers[] = {{SHADER_TYPE_PIXEL, "g_Texture", SamLinearClampDesc}};

    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers    = ImmutableSamplers;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImmutableSamplers);
    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType  = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

    m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pImageBlitPSO);
    VERIFY_EXPR(m_pImageBlitPSO != nullptr);

    m_pImageBlitPSO->CreateShaderResourceBinding(&m_pImageBlitSRB, true);
    VERIFY_EXPR(m_pImageBlitSRB != nullptr);
}

void RayTracing::CreateRayTracingPSO()
{
    try
    {
        m_pRayTracingPSO = nullptr;
        m_pRayTracingSRB = nullptr;

        RayTracingPipelineStateCreateInfo PSOCreateInfo;

        PSOCreateInfo.PSODesc.Name         = "Ray tracing PSO";
        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_RAY_TRACING;

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("NUM_TEXTURES", NumTextures);

        RefCntAutoPtr<IShader> pRG;
        m_ShaderDebugger.CompileFromFile(&pRG, SHADER_TYPE_RAY_GEN, "RayTrace.rgen", "Ray tracing RG", Macros, DE::EShaderDebugMode::ClockHeatmap);
        CHECK_THROW(pRG);

        RefCntAutoPtr<IShader> pPrimaryMiss, pShadowMiss;
        m_ShaderDebugger.CompileFromFile(&pPrimaryMiss, SHADER_TYPE_RAY_MISS, "PrimaryMiss.rmiss", "Primary ray miss shader", Macros);
        m_ShaderDebugger.CompileFromFile(&pShadowMiss, SHADER_TYPE_RAY_MISS, "ShadowMiss.rmiss", "Shadow ray miss shader", Macros);
        CHECK_THROW(pPrimaryMiss && pShadowMiss);

        RefCntAutoPtr<IShader> pCubePrimaryHit, pGroundHit, pGlassPrimaryHit, pSpherePrimaryHit;
        m_ShaderDebugger.CompileFromFile(&pCubePrimaryHit, SHADER_TYPE_RAY_CLOSEST_HIT, "CubePrimaryHit.rchit", "Cube primary ray closest hit shader", Macros);
        m_ShaderDebugger.CompileFromFile(&pGroundHit, SHADER_TYPE_RAY_CLOSEST_HIT, "Ground.rchit", "Ground primary ray closest hit shader", Macros);
        m_ShaderDebugger.CompileFromFile(&pGlassPrimaryHit, SHADER_TYPE_RAY_CLOSEST_HIT, "GlassPrimaryHit.rchit", "Glass primary ray closest hit shader", Macros);
        m_ShaderDebugger.CompileFromFile(&pSpherePrimaryHit, SHADER_TYPE_RAY_CLOSEST_HIT, "SpherePrimaryHit.rchit", "Sphere primary ray closest hit shader", Macros);
        CHECK_THROW(pCubePrimaryHit && pGroundHit && pGlassPrimaryHit && pSpherePrimaryHit);

        RefCntAutoPtr<IShader> pSphereIntersection;
        m_ShaderDebugger.CompileFromFile(&pSphereIntersection, SHADER_TYPE_RAY_INTERSECTION, "SphereIntersection.rint", "Sphere intersection shader", Macros);
        CHECK_THROW(pSphereIntersection);

        const RayTracingGeneralShaderGroup GeneralShaders[] = //
            {
                {"Main", pRG},
                {"PrimaryMiss", pPrimaryMiss},
                {"ShadowMiss", pShadowMiss} //
            };
        const RayTracingTriangleHitShaderGroup TriangleHitShaders[] = //
            {
                {"CubePrimaryHit", pCubePrimaryHit},
                {"GroundHit", pGroundHit},
                {"GlassPrimaryHit", pGlassPrimaryHit} //
            };
        const RayTracingProceduralHitShaderGroup ProceduralHitShaders[] = //
            {
                {"SpherePrimaryHit", pSphereIntersection, pSpherePrimaryHit},
                {"SphereShadowHit", pSphereIntersection} //
            };

        PSOCreateInfo.pGeneralShaders          = GeneralShaders;
        PSOCreateInfo.GeneralShaderCount       = _countof(GeneralShaders);
        PSOCreateInfo.pTriangleHitShaders      = TriangleHitShaders;
        PSOCreateInfo.TriangleHitShaderCount   = _countof(TriangleHitShaders);
        PSOCreateInfo.pProceduralHitShaders    = ProceduralHitShaders;
        PSOCreateInfo.ProceduralHitShaderCount = _countof(ProceduralHitShaders);

        PSOCreateInfo.RayTracingPipeline.MaxRecursionDepth = MaxRecursionDepth;

        SamplerDesc SamLinearWrapDesc{
            FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
            TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP, TEXTURE_ADDRESS_WRAP //
        };
        ImmutableSamplerDesc ImmutableSamplers[] = //
            {
                {SHADER_TYPE_RAY_CLOSEST_HIT, "g_Texture", SamLinearWrapDesc},
                {SHADER_TYPE_RAY_CLOSEST_HIT, "g_GroundTexture", SamLinearWrapDesc} //
            };

        PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers    = ImmutableSamplers;
        PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImmutableSamplers);
        PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType  = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

        m_ShaderDebugger.CreatePipeline(PSOCreateInfo, &m_pRayTracingPSO);
        CHECK_THROW(m_pRayTracingPSO != nullptr);

        m_ShaderDebugger.CreateSRB(m_pRayTracingPSO, &m_pRayTracingSRB);
        CHECK_THROW(m_pRayTracingSRB != nullptr);
    }
    catch (...)
    {
        m_pRayTracingPSO = nullptr;
        m_pRayTracingSRB = nullptr;
    }
}

void RayTracing::LoadTextures()
{
    StateTransitionDesc Barriers[NumTextures];
    for (int tex = 0; tex < NumTextures; ++tex)
    {
        // Load current texture
        TextureLoadInfo loadInfo;
        loadInfo.IsSRGB = true;

        std::stringstream FileNameSS;
        FileNameSS << "DGLogo" << tex << ".png";
        auto FileName = FileNameSS.str();
        CreateTextureFromFile(FileName.c_str(), loadInfo, m_pDevice, &m_Textures[tex]);

        Barriers[tex] = StateTransitionDesc{m_Textures[tex], RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true};
    }
    m_pImmediateContext->TransitionResourceStates(_countof(Barriers), Barriers);

    CreateTextureFromFile("Ground.jpg", TextureLoadInfo{}, m_pDevice, &m_pGroundTex);
}

void RayTracing::CreateCubeBLAS()
{
    // clang-format off
    const float3 CubePos[] =
    {
        float3(-1,-1,-1), float3(-1,+1,-1), float3(+1,+1,-1), float3(+1,-1,-1),
        float3(-1,-1,-1), float3(-1,-1,+1), float3(+1,-1,+1), float3(+1,-1,-1),
        float3(+1,-1,-1), float3(+1,-1,+1), float3(+1,+1,+1), float3(+1,+1,-1),
        float3(+1,+1,-1), float3(+1,+1,+1), float3(-1,+1,+1), float3(-1,+1,-1),
        float3(-1,+1,-1), float3(-1,+1,+1), float3(-1,-1,+1), float3(-1,-1,-1),
        float3(-1,-1,+1), float3(+1,-1,+1), float3(+1,+1,+1), float3(-1,+1,+1)
    };

    const float4 CubeUV[] = 
    {
        float4(0,0,0,0), float4(0,1,0,0), float4(1,1,0,0), float4(1,0,0,0),
        float4(0,0,0,0), float4(0,1,0,0), float4(1,1,0,0), float4(1,0,0,0),
        float4(0,0,0,0), float4(1,0,0,0), float4(1,1,0,0), float4(0,1,0,0),
        float4(0,0,0,0), float4(0,1,0,0), float4(1,1,0,0), float4(1,0,0,0),
        float4(1,1,0,0), float4(0,1,0,0), float4(0,0,0,0), float4(1,0,0,0),
        float4(1,0,0,0), float4(0,0,0,0), float4(0,1,0,0), float4(1,1,0,0)
    };

    const float4 CubeNormals[] = 
    {
        float4(0, 0, -1, 0), float4(0, 0, -1, 0), float4(0, 0, -1, 0), float4(0, 0, -1, 0),
        float4(0, -1, 0, 0), float4(0, -1, 0, 0), float4(0, -1, 0, 0), float4(0, -1, 0, 0),
        float4(+1, 0, 0, 0), float4(+1, 0, 0, 0), float4(+1, 0, 0, 0), float4(+1, 0, 0, 0),
        float4(0, +1, 0, 0), float4(0, +1, 0, 0), float4(0, +1, 0, 0), float4(0, +1, 0, 0),
        float4(-1, 0, 0, 0), float4(-1, 0, 0, 0), float4(-1, 0, 0, 0), float4(-1, 0, 0, 0),
        float4(0, 0, +1, 0), float4(0, 0, +1, 0), float4(0, 0, +1, 0), float4(0, 0, +1, 0)
    };

    const uint Indices[] =
    {
        2,0,1,    2,3,0,
        4,6,5,    4,7,6,
        8,10,9,   8,11,10,
        12,14,13, 12,15,14,
        16,18,17, 16,19,18,
        20,21,22, 20,22,23
    };
    // clang-format on

    {
        CubeAttribs Attribs;

        static_assert(sizeof(Attribs.UVs) == sizeof(CubeUV), "size mismatch");
        std::memcpy(Attribs.UVs, CubeUV, sizeof(CubeUV));

        static_assert(sizeof(Attribs.Normals) == sizeof(CubeNormals), "size mismatch");
        std::memcpy(Attribs.Normals, CubeNormals, sizeof(CubeNormals));

        for (Uint32 i = 0; i < _countof(Indices); i += 3)
            Attribs.Primitives[i / 3] = uint4{Indices[i], Indices[i + 1], Indices[i + 2], 0};

        BufferData BufData = {&Attribs, sizeof(Attribs)};
        BufferDesc BuffDesc;
        BuffDesc.Name          = "Cube Attribs";
        BuffDesc.Usage         = USAGE_IMMUTABLE;
        BuffDesc.BindFlags     = BIND_UNIFORM_BUFFER;
        BuffDesc.uiSizeInBytes = sizeof(Attribs);

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_CubeAttribsCB);
        VERIFY_EXPR(m_CubeAttribsCB != nullptr);
    }

    RefCntAutoPtr<IBuffer> CubeVertexBuffer;
    {
        BufferData BufData = {CubePos, sizeof(CubePos)};
        BufferDesc BuffDesc;
        BuffDesc.Name          = "Cube vertices";
        BuffDesc.Usage         = USAGE_IMMUTABLE;
        BuffDesc.BindFlags     = BIND_RAY_TRACING;
        BuffDesc.uiSizeInBytes = sizeof(CubePos);

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &CubeVertexBuffer);
        VERIFY_EXPR(CubeVertexBuffer != nullptr);
    }

    RefCntAutoPtr<IBuffer> CubeIndexBuffer;
    {
        BufferData BufData = {Indices, sizeof(Indices)};
        BufferDesc BuffDesc;
        BuffDesc.Name          = "Cube indices";
        BuffDesc.Usage         = USAGE_IMMUTABLE;
        BuffDesc.BindFlags     = BIND_RAY_TRACING;
        BuffDesc.uiSizeInBytes = sizeof(Indices);

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &CubeIndexBuffer);
        VERIFY_EXPR(CubeIndexBuffer != nullptr);
    }

    {
        BLASTriangleDesc Triangles;
        {
            Triangles.GeometryName         = "Cube";
            Triangles.MaxVertexCount       = _countof(CubePos);
            Triangles.VertexValueType      = VT_FLOAT32;
            Triangles.VertexComponentCount = 3;
            Triangles.MaxPrimitiveCount    = _countof(Indices) / 3;
            Triangles.IndexType            = VT_UINT32;

            BottomLevelASDesc ASDesc;
            ASDesc.Name          = "Cube BLAS";
            ASDesc.Flags         = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
            ASDesc.pTriangles    = &Triangles;
            ASDesc.TriangleCount = 1;

            m_pDevice->CreateBLAS(ASDesc, &m_pCubeBLAS);
            VERIFY_EXPR(m_pCubeBLAS != nullptr);
        }

        RefCntAutoPtr<IBuffer> pScratchBuffer;
        {
            BufferDesc BuffDesc;
            BuffDesc.Name          = "BLAS Scratch Buffer";
            BuffDesc.Usage         = USAGE_DEFAULT;
            BuffDesc.BindFlags     = BIND_RAY_TRACING;
            BuffDesc.uiSizeInBytes = m_pCubeBLAS->GetScratchBufferSizes().Build;

            m_pDevice->CreateBuffer(BuffDesc, nullptr, &pScratchBuffer);
            VERIFY_EXPR(pScratchBuffer != nullptr);
        }

        BLASBuildTriangleData TriangleData;
        TriangleData.GeometryName         = Triangles.GeometryName;
        TriangleData.pVertexBuffer        = CubeVertexBuffer;
        TriangleData.VertexStride         = sizeof(CubePos[0]);
        TriangleData.VertexCount          = Triangles.MaxVertexCount;
        TriangleData.VertexValueType      = Triangles.VertexValueType;
        TriangleData.VertexComponentCount = Triangles.VertexComponentCount;
        TriangleData.pIndexBuffer         = CubeIndexBuffer;
        TriangleData.PrimitiveCount       = Triangles.MaxPrimitiveCount;
        TriangleData.IndexType            = Triangles.IndexType;
        TriangleData.Flags                = RAYTRACING_GEOMETRY_FLAG_OPAQUE;

        BuildBLASAttribs Attribs;
        Attribs.pBLAS                       = m_pCubeBLAS;
        Attribs.pTriangleData               = &TriangleData;
        Attribs.TriangleDataCount           = 1;
        Attribs.pScratchBuffer              = pScratchBuffer;
        Attribs.BLASTransitionMode          = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Attribs.GeometryTransitionMode      = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Attribs.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

        m_pImmediateContext->BuildBLAS(Attribs);
    }
}

void RayTracing::CreateProceduralBLAS()
{
    struct AABB
    {
        float minX, minY, minZ;
        float maxX, maxY, maxZ;
        float padding0, padding1;

        AABB(float _minX, float _minY, float _minZ, float _maxX, float _maxY, float _maxZ) :
            minX{_minX}, minY{_minY}, minZ{_minZ}, maxX{_maxX}, maxY{_maxY}, maxZ{_maxZ}
        {}
    };
    static_assert(sizeof(AABB) % 16 == 0, "must be aligned by 16 bytes");
    static_assert(sizeof(AABB) == sizeof(BoxAttribs), "size mismatch");

    const AABB Boxes[] = {AABB{-1.5f, -1.5f, -1.5f, 1.5f, 1.5f, 1.5f}};

    // Create box buffer
    {
        BufferData BufData = {Boxes, sizeof(Boxes)};
        BufferDesc BuffDesc;
        BuffDesc.Name              = "AABB Buffer";
        BuffDesc.Usage             = USAGE_IMMUTABLE;
        BuffDesc.BindFlags         = BIND_RAY_TRACING | BIND_SHADER_RESOURCE;
        BuffDesc.uiSizeInBytes     = sizeof(Boxes);
        BuffDesc.ElementByteStride = sizeof(Boxes[0]);
        BuffDesc.Mode              = BUFFER_MODE_STRUCTURED;

        m_pDevice->CreateBuffer(BuffDesc, &BufData, &m_BoxAttribsCB);
        VERIFY_EXPR(m_BoxAttribsCB != nullptr);
    }

    // Create & build bottom level acceleration structure
    {
        // Create BLAS
        BLASBoundingBoxDesc BoxInfo;
        {
            BoxInfo.GeometryName = "Box";
            BoxInfo.MaxBoxCount  = 1;

            BottomLevelASDesc ASDesc;
            ASDesc.Name     = "Procedural BLAS";
            ASDesc.Flags    = RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;
            ASDesc.pBoxes   = &BoxInfo;
            ASDesc.BoxCount = 1;

            m_pDevice->CreateBLAS(ASDesc, &m_pProceduralBLAS);
            VERIFY_EXPR(m_pProceduralBLAS != nullptr);
        }

        // Create scratch buffer
        RefCntAutoPtr<IBuffer> pScratchBuffer;
        {
            BufferDesc BuffDesc;
            BuffDesc.Name          = "BLAS Scratch Buffer";
            BuffDesc.Usage         = USAGE_DEFAULT;
            BuffDesc.BindFlags     = BIND_RAY_TRACING;
            BuffDesc.uiSizeInBytes = m_pProceduralBLAS->GetScratchBufferSizes().Build;

            m_pDevice->CreateBuffer(BuffDesc, nullptr, &pScratchBuffer);
            VERIFY_EXPR(pScratchBuffer != nullptr);
        }

        // Build BLAS
        BLASBuildBoundingBoxData BoxData;
        BoxData.GeometryName = BoxInfo.GeometryName;
        BoxData.BoxCount     = 1;
        BoxData.BoxStride    = sizeof(Boxes[0]);
        BoxData.pBoxBuffer   = m_BoxAttribsCB;

        BuildBLASAttribs Attribs;
        Attribs.pBLAS                       = m_pProceduralBLAS;
        Attribs.pBoxData                    = &BoxData;
        Attribs.BoxDataCount                = 1;
        Attribs.pScratchBuffer              = pScratchBuffer;
        Attribs.BLASTransitionMode          = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Attribs.GeometryTransitionMode      = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        Attribs.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

        m_pImmediateContext->BuildBLAS(Attribs);
    }
}

void RayTracing::UpdateTLAS()
{
    static constexpr int NumInstances = NumCubes + 3;

    bool NeedUpdate = true;

    // Create TLAS
    if (!m_pTLAS)
    {
        TopLevelASDesc TLASDesc;
        TLASDesc.Name             = "TLAS";
        TLASDesc.MaxInstanceCount = NumInstances;
        TLASDesc.Flags            = RAYTRACING_BUILD_AS_ALLOW_UPDATE | RAYTRACING_BUILD_AS_PREFER_FAST_TRACE;

        m_pDevice->CreateTLAS(TLASDesc, &m_pTLAS);
        VERIFY_EXPR(m_pTLAS != nullptr);

        NeedUpdate = false; // build on first run
    }

    // Create scratch buffer
    if (!m_ScratchBuffer)
    {
        BufferDesc BuffDesc;
        BuffDesc.Name          = "TLAS Scratch Buffer";
        BuffDesc.Usage         = USAGE_DEFAULT;
        BuffDesc.BindFlags     = BIND_RAY_TRACING;
        BuffDesc.uiSizeInBytes = std::max(m_pTLAS->GetScratchBufferSizes().Build, m_pTLAS->GetScratchBufferSizes().Update);

        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_ScratchBuffer);
        VERIFY_EXPR(m_ScratchBuffer != nullptr);
    }

    // Create instance buffer
    if (!m_InstanceBuffer)
    {
        BufferDesc BuffDesc;
        BuffDesc.Name          = "TLAS Instance Buffer";
        BuffDesc.Usage         = USAGE_DEFAULT;
        BuffDesc.BindFlags     = BIND_RAY_TRACING;
        BuffDesc.uiSizeInBytes = TLAS_INSTANCE_DATA_SIZE * NumInstances;

        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_InstanceBuffer);
        VERIFY_EXPR(m_InstanceBuffer != nullptr);
    }

    // Setup instances
    TLASBuildInstanceData Instances[NumInstances] = {};
    const float3          InstanceBasePos[]       = {float3{1, 1, 1}, float3{2, 0, -1}, float3{-1, 1, 2}, float3{-2, 0, -1}};
    const float           InstanceTimeOffset[]    = {0.0f, 0.53f, 1.27f, 4.16f};
    static_assert(_countof(InstanceBasePos) == NumCubes, "mismatch");
    static_assert(_countof(InstanceTimeOffset) == NumCubes, "mismatch");

    const auto AnimateTransform = [&](TLASBuildInstanceData& Dst) //
    {
        float  t     = sin(m_AnimationTime * PI_F * 0.5f) + InstanceTimeOffset[Dst.CustomId];
        float3 Pos   = InstanceBasePos[Dst.CustomId] * 2.0f + float3(sin(t * 1.13f), sin(t * 0.77f), sin(t * 2.15f)) * 0.5f;
        float  angle = 0.1f * PI_F * (m_AnimationTime + InstanceTimeOffset[Dst.CustomId] * 2.0f);

        if (!m_EnableCubes[Dst.CustomId])
            Dst.Mask = 0;

        Dst.Transform.SetTranslation(Pos.x, Pos.y, Pos.z);
        Dst.Transform.SetRotation(float3x3::RotationY(angle).Data());
    };

    Instances[0].InstanceName = "Cube Instance 1";
    Instances[0].CustomId     = 0; // texture index
    Instances[0].pBLAS        = m_pCubeBLAS;
    Instances[0].Mask         = OPAQUE_GEOM_MASK;
    AnimateTransform(Instances[0]);

    Instances[1].InstanceName = "Cube Instance 2";
    Instances[1].CustomId     = 1; // texture index
    Instances[1].pBLAS        = m_pCubeBLAS;
    Instances[1].Mask         = OPAQUE_GEOM_MASK;
    AnimateTransform(Instances[1]);

    Instances[2].InstanceName = "Cube Instance 3";
    Instances[2].CustomId     = 2; // texture index
    Instances[2].pBLAS        = m_pCubeBLAS;
    Instances[2].Mask         = OPAQUE_GEOM_MASK;
    AnimateTransform(Instances[2]);

    Instances[3].InstanceName = "Cube Instance 4";
    Instances[3].CustomId     = 3; // texture index
    Instances[3].pBLAS        = m_pCubeBLAS;
    Instances[3].Mask         = OPAQUE_GEOM_MASK;
    AnimateTransform(Instances[3]);

    Instances[4].InstanceName = "Ground Instance";
    Instances[4].pBLAS        = m_pCubeBLAS;
    Instances[4].Mask         = OPAQUE_GEOM_MASK;
    Instances[4].Transform.SetRotation(float3x3::Scale(100.0f, 0.1f, 100.0f).Data());
    Instances[4].Transform.SetTranslation(0.0f, 6.0f, 0.0f);

    Instances[5].InstanceName = "Sphere Instance";
    Instances[5].CustomId     = 0; // box index
    Instances[5].pBLAS        = m_pProceduralBLAS;
    Instances[5].Mask         = OPAQUE_GEOM_MASK;
    Instances[5].Transform.SetTranslation(-3.0f, 3.0f, -5.f);

    Instances[6].InstanceName = "Glass Instance";
    Instances[6].pBLAS        = m_pCubeBLAS;
    Instances[6].Mask         = TRANSPARENT_GEOM_MASK;
    Instances[6].Transform.SetTranslation(4.0f, 4.5f, -7.0f);

    // Build or update TLAS
    BuildTLASAttribs Attribs;
    Attribs.pTLAS                        = m_pTLAS;
    Attribs.Update                       = NeedUpdate;
    Attribs.pScratchBuffer               = m_ScratchBuffer;
    Attribs.pInstanceBuffer              = m_InstanceBuffer;
    Attribs.pInstances                   = Instances;
    Attribs.InstanceCount                = _countof(Instances);
    Attribs.BindingMode                  = HIT_GROUP_BINDING_MODE_PER_INSTANCE;
    Attribs.HitGroupStride               = HIT_GROUP_STRIDE;
    Attribs.TLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.BLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.InstanceBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.ScratchBufferTransitionMode  = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    m_pImmediateContext->BuildTLAS(Attribs);
}

void RayTracing::CreateSBT()
{
    try
    {
        m_pSBT = nullptr;

        if (m_pRayTracingPSO)
        {
            ShaderBindingTableDesc SBTDesc;
            SBTDesc.Name = "SBT";
            SBTDesc.pPSO = m_pRayTracingPSO;

            m_ShaderDebugger.CreateSBT(SBTDesc, &m_pSBT);
            CHECK_THROW(m_pSBT != nullptr);

            // clang-format off
            m_pSBT->BindRayGenShader("Main");

            m_pSBT->BindMissShader("PrimaryMiss", PRIMARY_RAY_INDEX);
            m_pSBT->BindMissShader("ShadowMiss",  SHADOW_RAY_INDEX );

            m_pSBT->BindHitGroups(m_pTLAS, "Cube Instance 1",  PRIMARY_RAY_INDEX, "CubePrimaryHit"  );
            m_pSBT->BindHitGroups(m_pTLAS, "Cube Instance 2",  PRIMARY_RAY_INDEX, "CubePrimaryHit"  );
            m_pSBT->BindHitGroups(m_pTLAS, "Cube Instance 3",  PRIMARY_RAY_INDEX, "CubePrimaryHit"  );
            m_pSBT->BindHitGroups(m_pTLAS, "Cube Instance 4",  PRIMARY_RAY_INDEX, "CubePrimaryHit"  );
            m_pSBT->BindHitGroups(m_pTLAS, "Ground Instance",  PRIMARY_RAY_INDEX, "GroundHit"       );
            m_pSBT->BindHitGroups(m_pTLAS, "Glass Instance",   PRIMARY_RAY_INDEX, "GlassPrimaryHit" );
            m_pSBT->BindHitGroups(m_pTLAS, "Sphere Instance",  PRIMARY_RAY_INDEX, "SpherePrimaryHit");

            m_pSBT->BindHitGroupForAll(m_pTLAS, SHADOW_RAY_INDEX, "");
            m_pSBT->BindHitGroups(m_pTLAS, "Sphere Instance",  SHADOW_RAY_INDEX,  "SphereShadowHit");
            // clang-format on
        }
    }
    catch (...)
    {
        m_pSBT = nullptr;
    }
}

void RayTracing::BindResources()
{
    if (m_pRayTracingSRB)
    {
        m_pRayTracingSRB->BindAllVariables(SHADER_TYPE_RAY_GEN, "un_ConstantsCB", m_ConstantsCB);
        m_pRayTracingSRB->BindAllVariables(SHADER_TYPE_RAY_CLOSEST_HIT, "un_ConstantsCB", m_ConstantsCB);

        m_pRayTracingSRB->BindAllVariables(SHADER_TYPE_RAY_GEN, "g_TLAS", m_pTLAS);
        m_pRayTracingSRB->BindAllVariables(SHADER_TYPE_RAY_CLOSEST_HIT, "g_TLAS", m_pTLAS);

        m_pRayTracingSRB->BindAllVariables(SHADER_TYPE_RAY_INTERSECTION, "un_BoxAttribs", m_BoxAttribsCB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        m_pRayTracingSRB->BindAllVariables(SHADER_TYPE_RAY_CLOSEST_HIT, "un_CubeAttribsCB", m_CubeAttribsCB);

        IDeviceObject* pTexSRVs[NumTextures] = {};
        for (Uint32 i = 0; i < NumTextures; ++i)
        {
            auto* pTextureSRV = m_Textures[i]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
            pTexSRVs[i]       = pTextureSRV;
        }

        m_pRayTracingSRB->BindAllVariables(SHADER_TYPE_RAY_CLOSEST_HIT, "g_Texture", pTexSRVs, 0, NumTextures);
        m_pRayTracingSRB->BindAllVariables(SHADER_TYPE_RAY_CLOSEST_HIT, "g_GroundTexture", m_pGroundTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    }
}

void RayTracing::Initialize(const SampleInitInfo& InitInfo)
{
    SampleBase::Initialize(InitInfo);

    m_ShaderDebugger.Initialize(m_pEngineFactory, m_pDevice);
    m_ShaderDebugger.InitDebugOutput(DEBUG_TRACE_PATH);

    CreateGraphicsPSO();
    CreateRayTracingPSO();
    LoadTextures();
    CreateCubeBLAS();
    CreateProceduralBLAS();
    UpdateTLAS();
    CreateSBT();

    // Create a buffer with shared constants.
    BufferDesc BuffDesc;
    BuffDesc.Name          = "Constant buffer";
    BuffDesc.uiSizeInBytes = sizeof(m_Constants);
    BuffDesc.Usage         = USAGE_DEFAULT;
    BuffDesc.BindFlags     = BIND_UNIFORM_BUFFER;

    m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_ConstantsCB);
    VERIFY_EXPR(m_ConstantsCB != nullptr);

    BindResources();

    // Setup camera.
    m_Camera.SetPos(float3(-7.f, -0.5f, 16.5f));
    m_Camera.SetRotation(-2.67f, -0.145f);
    m_Camera.SetRotationSpeed(0.005f);
    m_Camera.SetMoveSpeed(5.f);
    m_Camera.SetSpeedUpScales(5.f, 10.f);

    // Initialize constants.
    {
        m_Constants.ClipPlanes   = float2{0.1f, 100.0f};
        m_Constants.ShadowPCF    = 1;
        m_Constants.MaxRecursion = MaxRecursionDepth / 2;

        // Sphere constants.
        m_Constants.SphereReflectionColorMask = {0.81f, 1.0f, 0.45f};
        m_Constants.SphereReflectionBlur      = 1;

        // Glass cube constants.
        m_Constants.GlassReflectionColorMask = {0.25f, 0.02f, 0.50f};
        m_Constants.GlassOpticalDepth        = 0.5f;
        m_Constants.GlassMaterialColor       = {0.71f, 0.42f, 0.13f};
        m_Constants.GlassIndexOfRefraction   = {1.01f, 1.02f};
        m_Constants.GlassEnableInterference  = 0;

        // Wavelength to RGB and index of refraction interpolation factor.
        m_Constants.InterferenceSamples[0]  = {0.140000f, 0.000000f, 0.266667f, 0.53f};
        m_Constants.InterferenceSamples[1]  = {0.130031f, 0.037556f, 0.612267f, 0.25f};
        m_Constants.InterferenceSamples[2]  = {0.100123f, 0.213556f, 0.785067f, 0.16f};
        m_Constants.InterferenceSamples[3]  = {0.050277f, 0.533556f, 0.785067f, 0.00f};
        m_Constants.InterferenceSamples[4]  = {0.000000f, 0.843297f, 0.619682f, 0.13f};
        m_Constants.InterferenceSamples[5]  = {0.000000f, 0.927410f, 0.431834f, 0.38f};
        m_Constants.InterferenceSamples[6]  = {0.000000f, 0.972325f, 0.270893f, 0.27f};
        m_Constants.InterferenceSamples[7]  = {0.000000f, 0.978042f, 0.136858f, 0.19f};
        m_Constants.InterferenceSamples[8]  = {0.324000f, 0.944560f, 0.029730f, 0.47f};
        m_Constants.InterferenceSamples[9]  = {0.777600f, 0.871879f, 0.000000f, 0.64f};
        m_Constants.InterferenceSamples[10] = {0.972000f, 0.762222f, 0.000000f, 0.77f};
        m_Constants.InterferenceSamples[11] = {0.971835f, 0.482222f, 0.000000f, 0.62f};
        m_Constants.InterferenceSamples[12] = {0.886744f, 0.202222f, 0.000000f, 0.73f};
        m_Constants.InterferenceSamples[13] = {0.715967f, 0.000000f, 0.000000f, 0.68f};
        m_Constants.InterferenceSamples[14] = {0.459920f, 0.000000f, 0.000000f, 0.91f};
        m_Constants.InterferenceSamples[15] = {0.218000f, 0.000000f, 0.000000f, 0.99f};
        m_Constants.InterferenceSampleCount = 4;

        m_Constants.AmbientColor  = float4(1.f, 1.f, 1.f, 0.f) * 0.015f;
        m_Constants.LightPos[0]   = {8.00f, -8.0f, +0.00f, 0.f};
        m_Constants.LightColor[0] = {1.00f, +0.8f, +0.80f, 0.f};
        m_Constants.LightPos[1]   = {0.00f, -4.0f, -5.00f, 0.f};
        m_Constants.LightColor[1] = {0.85f, +1.0f, +0.85f, 0.f};

        // Random points on disc.
        m_Constants.DiscPoints[0] = {+0.0f, +0.0f, +0.9f, -0.9f};
        m_Constants.DiscPoints[1] = {-0.8f, +1.0f, -1.1f, -0.8f};
        m_Constants.DiscPoints[2] = {+1.5f, +1.2f, -2.1f, +0.7f};
        m_Constants.DiscPoints[3] = {+0.1f, -2.2f, -0.2f, +2.4f};
        m_Constants.DiscPoints[4] = {+2.4f, -0.3f, -3.0f, +2.8f};
        m_Constants.DiscPoints[5] = {+2.0f, -2.6f, +0.7f, +3.5f};
        m_Constants.DiscPoints[6] = {-3.2f, -1.6f, +3.4f, +2.2f};
        m_Constants.DiscPoints[7] = {-1.8f, -3.2f, -1.1f, +3.6f};
    }
    static_assert(sizeof(Constants) % 16 == 0, "must be aligned by 16 bytes");
}

void RayTracing::GetEngineInitializationAttribs(RENDER_DEVICE_TYPE DeviceType, EngineCreateInfo& EngineCI, SwapChainDesc& SCDesc)
{
    SampleBase::GetEngineInitializationAttribs(DeviceType, EngineCI, SCDesc);

    EngineCI.Features = DeviceFeatures{DEVICE_FEATURE_STATE_OPTIONAL};
}

// Render a frame
void RayTracing::Render()
{
    UpdateTLAS();

    // Update constants
    {
        float3 CameraWorldPos = float3::MakeVector(m_Camera.GetWorldMatrix()[3]);
        auto   CameraViewProj = m_Camera.GetViewMatrix() * m_Camera.GetProjMatrix();

        ViewFrustum Frustum;
        ExtractViewFrustumPlanesFromMatrix(CameraViewProj, Frustum, false);

        // Normalize frustum planes.
        for (uint i = 0; i < ViewFrustum::NUM_PLANES; ++i)
        {
            Plane3D& plane  = Frustum.GetPlane(static_cast<ViewFrustum::PLANE_IDX>(i));
            float    invlen = 1.0f / length(plane.Normal);
            plane.Normal *= invlen;
            plane.Distance *= invlen;
        }

        // Calculate ray formed by the intersection of two planes.
        const auto GetPlaneIntersection = [&Frustum](ViewFrustum::PLANE_IDX lhs, ViewFrustum::PLANE_IDX rhs, float4& result) {
            const Plane3D& lp = Frustum.GetPlane(lhs);
            const Plane3D& rp = Frustum.GetPlane(rhs);

            const float3 dir = cross(lp.Normal, rp.Normal);
            const float  len = dot(dir, dir);

            VERIFY_EXPR(len > 1.0e-5);

            result = dir * (1.0f / sqrt(len));
        };

        // clang-format off
        GetPlaneIntersection(ViewFrustum::BOTTOM_PLANE_IDX, ViewFrustum::LEFT_PLANE_IDX,   m_Constants.FrustumRayLB);
        GetPlaneIntersection(ViewFrustum::LEFT_PLANE_IDX,   ViewFrustum::TOP_PLANE_IDX,    m_Constants.FrustumRayLT);
        GetPlaneIntersection(ViewFrustum::RIGHT_PLANE_IDX,  ViewFrustum::BOTTOM_PLANE_IDX, m_Constants.FrustumRayRB);
        GetPlaneIntersection(ViewFrustum::TOP_PLANE_IDX,    ViewFrustum::RIGHT_PLANE_IDX,  m_Constants.FrustumRayRT);
        // clang-format on
        m_Constants.Position = -float4{CameraWorldPos, 1.0f};

        m_pImmediateContext->UpdateBuffer(m_ConstantsCB, 0, sizeof(m_Constants), &m_Constants, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    // Trace rays
    if (m_pRayTracingPSO && m_pSBT)
    {
        m_pRayTracingSRB->BindAllVariables(SHADER_TYPE_RAY_GEN, "g_ColorBuffer", m_pColorRT->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS));

        IPipelineState* pPSO = m_pRayTracingPSO;
        if (m_ClockHeatmap)
        {
            m_ShaderDebugger.BeginClockHeatmap(m_pImmediateContext, pPSO, SHADER_TYPE_RAY_GEN, uint2{m_pColorRT->GetDesc().Width, m_pColorRT->GetDesc().Height});
        }
        /*else if (m_DebugShader)
        {
            m_ShaderDebugger.BeginRayTraceDebugger(m_pImmediateContext, pPSO, SHADER_TYPE_RAY_GEN, uint3(m_DebugCoord.x, m_DebugCoord.y, 0));
        }
        else if (m_ProfileShader)
        {
            m_ShaderDebugger.BeginRayTraceProfiler(m_pImmediateContext, pPSO, SHADER_TYPE_RAY_GEN, uint3(m_DebugCoord.x, m_DebugCoord.y, 0));
        }*/

        m_pImmediateContext->SetPipelineState(pPSO);
        m_ShaderDebugger.BindSRB(m_pImmediateContext, pPSO, m_pRayTracingSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        TraceRaysAttribs Attribs;
        Attribs.DimensionX        = m_pColorRT->GetDesc().Width;
        Attribs.DimensionY        = m_pColorRT->GetDesc().Height;
        Attribs.SBTTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

        m_ShaderDebugger.GetSBT(pPSO, m_pSBT, Attribs.pSBT);

        m_pImmediateContext->TraceRays(Attribs);

        if (m_ClockHeatmap)
        {
            m_ShaderDebugger.EndClockHeatmap(m_pImmediateContext, m_pColorRT);
        }

        m_ShaderDebugger.EndTrace(m_pImmediateContext);
        m_ShaderDebugger.ReadTrace(m_pImmediateContext);
    }

    // Blit to swapchain image
    {
        m_pImageBlitSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_pColorRT->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

        auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
        m_pImmediateContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pImmediateContext->SetPipelineState(m_pImageBlitPSO);
        m_pImmediateContext->CommitShaderResources(m_pImageBlitSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawAttribs Attribs;
        Attribs.NumVertices = 4;
        m_pImmediateContext->Draw(Attribs);
    }
}

void RayTracing::Update(double CurrTime, double ElapsedTime)
{
    SampleBase::Update(CurrTime, ElapsedTime);
    UpdateUI();

    m_AnimationTime += static_cast<float>(std::min(m_MaxAnimationTimeDelta, ElapsedTime));

    m_Camera.Update(m_InputController, static_cast<float>(ElapsedTime));

    float3 oldPos = m_Camera.GetPos();
    if (oldPos.y < -5.7f)
    {
        oldPos.y = -5.7f;
        m_Camera.SetPos(oldPos);
    }

    if (GetInputController().GetKeyState(InputKeys::ControlDown) & INPUT_KEY_STATE_FLAG_KEY_WAS_DOWN)
    {
        m_pImmediateContext->WaitForIdle();
        LOG_INFO_MESSAGE("============== Reload shaders ==============\n");
        CreateRayTracingPSO();
        CreateSBT();
        BindResources();
    }

    if (GetInputController().GetKeyState(InputKeys::ShiftDown) & INPUT_KEY_STATE_FLAG_KEY_WAS_DOWN)
        m_ClockHeatmap = !m_ClockHeatmap;

    m_DebugShader = false;
    if (GetInputController().GetKeyState(InputKeys::ZoomIn) & INPUT_KEY_STATE_FLAG_KEY_WAS_DOWN)
        m_DebugShader = true;

    m_ProfileShader = false;
    if (GetInputController().GetKeyState(InputKeys::ZoomOut) & INPUT_KEY_STATE_FLAG_KEY_WAS_DOWN)
        m_ProfileShader = true;

    auto& mstate   = GetInputController().GetMouseState();
    m_DebugCoord.x = Uint32(mstate.PosX);
    m_DebugCoord.y = Uint32(mstate.PosY);

    GetInputController().ClearState();
}

void RayTracing::WindowResize(Uint32 Width, Uint32 Height)
{
    float AspectRatio = static_cast<float>(Width) / static_cast<float>(Height);
    m_Camera.SetProjAttribs(m_Constants.ClipPlanes.x, m_Constants.ClipPlanes.y, AspectRatio, PI_F / 4.f,
                            m_pSwapChain->GetDesc().PreTransform, m_pDevice->GetDeviceCaps().IsGLDevice());

    if (m_pColorRT != nullptr &&
        m_pColorRT->GetDesc().Width == Width &&
        m_pColorRT->GetDesc().Height == Height)
        return;

    if (Width == 0 || Height == 0)
        return;

    m_pColorRT = nullptr;

    TextureDesc RTDesc       = {};
    RTDesc.Name              = "Color buffer";
    RTDesc.Type              = RESOURCE_DIM_TEX_2D;
    RTDesc.Width             = Width;
    RTDesc.Height            = Height;
    RTDesc.BindFlags         = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    RTDesc.ClearValue.Format = m_ColorBufferFormat;
    RTDesc.Format            = m_ColorBufferFormat;

    m_pDevice->CreateTexture(RTDesc, nullptr, &m_pColorRT);
    VERIFY_EXPR(m_pColorRT != nullptr);
}

void RayTracing::UpdateUI()
{
    const float MaxIndexOfRefraction = 1.2f;

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Use WASD to move camera");
        ImGui::SliderInt("Shadow blur", &m_Constants.ShadowPCF, 0, 16);
        ImGui::SliderInt("Max recursion", &m_Constants.MaxRecursion, 0, MaxRecursionDepth);

        for (int i = 0; i < NumCubes; ++i)
        {
            ImGui::Checkbox(("Cube " + std::to_string(i)).c_str(), &m_EnableCubes[i]);
            if (i + 1 < NumCubes)
                ImGui::SameLine();
        }

        ImGui::Separator();
        ImGui::Text("Glass cube");
        ImGui::Checkbox("Interference", &m_Constants.GlassEnableInterference);
        if (m_Constants.GlassEnableInterference)
        {
            ImGui::SliderFloat("Index or refraction min", &m_Constants.GlassIndexOfRefraction.x, 1.0f, MaxIndexOfRefraction);

            m_Constants.GlassIndexOfRefraction.y = std::max(m_Constants.GlassIndexOfRefraction.x, m_Constants.GlassIndexOfRefraction.y);
            ImGui::SliderFloat("Index or refraction max", &m_Constants.GlassIndexOfRefraction.y, 1.0f, MaxIndexOfRefraction);

            int rsamples = PlatformMisc::GetLSB(m_Constants.InterferenceSampleCount);
            ImGui::SliderInt("Refraction samples", &rsamples, 1, PlatformMisc::GetLSB(Uint32{MAX_INTERF_SAMPLES}), std::to_string(1 << rsamples).c_str());
            m_Constants.InterferenceSampleCount = 1u << rsamples;
        }
        else
        {
            ImGui::SliderFloat("Index or refraction", &m_Constants.GlassIndexOfRefraction.x, 1.0f, MaxIndexOfRefraction);
            m_Constants.GlassIndexOfRefraction.y = m_Constants.GlassIndexOfRefraction.x + 0.02f;
        }
        ImGui::ColorEdit3("Reflection color", m_Constants.GlassReflectionColorMask.Data(), ImGuiColorEditFlags_NoAlpha);
        ImGui::ColorEdit3("Material color", m_Constants.GlassMaterialColor.Data(), ImGuiColorEditFlags_NoAlpha);
        ImGui::SliderFloat("Optical depth", &m_Constants.GlassOpticalDepth, 0.0f, 2.0f);

        ImGui::Separator();
        ImGui::Text("Sphere");
        ImGui::SliderInt("Reflection blur", &m_Constants.SphereReflectionBlur, 1, 16);
        ImGui::ColorEdit3("Color mask", m_Constants.SphereReflectionColorMask.Data(), ImGuiColorEditFlags_NoAlpha);
    }
    ImGui::End();
}

} // namespace Diligent
