idf_component_register(
    SRCS "sv_engine.c" "task_sv.c"
    INCLUDE_DIRS "."
    REQUIRES esp-tflite-micro nvs_flash driver freertos config frontend
)
