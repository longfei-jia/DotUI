# example/dotui_sim —— SDL 模拟器示例（DotUI 的使用示例，非库本体）
# 仅在 DOTUI_BUILD_EXAMPLE=ON 时由 cmake/basic.cmake 引入

# VC 版 SDL2 的 CMake 配置目录（按本机安装路径调整）
set(SDL2_DIR "E:/Software/InstallPackage/SDL2-devel-2.32.10-VC/SDL2-2.32.10/cmake"
    CACHE PATH "VC 版 SDL2 的 CMake 配置目录")
find_package(SDL2 CONFIG REQUIRED)

# ---- 模拟器专属的 u8g2 扩展：SDL 显示后端（追加进 u8g2 目标）----
set(DOTUI_U8G2_DIR ${DOTUI_ROOT_DIR}/lib/u8g2)
target_sources(u8g2 PRIVATE
    "${DOTUI_U8G2_DIR}/sys/sdl/common/u8x8_d_sdl_128x64.c"      # SDL 显示驱动
    "${DOTUI_U8G2_DIR}/sys/sdl/common/u8x8_sdl_key.c")          # SDL 按键
target_link_libraries(u8g2 PUBLIC SDL2::SDL2)

# ---- 中文字体单独成库：源文件属性是目录作用域，u8g2 目标在根目录创建，
#      必须在本目录内建独立目标才能让 COMPILE_DEFINITIONS/-include 生效 ----
add_library(dotui_font STATIC
    "${DOTUI_U8G2_DIR}/tools/font/build/single_font_files/u8g2_font_wqy12_t_gb2312.c")
set_source_files_properties(
    "${DOTUI_U8G2_DIR}/tools/font/build/single_font_files/u8g2_font_wqy12_t_gb2312.c"
    PROPERTIES COMPILE_DEFINITIONS "U8G2_USE_LARGE_FONTS"
               COMPILE_OPTIONS "-include;${DOTUI_U8G2_DIR}/csrc/u8g2.h")
target_include_directories(dotui_font PRIVATE "${DOTUI_U8G2_DIR}/csrc")

# ---- 模拟器可执行文件：链接 dot_ui 库（库/示例边界）----
add_executable(dotui_sim
    main.cpp
    dot_user_app.cpp)
target_link_libraries(dotui_sim PRIVATE dot_ui dotui_font)

# ---- 运行时 DLL 自动复制（保证 build 目录可独立双击运行）----
if(MINGW)
    set(DOTUI_DLLS "$<TARGET_FILE:SDL2::SDL2>")
    get_filename_component(DOTUI_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(d libwinpthread-1.dll libstdc++-6.dll libgcc_s_seh-1.dll)
        if(EXISTS "${DOTUI_MINGW_BIN}/${d}")
            list(APPEND DOTUI_DLLS "${DOTUI_MINGW_BIN}/${d}")
        endif()
    endforeach()
    add_custom_command(TARGET dotui_sim POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DOTUI_DLLS}
                "$<TARGET_FILE_DIR:dotui_sim>")
endif()
