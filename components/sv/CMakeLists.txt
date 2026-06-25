idf_component_register(
    SRCS "sv_engine.c" "task_sv.c"
    INCLUDE_DIRS "." "../frontend" "../config"
    REQUIRES esp-tflite-micro nvs_flash driver freertos esp_timer
)
