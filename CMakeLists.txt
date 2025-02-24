cmake_minimum_required(VERSION 3.5.0)

project(player VERSION 0.1.0 LANGUAGES C CXX)   #启用C和C++的编译器。包含.c文件时需要启用C编译器

set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_PREFIX_PATH "d:/QT/6.8.1/mingw_64") # Qt Kit Dir
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)   # 启用自动MOC处理
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 COMPONENTS Widgets REQUIRED) # Qt COMPONENTS

set(HOME ${CMAKE_SOURCE_DIR})
set(EXECUTABLE_OUTPUT_PATH ${HOME}/bin)
set(FFMPEG_PATH D://FFmpeg//ffmpeg-7.0-fdk_aac)
set(SDL_PATH D://FFmpeg//SDL-release-2.30.6//build64)

#设置资源文件
set(PROJECT_RESOURCES res/res.qrc)

#包含头文件
include_directories(${HOME}/include)
include_directories(${FFMPEG_PATH}/include)
include_directories(${SDL_PATH}/include)

# 指定要链接的动态库的路径
link_directories(${FFMPEG_PATH}/bin)
link_directories(${SDL_PATH}/bin)

# GLOB_RECURSE：递归搜索指定目录
file(GLOB MAIN_SRC ${HOME}/src/*.cpp ${HOME}/src/*.c)
file(GLOB MAIN_HEAD ${HOME}/include/*.h)

message("Source files: ${MAIN_SRC}")  # 打印所有源文件路径
message("Header files: ${MAIN_HEAD}")  # 打印所有源文件路径

message("C compiler: ${CMAKE_C_COMPILER}")
message("C++ compiler: ${CMAKE_CXX_COMPILER}")


add_executable(${PROJECT_NAME} ${MAIN_SRC} ${MAIN_HEAD} ${PROJECT_RESOURCES})

# 指定要链接的动态库
target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets SDL2 avcodec-61 avdevice-61 avfilter-10 avformat-61 avutil-59 postproc-58 swresample-5 swscale-8)

