#include "ShaderDebugger.h"

#include "DataBlobImpl.hpp"
#include "FileSystem.hpp"
#include "Align.hpp"

#include "../include/VulkanUtilities/VulkanHeaders.h"
#include "RenderDeviceVk.h"
#include "../include/RenderDeviceVkImpl.hpp"

#include "spirv-tools/optimizer.hpp"
#include "spirv-tools/libspirv.h"

#include <Windows.h>
#undef CreateDirectory

namespace DE
{
namespace
{
static constexpr SHADER_TYPE AllShaderStages  = SHADER_TYPE((SHADER_TYPE_LAST << 1) - 1);
static constexpr SHADER_TYPE RayTracingStages = SHADER_TYPE_RAY_GEN | SHADER_TYPE_RAY_MISS | SHADER_TYPE_RAY_CLOSEST_HIT | SHADER_TYPE_RAY_ANY_HIT | SHADER_TYPE_RAY_INTERSECTION | SHADER_TYPE_CALLABLE;

static constexpr const char DbgStorageName[] = "dbg_ShaderTraceStorage";
} // namespace



class SharedSRB final : public RefCountedObject<ISharedSRB>
{
public:
    SharedSRB(IReferenceCounters* pRefCounters, const std::vector<RefCntAutoPtr<IPipelineState>>& Pipelines);

    void DILIGENT_CALL_TYPE BindAllVariables(SHADER_TYPE Stages, const char* pName, IDeviceObject* pObject) override;
    void DILIGENT_CALL_TYPE BindAllVariables(SHADER_TYPE Stages, const char* pName, IDeviceObject* const* ppObjects, Uint32 FirstElement, Uint32 NumElements) override;

    void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override;

    IShaderResourceBinding* GetSRB(IPipelineState* pPSO) noexcept;

private:
    struct Hasher
    {
        size_t operator()(const RefCntAutoPtr<IPipelineState>& ptr) const { return std::hash<const void*>{}(ptr.RawPtr()); }
    };
    std::unordered_map<RefCntAutoPtr<IPipelineState>, RefCntAutoPtr<IShaderResourceBinding>, Hasher> m_SRBs;
};

SharedSRB::SharedSRB(IReferenceCounters* pRefCounters, const std::vector<RefCntAutoPtr<IPipelineState>>& Pipelines) :
    RefCountedObject<ISharedSRB>{pRefCounters}
{
    for (auto& ppln : Pipelines)
    {
        RefCntAutoPtr<IShaderResourceBinding> pSRB;
        const_cast<IPipelineState*>(ppln.RawPtr())->CreateShaderResourceBinding(&pSRB, true);
        m_SRBs.emplace(ppln, pSRB);
    }
}

void SharedSRB::BindAllVariables(SHADER_TYPE Stages, const char* pName, IDeviceObject* pObject)
{
    VERIFY_EXPR(pObject != nullptr);

    while (Stages != SHADER_TYPE_UNKNOWN)
    {
        SHADER_TYPE Stage = Stages & SHADER_TYPE(~(Stages - 1));
        Stages            = Stages & ~Stage;

        for (auto& Pair : m_SRBs)
        {
            if (auto* pVar = Pair.second->GetVariableByName(Stage, pName))
                pVar->Set(pObject);
        }
    }
}

void SharedSRB::BindAllVariables(SHADER_TYPE Stages, const char* pName, IDeviceObject* const* ppObjects, Uint32 FirstElement, Uint32 NumElements)
{
    for (Uint32 i = 0; i < NumElements; ++i)
    {
        VERIFY_EXPR(ppObjects[i] != nullptr);
    }

    while (Stages != SHADER_TYPE_UNKNOWN)
    {
        SHADER_TYPE Stage = Stages & SHADER_TYPE(~(Stages - 1));
        Stages            = Stages & ~Stage;

        for (auto& Pair : m_SRBs)
        {
            if (auto* pVar = Pair.second->GetVariableByName(Stage, pName))
                pVar->SetArray(ppObjects, FirstElement, NumElements);
        }
    }
}

void SharedSRB::QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface)
{
}

IShaderResourceBinding* SharedSRB::GetSRB(IPipelineState* pPSO) noexcept
{
    auto Iter = m_SRBs.find(RefCntAutoPtr<IPipelineState>{pPSO});
    if (Iter != m_SRBs.end())
        return Iter->second;
    else
        return nullptr;
}
//-----------------------------------------------------------------------------




class SharedSBT final : public RefCountedObject<ISharedSBT>
{
public:
    SharedSBT(IReferenceCounters* pRefCounters, IRenderDevice* pDevice, const char* Name, const std::vector<RefCntAutoPtr<IPipelineState>>& Pipelines) :
        RefCountedObject<ISharedSBT>{pRefCounters}
    {
        for (auto& ppln : Pipelines)
        {
            RefCntAutoPtr<IShaderBindingTable> pSBT;
            ShaderBindingTableDesc             Desc;
            Desc.Name = Name;
            Desc.pPSO = const_cast<IPipelineState*>(ppln.RawPtr());
            pDevice->CreateSBT(Desc, &pSBT);
            m_SBTs.emplace(ppln, pSBT);
        }
    }

    void GetSBT(IPipelineState* pPSO, IShaderBindingTable*& pActual)
    {
        auto Iter = m_SBTs.find(RefCntAutoPtr<IPipelineState>{pPSO});
        if (Iter != m_SBTs.end())
        {
            pActual = Iter->second;
        }
        else
            pActual = nullptr;
    }

    void DILIGENT_CALL_TYPE QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface) override
    {}

    void DILIGENT_CALL_TYPE ResetHitGroups() override
    {
        for (auto& SBT : m_SBTs)
        {
            SBT.second->ResetHitGroups();
        }
    }

    void DILIGENT_CALL_TYPE BindRayGenShader(const char* pShaderGroupName,
                                             const void* pData,
                                             Uint32      DataSize) override
    {
        for (auto& SBT : m_SBTs)
        {
            SBT.second->BindRayGenShader(pShaderGroupName, pData, DataSize);
        }
    }

    void DILIGENT_CALL_TYPE BindMissShader(const char* pShaderGroupName,
                                           Uint32      MissIndex,
                                           const void* pData,
                                           Uint32      DataSize) override
    {
        for (auto& SBT : m_SBTs)
        {
            SBT.second->BindMissShader(pShaderGroupName, MissIndex, pData, DataSize);
        }
    }

    void DILIGENT_CALL_TYPE BindHitGroup(ITopLevelAS* pTLAS,
                                         const char*  pInstanceName,
                                         const char*  pGeometryName,
                                         Uint32       RayOffsetInHitGroupIndex,
                                         const char*  pShaderGroupName,
                                         const void*  pData,
                                         Uint32       DataSize) override
    {
        for (auto& SBT : m_SBTs)
        {
            SBT.second->BindHitGroup(pTLAS, pInstanceName, pGeometryName, RayOffsetInHitGroupIndex, pShaderGroupName, pData, DataSize);
        }
    }

    void DILIGENT_CALL_TYPE BindHitGroupByIndex(Uint32      BindingIndex,
                                                const char* pShaderGroupName,
                                                const void* pData,
                                                Uint32      DataSize) override
    {
        for (auto& SBT : m_SBTs)
        {
            SBT.second->BindHitGroupByIndex(BindingIndex, pShaderGroupName, pData, DataSize);
        }
    }

    void DILIGENT_CALL_TYPE BindHitGroups(ITopLevelAS* pTLAS,
                                          const char*  pInstanceName,
                                          Uint32       RayOffsetInHitGroupIndex,
                                          const char*  pShaderGroupName,
                                          const void*  pData,
                                          Uint32       DataSize) override
    {
        for (auto& SBT : m_SBTs)
        {
            SBT.second->BindHitGroups(pTLAS, pInstanceName, RayOffsetInHitGroupIndex, pShaderGroupName, pData, DataSize);
        }
    }

    void DILIGENT_CALL_TYPE BindHitGroupForAll(ITopLevelAS* pTLAS,
                                               Uint32       RayOffsetInHitGroupIndex,
                                               const char*  pShaderGroupName,
                                               const void*  pData,
                                               Uint32       DataSize) override
    {
        for (auto& SBT : m_SBTs)
        {
            SBT.second->BindHitGroupForAll(pTLAS, RayOffsetInHitGroupIndex, pShaderGroupName, pData, DataSize);
        }
    }

    void DILIGENT_CALL_TYPE BindCallableShader(const char* pShaderGroupName,
                                               Uint32      CallableIndex,
                                               const void* pData,
                                               Uint32      DataSize) override
    {
        for (auto& SBT : m_SBTs)
        {
            SBT.second->BindCallableShader(pShaderGroupName, CallableIndex, pData, DataSize);
        }
    }

