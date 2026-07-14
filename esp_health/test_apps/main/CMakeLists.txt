idf_component_register(SRCS "test_esp_health_main.c"
                            "test_esp_health_hr.c"
                            "hr_s1_data.c"
                            "test_esp_health_spo2.c"
                            "spo2_ptt_data.c"
                       INCLUDE_DIRS "."
                                    "../../examples/heart_rate_detect/main"
                       PRIV_REQUIRES unity esp_health
                       WHOLE_ARCHIVE)
