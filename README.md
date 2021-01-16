# Tools

* [ShaderDebugger](Experimental/Tools/ShaderDebugger) - GLSL debugger and profiler.
* [VR](Experimental/Tools/VR) - OpenVR wrapper and VR emulator.


# Samples

* [RT_Tutorial](Experimental/Samples/RT_Tutorial) - Tutorial21 with GLSL shaders and shader debugging/profiling.
* [RT_Scene](Experimental/Samples/RT_Scene) - Example of how to load and draw ray traced GLTF scene.
* [VR_Test](Experimental/Samples/VR_Test) - Instanced cubes for VR.


# Supported platforms
OS: Windows 10
GAPI: Vulkan 1.2
Compiler: VS 2019


# Clone and build
Clone:
```
git clone --recursive https://github.com/azhirnov/DE-Experimental.git
git lfs pull
```

Build:
```
mkdir build
cd build
cmake -G "Visual Studio 16 2019" -A x64 .. 
cmake --build . --config Debug
```
