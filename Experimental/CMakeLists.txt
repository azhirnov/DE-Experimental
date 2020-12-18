cmake_minimum_required (VERSION 3.10)

# GLFW
if(TRUE)
	set(GLFW_BUILD_DOCS OFF CACHE INTERNAL "" FORCE)
	set(GLFW_BUILD_EXAMPLES OFF CACHE INTERNAL "" FORCE)
	set(GLFW_BUILD_TESTS OFF CACHE INTERNAL "" FORCE)
	set(GLFW_INSTALL OFF CACHE INTERNAL "" FORCE)
	add_subdirectory("ThirdParty/glfw")
	set_target_properties("glfw" PROPERTIES FOLDER "Exp.ThirdParty")
endif()


add_subdirectory(Tools)
add_subdirectory(Samples)
