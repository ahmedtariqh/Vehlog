idf_component_register(SRCS "vl_main.c" INCLUDE_DIRS "." PRIV_REQUIRES vl_esp vl_text vl_freertos_can)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu11)
