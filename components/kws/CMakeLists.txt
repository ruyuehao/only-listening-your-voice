idf_component_register(
    SRCS "kws_engine.c" "task_kws.c"
    INCLUDE_DIRS "."
    REQUIRES esp-tflite-micro driver freertos config frontend
)
