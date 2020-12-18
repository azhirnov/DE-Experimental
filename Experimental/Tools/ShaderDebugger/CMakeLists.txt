cmake_minimum_required (VERSION 3.10)

project(Tools.ShaderDebugger)

file(GLOB_RECURSE SOURCES "src/*.*")
add_library(${PROJECT_NAME} STATIC ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PUBLIC DILIGENT_USE_VOLK=1)

target_include_directories(${PROJECT_NAME}
PRIVATE
    ../../../DiligentCore/ThirdParty/Vulkan-Headers/include
    ../../../DiligentCore/ThirdParty
    ../../../DiligentCore/Graphics/GraphicsEngineNextGenBase/include
    ../../../DiligentCore/Graphics/GraphicsEngineVulkan/include
    ../../../DiligentCore/Graphics/ShaderTools/include
PUBLIC
    ../../ThirdParty/glsl_trace
    ../
    src
)

set(SHADER_TRACE_DLL "${CMAKE_CURRENT_SOURCE_DIR}/../../ThirdParty/glsl_trace/SpvCompiler.dll" CACHE INTERNAL "" FORCE)

target_link_libraries(${PROJECT_NAME}
PUBLIC
    Diligent-BuildSettings
    Diligent-GraphicsTools
    Diligent-GraphicsEngine
    Diligent-GraphicsEngineVk-static
PRIVATE
    SPIRV
    SPIRV-Tools-opt
)

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "Exp.Tools")
