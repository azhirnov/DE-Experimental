#include "OpenVRDevice.h"

#ifdef WIN32
#    include <Windows.h>
#endif

namespace DEVR
{
namespace
{

float4x4 OpenVRMatToMat4(const HmdMatrix44_t& mat) noexcept
{
    return float4x4{
        mat.m[0][0], mat.m[1][0], mat.m[2][0], mat.m[3][0],
        mat.m[0][1], mat.m[1][1], mat.m[2][1], mat.m[3][1],
        mat.m[0][2], mat.m[1][2], mat.m[2][2], mat.m[3][2],
        mat.m[0][3], mat.m[1][3], mat.m[2][3], mat.m[3][3]};
}

float4x4 OpenVRMatToMat4(const HmdMatrix34_t& mat) noexcept
{
    return float4x4{
        mat.m[0][0], mat.m[1][0], mat.m[2][0], 0.0f,
        mat.m[0][1], mat.m[1][1], mat.m[2][1], 0.0f,
        mat.m[0][2], mat.m[1][2], mat.m[2][2], 0.0f,
        mat.m[0][3], mat.m[1][3], mat.m[2][3], 1.0f};
}

float3x3 OpenVRMatToMat3(const HmdMatrix34_t& mat) noexcept
{
    return float3x3{
        mat.m[0][0], mat.m[1][0], mat.m[2][0],
        mat.m[0][1], mat.m[1][1], mat.m[2][1],
        mat.m[0][2], mat.m[1][2], mat.m[2][2]};
}

uint32_t ConvertFormat(TEXTURE_FORMAT src) noexcept
{
    switch (src)
    {
        case TEX_FORMAT_RGBA8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
        case TEX_FORMAT_RGBA8_UNORM_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case TEX_FORMAT_BGRA8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
        //case TEX_FORMAT_BGRA8_UNORM_sRGB: return VK_FORMAT_B8G8R8A8_SRGB;
        case TEX_FORMAT_RGBA32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case TEX_FORMAT_RGB32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
        case TEX_FORMAT_RGBA16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case TEX_FORMAT_RGB10A2_UNORM: return VK_FORMAT_A2R10G10B10_UINT_PACK32;
    }
    return VK_FORMAT_UNDEFINED; // TODO: throw ?
}

} // namespace

using namespace std::string_literals;

OpenVRDevice::~OpenVRDevice()
{
    if (m_hmd)
    {
        m_ovr.ShutdownInternal();
        m_hmd          = {};
        m_vrSystem     = nullptr;
        m_vrCompositor = nullptr;
    }

    if (m_openVRLib)
    {
#ifdef WIN32
        ::FreeLibrary(HMODULE(m_openVRLib));
#else
        ::dlclose(m_openVRLib);
#endif
        m_openVRLib = nullptr;
    }
}

String OpenVRDevice::GetTrackedDeviceString(TrackedDeviceIndex_t unDevice, TrackedDeviceProperty prop, TrackedPropertyError* peError) const noexcept
{
    uint32_t unRequiredBufferLen = m_vrSystem->GetStringTrackedDeviceProperty(unDevice, prop, nullptr, 0, peError);
    if (unRequiredBufferLen == 0)
        return "";

    char* pchBuffer     = new char[unRequiredBufferLen];
    unRequiredBufferLen = m_vrSystem->GetStringTrackedDeviceProperty(unDevice, prop, pchBuffer, unRequiredBufferLen, peError);
    std::string sResult = pchBuffer;
    delete[] pchBuffer;
    return sResult;
}

bool OpenVRDevice::Create() noexcept
{
    if (!LoadLib("openvr_api.dll"))
    {
        LOG_ERROR_MESSAGE("failed to load OpenVR library");
        return false;
    }

    if (!m_ovr.IsHmdPresent())
    {
        LOG_ERROR_MESSAGE("VR Headset is not present");
        return false;
    }

    if (!m_ovr.IsRuntimeInstalled())
    {
        LOG_ERROR_MESSAGE("VR Runtime is not installed");
        return false;
    }

    EVRInitError err = EVRInitError_VRInitError_None;
    m_hmd            = m_ovr.InitInternal(&err, EVRApplicationType_VRApplication_Scene);

    if (err != EVRInitError_VRInitError_None)
    {
        LOG_ERROR_MESSAGE("VR_Init error: "s + m_ovr.GetVRInitErrorAsEnglishDescription(err));
        return false;
    }
    m_vrSystem = reinterpret_cast<IVRSystemTable>(m_ovr.GetGenericInterface(("FnTable:"s + IVRSystem_Version).c_str(), &err));
    if (!(m_vrSystem && err == EVRInitError_VRInitError_None))
    {
        LOG_ERROR_MESSAGE("can't get IVRSystemTable");
        return false;
    }

    LOG_INFO_MESSAGE("driver:  "s + GetTrackedDeviceString(k_unTrackedDeviceIndex_Hmd, ETrackedDeviceProperty_Prop_TrackingSystemName_String) +
                     "display: " + GetTrackedDeviceString(k_unTrackedDeviceIndex_Hmd, ETrackedDeviceProperty_Prop_TrackingSystemName_String));

    m_vrCompositor = reinterpret_cast<IVRCompositorTable>(m_ovr.GetGenericInterface(("FnTable:"s + IVRCompositor_Version).c_str(), &err));
    if (!(m_vrCompositor && err == EVRInitError_VRInitError_None))
    {
        LOG_ERROR_MESSAGE("can't get IVRCompositorTable");
        return false;
    }

    m_vrCompositor->SetTrackingSpace(ETrackingUniverseOrigin_TrackingUniverseStanding);

    EDeviceActivityLevel level = m_vrSystem->GetTrackedDeviceActivityLevel(k_unTrackedDeviceIndex_Hmd);
    switch (level)
    {
        case EDeviceActivityLevel_k_EDeviceActivityLevel_Unknown: m_hmdStatus = EHmdStatus::PowerOff; break;
        case EDeviceActivityLevel_k_EDeviceActivityLevel_Idle: m_hmdStatus = EHmdStatus::Standby; break;
        case EDeviceActivityLevel_k_EDeviceActivityLevel_UserInteraction: m_hmdStatus = EHmdStatus::Active; break;
        case EDeviceActivityLevel_k_EDeviceActivityLevel_UserInteraction_Timeout: m_hmdStatus = EHmdStatus::Standby; break;
        case EDeviceActivityLevel_k_EDeviceActivityLevel_Standby: m_hmdStatus = EHmdStatus::Standby; break;
        case EDeviceActivityLevel_k_EDeviceActivityLevel_Idle_Timeout: m_hmdStatus = EHmdStatus::Standby; break;
    }

    return true;
}

bool OpenVRDevice::BeginFrame() noexcept
{
    if (!m_vrSystem || !m_vrCompositor)
    {
        LOG_ERROR_MESSAGE("OpenVR is not created");
        return false;
    }

    VREvent_t ev;
    while (m_vrSystem->PollNextEvent(&ev, sizeof(ev)))
    {
        if (ev.trackedDeviceIndex == k_unTrackedDeviceIndex_Hmd)
            ProcessHmdEvents(ev);
    }

    return true;
}

void OpenVRDevice::ProcessHmdEvents(const VREvent_t& ev) noexcept
{
    switch (ev.eventType)
    {
        case EVREventType_VREvent_TrackedDeviceActivated:
        case EVREventType_VREvent_TrackedDeviceUserInteractionStarted:
            if (m_hmdStatus < EHmdStatus::Active)
            {
                m_hmdStatus = EHmdStatus::Active;
            }
            break;

        case EVREventType_VREvent_TrackedDeviceUserInteractionEnded:
            m_hmdStatus = EHmdStatus::Standby;
            break;

        case EVREventType_VREvent_ButtonPress:
            if (ev.data.controller.button == EVRButtonId_k_EButton_ProximitySensor)
            {
                m_hmdStatus = EHmdStatus::Mounted;
            }
            break;

        case EVREventType_VREvent_ButtonUnpress:
            if (ev.data.controller.button == EVRButtonId_k_EButton_ProximitySensor)
            {
                m_hmdStatus = EHmdStatus::Active;
            }
            break;

        case EVREventType_VREvent_Quit:
            m_hmdStatus = EHmdStatus::PowerOff;
            break;

        case EVREventType_VREvent_LensDistortionChanged:
            SetupCamera(m_camera.clipPlanes);
            break;

        case EVREventType_VREvent_PropertyChanged:
            break;
    }
}

void OpenVRDevice::SetupCamera(const float2& clipPlanes) noexcept
{
    if (!m_vrSystem)
        return;

    m_camera.clipPlanes = clipPlanes;
    m_camera.left.proj  = OpenVRMatToMat4(m_vrSystem->GetProjectionMatrix(EVREye_Eye_Left, m_camera.clipPlanes[0], m_camera.clipPlanes[1]));
    m_camera.right.proj = OpenVRMatToMat4(m_vrSystem->GetProjectionMatrix(EVREye_Eye_Right, m_camera.clipPlanes[0], m_camera.clipPlanes[1]));
    m_camera.left.view  = OpenVRMatToMat4(m_vrSystem->GetEyeToHeadTransform(EVREye_Eye_Left)).Inverse();
    m_camera.right.view = OpenVRMatToMat4(m_vrSystem->GetEyeToHeadTransform(EVREye_Eye_Right)).Inverse();
}

void OpenVRDevice::UpdateHMDMatrixPose() noexcept
{
    if (m_vrCompositor->WaitGetPoses(m_trackedDevicePose, _countof(m_trackedDevicePose), nullptr, 0) != EVRCompositorError_VRCompositorError_None)
    {
        LOG_INFO_MESSAGE("Failed to update pos");
        return;
    }

    auto& hmd_pose = m_trackedDevicePose[k_unTrackedDeviceIndex_Hmd];

    if (hmd_pose.bPoseIsValid)
    {
        auto& mat  = hmd_pose.mDeviceToAbsoluteTracking;
        auto& vel  = hmd_pose.vVelocity;
        auto& avel = hmd_pose.vAngularVelocity;

        m_camera.pose            = OpenVRMatToMat4(mat).Inverse();
        m_camera.position        = {mat.m[0][3], mat.m[1][3], mat.m[2][3]};
        m_camera.velocity        = {vel.v[0], vel.v[1], vel.v[2]};
        m_camera.angularVelocity = {avel.v[0], avel.v[1], avel.v[2]};
    }
    else
    {
        m_camera.velocity        = float3{0.0f, 0.0f, 0.0f};
        m_camera.angularVelocity = float3{0.0f, 0.0f, 0.0f};
    }
}

bool OpenVRDevice::LoadLib(const String& libPath) noexcept
{
#ifdef WIN32
    m_openVRLib = ::LoadLibraryA(libPath.c_str());

    const auto Load = [lib = m_openVRLib](auto& outResult, const char* procName) -> bool {
        using FN  = std::remove_reference_t<decltype(outResult)>;
        outResult = reinterpret_cast<FN>(::GetProcAddress(HMODULE(lib), procName));
        return outResult != nullptr;
    };
#else
    m_openVRLib = ::dlopen(libPath.c_str(), RTLD_LAZY | RTLD_LOCAL);

    const auto Load = [lib = _openVRLib](auto& outResult, const char* procName) -> bool {
        using FN = std::remove_reference_t<decltype(outResult)>;
        outResult = reinterpret_cast<FN>(::dlsym(lib, procName));
        return outResult != null;
    };
#endif
#define VR_LOAD(_name_) res &= Load(m_ovr._name_, "VR_" #_name_)

    if (m_openVRLib == nullptr)
        return false;

    bool res = true;
    res &= VR_LOAD(InitInternal);
    res &= VR_LOAD(ShutdownInternal);
    res &= VR_LOAD(IsHmdPresent);
    res &= VR_LOAD(GetGenericInterface);
    res &= VR_LOAD(IsRuntimeInstalled);
    res &= VR_LOAD(GetVRInitErrorAsSymbol);
    res &= VR_LOAD(GetVRInitErrorAsEnglishDescription);
    return res;

#undef VR_LOAD
}

bool OpenVRDeviceVk::GetRequirements(Requirements& req) const noexcept
{
    volkInitialize();

    VkApplicationInfo    appInfo  = {};
    VkInstanceCreateInfo info     = {};
    VkInstance           instance = VK_NULL_HANDLE;

    appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.apiVersion       = VK_API_VERSION_1_0;
    appInfo.pApplicationName = "temp";
    appInfo.pEngineName      = "temp";

    info.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &appInfo;

    if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS)
    {
        LOG_ERROR_MESSAGE("Failed to create Vulkan instance");
        return false;
    }

