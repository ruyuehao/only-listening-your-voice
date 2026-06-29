idf_component_register(
    SRCS "kws_engine.c" "task_kws.c"
    INCLUDE_DIRS "."
    REQUIRES esp-tflite-micro driver freertos config frontend
)

# kws_engine.c uses C++ (TFLite Micro C++ API: new, nullptr, templates)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/kws_engine.c" PROPERTIES LANGUAGE CXX)
