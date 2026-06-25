idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        driver
        esp_timer
        fatfs
        config
        audio
        frontend
        kws
        sv
        decision
        comm
)
