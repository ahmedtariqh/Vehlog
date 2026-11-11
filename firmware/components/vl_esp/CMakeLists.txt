idf_component_register(SRCS "vl_twai.c" INCLUDE_DIRS "include"
                       REQUIRES vl_core
                       PRIV_REQUIRES esp_driver_gpio esp_driver_twai esp_hal_twai esp_timer vl_freertos_can)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu11)
