#pragma once

#include "RefCntAutoPtr.hpp"
#include "../include/VulkanUtilities/VulkanHeaders.h"
#include "DeviceContextVk.h"
#include "RenderDeviceVk.h"
#include "TextureVk.h"

#include "IVRDevice.h"

#include "openvr_capi.h"

namespace DEVR
{

class OpenVRDevice : public IVRDevice
{
public:
    OpenVRDevice() {}
    ~OpenVRDevice() override;

    bool Create() noexcept override;
    bool BeginFrame() noexcept override;
    void SetupCamera(const float2& clipPlanes) noexcept override;

protected:
    void UpdateHMDMatrixPose() noexcept;

private:
    bool   LoadLib(const String& libPath) noexcept;
    void   ProcessHmdEvents(const VREvent_t& ev) noexcept;
    String GetTrackedDeviceString(TrackedDeviceIndex_t unDevice, TrackedDeviceProperty prop, TrackedPropertyError* peError = nullptr) const noexcept;

protected:
    using IVRSystemPtr       = intptr_t;
    using IVRSystemTable     = VR_IVRSystem_FnTable*;
    using IVRCompositorTable = VR_IVRCompositor_FnTable*;

    struct OpenVRLoader
    {
        using VR_InitInternal_t                       = intptr_t (*)(EVRInitError* peError, EVRApplicationType eType);
        using VR_ShutdownInternal_t                   = void (*)();
        using VR_IsHmdPresent_t                       = int (*)();
        using VR_GetGenericInterface_t                = intptr_t (*)(const char* pchInterfaceVersion, EVRInitError* peError);
        using VR_IsRuntimeInstalled_t                 = int (*)();
        using VR_GetVRInitErrorAsSymbol_t             = const char* (*)(EVRInitError error);
        using VR_GetVRInitErrorAsEnglishDescription_t = const char* (*)(EVRInitError error);

        VR_InitInternal_t                       InitInternal;
        VR_ShutdownInternal_t                   ShutdownInternal;
        VR_IsHmdPresent_t                       IsHmdPresent;
        VR_GetGenericInterface_t                GetGenericInterface;
        VR_IsRuntimeInstalled_t                 IsRuntimeInstalled;
        VR_GetVRInitErrorAsSymbol_t             GetVRInitErrorAsSymbol;
        VR_GetVRInitErrorAsEnglishDescription_t GetVRInitErrorAsEnglishDescription;
    };

    IVRSystemPtr        m_hmd          = {};
    IVRSystemTable      m_vrSystem     = nullptr;
    IVRCompositorTable  m_vrCompositor = nullptr;
    void*               m_openVRLib    = nullptr;
    OpenVRLoader        m_ovr;
    TrackedDevicePose_t m_trackedDevicePose[k_unMaxTrackedDeviceCount];
};


class OpenVRDeviceVk final : public OpenVRDevice
{
public:
    ~OpenVRDeviceVk() override {}

    bool  Initialize(IRenderDevice* device, IDeviceContext* context) noexcept override;
    bool  EndFrame() noexcept override;

    bool  GetRequirements(Requirements& req) const noexcept override;
    uint2 GetRenderTargetDimension() const noexcept override;
    bool  GetRenderTargets(ITexture** ppLeft, ITexture** ppRight) noexcept override;

private:
    RefCntAutoPtr<IRenderDeviceVk>  m_RenderDevice;
    RefCntAutoPtr<IDeviceContextVk> m_DeviceContext;
    RefCntAutoPtr<ITexture>         m_ColorTextures[2];
};

} // namespace DEVR
