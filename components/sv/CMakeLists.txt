idf_component_register(
    SRCS "sv_engine.c" "task_sv.c"
    INCLUDE_DIRS "."
    REQUIRES esp-tflite-micro nvs_flash driver freertos config frontend
)

# sv_engine.c uses C++ (TFLite Micro C++ API: new, nullptr, templates)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/sv_engine.c" PROPERTIES LANGUAGE CXX)
