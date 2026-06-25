idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "." "../config"
    REQUIRES
        nvs_flash
        driver
        esp_timer
        audio
        frontend
        kws
        sv
        decision
        comm
)
