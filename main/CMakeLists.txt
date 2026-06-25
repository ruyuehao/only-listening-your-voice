idf_component_register(
    SRCS "main.c" "../config/model_loader.c"
    INCLUDE_DIRS "." "../config"
    REQUIRES
        nvs_flash
        driver
        esp_timer
        fatfs
        audio
        frontend
        kws
        sv
        decision
        comm
)
