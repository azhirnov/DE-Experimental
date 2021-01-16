cmake_minimum_required (VERSION 3.10)

project(Sample.RT_Scene)

set(SHADERS
    assets/Primary.rmiss
    assets/PrimaryOpaqueHit.rchit
    assets/Shadow.rmiss
    assets/ShadowHit.rchit
    assets/RayTrace.rgen
    assets/structures.h
    assets/Lighting.h
    assets/Material.h

    assets/ToneMapping.psh
    assets/ToneMapping.vsh
)

set(EXT_SOURCES
    ../../../DiligentSamples/SampleBase/include/FirstPersonCamera.hpp
    ../../../DiligentSamples/SampleBase/src/FirstPersonCamera.cpp
)

file(GLOB_RECURSE SOURCES "src/*.*")
add_executable(${PROJECT_NAME} ${SOURCES} ${EXT_SOURCES} ${ASSETS} ${SHADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
source_group("src" FILES ${EXT_SOURCES})
source_group("assets" FILES ${SHADERS})

target_include_directories(${PROJECT_NAME}
PRIVATE
    ../../../DiligentSamples/SampleBase/include
    ../../../DiligentCore/ThirdParty/Vulkan-Headers/include
    ../../../DiligentCore/ThirdParty
)

target_link_libraries(${PROJECT_NAME}
PRIVATE
    Diligent-BuildSettings
    Diligent-TextureLoader
    Diligent-GraphicsTools
    Diligent-AssetLoader
    glfw
    Tools.ShaderDebugger
)

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES 
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "Exp.Samples")

target_compile_definitions(${PROJECT_NAME} PRIVATE SHADER_TRACE_DLL="${SHADER_TRACE_DLL}")
target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_TRACE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/dbg_shaders")