private:
    struct Hasher
    {
        size_t operator()(const RefCntAutoPtr<IPipelineState>& ptr) const { return std::hash<const void*>{}(ptr.RawPtr()); }
    };
    std::unordered_map<RefCntAutoPtr<IPipelineState>, RefCntAutoPtr<IShaderBindingTable>, Hasher> m_SBTs;
};
//-----------------------------------------------------------------------------



ShaderDebugger::ShaderDebugInfo::ShaderDebugInfo(ShaderDebugInfo&& other) :
    Mode{other.Mode},
    Name{std::move(other.Name)},
    Origin{std::move(other.Origin)},
    ClockHeatmapShader{std::move(other.ClockHeatmapShader)},
    pDebugInfo{other.pDebugInfo},
    DebugShader{std::move(other.DebugShader)},
    ProfileShader{std::move(other.ProfileShader)}
{
    other.pDebugInfo   = nullptr;
    other.pProfiltInfo = nullptr;
}

ShaderDebugger::ShaderDebugInfo::~ShaderDebugInfo()
{
    VERIFY_EXPR(pDebugInfo == nullptr);
    VERIFY_EXPR(pProfiltInfo == nullptr);
}
//-----------------------------------------------------------------------------


ShaderDebugger::ShaderDebugger(const char* CompilerLib)
{
    if (CompilerLib == nullptr)
        CompilerLib = "SpvCompiler.dll";

    m_pSpvCompilerLib = ::LoadLibraryA(CompilerLib);
    if (m_pSpvCompilerLib)
    {
        auto* pGetSpvCompilerFn = reinterpret_cast<decltype(GetSpvCompilerFn)*>(::GetProcAddress(HMODULE(m_pSpvCompilerLib), "GetSpvCompilerFn"));
        if (pGetSpvCompilerFn)
        {
            pGetSpvCompilerFn(&m_CompilerFn);
        }
        else
        {
            ::FreeLibrary(HMODULE(m_pSpvCompilerLib));
            m_pSpvCompilerLib = nullptr;
        }
    }
}

ShaderDebugger::~ShaderDebugger()
{
    if (m_pSpvCompilerLib)
    {
        for (auto& Sh : m_DbgShaders)
        {
            if (Sh.second.pDebugInfo)
                m_CompilerFn.ReleaseShader(Sh.second.pDebugInfo);
            if (Sh.second.pProfiltInfo)
                m_CompilerFn.ReleaseShader(Sh.second.pProfiltInfo);

            Sh.second.pDebugInfo   = nullptr;
            Sh.second.pProfiltInfo = nullptr;
        }

        ::FreeLibrary(HMODULE(m_pSpvCompilerLib));
    }
}

bool ShaderDebugger::Initialize(IEngineFactory* pFactory, IRenderDevice* pDevice) noexcept
{
    if (pFactory == nullptr || pDevice == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to initialize shader debugger: invalid arguments");
        return false;
    }

    if (!pDevice->GetDeviceCaps().IsVulkanDevice())
    {
        LOG_ERROR_MESSAGE("Failed to initialize shader debugger: required Vulkan device");
        return false;
    }

    RefCntAutoPtr<IRenderDeviceVk> pRenderDeviceVk;
    pDevice->QueryInterface(IID_RenderDeviceVk, reinterpret_cast<IObject**>(static_cast<IRenderDeviceVk**>(&pRenderDeviceVk)));

    if (pRenderDeviceVk == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to get IRenderDeviceVk");
        return false;
    }

    const auto& Inst = pRenderDeviceVk.RawPtr<RenderDeviceVkImpl>()->GetVulkanInstance();
    const auto& Dev  = pRenderDeviceVk.RawPtr<RenderDeviceVkImpl>()->GetLogicalDevice();
    switch (Inst->GetVkVersion())
    {
        case VK_API_VERSION_1_0: m_CompilerVer = SPV_COMP_VERSION_VULKAN_1_0; break;
        case VK_API_VERSION_1_1: m_CompilerVer = Dev.GetEnabledExtFeatures().Spirv14 ? SPV_COMP_VERSION_VULKAN_1_1_SPIRV_1_4 : SPV_COMP_VERSION_VULKAN_1_1; break;
        case VK_API_VERSION_1_2: m_CompilerVer = SPV_COMP_VERSION_VULKAN_1_2; break;
        default: UNEXPECTED("unknown vulkan version");
    }

    // create fence
    {
        FenceDesc Desc;
        Desc.Name = "Debug storage sync";
        pDevice->CreateFence(Desc, &m_pFence);
    }

    m_pEngineFactory = pFactory;
    m_pRenderDevice  = pDevice;
    return true;
}

bool ShaderDebugger::InitDebugOutput(const char* Folder) noexcept
{
    if (m_pEngineFactory == nullptr || m_pRenderDevice == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to initialize debug output");
        return false;
    }

    try
    {
        CreateClockHeatmapPipelines();
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to compile pipelines for clock heatmap");
        return false;
    }

    if (FileSystem::IsDirectory(Folder))
    {
        FileSystem::ClearDirectory(Folder, true);
    }
    else
    {
        if (!FileSystem::CreateDirectory(Folder))
        {
            LOG_ERROR_MESSAGE("Failed to create directory '", Folder, "' for shader debug output");
            return false;
        }
    }

    m_OutputFolder = Folder;
    m_Callback     = [this](auto* name, auto& output) {
        DefaultShaderDebugCallback(name, output);
    };
    return true;
}

bool ShaderDebugger::InitDebugOutput(ShaderDebugCallback_t&& CB) noexcept
{
    if (m_pEngineFactory == nullptr || m_pRenderDevice == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to initialize debug output");
        return false;
    }

    try
    {
        CreateClockHeatmapPipelines();
    }
    catch (...)
    {
        LOG_ERROR_MESSAGE("Failed to compile pipelines for clock heatmap");
        return false;
    }

    m_Callback = std::move(CB);
    return true;
}

void ShaderDebugger::CompileFromFile(IShader** ppShader, SHADER_TYPE Type, const char* pFilePath, const char* pName, const ShaderMacro* pMacro, EShaderDebugMode Mode) noexcept
{
    if (!m_pRenderDevice)
    {
        LOG_ERROR_MESSAGE("Shader debugger is not initialized");
        return;
    }

    RefCntAutoPtr<IShaderSourceInputStreamFactory> pShaderSourceFactory;
    m_pEngineFactory->CreateDefaultShaderSourceStreamFactory(nullptr, &pShaderSourceFactory);

    RefCntAutoPtr<IFileStream> pSourceStream;
    pShaderSourceFactory->CreateInputStream(pFilePath, &pSourceStream);

    if (pSourceStream == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to load shader source file '", pFilePath, '\'');
        return;
    }

    RefCntAutoPtr<DataBlobImpl> pFileData{MakeNewRCObj<DataBlobImpl>{}(0)};
    pSourceStream->ReadBlob(pFileData);

    if (pName == nullptr)
        pName = pFilePath;

    return CompileFromSource(ppShader, Type, static_cast<char*>(pFileData->GetDataPtr()), Uint32(pFileData->GetSize()), pName, pMacro, Mode);
}

void ShaderDebugger::CreateSRB(IPipelineState* pPipeline, ISharedSRB** ppSRB) noexcept
{
    std::vector<RefCntAutoPtr<IPipelineState>> Pipelines;

    auto Iter = m_Pipelines.find(static_cast<const void*>(pPipeline));
    if (Iter != m_Pipelines.end())
        Pipelines = Iter->second;

    Pipelines.insert(Pipelines.begin(), RefCntAutoPtr<IPipelineState>{pPipeline});

    RefCntAutoPtr<ISharedSRB> SRB{MakeNewRCObj<SharedSRB>{}(Pipelines)};
    *ppSRB = SRB.Detach();
}