    volkLoadInstance(instance);

    uint             count = 0;
    VkPhysicalDevice physicalDevices[8];
    vkEnumeratePhysicalDevices(instance, &count, nullptr);

    count = min(count, _countof(physicalDevices));
    vkEnumeratePhysicalDevices(instance, &count, physicalDevices);

    uint64_t device = 0;
    m_vrSystem->GetOutputDevice(&device, ETextureType_TextureType_Vulkan, static_cast<VkInstance_T*>(instance));

    bool found = false;
    for (uint i = 0; i < count; ++i)
    {
        if (physicalDevices[i] == VkPhysicalDevice(device))
        {
            found         = true;
            req.AdapterId = i;
            break;
        }
    }

    if (!found)
    {
        LOG_ERROR_MESSAGE("can't find vulkan device for VR output");
    }

    vkDestroyInstance(instance, nullptr);
    return found;
}

bool OpenVRDeviceVk::Initialize(IRenderDevice* device, IDeviceContext* context) noexcept
{
    if (!(device && context))
    {
        LOG_ERROR_MESSAGE("OpenVR is not created");
        return false;
    }

    device->QueryInterface(IID_RenderDeviceVk, reinterpret_cast<IObject**>(static_cast<IRenderDeviceVk**>(&m_RenderDevice)));
    context->QueryInterface(IID_DeviceContextVk, reinterpret_cast<IObject**>(static_cast<IDeviceContextVk**>(&m_DeviceContext)));

    if (!(m_DeviceContext && m_RenderDevice))
    {
        LOG_ERROR_MESSAGE("Failed to get DeviceContext and RenderDevice");
        return false;
    }

    uint64_t physicalDevice = 0;
    m_vrSystem->GetOutputDevice(&physicalDevice, ETextureType_TextureType_Vulkan, m_RenderDevice->GetVkInstance());

    if (VkPhysicalDevice(physicalDevice) != m_RenderDevice->GetVkPhysicalDevice())
    {
        LOG_ERROR_MESSAGE("VR output device is not compatible with render device");
        m_RenderDevice  = nullptr;
        m_DeviceContext = nullptr;
        return false;
    }

    m_RenderTargetFormat = TEX_FORMAT_RGBA8_UNORM;

    TextureDesc desc;
    desc.Type      = RESOURCE_DIM_TEX_2D;
    desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    desc.Format    = m_RenderTargetFormat;
    desc.MipLevels = 1;
    desc.Name      = "OpenVR left eye";

    m_vrSystem->GetRecommendedRenderTargetSize(&desc.Width, &desc.Height);

    m_RenderDevice->CreateTexture(desc, nullptr, &m_ColorTextures[0]);

    desc.Name = "OpenVR right eye";
    m_RenderDevice->CreateTexture(desc, nullptr, &m_ColorTextures[1]);

    return true;
}

