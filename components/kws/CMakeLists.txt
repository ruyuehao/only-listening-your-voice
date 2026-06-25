idf_component_register(
    SRCS "kws_engine.c" "task_kws.c"
    INCLUDE_DIRS "." "../frontend"
    REQUIRES esp-tflite-micro driver freertos esp_timer
)
