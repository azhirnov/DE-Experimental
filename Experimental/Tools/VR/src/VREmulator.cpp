#include "VREmulator.h"
#include "../include/VulkanUtilities/VulkanHeaders.h"
#include "DeviceContextVk.h"
#include "RenderDeviceVk.h"
#include "EngineFactoryVk.h"
#include "TextureVk.h"
#include "TextureViewVk.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"

namespace DEVR
{
namespace
{

template <typename T>
inline T Wrap(const T& value, const T& minValue, const T& maxValue)
{
    // check for NaN
    if (minValue >= maxValue)
        return minValue;

    T result = T(minValue + std::fmod(value - minValue, maxValue - minValue));

    if (result < minValue)
        result += (maxValue - minValue);

    return result;
}

float4x4 RotateX(float angle)
{
    float s = std::sin(angle);
    float c = std::cos(angle);

    return float4x4{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, c, s, 0.0f,
        0.0f, -s, c, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
}

float4x4 RotateY(float angle)
{
    float s = std::sin(angle);
    float c = std::cos(angle);

    return float4x4{
        c, 0.0f, -s, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        s, 0.0f, c, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f};
}

} // namespace


VREmulatorVk::VREmulatorVk()
{
    m_camera.left.view = float4x4{
        1.00000072f, -0.000183293581f, -0.000353380980f, -0.000000000f,
        0.000182049334f, 0.999995828f, -0.00308410777f, 0.000000000f,
        0.000353740237f, 0.00308382465f, 0.999995828f, -0.000000000f,
        0.0329737701f, -0.000433419773f, 0.000178515897f, 1.00000000f};

    m_camera.right.view = float4x4{
        1.00000072f, 0.000182215153f, 0.000351947267f, -0.000000000f,
        -0.000183455661f, 0.999995947f, 0.00308232009f, 0.000000000f,
        -0.000351546332f, -0.00308261835f, 0.999995947f, -0.000000000f,
        -0.0329739153f, 0.000422920042f, -0.000199772359f, 1.00000000f};
}

VREmulatorVk::~VREmulatorVk()
{
    if (m_Window)
    {
        glfwDestroyWindow(m_Window);
        glfwTerminate();
    }
}

bool VREmulatorVk::Create() noexcept
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

