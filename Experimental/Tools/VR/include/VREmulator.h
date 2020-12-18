#pragma once

#include <chrono>
#include "GLFW/glfw3.h"

#include "IVRDevice.h"
#include "RefCntAutoPtr.hpp"

namespace DEVR
{

class VREmulatorVk final : public IVRDevice
{
public:
    VREmulatorVk();
    ~VREmulatorVk() override;

    bool  Create() noexcept override;
    bool  Initialize(IRenderDevice* device, IDeviceContext* context) noexcept override;

    bool  BeginFrame() noexcept override;
    bool  EndFrame() noexcept override;

    void  SetupCamera(const float2& clipPlanes) noexcept override;
    bool  GetRequirements(Requirements& req) const noexcept override;
    uint2 GetRenderTargetDimension() const noexcept override;
    bool  GetRenderTargets(ITexture** ppLeft, ITexture** ppRight) noexcept override;

private:
    static void GLFW_ErrorCallback(int code, const char* msg);
    static void GLFW_RefreshCallback(GLFWwindow* wnd);
    static void GLFW_ResizeCallback(GLFWwindow* wnd, int w, int h);
    static void GLFW_KeyCallback(GLFWwindow* wnd, int key, int, int, int);
    static void GLFW_MouseButtonCallback(GLFWwindow* wnd, int button, int action, int mods);
    static void GLFW_CursorPosCallback(GLFWwindow* wnd, double xpos, double ypos);
    static void GLFW_MouseWheelCallback(GLFWwindow* wnd, double dx, double dy);

    void CreatePipelineState();

private:
    using TimePoint = std::chrono::high_resolution_clock::time_point;
    using Seconds   = std::chrono::duration<float>;

    GLFWwindow* m_Window = nullptr;

    float2 m_CameraAngle;
    float2 m_LastCursorPos;
    bool   m_MousePressed = false;

    const uint2 m_RenderTargetDim{1024, 1024};
    uint2       m_WindowSize{1024, 512};

    RefCntAutoPtr<IRenderDevice>  m_pDevice;
    RefCntAutoPtr<IDeviceContext> m_pContext;
    RefCntAutoPtr<ISwapChain>     m_pSwapChain;
    RefCntAutoPtr<ITexture>       m_ColorTextures[2];

    RefCntAutoPtr<IPipelineState>         m_pPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pSRB;

    TimePoint m_lastUpdateTime;
};

} // namespace DEVR
