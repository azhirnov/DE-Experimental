#include "RT_Scene.hpp"
#include "ShaderMacroHelper.hpp"
#include "DynamicLinearAllocator.hpp"
#include "FixedBlockMemoryAllocator.hpp"
#include "MapHelper.hpp"

#include "../include/VulkanUtilities/VulkanHeaders.h"
#include "EngineFactoryVk.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"

namespace Diligent
{
namespace
{
#include "../assets/structures.fxh"
static_assert(sizeof(CameraAttribs) % 16 == 0, "must be aligned by 16 bytes");
static_assert(sizeof(MaterialAttribs) % 16 == 0, "must be aligned by 16 bytes");
static_assert(sizeof(PrimitiveAttribs) % 16 == 0, "must be aligned by 16 bytes");
static_assert(sizeof(PrimitiveAttribs) == sizeof(GLTF::Model::TriangleAttribs), "size mismatch");
static_assert(sizeof(VertexAttribs) % 4 == 0, "must be aligned by 4 bytes");
static_assert(sizeof(BoxAttribs) % 16 == 0, "must be aligned by 16 bytes");


static constexpr SHADER_TYPE RayTracingStages =
    SHADER_TYPE_RAY_GEN | SHADER_TYPE_RAY_MISS | SHADER_TYPE_RAY_CLOSEST_HIT |
    SHADER_TYPE_RAY_ANY_HIT | SHADER_TYPE_RAY_INTERSECTION | SHADER_TYPE_CALLABLE;

void BindAllVariables(IShaderResourceBinding* pSRB, SHADER_TYPE Stages, const char* pName, IDeviceObject* pObject)
{
    VERIFY_EXPR(pObject != nullptr);

    for (Uint32 s = 1; s <= Stages; s <<= 1)
    {
        if (Stages & s)
        {
            if (auto* pVar = pSRB->GetVariableByName(SHADER_TYPE(s), pName))
                pVar->Set(pObject);
        }
    }
}

void BindAllVariables(IShaderResourceBinding* pSRB, SHADER_TYPE Stages, const char* pName, IDeviceObject* const* ppObjects, Uint32 FirstElement, Uint32 NumElements)
{
    for (Uint32 i = 0; i < NumElements; ++i)
    {
        VERIFY_EXPR(ppObjects[i] != nullptr);
    }

    for (Uint32 s = 1; s <= Stages; s <<= 1)
    {
        if (Stages & s)
        {
            if (auto* pVar = pSRB->GetVariableByName(SHADER_TYPE(s), pName))
                pVar->SetArray(ppObjects, FirstElement, NumElements);
        }
    }
}

} // namespace


RT_Scene::InputControllerGLFW::InputControllerGLFW()
{}

MouseState& RT_Scene::InputControllerGLFW::EditMouseState()
{
    return m_MouseState;
}

void RT_Scene::InputControllerGLFW::SetKeyState(InputKeys Key, INPUT_KEY_STATE_FLAGS Flags)
{
    m_Keys[Uint32(Key)] = Flags;
}


RT_Scene::RT_Scene()
{
}

RT_Scene::~RT_Scene()
{
    if (m_Window)
    {
        m_Window = nullptr;
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }
}

bool RT_Scene::Create(uint2 wndSize) noexcept
{
    // create window
    {
        if (glfwInit() != GLFW_TRUE)
        {
            LOG_ERROR("glfwInit - failed");
            return false;
        }

        if (glfwVulkanSupported() != GLFW_TRUE)
        {
            LOG_ERROR("Vulkan is not supported by GLFW");
            return false;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        m_Window = glfwCreateWindow(int(wndSize.x),
                                    int(wndSize.y),
                                    "RT Scene",
                                    nullptr,
                                    nullptr);
        if (m_Window == nullptr)
        {
            LOG_ERROR("Failed to create GLFW Window");
            return false;
        }

        glfwSetWindowUserPointer(m_Window, this);
        glfwSetWindowRefreshCallback(m_Window, &GLFW_RefreshCallback);
        glfwSetFramebufferSizeCallback(m_Window, &GLFW_ResizeCallback);
        glfwSetKeyCallback(m_Window, &GLFW_KeyCallback);
        glfwSetMouseButtonCallback(m_Window, &GLFW_MouseButtonCallback);
        glfwSetCursorPosCallback(m_Window, &GLFW_CursorPosCallback);
        glfwSetScrollCallback(m_Window, &GLFW_MouseWheelCallback);
    }

    // create device
    {
        EngineVkCreateInfo CreateInfo;
        CreateInfo.EnableValidation    = true;
        CreateInfo.NumDeferredContexts = 0;
        CreateInfo.Features.RayTracing = DEVICE_FEATURE_STATE_ENABLED;

        auto* Factory    = GetEngineFactoryVk();
        m_pEngineFactory = Factory;

        IRenderDevice*  Device  = nullptr;
        IDeviceContext* Context = nullptr;
        Factory->CreateDeviceAndContextsVk(CreateInfo, &Device, &Context);
        if (!Device)
            return false;

        m_pDevice  = Device;
        m_pContext = Context;

        SwapChainDesc     SCDesc;
        Win32NativeWindow Window{glfwGetWin32Window(m_Window)};
        Factory->CreateSwapChainVk(m_pDevice, m_pContext, SCDesc, Window, &m_pSwapChain);

        if (m_pSwapChain == nullptr)
        {
            LOG_ERROR("Failed to create swapchain for VR emulator");
            return false;
        }
    }

    m_Camera.SetPos(float3(27, 10, -2.f));
    m_Camera.SetRotation(PI_F / 2.f, 0);
    m_Camera.SetRotationSpeed(0.005f);
    m_Camera.SetMoveSpeed(5.f);
    m_Camera.SetSpeedUpScales(5.f, 10.f);

    OnResize(wndSize.x, wndSize.y);

    LoadScene("sponza/Sponza.gltf");
    CreateBLAS();
    //CreateProcBLAS();
    CreateTLAS();

    CreateRayTracingPSO();
    CreateToneMapPSO();
    CreateSBT();
    BindResources();

    m_pContext->Flush();

    m_LastUpdateTime = TimePoint::clock::now();
    return true;
}

void RT_Scene::CreateRayTracingPSO()
{
    try
    {
        RayTracingPipelineStateCreateInfo PSOCreateInfo;

        PSOCreateInfo.PSODesc.Name         = "Ray tracing PSO";
        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_RAY_TRACING;

        ShaderCreateInfo ShaderCI;

        // Create a shader source stream factory to load shaders from files.
        RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
        m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
        ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

        ShaderMacroHelper Macros;
        Macros.AddShaderMacro("HIT_SHADER_PER_INSTANCE", HitGroupStride);
        Macros.AddShaderMacro("PRIMARY_RAY_INDEX", PrimaryRayIndex);
        Macros.AddShaderMacro("SHADOW_RAY_INDEX", ShadowRayIndex);
        Macros.AddShaderMacro("NUM_TEXTURES", int(m_MaterialColorMaps.size()));
        Macros.AddShaderMacro("MAX_RECURSION_DEPTH", MaxRecursionDepth);

        ShaderCI.Macros         = Macros;
        ShaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;

        // Create ray generation shader.
        RefCntAutoPtr<IShader> pRG;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_GEN;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Ray tracing RG";
            ShaderCI.FilePath        = "RayTrace.rg";
            m_pDevice->CreateShader(ShaderCI, &pRG);
            CHECK_THROW(pRG != nullptr);
        }

        // Create ray miss shaders.
        RefCntAutoPtr<IShader> pPrimaryMiss;
        RefCntAutoPtr<IShader> pShadowMiss;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_MISS;
            ShaderCI.EntryPoint      = "main";

            ShaderCI.Desc.Name = "Primary miss shader";
            ShaderCI.FilePath  = "Primary.rm";
            m_pDevice->CreateShader(ShaderCI, &pPrimaryMiss);
            CHECK_THROW(pPrimaryMiss != nullptr);

            ShaderCI.Desc.Name = "Shadow miss shader";
            ShaderCI.FilePath  = "Shadow.rm";
            m_pDevice->CreateShader(ShaderCI, &pShadowMiss);
            CHECK_THROW(pShadowMiss != nullptr);
        }

        // Create ray closest hit shader.
        RefCntAutoPtr<IShader> pPrimaryOpaqueHit;
        RefCntAutoPtr<IShader> pShadowHit;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_RAY_CLOSEST_HIT;
            ShaderCI.EntryPoint      = "main";

            ShaderCI.Desc.Name = "Primary ray triangle opaque hit shader";
            ShaderCI.FilePath  = "PrimaryOpaqueHit.rch";
            m_pDevice->CreateShader(ShaderCI, &pPrimaryOpaqueHit);
            CHECK_THROW(pPrimaryOpaqueHit != nullptr);

            ShaderCI.Desc.Name = "Shadow ray triangle hit shader";
            ShaderCI.FilePath  = "ShadowHit.rch";
            m_pDevice->CreateShader(ShaderCI, &pShadowHit);
            CHECK_THROW(pShadowHit != nullptr);
        }

