cmake_minimum_required (VERSION 3.10)

project(Sample.VR_Test)

set(SHADERS
    assets/cube_inst.vsh
    assets/cube_inst.psh
)

set(ASSETS
    assets/DGLogo.png
)

set(COMMON_SOURCE
    ../../../DiligentSamples/Tutorials/Common/src/TexturedCube.hpp
    ../../../DiligentSamples/Tutorials/Common/src/TexturedCube.cpp
)

file(GLOB_RECURSE SOURCES "src/*.*")
add_executable(${PROJECT_NAME} ${SOURCES} ${COMMON_SOURCE} ${ASSETS} ${SHADERS})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})
source_group("src" FILES ${COMMON_SOURCE})
source_group("assets" FILES ${ASSETS} ${SHADERS})

target_link_libraries(${PROJECT_NAME} PUBLIC Tools.VR Diligent-TextureLoader)
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../../DiligentSamples/Tutorials/Common/src")

if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../ThirdParty/OpenVR/bin/win64/openvr_api.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/openvr_api.dll"
        VERBATIM)
        
    set_target_properties(${PROJECT_NAME} PROPERTIES 
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    )
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "Exp.Samples")
