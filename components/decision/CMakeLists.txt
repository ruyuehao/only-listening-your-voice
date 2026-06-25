idf_component_register(
    SRCS "fsm.c" "led_indicator.c" "task_decision.c" "enroll.c"
    INCLUDE_DIRS "."
    REQUIRES led_strip driver freertos nvs_flash config frontend sv
)