        const RayTracingGeneralShaderGroup GeneralShaders[] = {
            {"Main", pRG},
            {"PrimaryMiss", pPrimaryMiss},
            {"ShadowMiss", pShadowMiss} //
        };

        const RayTracingTriangleHitShaderGroup TriangleHitShaders[] = {
            {"PrimaryOpaqueHit", pPrimaryOpaqueHit},
            {"ShadowHit", pShadowHit} //
        };

        PSOCreateInfo.pGeneralShaders        = GeneralShaders;
        PSOCreateInfo.GeneralShaderCount     = _countof(GeneralShaders);
        PSOCreateInfo.pTriangleHitShaders    = TriangleHitShaders;
        PSOCreateInfo.TriangleHitShaderCount = _countof(TriangleHitShaders);

        PSOCreateInfo.RayTracingPipeline.MaxRecursionDepth = MaxRecursionDepth;

        PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

        m_pDevice->CreateRayTracingPipelineState(PSOCreateInfo, &m_pRayTracingPSO);
        CHECK_THROW(m_pRayTracingPSO != nullptr);

        m_pRayTracingPSO->CreateShaderResourceBinding(&m_pRayTracingSRB, true);
        CHECK_THROW(m_pRayTracingSRB != nullptr);
    }
    catch (...)
    {}
}

void RT_Scene::CreateToneMapPSO()
{
    try
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

        // Create a shader source stream factory to load shaders from files.
        RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
        m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);
        ShaderCI.pShaderSourceStreamFactory = pShaderSourceFactory;

        ShaderCI.UseCombinedTextureSamplers = true;
        ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;

        RefCntAutoPtr<IShader> pVS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Tone mapping vertex shader";
            ShaderCI.FilePath        = "ToneMapping.vsh";
            m_pDevice->CreateShader(ShaderCI, &pVS);
            CHECK_THROW(pVS != nullptr);
        }

        RefCntAutoPtr<IShader> pPS;
        {
            ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
            ShaderCI.EntryPoint      = "main";
            ShaderCI.Desc.Name       = "Tone mapping pixel shader";
            ShaderCI.FilePath        = "ToneMapping.psh";
            m_pDevice->CreateShader(ShaderCI, &pPS);
            CHECK_THROW(pPS != nullptr);
        }

        PSOCreateInfo.pVS = pVS;
        PSOCreateInfo.pPS = pPS;

        SamplerDesc SamLinearClampDesc{
            FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
            TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP};

        ImmutableSamplerDesc ImmutableSamplers[] = {
            {SHADER_TYPE_PIXEL, "g_ColorBuffer", SamLinearClampDesc},
            {SHADER_TYPE_PIXEL, "g_DepthBuffer", SamLinearClampDesc}};

        PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers    = ImmutableSamplers;
        PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImmutableSamplers);
        PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType  = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

        m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pToneMapPSO);
        CHECK_THROW(m_pToneMapPSO != nullptr);

        m_pToneMapPSO->CreateShaderResourceBinding(&m_pToneMapSRB, true);
        CHECK_THROW(m_pToneMapSRB != nullptr);
    }
    catch (...)
    {}
}

