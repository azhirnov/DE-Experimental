#pragma once

#include <vector>
#include <string>
#include <array>

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "BasicMath.hpp"

namespace DEVR
{
using namespace Diligent;

class IVRDevice
{
public:
    enum class EHmdStatus
    {
        PowerOff,
        Standby,
        Active,
        Mounted,
    };

    struct VRCamera
    {
        struct PerEye
        {
            float4x4 proj = float4x4::Identity();
            float4x4 view = float4x4::Identity();
        };
        PerEye   left;
        PerEye   right;
        float4x4 pose = float4x4::Identity(); // hmd rotation  // TODO: float3x3
        float3   position;                    // hmd position
        float2   clipPlanes;
        float3   velocity;
        float3   angularVelocity;
    };

    struct Requirements
    {
        std::vector<const char*> InstanceExtensions;
        std::vector<const char*> DeviceExtensions;
        std::string              InstanceExtensionsStr;
        std::string              DeviceExtensionsStr;
        Uint32                   AdapterId = DEFAULT_ADAPTER_ID;
    };

    virtual ~IVRDevice() {}

    virtual bool Create() noexcept                              = 0;
    virtual bool BeginFrame() noexcept                          = 0;
    virtual bool EndFrame() noexcept                            = 0;
    virtual void SetupCamera(const float2& clipPlanes) noexcept = 0;

    EHmdStatus      GetStatus() const noexcept { return m_hmdStatus; }
    VRCamera const& GetCamera() const noexcept { return m_camera; }
    TEXTURE_FORMAT  GetImageFormat() const noexcept { return m_RenderTargetFormat; }

    virtual bool  GetRequirements(Requirements& req) const noexcept                   = 0;
    virtual bool  Initialize(IRenderDevice* device, IDeviceContext* context) noexcept = 0;
    virtual uint2 GetRenderTargetDimension() const noexcept                           = 0;
    virtual bool  GetRenderTargets(ITexture** ppLeft, ITexture** ppRight) noexcept    = 0;

protected:
    VRCamera       m_camera;
    EHmdStatus     m_hmdStatus          = EHmdStatus::PowerOff;
    TEXTURE_FORMAT m_RenderTargetFormat = TEX_FORMAT_UNKNOWN;
};

} // namespace DEVR
