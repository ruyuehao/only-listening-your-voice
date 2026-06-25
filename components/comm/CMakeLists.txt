idf_component_register(
    SRCS "event_reporter.c"
    INCLUDE_DIRS "."
    REQUIRES driver freertos esp_timer config
)