void RT_Scene::BindResources()
{
    using BufID = GLTF::Model::BUFFER_ID;

    if (m_pRayTracingSRB)
    {
        BindAllVariables(m_pRayTracingSRB, RayTracingStages, "g_TLAS", m_pTLAS);

        BindAllVariables(m_pRayTracingSRB, RayTracingStages, "un_CameraAttribs", m_CameraAttribsCB);
        BindAllVariables(m_pRayTracingSRB, RayTracingStages, "un_LightAttribs", m_LightAttribsCB);

        BindAllVariables(m_pRayTracingSRB, RayTracingStages, "un_VertexAttribs", m_Model->GetBuffer(BufID::BUFFER_ID_VERTEX_BASIC_ATTRIBS)->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        BindAllVariables(m_pRayTracingSRB, RayTracingStages, "un_Primitives", m_Model->GetBuffer(BufID::BUFFER_ID_TRIANGLES)->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        BindAllVariables(m_pRayTracingSRB, RayTracingStages, "un_PrimitiveOffsets", reinterpret_cast<IDeviceObject* const*>(m_PrimitiveOffsets.data()), 0, Uint32(m_PrimitiveOffsets.size()));

        BindAllVariables(m_pRayTracingSRB, RayTracingStages, "un_MaterialAttribs", m_MaterialAttribsSB->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE));
        BindAllVariables(m_pRayTracingSRB, RayTracingStages, "g_MaterialColorMaps", reinterpret_cast<IDeviceObject* const*>(m_MaterialColorMaps.data()), 0, Uint32(m_MaterialColorMaps.size()));
    }
}

void RT_Scene::ReloadShaders()
{
    m_pContext->WaitForIdle();

    LOG_INFO_MESSAGE("============================================================\n");

    RefCntAutoPtr<IPipelineState>         oldRayTracingPSO = m_pRayTracingPSO;
    RefCntAutoPtr<IPipelineState>         oldToneMapPSO    = m_pToneMapPSO;
    RefCntAutoPtr<IShaderResourceBinding> oldRayTracingSRB = m_pRayTracingSRB;
    RefCntAutoPtr<IShaderResourceBinding> oldToneMapSRB    = m_pToneMapSRB;

    m_pRayTracingPSO = nullptr;
    m_pRayTracingSRB = nullptr;
    m_pToneMapPSO    = nullptr;
    m_pToneMapSRB    = nullptr;
    m_pSBT           = nullptr;

    CreateRayTracingPSO();
    CreateToneMapPSO();

    if (m_pRayTracingPSO == nullptr)
    {
        m_pRayTracingPSO = oldRayTracingPSO;
        m_pRayTracingSRB = oldRayTracingSRB;
    }

    if (m_pToneMapPSO == nullptr)
    {
        m_pToneMapPSO = oldToneMapPSO;
        m_pToneMapSRB = oldToneMapSRB;
    }

    BindResources();
    CreateSBT();
}

void RT_Scene::CreateBLAS()
{
    struct PerInstance
    {
        std::vector<BLASTriangleDesc>      TriangleInfos;
        std::vector<BLASBuildTriangleData> TriangleData;
        std::vector<Uint32>                PrimitiveOffsets;
        std::vector<const char*>           GeometryNames;
    };
    PerInstance            Opaque;
    PerInstance            Translucent;
    DynamicLinearAllocator TempPool{GetRawAllocator()};

    Opaque.TriangleInfos.reserve(m_Model->LinearNodes.size());
    Opaque.TriangleData.reserve(m_Model->LinearNodes.size());

    IBuffer* TriangleBuffer = m_Model->GetBuffer(GLTF::Model::BUFFER_ID_TRIANGLES);
    if (TriangleBuffer == nullptr)
        return;

    const Uint32 TriangleBufferSize = TriangleBuffer->GetDesc().uiSizeInBytes;

    for (auto* node : m_Model->LinearNodes)
    {
        if (node->pMesh)
        {
            Uint32 i = 0;
            for (auto& submesh : node->pMesh->Primitives)
            {
                auto&                  mat     = m_Model->Materials[submesh.MaterialId];
                BLASTriangleDesc*      Info    = nullptr;
                BLASBuildTriangleData* TriData = nullptr;
                const char*            Name    = TempPool.CopyString(node->Name + "_" + std::to_string(i++));

                VERIFY_EXPR((submesh.FirstTriangle + submesh.IndexCount / 3) * sizeof(PrimitiveAttribs) <= TriangleBufferSize);

                if (mat.AlphaMode == GLTF::Material::ALPHA_MODE_OPAQUE)
                {
                    Opaque.TriangleInfos.emplace_back();
                    Opaque.TriangleData.emplace_back();
                    Opaque.PrimitiveOffsets.push_back(submesh.FirstTriangle);
                    Opaque.GeometryNames.push_back(Name);
                    Info    = &Opaque.TriangleInfos.back();
                    TriData = &Opaque.TriangleData.back();
                }
                else
                {
                    Translucent.TriangleInfos.emplace_back();
                    Translucent.TriangleData.emplace_back();
                    Translucent.PrimitiveOffsets.push_back(submesh.FirstTriangle);
                    Translucent.GeometryNames.push_back(Name);
                    Info    = &Translucent.TriangleInfos.back();
                    TriData = &Translucent.TriangleData.back();
                }

                Info->GeometryName         = Name;
                Info->MaxVertexCount       = submesh.VertexCount;
                Info->VertexValueType      = VT_FLOAT32;
                Info->VertexComponentCount = 3;
                Info->MaxPrimitiveCount    = submesh.IndexCount / 3;
                Info->IndexType            = submesh.HasIndices() ? VT_UINT32 : VT_UNDEFINED;

                TriData->GeometryName         = Info->GeometryName;
                TriData->pVertexBuffer        = m_Model->GetBuffer(GLTF::Model::BUFFER_ID_VERTEX_BASIC_ATTRIBS);
                TriData->VertexStride         = sizeof(GLTF::Model::VertexBasicAttribs);
                TriData->VertexCount          = Info->MaxVertexCount;
                TriData->VertexValueType      = Info->VertexValueType;
                TriData->VertexComponentCount = Info->VertexComponentCount;
                TriData->pIndexBuffer         = submesh.HasIndices() ? m_Model->GetBuffer(GLTF::Model::BUFFER_ID_INDEX) : nullptr;
                TriData->PrimitiveCount       = Info->MaxPrimitiveCount;
                TriData->IndexOffset          = submesh.FirstIndex * sizeof(Uint32);
                TriData->IndexType            = Info->IndexType;
                TriData->Flags                = RAYTRACING_GEOMETRY_FLAG_NONE;
            }
        }
    }

    // create AS
    BottomLevelASDesc ASDesc;

    if (Opaque.TriangleInfos.size())
    {
        ASDesc.Name          = "Opaque BLAS";
        ASDesc.pTriangles    = Opaque.TriangleInfos.data();
        ASDesc.TriangleCount = Uint32(Opaque.TriangleInfos.size());
        m_pDevice->CreateBLAS(ASDesc, &m_pOpaqueBLAS);
        VERIFY_EXPR(m_pOpaqueBLAS != nullptr);
    }

    if (Translucent.TriangleInfos.size())
    {
        ASDesc.Name          = "Translucent BLAS";
        ASDesc.pTriangles    = Translucent.TriangleInfos.data();
        ASDesc.TriangleCount = Uint32(Translucent.TriangleInfos.size());
        m_pDevice->CreateBLAS(ASDesc, &m_pTranslucentBLAS);
        VERIFY_EXPR(m_pTranslucentBLAS != nullptr);
    }

    // create scratch buffer
    RefCntAutoPtr<IBuffer> ScratchBuffer;
    BufferDesc             BuffDesc;
    BuffDesc.Name          = "BLAS Scratch Buffer";
    BuffDesc.Usage         = USAGE_DEFAULT;
    BuffDesc.BindFlags     = BIND_RAY_TRACING;
    BuffDesc.uiSizeInBytes = max(m_pOpaqueBLAS ? m_pOpaqueBLAS->GetScratchBufferSizes().Build : 0,
                                 m_pTranslucentBLAS ? m_pTranslucentBLAS->GetScratchBufferSizes().Build : 0);
    m_pDevice->CreateBuffer(BuffDesc, nullptr, &ScratchBuffer);
    VERIFY_EXPR(ScratchBuffer != nullptr);

    // build AS
    BuildBLASAttribs Attribs;
    Attribs.BLASTransitionMode          = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.GeometryTransitionMode      = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.pScratchBuffer              = ScratchBuffer;
    Attribs.ScratchBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

    if (m_pOpaqueBLAS)
    {
        Attribs.pBLAS             = m_pOpaqueBLAS;
        Attribs.pTriangleData     = Opaque.TriangleData.data();
        Attribs.TriangleDataCount = Uint32(Opaque.TriangleData.size());
        m_pContext->BuildBLAS(Attribs);

        BuffDesc.Name          = "opaque primitives offsets";
        BuffDesc.BindFlags     = BIND_SHADER_RESOURCE;
        BuffDesc.uiSizeInBytes = Uint32(Opaque.PrimitiveOffsets.size() * sizeof(Opaque.PrimitiveOffsets[0]));
        BuffDesc.Mode          = BUFFER_MODE_RAW;

        BufferData             BuffData{Opaque.PrimitiveOffsets.data(), BuffDesc.uiSizeInBytes};
        RefCntAutoPtr<IBuffer> Buff;
        m_pDevice->CreateBuffer(BuffDesc, &BuffData, &Buff);
        VERIFY_EXPR(Buff != nullptr);
        m_PrimitiveOffsets[0] = Buff->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
    }

    if (m_pTranslucentBLAS)
    {
        Attribs.pBLAS             = m_pTranslucentBLAS;
        Attribs.pTriangleData     = Translucent.TriangleData.data();
        Attribs.TriangleDataCount = Uint32(Translucent.TriangleData.size());
        m_pContext->BuildBLAS(Attribs);

        BuffDesc.Name          = "translucent primitives offsets";
        BuffDesc.BindFlags     = BIND_SHADER_RESOURCE;
        BuffDesc.uiSizeInBytes = Uint32(Translucent.PrimitiveOffsets.size() * sizeof(Translucent.PrimitiveOffsets[0]));
        BuffDesc.Mode          = BUFFER_MODE_RAW;

        BufferData             BuffData{Translucent.PrimitiveOffsets.data(), BuffDesc.uiSizeInBytes};
        RefCntAutoPtr<IBuffer> Buff;
        m_pDevice->CreateBuffer(BuffDesc, &BuffData, &Buff);
        VERIFY_EXPR(Buff != nullptr);
        m_PrimitiveOffsets[1] = Buff->GetDefaultView(BUFFER_VIEW_SHADER_RESOURCE);
    }
}

void RT_Scene::CreateTLAS()
{
    std::vector<TLASBuildInstanceData> Instances;

    const auto     ScaleMat = float4x4::RotationX(PI_F) * float4x4::Scale(0.05f);
    InstanceMatrix Transform;

    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 4; ++r)
            Transform.data[c][r] = ScaleMat.m[c][r];

    if (m_pOpaqueBLAS)
    {
        Instances.emplace_back();
        TLASBuildInstanceData& Inst = Instances.back();

        Inst.InstanceName                = "Opaque";
        Inst.pBLAS                       = m_pOpaqueBLAS;
        Inst.CustomId                    = 0;
        Inst.Flags                       = RAYTRACING_INSTANCE_FORCE_OPAQUE;
        Inst.Mask                        = 0xFF;
        Inst.ContributionToHitGroupIndex = TLAS_INSTANCE_OFFSET_AUTO;
        Inst.Transform                   = Transform;
    }

    if (m_pTranslucentBLAS)
    {
        Instances.emplace_back();
        TLASBuildInstanceData& Inst = Instances.back();

        Inst.InstanceName                = "Translucent";
        Inst.pBLAS                       = m_pTranslucentBLAS;
        Inst.CustomId                    = 0;
        Inst.Flags                       = RAYTRACING_INSTANCE_FORCE_NO_OPAQUE;
        Inst.Mask                        = 0xFF;
        Inst.ContributionToHitGroupIndex = TLAS_INSTANCE_OFFSET_AUTO;
        Inst.Transform                   = Transform;
    }

    // create
    TopLevelASDesc TLASDesc;
    TLASDesc.Name             = "TLAS";
    TLASDesc.MaxInstanceCount = Uint32(Instances.size());
    TLASDesc.Flags            = RAYTRACING_BUILD_AS_NONE;
    m_pDevice->CreateTLAS(TLASDesc, &m_pTLAS);
    VERIFY_EXPR(m_pTLAS != nullptr);

    // create scratch buffer
    RefCntAutoPtr<IBuffer> ScratchBuffer;
    BufferDesc             BuffDesc;
    BuffDesc.Name          = "TLAS Scratch Buffer";
    BuffDesc.Usage         = USAGE_DEFAULT;
    BuffDesc.BindFlags     = BIND_RAY_TRACING;
    BuffDesc.uiSizeInBytes = m_pTLAS->GetScratchBufferSizes().Build;
    m_pDevice->CreateBuffer(BuffDesc, nullptr, &ScratchBuffer);
    VERIFY_EXPR(ScratchBuffer != nullptr);

    // create instance buffer
    RefCntAutoPtr<IBuffer> InstanceBuffer;
    BuffDesc.Name          = "TLAS Instance Buffer";
    BuffDesc.Usage         = USAGE_DEFAULT;
    BuffDesc.BindFlags     = BIND_RAY_TRACING;
    BuffDesc.uiSizeInBytes = TLAS_INSTANCE_DATA_SIZE * Uint32(Instances.size());
    m_pDevice->CreateBuffer(BuffDesc, nullptr, &InstanceBuffer);
    VERIFY_EXPR(InstanceBuffer != nullptr);

    // build
    BuildTLASAttribs Attribs;
    Attribs.pTLAS                        = m_pTLAS;
    Attribs.pInstances                   = Instances.data();
    Attribs.InstanceCount                = Uint32(Instances.size());
    Attribs.HitGroupStride               = HitGroupStride;
    Attribs.BindingMode                  = HIT_GROUP_BINDING_MODE_PER_INSTANCE;
    Attribs.TLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.BLASTransitionMode           = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.pInstanceBuffer              = InstanceBuffer;
    Attribs.InstanceBufferTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    Attribs.pScratchBuffer               = ScratchBuffer;
    Attribs.ScratchBufferTransitionMode  = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    m_pContext->BuildTLAS(Attribs);
}

