cmake_minimum_required(VERSION 3.24)
project(kabegami VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(KABEGAMI_GUI "Build the window with the live preview (GLFW, OpenGL 3.3, Dear ImGui)" ON)
option(KABEGAMI_TESTS "Build the tests" ON)

include(FetchContent)
# images in and out
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG 003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5)
FetchContent_MakeAvailable(stb)
if(KABEGAMI_TESTS)
  FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.5.2)
  FetchContent_MakeAvailable(doctest)
  enable_testing()
endif()

function(kabegami_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /fp:precise)
  else()
    # no fused multiply-adds: the console rounds every product, and the
    # picture has to come out the same on every compiler
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -ffp-contract=off)
  endif()
endfunction()

# a part of Kabegami: a static library of the .cpp files in src/core/<dir>,
# and when tests/<testdir> has any, a test program <name>_tests. each part
# builds on its own, `cmake --build build --target <name>_tests`, so one being
# written doesn't hold up the others. a part with no .cpp yet is headers only.
function(kabegami_module name dir testdir)
  file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/core/${dir}/*.cpp)
  if(sources)
    add_library(${name} STATIC ${sources})
    target_include_directories(${name} PUBLIC ${CMAKE_SOURCE_DIR}/src)
    target_include_directories(${name} SYSTEM PRIVATE ${stb_SOURCE_DIR})
    target_link_libraries(${name} PUBLIC ${ARGN})
    kabegami_warnings(${name})
  else()
    add_library(${name} INTERFACE)
    target_include_directories(${name} INTERFACE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(${name} INTERFACE ${ARGN})
  endif()
  file(GLOB tests CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/${testdir}/*.cpp)
  if(KABEGAMI_TESTS AND tests)
    add_executable(${name}_tests ${CMAKE_SOURCE_DIR}/tests/main.cpp ${tests})
    target_link_libraries(${name}_tests PRIVATE ${name} doctest::doctest)
    target_include_directories(${name}_tests SYSTEM PRIVATE ${stb_SOURCE_DIR})
    kabegami_warnings(${name}_tests)
    add_test(NAME ${name} COMMAND ${name}_tests)
  endif()
endfunction()

# the parts, each after what it uses
kabegami_module(kabegami_base "." "base")
# CGFX: the container, then models, materials and textures; animations; and
# the camera and the shader program
kabegami_module(kabegami_cgfx "cgfx" "cgfx" kabegami_base)
kabegami_module(kabegami_cgfx_model "cgfx/model" "cgfx/model" kabegami_cgfx)
kabegami_module(kabegami_cgfx_material "cgfx/material" "cgfx/material" kabegami_cgfx)
kabegami_module(kabegami_cgfx_anim "cgfx/anim" "cgfx/anim" kabegami_cgfx)
kabegami_module(kabegami_cgfx_program "cgfx/program" "cgfx/program" kabegami_cgfx)
# the PICA200 in software: the shader unit, the fragment pipeline, and the
# GPU that runs command lists through them
kabegami_module(kabegami_pica_shader "pica/shader" "pica/shader" kabegami_base)
# its tests run HOME's own vertex program, which takes the CGFX readers
if(TARGET kabegami_pica_shader_tests)
  target_link_libraries(kabegami_pica_shader_tests PRIVATE kabegami_cgfx_model kabegami_cgfx_program)
endif()
kabegami_module(kabegami_pica_fragment "pica/fragment" "pica/fragment" kabegami_base)
kabegami_module(kabegami_pica_gpu "pica/gpu" "pica/gpu" kabegami_pica_shader kabegami_pica_fragment kabegami_image)
# the wallpaper's scene drawn as HOME draws it
kabegami_module(kabegami_scene "scene" "scene" kabegami_cgfx_model kabegami_cgfx_material kabegami_cgfx_anim kabegami_cgfx_program
  kabegami_pica_gpu kabegami_image)
# what a user changes, and recipes of it
kabegami_module(kabegami_edit "edit" "edit" kabegami_cgfx_model kabegami_cgfx_material kabegami_cgfx_anim kabegami_pica_fragment
  kabegami_image)
# pictures in and out, and putting a mod where Luma, Azahar and Zakuro read it
kabegami_module(kabegami_image "image" "image" kabegami_base)
kabegami_module(kabegami_install "install" "install" kabegami_base)

set(KABEGAMI_APP_SOURCES src/app/main.cpp src/app/cli.cpp)
if(KABEGAMI_GUI)
  find_package(OpenGL REQUIRED)
  find_package(glfw3 3.3 QUIET)
  if(NOT glfw3_FOUND)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
    FetchContent_MakeAvailable(glfw)
  endif()
  FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.92.9)
  FetchContent_MakeAvailable(imgui)
  add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
  target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
  file(GLOB_RECURSE KABEGAMI_GUI_SOURCES CONFIGURE_DEPENDS src/app/gui/*.cpp)
  list(APPEND KABEGAMI_APP_SOURCES ${KABEGAMI_GUI_SOURCES})
endif()

add_executable(kabegami ${KABEGAMI_APP_SOURCES})
target_include_directories(kabegami PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(kabegami PRIVATE kabegami_scene kabegami_edit kabegami_install kabegami_image)
kabegami_warnings(kabegami)
if(KABEGAMI_GUI)
  target_link_libraries(kabegami PRIVATE imgui)
  target_compile_definitions(kabegami PRIVATE KABEGAMI_GUI=1)
endif()
