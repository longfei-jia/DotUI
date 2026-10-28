cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# 工具链：自动查找 CLion 自带 MinGW（不依赖 UCRT64）
# CLion 升级后目录名会变化，路径失效时自动找最新的 CLion*/bin/mingw
# ---------------------------------------------------------------------------
set(CLION_MINGW_ROOT "E:/Program Files/JetBrains/CLion 2026.2.4/bin/mingw"
    CACHE PATH "CLion 自带 MinGW 根目录")

if(NOT EXISTS "${CLION_MINGW_ROOT}/bin/gcc.exe")
    file(GLOB _clion_mingw_roots "E:/Program Files/JetBrains/CLion*/bin/mingw")
    if(_clion_mingw_roots)
        list(SORT _clion_mingw_roots)
        list(GET _clion_mingw_roots -1 _clion_mingw_latest)
        set(CLION_MINGW_ROOT "${_clion_mingw_latest}"
            CACHE PATH "CLion 自带 MinGW 根目录" FORCE)
    endif()
endif()

if(NOT EXISTS "${CLION_MINGW_ROOT}/bin/gcc.exe")
    message(FATAL_ERROR "未找到 CLion 自带的 MinGW：${CLION_MINGW_ROOT}")
endif()

# 缓存里的编译器路径失效时（例如 CLion 升级），自动切到新路径，
# 同时清掉旧的 ar/ranlib 缓存，避免链接时仍调用已删除的旧工具
if(NOT DEFINED CMAKE_C_COMPILER OR NOT EXISTS "${CMAKE_C_COMPILER}")
    foreach(_tool_var
            CMAKE_C_COMPILER_AR CMAKE_C_COMPILER_RANLIB
            CMAKE_CXX_COMPILER_AR CMAKE_CXX_COMPILER_RANLIB)
        unset(${_tool_var} CACHE)
    endforeach()

    set(CMAKE_C_COMPILER "${CLION_MINGW_ROOT}/bin/gcc.exe" CACHE FILEPATH "C 编译器" FORCE)
    set(CMAKE_CXX_COMPILER "${CLION_MINGW_ROOT}/bin/g++.exe" CACHE FILEPATH "C++ 编译器" FORCE)
endif()

project(DotUI VERSION 0.1.0 LANGUAGES C CXX)

# Ninja 是单配置生成器，没指定构建类型时默认用 Debug，便于断点调试
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "构建类型（默认 Debug）" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# u8g2 核心 + SDL 后端
# ---------------------------------------------------------------------------
set(U8G2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/u8g2")

if(NOT EXISTS "${U8G2_DIR}/csrc/u8g2.h")
    message(FATAL_ERROR "未找到 u8g2：${U8G2_DIR}/csrc/u8g2.h")
endif()

file(GLOB U8G2_CORE_SRC CONFIGURE_DEPENDS "${U8G2_DIR}/csrc/*.c")

add_library(u8g2 STATIC
    ${U8G2_CORE_SRC}
    "${U8G2_DIR}/sys/sdl/common/u8x8_d_sdl_128x64.c"
    "${U8G2_DIR}/sys/sdl/common/u8x8_sdl_key.c"
)

target_include_directories(u8g2 PUBLIC "${U8G2_DIR}/csrc")

# ---------------------------------------------------------------------------
# SDL2：使用 VC 版 SDL2（不依赖 UCRT64 的 SDL2）
# ---------------------------------------------------------------------------
set(SDL2_DIR "E:/Software/InstallPackage/SDL2-devel-2.32.10-VC/SDL2-2.32.10/cmake"
    CACHE PATH "VC 版 SDL2 的 CMake 配置目录")

find_package(SDL2 CONFIG REQUIRED)

# 只链接 SDL2 运行库；不链接 SDL2main（MSVC 静态库，MinGW 下改用 SDL_MAIN_HANDLED）
target_link_libraries(u8g2 PUBLIC SDL2::SDL2)

# ---------------------------------------------------------------------------
# 模拟器可执行文件
# ---------------------------------------------------------------------------
add_executable(dotui_sim src/main.cpp)
target_link_libraries(dotui_sim PRIVATE u8g2)

# ---------------------------------------------------------------------------
# 把运行期 DLL 复制到 exe 旁边，双击即可运行
# ---------------------------------------------------------------------------
if(MINGW)
    set(DOTUI_RUNTIME_DLLS "$<TARGET_FILE:SDL2::SDL2>")

    get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_dll libwinpthread-1.dll libstdc++-6.dll libgcc_s_seh-1.dll)
        if(EXISTS "${MINGW_BIN}/${_dll}")
            list(APPEND DOTUI_RUNTIME_DLLS "${MINGW_BIN}/${_dll}")
        endif()
    endforeach()

    add_custom_command(TARGET dotui_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${DOTUI_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:dotui_sim>"
        COMMENT "复制运行期 DLL 到模拟器目录"
    )
endif()