void RT_Scene::CreateSBT()
{
    ShaderBindingTableDesc SBTDesc;
    SBTDesc.Name = "SBT";
    SBTDesc.pPSO = m_pRayTracingPSO;
    m_pDevice->CreateSBT(SBTDesc, &m_pSBT);

    if (m_pSBT != nullptr)
    {
        // clang-format off
        m_pSBT->BindRayGenShader("Main");
        m_pSBT->BindMissShader("PrimaryMiss", PrimaryRayIndex);
        m_pSBT->BindMissShader("ShadowMiss",  ShadowRayIndex);
    
        if (m_pOpaqueBLAS)
        {
            m_pSBT->BindHitGroups(m_pTLAS, "Opaque",      PrimaryRayIndex, "PrimaryOpaqueHit");
            m_pSBT->BindHitGroups(m_pTLAS, "Opaque",      ShadowRayIndex,  "ShadowHit");
        }
        if (m_pTranslucentBLAS)
        {
            m_pSBT->BindHitGroups(m_pTLAS, "Translucent", PrimaryRayIndex, "PrimaryOpaqueHit");
            m_pSBT->BindHitGroups(m_pTLAS, "Translucent", ShadowRayIndex,  "ShadowHit");
        }
        // clang-format on
    }
}

void RT_Scene::LoadScene(const char* Path)
{
    // create model
    {
        GLTF::Model::CreateInfo ModelCI;
        ModelCI.FileName = Path;

        m_Model.reset(new GLTF::Model(m_pDevice, m_pContext, ModelCI));
    }

    // create default resources
    {
        static constexpr Uint32  TexDim = 8;
        static const SamplerDesc Sam_LinearClamp{
            FILTER_TYPE_LINEAR,
            FILTER_TYPE_LINEAR,
            FILTER_TYPE_LINEAR,
            TEXTURE_ADDRESS_CLAMP,
            TEXTURE_ADDRESS_CLAMP,
            TEXTURE_ADDRESS_CLAMP};

        TextureDesc TexDesc;
        TexDesc.Name      = "White texture for RT Scene";
        TexDesc.Type      = RESOURCE_DIM_TEX_2D;
        TexDesc.Usage     = USAGE_IMMUTABLE;
        TexDesc.BindFlags = BIND_SHADER_RESOURCE;
        TexDesc.Width     = TexDim;
        TexDesc.Height    = TexDim;
        TexDesc.Format    = TEX_FORMAT_RGBA8_UNORM;
        TexDesc.MipLevels = 1;
        std::vector<Uint32>     Data(TexDim * TexDim, 0xFFFFFFFF);
        TextureSubResData       Level0Data{Data.data(), TexDim * 4};
        TextureData             InitData{&Level0Data, 1};
        RefCntAutoPtr<ITexture> pWhiteTex;
        m_pDevice->CreateTexture(TexDesc, &InitData, &pWhiteTex);
        m_pWhiteTexSRV = pWhiteTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Black texture for RT Scene";
        for (auto& c : Data) c = 0;
        RefCntAutoPtr<ITexture> pBlackTex;
        m_pDevice->CreateTexture(TexDesc, &InitData, &pBlackTex);
        m_pBlackTexSRV = pBlackTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        TexDesc.Name = "Default normal map for RT Scene";
        for (auto& c : Data) c = 0x00FF7F7F;
        RefCntAutoPtr<ITexture> pDefaultNormalMap;
        m_pDevice->CreateTexture(TexDesc, &InitData, &pDefaultNormalMap);
        m_pDefaultNormalMapSRV = pDefaultNormalMap->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

        StateTransitionDesc Barriers[] = {
            {pWhiteTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true},
            {pBlackTex, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true},
            {pDefaultNormalMap, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_SHADER_RESOURCE, true}};

        m_pContext->TransitionResourceStates(_countof(Barriers), Barriers);

        RefCntAutoPtr<ISampler> pDefaultSampler;
        m_pDevice->CreateSampler(Sam_LinearClamp, &pDefaultSampler);
        m_pWhiteTexSRV->SetSampler(pDefaultSampler);
        m_pBlackTexSRV->SetSampler(pDefaultSampler);
        m_pDefaultNormalMapSRV->SetSampler(pDefaultSampler);
    }

    // create materials
    std::vector<MaterialAttribs> Materials;
    Materials.reserve(m_Model->Materials.size());
    m_MaterialColorMaps.reserve(m_Model->Materials.size());

    for (auto& mat : m_Model->Materials)
    {
        int       baseColorTexId = mat.TextureIds[GLTF::Material::TEXTURE_ID_BASE_COLOR];
        ITexture* pBaseColorTex  = baseColorTexId < 0 ? nullptr : m_Model->GetTexture(baseColorTexId);
        //int physDescTexId = mat.TextureIds[GLTF::Material::TEXTURE_ID_PHYSICAL_DESC];
        //ITexture* pPhysDescTex  = physDescTexId < 0 ? nullptr : m_Model->GetTexture(physDescTexId);

        m_MaterialColorMaps.emplace_back(pBaseColorTex ? pBaseColorTex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : m_pWhiteTexSRV);

        MaterialAttribs mtrAttribs;
        //mtrAttribs.DiffuseFactor   = mat.extension.DiffuseFactor;
        //mtrAttribs.SpecularFactor  = mat.extension.SpecularFactor;
        mtrAttribs.BaseColorFactor = mat.Attribs.BaseColorFactor;
        mtrAttribs.EmissiveFactor  = mat.Attribs.EmissiveFactor;
        mtrAttribs.MetallicFactor  = mat.Attribs.MetallicFactor;
        mtrAttribs.RoughnessFactor = mat.Attribs.RoughnessFactor;

        Materials.push_back(mtrAttribs);
    }

    // create buffers
    {
        BufferDesc BuffDesc;
        BuffDesc.Name           = "Camera attribs buffer";
        BuffDesc.uiSizeInBytes  = sizeof(CameraAttribs);
        BuffDesc.Usage          = USAGE_DYNAMIC;
        BuffDesc.BindFlags      = BIND_UNIFORM_BUFFER;
        BuffDesc.CPUAccessFlags = CPU_ACCESS_WRITE;

        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_CameraAttribsCB);
        VERIFY_EXPR(m_CameraAttribsCB != nullptr);

        BuffDesc.Name          = "Light attribs buffer";
        BuffDesc.uiSizeInBytes = sizeof(LightAttribs);
        m_pDevice->CreateBuffer(BuffDesc, nullptr, &m_LightAttribsCB);
        VERIFY_EXPR(m_LightAttribsCB != nullptr);

        BuffDesc.Name           = "Material attribs buffer";
        BuffDesc.uiSizeInBytes  = Uint32(Materials.size() * sizeof(Materials[0]));
        BuffDesc.Usage          = USAGE_DEFAULT;
        BuffDesc.BindFlags      = BIND_SHADER_RESOURCE;
        BuffDesc.Mode           = BUFFER_MODE_RAW;
        BuffDesc.CPUAccessFlags = CPU_ACCESS_NONE;

        BufferData BuffData;
        BuffData.pData    = Materials.data();
        BuffData.DataSize = BuffDesc.uiSizeInBytes;

        m_pDevice->CreateBuffer(BuffDesc, &BuffData, &m_MaterialAttribsSB);
        VERIFY_EXPR(m_MaterialAttribsSB != nullptr);
    }
}

