# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(PatternBook VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    # Static CRT so the DLL has no runtime dependencies on the target machine.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(FFGL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/ffgl)
if(NOT EXISTS ${FFGL_DIR}/source/lib/FFGLSDK.cpp)
    message(FATAL_ERROR "FFGL SDK not found. Run: git submodule update --init --recursive")
endif()

# ---------------------------------------------------------------------------
# The FFGL plugin: .bundle on macOS, .dll on Windows.
# ---------------------------------------------------------------------------
add_library(PatternBook MODULE
    src/PatternBook.h
    src/PatternBook.cpp
    
    src/Shaders.h
    ${FFGL_DIR}/source/lib/FFGLSDK.cpp
)
target_include_directories(PatternBook PRIVATE src ${FFGL_DIR}/source/lib)
# The plugin reports the project version to the host.
target_compile_definitions(PatternBook PRIVATE
    PATTERNBOOK_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    PATTERNBOOK_VERSION_MINOR=${PROJECT_VERSION_MINOR})
# FFGL_DEBUG makes the SDK assert that the plugin leaves the GL context in its default state.
target_compile_definitions(PatternBook PRIVATE $<$<CONFIG:Debug>:DEBUG>)

if(APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(PatternBook PRIVATE ${OPENGL_FRAMEWORK})
    target_compile_definitions(PatternBook PRIVATE GL_SILENCE_DEPRECATION)
    set_target_properties(PatternBook PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION bundle
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER com.patternbook.ffgl
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    )
elseif(WIN32)
    set(GLEW_DIR ${FFGL_DIR}/deps/glew-2.1.0)
    target_include_directories(PatternBook PRIVATE ${GLEW_DIR}/include)
    target_compile_definitions(PatternBook PRIVATE GLEW_STATIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
    target_link_libraries(PatternBook PRIVATE ${GLEW_DIR}/lib/Release/x64/glew32s.lib opengl32)
    set_target_properties(PatternBook PROPERTIES PREFIX "")
endif()

# ---------------------------------------------------------------------------
# Offscreen test harness (macOS only for now): loads the built plugin like a
# host would, renders frames and writes PNGs.
# ---------------------------------------------------------------------------
option(PATTERNBOOK_BUILD_HARNESS "Build the offscreen test harness" ${APPLE})
if(PATTERNBOOK_BUILD_HARNESS AND APPLE)
    enable_language(OBJCXX)
    add_executable(patternbook-harness tools/harness/main.mm)
    target_include_directories(patternbook-harness PRIVATE ${FFGL_DIR}/source/lib)
    target_compile_definitions(patternbook-harness PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(patternbook-harness PRIVATE
        "-framework OpenGL" "-framework CoreGraphics" "-framework ImageIO" "-framework CoreServices" "-framework Foundation")
    set_source_files_properties(tools/harness/main.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    add_dependencies(patternbook-harness PatternBook)
endif()
