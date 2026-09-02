idf_component_register(SRCS "ppg_spo2.c"
                       INCLUDE_DIRS "."
                       REQUIRES driver esp_driver_i2c esp_driver_gpio
                                esp_board_manager gen_bmgr_codes esp_health
                       WHOLE_ARCHIVE)