bool RT_Scene::Update() noexcept
{
    if (m_Window == nullptr)
        return false;

    if (glfwWindowShouldClose(m_Window))
        return false;

    if (m_pSwapChain == nullptr)
        return false;

    glfwPollEvents();

    if (m_ColorUAV == nullptr)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        return true;
    }

    auto time        = TimePoint::clock::now();
    auto dt          = std::chrono::duration_cast<Seconds>(time - m_LastUpdateTime).count();
    m_LastUpdateTime = time;

    m_Camera.Update(m_InputController, dt);
    m_InputController.ClearState();

    Render();
    return true;
}

void RT_Scene::Render()
{
    // update constants
    {
        float3 CameraWorldPos = float3::MakeVector(m_Camera.GetWorldMatrix()[3]);
        auto   CameraViewProj = m_Camera.GetViewMatrix() * m_Camera.GetProjMatrix();

        ViewFrustum Frustum;
        ExtractViewFrustumPlanesFromMatrix(CameraViewProj, Frustum, false);

        for (uint i = 0; i < ViewFrustum::NUM_PLANES; ++i)
        {
            Plane3D& plane  = Frustum.GetPlane(static_cast<ViewFrustum::PLANE_IDX>(i));
            float    invlen = 1.0f / length(plane.Normal);
            plane.Normal *= invlen;
            plane.Distance *= invlen;
        }

        const auto GetPlaneIntersection = [&Frustum](ViewFrustum::PLANE_IDX lhs, ViewFrustum::PLANE_IDX rhs, float4& result) {
            const Plane3D& lp = Frustum.GetPlane(lhs);
            const Plane3D& rp = Frustum.GetPlane(rhs);

            const float3 dir = cross(lp.Normal, rp.Normal);
            const float  len = dot(dir, dir);

            if (std::abs(len) <= 1.0e-5)
                return false;

            result = dir * (1.0f / sqrt(len));
            return true;
        };

        MapHelper<CameraAttribs> CamAttribs{m_pContext, m_CameraAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};
        MapHelper<LightAttribs>  LightAttribs{m_pContext, m_LightAttribsCB, MAP_WRITE, MAP_FLAG_DISCARD};

        // clang-format off
        GetPlaneIntersection(ViewFrustum::BOTTOM_PLANE_IDX, ViewFrustum::LEFT_PLANE_IDX,   CamAttribs->FrustumRayLB);
        GetPlaneIntersection(ViewFrustum::LEFT_PLANE_IDX,   ViewFrustum::TOP_PLANE_IDX,    CamAttribs->FrustumRayLT);
        GetPlaneIntersection(ViewFrustum::RIGHT_PLANE_IDX,  ViewFrustum::BOTTOM_PLANE_IDX, CamAttribs->FrustumRayRB);
        GetPlaneIntersection(ViewFrustum::TOP_PLANE_IDX,    ViewFrustum::RIGHT_PLANE_IDX,  CamAttribs->FrustumRayRT);
        // clang-format on
        CamAttribs->Position   = -float4{CameraWorldPos, 1.0f};
        CamAttribs->ClipPlanes = float2{0.1f, 1000.0f};

        LightAttribs->OmniLightCount.x = 1;
        {
            auto& light       = LightAttribs->OmniLights[0];
            light.Position    = float4(m_Camera.GetPos().x, m_Camera.GetPos().y, m_Camera.GetPos().z, 0.0f);
            light.Color       = float4(1.0f, 1.0f, 0.0f, 1.0f);
            light.Attenuation = float4(0.0f, 0.3f, 0.0f, 0.0f);
        }
    }

    // trace rays
    if (m_pRayTracingSRB && m_pRayTracingPSO && m_pSBT && m_ColorUAV)
    {
        m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_GEN, "g_ColorBuffer")->Set(m_ColorUAV);
        m_pRayTracingSRB->GetVariableByName(SHADER_TYPE_RAY_GEN, "g_DepthBuffer")->Set(m_DepthUAV);

        m_pContext->SetPipelineState(m_pRayTracingPSO);
        m_pContext->CommitShaderResources(m_pRayTracingSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        TraceRaysAttribs Attribs;
        Attribs.DimensionX        = m_ColorUAV->GetTexture()->GetDesc().Width;
        Attribs.DimensionY        = m_ColorUAV->GetTexture()->GetDesc().Height;
        Attribs.pSBT              = m_pSBT;
        Attribs.SBTTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;

        m_pContext->TraceRays(Attribs);
    }

    // blit to swapchain image
    if (m_pToneMapSRB && m_pToneMapPSO && m_ColorUAV)
    {
        m_pToneMapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_ColorBuffer")->Set(m_ColorSRV);
        m_pToneMapSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_DepthBuffer")->Set(m_DepthSRV);

        auto* pRTV = m_pSwapChain->GetCurrentBackBufferRTV();
        m_pContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pContext->SetPipelineState(m_pToneMapPSO);
        m_pContext->CommitShaderResources(m_pToneMapSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawAttribs Attribs;
        Attribs.NumVertices = 4;
        m_pContext->Draw(Attribs);
    }

    m_pContext->Flush();
    m_pContext->FinishFrame();
    m_pSwapChain->Present();
    m_pDevice->ReleaseStaleResources();
}

void RT_Scene::GLFW_ErrorCallback(int code, const char* msg)
{
}

void RT_Scene::GLFW_RefreshCallback(GLFWwindow* wnd)
{
}

void RT_Scene::GLFW_ResizeCallback(GLFWwindow* wnd, int w, int h)
{
    auto* self = static_cast<RT_Scene*>(glfwGetWindowUserPointer(wnd));
    self->OnResize(w, h);
}

void RT_Scene::OnResize(Uint32 w, Uint32 h)
{
    if (m_pSwapChain)
        m_pSwapChain->Resize(w, h);

    float NearPlane   = 0.1f;
    float FarPlane    = 250.f;
    float AspectRatio = static_cast<float>(w) / static_cast<float>(h);
    m_Camera.SetProjAttribs(NearPlane, FarPlane, AspectRatio, PI_F / 4.f, SURFACE_TRANSFORM_IDENTITY, false);

    if (m_ColorUAV != nullptr &&
        m_ColorUAV->GetTexture()->GetDesc().Width == w &&
        m_ColorUAV->GetTexture()->GetDesc().Height == h)
        return;

    m_ColorUAV = nullptr;
    m_ColorSRV = nullptr;
    m_DepthUAV = nullptr;
    m_DepthSRV = nullptr;

    if (w == 0 || h == 0)
        return;

    // Create window-size render targets
    RefCntAutoPtr<ITexture> pColorRT;
    RefCntAutoPtr<ITexture> pDepthRT;
    TextureDesc             RTDesc;

    RTDesc.Type      = RESOURCE_DIM_TEX_2D;
    RTDesc.Width     = w;
    RTDesc.Height    = h;
    RTDesc.BindFlags = BIND_UNORDERED_ACCESS | BIND_SHADER_RESOURCE;

    // Define optimal clear value
    RTDesc.ClearValue.Format   = m_ColorBufferFormat;
    RTDesc.ClearValue.Color[0] = 0.0f;
    RTDesc.ClearValue.Color[1] = 0.0f;
    RTDesc.ClearValue.Color[2] = 0.0f;
    RTDesc.ClearValue.Color[3] = 0.0f;

    RTDesc.Format            = m_ColorBufferFormat;
    RTDesc.ClearValue.Format = m_ColorBufferFormat;
    RTDesc.Name              = "Color buffer";
    m_pDevice->CreateTexture(RTDesc, nullptr, &pColorRT);
    if (pColorRT)
    {
        m_ColorUAV = pColorRT->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS);
        m_ColorSRV = pColorRT->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }

    RTDesc.Format            = m_DepthBufferFormat;
    RTDesc.ClearValue.Format = m_DepthBufferFormat;
    RTDesc.Name              = "Depth buffer";
    m_pDevice->CreateTexture(RTDesc, nullptr, &pDepthRT);
    if (pDepthRT)
    {
        m_DepthUAV = pDepthRT->GetDefaultView(TEXTURE_VIEW_UNORDERED_ACCESS);
        m_DepthSRV = pDepthRT->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }
}