void ShaderDebugger::BindSRB(IDeviceContext* pContext, IPipelineState* pPipeline, ISharedSRB* pSRB, RESOURCE_STATE_TRANSITION_MODE TransitionMode) noexcept
{
    if (pContext == nullptr || pPipeline == nullptr || pSRB == nullptr)
        return;

    pContext->SetPipelineState(pPipeline);

    auto* pActualSRB = static_cast<SharedSRB*>(pSRB)->GetSRB(pPipeline);

    if (m_DbgModes.size() && m_DbgModes.back().pPSO == pPipeline)
    {
        auto&       DbgMode = m_DbgModes.back();
        SHADER_TYPE Stages  = AllShaderStages;
        bool        WasSet  = false;

        while (Stages != SHADER_TYPE_UNKNOWN)
        {
            SHADER_TYPE Stage = Stages & SHADER_TYPE(~(Stages - 1));
            Stages            = Stages & ~Stage;

            if (auto* pVar = pActualSRB->GetVariableByName(Stage, DbgStorageName))
            {
                pVar->Set(DbgMode.pStorageView);
                WasSet = true;
            }
        }
        VERIFY_EXPR(WasSet);
    }

    pContext->CommitShaderResources(pActualSRB, TransitionMode);
}

void ShaderDebugger::CreateSBT(const ShaderBindingTableDesc& Desc, ISharedSBT** ppSBT) noexcept
{
    std::vector<RefCntAutoPtr<IPipelineState>> Pipelines;

    auto Iter = m_Pipelines.find(static_cast<const void*>(Desc.pPSO));
    if (Iter != m_Pipelines.end())
        Pipelines = Iter->second;

    Pipelines.insert(Pipelines.begin(), RefCntAutoPtr<IPipelineState>{Desc.pPSO});

    RefCntAutoPtr<ISharedSBT> SBT{MakeNewRCObj<SharedSBT>{}(m_pRenderDevice, Desc.Name, Pipelines)};
    *ppSBT = SBT.Detach();
}

void ShaderDebugger::GetSBT(IPipelineState* pPipeline, ISharedSBT* pShared, IShaderBindingTable*& pActual) noexcept
{
    if (pPipeline == nullptr || pShared == nullptr)
        return;

    static_cast<SharedSBT*>(pShared)->GetSBT(pPipeline, pActual);
}

