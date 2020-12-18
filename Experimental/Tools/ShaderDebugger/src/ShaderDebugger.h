#pragma once

#include <unordered_map>
#include <functional>

#include "EngineFactory.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "RefCntAutoPtr.hpp"
#include "BasicMath.hpp"
#include "SpvCompiler.h"

namespace DE
{
using namespace Diligent;

class SharedSRB;

enum class EShaderDebugMode : Uint32
{
    None         = 0,
    Trace        = 1 << 0,
    Profiling    = 1 << 1,
    ClockHeatmap = 1 << 2,
    Last         = ClockHeatmap,
    All          = Trace | Profiling | ClockHeatmap,
};


class ISharedSRB : public IObject
{
public:
    virtual void DILIGENT_CALL_TYPE BindAllVariables(SHADER_TYPE Stages, const char* pName, IDeviceObject* pObject) = 0;

    virtual void DILIGENT_CALL_TYPE BindAllVariables(SHADER_TYPE Stages, const char* pName, IDeviceObject* const* ppObjects, Uint32 FirstElement, Uint32 NumElements) = 0;
};


class ISharedSBT : public IObject
{
public:
    virtual void DILIGENT_CALL_TYPE ResetHitGroups() = 0;

    virtual void DILIGENT_CALL_TYPE BindRayGenShader(const char* pShaderGroupName,
                                                     const void* pData    = nullptr,
                                                     Uint32      DataSize = 0) = 0;

    virtual void DILIGENT_CALL_TYPE BindMissShader(const char* pShaderGroupName,
                                                   Uint32      MissIndex,
                                                   const void* pData    = nullptr,
                                                   Uint32      DataSize = 0) = 0;

    virtual void DILIGENT_CALL_TYPE BindHitGroup(ITopLevelAS* pTLAS,
                                                 const char*  pInstanceName,
                                                 const char*  pGeometryName,
                                                 Uint32       RayOffsetInHitGroupIndex,
                                                 const char*  pShaderGroupName,
                                                 const void*  pData    = nullptr,
                                                 Uint32       DataSize = 0) = 0;

    virtual void DILIGENT_CALL_TYPE BindHitGroupByIndex(Uint32      BindingIndex,
                                                        const char* pShaderGroupName,
                                                        const void* pData    = nullptr,
                                                        Uint32      DataSize = 0) = 0;

    virtual void DILIGENT_CALL_TYPE BindHitGroups(ITopLevelAS* pTLAS,
                                                  const char*  pInstanceName,
                                                  Uint32       RayOffsetInHitGroupIndex,
                                                  const char*  pShaderGroupName,
                                                  const void*  pData    = nullptr,
                                                  Uint32       DataSize = 0) = 0;

    virtual void DILIGENT_CALL_TYPE BindHitGroupForAll(ITopLevelAS* pTLAS,
                                                       Uint32       RayOffsetInHitGroupIndex,
                                                       const char*  pShaderGroupName,
                                                       const void*  pData    = nullptr,
                                                       Uint32       DataSize = 0) = 0;


    virtual void DILIGENT_CALL_TYPE BindCallableShader(const char* pShaderGroupName,
                                                       Uint32      CallableIndex,
                                                       const void* pData    = nullptr,
                                                       Uint32      DataSize = 0) = 0;
};

using ShaderDebugCallback_t = std::function<void(const std::vector<const char*>& output)>;


class ShaderDebugger
{
public:
    explicit ShaderDebugger(const char* CompilerLib);
    ~ShaderDebugger();

    bool Initialize(IEngineFactory* pFactory, IRenderDevice* pDevice) noexcept;
    bool InitDebugOutput(const char* Folder) noexcept;
    bool InitDebugOutput(ShaderDebugCallback_t&& CB) noexcept;

    void CompileFromSource(IShader** ppShader, SHADER_TYPE Type, const char* pSource, Uint32 SourceLen, const char* pName, const ShaderMacro* pMacro = nullptr, EShaderDebugMode Mode = EShaderDebugMode::None) noexcept;
    void CompileFromFile(IShader** ppShader, SHADER_TYPE Type, const char* pFilePath, const char* pName, const ShaderMacro* pMacro = nullptr, EShaderDebugMode Mode = EShaderDebugMode::None) noexcept;

