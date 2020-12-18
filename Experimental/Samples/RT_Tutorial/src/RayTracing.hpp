#pragma once

#include "SampleBase.hpp"
#include "BasicMath.hpp"
#include "FirstPersonCamera.hpp"
#include "ShaderDebugger.h"

namespace Diligent
{
#include "../assets/structures.fxh"


class RayTracing final : public SampleBase
{
public:
    RayTracing();

    virtual void GetEngineInitializationAttribs(RENDER_DEVICE_TYPE DeviceType, EngineCreateInfo& EngineCI, SwapChainDesc& SCDesc) override final;
    virtual void Initialize(const SampleInitInfo& InitInfo) override final;

    virtual void Render() override final;
    virtual void Update(double CurrTime, double ElapsedTime) override final;

    virtual const Char* GetSampleName() const override final { return "Tutorial21: Ray tracing"; }

    virtual void WindowResize(Uint32 Width, Uint32 Height) override final;

private:
    void CreateRayTracingPSO();
    void CreateGraphicsPSO();
    void CreateCubeBLAS();
    void CreateProceduralBLAS();
    void UpdateTLAS();
    void CreateSBT();
    void LoadTextures();
    void UpdateUI();
    void BindResources();

    static constexpr int NumTextures       = 4;
    static constexpr int MaxRecursionDepth = 8;
    static constexpr int NumCubes          = 4;

    RefCntAutoPtr<IBuffer>  m_CubeAttribsCB;
    RefCntAutoPtr<IBuffer>  m_BoxAttribsCB;
    RefCntAutoPtr<IBuffer>  m_ConstantsCB;
    RefCntAutoPtr<ITexture> m_Textures[NumTextures] = {};
    RefCntAutoPtr<ITexture> m_pGroundTex;

    RefCntAutoPtr<IPipelineState> m_pRayTracingPSO;
    RefCntAutoPtr<DE::ISharedSRB> m_pRayTracingSRB;

    RefCntAutoPtr<IPipelineState>         m_pImageBlitPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pImageBlitSRB;

    RefCntAutoPtr<IBottomLevelAS> m_pCubeBLAS;
    RefCntAutoPtr<IBottomLevelAS> m_pProceduralBLAS;
    RefCntAutoPtr<ITopLevelAS>    m_pTLAS;
    RefCntAutoPtr<IBuffer>        m_InstanceBuffer;
    RefCntAutoPtr<IBuffer>        m_ScratchBuffer;
    RefCntAutoPtr<DE::ISharedSBT> m_pSBT;

    const double      m_MaxAnimationTimeDelta = 1.0 / 60.0;
    float             m_AnimationTime         = 0.0f;
    Constants         m_Constants             = {};
    bool              m_EnableCubes[NumCubes] = {true, true, true, true};
    FirstPersonCamera m_Camera;

    bool  m_ClockHeatmap  = false;
    bool  m_DebugShader   = false;
    bool  m_ProfileShader = false;
    uint2 m_DebugCoord;

    TEXTURE_FORMAT          m_ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;
    RefCntAutoPtr<ITexture> m_pColorRT;

    DE::ShaderDebugger m_ShaderDebugger;
};

} // namespace Diligent
