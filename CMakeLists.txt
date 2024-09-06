cmake_minimum_required(VERSION 3.1...3.29)

project(
  AlvrRender
  VERSION 1.0
  LANGUAGES CXX)

# TODO: Do the intended cmake way
add_compile_options(-fdiagnostics-color=always)
add_compile_options(-std=c++20)

set(PROJECT_SOURCE_DIR src/)

set(SRC_FILES
	      ${PROJECT_SOURCE_DIR}/EncodePipeline.cpp
	      # ${PROJECT_SOURCE_DIR}/EncodePipelineNvEnc.cpp
	      # ${PROJECT_SOURCE_DIR}/EncodePipelineSW.cpp
	      ${PROJECT_SOURCE_DIR}/EncodePipelineVAAPI.cpp
	      ${PROJECT_SOURCE_DIR}/ffmpeg_helper.cpp
	      # ${PROJECT_SOURCE_DIR}/FormatConverter.cpp
	      # ${PROJECT_SOURCE_DIR}/FrameRender.cpp
	      # ${PROJECT_SOURCE_DIR}/Renderer.cpp
	      ${PROJECT_SOURCE_DIR}/alvr_server/IDRScheduler.cpp
	      ${PROJECT_SOURCE_DIR}/alvr_server/Logger.cpp
	      ${PROJECT_SOURCE_DIR}/alvr_server/Settings.cpp
	      ${PROJECT_SOURCE_DIR}/alvr_server/NalParsing.cpp

              ${PROJECT_SOURCE_DIR}/Renderer.cpp
              ${PROJECT_SOURCE_DIR}/Encoder.cpp
              )

include_directories(${PROJECT_SOURCE_DIR})

add_library(AlvrRender ${SRC_FILES})
set_target_properties(AlvrRender PROPERTIES PUBLIC_HEADER src/monado_interface.h)
set_target_properties(AlvrRender PROPERTIES PUBLIC_HEADER src/EventManager.hpp)
set_target_properties(AlvrRender PROPERTIES PUBLIC_HEADER src/Encoder.hpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavdevice
    libavfilter
    libavformat
    libavcodec
    libswresample
    libswscale
    libavutil
)

find_package(Vulkan REQUIRED)

add_library(libalvr STATIC IMPORTED)
set_target_properties(libalvr PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../alvr-monado/build/alvr_server_core/libalvr_server_core.so)

target_link_libraries(AlvrRender PRIVATE PkgConfig::LIBAV libalvr ${VULKAN_LIB_LIST})
