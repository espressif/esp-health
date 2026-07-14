cmake_minimum_required(VERSION 3.16)

idf_component_register(INCLUDE_DIRS "include"
                       REQUIRES log)

set(_esp_health_lib "${CMAKE_CURRENT_SOURCE_DIR}/lib/${CONFIG_IDF_TARGET}/libesp_health.a")
if(NOT EXISTS "${_esp_health_lib}")
    message(FATAL_ERROR "Prebuilt lib not found for ${CONFIG_IDF_TARGET}: ${_esp_health_lib}")
endif()

add_prebuilt_library(esp_health_prebuilt "${_esp_health_lib}")
target_link_libraries(${COMPONENT_LIB} INTERFACE esp_health_prebuilt)