bool OpenVRDeviceVk::EndFrame() noexcept
{
    if (!(m_hmd && m_vrSystem && m_vrCompositor))
    {
        LOG_ERROR_MESSAGE("OpenVR is not created");
        return false;
    }

    if (!(m_DeviceContext && m_RenderDevice))
    {
        LOG_ERROR_MESSAGE("Vulkan OpenVR is not initialized");
        return false;
    }

    StateTransitionDesc barriers[2] = {};
    barriers[0].pResource           = m_ColorTextures[0];
    barriers[0].NewState            = RESOURCE_STATE_COPY_SOURCE;
    barriers[0].UpdateResourceState = true;
    barriers[1].pResource           = m_ColorTextures[1];
    barriers[1].NewState            = RESOURCE_STATE_COPY_SOURCE;
    barriers[1].UpdateResourceState = true;
    m_DeviceContext->TransitionResourceStates(_countof(barriers), barriers);

    m_DeviceContext->Flush();
    m_DeviceContext->FinishFrame();


    auto& ldesc = m_ColorTextures[0]->GetDesc();
    auto& rdesc = m_ColorTextures[1]->GetDesc();

    VRTextureBounds_t bounds;
    bounds.uMin = 0;
    bounds.uMax = 1.0f;
    bounds.vMin = 0;
    bounds.vMax = 1.0f;

    auto* queue = m_DeviceContext->LockCommandQueue();
    {
        VRVulkanTextureData_t vkData;
        vkData.m_nImage            = uint64_t(m_ColorTextures[0]->GetNativeHandle());
        vkData.m_pDevice           = m_RenderDevice->GetVkDevice();
        vkData.m_pPhysicalDevice   = m_RenderDevice->GetVkPhysicalDevice();
        vkData.m_pInstance         = m_RenderDevice->GetVkInstance();
        vkData.m_pQueue            = queue->GetVkQueue();
        vkData.m_nQueueFamilyIndex = queue->GetQueueFamilyIndex();
        vkData.m_nWidth            = ldesc.Width;
        vkData.m_nHeight           = ldesc.Height;
        vkData.m_nFormat           = ConvertFormat(ldesc.Format);
        vkData.m_nSampleCount      = ldesc.SampleCount;

        Texture_t texture = {&vkData, ETextureType_TextureType_Vulkan, EColorSpace_ColorSpace_Auto};

        auto err1 = m_vrCompositor->Submit(EVREye_Eye_Left, &texture, &bounds, EVRSubmitFlags_Submit_Default);

        vkData.m_nImage  = uint64_t(m_ColorTextures[1]->GetNativeHandle());
        vkData.m_nFormat = ConvertFormat(rdesc.Format);

        auto err2 = m_vrCompositor->Submit(EVREye_Eye_Right, &texture, &bounds, EVRSubmitFlags_Submit_Default);

        if (err1 != EVRCompositorError_VRCompositorError_None || err2 != EVRCompositorError_VRCompositorError_None)
            LOG_INFO_MESSAGE("OpenVR::Submit failed");
    }
    m_DeviceContext->UnlockCommandQueue();

    UpdateHMDMatrixPose();
    return true;
}

uint2 OpenVRDeviceVk::GetRenderTargetDimension() const noexcept
{
    if (!m_vrSystem)
    {
        LOG_ERROR_MESSAGE("OpenVR is not created");
        return uint2{0, 0};
    }

    uint2 result;
    m_vrSystem->GetRecommendedRenderTargetSize(&result.x, &result.y);
    return result;
}

bool OpenVRDeviceVk::GetRenderTargets(ITexture** ppLeft, ITexture** ppRight) noexcept
{
    if (m_ColorTextures[0] == nullptr || m_ColorTextures[1] == nullptr)
        return false;

    *ppLeft  = m_ColorTextures[0];
    *ppRight = m_ColorTextures[1];
    return true;
}

} // namespace DEVR
