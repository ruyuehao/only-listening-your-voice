idf_component_register(
    SRCS "audio_capture.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos config
)
