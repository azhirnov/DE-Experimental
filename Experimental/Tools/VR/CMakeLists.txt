cmake_minimum_required (VERSION 3.10)

project(Tools.VR)

file(GLOB_RECURSE SOURCES "src/*.*" "include/*.*")
add_library(${PROJECT_NAME} STATIC ${SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

target_compile_definitions(${PROJECT_NAME} PUBLIC DILIGENT_USE_VOLK=1)

target_include_directories(${PROJECT_NAME}
PUBLIC
    ../../../DiligentCore/ThirdParty/Vulkan-Headers/include
    ../../../DiligentCore/ThirdParty
    ../../../DiligentSamples/Tutorials/Common/src/
    ../../ThirdParty/OpenVR/headers
    include
)

target_link_libraries(${PROJECT_NAME}
PUBLIC
    Diligent-BuildSettings
    Diligent-TextureLoader
    Diligent-GraphicsTools
    glfw
    Diligent-GraphicsEngine
    Diligent-GraphicsEngineVk-static
)

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "Exp.Tools")
