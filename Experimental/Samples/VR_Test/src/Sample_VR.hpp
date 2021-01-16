#pragma once

#include "IVRDevice.h"

namespace DEVR
{

class Sample_VR
{
public:
    void Run();

private:
    bool Initialize();
    void Update(double CurrTime, double ElapsedTime);
    void Render();

    void     CreatePipelineState();
    void     CreateInstanceBuffer();
    void     PopulateInstanceBuffer();
    void     CreateRenderTargets();
    void     CreateUniformBuffer();
    float4x4 GetAdjustedProjectionMatrix(float FOV, float NearPlane, float FarPlane) const;

    RefCntAutoPtr<IEngineFactory> m_pEngineFactory;
    RefCntAutoPtr<IRenderDevice>  m_pDevice;
    RefCntAutoPtr<IDeviceContext> m_pContext;
    std::unique_ptr<IVRDevice>    m_VRDevice;

    RefCntAutoPtr<ITexture>     m_DepthTexture;
    const TEXTURE_FORMAT        m_DepthFormat = TEX_FORMAT_D32_FLOAT;

    RefCntAutoPtr<IPipelineState>         m_pPSO;
    RefCntAutoPtr<IBuffer>                m_CubeVertexBuffer;
    RefCntAutoPtr<IBuffer>                m_CubeIndexBuffer;
    RefCntAutoPtr<IBuffer>                m_InstanceBuffer;
    RefCntAutoPtr<IBuffer>                m_VSConstants;
    RefCntAutoPtr<ITextureView>           m_TextureSRV;
    RefCntAutoPtr<IShaderResourceBinding> m_SRB;

    float4x4             m_ViewProjMatrix[2];
    float4x4             m_RotationMatrix;
    int                  m_GridSize   = 5;
    static constexpr int MaxGridSize  = 32;
    static constexpr int MaxInstances = MaxGridSize * MaxGridSize * MaxGridSize;
};

} // namespace DEVR
