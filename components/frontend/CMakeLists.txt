idf_component_register(
    SRCS "micro_frontend.c" "feature_buffer.c" "frontend_pipeline.c"
    INCLUDE_DIRS "."
    REQUIRES esp-dsp driver freertos audio config
)