void RT_Scene::GLFW_KeyCallback(GLFWwindow* wnd, int key, int, int action, int)
{
    auto*                 self  = static_cast<RT_Scene*>(glfwGetWindowUserPointer(wnd));
    INPUT_KEY_STATE_FLAGS Flags = INPUT_KEY_STATE_FLAG_KEY_NONE;

    switch (action)
    {
        // clang-format off
        case GLFW_RELEASE: Flags = INPUT_KEY_STATE_FLAG_KEY_WAS_DOWN; break;
        case GLFW_PRESS:   Flags = INPUT_KEY_STATE_FLAG_KEY_IS_DOWN;  break;
        case GLFW_REPEAT:  Flags = INPUT_KEY_STATE_FLAG_KEY_IS_DOWN;  break;
            // clang-format on
    }

    switch (key)
    {
        // clang-format off
        case GLFW_KEY_W: self->m_InputController.SetKeyState(InputKeys::MoveForward,  Flags); break;
        case GLFW_KEY_A: self->m_InputController.SetKeyState(InputKeys::MoveLeft,     Flags); break;
        case GLFW_KEY_S: self->m_InputController.SetKeyState(InputKeys::MoveBackward, Flags); break;
        case GLFW_KEY_D: self->m_InputController.SetKeyState(InputKeys::MoveRight,    Flags); break;
        case GLFW_KEY_F: self->m_InputController.SetKeyState(InputKeys::MoveUp,       Flags); break;
        case GLFW_KEY_C: self->m_InputController.SetKeyState(InputKeys::MoveDown,     Flags); break;

        case GLFW_KEY_R: if (action == GLFW_RELEASE) self->ReloadShaders(); break;
            // clang-format on
    }
}

