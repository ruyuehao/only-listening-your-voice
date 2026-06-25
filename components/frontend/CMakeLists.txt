idf_component_register(
    SRCS "micro_frontend.c" "feature_buffer.c" "frontend_pipeline.c"
    INCLUDE_DIRS "." "../config"
    REQUIRES esp-dsp driver freertos audio
)