    bool CreatePipeline(const GraphicsPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipeline) noexcept;
    bool CreatePipeline(const ComputePipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipeline) noexcept;
    bool CreatePipeline(const RayTracingPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipeline) noexcept;

    void CreateSRB(IPipelineState* pPipeline, ISharedSRB** ppSRB) noexcept;
    void BindSRB(IDeviceContext* pContext, IPipelineState* pPipeline, ISharedSRB* pSRB, RESOURCE_STATE_TRANSITION_MODE TransitionMode = RESOURCE_STATE_TRANSITION_MODE_NONE) noexcept;

    void CreateSBT(const ShaderBindingTableDesc& Desc, ISharedSBT** ppSBT) noexcept;
    void GetSBT(IPipelineState* pPipeline, ISharedSBT* pShared, IShaderBindingTable*& pActual) noexcept;

    bool BeginFragmentDebugger(IDeviceContext* pContext, IPipelineState*& pPipeline, uint2 FragCoord) noexcept;
    bool BeginFragmentProfiler(IDeviceContext* pContext, IPipelineState*& pPipeline, uint2 FragCoord) noexcept;

    bool BeginComputeDebugger(IDeviceContext* pContext, IPipelineState*& pPipeline, uint3 GlobalID) noexcept;
    bool BeginComputeProfiler(IDeviceContext* pContext, IPipelineState*& pPipeline, uint3 GlobalID) noexcept;

    bool BeginRayTraceDebugger(IDeviceContext* pContext, IPipelineState*& pPipeline, SHADER_TYPE Stages, uint3 LaunchID) noexcept;
    bool BeginRayTraceProfiler(IDeviceContext* pContext, IPipelineState*& pPipeline, SHADER_TYPE Stages, uint3 LaunchID) noexcept;

    bool BeginClockHeatmap(IDeviceContext* pContext, IPipelineState*& pPipeline, SHADER_TYPE Stages, uint2 dim) noexcept;
    bool EndClockHeatmap(IDeviceContext* pContext, ITextureView* pRTV) noexcept;
    bool EndClockHeatmap(IDeviceContext* pContext, ITexture* pRT) noexcept;

    bool BeginDebugger(IDeviceContext* pContext, IPipelineState*& pPipeline, SHADER_TYPE Stages) noexcept;
    bool BeginProfiler(IDeviceContext* pContext, IPipelineState*& pPipeline, SHADER_TYPE Stages) noexcept;
    void EndTrace(IDeviceContext* pContext) noexcept;
    void ReadTrace(IDeviceContext* pContext) noexcept;


private:
    struct ShaderDebugInfo
    {
        EShaderDebugMode Mode = EShaderDebugMode::None;

        RefCntAutoPtr<IShader> Origin;
        RefCntAutoPtr<IShader> ClockHeatmapShader;

        CompiledShader*        pDebugInfo = nullptr;
        RefCntAutoPtr<IShader> DebugShader;

        CompiledShader*        pProfiltInfo = nullptr;
        RefCntAutoPtr<IShader> ProfileShader;

        ShaderDebugInfo() {}
        ShaderDebugInfo(ShaderDebugInfo&&);
        ~ShaderDebugInfo();
    };

    struct PipelineKey
    {
        RefCntAutoPtr<IPipelineState> SrcPipeline;
        EShaderDebugMode              Mode   = EShaderDebugMode::None;
        SHADER_TYPE                   Stages = SHADER_TYPE_UNKNOWN;

        bool operator==(const PipelineKey& rhs) const
        {
            return SrcPipeline == rhs.SrcPipeline &&
                Mode == rhs.Mode &&
                Stages == rhs.Stages;
        }
    };

    struct PipelineKeyHash
    {
        size_t operator()(const PipelineKey& key) const
        {
            size_t h = std::hash<const void*>{}(key.SrcPipeline.RawPtr());
            HashCombine(h, key.Mode);
            HashCombine(h, key.Stages);
            return h;
        }
    };

    struct PipelineDebugInfo
    {
        RefCntAutoPtr<IPipelineState> DebugPipeline;
        std::vector<CompiledShader*>  DebugTraces;
    };

