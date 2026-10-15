# main 组件：扫描 + 输出；依赖 identify 子组件
# 注意：显式写 REQUIRES 会覆盖默认的公共依赖，因此 esp_timer / nvs_flash / bt
# 必须自己列出（否则报 "esp_timer.h: No such file or directory"）。
idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES identify esp_timer nvs_flash bt)
