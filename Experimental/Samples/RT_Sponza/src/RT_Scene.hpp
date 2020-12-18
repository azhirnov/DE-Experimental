/*
    used glTF Sponza from https://github.com/KhronosGroup/glTF-Sample-Models/tree/master/2.0/Sponza
*/

#pragma once

#include <chrono>
#include <array>
#include "GLFW/glfw3.h"

#include "BasicMath.hpp"
#include "RefCntAutoPtr.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "GLTFLoader.hpp"
#include "FirstPersonCamera.hpp"

namespace Diligent
{

class RT_Scene
{
public:
    RT_Scene();
    ~RT_Scene();

    bool Create(uint2 size) noexcept;
    bool Update() noexcept;

private:
    static void GLFW_ErrorCallback(int code, const char* msg);
    static void GLFW_RefreshCallback(GLFWwindow* wnd);
    static void GLFW_ResizeCallback(GLFWwindow* wnd, int w, int h);
    static void GLFW_KeyCallback(GLFWwindow* wnd, int key, int, int, int);
    static void GLFW_MouseButtonCallback(GLFWwindow* wnd, int button, int action, int mods);
    static void GLFW_CursorPosCallback(GLFWwindow* wnd, double xpos, double ypos);
    static void GLFW_MouseWheelCallback(GLFWwindow* wnd, double dx, double dy);

    void CreateRayTracingPSO();
    void CreateToneMapPSO();
    void BindResources();
    void ReloadShaders();

    void CreateBLAS();
    void CreateTLAS();
    void CreateSBT();
    void LoadScene(const char* Path);
    void Render();
    void OnResize(Uint32 w, Uint32 h);

private:
    class InputControllerGLFW : public InputControllerBase
    {
    public:
        InputControllerGLFW();

        MouseState& EditMouseState();

        void SetKeyState(InputKeys Key, INPUT_KEY_STATE_FLAGS Flags);
    };

    using TimePoint = std::chrono::high_resolution_clock::time_point;
    using Seconds   = std::chrono::duration<float>;

    static constexpr uint MaxRecursionDepth = 1;
    static constexpr int  HitGroupStride    = 2;
    static constexpr int  PrimaryRayIndex   = 0;
    static constexpr int  ShadowRayIndex    = 1;

    RefCntAutoPtr<IDeviceContext> m_pContext;
    RefCntAutoPtr<IRenderDevice>  m_pDevice;
    RefCntAutoPtr<ISwapChain>     m_pSwapChain;
    RefCntAutoPtr<IEngineFactory> m_pEngineFactory;
    GLFWwindow*                   m_Window = nullptr;

    RefCntAutoPtr<IBottomLevelAS>         m_pOpaqueBLAS;
    RefCntAutoPtr<IBottomLevelAS>         m_pTranslucentBLAS;
    RefCntAutoPtr<ITopLevelAS>            m_pTLAS;
    RefCntAutoPtr<IShaderBindingTable>    m_pSBT;
    RefCntAutoPtr<IPipelineState>         m_pRayTracingPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pRayTracingSRB;

    RefCntAutoPtr<IPipelineState>         m_pToneMapPSO;
    RefCntAutoPtr<IShaderResourceBinding> m_pToneMapSRB;

    std::unique_ptr<GLTF::Model>              m_Model;
    RefCntAutoPtr<IBuffer>                    m_CameraAttribsCB;
    RefCntAutoPtr<IBuffer>                    m_LightAttribsCB;
    RefCntAutoPtr<IBuffer>                    m_MaterialAttribsSB;
    std::vector<RefCntAutoPtr<ITextureView>>  m_MaterialColorMaps;
    std::vector<RefCntAutoPtr<ITextureView>>  m_MaterialPhysicalDescMaps;
    std::vector<RefCntAutoPtr<ITextureView>>  m_MaterialNormalMaps;
    std::vector<RefCntAutoPtr<ITextureView>>  m_MaterialEmissiveMaps;
    std::array<RefCntAutoPtr<IBufferView>, 2> m_PrimitiveOffsets;
    RefCntAutoPtr<ITextureView>               m_pWhiteTexSRV;
    RefCntAutoPtr<ITextureView>               m_pBlackTexSRV;
    RefCntAutoPtr<ITextureView>               m_pDefaultNormalMapSRV;

    TEXTURE_FORMAT              m_ColorBufferFormat = TEX_FORMAT_RGBA16_FLOAT;
    TEXTURE_FORMAT              m_DepthBufferFormat = TEX_FORMAT_R32_FLOAT;
    RefCntAutoPtr<ITextureView> m_ColorUAV;
    RefCntAutoPtr<ITextureView> m_ColorSRV;
    RefCntAutoPtr<ITextureView> m_DepthUAV;
    RefCntAutoPtr<ITextureView> m_DepthSRV;

    TimePoint           m_LastUpdateTime;
    FirstPersonCamera   m_Camera;
    InputControllerGLFW m_InputController;
};

} // namespace Diligent
