idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES nice_gfx_3d nice_gfx esp_timer freertos)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
