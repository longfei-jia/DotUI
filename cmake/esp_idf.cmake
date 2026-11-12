# cmake/esp_idf.cmake —— ESP-IDF 组件注册
# 根 CMakeLists 检测到 ESP_PLATFORM 后进入本文件
#
# 开箱即用：u8g2 源码已随仓库 vendored（lib/u8g2，上游锁定版本），
# 默认直接编进本组件，消费方 clone 到 components/ 后无需任何外部依赖。
# 若消费方已有自己的 u8g2 组件，在构建前设置：
#   set(DOTUI_IDF_EXTERNAL_U8G2 ON)   # 跳过内置 u8g2，改 REQUIRES u8g2
#   set(DOTUI_IDF_REQUIRES my_u8g2)   # 可选：自定义依赖组件名

# 组件源文件（与 basic.cmake 保持一致）
# 注意：idf_component_register 的相对路径基于 CMAKE_CURRENT_LIST_DIR（即 cmake/）
# 解析，必须使用绝对路径变量 DOTUI_ROOT_DIR 指向组件根
set(DOTUI_SRCS
    "${DOTUI_ROOT_DIR}/src/dot_ui.cpp"
    "${DOTUI_ROOT_DIR}/src/dot_page.cpp"
    "${DOTUI_ROOT_DIR}/src/dot_item.cpp"
    "${DOTUI_ROOT_DIR}/src/dot_input.cpp"
    "${DOTUI_ROOT_DIR}/src/resources.cpp")
set(DOTUI_INCS "${DOTUI_ROOT_DIR}/inc")

if(DOTUI_IDF_EXTERNAL_U8G2)
    # 外部 u8g2 模式：头文件与库均由消费方组件提供
    if(NOT DOTUI_IDF_REQUIRES)
        set(DOTUI_IDF_REQUIRES u8g2)
    endif()
else()
    # 内置模式：编译 vendored u8g2 全量 csrc（核心 + 屏驱 + 字体，上游标准做法）
    file(GLOB DOTUI_U8G2_SRCS "${DOTUI_ROOT_DIR}/lib/u8g2/csrc/*.c")
    list(APPEND DOTUI_SRCS ${DOTUI_U8G2_SRCS})
    list(APPEND DOTUI_INCS "${DOTUI_ROOT_DIR}/lib/u8g2/csrc")
endif()

idf_component_register(
    SRCS ${DOTUI_SRCS}
    INCLUDE_DIRS ${DOTUI_INCS}
    REQUIRES ${DOTUI_IDF_REQUIRES})

# 组件名固定为 dot_ui（目录名），消费方 REQUIRES dot_ui
