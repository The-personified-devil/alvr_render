cmake_minimum_required(VERSION 3.1...3.29)

project(
  AlvrRender
  VERSION 1.0
  LANGUAGES CXX)

add_compile_options(-fdiagnostics-color=always)

set(PROJECT_SOURCE_DIR src/)

set(SRC_FILES
	      ${PROJECT_SOURCE_DIR}/EncodePipeline.cpp
	      # ${PROJECT_SOURCE_DIR}/EncodePipelineNvEnc.cpp
	      # ${PROJECT_SOURCE_DIR}/EncodePipelineSW.cpp
	      ${PROJECT_SOURCE_DIR}/EncodePipelineVAAPI.cpp
	      ${PROJECT_SOURCE_DIR}/ffmpeg_helper.cpp
	      # ${PROJECT_SOURCE_DIR}/FormatConverter.cpp
	      # ${PROJECT_SOURCE_DIR}/FrameRender.cpp
	      # ${PROJECT_SOURCE_DIR}/MEncoder.cpp
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

add_library(libalvr STATIC IMPORTED)
set_target_properties(libalvr PROPERTIES IMPORTED_LOCATION /home/duck/devel/alvr_intact/target/debug/libalvr_server.so)
set_property(TARGET libalvr PROPERTY CXX_STANDARD 20)

# Make sure you link your targets with this command. It can also link libraries and
# even flags, so linking a target that does not exist will not give a configure-time error.
target_link_libraries(AlvrRender PRIVATE PkgConfig::LIBAV libalvr)
