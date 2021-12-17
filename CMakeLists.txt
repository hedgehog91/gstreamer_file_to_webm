cmake_minimum_required(VERSION 3.5)

project(gstreamer_example LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (UNIX AND NOT APPLE)
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
    pkg_check_modules(GSTLIBS REQUIRED gstreamer-1.0 gstreamer-plugins-base-1.0)
    set(FFMPEG_LIBS PkgConfig::LIBAV)
elseif (APPLE)
    set(GSTLIBS_INCLUDE_DIRS /Library/Frameworks/GStreamer.framework/Headers)
    set(GSTLIBS_LIBRARIES
        /Library/Frameworks/GStreamer.framework/Libraries/libgstreamer-1.0.dylib
        /Library/Frameworks/GStreamer.framework/Libraries/libgobject-2.0.dylib
        /Library/Frameworks/GStreamer.framework/Libraries/libglib-2.0.dylib
    )
    set(FFMPEG_DIR /opt/local)
    include_directories(${FFMPEG_DIR}/include)
    include_directories(${FFMPEG_DIR}/include)
    set(FFMPEG_LIBS
            ${FFMPEG_DIR}/lib/libavformat.dylib
            ${FFMPEG_DIR}/lib/libavfilter.dylib
            ${FFMPEG_DIR}/lib/libavcodec.dylib
            ${FFMPEG_DIR}/lib/libswresample.dylib
            ${FFMPEG_DIR}/lib/libswscale.dylib
            ${FFMPEG_DIR}/lib/libavutil.dylib)
endif()

include_directories(${GSTLIBS_INCLUDE_DIRS})


add_executable(gstreamer_example
  main.cpp
  StreamReader.cpp
  StreamReader.h
)
target_link_libraries(gstreamer_example ${GSTLIBS_LIBRARIES} pthread ${FFMPEG_LIBS})
