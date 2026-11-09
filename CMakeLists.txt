cmake_minimum_required(VERSION 3.21)

# 设置MinGW路径
set(CLION_MINGW_ROOT "E:/Program Files/JetBrains/CLion 2026.2.4/bin/mingw"
    CACHE PATH "CLion 自带 MinGW 路径")

if(NOT EXISTS "${CLION_MINGW_ROOT}/bin/gcc.exe")
    file(GLOB _roots "E:/Program Files/JetBrains/CLion*/bin/mingw")
    list(SORT _roots)
    list(GET _roots -1 _latest)
    set(CLION_MINGW_ROOT "$ {_latest}" CACHE PATH "" FORCE)
endif()

set(CMAKE_C_COMPILER "${CLION_MINGW_ROOT}/bin/gcc.exe" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${CLION_MINGW_ROOT}/bin/g++.exe" CACHE FILEPATH "" FORCE)

project(DotUI VERSION 0.1.0 LANGUAGES CXX C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 包含u8g2库
set(U8G2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/u8g2")
file(GLOB U8G2_CORE_SRC CONFIGURE_DEPENDS "${U8G2_DIR}/csrc/*.c")
add_library(u8g2 STATIC
    ${U8G2_CORE_SRC}
    "${U8G2_DIR}/sys/sdl/common/u8x8_d_sdl_128x64.c"     # SDL 显示驱动
    "${U8G2_DIR}/sys/sdl/common/u8x8_sdl_key.c"          # SDL 按键
    "${U8G2_DIR}/tools/font/build/single_font_files/u8g2_font_wqy12_t_gb2312.c")
set_source_files_properties(
    "${U8G2_DIR}/tools/font/build/single_font_files/u8g2_font_wqy12_t_gb2312.c"
    PROPERTIES COMPILE_DEFINITIONS "U8G2_USE_LARGE_FONTS"
               COMPILE_OPTIONS "-include;${U8G2_DIR}/csrc/u8g2.h")
target_include_directories(u8g2 PUBLIC "${U8G2_DIR}/csrc")

set(SDL2_DIR "E:/Software/InstallPackage/SDL2-devel-2.32.10-VC/SDL2-2.32.10/cmake"
    CACHE PATH "VC 版 SDL2 的 CMake 配置目录")
find_package(SDL2 CONFIG REQUIRED)
target_link_libraries(u8g2 PUBLIC SDL2::SDL2)

# add_library(dot_ui STATIC
#     src/dot_ui.cpp
#     src/dot_page.cpp
#     src/dot_item.cpp
#     src/dot_input.cpp
#     src/resources.cpp
#     src/dot_user_app.cpp)
# target_include_directories(dot_ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/inc")
# target_link_libraries(dot_ui PUBLIC u8g2)

add_executable(dotui_sim
                src/main.cpp
                src/dot_input.cpp
                src/resources.cpp
                src/dot_page.cpp
                src/dot_item.cpp
                src/dot_ui.cpp
                src/dot_user_app.cpp)
target_link_libraries(dotui_sim PRIVATE u8g2)

include_directories(inc)

if(MINGW)
    set(DLLS "$<TARGET_FILE:SDL2::SDL2>")
    get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(d libwinpthread-1.dll libstdc++-6.dll libgcc_s_seh-1.dll)
        if(EXISTS "${MINGW_BIN}/${d}")
            list(APPEND DLLS "${MINGW_BIN}/${d}")
        endif()
    endforeach()
    add_custom_command(TARGET dotui_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DLLS}
                "$<TARGET_FILE_DIR:dotui_sim>")
endif()