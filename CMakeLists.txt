# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# main/ 已经是主组件；把 identify 组件目录单独注册为额外组件。
set(EXTRA_COMPONENT_DIRS "main/identify")

# 强制禁用 ccache。原因：VSCode 插件会以命令行 -DCCACHE_ENABLE=1 注入，
# 而命令行参数优先级高于普通 set()，所以必须用 CACHE FORCE 覆盖。
# （本机 ccache.exe 被 Windows 拒绝执行：ninja "CreateProcess: Access is denied"）
set(CCACHE_ENABLE 0 CACHE BOOL "force disable ccache" FORCE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ble_antitrack)
