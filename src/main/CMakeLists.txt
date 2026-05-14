# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

#FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

# 【修复】简化路径：CMake在main目录下，直接写相对路径
# 【修复】删除多余引号，严格遵循ESP-IDF语法
FILE(GLOB_RECURSE app_sources 
        ${CMAKE_SOURCE_DIR}/src/main/source/*.c
)

idf_component_register(
SRCS  ${app_sources} # 批量加载source下所有C文件，不用手动写
#SRCS "source/main.c" "source/disp_driver.c" "source/touch_driver.c" "source/touch_driver.c"# 要编译的源代码文件
# 头文件搜索路径： include 目录，以及 LVGL 根目录（为了找到 lv_conf.h）
INCLUDE_DIRS "include"  
REQUIRES lvgl freertos  driver  esp_netif lwip # 依赖正确
)