bool ShaderDebugger::CreatePipeline(const GraphicsPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipeline) noexcept
{
    VERIFY_EXPR(PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType != SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

    IPipelineState* pPipeline = nullptr;
    m_pRenderDevice->CreateGraphicsPipelineState(PSOCreateInfo, &pPipeline);
    if (pPipeline == nullptr)
        return false;

    *ppPipeline = pPipeline;

    GraphicsPipelineStateCreateInfo CreateInfo = PSOCreateInfo;

    std::vector<ShaderResourceVariableDesc> Variables;
    Variables.assign(PSOCreateInfo.PSODesc.ResourceLayout.Variables,
                     PSOCreateInfo.PSODesc.ResourceLayout.Variables + PSOCreateInfo.PSODesc.ResourceLayout.NumVariables);
    Variables.emplace_back(AllShaderStages, DbgStorageName, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

    CreateInfo.PSODesc.ResourceLayout.NumVariables = Uint32(Variables.size());
    CreateInfo.PSODesc.ResourceLayout.Variables    = Variables.data();

    std::vector<ShaderInfo> traces;
    EShaderDebugMode        Mode          = EShaderDebugMode(1);
    bool                    Changed       = false;
    const auto              ReplaceShader = [this, &traces, &Changed, &Mode](IShader*& pShader) //
    {
        auto Iter = m_DbgShaders.find(static_cast<const void*>(pShader));
        if (Iter == m_DbgShaders.end())
            return;

        if (!(Iter->second.Mode & Mode))
            return;

        switch (Mode)
        {
            // clang-format off
            case EShaderDebugMode::Trace:        pShader = Iter->second.DebugShader;        traces.emplace_back(Iter->second.Name.c_str(), Iter->second.pDebugInfo);   break;
            case EShaderDebugMode::Profiling:    pShader = Iter->second.ProfileShader;      traces.emplace_back(Iter->second.Name.c_str(), Iter->second.pProfiltInfo); break;
            case EShaderDebugMode::ClockHeatmap: pShader = Iter->second.ClockHeatmapShader; break;
                // clang-format on
        }
        Changed = true;
    };

    for (; Mode <= EShaderDebugMode::Last; Mode = EShaderDebugMode(Uint32(Mode) << 1))
    {
        for (SHADER_TYPE Stage = SHADER_TYPE_VERTEX; Stage <= SHADER_TYPE_MESH; Stage = SHADER_TYPE(Stage << 1))
        {
            traces.clear();
            Changed = false;

            switch (Stage)
            {
                // clang-format off
                case SHADER_TYPE_VERTEX:        CreateInfo.pVS = PSOCreateInfo.pVS;  ReplaceShader(CreateInfo.pVS); break;
                case SHADER_TYPE_PIXEL:         CreateInfo.pPS = PSOCreateInfo.pPS;  ReplaceShader(CreateInfo.pPS); break;
                case SHADER_TYPE_GEOMETRY:      CreateInfo.pGS = PSOCreateInfo.pGS;  ReplaceShader(CreateInfo.pGS); break;
                case SHADER_TYPE_HULL:          CreateInfo.pHS = PSOCreateInfo.pHS;  ReplaceShader(CreateInfo.pHS); break;
                case SHADER_TYPE_DOMAIN:        CreateInfo.pDS = PSOCreateInfo.pDS;  ReplaceShader(CreateInfo.pDS); break;
                case SHADER_TYPE_AMPLIFICATION: CreateInfo.pAS = PSOCreateInfo.pAS;  ReplaceShader(CreateInfo.pAS); break;
                case SHADER_TYPE_MESH:          CreateInfo.pMS = PSOCreateInfo.pMS;  ReplaceShader(CreateInfo.pMS); break;
                    // clang-format on
            }

            if (Changed)
            {
                Variables.back().ShaderStages = Stage;

                RefCntAutoPtr<IPipelineState> pDbgPipeline;
                m_pRenderDevice->CreateGraphicsPipelineState(CreateInfo, &pDbgPipeline);

                if (pDbgPipeline)
                {
                    PipelineKey key;
                    key.SrcPipeline = pPipeline;
                    key.Mode        = Mode;
                    key.Stages      = Stage;

                    PipelineDebugInfo info;
                    info.DebugPipeline = pDbgPipeline;
                    info.DebugTraces   = traces;

                    auto  iter  = m_DbgPipelines.emplace(std::move(key), std::move(info)).first;
                    auto& pplns = m_Pipelines[static_cast<const void*>(pPipeline)];
                    pplns.push_back(pDbgPipeline);
                }
            }
        }
    }
    return true;
}

bool ShaderDebugger::CreatePipeline(const ComputePipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipeline) noexcept
{
    VERIFY_EXPR(PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType != SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

    IPipelineState* pPipeline = nullptr;
    m_pRenderDevice->CreateComputePipelineState(PSOCreateInfo, &pPipeline);
    if (pPipeline == nullptr)
        return false;

    *ppPipeline = pPipeline;

    ComputePipelineStateCreateInfo CreateInfo = PSOCreateInfo;

    std::vector<ShaderResourceVariableDesc> Variables;
    Variables.assign(PSOCreateInfo.PSODesc.ResourceLayout.Variables,
                     PSOCreateInfo.PSODesc.ResourceLayout.Variables + PSOCreateInfo.PSODesc.ResourceLayout.NumVariables);
    Variables.emplace_back(SHADER_TYPE_COMPUTE, DbgStorageName, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

    CreateInfo.PSODesc.ResourceLayout.NumVariables = Uint32(Variables.size());
    CreateInfo.PSODesc.ResourceLayout.Variables    = Variables.data();

    std::vector<ShaderInfo> traces;

    for (EShaderDebugMode Mode = EShaderDebugMode(1); Mode <= EShaderDebugMode::Last; Mode = EShaderDebugMode(Uint32(Mode) << 1))
    {
        traces.clear();

        auto Iter = m_DbgShaders.find(static_cast<const void*>(PSOCreateInfo.pCS));
        if (Iter == m_DbgShaders.end())
            continue;

        if (!(Iter->second.Mode & Mode))
            continue;

        switch (Mode)
        {
            // clang-format off
            case EShaderDebugMode::Trace:        CreateInfo.pCS = Iter->second.DebugShader;        traces.emplace_back(Iter->second.Name.c_str(), Iter->second.pDebugInfo);   break;
            case EShaderDebugMode::Profiling:    CreateInfo.pCS = Iter->second.ProfileShader;      traces.emplace_back(Iter->second.Name.c_str(), Iter->second.pProfiltInfo); break;
            case EShaderDebugMode::ClockHeatmap: CreateInfo.pCS = Iter->second.ClockHeatmapShader; break;
                // clang-format on
        }

        RefCntAutoPtr<IPipelineState> pDbgPipeline;
        m_pRenderDevice->CreateComputePipelineState(CreateInfo, &pDbgPipeline);

        if (pDbgPipeline)
        {
            PipelineKey key;
            key.SrcPipeline = pPipeline;
            key.Mode        = Mode;
            key.Stages      = SHADER_TYPE_COMPUTE;

            PipelineDebugInfo info;
            info.DebugPipeline = pDbgPipeline;
            info.DebugTraces   = traces;

            auto  iter  = m_DbgPipelines.emplace(std::move(key), std::move(info)).first;
            auto& pplns = m_Pipelines[static_cast<const void*>(pPipeline)];
            pplns.push_back(pDbgPipeline);
        }
    }
    return true;
}

bool ShaderDebugger::CreatePipeline(const RayTracingPipelineStateCreateInfo& PSOCreateInfo, IPipelineState** ppPipeline) noexcept
{
    VERIFY_EXPR(PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType != SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

    IPipelineState* pPipeline = nullptr;
    m_pRenderDevice->CreateRayTracingPipelineState(PSOCreateInfo, &pPipeline);
    if (pPipeline == nullptr)
        return false;

    *ppPipeline = pPipeline;

    RayTracingPipelineStateCreateInfo CreateInfo = PSOCreateInfo;

    std::vector<ShaderResourceVariableDesc> Variables;
    Variables.assign(PSOCreateInfo.PSODesc.ResourceLayout.Variables,
                     PSOCreateInfo.PSODesc.ResourceLayout.Variables + PSOCreateInfo.PSODesc.ResourceLayout.NumVariables);
    Variables.emplace_back(AllShaderStages, DbgStorageName, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);

    CreateInfo.PSODesc.ResourceLayout.NumVariables = Uint32(Variables.size());
    CreateInfo.PSODesc.ResourceLayout.Variables    = Variables.data();

    std::vector<ShaderInfo> traces;
    EShaderDebugMode        Mode          = EShaderDebugMode(1);
    bool                    Changed       = false;
    const auto              ReplaceShader = [this, &traces, &Changed, &Mode](IShader*& pShader) //
    {
        auto Iter = m_DbgShaders.find(static_cast<const void*>(pShader));
        if (Iter == m_DbgShaders.end())
            return;

        if (!(Iter->second.Mode & Mode))
            return;

        switch (Mode)
        {
            // clang-format off
            case EShaderDebugMode::Trace:        pShader = Iter->second.DebugShader;        traces.emplace_back(Iter->second.Name.c_str(), Iter->second.pDebugInfo);   break;
            case EShaderDebugMode::Profiling:    pShader = Iter->second.ProfileShader;      traces.emplace_back(Iter->second.Name.c_str(), Iter->second.pProfiltInfo); break;
            case EShaderDebugMode::ClockHeatmap: pShader = Iter->second.ClockHeatmapShader; break;
                // clang-format on
        }
        Changed = true;
    };

    std::vector<RayTracingGeneralShaderGroup>       GeneralShaders;
    std::vector<RayTracingTriangleHitShaderGroup>   TriangleHitShaders;
    std::vector<RayTracingProceduralHitShaderGroup> ProceduralHitShaders;

    for (; Mode <= EShaderDebugMode::Last; Mode = EShaderDebugMode(Uint32(Mode) << 1))
    {
        for (SHADER_TYPE Stage = SHADER_TYPE_RAY_GEN; Stage <= SHADER_TYPE_CALLABLE; Stage = SHADER_TYPE(Stage << 1))
        {
            traces.clear();
            Changed = false;

            GeneralShaders.clear();
            if (PSOCreateInfo.pGeneralShaders)
                GeneralShaders.assign(PSOCreateInfo.pGeneralShaders, PSOCreateInfo.pGeneralShaders + PSOCreateInfo.GeneralShaderCount);

            TriangleHitShaders.clear();
            if (PSOCreateInfo.pTriangleHitShaders)
                TriangleHitShaders.assign(PSOCreateInfo.pTriangleHitShaders, PSOCreateInfo.pTriangleHitShaders + PSOCreateInfo.TriangleHitShaderCount);

            ProceduralHitShaders.clear();
            if (PSOCreateInfo.pProceduralHitShaders)
                ProceduralHitShaders.assign(PSOCreateInfo.pProceduralHitShaders, PSOCreateInfo.pProceduralHitShaders + PSOCreateInfo.ProceduralHitShaderCount);

            for (auto& Sh : GeneralShaders)
            {
                if (Sh.pShader && Sh.pShader->GetDesc().ShaderType == Stage)
                    ReplaceShader(Sh.pShader);
            }
            for (auto& Sh : TriangleHitShaders)
            {
                if (Sh.pClosestHitShader && Sh.pClosestHitShader->GetDesc().ShaderType == Stage)
                    ReplaceShader(Sh.pClosestHitShader);
                if (Sh.pAnyHitShader && Sh.pAnyHitShader->GetDesc().ShaderType == Stage)
                    ReplaceShader(Sh.pAnyHitShader);
            }
            for (auto& Sh : ProceduralHitShaders)
            {
                if (Sh.pIntersectionShader && Sh.pIntersectionShader->GetDesc().ShaderType == Stage)
                    ReplaceShader(Sh.pIntersectionShader);
                if (Sh.pClosestHitShader && Sh.pClosestHitShader->GetDesc().ShaderType == Stage)
                    ReplaceShader(Sh.pClosestHitShader);
                if (Sh.pAnyHitShader && Sh.pAnyHitShader->GetDesc().ShaderType == Stage)
                    ReplaceShader(Sh.pAnyHitShader);
            }

            if (Changed)
            {
                CreateInfo.pGeneralShaders          = GeneralShaders.size() ? GeneralShaders.data() : nullptr;
                CreateInfo.GeneralShaderCount       = Uint32(GeneralShaders.size());
                CreateInfo.pTriangleHitShaders      = TriangleHitShaders.size() ? TriangleHitShaders.data() : nullptr;
                CreateInfo.TriangleHitShaderCount   = Uint32(TriangleHitShaders.size());
                CreateInfo.pProceduralHitShaders    = ProceduralHitShaders.size() ? ProceduralHitShaders.data() : nullptr;
                CreateInfo.ProceduralHitShaderCount = Uint32(ProceduralHitShaders.size());

                RefCntAutoPtr<IPipelineState> pDbgPipeline;
                m_pRenderDevice->CreateRayTracingPipelineState(CreateInfo, &pDbgPipeline);

                if (pDbgPipeline)
                {
                    PipelineKey key;
                    key.SrcPipeline = pPipeline;
                    key.Mode        = Mode;
                    key.Stages      = Stage;

                    PipelineDebugInfo info;
                    info.DebugPipeline = pDbgPipeline;
                    info.DebugTraces   = traces;

                    auto  iter  = m_DbgPipelines.emplace(std::move(key), std::move(info)).first;
                    auto& pplns = m_Pipelines[static_cast<const void*>(pPipeline)];
                    pplns.push_back(pDbgPipeline);
                }
            }
        }
    }
    return true;
}

bool ShaderDebugger::BeginFragmentDebugger(IDeviceContext* pContext, IPipelineState*& pPipeline, uint2 FragCoord) noexcept
{
    return BeginDebugging(pContext, pPipeline, uint4{FragCoord.x, FragCoord.y, 0, 0}, SHADER_TYPE_PIXEL, EShaderDebugMode::Trace);
}

bool ShaderDebugger::BeginFragmentProfiler(IDeviceContext* pContext, IPipelineState*& pPipeline, uint2 FragCoord) noexcept
{
    return BeginDebugging(pContext, pPipeline, uint4{FragCoord.x, FragCoord.y, 0, 0}, SHADER_TYPE_PIXEL, EShaderDebugMode::Profiling);
}

bool ShaderDebugger::BeginComputeDebugger(IDeviceContext* pContext, IPipelineState*& pPipeline, uint3 GlobalID) noexcept
{
    return BeginDebugging(pContext, pPipeline, uint4{GlobalID.x, GlobalID.y, GlobalID.z, 0}, SHADER_TYPE_COMPUTE, EShaderDebugMode::Trace);
}

bool ShaderDebugger::BeginComputeProfiler(IDeviceContext* pContext, IPipelineState*& pPipeline, uint3 GlobalID) noexcept
{
    return BeginDebugging(pContext, pPipeline, uint4{GlobalID.x, GlobalID.y, GlobalID.z, 0}, SHADER_TYPE_COMPUTE, EShaderDebugMode::Profiling);
}

bool ShaderDebugger::BeginRayTraceDebugger(IDeviceContext* pContext, IPipelineState*& pPipeline, SHADER_TYPE Stages, uint3 LaunchID) noexcept
{
    return BeginDebugging(pContext, pPipeline, uint4{LaunchID.x, LaunchID.y, LaunchID.z, 0}, Stages & RayTracingStages, EShaderDebugMode::Trace);
}

bool ShaderDebugger::BeginRayTraceProfiler(IDeviceContext* pContext, IPipelineState*& pPipeline, SHADER_TYPE Stages, uint3 LaunchID) noexcept
{
    return BeginDebugging(pContext, pPipeline, uint4{LaunchID.x, LaunchID.y, LaunchID.z, 0}, Stages & RayTracingStages, EShaderDebugMode::Profiling);
}

bool ShaderDebugger::BeginDebugger(IDeviceContext* pContext, IPipelineState*& pPipeline, SHADER_TYPE Stages) noexcept
{
    return BeginDebugging(pContext, pPipeline, uint4{}, Stages, EShaderDebugMode::Trace);
}

bool ShaderDebugger::BeginProfiler(IDeviceContext* pContext, IPipelineState*& pPipeline, SHADER_TYPE Stages) noexcept
{
    return BeginDebugging(pContext, pPipeline, uint4{}, Stages, EShaderDebugMode::Profiling);
}

bool ShaderDebugger::BeginDebugging(IDeviceContext* pContext, IPipelineState*& pPipeline, const uint4& Header, SHADER_TYPE Stages, EShaderDebugMode Mode)
{
    PipelineKey key;
    key.SrcPipeline = pPipeline;
    key.Stages      = Stages;
    key.Mode        = Mode;

    auto iter = m_DbgPipelines.find(key);
    if (iter == m_DbgPipelines.end())
        return false;

    DebugMode DbgMode;
    DbgMode.Traces = iter->second.DebugTraces;
    DbgMode.Mode   = Mode;
    DbgMode.pPSO   = iter->second.DebugPipeline;

    if (!AllocBuffer(pContext, DbgMode, DefaultBufferSize))
        return false;

    pContext->UpdateBuffer(DbgMode.pStorage, DbgMode.pStorageView->GetDesc().ByteOffset, sizeof(Header), &Header, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    StateTransitionDesc Barrier{DbgMode.pStorage, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS, true};
    pContext->TransitionResourceStates(1, &Barrier);

    m_DbgModes.push_back(std::move(DbgMode));

    pPipeline = iter->second.DebugPipeline;
    return true;
}

void ShaderDebugger::EndTrace(IDeviceContext* pContext) noexcept
{
    if (m_StorageBuffers.empty())
        return;

    // copy to staging buffer
    for (auto& SB : m_StorageBuffers)
    {
        pContext->CopyBuffer(SB.pStorageBuffer, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION,
                             SB.pReadbackBuffer, 0, SB.pReadbackBuffer->GetDesc().uiSizeInBytes, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }

    pContext->SignalFence(m_pFence, ++m_FenceValue);
}

void ShaderDebugger::ReadTrace(IDeviceContext* pContext) noexcept
{
    if (m_DbgModes.empty())
        return;

    pContext->WaitForFence(m_pFence, m_FenceValue, true);

    if (m_Callback)
    {
        for (auto& DbgMode : m_DbgModes)
        {
            ParseDebugOutput(pContext, DbgMode);
        }
    }

    m_DbgModes.clear();
    m_StorageBuffers.clear();
}

void ShaderDebugger::ParseDebugOutput(IDeviceContext* pContext, DebugMode& DbgMode) const
{
    if (DbgMode.Traces.empty() || DbgMode.pReadbackBuffer == nullptr)
        return;

    void* pMapped = nullptr;
    pContext->MapBuffer(DbgMode.pReadbackBuffer, MAP_READ, MAP_FLAG_DO_NOT_WAIT, pMapped);
    if (pMapped)
    {
        std::vector<const char*> TempStrings;
        for (auto& Info : DbgMode.Traces)
        {
            ShaderTraceResult* pResult = nullptr;
            if (m_CompilerFn.ParseShaderTrace(Info.Compiled, pMapped, DbgMode.pStorageView->GetDesc().ByteWidth, &pResult))
            {
                Uint32 Count = 0;
                m_CompilerFn.GetTraceResultCount(pResult, &Count);

                TempStrings.resize(Count);
                for (Uint32 i = 0; i < Count; ++i)
                {
                    m_CompilerFn.GetTraceResultString(pResult, i, &TempStrings[i]);
                }

                if (TempStrings.size())
                    m_Callback(Info.Name, TempStrings);

                m_CompilerFn.ReleaseTraceResult(pResult);
            }
        }
    }
    pContext->UnmapBuffer(DbgMode.pReadbackBuffer, MAP_READ);
}

bool ShaderDebugger::AllocBuffer(IDeviceContext* pContext, DebugMode& Dbg, Uint32 Size)
{
    // find place in existing storage buffers
    for (auto& SB : m_StorageBuffers)
    {
        Uint32 offset = Align(SB.Size, m_BufferAlign);

        if (Size <= (SB.Capacity - offset))
        {
            Dbg.pStorage        = SB.pStorageBuffer;
            Dbg.pReadbackBuffer = SB.pReadbackBuffer;
            SB.Size             = offset + Size;

            BufferViewDesc ViewDesc;
            ViewDesc.ByteOffset = offset;
            ViewDesc.ByteWidth  = Size;
            ViewDesc.ViewType   = BUFFER_VIEW_UNORDERED_ACCESS;

            Dbg.pStorage->CreateView(ViewDesc, &Dbg.pStorageView);
            VERIFY_EXPR(Dbg.pStorageView != nullptr);

            return (Dbg.pStorageView != nullptr);
        }
    }

    // create new buffer
    DebugStorage Storage;
    Storage.Size     = Size;
    Storage.Capacity = max(DefaultBufferSize * 8, Size * 4);

    BufferDesc BuffDesc;
    BuffDesc.Name          = "Debug storage";
    BuffDesc.Usage         = USAGE_DEFAULT;
    BuffDesc.BindFlags     = BIND_UNORDERED_ACCESS;
    BuffDesc.Mode          = BUFFER_MODE_RAW;
    BuffDesc.uiSizeInBytes = Storage.Capacity;

    m_pRenderDevice->CreateBuffer(BuffDesc, nullptr, &Storage.pStorageBuffer);
    VERIFY_EXPR(Storage.pStorageBuffer != nullptr);

    if (Storage.pStorageBuffer == nullptr)
        return false;

    BuffDesc.Name           = "Readback";
    BuffDesc.Usage          = USAGE_STAGING;
    BuffDesc.BindFlags      = BIND_NONE;
    BuffDesc.Mode           = BUFFER_MODE_UNDEFINED;
    BuffDesc.CPUAccessFlags = CPU_ACCESS_READ;

    m_pRenderDevice->CreateBuffer(BuffDesc, nullptr, &Storage.pReadbackBuffer);
    VERIFY_EXPR(Storage.pReadbackBuffer != nullptr);

    if (Storage.pReadbackBuffer == nullptr)
        return false;

    Dbg.pStorage        = Storage.pStorageBuffer;
    Dbg.pReadbackBuffer = Storage.pReadbackBuffer;

    BufferViewDesc ViewDesc;
    ViewDesc.ByteOffset = 0;
    ViewDesc.ByteWidth  = Size;
    ViewDesc.ViewType   = BUFFER_VIEW_UNORDERED_ACCESS;

    Dbg.pStorage->CreateView(ViewDesc, &Dbg.pStorageView);
    VERIFY_EXPR(Dbg.pStorageView != nullptr);

    if (Dbg.pStorageView == nullptr)
        return false;

    m_StorageBuffers.push_back(std::move(Storage));

    return true;
}

void ShaderDebugger::DefaultShaderDebugCallback(const char* Name, const std::vector<const char*>& Output) const
{
    String     fname;
    const auto BuildName = [this, &fname, Name](uint index) //
    {
        fname = String(m_OutputFolder) + '/' + Name + '_' + std::to_string(index) + ".glsl_dbg";
    };
    const auto IsExists = [](const char* path) //
    {
        return FileSystem::PathExists(path);
    };

    for (auto& Str : Output)
    {
        Uint32       MinIndex = 0;
        Uint32       MaxIndex = 1;
        const Uint32 Step     = 100;

        for (; MinIndex < MaxIndex;)
        {
            BuildName(MaxIndex);

            if (!IsExists(fname.c_str()))
                break;

            MinIndex = MaxIndex;
            MaxIndex += Step;
        }

        for (Uint32 Index = MinIndex; Index <= MaxIndex; ++Index)
        {
            BuildName(Index);

            if (IsExists(fname.c_str()))
                continue;

            CFile File{FileOpenAttribs{fname.c_str(), EFileAccessMode::Overwrite}};

            File.Write(Str, strlen(Str));

            ::OutputDebugStringA((fname + "(1): trace saved\n").c_str());
        }
    }
}

bool ShaderDebugger::BeginClockHeatmap(IDeviceContext* pContext, IPipelineState*& pPipeline, SHADER_TYPE Stages, uint2 dim) noexcept
{
    PipelineKey key;
    key.SrcPipeline = pPipeline;
    key.Stages      = Stages;
    key.Mode        = EShaderDebugMode::ClockHeatmap;

    auto iter = m_DbgPipelines.find(key);
    if (iter == m_DbgPipelines.end())
        return false;

    DebugMode DbgMode;
    DbgMode.Mode       = EShaderDebugMode::ClockHeatmap;
    DbgMode.pPSO       = iter->second.DebugPipeline;
    DbgMode.HeatmapDim = dim;

    BufferDesc BuffDesc;
    BuffDesc.Name          = "Debug storage for heatmap";
    BuffDesc.Usage         = USAGE_DEFAULT;
    BuffDesc.BindFlags     = BIND_UNORDERED_ACCESS;
    BuffDesc.Mode          = BUFFER_MODE_RAW;
    BuffDesc.uiSizeInBytes = sizeof(uint4) + // first 4 components
        (dim.x * dim.y * sizeof(float)) +    // output pixels
        m_BufferAlign +                      // align
        (dim.y * sizeof(float2));            // temporary line

    m_pRenderDevice->CreateBuffer(BuffDesc, nullptr, &DbgMode.pStorage);
    VERIFY_EXPR(DbgMode.pStorage != nullptr);

    if (DbgMode.pStorage == nullptr)
        return false;

    DbgMode.pStorageView = DbgMode.pStorage->GetDefaultView(BUFFER_VIEW_UNORDERED_ACCESS);
    if (DbgMode.pStorageView == nullptr)
        return false;

    uint4 Header;
    Header.x = BitCast<uint>(1.0f);
    Header.y = BitCast<uint>(1.0f);
    Header.z = dim.x;
    Header.w = dim.y;
    pContext->UpdateBuffer(DbgMode.pStorage, 0, sizeof(Header), &Header, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // don't use transition because ranges are not overlapped
    pContext->FillBuffer(DbgMode.pStorage, sizeof(Header), (dim.x * dim.y * sizeof(float)), 0, RESOURCE_STATE_TRANSITION_MODE_VERIFY);

    StateTransitionDesc Barrier{DbgMode.pStorage, RESOURCE_STATE_UNKNOWN, RESOURCE_STATE_UNORDERED_ACCESS, true};
    pContext->TransitionResourceStates(1, &Barrier);

    m_DbgModes.push_back(std::move(DbgMode));

    pPipeline = iter->second.DebugPipeline;
    return true;
}

bool ShaderDebugger::EndClockHeatmap(IDeviceContext* pContext, ITexture* pRT) noexcept
{
    return EndClockHeatmap(pContext, pRT ? pRT->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) : nullptr);
}

bool ShaderDebugger::EndClockHeatmap(IDeviceContext* pContext, ITextureView* pRTV) noexcept
{
    if (m_DbgModes.empty() || m_DbgModes.back().Mode != EShaderDebugMode::ClockHeatmap)
        return false;

    if (pRTV == nullptr || pRTV->GetDesc().ViewType != TEXTURE_VIEW_RENDER_TARGET)
    {
        LOG_ERROR_MESSAGE("Invalid render target for clock heatmap");
        return false;
    }

    auto&       DbgMode = m_DbgModes.back();
    const auto& Dim     = DbgMode.HeatmapDim;
    const auto  TexDesc = pRTV->GetTexture()->GetDesc();

    BufferViewDesc ViewDesc;
    ViewDesc.ByteOffset = Uint32(Align(sizeof(uint4) + (Dim.x * Dim.y * sizeof(float)), m_BufferAlign));
    ViewDesc.ByteWidth  = (Dim.y * sizeof(float2));
    ViewDesc.ViewType   = BUFFER_VIEW_UNORDERED_ACCESS;

    RefCntAutoPtr<IBufferView> pLineStorageView;
    DbgMode.pStorage->CreateView(ViewDesc, &pLineStorageView);
    VERIFY_EXPR(pLineStorageView != nullptr);

    if (pLineStorageView == nullptr)
        return false;

    // pass 1
    {
        pContext->SetPipelineState(m_pHeatmapPass1);
        m_pHeatSRB1->GetVariableByName(SHADER_TYPE_COMPUTE, "un_Heatmap")->Set(DbgMode.pStorageView);
        m_pHeatSRB1->GetVariableByName(SHADER_TYPE_COMPUTE, "un_MaxValues")->Set(pLineStorageView);
        pContext->CommitShaderResources(m_pHeatSRB1, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DispatchComputeAttribs Attribs;
        Attribs.ThreadGroupCountX = (Dim.x + m_HeatmapPass1LocalSize - 1) / m_HeatmapPass1LocalSize;
        pContext->DispatchCompute(Attribs);
    }

    // pass 2
    {
        pContext->SetPipelineState(m_pHeatmapPass2);
        m_pHeatSRB2->GetVariableByName(SHADER_TYPE_COMPUTE, "un_Heatmap")->Set(DbgMode.pStorageView);
        m_pHeatSRB2->GetVariableByName(SHADER_TYPE_COMPUTE, "un_MaxValues")->Set(pLineStorageView);
        pContext->CommitShaderResources(m_pHeatSRB2, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DispatchComputeAttribs Attribs;
        Attribs.ThreadGroupCountX = 1;
        pContext->DispatchCompute(Attribs);
    }

    // pass 3
    {
        IPipelineState* pPSO = nullptr;
        GetClockHeatmapPipeline(TexDesc.Format, pPSO);

        if (pPSO == nullptr || m_pHeatSRB3 == nullptr)
            return false;

        pContext->SetRenderTargets(1, &pRTV, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        pContext->SetPipelineState(pPSO);
        m_pHeatSRB3->GetVariableByName(SHADER_TYPE_PIXEL, "un_Heatmap")->Set(DbgMode.pStorageView);
        pContext->CommitShaderResources(m_pHeatSRB3, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

        DrawAttribs Attribs;
        Attribs.NumVertices = 4;
        pContext->Draw(Attribs);

        pContext->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    }

    m_DbgModes.pop_back();
    return true;
}

void ShaderDebugger::CreateClockHeatmapPipelines()
{
    // pass 1
    {
        const char             Source[] = R"glsl(
#version 460
layout (local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

layout(std430) /*readonly*/ buffer un_Heatmap
{
    float	maxValue;
    float   minValue;
    uvec2	dimension;
    float	pixels[];
};

layout(std430) buffer un_MaxValues
{
    vec2	line[];
};

void main ()
{
    float	max_val = 0.0;
    float	min_val = 1.0e+30;

    for (uint x = 0; x < dimension.x; ++x)
    {
        uint	i = x + dimension.x * gl_GlobalInvocationID.x;
        float	v = pixels[i];
        max_val = max(max_val, v);
        min_val = min(min_val, v);
    }
    line[gl_GlobalInvocationID.x] = vec2(max_val, min_val);
}
)glsl";
        RefCntAutoPtr<IShader> pCS;
        CompileFromSource(&pCS, SHADER_TYPE_COMPUTE, Source, 0, "");

        ComputePipelineStateCreateInfo PSOCreateInfo;
        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
        PSOCreateInfo.pCS                  = pCS;
        PSOCreateInfo.PSODesc.Name         = "ShaderClock Heatmap Postprocess 1";

        PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

        m_pRenderDevice->CreateComputePipelineState(PSOCreateInfo, &m_pHeatmapPass1);
        CHECK_THROW(m_pHeatmapPass1);
        m_pHeatmapPass1->CreateShaderResourceBinding(&m_pHeatSRB1, true);
        CHECK_THROW(m_pHeatSRB1);
    }

    // pass 2
    {
        const char             Source[] = R"glsl(
#version 460
layout (local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std430) buffer un_Heatmap
{
    float	maxValue;
    float   minValue;
    uvec2	dimension;
    float	pixels[];
};

layout(std430) /*readonly*/ buffer un_MaxValues
{
    vec2	line[];
};

void main ()
{
    float	max_val = 0.0;
    float	min_val = 1.0e+30;

    for (uint i = 0; i < dimension.y; ++i)
    {
        vec2	v = line[i];
        max_val = max(max_val, v.x);
        min_val = min(min_val, v.y);
    }

    maxValue = max_val;
    minValue = min_val;
}
)glsl";
        RefCntAutoPtr<IShader> pCS;
        CompileFromSource(&pCS, SHADER_TYPE_COMPUTE, Source, 0, "");

        ComputePipelineStateCreateInfo PSOCreateInfo;
        PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_COMPUTE;
        PSOCreateInfo.pCS                  = pCS;
        PSOCreateInfo.PSODesc.Name         = "ShaderClock Heatmap Postprocess 2";

        PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

        m_pRenderDevice->CreateComputePipelineState(PSOCreateInfo, &m_pHeatmapPass2);
        CHECK_THROW(m_pHeatmapPass2);
        m_pHeatmapPass2->CreateShaderResourceBinding(&m_pHeatSRB2, true);
        CHECK_THROW(m_pHeatSRB2);
    }
}

void ShaderDebugger::GetClockHeatmapPipeline(TEXTURE_FORMAT Format, IPipelineState*& pPipeline) noexcept
{
    auto Iter = m_pHeatmapPass3.find(Format);
    if (Iter != m_pHeatmapPass3.end())
    {
        pPipeline = Iter->second;
        return;
    }

    const char VS[] = R"glsl(
#version 450

layout(location=0) out vec2 v_Texcoord;

void main()
{
    v_Texcoord  = vec2(float(gl_VertexIndex & 1), float(gl_VertexIndex >> 1));
    gl_Position = vec4(v_Texcoord * 2.0 - 1.0, 0.0, 1.0);
    v_Texcoord.y = 1.0 - v_Texcoord.y;
}
)glsl";

    const char PS[] = R"glsl(
#version 450

layout(location=0) in  vec2 v_Texcoord;
layout(location=0) out vec4 out_Color;

layout(std430) /*readonly*/ buffer un_Heatmap
{
    float	maxValue;
    float   minValue;
    ivec2	dimension;
    float	pixels[];
};

vec3 HSVtoRGB (const vec3 hsv)
{
    vec3 col = vec3( abs( hsv.x * 6.0 - 3.0 ) - 1.0,
                     2.0 - abs( hsv.x * 6.0 - 2.0 ),
                     2.0 - abs( hsv.x * 6.0 - 4.0 ));
    return (( clamp( col, vec3(0.0), vec3(1.0) ) - 1.0 ) * hsv.y + 1.0 ) * hsv.z;
}

float Remap (const vec2 src, const vec2 dst, const float x)
{
    return (x - src.x) / (src.y - src.x) * (dst.y - dst.x) + dst.x;
}

float RemapClamped (const vec2 src, const vec2 dst, const float x)
{
    return clamp( Remap( src, dst, x ), dst.x, dst.y );
}

vec3 Heatmap (float factor)
{
    return HSVtoRGB( vec3(RemapClamped( vec2(0.0, 1.45), vec2(0.0, 1.0), 1.0 - factor ), 1.0, 1.0) );
}

void main ()
{
    int   index  = int(gl_FragCoord.x) + int(gl_FragCoord.y) * dimension.x;
    float time   = pixels[index];
    float factor = (time - minValue) / (maxValue - minValue);

    out_Color = vec4(Heatmap( factor ), 1.0 );
}
)glsl";

    RefCntAutoPtr<IShader> pVS, pPS;
    CompileFromSource(&pVS, SHADER_TYPE_VERTEX, VS, 0, "");
    CompileFromSource(&pPS, SHADER_TYPE_PIXEL, PS, 0, "");

    GraphicsPipelineStateCreateInfo PSOCreateInfo;
    PSOCreateInfo.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    PSOCreateInfo.pVS                  = pVS;
    PSOCreateInfo.pPS                  = pPS;
    PSOCreateInfo.PSODesc.Name         = "ShaderClock Heatmap Postprocess 3";

    PSOCreateInfo.GraphicsPipeline.NumRenderTargets             = 1;
    PSOCreateInfo.GraphicsPipeline.RTVFormats[0]                = Format;
    PSOCreateInfo.GraphicsPipeline.PrimitiveTopology            = PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    PSOCreateInfo.GraphicsPipeline.RasterizerDesc.CullMode      = CULL_MODE_NONE;
    PSOCreateInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = False;

    PSOCreateInfo.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC;

    RefCntAutoPtr<IPipelineState> pPSO;
    m_pRenderDevice->CreateGraphicsPipelineState(PSOCreateInfo, &pPSO);
    if (pPSO == nullptr)
        return;

    m_pHeatmapPass3.emplace(Format, pPSO);
    pPipeline = pPSO;

    if (m_pHeatSRB3 == nullptr)
    {
        pPSO->CreateShaderResourceBinding(&m_pHeatSRB3, true);
    }
}

namespace
{
SPV_COMP_SHADER_TYPE ConvertShaderType(SHADER_TYPE Type)
{
    switch (Type)
    {
        // clang-format off
        case SHADER_TYPE_VERTEX:            return SPV_COMP_SHADER_TYPE_VERTEX;
        case SHADER_TYPE_PIXEL:             return SPV_COMP_SHADER_TYPE_FRAGMENT;
        case SHADER_TYPE_GEOMETRY:          return SPV_COMP_SHADER_TYPE_GEOMETRY;
        case SHADER_TYPE_HULL:              return SPV_COMP_SHADER_TYPE_TESS_CONTROL;
        case SHADER_TYPE_DOMAIN:            return SPV_COMP_SHADER_TYPE_TESS_EVALUATION;
        case SHADER_TYPE_COMPUTE:           return SPV_COMP_SHADER_TYPE_COMPUTE;
        case SHADER_TYPE_AMPLIFICATION:     return SPV_COMP_SHADER_TYPE_MESH_TASK;
        case SHADER_TYPE_MESH:              return SPV_COMP_SHADER_TYPE_MESH;
        case SHADER_TYPE_RAY_GEN:           return SPV_COMP_SHADER_TYPE_RAY_GEN;
        case SHADER_TYPE_RAY_MISS:          return SPV_COMP_SHADER_TYPE_RAY_MISS;
        case SHADER_TYPE_RAY_CLOSEST_HIT:   return SPV_COMP_SHADER_TYPE_RAY_CLOSEST_HIT;
        case SHADER_TYPE_RAY_ANY_HIT:       return SPV_COMP_SHADER_TYPE_RAY_ANY_HIT;
        case SHADER_TYPE_RAY_INTERSECTION:  return SPV_COMP_SHADER_TYPE_RAY_INTERSECT;
        case SHADER_TYPE_CALLABLE:          return SPV_COMP_SHADER_TYPE_RAY_CALLABLE;
            // clang-format on
    }
    UNEXPECTED("unknown shader type");
    return SPV_COMP_SHADER_TYPE(~0u);
}

SPV_COMP_DEBUG_MODE ConvertDebugMode(EShaderDebugMode Mode)
{
    switch (Mode)
    {
        // clang-format off
        case EShaderDebugMode::None:            return SPV_COMP_DEBUG_MODE_NONE;
        case EShaderDebugMode::Trace:           return SPV_COMP_DEBUG_MODE_TRACE;
        case EShaderDebugMode::Profiling:       return SPV_COMP_DEBUG_MODE_PROFILE;
        case EShaderDebugMode::ClockHeatmap:    return SPV_COMP_DEBUG_MODE_CLOCK_HEATMAP;
            // clang-format on
    }
    UNEXPECTED("unknown debug mode");
    return SPV_COMP_DEBUG_MODE(~0u);
}

String DebugModeToString(EShaderDebugMode Mode)
{
    switch (Mode)
    {
        // clang-format off
        case EShaderDebugMode::None:            return "";
        case EShaderDebugMode::Trace:           return " (trace)";
        case EShaderDebugMode::Profiling:       return " (profile)";
        case EShaderDebugMode::ClockHeatmap:    return " (heatmap)";
            // clang-format on
    }
    UNEXPECTED("unknown debug mode");
    return " (unknown)";
}

} // namespace


bool ShaderDebugger::CreateShader(SHADER_TYPE           Type,
                                  const char*           pSource,
                                  Uint32                SourceLen,
                                  const char*           pName,
                                  EShaderDebugMode      DbgMode,
                                  const ShaderMacro*    pMacros,
                                  SPV_COMP_OPTIMIZATION OptMode,
                                  CompiledShader**      ppDbgInfo,
                                  IShader**             ppShader)
{
    if (ppDbgInfo != nullptr)
        *ppDbgInfo = nullptr;

    if (!m_CompilerFn.Compile)
    {
        LOG_ERROR_MESSAGE("Shader '", pName, "' error: compiler is not loaded");
        return false;
    }

    const char*     includeDirs[] = {""};
    CompiledShader* compiled      = nullptr;

    String Defines = "";
    if (pMacros != nullptr)
    {
        for (auto* pMacro = pMacros; pMacro->Name != nullptr && pMacro->Definition != nullptr; ++pMacro)
        {
            Defines += "#define ";
            Defines += pMacro->Name;
            Defines += ' ';
            Defines += pMacro->Definition;
            Defines += "\n";
        }
    }

    ShaderParams Params;
    Params.shaderSources           = &pSource;
    Params.shaderSourceLengths     = (const int*)&SourceLen;
    Params.shaderSourcesCount      = 1;
    Params.entryName               = "main";
    Params.defines                 = Defines.c_str();
    Params.includeDirs             = includeDirs;
    Params.includeDirsCount        = _countof(includeDirs);
    Params.shaderType              = ConvertShaderType(Type);
    Params.version                 = m_CompilerVer;
    Params.mode                    = ConvertDebugMode(DbgMode);
    Params.optimization            = OptMode;
    Params.autoMapBindings         = true;
    Params.autoMapLocations        = false;
    Params.debugDescriptorSetIndex = 0;

    if (!m_CompilerFn.Compile(&Params, &compiled))
    {
        if (compiled)
        {
            const char* log = "";
            m_CompilerFn.GetShaderLog(compiled, &log);
            LOG_ERROR_MESSAGE("Shader '", pName, "' compilation failed with errors:\n", log);
            m_CompilerFn.ReleaseShader(compiled);
        }
        else
        {
            LOG_ERROR_MESSAGE("Shader '", pName, "' compilation failed with errors");
        }
        return false;
    }

    const Uint32* pSpirv    = nullptr;
    Uint32        SpirvSize = 0;

    if (!m_CompilerFn.GetShaderBinary(compiled, &pSpirv, &SpirvSize))
    {
        m_CompilerFn.ReleaseShader(compiled);
        LOG_ERROR_MESSAGE("Shader '", pName, "' error: failed to get SPIRV binary");
        return false;
    }

    ShaderCreateInfo ShaderCI;
    ShaderCI.UseCombinedTextureSamplers = true;

    String Name = pName;
    Name += DebugModeToString(DbgMode);

    ShaderCI.Desc.ShaderType = Type;
    ShaderCI.Desc.Name       = Name.c_str();
    ShaderCI.SourceLanguage  = SHADER_SOURCE_LANGUAGE_DEFAULT;
    ShaderCI.ByteCode        = pSpirv;
    ShaderCI.ByteCodeSize    = SpirvSize;
    m_pRenderDevice->CreateShader(ShaderCI, ppShader);

    if (ppDbgInfo != nullptr && *ppShader != nullptr)
    {
        m_CompilerFn.TrimShader(compiled);
        *ppDbgInfo = compiled;
    }
    else
    {
        m_CompilerFn.ReleaseShader(compiled);
    }
    return (*ppShader != nullptr);
}

void ShaderDebugger::CompileFromSource(IShader** ppShader, SHADER_TYPE Type, const char* pSource, Uint32 SourceLen, const char* pName, const ShaderMacro* pMacro, EShaderDebugMode Mode) noexcept
{
    if (!m_pRenderDevice)
        return;

    if (SourceLen == 0)
        SourceLen = Uint32(strlen(pSource));

    if (!CreateShader(Type, pSource, SourceLen, pName, EShaderDebugMode::None, pMacro, SPV_COMP_OPTIMIZATION_NONE, nullptr, ppShader))
        return;

    if (Mode != EShaderDebugMode::None && *ppShader != nullptr)
    {
        // check features
        const auto& Caps = m_pRenderDevice->GetDeviceCaps();
        switch (Type)
        {
            case SHADER_TYPE_VERTEX:
            case SHADER_TYPE_GEOMETRY:
            case SHADER_TYPE_HULL:
            case SHADER_TYPE_DOMAIN:
                if (Caps.Features.VertexPipelineUAVWritesAndAtomics != DEVICE_FEATURE_STATE_ENABLED)
                    return; // can't write trace
                break;
            case SHADER_TYPE_PIXEL:
                if (Caps.Features.PixelUAVWritesAndAtomics != DEVICE_FEATURE_STATE_ENABLED)
                    return; // can't write trace
                break;
        }

        ShaderDebugInfo DbgInfo;
        DbgInfo.Origin = *ppShader;
        DbgInfo.Name   = pName;

        // validate name
        for (auto& c : DbgInfo.Name)
        {
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')
                continue;

            c = '_';
        }

        if (!!(Mode & EShaderDebugMode::Trace))
            if (CreateShader(Type, pSource, SourceLen, pName, EShaderDebugMode::Trace, pMacro, SPV_COMP_OPTIMIZATION_NONE, &DbgInfo.pDebugInfo, &DbgInfo.DebugShader))
                DbgInfo.Mode = DbgInfo.Mode | EShaderDebugMode::Trace;

        if (Caps.Features.ShaderClock == DEVICE_FEATURE_STATE_ENABLED)
        {
            if (!!(Mode & EShaderDebugMode::Profiling))
                if (CreateShader(Type, pSource, SourceLen, pName, EShaderDebugMode::Profiling, pMacro, SPV_COMP_OPTIMIZATION_NONE, &DbgInfo.pProfiltInfo, &DbgInfo.ProfileShader))
                    DbgInfo.Mode = DbgInfo.Mode | EShaderDebugMode::Profiling;

            if (!!(Mode & EShaderDebugMode::ClockHeatmap))
                if (CreateShader(Type, pSource, SourceLen, pName, EShaderDebugMode::ClockHeatmap, pMacro, SPV_COMP_OPTIMIZATION_NONE, nullptr, &DbgInfo.ClockHeatmapShader))
                    DbgInfo.Mode = DbgInfo.Mode | EShaderDebugMode::ClockHeatmap;
        }

        if (!!DbgInfo.Mode)
            m_DbgShaders.emplace(static_cast<const void*>(*ppShader), std::move(DbgInfo));
    }
}

} // namespace DE
