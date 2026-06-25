idf_component_register(
    SRCS "fsm.c" "led_indicator.c" "task_decision.c" "enroll.c"
    INCLUDE_DIRS "." "../sv" "../frontend"
    REQUIRES led_strip driver freertos esp_timer nvs_flash
)
