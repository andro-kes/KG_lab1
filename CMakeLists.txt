cmake_minimum_required(VERSION 3.22)
project(lab1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Immutable upstream revisions; sources remain in build/_deps.
set(CMAKE_TLS_VERIFY TRUE)
FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/archive/70a9bb3881fe80fd483236e2b203cb451c6ecf40.tar.gz
    URL_HASH SHA256=615e919e342d57ed748d7af19b1b11060d3df0e35e2b200be568620f007399f8)
FetchContent_Declare(vk-bootstrap
    URL https://github.com/charles-lunarg/vk-bootstrap/archive/522a579912cbeed7b52c948072d87ce001488ded.tar.gz
    URL_HASH SHA256=6a0345247a1c6f0bf757568f6ab9ab412a3e6c9016f951f385782e6efaE8c7ab)
FetchContent_Declare(VulkanMemoryAllocator
    URL https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/3aa921224c154a0d2c43912bc88e1c42ce1f7607.tar.gz
    URL_HASH SHA256=5935dc47d8659bb681f1b7218f903ec171d10c39509752fd64eb2c9daf601ced)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/01380c579715e62fb9a8d6ec0502c4ea83bfde6e.tar.gz
    URL_HASH SHA256=c7bc489afefa2461c40a84812118e6dff86e7eadfc4b7e2f851a8c94ade8910d)
FetchContent_Declare(glm
    URL https://github.com/g-truc/glm/archive/0af55ccecd98d4e5a8d1fad7de25ba429d60e863.tar.gz
    URL_HASH SHA256=e7f187d83523f505eb38dd25d297ea6c0d4ed856d733e808f18253f5a8fa88a0)
set(GLFW_LIBRARY_TYPE STATIC)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_ENABLE_CXX_20 ON CACHE BOOL "" FORCE)
find_package(Vulkan REQUIRED COMPONENTS glslc)
FetchContent_MakeAvailable(glfw vk-bootstrap VulkanMemoryAllocator imgui glm)

add_executable(lab1
    source/main.cpp source/graphics_internal.cpp source/graphics.cpp source/application.cpp)
target_link_libraries(lab1 PRIVATE glfw Vulkan::Vulkan vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator glm::glm)
target_include_directories(lab1 PRIVATE "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
# Suppress diagnostics from the external single-header allocator implementation.
target_include_directories(lab1 SYSTEM PRIVATE "${vulkanmemoryallocator_SOURCE_DIR}/include")
target_sources(lab1 PRIVATE
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
target_compile_definitions(lab1 PRIVATE _USE_MATH_DEFINES NOMINMAX)
if(MSVC)
    target_compile_options(lab1 PRIVATE /W4 /wd4201 /utf-8)
else()
    target_compile_options(lab1 PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()
# Keep console output for validation diagnostics.
set_target_properties(lab1 PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(lab1 PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}")
endforeach()
set(shader_binaries)
foreach(shader cone.vert cone.frag)
    set(source "${CMAKE_SOURCE_DIR}/shaders/${shader}")
    set(binary "${CMAKE_BINARY_DIR}/shaders/${shader}.spv")
    add_custom_command(OUTPUT "${binary}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND Vulkan::glslc --target-env=vulkan1.1 "${source}" -o "${binary}"
        DEPENDS "${source}" VERBATIM)
    list(APPEND shader_binaries "${binary}")
endforeach()
add_custom_target(shaders DEPENDS ${shader_binaries})
add_dependencies(lab1 shaders)
