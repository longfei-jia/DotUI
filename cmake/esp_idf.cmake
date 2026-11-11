# cmake/esp_idf.cmake —— ESP-IDF 组件注册
# 根 CMakeLists 检测到 ESP_PLATFORM 后进入本文件
# u8g2 组件由消费方通过 idf_component.yml（espressif/u8g2）或手动移植提供，
# u8g2 头文件路径由组件系统自动传递，此处不硬编码依赖组件名

# 组件源文件（与 basic.cmake 保持一致）
set(DOTUI_SRCS
    "src/dot_ui.cpp"
    "src/dot_page.cpp"
    "src/dot_item.cpp"
    "src/dot_input.cpp"
    "src/resources.cpp")

# 消费方可提前 set(DOTUI_IDF_REQUIRES "u8g2") 覆盖依赖组件名
if(NOT DEFINED DOTUI_IDF_REQUIRES)
    set(DOTUI_IDF_REQUIRES "")   # 默认留空：头文件路径由消费方组件传递
endif()

idf_component_register(
    SRCS ${DOTUI_SRCS}
    INCLUDE_DIRS "inc"
    REQUIRES ${DOTUI_IDF_REQUIRES})

# 组件名固定为 dot_ui（目录名），消费方 REQUIRES dot_ui