    struct DebugMode
    {
        std::vector<CompiledShader*> Traces;
        EShaderDebugMode             Mode = EShaderDebugMode::None;
        RefCntAutoPtr<IBuffer>       pStorage;
        RefCntAutoPtr<IBufferView>   pStorageView;
        RefCntAutoPtr<IBuffer>       pReadbackBuffer;
        IPipelineState*              pPSO = nullptr;
        uint2                        HeatmapDim;
    };

    struct DebugStorage
    {
        RefCntAutoPtr<IBuffer> pStorageBuffer;
        RefCntAutoPtr<IBuffer> pReadbackBuffer;
        Uint32                 Size     = 0;
        Uint32                 Capacity = 0;
    };

    static constexpr Uint32 DefaultBufferSize = 8u << 20;

    using DebugShaders_t       = std::unordered_map<const void*, ShaderDebugInfo>;
    using DebugPipelines_t     = std::unordered_map<PipelineKey, PipelineDebugInfo, PipelineKeyHash>;
    using Pipelines_t          = std::unordered_map<const void*, std::vector<RefCntAutoPtr<IPipelineState>>>;
    using DebugModes_t         = std::vector<DebugMode>;
    using StorageBuffers_t     = std::vector<DebugStorage>;
    using HeatmapPipelineMap_t = std::unordered_map<TEXTURE_FORMAT, RefCntAutoPtr<IPipelineState>>;

private:
    bool CreateShader(SHADER_TYPE           Type,
                      const char*           pSource,
                      Uint32                SourceLen,
                      const char*           pName,
                      EShaderDebugMode      Mode,
                      const ShaderMacro*    pMacro,
                      SPV_COMP_OPTIMIZATION OptMode,
                      CompiledShader**      ppDbgInfo,
                      IShader**             ppShader);

    bool AllocBuffer(IDeviceContext* pContext, DebugMode& Dbg, Uint32 Size);

    bool BeginDebugging(IDeviceContext* pContext, IPipelineState*& pPipeline, const uint4& Header, SHADER_TYPE Stages, EShaderDebugMode Mode);
    void ParseDebugOutput(IDeviceContext* pContext, DebugMode& DbgMode) const;

    void DefaultShaderDebugCallback(const char* Name, const std::vector<const char*>& Output) const;

    void CreateClockHeatmapPipelines() noexcept(false);
    void GetClockHeatmapPipeline(TEXTURE_FORMAT Format, IPipelineState*& pPipeline) noexcept;

private:
    RefCntAutoPtr<IEngineFactory> m_pEngineFactory;
    RefCntAutoPtr<IRenderDevice>  m_pRenderDevice;

    DebugShaders_t   m_DbgShaders;
    DebugPipelines_t m_DbgPipelines;
    Pipelines_t      m_Pipelines;

    String                m_OutputFolder;
    ShaderDebugCallback_t m_Callback;
    RefCntAutoPtr<IFence> m_pFence;
    Uint64                m_FenceValue = 0;
    StorageBuffers_t      m_StorageBuffers;
    DebugModes_t          m_DbgModes;
    Uint32                m_BufferAlign = 256; // min align for storage buffer

    RefCntAutoPtr<IPipelineState>         m_pHeatmapPass1;
    RefCntAutoPtr<IPipelineState>         m_pHeatmapPass2;
    HeatmapPipelineMap_t                  m_pHeatmapPass3;
    RefCntAutoPtr<IShaderResourceBinding> m_pHeatSRB1;
    RefCntAutoPtr<IShaderResourceBinding> m_pHeatSRB2;
    RefCntAutoPtr<IShaderResourceBinding> m_pHeatSRB3;
    static constexpr Uint32               m_HeatmapPass1LocalSize = 32;

    void*            m_pSpvCompilerLib;
    SpvCompilerFn    m_CompilerFn  = {};
    SPV_COMP_VERSION m_CompilerVer = SPV_COMP_VERSION_VULKAN_1_0;
};



inline EShaderDebugMode operator|(EShaderDebugMode lhs, EShaderDebugMode rhs)
{
    return EShaderDebugMode(Uint32(lhs) | Uint32(rhs));
}

inline EShaderDebugMode operator&(EShaderDebugMode lhs, EShaderDebugMode rhs)
{
    return EShaderDebugMode(Uint32(lhs) & Uint32(rhs));
}

inline bool operator!(EShaderDebugMode value)
{
    return !Uint32(value);
}

} // namespace DE
