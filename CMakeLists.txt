cmake_minimum_required (VERSION 3.6)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(DE-Experimental)

set(DILIGENT_BUILD_TOOLS ON CACHE INTERNAL "" FORCE)
set(DILIGENT_BUILD_FX ON CACHE INTERNAL "" FORCE)
set(DILIGENT_BUILD_SAMPLES ON CACHE INTERNAL "" FORCE)

add_subdirectory(DiligentCore)
add_subdirectory(DiligentTools)
add_subdirectory(DiligentFX)
add_subdirectory(DiligentSamples)

if(NOT DEFINED ANDROID)
    add_subdirectory(Experimental)
endif()