void RT_Scene::GLFW_MouseButtonCallback(GLFWwindow* wnd, int button, int action, int)
{
    auto* self  = static_cast<RT_Scene*>(glfwGetWindowUserPointer(wnd));
    auto& state = self->m_InputController.EditMouseState();

    const auto SetButtonFlags = [action, &state](MouseState::BUTTON_FLAGS Flag) {
        if (action == GLFW_RELEASE)
            state.ButtonFlags &= ~Flag;
        else
            state.ButtonFlags |= Flag;
    };

    switch (button)
    {
        // clang-format off
        case GLFW_MOUSE_BUTTON_LEFT:   SetButtonFlags(MouseState::BUTTON_FLAG_LEFT);   break;
        case GLFW_MOUSE_BUTTON_RIGHT:  SetButtonFlags(MouseState::BUTTON_FLAG_RIGHT);  break;
        case GLFW_MOUSE_BUTTON_MIDDLE: SetButtonFlags(MouseState::BUTTON_FLAG_MIDDLE); break;
            // clang-format on
    }
}

void RT_Scene::GLFW_CursorPosCallback(GLFWwindow* wnd, double xpos, double ypos)
{
    auto* self  = static_cast<RT_Scene*>(glfwGetWindowUserPointer(wnd));
    auto& state = self->m_InputController.EditMouseState();

    state.PosX = float(xpos);
    state.PosY = float(ypos);
}

void RT_Scene::GLFW_MouseWheelCallback(GLFWwindow* wnd, double, double dy)
{
}

} // namespace Diligent

int main()
{
    Diligent::RT_Scene Scene;
    if (!Scene.Create({1280, 1024}))
        return -1;

    for (; Scene.Update();) {}
}
