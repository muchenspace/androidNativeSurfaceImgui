cmake_minimum_required(VERSION 3.18.1)

set(ANDROID_NDK "C:/Android/Sdk/ndk/27.3.13750724")# ndk路径

set(CMAKE_TOOLCHAIN_FILE "${ANDROID_NDK}/build/cmake/android.toolchain.cmake" CACHE FILEPATH "Toolchain")
set(ANDROID_ABI "arm64-v8a" CACHE STRING "Target ABI")
set(ANDROID_PLATFORM "android-29" CACHE STRING "Target API Level")
set(ANDROID_STL c++_static)

project(androidNativeSurfaceImgui CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ANDROID_SDK_ROOT "C:/Android/Sdk")# sdk路径
find_library(ANDROID_LIB android)
find_library(VULKAN_LIB vulkan)
find_library(LOG_LIB log)
find_library(DL_LIB dl)




file(GLOB_RECURSE _PLATFORM_JARS LIST_DIRECTORIES false "${ANDROID_SDK_ROOT}/platforms/android-*/android.jar")
if(_PLATFORM_JARS)
    list(GET _PLATFORM_JARS -1 ANDROID_JAR)
    message(STATUS "android.jar: ${ANDROID_JAR}")
else()
    message(FATAL_ERROR "android.jar未找到 ${ANDROID_SDK_ROOT}/platforms")
endif()#android.jar路径

file(GLOB_RECURSE _D8_CANDIDATES LIST_DIRECTORIES false
    "${ANDROID_SDK_ROOT}/build-tools/*/d8.bat"
    "${ANDROID_SDK_ROOT}/build-tools/*/d8"
)#d8路径

if(_D8_CANDIDATES)
    list(GET _D8_CANDIDATES -1 D8_EXECUTABLE)
    message(STATUS "d8: ${D8_EXECUTABLE}")
else()
    message(FATAL_ERROR "d8未找到 ${ANDROID_SDK_ROOT}/build-tools")
endif()#选取最后一个

find_program(JAVAC_EXECUTABLE javac REQUIRED)
find_program(JAR_EXECUTABLE jar REQUIRED)#jdk

set(OUTPUT_DEX "${CMAKE_CURRENT_BINARY_DIR}/classes.dex")
set(DEX_CLASSES_DIR "${CMAKE_CURRENT_BINARY_DIR}/dex_classes")
set(DEX_CLASSES_JAR "${CMAKE_CURRENT_BINARY_DIR}/dex_classes.jar")

file(GLOB_RECURSE JAVA_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/window/java/*.java"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/demo/java/*.java"
)#源码

if(CMAKE_HOST_WIN32 AND (D8_EXECUTABLE MATCHES "\\.bat$" OR D8_EXECUTABLE MATCHES "\\.cmd$"))
    set(D8_COMMAND cmd.exe /c "${D8_EXECUTABLE}")
else()
    set(D8_COMMAND "${D8_EXECUTABLE}")
endif()

add_custom_command(
    OUTPUT "${OUTPUT_DEX}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEX_CLASSES_DIR}"
    COMMAND "${JAVAC_EXECUTABLE}" -cp "${ANDROID_JAR}" -d "${DEX_CLASSES_DIR}" ${JAVA_SOURCES}
    COMMAND "${JAR_EXECUTABLE}" cf "${DEX_CLASSES_JAR}" -C "${DEX_CLASSES_DIR}" .
    COMMAND ${D8_COMMAND} --lib "${ANDROID_JAR}" --output "${CMAKE_CURRENT_BINARY_DIR}" "${DEX_CLASSES_JAR}"
    DEPENDS ${JAVA_SOURCES}
)# 编译java代码

add_custom_target(generate_classes_dex ALL DEPENDS "${OUTPUT_DEX}")#目标

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/demo/imgui" CACHE PATH "")# imgui路径

set(SRC_IMGUI_BACKEND
    src/imgui_backend/imgui_impl_android_native.cpp
    src/imgui_backend/input/evdev_input.cpp
)

set(SRC_WINDOW
    src/window/native_surface.cpp
    src/window/surface_jni.cpp
)

set(SRC_WSI
    src/wsi/vulkan_wsi.cpp
)

set(SRC_IMGUI_CORE
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)

set(SRC_DEMO
    src/demo/run.cpp
)

add_library(androidNativeSurfaceImgui SHARED
    ${SRC_IMGUI_BACKEND}
    ${SRC_WINDOW}
    ${SRC_WSI}
    ${SRC_IMGUI_CORE}
    ${SRC_DEMO}
)

add_dependencies(androidNativeSurfaceImgui generate_classes_dex)

target_include_directories(androidNativeSurfaceImgui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(androidNativeSurfaceImgui PRIVATE
    ${ANDROID_LIB}
    ${VULKAN_LIB}
    ${LOG_LIB}
    ${DL_LIB}
)

target_compile_options(androidNativeSurfaceImgui PRIVATE
    -pthread
    -Wall
    -Wextra
    -Wno-unused-parameter
)

target_link_options(androidNativeSurfaceImgui PRIVATE
    -pthread
)