    m_Window = glfwCreateWindow(int(m_WindowSize.x),
                                int(m_WindowSize.y),
                                "VR emulator",
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

    m_hmdStatus      = EHmdStatus::Mounted;
    m_lastUpdateTime = TimePoint::clock::now();
    return true;
}

bool VREmulatorVk::BeginFrame() noexcept
{
    if (m_Window == nullptr)
        return false;

    if (glfwWindowShouldClose(m_Window))
        return false;

    if (m_pSwapChain == nullptr)
        return false;

    glfwPollEvents();

    auto time        = TimePoint::clock::now();
    auto dt          = std::chrono::duration_cast<Seconds>(time - m_lastUpdateTime).count();
    m_lastUpdateTime = time;

    m_CameraAngle.x = Wrap(m_CameraAngle.x, -PI_F, PI_F);
    m_CameraAngle.y = Wrap(m_CameraAngle.y, -PI_F, PI_F);
    m_camera.pose   = RotateX(-m_CameraAngle.y) * RotateY(-m_CameraAngle.x);

    (void)(dt);

    return true;
}

bool VREmulatorVk::EndFrame() noexcept
{
    if (m_pSwapChain == nullptr)
    {
        LOG_ERROR("Swapchain is not created");
        return false;
    }

    ITextureView* RTV = m_pSwapChain->GetCurrentBackBufferRTV();
    m_pContext->SetRenderTargets(1, &RTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_LeftTex")->Set(m_ColorTextures[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    m_pSRB->GetVariableByName(SHADER_TYPE_PIXEL, "g_RightTex")->Set(m_ColorTextures[0]->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));

    m_pContext->SetPipelineState(m_pPSO);
    m_pContext->CommitShaderResources(m_pSRB, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    DrawAttribs Attribs;
    Attribs.NumVertices = 4;
    m_pContext->Draw(Attribs);

    m_pContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    m_pSwapChain->Present();

    return true;
}

void VREmulatorVk::SetupCamera(const float2& clipPlanes) noexcept
{
    if (m_camera.clipPlanes != clipPlanes)
    {
        m_camera.clipPlanes = clipPlanes;

        const float fov_y         = 1.0f;
        const float aspect        = 1.0f;
        const float tan_half_fovy = std::tan(fov_y * 0.5f);

        float4x4 proj = float4x4::Identity();
        proj[0][0]    = 1.0f / (aspect * tan_half_fovy);
        proj[1][1]    = 1.0f / tan_half_fovy;
        proj[2][2]    = clipPlanes[1] / (clipPlanes[1] - clipPlanes[0]);
        proj[2][3]    = 1.0f;
        proj[3][2]    = -(clipPlanes[1] * clipPlanes[0]) / (clipPlanes[1] - clipPlanes[0]);

        m_camera.left.proj  = proj;
        m_camera.right.proj = proj;
    }
}

bool VREmulatorVk::GetRequirements(Requirements&) const noexcept
{
    // you can use any adapter
    return false;
}

bool VREmulatorVk::Initialize(IRenderDevice* device, IDeviceContext* context) noexcept
{
    if (m_Window == nullptr)
    {
        LOG_ERROR("VR emulator window is not created");
        return false;
    }

    IRenderDeviceVk*  renderDevice  = nullptr;
    IDeviceContextVk* deviceContext = nullptr;
    device->QueryInterface(IID_RenderDeviceVk, reinterpret_cast<IObject**>(static_cast<IRenderDeviceVk**>(&renderDevice)));
    context->QueryInterface(IID_DeviceContextVk, reinterpret_cast<IObject**>(static_cast<IDeviceContextVk**>(&deviceContext)));

    if (!(deviceContext && renderDevice))
    {
        LOG_ERROR("Failed to get DeviceContext and RenderDevice");
        return false;
    }

    IEngineFactory* factory = device->GetEngineFactory();
    if (factory == nullptr)
        return false;

    IEngineFactoryVk* factoryVk;
    factory->QueryInterface(IID_EngineFactoryVk, reinterpret_cast<IObject**>(static_cast<IEngineFactoryVk**>(&factoryVk)));

    SwapChainDesc     SCDesc;
    Win32NativeWindow Window{glfwGetWin32Window(m_Window)};
    factoryVk->CreateSwapChainVk(device, context, SCDesc, Window, &m_pSwapChain);

    if (m_pSwapChain == nullptr)
    {
        LOG_ERROR("Failed to create swapchain for VR emulator");
        return false;
    }

    m_pDevice  = device;
    m_pContext = context;

    CreatePipelineState();
    
    m_RenderTargetFormat = TEX_FORMAT_RGBA8_UNORM;

    TextureDesc desc;
    desc.Type      = RESOURCE_DIM_TEX_2D;
    desc.Width     = m_RenderTargetDim.x;
    desc.Height    = m_RenderTargetDim.y;
    desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    desc.Format    = m_RenderTargetFormat;
    desc.MipLevels = 1;
    desc.Name      = "VR left eye";

    m_pDevice->CreateTexture(desc, nullptr, &m_ColorTextures[0]);

    desc.Name = "VR right eye";
    m_pDevice->CreateTexture(desc, nullptr, &m_ColorTextures[1]);

    return true;
}

uint2 VREmulatorVk::GetRenderTargetDimension() const noexcept
{
    return m_RenderTargetDim;
}

bool VREmulatorVk::GetRenderTargets(ITexture** ppLeft, ITexture** ppRight) noexcept
{
    if (m_ColorTextures[0] == nullptr || m_ColorTextures[1] == nullptr)
        return false;

    *ppLeft  = m_ColorTextures[0];
    *ppRight = m_ColorTextures[1];
    return true;
}

void VREmulatorVk::CreatePipelineState()
{
    GraphicsPipelineStateCreateInfo PSOCreateInfo;

    PSOCreateInfo.PSODesc.Name         = "VR emulator blit PSO";
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;

    PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = m_pSwapChain->GetDesc().ColorBufferFormat;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

    ShaderCreateInfo ShaderCI;
    ShaderCI.SourceLanguage             = SHADER_SOURCE_LANGUAGE_GLSL_VERBATIM;
    ShaderCI.UseCombinedTextureSamplers = true;

    RefCntAutoPtr<IShader> pVS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "VR emulator blit VS";
        ShaderCI.Source          = R"glsl(
#version 450

layout(location=0) out vec2 v_Texcoord;

void main()
{
    v_Texcoord  = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
    gl_Position = vec4(v_Texcoord * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";
        m_pDevice->CreateShader(ShaderCI, &pVS);
        VERIFY_EXPR(pVS != nullptr);
    }

    RefCntAutoPtr<IShader> pPS;
    {
        ShaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL;
        ShaderCI.EntryPoint      = "main";
        ShaderCI.Desc.Name       = "VR emulator blit PS";
        ShaderCI.Source          = R"glsl(
#version 450

uniform sampler2D g_LeftTex;
uniform sampler2D g_RightTex;

layout(location=0) in  vec2 v_Texcoord;
layout(location=0) out vec4 out_Color;

void main()
{
    vec2 ltc = vec2(v_Texcoord.x * 2.0, v_Texcoord.y);
    vec2 rtc = vec2((v_Texcoord.x - 0.5) * 2.0, v_Texcoord.y);
    vec4 col = vec4(0.0);

    if (v_Texcoord.x < 0.5)
        col = texture(g_LeftTex, ltc);
    else
    if (v_Texcoord.x > 0.5)
        col = texture(g_RightTex, rtc);

    out_Color = col;
}
)glsl";
        m_pDevice->CreateShader(ShaderCI, &pPS);
        VERIFY_EXPR(pVS != nullptr);
    }

    PSOCreateInfo.pVS = pVS;
    PSOCreateInfo.pPS = pPS;

    SamplerDesc SamLinearClampDesc{
        FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR, FILTER_TYPE_LINEAR,
        TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP, TEXTURE_ADDRESS_CLAMP};

    ImmutableSamplerDesc ImmutableSamplers[]                  = {{SHADER_TYPE_PIXEL, "g_LeftTex", SamLinearClampDesc}, {SHADER_TYPE_PIXEL, "g_RightTex", SamLinearClampDesc}};
    PSOCreateInfo.PSODesc.ResourceLayout.ImmutableSamplers    = ImmutableSamplers;
    PSOCreateInfo.PSODesc.ResourceLayout.NumImmutableSamplers = _countof(ImmutableSamplers);
    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType  = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;

    m_pDevice->CreateGraphicsPipelineState(PSOCreateInfo, &m_pPSO);

    m_pPSO->CreateShaderResourceBinding(&m_pSRB, true);
}

void VREmulatorVk::GLFW_ErrorCallback(int code, const char* msg)
{
}

void VREmulatorVk::GLFW_RefreshCallback(GLFWwindow* wnd)
{
}

void VREmulatorVk::GLFW_ResizeCallback(GLFWwindow* wnd, int w, int h)
{
    auto* self = static_cast<VREmulatorVk*>(glfwGetWindowUserPointer(wnd));

    self->m_WindowSize = uint2{uint(w), uint(h)};

    if (self->m_pSwapChain)
        self->m_pSwapChain->Resize(w, h);
}

void VREmulatorVk::GLFW_KeyCallback(GLFWwindow* wnd, int key, int, int action, int)
{
}

void VREmulatorVk::GLFW_MouseButtonCallback(GLFWwindow* wnd, int button, int action, int)
{
    auto* self = static_cast<VREmulatorVk*>(glfwGetWindowUserPointer(wnd));

    if (button == GLFW_MOUSE_BUTTON_LEFT)
        self->m_MousePressed = (action != GLFW_RELEASE);
}

void VREmulatorVk::GLFW_CursorPosCallback(GLFWwindow* wnd, double xpos, double ypos)
{
    auto*  self = static_cast<VREmulatorVk*>(glfwGetWindowUserPointer(wnd));
    float2 pos  = {float(xpos), float(ypos)};

    if (self->m_MousePressed)
    {
        float2 delta = pos - self->m_LastCursorPos;
        self->m_CameraAngle += float2{delta.x, -delta.y} * 0.01f;
    }

    self->m_LastCursorPos = pos;
}

void VREmulatorVk::GLFW_MouseWheelCallback(GLFWwindow* wnd, double, double dy)
{
}

} // namespace DEVR
