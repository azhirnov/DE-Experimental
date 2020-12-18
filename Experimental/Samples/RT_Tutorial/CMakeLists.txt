cmake_minimum_required (VERSION 3.6)

project(Sample.RT_Tutorial CXX)

set(SOURCE
    src/RayTracing.cpp
)

set(INCLUDE
    src/RayTracing.hpp
)

set(SHADERS
    assets/structures.fxh
    assets/RayUtils.fxh
    assets/CubePrimaryHit.rchit
    assets/GlassPrimaryHit.rchit
    assets/SpherePrimaryHit.rchit
    assets/SphereIntersection.rint
    assets/Ground.rchit
    assets/RayTrace.rgen
    assets/PrimaryMiss.rmiss
    assets/ShadowMiss.rmiss
    assets/ImageBlit.psh
    assets/ImageBlit.vsh
)

set(ASSETS
    assets/DGLogo0.png
    assets/DGLogo1.png
    assets/DGLogo2.png
    assets/DGLogo3.png
    assets/Ground.jpg
)

add_sample_app(${PROJECT_NAME} "Exp.Samples" "${SOURCE}" "${INCLUDE}" "${SHADERS}" "${ASSETS}")

target_link_libraries(${PROJECT_NAME} PRIVATE Tools.ShaderDebugger)
target_compile_definitions(${PROJECT_NAME} PRIVATE SHADER_TRACE_DLL="${SHADER_TRACE_DLL}")
target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_TRACE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/dbg_shaders")
