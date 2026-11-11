cmake_minimum_required(VERSION 3.21)

# DotUI 构建分发器（参考 smooth_ui_toolkit 模式）
# - ESP-IDF 以组件方式构建时自动定义 ESP_PLATFORM → 走 idf 组件注册
# - 其余环境（独立构建 / add_subdirectory / FetchContent）→ 走标准 CMake 库构建
set(DOTUI_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})

if(ESP_PLATFORM)
    include(${DOTUI_ROOT_DIR}/cmake/esp_idf.cmake)
else()
    include(${DOTUI_ROOT_DIR}/cmake/basic.cmake)
endif()
