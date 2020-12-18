#include <random>
#include <thread>

#include "Timer.hpp"
#include "MapHelper.hpp"
#include "Sample_VR.hpp"
#include "TextureUtilities.h"
#include "TexturedCube.hpp"
#include "EngineFactoryVk.h"
#include "OpenVRDevice.h"
#include "VREmulator.h"

namespace DEVR
{
using EHmdStatus = IVRDevice::EHmdStatus;

void Sample_VR::CreateUniformBuffer()
{
    BufferDesc CBDesc;
    CBDesc.Name           = "VS constants CB";
    CBDesc.uiSizeInBytes  = sizeof(float4x4) * 2;
    CBDesc.Usage          = USAGE_DYNAMIC;
    CBDesc.BindFlags      = BIND_UNIFORM_BUFFER;
    CBDesc.CPUAccessFlags = CPU_ACCESS_WRITE;

    m_pDevice->CreateBuffer(CBDesc, nullptr, &m_VSConstants);
}

void Sample_VR::CreatePipelineState()
{
    LayoutElement LayoutElems[] =
        {
            LayoutElement{0, 0, 3, VT_FLOAT32, False},
            LayoutElement{1, 0, 2, VT_FLOAT32, False},
            LayoutElement{2, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
            LayoutElement{3, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
            LayoutElement{4, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE},
            LayoutElement{5, 1, 4, VT_FLOAT32, False, INPUT_ELEMENT_FREQUENCY_PER_INSTANCE}};

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);

    m_pPSO = TexturedCube::CreatePipelineState(m_pDevice,
                                               m_VRDevice->GetImageFormat(),
                                               m_DepthFormat,
                                               pShaderSourceFactory,
                                               "cube_inst.vsh",
                                               "cube_inst.psh",
                                               LayoutElems,
                                               _countof(LayoutElems));

    m_pPSO->GetStaticVariableByName(SHADER_TYPE_VERTEX, "Constants")->Set(m_VSConstants);

    m_pPSO->CreateShaderResourceBinding(&m_SRB, true);
}

void Sample_VR::CreateInstanceBuffer()
{
    BufferDesc InstBuffDesc;
    InstBuffDesc.Name          = "Instance data buffer";
    InstBuffDesc.Usage         = USAGE_DEFAULT;
    InstBuffDesc.BindFlags     = BIND_VERTEX_BUFFER;
    InstBuffDesc.uiSizeInBytes = sizeof(float4x4) * MaxInstances;
    m_pDevice->CreateBuffer(InstBuffDesc, nullptr, &m_InstanceBuffer);
    PopulateInstanceBuffer();
}

void Sample_VR::PopulateInstanceBuffer()
{
    std::vector<float4x4> InstanceData(m_GridSize * m_GridSize * m_GridSize);

    float fGridSize = static_cast<float>(m_GridSize);

    std::mt19937                          gen;
    std::uniform_real_distribution<float> scale_distr(0.3f, 1.0f);
    std::uniform_real_distribution<float> offset_distr(-0.15f, +0.15f);
    std::uniform_real_distribution<float> rot_distr(-PI_F, +PI_F);

    float BaseScale = 0.6f / fGridSize;
    int   instId    = 0;
    for (int x = 0; x < m_GridSize; ++x)
    {
        for (int y = 0; y < m_GridSize; ++y)
        {
            for (int z = 0; z < m_GridSize; ++z)
            {
                float xOffset = 2.f * (x + 0.5f + offset_distr(gen)) / fGridSize - 1.f;
                float yOffset = 2.f * (y + 0.5f + offset_distr(gen)) / fGridSize - 1.f;
                float zOffset = 2.f * (z + 0.5f + offset_distr(gen)) / fGridSize - 1.f;
                float scale   = BaseScale * scale_distr(gen);

                float4x4 rotation      = float4x4::RotationX(rot_distr(gen)) * float4x4::RotationY(rot_distr(gen)) * float4x4::RotationZ(rot_distr(gen));
                float4x4 matrix        = rotation * float4x4::Scale(scale, scale, scale) * float4x4::Translation(xOffset, yOffset, zOffset);
                InstanceData[instId++] = matrix;
            }
        }
    }

    Uint32 DataSize = static_cast<Uint32>(sizeof(InstanceData[0]) * InstanceData.size());
    m_pContext->UpdateBuffer(m_InstanceBuffer, 0, DataSize, InstanceData.data(), RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Sample_VR::CreateRenderTargets()
{
    TextureDesc desc;
    desc.Name      = "Depth texture";
    desc.Type      = RESOURCE_DIM_TEX_2D;
    desc.Width     = m_VRDevice->GetRenderTargetDimension().x;
    desc.Height    = m_VRDevice->GetRenderTargetDimension().y;
    desc.MipLevels = 1;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    desc.Format    = m_DepthFormat;
    m_pDevice->CreateTexture(desc, nullptr, &m_DepthTexture);
}

void Sample_VR::Render()
{
    ITexture*     RenderTargets[2] = {};
    ITextureView* RTViews[2]       = {};
    ITextureView* DSView           = m_DepthTexture->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);

    if (!m_VRDevice->GetRenderTargets(&RenderTargets[0], &RenderTargets[1]))
        return;

    for (uint i = 0; i < 2; ++i)
    {
        RTViews[i] = RenderTargets[i]->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        m_pContext->SetRenderTargets(1, &RTViews[i], DSView, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        const float ClearColor[] = {0.350f, 0.350f, 0.350f, 1.0f};
        m_pContext->ClearRenderTarget(RTViews[i], ClearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_pContext->ClearDepthStencil(DSView, CLEAR_DEPTH_FLAG, 1.f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        {
            MapHelper<float4x4> CBConstants(m_pContext, m_VSConstants, MAP_WRITE, MAP_FLAG_DISCARD);
            CBConstants[0] = m_ViewProjMatrix[i].Transpose();
            CBConstants[1] = m_RotationMatrix.Transpose();
        }

        Uint32   offsets[] = {0, 0};
        IBuffer* pBuffs[]  = {m_CubeVertexBuffer, m_InstanceBuffer};
        m_pContext->SetVertexBuffers(0, _countof(pBuffs), pBuffs, offsets, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, SET_VERTEX_BUFFERS_FLAG_RESET);
        m_pContext->SetIndexBuffer(m_CubeIndexBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        m_pContext->SetPipelineState(m_pPSO);
        m_pContext->CommitShaderResources(m_SRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawIndexedAttribs DrawAttrs;
        DrawAttrs.IndexType    = VT_UINT32;
        DrawAttrs.NumIndices   = 36;
        DrawAttrs.NumInstances = m_GridSize * m_GridSize * m_GridSize;

        DrawAttrs.Flags = DRAW_FLAG_VERIFY_ALL;
        m_pContext->DrawIndexed(DrawAttrs);
    }
}

float4x4 Sample_VR::GetAdjustedProjectionMatrix(float FOV, float NearPlane, float FarPlane) const
{
    uint2 Dim         = m_VRDevice->GetRenderTargetDimension();
    float AspectRatio = float(Dim.x) / Dim.y;
    float YScale      = 1.f / std::tan(FOV / 2.f);
    float XScale      = YScale / AspectRatio;

    float4x4 Proj;
    Proj._11 = XScale;
    Proj._22 = YScale;
    Proj.SetNearFarClipPlanes(NearPlane, FarPlane, m_pDevice->GetDeviceCaps().IsGLDevice());
    return Proj;
}

void Sample_VR::Update(double CurrTime, double ElapsedTime)
{
    auto& vrCamera = m_VRDevice->GetCamera();

    float4x4 View = float4x4::Translation(vrCamera.position) * vrCamera.pose; //float4x4::RotationX(-0.6f) * float4x4::Translation(0.f, 0.f, 4.0f);
    float4x4 Proj = GetAdjustedProjectionMatrix(PI_F / 4.0f, 0.1f, 100.f);

    m_ViewProjMatrix[0] = View * vrCamera.left.view * vrCamera.left.proj;
    m_ViewProjMatrix[1] = View * vrCamera.right.view * vrCamera.right.proj;

    m_RotationMatrix = float4x4::RotationY(float(CurrTime) * 0.01f) * float4x4::RotationX(-float(CurrTime) * 0.025f);
}

bool Sample_VR::Initialize()
{
    // engine initialization
    {
        m_VRDevice.reset(new VREmulatorVk{});
        //m_VRDevice.reset(new OpenVRDeviceVk{});
        //m_VRDevice.reset(new OpenXRDeviceVk{});

        if (!m_VRDevice->Create())
            return false;

        IVRDevice::Requirements Req;
        m_VRDevice->GetRequirements(Req);

        EngineVkCreateInfo CreateInfo;
        CreateInfo.EnableValidation         = true;
        CreateInfo.NumDeferredContexts      = 0;
        CreateInfo.AdapterId                = Req.AdapterId;
        CreateInfo.InstanceExtensionCount   = Uint32(Req.InstanceExtensions.size());
        CreateInfo.ppInstanceExtensionNames = CreateInfo.InstanceExtensionCount ? Req.InstanceExtensions.data() : nullptr;
        CreateInfo.DeviceExtensionCount     = Uint32(Req.DeviceExtensions.size());
        CreateInfo.ppDeviceExtensionNames   = CreateInfo.DeviceExtensionCount ? Req.DeviceExtensions.data() : nullptr;

        auto* Factory    = GetEngineFactoryVk();
        m_pEngineFactory = Factory;

        IRenderDevice*  Device  = nullptr;
        IDeviceContext* Context = nullptr;
        Factory->CreateDeviceAndContextsVk(CreateInfo, &Device, &Context);
        if (!Device)
            return false;

        m_pDevice  = Device;
        m_pContext = Context;

        m_VRDevice->Initialize(Device, Context);
        m_VRDevice->SetupCamera(float2{0.1f, 100.f});
    }

    CreateRenderTargets();
    CreateUniformBuffer();
    CreatePipelineState();

    m_CubeVertexBuffer = TexturedCube::CreateVertexBuffer(m_pDevice);
    m_CubeIndexBuffer  = TexturedCube::CreateIndexBuffer(m_pDevice);
    m_TextureSRV       = TexturedCube::LoadTexture(m_pDevice, "DGLogo.png")->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    m_SRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture")->Set(m_TextureSRV);

    CreateInstanceBuffer();
    return true;
}

void Sample_VR::Run()
{
    if (!Initialize())
        return;

    Diligent::Timer Timer;
    auto            PrevTime = Timer.GetElapsedTime();

    for (;;)
    {
        if (!m_VRDevice->BeginFrame())
            break;

        EHmdStatus status = m_VRDevice->GetStatus();

        auto CurrTime    = Timer.GetElapsedTime();
        auto ElapsedTime = CurrTime - PrevTime;
        PrevTime         = CurrTime;

        switch (status)
        {
            case EHmdStatus::Active:
            case EHmdStatus::Mounted:
                Update(CurrTime, ElapsedTime);
                Render();
                break;

            case EHmdStatus::Standby:
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
                break;

            case EHmdStatus::PowerOff:
                return;
        }

        m_VRDevice->EndFrame();

        m_pDevice->ReleaseStaleResources();
    }
}

} // namespace DEVR

int main()
{
    DEVR::Sample_VR sample;
    sample.Run();
    return 0;
}
