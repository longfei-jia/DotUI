# cmake/basic.cmake —— 标准 CMake 构建（独立构建 / add_subdirectory / FetchContent 通用）
#
# 用法：
#   独立开发模拟器：cmake -B build -DDOTUI_BUILD_EXAMPLE=ON
#   消费方引入：    add_subdirectory(DotUI) 即可，u8g2 缺失时自动拉取到 DotUI/lib/u8g2

# =====================================================================
# Windows 本地开发工具链：可选使用 CLion 自带 MinGW（编译器须在 project() 前指定）
# 默认关闭，不影响消费方；本地开发通过 preset 或 -DDOTUI_WINDOWS_USE_CLION_MINGW=ON 开启
# =====================================================================
if(WIN32)
    option(DOTUI_WINDOWS_USE_CLION_MINGW
        "Use the MinGW toolchain bundled with CLion (Windows local dev only)" OFF)
    if(DOTUI_WINDOWS_USE_CLION_MINGW)
        set(CLION_MINGW_ROOT "E:/Program Files/JetBrains/CLion 2026.2.4/bin/mingw"
            CACHE PATH "CLion 自带 MinGW 路径")
        if(NOT EXISTS "${CLION_MINGW_ROOT}/bin/gcc.exe")
            # 版本号变化时自动探测最新 CLion 安装目录
            file(GLOB _dotui_clion_roots "E:/Program Files/JetBrains/CLion*/bin/mingw")
            list(SORT _dotui_clion_roots)
            list(GET _dotui_clion_roots -1 _dotui_clion_latest)
            set(CLION_MINGW_ROOT "${_dotui_clion_latest}" CACHE PATH "" FORCE)
        endif()
        set(CMAKE_C_COMPILER   "${CLION_MINGW_ROOT}/bin/gcc.exe" CACHE FILEPATH "" FORCE)
        set(CMAKE_CXX_COMPILER "${CLION_MINGW_ROOT}/bin/g++.exe" CACHE FILEPATH "" FORCE)
    endif()
endif()

project(DotUI VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =====================================================================
# u8g2 依赖：缺失时自动拉取（clone 锁定版本 + 应用 SDL 模拟器补丁）
# 消费方若已提供 u8g2 目标（自行选好屏幕驱动），此段自动跳过
# =====================================================================
set(DOTUI_U8G2_DIR ${DOTUI_ROOT_DIR}/lib/u8g2)
# 锁定 vendored 时的上游精确 commit，保证补丁可干净应用；消费方可覆盖为其他 tag
set(DOTUI_U8G2_TAG "d6c8499c5f2707cac8eccd09fd8f677d12b17977"
    CACHE STRING "u8g2 上游锁定版本（commit 或 tag）")

if(NOT TARGET u8g2)
    if(NOT EXISTS "${DOTUI_U8G2_DIR}/csrc/u8g2.h")
        find_package(Git REQUIRED)
        message(STATUS "DotUI: 拉取 u8g2 (${DOTUI_U8G2_TAG}) ...")
        # git clone --branch 不支持 commit hash，故用 init + fetch origin <sha> 浅拉取
        execute_process(
            COMMAND ${GIT_EXECUTABLE} init ${DOTUI_U8G2_DIR}
            COMMAND ${GIT_EXECUTABLE} -C ${DOTUI_U8G2_DIR} remote add origin
                    https://github.com/olikraus/u8g2.git
            COMMAND ${GIT_EXECUTABLE} -C ${DOTUI_U8G2_DIR} fetch --depth 1
                    origin ${DOTUI_U8G2_TAG}
            COMMAND ${GIT_EXECUTABLE} -C ${DOTUI_U8G2_DIR}
                    checkout --quiet FETCH_HEAD
            RESULT_VARIABLE _dotui_git_result)
        if(NOT _dotui_git_result EQUAL 0)
            message(FATAL_ERROR
                "DotUI: u8g2 拉取失败，请检查网络，或手动执行: "
                "git clone https://github.com/olikraus/u8g2.git ${DOTUI_U8G2_DIR} "
                "&& git -C ${DOTUI_U8G2_DIR} checkout ${DOTUI_U8G2_TAG}")
        endif()
        # 应用 SDL 后端补丁（仅影响 example 模拟器，核心库不受影响）
        execute_process(
            COMMAND ${GIT_EXECUTABLE} -C ${DOTUI_U8G2_DIR}
                    apply ${DOTUI_ROOT_DIR}/patches/u8g2-sdl-local.patch
            RESULT_VARIABLE _dotui_patch_result)
        if(NOT _dotui_patch_result EQUAL 0)
            message(WARNING "DotUI: u8g2 SDL 补丁应用失败，模拟器功能可能不可用")
        endif()
    endif()

    # 用拉取的源码构建 u8g2 库（仅 csrc 核心，SDL 后端由 example 追加）
    file(GLOB DOTUI_U8G2_SRC CONFIGURE_DEPENDS "${DOTUI_U8G2_DIR}/csrc/*.c")
    add_library(u8g2 STATIC ${DOTUI_U8G2_SRC})
    target_include_directories(u8g2 PUBLIC "${DOTUI_U8G2_DIR}/csrc")
endif()

# =====================================================================
# DotUI 核心库（平台无关）
# =====================================================================
add_library(dot_ui STATIC
    ${DOTUI_ROOT_DIR}/src/dot_ui.cpp
    ${DOTUI_ROOT_DIR}/src/dot_page.cpp
    ${DOTUI_ROOT_DIR}/src/dot_item.cpp
    ${DOTUI_ROOT_DIR}/src/dot_input.cpp
    ${DOTUI_ROOT_DIR}/src/resources.cpp)
target_include_directories(dot_ui PUBLIC ${DOTUI_ROOT_DIR}/inc)
target_compile_features(dot_ui PUBLIC cxx_std_17)
target_link_libraries(dot_ui PUBLIC u8g2)

# =====================================================================
# SDL 模拟器示例（默认关闭，仅本机开发时开启）
# =====================================================================
option(DOTUI_BUILD_EXAMPLE "Build the SDL simulator example" OFF)
if(DOTUI_BUILD_EXAMPLE)
    add_subdirectory(${DOTUI_ROOT_DIR}/example/dotui_sim)
endif()
