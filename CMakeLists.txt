cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "C:/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "C:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

project(AlignImages VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ALIGN_IMAGES_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

if(EXISTS "C:/vcpkg/installed/x64-windows")
    list(APPEND CMAKE_PREFIX_PATH "C:/vcpkg/installed/x64-windows")
endif()

if(NOT DEFINED OpenCV_DIR AND EXISTS "C:/vcpkg/installed/x64-windows/share/opencv4")
    set(OpenCV_DIR "C:/vcpkg/installed/x64-windows/share/opencv4" CACHE PATH "OpenCV package config path")
endif()

set(ALIGN_IMAGES_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/core/DpiUtilsWin.cpp
    src/core/WinDialogUtils.cpp
    src/core/Transform2DMath.cpp
    src/data/SessionModel.cpp
    src/data/PairingModel.cpp
    src/data/RegistrationResult.cpp
    src/io/DirectoryScanner.cpp
    src/io/ImageLoader.cpp
    src/io/DicomLoader.cpp
    src/io/DicomPixelCache.cpp
    src/io/DicomSeriesStitcher.cpp
    src/io/SessionSerializer.cpp
    src/export/ExportController.cpp
    src/registration/ConvergenceAnalyzer.cpp
    src/registration/RegistrationEngine.cpp
    src/registration/LandmarkRegistration.cpp
    src/registration/TransformInterpolator.cpp
    src/timeline/TimelinePanel.cpp
    src/ui/MainWindow.cpp
    src/viewer/ImageTexture.cpp
    src/viewer/ViewerPanel.cpp
)

add_executable(AlignImages WIN32 ${ALIGN_IMAGES_SOURCES})

target_include_directories(AlignImages
    PRIVATE
        src
)

if(MSVC)
    target_compile_options(AlignImages PRIVATE /W4 /permissive- /EHsc)
    if(ALIGN_IMAGES_WARNINGS_AS_ERRORS)
        target_compile_options(AlignImages PRIVATE /WX)
    endif()
else()
    target_compile_options(AlignImages PRIVATE -Wall -Wextra -Wpedantic)
    if(ALIGN_IMAGES_WARNINGS_AS_ERRORS)
        target_compile_options(AlignImages PRIVATE -Werror)
    endif()
endif()

find_package(OpenGL REQUIRED)
find_package(OpenCV REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(GDCM REQUIRED)
include(${GDCM_USE_FILE})

find_package(nlohmann_json CONFIG QUIET)

target_link_libraries(AlignImages
    PRIVATE
        OpenGL::GL
        glfw
        Shcore
        Ole32
        Shell32
        Comdlg32
        ${OpenCV_LIBS}
        gdcmMSFF
)

if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(AlignImages PRIVATE nlohmann_json::nlohmann_json)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann/json.hpp")
    target_include_directories(AlignImages PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
else()
    message(FATAL_ERROR "nlohmann/json not found. Install the package or place json.hpp under third_party/nlohmann.")
endif()

target_include_directories(AlignImages PRIVATE ${OpenCV_INCLUDE_DIRS} ${GDCM_INCLUDE_DIRS})

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR "Dear ImGui not found. Expected vendor checkout at third_party/imgui.")
endif()

target_sources(AlignImages
    PRIVATE
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(AlignImages
    PRIVATE
        ${IMGUI_DIR}
        ${IMGUI_DIR}/backends
)

target_compile_definitions(AlignImages
    PRIVATE
        GLFW_INCLUDE_NONE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
)
